"""Focused stdlib/ffmpeg tests. Run from repo root; no backend/build/providers."""
from fractions import Fraction
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "loom/tools/video_basic"))
import video_basic as video
from make_fixture import make_clip


class VideoBasicTests(unittest.TestCase):
    def setUp(self):
        video.require_space(ROOT, 1073741824)
        self.tmp = tempfile.TemporaryDirectory(prefix="video-basic-test-")
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        self.source = Path(__file__).with_name("synthetic.mkv")
        self.out = self.dir / "bundle"

    def assert_code(self, code, function, *args, **kwargs):
        with self.assertRaises(video.VideoError) as caught:
            function(*args, **kwargs)
        self.assertEqual(caught.exception.code, code)

    def extract(self, **kwargs):
        return video.extract(self.source, self.out, **kwargs)

    def fake_tool(self, body):
        script = self.dir / "fake-tool"
        script.write_text(f"#!{sys.executable}\n" + body)
        script.chmod(0o700)
        return str(script)

    def test_metadata_pts_hashes_and_direct_image_handoff(self):
        original = self.source.read_bytes()
        result = self.extract(expected_sha256=video.sha256(original), overrides={"max_frames": 3})
        doc = result["receipt"]
        self.assertEqual(doc["original"], {"sha256": video.sha256(original), "size": len(original)})
        stream = doc["metadata"]["streams"][0]
        self.assertEqual((stream["width"], stream["height"], stream["codec_name"]), (64, 48, "ffv1"))
        self.assertIsNotNone(doc["method"]["ffprobe_version"])
        self.assertIsNotNone(doc["method"]["ffmpeg_version"])
        self.assertFalse(doc["sampling"]["whole_video_coverage"])
        frames = doc["frames"]
        self.assertEqual([f["decoded_frame_ordinal"] for f in frames], [0, 2, 4])
        # Actual first PTS is two seconds, even though sample ordinal starts at 0.
        self.assertEqual([Fraction(*f["timestamp"]["seconds"]) for f in frames],
                         [Fraction(2), Fraction(5, 2), Fraction(3)])
        images = video.image_inputs(self.out, original_sha256=doc["original"]["sha256"],
                                    receipt_sha256=result["receipt_sha256"])
        self.assertEqual(len(images), 3)
        for ordinal, image in enumerate(images):
            frame = frames[ordinal]
            self.assertEqual(video.sha256(image.bytes), frame["sha256"])
            self.assertEqual(image.mime, "image/png")
            self.assertFalse(image.has_ocr)
            self.assertEqual(image.note["original_locator"]["source"], "sha256:" + video.sha256(original))
            self.assertEqual(image.note["unit_locator"],
                             {"source": "sha256:" + result["receipt_sha256"], "json_pointer": f"/frames/{ordinal}"})
            self.assertEqual(image.note["observation_locator"]["json_pointer"], f"/frames/{ordinal}/sha256")
            self.assertEqual(image.note["observation_text"], frame["sha256"])
        self.assertEqual(self.source.read_bytes(), original)
        self.assertEqual(sorted(p.name for p in self.out.iterdir()),
                         ["frame-000000.png", "frame-000001.png", "frame-000002.png", "receipt.json"])

    def test_deterministic_repeated_extraction_and_fixture_recipe(self):
        first = self.extract()
        second_dir = self.dir / "again"
        second = video.extract(self.source, second_dir)
        self.assertEqual(first, second)
        for p in self.out.iterdir():
            self.assertEqual(p.read_bytes(), (second_dir / p.name).read_bytes())
        rebuilt = self.dir / "regenerated.mkv"
        make_clip(rebuilt)
        twice = self.dir / "regenerated-again.mkv"
        make_clip(twice)
        # Recipe emits the same bytes on the current local ffmpeg build.
        self.assertEqual(rebuilt.read_bytes(), twice.read_bytes())
        regenerated = video.extract(rebuilt, self.dir / "regenerated-bundle")
        self.assertEqual([f["sha256"] for f in first["receipt"]["frames"]],
                         [f["sha256"] for f in regenerated["receipt"]["frames"]])

    def test_vfr_pts_are_not_nominal_fps_arithmetic(self):
        clip = self.dir / "vfr.mkv"
        cfg = video.settings()
        video._run(["ffmpeg", "-nostdin", "-v", "error", "-n", "-threads", "1",
                    "-filter_threads", "1", "-f", "lavfi", "-i", "testsrc=size=32x24:rate=10",
                    "-vf", "setpts=(N*N+20)/(10*TB)", "-frames:v", "6", "-fps_mode", "passthrough",
                    "-c:v", "ffv1", "-threads", "1", "-f", "matroska", str(clip)],
                   cfg, time.monotonic() + 10, 1048576)
        frames = video.extract(clip, self.out, overrides={"max_frames": 3})["receipt"]["frames"]
        self.assertEqual([Fraction(*f["timestamp"]["seconds"]) for f in frames],
                         [Fraction(2), Fraction(12, 5), Fraction(18, 5)])

    def test_short_clip_and_frame_step_one(self):
        result = self.extract(overrides={"max_frames": 20, "frame_step": 1})
        self.assertEqual(len(result["receipt"]["frames"]), 7)
        self.assertEqual([f["decoded_frame_ordinal"] for f in result["receipt"]["frames"]], list(range(7)))

    def test_corrupt_and_nonvideo_are_clean_failures(self):
        for content in (b"not a video", b"#EXTM3U\nhttp://127.0.0.1:9/private.mkv\n"):
            bad = self.dir / "bad.media"
            bad.write_bytes(content)
            self.assert_code("tool_failed_or_resource_limit", video.extract, bad, self.out)
            self.assertFalse(self.out.exists())
            self.assertFalse(list(self.dir.glob(".video-basic-*")))

    def test_uri_fifo_symlink_and_empty_rejected(self):
        with patch.object(video, "_run", side_effect=AssertionError("subprocess must not run")):
            for uri in ("https://example.invalid/video", "file:/tmp/video", "pipe:0", "concat:a|b", "//host/video"):
                self.assert_code("local_file_required", video.extract, uri, self.out)
            fifo = self.dir / "fifo"
            os.mkfifo(fifo)
            self.assert_code("local_file_required", video.extract, fifo, self.out)
            symlink = self.dir / "link"
            symlink.symlink_to(self.source)
            self.assert_code("local_file_required", video.extract, symlink, self.out)
            empty = self.dir / "empty"
            empty.touch()
            self.assert_code("empty_input", video.extract, empty, self.out)

    def test_input_and_original_hash_limits_before_tools(self):
        with patch.object(video, "_run", side_effect=AssertionError("subprocess must not run")):
            self.assert_code("input_byte_limit", self.extract, overrides={"max_input_bytes": 1})
            self.assert_code("original_hash_mismatch", self.extract, expected_sha256="0" * 64)
            self.assert_code("invalid_original_hash", self.extract, expected_sha256="bad")
        self.assertFalse(self.out.exists())

    def test_dimension_and_pixel_limits(self):
        for option in ({"max_width": 63}, {"max_height": 47}, {"max_pixels": 64 * 48 - 1}):
            self.assert_code("dimension_limit", self.extract, overrides=option)
            self.assertFalse(self.out.exists())

    def test_output_budget_and_file_resource_limit(self):
        self.assert_code("output_byte_limit", self.extract,
                         overrides={"max_output_bytes": 8, "max_probe_bytes": 262144})
        cfg = video.settings()
        target = self.dir / "oversized"
        body = f"with open({str(target)!r}, 'wb') as out: out.write(b'x' * 4096)\n"
        tool = self.fake_tool(body)
        self.assert_code("tool_failed_or_resource_limit", video._run, [tool], cfg, time.monotonic() + 3, 100)
        self.assertLessEqual(target.stat().st_size, 100)

    def test_stdout_stderr_bounds(self):
        cfg = video.settings({"max_probe_bytes": 100, "max_log_bytes": 100})
        for descriptor in (1, 2):
            tool = self.fake_tool(f"import os\nos.write({descriptor}, b'x' * 20000)\n")
            self.assert_code("subprocess_output_limit", video._run, [tool], cfg, time.monotonic() + 3, 1048576)

    def test_timeout_kills_child_group_and_cleans_staging(self):
        pid_file = self.dir / "pid"
        tool = self.fake_tool(f"import os,time\nopen({str(pid_file)!r}, 'w').write(str(os.getpid()))\ntime.sleep(30)\n")
        start = time.monotonic()
        self.assert_code("timeout", self.extract, ffprobe=tool, overrides={"timeout_seconds": 0.4})
        self.assertLess(time.monotonic() - start, 3)
        pid = int(pid_file.read_text())
        with self.assertRaises(ProcessLookupError):
            os.kill(pid, 0)
        self.assertFalse(self.out.exists())
        self.assertFalse(list(self.dir.glob(".video-basic-*")))

    def test_cpu_and_address_space_resources(self):
        cfg = video.settings({"cpu_seconds": 1})
        tool = self.fake_tool("while True: pass\n")
        start = time.monotonic()
        self.assert_code("tool_failed_or_resource_limit", video._run, [tool], cfg, time.monotonic() + 4, 1048576)
        self.assertLess(time.monotonic() - start, 3)
        cfg = video.settings({"address_space_bytes": 67108864})
        tool = self.fake_tool("blob = bytearray(134217728)\n")
        self.assert_code("tool_failed_or_resource_limit", video._run, [tool], cfg, time.monotonic() + 3, 1048576)

    def test_argv_protocol_thread_and_environment_invariants(self):
        cfg = video.settings({"demuxers": ["hls"]})
        opts = video._input_options(cfg)
        self.assertEqual(opts[opts.index("-protocol_whitelist") + 1], "file")
        self.assertEqual(opts[opts.index("-threads") + 1], "1")
        tool = self.fake_tool("import json,os,sys\nprint(json.dumps({'args':sys.argv[1:], 'env':dict(os.environ)}))\n")
        with patch.dict(os.environ, {"VIDEO_SYNTHETIC_SECRET": "must-not-inherit", "HTTP_PROXY": "http://invalid"}):
            stdout, _ = video._run([tool, "$(touch should-not-exist); `x`"], cfg, time.monotonic() + 3, 1048576)
        payload = json.loads(stdout)
        self.assertEqual(payload["args"], ["$(touch should-not-exist); `x`"])
        self.assertNotIn("VIDEO_SYNTHETIC_SECRET", payload["env"])
        self.assertNotIn("HTTP_PROXY", payload["env"])
        self.assertEqual(payload["env"]["OMP_NUM_THREADS"], "1")

    def test_existing_destination_and_pressure_preserve_work(self):
        self.out.mkdir()
        marker = self.out / "owned"
        marker.write_bytes(b"preserve")
        self.assert_code("destination_exists", self.extract)
        self.assertEqual(marker.read_bytes(), b"preserve")
        other = self.dir / "other"
        with patch.object(video, "require_space", side_effect=video.VideoError("free_space_limit")):
            self.assert_code("free_space_limit", video.extract, self.source, other)
        self.assertFalse(other.exists())

    def test_atomic_publication_refuses_even_concurrent_empty_directory(self):
        pending = self.dir / "pending"
        pending.mkdir()
        marker = pending / "marker"
        marker.write_bytes(b"preserve staging")
        self.out.mkdir()
        self.assert_code("destination_exists", video._publish, pending, self.out)
        self.assertEqual(marker.read_bytes(), b"preserve staging")
        self.assertEqual(list(self.out.iterdir()), [])

    def test_changed_input_is_not_published(self):
        actual = video.os.fstat
        calls = 0
        def changed(fd):
            nonlocal calls
            result = actual(fd)
            calls += 1
            if calls == 2:
                from types import SimpleNamespace
                return SimpleNamespace(st_size=result.st_size, st_mtime_ns=result.st_mtime_ns + 1,
                                       st_ctime_ns=result.st_ctime_ns)
            return result
        with patch.object(video.os, "fstat", side_effect=changed):
            self.assert_code("input_changed", self.extract)
        self.assertFalse(self.out.exists())

    def test_pinned_receipt_frame_and_original_integrity(self):
        result = self.extract()
        pinned = {"original_sha256": result["receipt"]["original"]["sha256"],
                  "receipt_sha256": result["receipt_sha256"]}
        self.assert_code("original_hash_mismatch", video.image_inputs, self.out,
                         original_sha256="0" * 64, receipt_sha256=result["receipt_sha256"])
        receipt_file = self.out / "receipt.json"
        raw = receipt_file.read_bytes()
        receipt_file.write_bytes(raw + b" ")
        self.assert_code("receipt_hash_mismatch", video.image_inputs, self.out, **pinned)
        receipt_file.write_bytes(raw)
        frame = self.out / "frame-000003.png"
        frame.write_bytes(frame.read_bytes() + b"tamper")
        self.assert_code("frame_hash_mismatch", video.image_inputs, self.out, **pinned)

    def test_receipt_traversal_symlink_and_pts_rejected(self):
        result = self.extract()
        original = result["receipt"]["original"]["sha256"]
        for change in (lambda d: d["frames"][0].update(file="../outside.png"),
                       lambda d: d["frames"][0]["timestamp"].update(seconds=[999, 1]),
                       lambda d: d["frames"][0].update(stream_index=999)):
            doc = json.loads(video.canonical(result["receipt"]))
            change(doc)
            raw = video.canonical(doc)
            (self.out / "receipt.json").write_bytes(raw)
            self.assert_code("invalid_frame_provenance", video.image_inputs, self.out,
                             original_sha256=original, receipt_sha256=video.sha256(raw))
        (self.out / "receipt.json").write_bytes(video.canonical(result["receipt"]))
        frame = self.out / "frame-000000.png"
        outside = self.dir / "outside.png"
        frame.rename(outside)
        frame.symlink_to(outside)
        self.assert_code("invalid_bundle", video.image_inputs, self.out,
                         original_sha256=original, receipt_sha256=result["receipt_sha256"])

    def test_settings_are_configurable_and_invalid_values_fail(self):
        self.assertEqual(video.settings({"max_frames": 2000000})["max_frames"], 2000000)
        for bad in ([], False, 0, {"max_frames": 0}, {"threads": True}, {"timeout_seconds": float("nan")},
                    {"max_width": -1}, {"demuxers": ["hls,http"]}, {"unexpected": 1}):
            self.assert_code("invalid_settings", video.settings, bad)

    def test_distinct_units_even_for_equal_presentation_timestamps(self):
        result = self.extract()
        doc = result["receipt"]
        doc["frames"][1]["timestamp"] = dict(doc["frames"][0]["timestamp"])
        raw = video.canonical(doc)
        (self.out / "receipt.json").write_bytes(raw)
        images = video.image_inputs(self.out, original_sha256=doc["original"]["sha256"],
                                    receipt_sha256=video.sha256(raw))
        self.assertEqual(images[0].note["original_locator"], images[1].note["original_locator"])
        self.assertNotEqual(images[0].note["unit_locator"], images[1].note["unit_locator"])


if __name__ == "__main__":
    unittest.main()
