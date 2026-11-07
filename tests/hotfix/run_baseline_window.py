"""Exercise hot reload while the real example_baseline window renders."""

from __future__ import annotations

import argparse
from collections import deque
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
from queue import Empty, Queue
import re
import signal
import subprocess
import threading
import time


TARGET = "horizon-baseline-live-patch"
FAILURE = "BASELINE_LIVE_INTENTIONAL_COMPILE_FAILURE"
STATUS = re.compile(
    r"\[baseline live\] label=(\S+) frame=(\d+) object=((?:0x)?[0-9a-fA-F]+) "
    r"previous=([01]) next=([01]) working=([01]) title=(.*)$"
)
IMPLEMENTATION = re.compile(r"\[baseline hotfix\] implementation=v(\d+) reload_count=(\d+)")


@dataclass(frozen=True)
class Status:
    label: str
    frame: int
    address: int
    previous: bool
    next: bool
    title: str
    implementation: int
    restores: int


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


class Baseline:
    def __init__(self, executable: Path, environment: dict[str, str], directory: Path) -> None:
        self.control = directory / "control.txt"
        self.temporary = directory / "control.tmp"
        self.log = directory / "baseline.log"
        self.control.unlink(missing_ok=True)
        self.temporary.unlink(missing_ok=True)
        self.lines: Queue[str | None] = Queue()
        self.tail: deque[str] = deque(maxlen=20)
        process_options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {"start_new_session": True}
        self.process = subprocess.Popen(
            [str(executable)], cwd=executable.parent, env=environment,
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace", **process_options,
        )
        self.reader = threading.Thread(target=self._drain_output, daemon=True)
        self.reader.start()

    def _drain_output(self) -> None:
        try:
            with self.log.open("w", encoding="utf-8") as output, self.process.stdout as pipe:
                output.write(f"pid={self.process.pid} command={self.process.args!r}\n")
                for line in pipe:
                    output.write(line)
                    output.flush()
                    self.lines.put(line.rstrip("\r\n"))
        finally:
            self.lines.put(None)

    def error(self, message: str) -> RuntimeError:
        return RuntimeError(f"{message}\nLog: {self.log}\nRecent output:\n" + "\n".join(self.tail))

    def require_alive(self) -> None:
        if self.process.poll() is not None:
            raise self.error(f"The baseline window exited with code {self.process.returncode}")

    def send(self, command: str, deadline: float) -> None:
        while self.control.exists():
            self.require_alive()
            if time.monotonic() >= deadline:
                raise self.error(f"previous command was not consumed before {command}")
            time.sleep(0.01)
        self.require_alive()
        self.temporary.write_text(f"{command}\n", encoding="ascii")
        self.temporary.replace(self.control)

    def line(self, deadline: float) -> str:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise self.error("timed out waiting for baseline window output")
        try:
            line = self.lines.get(timeout=remaining)
        except Empty:
            raise self.error("timed out waiting for baseline window output") from None
        if line is None:
            raise self.error(f"Baseline window output closed, exit code {self.process.poll()}")
        self.tail.append(line)
        return line

    def until(self, marker: str, deadline: float) -> None:
        while marker not in self.line(deadline):
            pass

    def idle(self, deadline: float) -> Status:
        while time.monotonic() < deadline:
            self.send("status", deadline)
            header = None
            while True:
                line = self.line(deadline)
                if match := STATUS.search(line):
                    header = match
                elif header and (implementation := IMPLEMENTATION.search(line)):
                    break
            label, frame, address, previous, next_, working, title = header.groups()
            generation, restores = implementation.groups()
            if working == "0":
                return Status(label, int(frame), int(address, 16), previous == "1", next_ == "1",
                              title, int(generation), int(restores))
            time.sleep(0.05)
        raise self.error("HotfixSystem did not become idle")

    def close(self) -> None:
        try:
            if self.process.poll() is None:
                try:
                    self.send("quit", time.monotonic() + 2)
                    self.process.wait(timeout=5)
                except (OSError, RuntimeError, subprocess.TimeoutExpired):
                    if os.name == "nt":
                        self.process.kill()
                    else:
                        try:
                            os.killpg(self.process.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                    self.process.wait(timeout=5)
        finally:
            self.reader.join(timeout=5)
            self.control.unlink(missing_ok=True)
            self.temporary.unlink(missing_ok=True)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def staged_files(root: Path) -> dict[Path, tuple[int, str]]:
    return {path: (path.stat().st_mtime_ns, digest(path)) for path in root.rglob("*") if path.is_file()}


def write_source(source: Path, contents: bytes) -> None:
    before = source.stat().st_mtime_ns
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        source.write_bytes(contents)
        if source.stat().st_mtime_ns != before:
            return
        time.sleep(0.02)
    raise RuntimeError("the watched source timestamp did not change")


def check_status(app: Baseline, state: Status, generation: int, restores: int,
                 *, previous: bool, next_: bool, before: Status | None = None) -> None:
    label = f"baseline-live-v{generation}"
    title = f"example_baseline | {label}"
    expected = (label, title, generation, restores, previous, next_)
    actual = (state.label, state.title, state.implementation, state.restores, state.previous, state.next)
    require(actual == expected, f"unexpected baseline status: {state}")
    require(state.address != 0 and (before is None or state.frame > before.frame),
            f"the window did not keep rendering a live object: {state}")
    app.require_alive()
    print(f"pid={app.process.pid} title={state.title!r} frame={state.frame} object={state.address:#x} "
          f"implementation=v{generation} reload_count={restores} previous={int(previous)} next={int(next_)}",
          flush=True)


def exercise(app: Baseline, source: Path, original: bytes, build: Path, config: str, timeout: float) -> None:
    stage_root = build / "hotfix-staged"
    manifest = build / "hotfix-manifests" / TARGET / config / "module.txt"

    def reload(contents: bytes, failure: str | None = None) -> Status:
        write_source(source, contents)
        deadline = time.monotonic() + timeout
        app.send("reload", deadline)
        app.until("source change detected; rebuilding", deadline)
        if failure:
            app.until(failure, deadline)
        return app.idle(deadline)

    active = app.idle(time.monotonic() + timeout)
    check_status(app, active, 1, 0, previous=False, next_=False)
    last_digest = None
    for generation in (2, 3):
        staged_before = staged_files(stage_root)
        revision = original.replace(b"implementation=v1", f"implementation=v{generation}".encode("ascii"))
        revision = revision.replace(b"baseline-live-v1", f"baseline-live-v{generation}".encode("ascii"))
        updated = reload(revision)
        check_status(app, updated, generation, generation - 1, previous=True, next_=False, before=active)
        require(updated.address != active.address, "successful update did not replace the live object")
        artifact = Path(manifest.read_text(encoding="utf-8").strip()).resolve(strict=True)
        artifact_digest = digest(artifact)
        require(artifact_digest != last_digest, "successive builds returned the same module artifact")
        last_digest = artifact_digest

        staged_after = staged_files(stage_root)
        added = sorted(staged_after.keys() - staged_before.keys())
        require(len(added) == 1 and all(staged_after.get(path) == info for path, info in staged_before.items()),
                f"expected one new staged module and preserved older modules, added: {added}")
        staged = added[0]
        require(staged.name == artifact.name and staged.parent.name.startswith("generation_") and
                not artifact.samefile(staged) and staged_after[staged][1] == artifact_digest,
                f"the new module was not staged as a separate copy: {staged}")
        print(f"staged={staged}", flush=True)
        active = updated

    deadline = time.monotonic() + timeout
    app.send("previous", deadline)
    rolled_back = app.idle(deadline)
    check_status(app, rolled_back, 2, 3, previous=True, next_=True, before=active)
    require(rolled_back.address != active.address, "rollback did not replace the live object")

    staged_before = staged_files(stage_root)
    artifact_time = artifact.stat().st_mtime_ns
    unchanged = reload(original + f"\n#error {FAILURE}\n".encode("ascii"), FAILURE)
    check_status(app, unchanged, 2, 3, previous=True, next_=True, before=rolled_back)
    require(unchanged.address == rolled_back.address, "compile failure changed the current live object")
    require(staged_files(stage_root) == staged_before and artifact.stat().st_mtime_ns == artifact_time and
            digest(artifact) == last_digest, "compile failure changed staged modules or the last successful artifact")
    print(f"compile failure retained the live object, title, artifacts and history: {FAILURE}", flush=True)

    deadline = time.monotonic() + timeout
    app.send("next", deadline)
    latest = app.idle(deadline)
    check_status(app, latest, 3, 4, previous=True, next_=False, before=unchanged)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("build_directory", type=Path)
    parser.add_argument("configuration")
    parser.add_argument("scratch_source", type=Path)
    parser.add_argument("original_source", type=Path)
    parser.add_argument("cmake", type=Path)
    parser.add_argument("--timeout", type=float, default=90)
    args = parser.parse_args()

    build = args.build_directory.resolve(strict=True)
    source = args.scratch_source.resolve(strict=True)
    original_source = args.original_source.resolve(strict=True)
    require(source.is_relative_to(build) and not source.samefile(original_source),
            "the watched source must be an independent copy in the build directory")
    original = original_source.read_bytes()
    require(original.count(b"implementation=v1") == 1 and original.count(b"baseline-live-v1") == 1,
            "the pristine source must contain exactly one implementation and window title marker")
    directory = build / "baseline-window-test"
    directory.mkdir(exist_ok=True)
    environment = dict(os.environ)
    build_environment = build / "CMakeFiles/horizon-build-environment.json"
    if build_environment.is_file():
        environment.update(json.loads(build_environment.read_text(encoding="utf-8")))
    environment.update({
        "HORIZON_DEV_BOOTSTRAP_ACTIVE": "1",
        "HORIZON_BASELINE_LIVE_CMAKE": str(args.cmake.resolve(strict=True)),
        "HORIZON_BASELINE_LIVE_BUILD": str(build),
        "HORIZON_BASELINE_LIVE_CONFIG": args.configuration,
        "HORIZON_BASELINE_LIVE_CONTROL": str(directory / "control.txt"),
        "HORIZON_BASELINE_LIVE_SOURCE": str(source),
    })

    write_source(source, original)
    app = Baseline(args.executable.resolve(strict=True), environment, directory)
    try:
        print(f"running {app.process.args!r}, pid={app.process.pid}, log={app.log}", flush=True)
        exercise(app, source, original, build, args.configuration, args.timeout)
        app.send("quit", time.monotonic() + 5)
        require(app.process.wait(timeout=10) == 0, "the baseline process did not shut down cleanly")
    finally:
        try:
            app.close()
        finally:
            source.write_bytes(original)
    print(f"PASS baseline hotfix update, rollback and failed compile on pid={app.process.pid}", flush=True)


if __name__ == "__main__":
    main()
