#!/usr/bin/env python3
"""Finite stdin/JSONL adapter. Run in a host worker, never on a Chat/UI thread."""
import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import stat
import sys
import types
import uuid

HERE = Path(__file__).resolve().parent
REQUEST = "loom.face_preview.request/1"
RESULT = "loom.face_preview.result/1"
RECEIPT = "loom.face_preview.receipt/1"
PINS = {
    "renderer.py": "cf688d4b13eee19e28abbf3083161ad02edf64c378059d8a9a6403a3141dac23",
    "async_controller.py": "f0b5b75e82dd966888b0a48b2da606637edc7b702308007f14cdeb0bc0c0cb28",
}
FILES = {"input.jsonl", "config.json", "preview.png", "receipt.json"}
COMMON = {"schema", "op", "request_id", "version", "context_id"}


class Refusal(Exception):
    pass


def require(condition, code):
    if not condition:
        raise Refusal(code)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def encode(value):
    return json.dumps(value, ensure_ascii=True, allow_nan=False,
                      sort_keys=True, separators=(",", ":")).encode("ascii")


def pairs(items):
    result = {}
    for key, value in items:
        require(key not in result, "duplicate_field")
        result[key] = value
    return result


def invalid_constant(_):
    raise Refusal("nonfinite_number")


def decode(raw):
    try:
        return json.loads(raw.decode("utf-8"), object_pairs_hook=pairs,
                          parse_constant=invalid_constant)
    except (ValueError, UnicodeError, RecursionError) as exc:
        raise Refusal("invalid_json") from exc


def load_pinned(name):
    raw = (HERE / "vendor" / name).read_bytes()
    require(digest(raw) == PINS[name], "reference_pin_mismatch")
    # Compile the verified bytes, not a second file read or an existing pycache.
    module_name = "_face_preview_" + name.removesuffix(".py")
    module = types.ModuleType(module_name)
    sys.modules[module_name] = module  # dataclass needs its defining module
    exec(compile(raw, str(HERE / "vendor" / name), "exec"), module.__dict__)
    return module


def open_root(path):
    root = Path(path)
    require(root.is_absolute() and root == root.resolve(), "unsafe_output_root")
    try:
        root.mkdir(mode=0o700)
    except FileExistsError:
        pass
    fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    info = os.fstat(fd)
    if info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) != 0o700:
        os.close(fd)
        raise Refusal("unsafe_output_root")
    return fd


def write_file(directory, name, raw):
    fd = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                 0o600, dir_fd=directory)
    with os.fdopen(fd, "wb") as stream:
        stream.write(raw)


