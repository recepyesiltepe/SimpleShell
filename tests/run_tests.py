#!/usr/bin/env python3
import os
import re
import stat
import subprocess
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
SHELL_BINARY = REPO_ROOT / "bin" / "SimpleShell"


def strip_ansi(text: str) -> str:
    return re.sub(r"\x1b\[[0-9;?]*[ -/]*[@-~]", "", text)


class ShellTest:
    def __init__(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory(prefix="simpleshell-test-")
        self.root = Path(self.temp_dir.name)
        self.home = self.root / "home"
        self.work = self.root / "work"
        self.home.mkdir()
        self.work.mkdir()

    def cleanup(self) -> None:
        self.temp_dir.cleanup()

    def run_interactive(self, commands: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
        script = "\n".join(commands + ["exit 0"]) + "\n"
        env = {
            **os.environ,
            "HOME": str(self.home),
            "SIMPLESHELL_NO_TTY_EDITOR": "1",
            "SIMPLESHELL_PROMPT": "PROMPT[\\?]> ",
        }
        return subprocess.run(
            [str(SHELL_BINARY)],
            input=script,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            cwd=cwd or self.work,
            env=env,
            timeout=10,
        )

    def run_script(self, script_path: Path, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
        env = {**os.environ, "HOME": str(self.home)}
        return subprocess.run(
            [str(SHELL_BINARY), str(script_path)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            cwd=cwd or self.work,
            env=env,
            timeout=10,
        )


def assert_contains(output: str, expected: str) -> None:
    if expected not in output:
        raise AssertionError(f"expected {expected!r} in output:\n{output}")


def test_basic_execution_and_conditionals() -> None:
    test = ShellTest()
    try:
        result = test.run_interactive(
            [
                "echo hello",
                "false || echo fallback",
                "true && echo chained",
                "echo one; echo two",
            ]
        )
        output = strip_ansi(result.stdout)
        assert result.returncode == 0
        assert_contains(output, "hello")
        assert_contains(output, "fallback")
        assert_contains(output, "chained")
        assert_contains(output, "one")
        assert_contains(output, "two")
    finally:
        test.cleanup()


def test_builtin_redirection_aliases_and_env() -> None:
    test = ShellTest()
    try:
        result = test.run_interactive(
            [
                "pwd > pwd.txt",
                "FOO=temporary printenv FOO",
                "printenv FOO",
                "FOO=persistent",
                "printenv FOO",
                "alias hi='echo hello-alias'",
                "hi",
                "unalias hi",
                "hi",
                "echo missing-status:$?",
            ]
        )
        output = strip_ansi(result.stdout)
        assert result.returncode == 0
        assert (test.work / "pwd.txt").read_text().strip() == str(test.work)
        assert_contains(output, "temporary")
        assert_contains(output, "persistent")
        assert_contains(output, "hello-alias")
        assert_contains(output, "hi: No such file or directory")
        assert_contains(output, "missing-status:127")
    finally:
        test.cleanup()


def test_expansion_quotes_globs_and_comments() -> None:
    test = ShellTest()
    try:
        (test.work / "alpha.one").write_text("")
        (test.work / "beta.one").write_text("")
        result = test.run_interactive(
            [
                "echo hello\\ world",
                'echo "literal # kept"',
                "echo visible # hidden",
                "echo ~",
                "echo *.one",
                "echo '*.one'",
            ]
        )
        output = strip_ansi(result.stdout)
        assert result.returncode == 0
        assert_contains(output, "hello world")
        assert_contains(output, "literal # kept")
        assert_contains(output, "visible")
        if "hidden" in output:
            raise AssertionError(f"comment text leaked into output:\n{output}")
        assert_contains(output, str(test.home))
        assert_contains(output, "alpha.one")
        assert_contains(output, "beta.one")
        assert_contains(output, "*.one")
    finally:
        test.cleanup()


def test_script_mode_and_source() -> None:
    test = ShellTest()
    try:
        script = test.root / "script.sh"
        script.write_text(
            "\n".join(
                [
                    "# script comment",
                    "echo script-start",
                    "SCRIPT_VAR=from-script",
                    "printenv SCRIPT_VAR",
                    "exit 7",
                ]
            )
            + "\n"
        )

        script_result = test.run_script(script)
        assert script_result.returncode == 7
        assert_contains(script_result.stdout, "script-start")
        assert_contains(script_result.stdout, "from-script")

        source_file = test.root / "source.sh"
        source_file.write_text(
            "\n".join(
                [
                    "SOURCED_VAR=from-source",
                    "alias sourced_hi='echo sourced-alias'",
                    "cd ~",
                ]
            )
            + "\n"
        )

        result = test.run_interactive(
            [
                f"source {source_file}",
                "printenv SOURCED_VAR",
                "sourced_hi",
                "pwd",
            ],
            cwd=Path("/"),
        )
        output = strip_ansi(result.stdout)
        assert result.returncode == 0
        assert_contains(output, "from-source")
        assert_contains(output, "sourced-alias")
        assert_contains(output, str(test.home))
    finally:
        test.cleanup()


def test_which_and_exec_status_codes() -> None:
    test = ShellTest()
    try:
        noexec = test.work / "noexec.sh"
        noexec.write_text("#!/bin/sh\necho should-not-run\n")
        noexec.chmod(stat.S_IRUSR | stat.S_IWUSR | stat.S_IRGRP | stat.S_IROTH)

        result = test.run_interactive(
            [
                "which sh",
                "which definitely_missing_simpleshell_command",
                "echo which-status:$?",
                "definitely_missing_simpleshell_command",
                "echo missing-status:$?",
                "./noexec.sh",
                "echo noexec-status:$?",
            ]
        )
        output = strip_ansi(result.stdout)
        assert result.returncode == 0
        if not re.search(r"/sh\n", output):
            raise AssertionError(f"which sh did not print an executable path:\n{output}")
        assert_contains(output, "which-status:1")
        assert_contains(output, "missing-status:127")
        assert_contains(output, "noexec-status:126")
        if "should-not-run" in output:
            raise AssertionError(f"non-executable script unexpectedly ran:\n{output}")
    finally:
        test.cleanup()


def main() -> int:
    tests = [
        test_basic_execution_and_conditionals,
        test_builtin_redirection_aliases_and_env,
        test_expansion_quotes_globs_and_comments,
        test_script_mode_and_source,
        test_which_and_exec_status_codes,
    ]

    failures = 0
    for test in tests:
        try:
            test()
            print(f"PASS {test.__name__}")
        except Exception as error:
            failures += 1
            print(f"FAIL {test.__name__}: {error}")

    if failures:
        print(f"{failures} test(s) failed")
        return 1

    print(f"{len(tests)} test(s) passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
