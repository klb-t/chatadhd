#!/usr/bin/env python3
"""One scoped mechanism check; one real render, no model or statistical study."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("face_preview_port", HERE / "port.py")
port = importlib.util.module_from_spec(spec)
spec.loader.exec_module(port)


def check_png(raw):
    assert raw[:8] == b"\x89PNG\r\n\x1a\n"
    offset, compressed, kinds = 8, b"", []
    while offset < len(raw):
        size = struct.unpack(">I", raw[offset:offset + 4])[0]
        kind = raw[offset + 4:offset + 8]
        data = raw[offset + 8:offset + 8 + size]
        crc = struct.unpack(">I", raw[offset + 8 + size:offset + 12 + size])[0]
        assert zlib.crc32(kind + data) == crc
        kinds.append(kind)
        if kind == b"IHDR":
            assert struct.unpack(">IIBBBBB", data) == (256, 256, 8, 2, 0, 0, 0)
        if kind == b"IDAT":
            compressed += data
        offset += size + 12
    assert offset == len(raw) and kinds == [b"IHDR", b"IDAT", b"IEND"]
    pixels = zlib.decompress(compressed)
    assert len(pixels) == 256 * (1 + 256 * 3)
    assert all(pixels[row * 769] == 0 for row in range(256))
    assert len(set(pixels)) > 32  # nonblank raster, not just a PNG signature


def run():
    source = (HERE / "fixtures" / "render.jsonl").read_bytes()
    request = json.loads(source)
    limits = json.loads((HERE / "data" / "limits.json").read_bytes())
    with tempfile.TemporaryDirectory(prefix="face-preview-sanity-") as temp:
        root = Path(temp) / "outputs"
        # Empty environment, argv array, no shell. Exercises actual executable boundary.
        completed = subprocess.run([sys.executable, "-I", "-B", str(HERE / "port.py"),
                                    "--output-root", str(root)], input=source,
                                   capture_output=True, env={"LANG": "C.UTF-8"}, timeout=20)
        assert completed.returncode == 0 and completed.stderr == b"", completed.stderr
        response = json.loads(completed.stdout)
        assert response["status"] == "rendered"
        artifact = root / response["artifact_id"]
        png = (artifact / "preview.png").read_bytes()
        check_png(png)
        receipt_raw = (artifact / "receipt.json").read_bytes()
        receipt = json.loads(receipt_raw)
        assert response["receipt_sha256"] == port.digest(receipt_raw)
        assert receipt == response["receipt"]
        assert (artifact / "input.jsonl").read_bytes() == source
        assert receipt["input"]["sha256"] == port.digest(source)
        assert receipt["config"]["sha256"] == port.digest((artifact / "config.json").read_bytes())
        assert receipt["png"]["sha256"] == port.digest(png)
        assert receipt["method"]["renderer_sha256"] == port.PINS["renderer.py"]
        assert receipt["method"]["adapter_sha256"] == port.digest((HERE / "port.py").read_bytes())
        assert receipt["version"] == request["version"] and receipt["context_id"] == request["context_id"]
        assert receipt["evidence"]["embedding"] == receipt["evidence"]["inference"] == "NOT_RUN"
        assert receipt["evidence"]["model"] is None
        assert receipt["evidence"]["evidence_class"] == "derived"
        assert stat_mode(root) == 0o700
        assert all(stat_mode(path) == 0o600 for path in artifact.iterdir())

        # This evidence is from the current single render, copied before cleanup.
        evidence = HERE / "fixtures" / "actual"
        evidence.mkdir(exist_ok=True)
        for name in port.FILES:
            (evidence / name).write_bytes((artifact / name).read_bytes())
        (evidence / "response.json").write_bytes(completed.stdout)

        adapter = port.Port(str(root), limits)
        refused = []

        def refusal(name, value, expected, raw=None):
            before = set(root.iterdir())
            try:
                adapter.handle(raw if raw is not None else port.encode(value))
                raise AssertionError("accepted " + name)
            except port.Refusal as exc:
                assert str(exc) == expected, (name, str(exc), expected)
            assert before == set(root.iterdir())
            refused.append(name)

        def changed(**values):
            result = copy.deepcopy(request)
            result.update(values)
            return result

        refusal("unknown_path_field", changed(output_path="../../escape.png"), "unknown_or_missing_field")
        refusal("wrong_schema", changed(schema="loom.face_preview.request/2"), "wrong_schema_version")
        refusal("wrong_size", changed(size=512), "unsupported_size")
        refusal("bool_size", changed(size=True), "unsupported_size")
        refusal("bool_coordinate", changed(params=[True] * 9), "invalid_vector")
        refusal("wrong_dimension", changed(params=[0] * 8), "invalid_vector")
        refusal("renderer_domain", changed(params=[0] * 8 + [-0.1]), "vector_outside_renderer_domain")
        refusal("nan", request, "nonfinite_number", source.replace(b"0.2", b"NaN", 1))
        refusal("overflow_number", request, "nonfinite_number", source.replace(b"0.2", b"1e999", 1))
        refusal("duplicate_key", request, "duplicate_field", source.replace(b'{', b'{"op":"render",', 1))
        refusal("unknown_parameter_source", changed(parameter_source="Gemini"), "invalid_parameter_source")
        refusal("wrong_epoch_type", changed(version=True), "invalid_version")
        refusal("oversize", request, "line_too_large", b" " * (limits["max_line_bytes"] + 1))
        adapter.version = 1
        refusal("stale_epoch", request, "stale_version")
        cleanup = {key: request[key] for key in port.COMMON}
        cleanup.update(op="cleanup", artifact_id=response["artifact_id"])
        refusal("traversal", dict(cleanup, artifact_id="../escape"), "unsafe_artifact_id")
        refusal("cleanup_wrong_context", dict(cleanup, context_id="other"), "cleanup_binding_mismatch")
        link = root / ("fp-" + "0" * 32)
        link.symlink_to(artifact, target_is_directory=True)
        try:
            adapter.handle(port.encode(dict(cleanup, artifact_id=link.name)))
            raise AssertionError("followed directory symlink")
        except OSError:
            refused.append("symlink_directory")
        link.unlink()
        preview = artifact / "preview.png"
        saved_png = preview.read_bytes()
        preview.unlink()
        preview.symlink_to(evidence / "preview.png")
        refusal("symlink_file", cleanup, "cleanup_unsafe_file")
        preview.unlink()
        preview.write_bytes(saved_png)
        os.chmod(preview, 0o600)
        assert adapter.handle(port.encode(cleanup))["status"] == "cleaned"
        assert not list(root.iterdir())
        adapter.close()

        # Existing host runner argv shape: cleanup only, no extra render.
        host = dict(output_root=str(root), limits=limits)
        result = subprocess.run([sys.executable, "-I", "-B", str(HERE / "port.py"), "-", json.dumps(host)],
                                input=port.encode(dict(cleanup, artifact_id="../escape")) + b"\n",
                                capture_output=True, env={"LANG": "C.UTF-8"}, timeout=20)
        assert result.returncode == 0 and json.loads(result.stdout)["error"]["code"] == "unsafe_artifact_id"
        # Oversize is terminal: a valid-looking tail must never become a request.
        result = subprocess.run([sys.executable, "-I", "-B", str(HERE / "port.py"), "-", json.dumps(host)],
                                input=b" " * (limits["max_line_bytes"] + 1) + b"\n" + source,
                                capture_output=True, env={"LANG": "C.UTF-8"}, timeout=20)
        assert result.returncode == 2 and len(result.stdout.splitlines()) == 1
        assert not list(root.iterdir())
        report = dict(status="PASS", real_render_count=1, png_sha256=port.digest(png),
                      receipt_sha256=port.digest(receipt_raw), refusals=refused,
                      refusal_count=len(refused), cleanup="PASS", host_argv="PASS",
                      oversized_stream_terminal="PASS", full_build="NOT_RUN",
                      host_cpp="NOT_RUN", embedding="NOT_RUN", inference="NOT_RUN")
        (evidence / "sanity.json").write_bytes(port.encode(report) + b"\n")
        print(json.dumps(report, ensure_ascii=False))


def stat_mode(path):
    return path.stat().st_mode & 0o777


if __name__ == "__main__":
    run()