class Port:
    def __init__(self, output_root, limits):
        require(__debug__, "optimized_python_unsupported")
        require(isinstance(limits, dict) and set(limits) == {
            "max_line_bytes", "max_identifier_bytes", "max_png_bytes", "max_receipt_bytes"
        }, "invalid_limits")
        require(all(type(v) is int and v > 0 for v in limits.values()), "invalid_limits")
        self.limits = limits
        self.renderer = load_pinned("renderer.py")
        self.controller = load_pinned("async_controller.py")
        self.method_raw = (HERE / "data" / "method.json").read_bytes()
        self.method = decode(self.method_raw)
        require(self.method["schema"] == "loom.face_preview.method/1" and
                self.method["coordinate_order"] == list(self.renderer.EXPRESSION_DEFAULTS),
                "invalid_method")
        self.renderer.validate(self.method["config"])
        self.root = open_root(output_root)
        self.version = 0  # stream-local epoch only; the host owns the cross-process gate

    def close(self):
        os.close(self.root)

    def validate(self, raw):
        require(len(raw) <= self.limits["max_line_bytes"], "line_too_large")
        value = decode(raw)
        require(isinstance(value, dict), "invalid_request")
        require(value.get("schema") == REQUEST, "wrong_schema_version")
        operation = value.get("op")
        require(operation in ("render", "cleanup"), "unknown_operation")
        fields = ({"params", "parameter_source", "size"} if operation == "render"
                  else {"artifact_id"})
        require(set(value) == COMMON | fields, "unknown_or_missing_field")
        for key in ("request_id", "context_id"):
            text = value[key]
            require(isinstance(text, str) and text and "\0" not in text, "invalid_identifier")
            try:
                require(len(text.encode("utf-8")) <= self.limits["max_identifier_bytes"],
                        "identifier_too_large")
            except UnicodeError as exc:
                raise Refusal("invalid_identifier") from exc
        require(type(value["version"]) is int and 0 < value["version"] <= 2**53 - 1,
                "invalid_version")  # exact integer transport to the TS caller
        if operation == "cleanup":
            require(isinstance(value["artifact_id"], str) and
                    re.fullmatch(r"fp-[0-9a-f]{32}", value["artifact_id"]), "unsafe_artifact_id")
        else:
            require(value["version"] > self.version, "stale_version")
            require(type(value["size"]) is int and value["size"] == 256, "unsupported_size")
            require(value["parameter_source"] in ("user_vector", "library"), "invalid_parameter_source")
            params = value["params"]
            require(isinstance(params, list) and len(params) == 9 and
                    all(type(v) in (int, float) for v in params), "invalid_vector")
            try:
                require(all(math.isfinite(v) for v in params), "nonfinite_number")
                # Reuse the existing renderer-domain validation. No second controller.
                self.controller.vector(params)
            except (ValueError, OverflowError) as exc:
                raise Refusal("vector_outside_renderer_domain") from exc
        return value

    def handle(self, raw):
        value = self.validate(raw)
        binding = {key: value[key] for key in ("request_id", "version", "context_id")}
        if value["op"] == "cleanup":
            self.cleanup(value["artifact_id"], value["version"], value["context_id"])
            return dict(schema=RESULT, status="cleaned", artifact_id=value["artifact_id"], **binding)

        config = copy.deepcopy(self.method["config"])
        config["expression"] = dict(zip(self.method["coordinate_order"], value["params"]))
        # This synchronous local process is intended for the existing host's worker.
        png = self.renderer.png_bytes(self.renderer.render(config))
        require(len(png) <= self.limits["max_png_bytes"], "png_too_large")
        config_raw = encode(config)
        artifact_id = "fp-" + uuid.uuid4().hex
        source_ref = "sha256:" + digest(raw)
        locator = dict(source=source_ref, member="", json_pointer="", byte_start=0,
                       byte_len=len(raw), time_start=None, time_end=None, line=1)
        receipt = dict(
            schema=RECEIPT, artifact_id=artifact_id, **binding,
            method=dict(id=self.renderer.VERSION, renderer_sha256=PINS["renderer.py"],
                        controller_sha256=PINS["async_controller.py"],
                        method_data_sha256=digest(self.method_raw),
                        adapter_sha256=digest(Path(__file__).read_bytes()),
                        python=sys.version.split()[0], numpy=self.renderer.np.__version__),
            input=dict(file="input.jsonl", sha256=digest(raw), bytes=len(raw),
                       parameter_source=value["parameter_source"]),
            config=dict(file="config.json", sha256=digest(config_raw)),
            png=dict(file="preview.png", sha256=digest(png), bytes=len(png),
                     media_type="image/png", width=256, height=256),
            provenance=dict(source_ref=source_ref, unit_locator=locator,
                            observation_locator=dict(locator, json_pointer="/params"),
                            observation_kind="field"),
            evidence=dict(evidence_class="derived", origin="system", confidence=None,
                          representation="procedural_2d_preview", embedding="NOT_RUN",
                          inference="NOT_RUN", real_expression="NOT_RUN", avatar_3d="unavailable",
                          model=None, provider=None, coalescence="not_requested"),
            cleanup=dict(operation="cleanup", artifact_id=artifact_id,
                         retention="explicit_cleanup_or_host_workspace_removal"))
        receipt_raw = encode(receipt)
        require(len(receipt_raw) <= self.limits["max_receipt_bytes"], "receipt_too_large")
        os.mkdir(artifact_id, mode=0o700, dir_fd=self.root)
        directory = os.open(artifact_id, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                            dir_fd=self.root)
        try:
            for name, content in (("input.jsonl", raw), ("config.json", config_raw),
                                  ("preview.png", png), ("receipt.json", receipt_raw)):
                write_file(directory, name, content)
        except BaseException:
            # Remove only our just-created files, never scan/delete a caller path.
            for name in FILES:
                try:
                    os.unlink(name, dir_fd=directory)
                except FileNotFoundError:
                    pass
            os.rmdir(artifact_id, dir_fd=self.root)
            raise
        finally:
            os.close(directory)
        self.version = value["version"]
        return dict(schema=RESULT, status="rendered", **binding, artifact_id=artifact_id,
                    png_path=artifact_id + "/preview.png", receipt=receipt,
                    receipt_sha256=digest(receipt_raw))

    def cleanup(self, artifact_id, version, context_id):
        directory = os.open(artifact_id, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                            dir_fd=self.root)
        try:
            require(set(os.listdir(directory)) == FILES, "cleanup_foreign_files")
            for name in FILES:
                info = os.stat(name, dir_fd=directory, follow_symlinks=False)
                require(stat.S_ISREG(info.st_mode) and info.st_uid == os.getuid()
                        and info.st_nlink == 1, "cleanup_unsafe_file")
            fd = os.open("receipt.json", os.O_RDONLY | os.O_NOFOLLOW, dir_fd=directory)
            with os.fdopen(fd, "rb") as stream:
                raw = stream.read(self.limits["max_receipt_bytes"] + 1)
            require(len(raw) <= self.limits["max_receipt_bytes"], "cleanup_invalid_receipt")
            receipt = decode(raw)
            require(isinstance(receipt, dict) and receipt.get("schema") == RECEIPT and
                    receipt.get("artifact_id") == artifact_id, "cleanup_invalid_receipt")
            require(receipt.get("version") == version and receipt.get("context_id") == context_id,
                    "cleanup_binding_mismatch")
            for name in FILES:
                os.unlink(name, dir_fd=directory)
        finally:
            os.close(directory)
        os.rmdir(artifact_id, dir_fd=self.root)


