"""Process-level capability fixtures; they claim no product test coverage."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


HELPER = Path(__file__).with_name("run_optional_npm_scripts.py")


class OptionalNpmScriptTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.package = self.directory / "package.json"
        self.log = self.directory / "command-calls.jsonl"
        self.command = self.directory / "fake-npm"
        self.command.write_text(
            f"#!{sys.executable}\n"
            "import json,os,pathlib,sys\n"
            "with pathlib.Path(os.environ['TASK_NPM_LOG']).open('a') as out:\n"
            "    out.write(json.dumps({'argv':sys.argv[1:],'cwd':os.getcwd()})+'\\n')\n"
            "sys.exit(17 if sys.argv[-1]=='failing-script' else 0)\n")
        self.command.chmod(0o755)

    def run_helper(self, scripts, requested, command=None):
        self.package.write_text(json.dumps({"name": "synthetic-ci-fixture", "scripts": scripts}))
        args = [sys.executable, str(HELPER), "--package-json", str(self.package),
                "--npm", str(command or self.command)]
        for name in requested:
            args.extend(["--script=" + name])
        return subprocess.run(args, env={**os.environ, "TASK_NPM_LOG": str(self.log)},
                              capture_output=True, text=True, check=False)

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def test_missing_capability_never_starts_a_command_or_claims_test_success(self):
        result = self.run_helper({}, ["future-test"], self.directory / "absent-executable")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("CAPABILITY_UNAVAILABLE", result.stdout)
        self.assertIn("no command executed", result.stdout)
        self.assertNotIn("START:", result.stdout)
        self.assertNotIn("PROCESS_RESULT:", result.stdout)
        self.assertEqual(self.calls(), [])

    def test_declared_name_is_one_argv_argument_and_uses_package_directory(self):
        name = "literal ; $(no-shell) script"
        result = self.run_helper({name: "synthetic command"}, [name])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls(), [{"argv": ["run", "--", name], "cwd": str(self.directory)}])
        self.assertIn("PROCESS_RESULT:", result.stdout)
        self.assertNotIn("CAPABILITY_UNAVAILABLE", result.stdout)

    def test_missing_then_declared_capability_starts_only_the_declared_one(self):
        result = self.run_helper({"available": "synthetic command"}, ["missing", "available"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("CAPABILITY_UNAVAILABLE", result.stdout)
        self.assertEqual([call["argv"] for call in self.calls()], [["run", "--", "available"]])

    def test_nonzero_child_status_fails_process_and_stops_remaining_sequence(self):
        scripts = {name: "synthetic command" for name in ("first", "failing-script", "after")}
        result = self.run_helper(scripts, ["first", "failing-script", "after"])
        self.assertEqual(result.returncode, 17, result.stderr)
        self.assertEqual([call["argv"] for call in self.calls()],
                         [["run", "--", "first"], ["run", "--", "failing-script"]])
        self.assertIn("exit=17", result.stdout)

    def test_declared_capability_with_unavailable_executable_is_a_failure(self):
        result = self.run_helper({"available": "synthetic command"}, ["available"],
                                 self.directory / "absent-executable")
        self.assertEqual(result.returncode, 127)
        self.assertIn("START_FAILED", result.stderr)
        self.assertNotIn("CAPABILITY_UNAVAILABLE", result.stdout)
        self.assertEqual(self.calls(), [])

    def test_invalid_package_is_an_error_not_an_unavailable_capability(self):
        self.package.write_text('{"scripts": []}')
        result = subprocess.run([sys.executable, str(HELPER), "--package-json", str(self.package),
                                 "--npm", str(self.command), "--script", "test"],
                                capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 2)
        self.assertIn("CONFIGURATION_ERROR", result.stderr)
        self.assertNotIn("CAPABILITY_UNAVAILABLE", result.stdout)
        self.assertEqual(self.calls(), [])


if __name__ == "__main__":
    unittest.main()
