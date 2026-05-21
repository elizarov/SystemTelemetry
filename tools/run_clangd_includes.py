#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import queue
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass
class FileState:
    index: int
    relative_path: str
    uri: str
    started_at: float
    allowed_lines: set[int]


@dataclass
class FileResult:
    index: int
    relative_path: str
    elapsed_seconds: float
    diagnostics: list[dict[str, Any]]
    timed_out: bool = False


class ClangdClient:
    def __init__(self, clangd: str, root: Path, compile_commands_dir: Path, workers: int) -> None:
        args = [
            clangd,
            f"--compile-commands-dir={compile_commands_dir}",
            "--background-index=false",
            "--enable-config",
            "--log=error",
            f"-j={workers}",
        ]
        self.process = subprocess.Popen(
            args,
            cwd=root,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.messages: queue.Queue[dict[str, Any] | None] = queue.Queue()
        self.stderr_lines: queue.Queue[str] = queue.Queue()
        self.next_id = 1
        self.write_lock = threading.Lock()
        self.reader = threading.Thread(target=self._read_stdout, daemon=True)
        self.stderr_reader = threading.Thread(target=self._read_stderr, daemon=True)
        self.reader.start()
        self.stderr_reader.start()

    def _read_stdout(self) -> None:
        assert self.process.stdout is not None
        stream = self.process.stdout
        while True:
            headers: dict[str, str] = {}
            while True:
                line = stream.readline()
                if not line:
                    self.messages.put(None)
                    return
                if line in (b"\r\n", b"\n"):
                    break
                key, _, value = line.decode("ascii", errors="replace").partition(":")
                headers[key.lower()] = value.strip()

            content_length = int(headers.get("content-length", "0"))
            if content_length <= 0:
                continue
            body = stream.read(content_length)
            if not body:
                self.messages.put(None)
                return
            self.messages.put(json.loads(body.decode("utf-8")))

    def _read_stderr(self) -> None:
        assert self.process.stderr is not None
        for raw_line in self.process.stderr:
            self.stderr_lines.put(raw_line.decode("utf-8", errors="replace").rstrip())

    def send(self, payload: dict[str, Any]) -> None:
        assert self.process.stdin is not None
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        header = f"Content-Length: {len(body)}\r\n\r\n".encode("ascii")
        with self.write_lock:
            self.process.stdin.write(header)
            self.process.stdin.write(body)
            self.process.stdin.flush()

    def request(self, method: str, params: Any) -> int:
        request_id = self.next_id
        self.next_id += 1
        self.send({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params})
        return request_id

    def shutdown(self) -> None:
        try:
            shutdown_id = self.request("shutdown", None)
            deadline = time.monotonic() + 5.0
            while time.monotonic() < deadline:
                try:
                    message = self.messages.get(timeout=0.2)
                except queue.Empty:
                    continue
                if message is None:
                    break
                if message.get("id") == shutdown_id:
                    break
            self.send({"jsonrpc": "2.0", "method": "exit", "params": {}})
        except (BrokenPipeError, OSError):
            pass
        finally:
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()


def path_to_uri(path: Path) -> str:
    return path.resolve().as_uri()


def line_filter_lines(line_filter: str) -> set[int]:
    try:
        payload = json.loads(line_filter)
    except json.JSONDecodeError:
        return set()

    lines: set[int] = set()
    for entry in payload:
        for start, end in entry.get("lines", []):
            lines.update(range(int(start), int(end) + 1))
    return lines


def is_unused_include_diagnostic(diagnostic: dict[str, Any]) -> bool:
    code = str(diagnostic.get("code", ""))
    message = str(diagnostic.get("message", ""))
    return code == "unused-includes" or "is not used directly" in message


def is_error_diagnostic(diagnostic: dict[str, Any]) -> bool:
    return diagnostic.get("severity") == 1


def diagnostic_line(diagnostic: dict[str, Any]) -> int:
    return int(diagnostic.get("range", {}).get("start", {}).get("line", 0)) + 1


def diagnostic_column(diagnostic: dict[str, Any]) -> int:
    return int(diagnostic.get("range", {}).get("start", {}).get("character", 0)) + 1


def filter_diagnostics(diagnostics: list[dict[str, Any]], allowed_lines: set[int]) -> list[dict[str, Any]]:
    filtered: list[dict[str, Any]] = []
    for diagnostic in diagnostics:
        if is_unused_include_diagnostic(diagnostic):
            if diagnostic_line(diagnostic) in allowed_lines:
                filtered.append(diagnostic)
            continue
        if is_error_diagnostic(diagnostic):
            filtered.append(diagnostic)
    return filtered


def format_diagnostic(relative_path: str, diagnostic: dict[str, Any]) -> str:
    severity = "error" if is_error_diagnostic(diagnostic) else "warning"
    source = diagnostic.get("source", "clangd")
    code = diagnostic.get("code", "")
    suffix = f" [{source}"
    if code:
        suffix += f":{code}"
    suffix += "]"
    return (
        f"{relative_path}:{diagnostic_line(diagnostic)}:{diagnostic_column(diagnostic)}: "
        f"{severity}: {diagnostic.get('message', '')}{suffix}"
    )


def truncate_progress_line(prefix: str, relative_path: str, suffix: str) -> str:
    columns = shutil.get_terminal_size((120, 20)).columns
    if columns <= 1:
        return f"{prefix}{relative_path}{suffix}"
    max_length = columns - 1
    full_line = f"{prefix}{relative_path}{suffix}"
    if len(full_line) <= max_length:
        return full_line
    path_budget = max_length - len(prefix) - len(suffix)
    if path_budget <= 3:
        return full_line[:max_length]
    return f"{prefix}...{relative_path[-(path_budget - 3):]}{suffix}"


class ProgressLine:
    def __init__(self, enabled: bool) -> None:
        self.enabled = enabled
        self.previous_length = 0

    def update(self, completed: int, total: int, relative_path: str, elapsed_seconds: float) -> None:
        if not self.enabled:
            return
        progress = truncate_progress_line(
            f"[{completed}/{total}] include-lint ",
            relative_path,
            f" in {elapsed_seconds:.2f}s",
        )
        padding = " " * max(0, self.previous_length - len(progress))
        sys.stdout.write(f"\r{progress}{padding}")
        sys.stdout.flush()
        self.previous_length = len(progress)

    def finish(self) -> None:
        if not self.enabled or self.previous_length <= 0:
            return
        sys.stdout.write(f"\r{' ' * self.previous_length}\r")
        sys.stdout.flush()
        self.previous_length = 0


def initialize(client: ClangdClient, root: Path, timeout_seconds: int) -> None:
    request_id = client.request(
        "initialize",
        {
            "processId": None,
            "rootUri": path_to_uri(root),
            "capabilities": {
                "textDocument": {
                    "publishDiagnostics": {
                        "tagSupport": {
                            "valueSet": [1, 2],
                        }
                    }
                }
            },
            "initializationOptions": {},
        },
    )
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        try:
            message = client.messages.get(timeout=0.2)
        except queue.Empty:
            continue
        if message is None:
            raise RuntimeError("clangd exited during initialize")
        if message.get("id") == request_id:
            if "error" in message:
                raise RuntimeError(f"clangd initialize failed: {message['error']}")
            client.send({"jsonrpc": "2.0", "method": "initialized", "params": {}})
            return
    raise TimeoutError("clangd initialize timed out")


def run(args: argparse.Namespace) -> int:
    root = Path(args.root).resolve()
    compile_commands_dir = Path(args.compile_commands_dir).resolve()
    manifest_path = Path(args.manifest).resolve()
    report_path = Path(args.report).resolve()

    with manifest_path.open("r", encoding="utf-8-sig") as handle:
        manifest = json.load(handle)

    files = []
    for index, item in enumerate(manifest):
        relative_path = item["relativePath"]
        full_path = root / relative_path
        files.append(
            FileState(
                index=index,
                relative_path=relative_path,
                uri=path_to_uri(full_path),
                started_at=0.0,
                allowed_lines=line_filter_lines(item["lineFilter"]),
            )
        )

    if not files:
        print("No files were provided to clangd include lint.")
        return 0

    report_path.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    client = ClangdClient(args.clangd, root, compile_commands_dir, args.parallelism)
    try:
        initialize(client, root, args.timeout_seconds)
        print(
            f"Running clangd include-cleaner unused-include sweep for {len(files)} "
            f"source and header files with parallelism={args.parallelism}..."
        )
        print(f"Writing full clangd output to {report_path}")

        next_file = 0
        active: dict[str, FileState] = {}
        completed: list[FileResult] = []
        progress = ProgressLine(sys.stdout.isatty())
        failed = False

        with report_path.open("w", encoding="utf-8", newline="\n") as report:
            report.write("clangd include-cleaner report\n")
            report.write(f"Scope: {args.scope}\n")
            report.write("Check set: includes\n")
            report.write(f"Max parallel: {args.parallelism}\n")
            report.write(f"Timeout seconds: {args.timeout_seconds}\n")
            report.write(f"Generated: {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
            report.write(f"Using clangd: {args.clangd}\n")
            report.write(f"Build database: {compile_commands_dir}\n")
            report.write("Line filter: include directives only\n\n")

            def open_next_files() -> None:
                nonlocal next_file
                while next_file < len(files) and len(active) < args.parallelism:
                    state = files[next_file]
                    next_file += 1
                    state.started_at = time.monotonic()
                    text = (root / state.relative_path).read_text(encoding="utf-8")
                    client.send(
                        {
                            "jsonrpc": "2.0",
                            "method": "textDocument/didOpen",
                            "params": {
                                "textDocument": {
                                    "uri": state.uri,
                                    "languageId": "cpp",
                                    "version": 1,
                                    "text": text,
                                }
                            },
                        }
                    )
                    active[state.uri] = state

            open_next_files()
            while active or next_file < len(files):
                now = time.monotonic()
                for uri, state in list(active.items()):
                    if now - state.started_at >= args.timeout_seconds:
                        result = FileResult(
                            index=state.index,
                            relative_path=state.relative_path,
                            elapsed_seconds=now - state.started_at,
                            diagnostics=[],
                            timed_out=True,
                        )
                        completed.append(result)
                        failed = True
                        del active[uri]
                        progress.update(len(completed), len(files), state.relative_path, result.elapsed_seconds)
                        client.send(
                            {
                                "jsonrpc": "2.0",
                                "method": "textDocument/didClose",
                                "params": {"textDocument": {"uri": state.uri}},
                            }
                        )

                open_next_files()

                try:
                    message = client.messages.get(timeout=0.2)
                except queue.Empty:
                    continue
                if message is None:
                    failed = True
                    print("clangd exited before all diagnostics were received.")
                    break
                if message.get("method") != "textDocument/publishDiagnostics":
                    continue

                uri = message.get("params", {}).get("uri")
                if uri not in active:
                    continue

                state = active.pop(uri)
                elapsed = time.monotonic() - state.started_at
                diagnostics = filter_diagnostics(message.get("params", {}).get("diagnostics", []), state.allowed_lines)
                result = FileResult(state.index, state.relative_path, elapsed, diagnostics)
                completed.append(result)
                if diagnostics:
                    failed = True
                progress.update(len(completed), len(files), state.relative_path, elapsed)

                report.write("===============================================================================\n")
                report.write(f"[{state.index + 1}/{len(files)}] {state.relative_path}\n")
                report.write(f"Elapsed seconds: {elapsed:.2f}\n")
                for diagnostic in diagnostics:
                    report.write(format_diagnostic(state.relative_path, diagnostic) + "\n")
                report.write("\n")
                report.flush()
                client.send(
                    {
                        "jsonrpc": "2.0",
                        "method": "textDocument/didClose",
                        "params": {"textDocument": {"uri": state.uri}},
                    }
                )
                open_next_files()

            for result in completed:
                if result.timed_out:
                    report.write("===============================================================================\n")
                    report.write(f"[{result.index + 1}/{len(files)}] {result.relative_path}\n")
                    report.write(f"Elapsed seconds: {result.elapsed_seconds:.2f}\n")
                    report.write(f"Timed out after {args.timeout_seconds} seconds\n\n")

            total_seconds = time.monotonic() - started
            report.write(f"Total elapsed seconds: {total_seconds:.2f}\n")
            progress.finish()

        if failed:
            print("clangd include lint failed.")
            print(f"Full clangd report: {report_path}")
            return 1

        print("clangd include lint passed.")
        print(f"Full clangd report: {report_path}")
        return 0
    finally:
        client.shutdown()


def main() -> int:
    parser = argparse.ArgumentParser(description="Run clangd unused-include diagnostics for CaseDash files.")
    parser.add_argument("--root", required=True)
    parser.add_argument("--clangd", required=True)
    parser.add_argument("--compile-commands-dir", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--scope", required=True)
    parser.add_argument("--parallelism", type=int, required=True)
    parser.add_argument("--timeout-seconds", type=int, required=True)
    args = parser.parse_args()
    try:
        return run(args)
    except Exception as exc:  # noqa: BLE001 - command-line tool should surface concise failures.
        print(f"clangd include lint failed to run: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