def emit(value):
    sys.stdout.buffer.write(encode(value) + b"\n")
    sys.stdout.buffer.flush()


def main():
    port = None
    try:
        if len(sys.argv) == 3 and sys.argv[1] == "-":
            # Existing read_local_json_bytes argv shape; installation-owned config.
            config = decode(sys.argv[2].encode("utf-8"))
            require(isinstance(config, dict) and set(config) == {"output_root", "limits"},
                    "invalid_host_configuration")
        else:
            parser = argparse.ArgumentParser(description=__doc__)
            parser.add_argument("--output-root", required=True)
            parser.add_argument("--limits-file", type=Path, default=HERE / "data" / "limits.json")
            args = parser.parse_args()
            config = dict(output_root=args.output_root, limits=decode(args.limits_file.read_bytes()))
        port = Port(config["output_root"], config["limits"])
        while True:
            raw = sys.stdin.buffer.readline(port.limits["max_line_bytes"] + 1)
            if not raw:
                break
            if len(raw) > port.limits["max_line_bytes"]:
                # Fail closed, do not interpret the tail as another request.
                emit(dict(schema=RESULT, status="refused", error=dict(code="line_too_large")))
                return 2
            try:
                emit(port.handle(raw))
            except Refusal as exc:
                emit(dict(schema=RESULT, status="refused", error=dict(code=str(exc))))
            except (OSError, ValueError, AssertionError):
                emit(dict(schema=RESULT, status="refused", error=dict(code="local_operation_failed")))
        return 0
    except ModuleNotFoundError:
        emit(dict(schema=RESULT, status="unavailable", error=dict(code="reference_dependency_unavailable")))
        return 3
    except Refusal as exc:
        emit(dict(schema=RESULT, status="refused", error=dict(code=str(exc))))
        return 2
    except (OSError, ValueError, KeyError, TypeError, AssertionError):
        emit(dict(schema=RESULT, status="unavailable", error=dict(code="local_configuration_failed")))
        return 3
    finally:
        if port is not None:
            port.close()


if __name__ == "__main__":
    sys.exit(main())
