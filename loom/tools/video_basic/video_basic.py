"""Bounded local video -> metadata and PNG snapshots, without OCR or models.

POSIX adapter; defaults are data, constraints are enforced here. Nothing is
written to Loom storage. The native caller owns Source/Unit/Observation commits.
"""
from __future__ import annotations

import argparse
import ctypes
from dataclasses import dataclass
import errno
from fractions import Fraction
import hashlib
import json
import math
import os
from pathlib import Path
import re
import selectors
import shutil
import signal
import stat
import struct
import subprocess
import sys
import tempfile
import time


HERE = Path(__file__).resolve().parent
SCHEMA = "loom.video_basic/1"
HASH = re.compile(r"[0-9a-f]{64}\Z")


class VideoError(Exception):
    def __init__(self, code):
        super().__init__(code)
        self.code = code


def canonical(value):
    return (json.dumps(value, ensure_ascii=False, sort_keys=True,
                       separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def settings(overrides=None):
    defaults = json.loads((HERE / "defaults.json").read_text())
    if overrides is not None:
        if not isinstance(overrides, dict) or set(overrides) - set(defaults):
            raise VideoError("invalid_settings")
        defaults.update(overrides)
    for key, value in defaults.items():
        if key == "demuxers":
            if (not isinstance(value, list) or not value or
                    any(not isinstance(x, str) or not re.fullmatch(r"[a-z0-9_]+", x) for x in value)):
                raise VideoError("invalid_settings")
        elif key == "timeout_seconds":
            if type(value) not in (int, float) or not math.isfinite(value) or value <= 0:
                raise VideoError("invalid_settings")
        elif type(value) is not int or value <= 0:
            raise VideoError("invalid_settings")
    if defaults["max_output_bytes"] // defaults["max_frames"] < 1:
        raise VideoError("invalid_settings")
    return defaults


def require_space(directory, minimum):
    # Check both global root and target, even when target is on a tmpfs.
    for path in (Path("/"), Path(directory)):
        v = os.statvfs(path)
        if v.f_bavail * v.f_frsize < minimum:
            raise VideoError("free_space_limit")


def _local_path(value):
    text = os.fspath(value)
    if (not isinstance(text, str) or not text or "\x00" in text or
            re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:", text) or text.startswith("//")):
        raise VideoError("local_file_required")
    return Path(text).absolute()


def _snapshot(source, target, limit, deadline):
    try:
        fd = os.open(source, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
    except OSError:
        raise VideoError("local_file_required") from None
    digest = hashlib.sha256()
    total = 0
    with os.fdopen(fd, "rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode):
            raise VideoError("local_file_required")
        if before.st_size > limit:
            raise VideoError("input_byte_limit")
        with target.open("xb") as out:
            while True:
                if time.monotonic() >= deadline:
                    raise VideoError("timeout")
                chunk = stream.read(min(65536, limit - total + 1))
                if not chunk:
                    break
                total += len(chunk)
                if total > limit:
                    raise VideoError("input_byte_limit")
                digest.update(chunk)
                out.write(chunk)
        after = os.fstat(stream.fileno())
    if (before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (
            after.st_size, after.st_mtime_ns, after.st_ctime_ns) or total != before.st_size:
        raise VideoError("input_changed")
    if not total:
        raise VideoError("empty_input")
    target.chmod(0o400)
    return {"sha256": digest.hexdigest(), "size": total}


def _run(argv, cfg, deadline, file_bytes, host_context=None):
    """Bound both pipes while the child runs; kill/reap its process group on failure."""
    executable = shutil.which(argv[0])
    if not executable:
        raise VideoError("tool_unavailable")
    limits = {k: cfg[k] for k in ("cpu_seconds", "address_space_bytes", "threads")}
    limits["file_bytes"] = file_bytes
    command = [sys.executable, "-I", str(HERE / "limited_exec.py"),
               json.dumps(limits), str(Path(executable).resolve()), *argv[1:]]
    if host_context is not None:
        work, runtime = host_context
        admitted = ['/usr/bin/setpriv', '--no-new-privs', '--landlock-access', 'fs']
        for rights, path in [('execute,read-file', str(Path(executable).resolve())),
                             ('execute,read-file',str(Path(sys.executable).resolve())),
                             ('execute,read-file,read-dir','/usr/lib'),
                             ('read-file',str(HERE/'limited_exec.py')),
                             ('read-file,write-file','/dev/null'),
                             ('read-file,read-dir,write-file,make-reg,truncate',str(work))]:
            admitted += ['--landlock-rule','path-beneath:'+rights+':'+path]
        command = admitted + command
    if time.monotonic() >= deadline:
        raise VideoError("timeout")
    try:
        proc = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, shell=False, start_new_session=host_context is None,
                                env={"PATH": os.defpath, "LC_ALL": "C"})
    except OSError:
        raise VideoError("tool_unavailable") from None
    buffers = [bytearray(), bytearray()]
    caps = [cfg["max_probe_bytes"], cfg["max_log_bytes"]]
    try:
        with selectors.DefaultSelector() as selector:
            for index, stream in enumerate((proc.stdout, proc.stderr)):
                os.set_blocking(stream.fileno(), False)
                selector.register(stream, selectors.EVENT_READ, index)
            while selector.get_map():
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise VideoError("timeout")
                for key, _ in selector.select(remaining):
                    index = key.data
                    chunk = os.read(key.fileobj.fileno(), min(65536, caps[index] + 1))
                    if not chunk:
                        selector.unregister(key.fileobj)
                    else:
                        buffers[index].extend(chunk)
                        if len(buffers[index]) > caps[index]:
                            raise VideoError("subprocess_output_limit")
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise VideoError("timeout")
        try:
            status = proc.wait(timeout=remaining)
        except subprocess.TimeoutExpired:
            raise VideoError("timeout") from None
        if status:
            # Never return codec stderr: it can contain input metadata/paths.
            raise VideoError("tool_failed_or_resource_limit")
        return bytes(buffers[0]), bytes(buffers[1])
    finally:
        # Also kills descendants holding inherited pipes on timeout/error.
        try:
            if host_context is None: os.killpg(proc.pid, signal.SIGKILL)
            else: proc.kill()  # outer native watchdog owns the inherited group
        except ProcessLookupError:
            pass
        proc.wait()
        proc.stdout.close()
        proc.stderr.close()


def _input_options(cfg):
    # Protocol restriction is invariant even if the caller enables a demuxer.
    return ["-protocol_whitelist", "file", "-format_whitelist", ",".join(cfg["demuxers"]),
            "-max_alloc", str(cfg["max_alloc_bytes"]),
            "-probesize", str(cfg["probe_size_bytes"]),
            "-analyzeduration", str(cfg["analyze_microseconds"]),
            "-threads", str(cfg["threads"])]


def _dimensions(width, height, cfg):
    if (type(width) is not int or type(height) is not int or width <= 0 or height <= 0 or
            width > cfg["max_width"] or height > cfg["max_height"] or
            width * height > cfg["max_pixels"]):
        raise VideoError("dimension_limit")


def _png(data, cfg):
    if len(data) < 33 or data[:8] != b"\x89PNG\r\n\x1a\n" or data[8:16] != b"\x00\x00\x00\rIHDR":
        raise VideoError("invalid_frame")
    width, height = struct.unpack(">II", data[16:24])
    _dimensions(width, height, cfg)
    return width, height


def _timing(log):
    text = log.decode("utf-8", "replace")
    bases = re.findall(r"config in time_base:\s*(\d+)/(\d+)", text)
    if len(bases) != 1:
        raise VideoError("timestamp_unavailable")
    numerator, denominator = map(int, bases[0])
    if numerator <= 0 or denominator <= 0:
        raise VideoError("timestamp_unavailable")
    rows = re.findall(r"\bn:\s*(\d+)\s+pts:\s*(-?\d+)\s+pts_time:", text)
    if not rows:
        raise VideoError("timestamp_unavailable")
    version = re.search(r"^ffmpeg version (\S+)", text)
    return (numerator, denominator), [(int(n), int(p)) for n, p in rows], version.group(1) if version else None


def _publish(bundle, destination):
    # Linux renameat2(RENAME_NOREPLACE) gives atomic publication without ever
    # replacing even an EMPTY destination created concurrently by another task.
    # Fail closed on platforms/filesystems without this operation.
    library = ctypes.CDLL(None, use_errno=True)
    rename = getattr(library, "renameat2", None)
    if rename is None:
        raise VideoError("atomic_publish_unavailable")
    rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    rename.restype = ctypes.c_int
    if rename(-100, os.fsencode(bundle), -100, os.fsencode(destination), 1):
        code = ctypes.get_errno()
        if code == errno.EEXIST:
            raise VideoError("destination_exists")
        raise VideoError("atomic_publish_unavailable")


def extract(source, destination, *, overrides=None, expected_sha256=None,
            ffprobe="ffprobe", ffmpeg="ffmpeg", host_context=None):
    """Publish a new private bundle directory or fail without changing destination.

    Sampling starts at decoded ordinal 0, then every frame_step. This is a
    bounded prefix, not coverage of the whole video. Timestamps are actual PTS
    copied from the selected decoded frames, never ordinal / nominal FPS.
    """
    cfg = settings(overrides)
    source = _local_path(source)
    destination = _local_path(destination)
    if expected_sha256 is not None and (not isinstance(expected_sha256, str) or not HASH.fullmatch(expected_sha256)):
        raise VideoError("invalid_original_hash")
    if destination.exists() or destination.is_symlink():
        raise VideoError("destination_exists")
    require_space(destination.parent, cfg["min_free_bytes"])
    deadline = time.monotonic() + cfg["timeout_seconds"]
    # TemporaryDirectory guarantees cleanup of snapshots and partial PNGs.
    with tempfile.TemporaryDirectory(prefix=".video-basic-", dir=destination.parent) as temporary:
        stage = Path(temporary)
        snapshot = stage / "input.media"
        original = _snapshot(source, snapshot, cfg["max_input_bytes"], deadline)
        if expected_sha256 is not None and original["sha256"] != expected_sha256:
            raise VideoError("original_hash_mismatch")
        require_space(stage, cfg["min_free_bytes"])
        raw, _ = _run([ffprobe, "-v", "error", *_input_options(cfg), "-select_streams", "v:0",
                       "-show_entries", "program_version=version:format=format_name,duration,start_time:"
                       "stream=index,codec_name,width,height,time_base,start_pts,start_time,duration_ts,"
                       "duration,avg_frame_rate,r_frame_rate,sample_aspect_ratio:stream_tags=rotate:"
                       "stream_side_data=rotation", "-show_program_version", "-of", "json", str(snapshot)],
                      cfg, deadline, cfg["max_output_bytes"], host_context)
        try:
            probe = json.loads(raw)
            stream, = probe["streams"]
            width, height = stream["width"], stream["height"]
            stream_index = stream["index"]
            if type(stream_index) is not int or stream_index < 0:
                raise ValueError()
        except (ValueError, KeyError, TypeError):
            raise VideoError("video_stream_unavailable") from None
        _dimensions(width, height, cfg)
        # Reserve receipt space. RLIMIT_FSIZE limits EVERY PNG, bounding their
        # aggregate even between parent observations, not just after decoding.
        frame_budget = (cfg["max_output_bytes"] - cfg["max_probe_bytes"]) // cfg["max_frames"]
        if frame_budget <= 0:
            raise VideoError("output_byte_limit")
        bundle = stage / "bundle"
        bundle.mkdir(mode=0o700)
        require_space(stage, cfg["min_free_bytes"])
        _, log = _run([ffmpeg, "-nostdin", "-loglevel", "info", "-n",
                       *_input_options(cfg), "-copyts", "-noautorotate", "-i", str(snapshot),
                       "-map", f"0:{stream_index}", "-an", "-sn", "-dn",
                       "-filter_threads", str(cfg["threads"]),
                       "-vf", f"select=not(mod(n\\,{cfg['frame_step']})),showinfo",
                       "-frames:v", str(cfg["max_frames"]), "-fps_mode", "passthrough",
                       "-c:v", "png", "-pix_fmt", "rgb24", "-threads", str(cfg["threads"]),
                       "-start_number", "0", "-f", "image2", str(bundle / "frame-%06d.png")],
                      cfg, deadline, frame_budget, host_context)
        base, rows, version = _timing(log)
        # No promise of cross-FFmpeg-version byte-identical PNGs.
        files = sorted(bundle.glob("frame-*.png"))
        if not files or len(files) > cfg["max_frames"] or len(rows) < len(files):
            raise VideoError("frame_count_mismatch")
        frames = []
        previous = None
        for ordinal, path in enumerate(files):
            if path.name != f"frame-{ordinal:06d}.png" or rows[ordinal][0] != ordinal:
                raise VideoError("frame_count_mismatch")
            data = path.read_bytes()
            if len(data) > frame_budget:
                raise VideoError("output_byte_limit")
            dimensions = _png(data, cfg)
            if dimensions != (width, height):
                raise VideoError("dimension_changed")
            pts = rows[ordinal][1]
            seconds = Fraction(pts * base[0], base[1])
            if previous is not None and seconds < previous:
                raise VideoError("nonmonotonic_timestamp")
            previous = seconds
            frames.append({"ordinal": ordinal, "decoded_frame_ordinal": ordinal * cfg["frame_step"],
                           "file": path.name, "sha256": sha256(data), "size": len(data),
                           "mime": "image/png", "width": width, "height": height,
                           "original_sha256": original["sha256"], "stream_index": stream_index,
                           "timestamp": {"pts": pts, "time_base": list(base),
                                         "seconds": [seconds.numerator, seconds.denominator]}})
        receipt = {"schema": SCHEMA, "original": original, "metadata": probe,
                   "method": {"id": "ffmpeg.video_basic", "version": "1", "evidence_class": "derived",
                              "ffprobe_version": probe.get("program_version", {}).get("version"),
                              "ffmpeg_version": version, "autorotate": False, "pixel_format": "rgb24"},
                   "limits": cfg,
                   "sampling": {"kind": "decoded_prefix", "frame_step": cfg["frame_step"],
                                "max_frames": cfg["max_frames"], "whole_video_coverage": False},
                   "frames": frames}
        encoded = canonical(receipt)
        if len(encoded) > cfg["max_probe_bytes"] or len(encoded) + sum(f["size"] for f in frames) > cfg["max_output_bytes"]:
            raise VideoError("output_byte_limit")
        (bundle / "receipt.json").write_bytes(encoded)
        require_space(stage, cfg["min_free_bytes"])
        _publish(bundle, destination)
    return {"receipt": receipt, "receipt_sha256": sha256(encoded)}


@dataclass(frozen=True)
class ImageInput:
    """Direct PreparedImage carrier fields: has_ocr is always false."""
    bytes: bytes
    mime: str
    name: str
    note: dict
    has_ocr: bool = False


def image_inputs(bundle, *, original_sha256, receipt_sha256, overrides=None):
    """Validate ALL snapshots before returning any, with caller-pinned hashes.

    The native caller can copy these carrier fields into PreparedImage without
    running prepare_image_attachment/screenshot_body (both can perform OCR).
    Unit IDs and storage transactions remain the native caller's operations.
    """
    cfg = settings(overrides)
    bundle = _local_path(bundle)
    if (not isinstance(original_sha256, str) or not HASH.fullmatch(original_sha256) or
            not isinstance(receipt_sha256, str) or not HASH.fullmatch(receipt_sha256)):
        raise VideoError("invalid_original_hash")
    raw = _read_regular(bundle / "receipt.json", cfg["max_probe_bytes"])
    if sha256(raw) != receipt_sha256:
        raise VideoError("receipt_hash_mismatch")
    try:
        doc = json.loads(raw)
        if doc["schema"] != SCHEMA or doc["original"]["sha256"] != original_sha256:
            raise VideoError("original_hash_mismatch")
        frames = doc["frames"]
        if not isinstance(frames, list) or not 0 < len(frames) <= cfg["max_frames"]:
            raise VideoError("frame_count_mismatch")
        inputs = []
        total = len(raw)
        previous = None
        for ordinal, frame in enumerate(frames):
            if (frame["file"] != f"frame-{ordinal:06d}.png" or frame["ordinal"] != ordinal or
                    frame["original_sha256"] != original_sha256 or frame["mime"] != "image/png"):
                raise VideoError("invalid_frame_provenance")
            data = _read_regular(bundle / frame["file"], cfg["max_output_bytes"] - total)
            total += len(data)
            if len(data) != frame["size"] or sha256(data) != frame["sha256"]:
                raise VideoError("frame_hash_mismatch")
            if _png(data, cfg) != (frame["width"], frame["height"]):
                raise VideoError("dimension_changed")
            pts = frame["timestamp"]["pts"]
            base = frame["timestamp"]["time_base"]
            if (type(pts) is not int or len(base) != 2 or
                    any(type(x) is not int or x <= 0 for x in base)):
                raise VideoError("invalid_frame_provenance")
            seconds = Fraction(pts * base[0], base[1])
            if (frame["timestamp"]["seconds"] != [seconds.numerator, seconds.denominator] or
                    (previous is not None and seconds < previous) or
                    frame["decoded_frame_ordinal"] != ordinal * doc["sampling"]["frame_step"] or
                    frame["stream_index"] != doc["metadata"]["streams"][0]["index"]):
                raise VideoError("invalid_frame_provenance")
            previous = seconds
            note = {"schema": "loom.video_frame/1", "original_sha256": original_sha256,
                    "frame": frame, "method": doc["method"], "receipt_sha256": receipt_sha256,
                    "unit_locator": {"source": "sha256:" + receipt_sha256,
                                     "json_pointer": f"/frames/{ordinal}"},
                    "original_locator": {"source": "sha256:" + original_sha256, "time_start": float(seconds)},
                    "observation_locator": {"source": "sha256:" + receipt_sha256,
                                            "json_pointer": f"/frames/{ordinal}/sha256"},
                    "observation_text": frame["sha256"], "evidence_class": "derived"}
            inputs.append(ImageInput(data, "image/png", frame["file"], note))
        return tuple(inputs)
    except (ValueError, KeyError, TypeError, IndexError, ZeroDivisionError, OverflowError):
        raise VideoError("invalid_receipt") from None


def _read_regular(path, limit):
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
        with os.fdopen(fd, "rb") as stream:
            meta = os.fstat(stream.fileno())
            if not stat.S_ISREG(meta.st_mode) or meta.st_size > limit:
                raise VideoError("output_byte_limit")
            data = stream.read(limit + 1)
            if len(data) > limit:
                raise VideoError("output_byte_limit")
            return data
    except OSError:
        raise VideoError("invalid_bundle") from None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source")
    parser.add_argument("destination")
    parser.add_argument("--settings", type=Path)
    parser.add_argument("--original-sha256")
    args = parser.parse_args()
    try:
        overrides = json.loads(args.settings.read_text()) if args.settings else None
        result = extract(args.source, args.destination, overrides=overrides, expected_sha256=args.original_sha256)
        print(json.dumps({"schema": SCHEMA, "receipt_sha256": result["receipt_sha256"],
                          "original_sha256": result["receipt"]["original"]["sha256"],
                          "frames": len(result["receipt"]["frames"])}))
    except (VideoError, OSError, ValueError) as error:
        print(json.dumps({"error": {"code": error.code if isinstance(error, VideoError) else "invalid_request"}}))
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
