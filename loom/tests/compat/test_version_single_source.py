"""R36: one source of the version.

loom/VERSION holds "x.yy.zz" (zero-padded, so versions sort as text). Every place that names the product version reads it:
CMake (project version and LOOM_VERSION_STRING), the Android build (versionName / versionCode), the package workflow
(APK, server archive and the one flat release zip), the Python app (main.py) and the web package (npm wants semver,
so it holds the same numbers without the padding). A hard-coded version anywhere else fails this test.
"""
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
LOOM = ROOT / "loom"


def version_text() -> str:
    return (LOOM / "VERSION").read_text(encoding="utf-8").strip()


class SingleVersionSource(unittest.TestCase):
    def test_format_sorts(self):
        self.assertRegex(version_text(), r"^\d+\.\d\d\.\d\d$")

    def test_cmake_reads_the_file(self):
        cm = (LOOM / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn('file(READ "${CMAKE_CURRENT_LIST_DIR}/VERSION" LOOM_VERSION_TEXT)', cm)
        self.assertIn("project(loom VERSION ${LOOM_VERSION_NUMERIC}", cm)
        self.assertIn('LOOM_VERSION_STRING="${LOOM_VERSION_TEXT}"', cm)
        self.assertNotRegex(cm, r"project\(loom VERSION \d")

    def test_android_reads_the_file(self):
        g = (LOOM / "android/app/build.gradle.kts").read_text(encoding="utf-8")
        self.assertIn('rootProject.file("../VERSION")', g)
        self.assertNotRegex(g, r'orElse\("\d+\.\d+\.\d+"\)')

    def test_package_workflow_names_from_the_file(self):
        wf = (ROOT / ".github/workflows/package.yml").read_text(encoding="utf-8")
        self.assertEqual(wf.count("VERSION)-ci.${GITHUB_RUN_NUMBER}"), 3, "APK, server archive and release zip")
        self.assertNotIn('"ci-${GITHUB_RUN_NUMBER}', wf)
        self.assertIn("release:", wf)

    def test_python_app_reads_the_file(self):
        self.assertIn('/ "loom" / "VERSION"', (ROOT / "main.py").read_text(encoding="utf-8"))

    def test_web_package_has_the_same_numbers(self):
        x, yy, zz = (int(p) for p in version_text().split("."))
        semver = f"{x}.{yy}.{zz}"
        self.assertEqual(json.loads((LOOM / "web/package.json").read_text(encoding="utf-8"))["version"], semver)
        lock = json.loads((LOOM / "web/package-lock.json").read_text(encoding="utf-8"))
        self.assertEqual(lock["version"], semver)
        self.assertEqual(lock["packages"][""]["version"], semver)


if __name__ == "__main__":
    unittest.main()
