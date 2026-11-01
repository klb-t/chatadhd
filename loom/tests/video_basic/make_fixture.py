"""Rebuild the tiny, entirely synthetic FFV1/Matroska fixture with local ffmpeg."""
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "loom/tools/video_basic"))
import video_basic as video


def make_clip(destination, *, width=64, height=48, frames=7, offset=2):
    cfg = video.settings()
    video.require_space(Path(destination).parent, cfg["min_free_bytes"])
    # No external bytes, text fonts, RNG, network, audio, or model involved.
    # Offset deliberately differs from zero to catch timestamp rebasing bugs.
    video._run(["ffmpeg", "-nostdin", "-v", "error", "-n", "-threads", "1",
                "-filter_threads", "1", "-f", "lavfi", "-i",
                f"testsrc=size={width}x{height}:rate=4", "-vf", f"setpts=PTS+{offset}/TB",
                "-frames:v", str(frames), "-c:v", "ffv1", "-threads", "1",
                "-fflags", "+bitexact", "-flags:v", "+bitexact", "-map_metadata", "-1",
                "-f", "matroska", str(Path(destination).absolute())],
               cfg, time.monotonic() + cfg["timeout_seconds"], 1048576)


if __name__ == "__main__":
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).with_name("synthetic.mkv")
    make_clip(path)
    print(json.dumps({"fixture": path.name, "sha256": video.sha256(path.read_bytes()),
                      "size": path.stat().st_size, "synthetic": True}))
