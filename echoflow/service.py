#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later
"""EchoFlow service core.

The fcitx addon owns input-context events and sends small commands here. This
service owns the push-to-talk state machine, recording, ASR, and text commit.
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import os
import signal
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from datetime import datetime
from enum import Enum
from pathlib import Path
from typing import Any, Callable, Optional, Protocol


class SessionState(str, Enum):
    IDLE = "idle"
    WAITING_FOR_HOLD = "waiting-for-hold"
    RECORDING = "recording"
    TRANSCRIBING = "transcribing"


@dataclasses.dataclass(frozen=True)
class Config:
    recordings_dir: str
    asr_runner: str
    asr_project_dir: str
    model_dir: str
    model_name: str
    language: Optional[str]
    asr_timeout_seconds: int
    hold_threshold_ms: int
    min_record_seconds: float
    pw_record: dict[str, Any]
    fcitx_commit: bool
    fcitx_socket: Optional[str]
    control_socket: Optional[str]
    ui_socket: Optional[str]
    notify: bool
    notify_timeout_ms: int
    strip_trailing_punctuation: bool

    @classmethod
    def default(cls) -> "Config":
        home = Path.home()
        return cls(
            recordings_dir=str(home / ".local/share/echoflow/recordings"),
            asr_runner="qwen-asr-transcribe",
            asr_project_dir=str(home / "AI/Model/Qwen3-ASR-GGUF"),
            model_dir=str(home / "AI/Model/Qwen3-ASR-GGUF/model-0.6B"),
            model_name="qwen-asr-0.6b",
            language="Chinese",
            asr_timeout_seconds=120,
            hold_threshold_ms=350,
            min_record_seconds=0.25,
            pw_record={"rate": 16000, "channels": 1, "format": "s16"},
            fcitx_commit=True,
            fcitx_socket=None,
            control_socket=None,
            ui_socket=None,
            notify=True,
            notify_timeout_ms=1200,
            strip_trailing_punctuation=False,
        )


@dataclasses.dataclass(frozen=True)
class RuntimeCheck:
    name: str
    passed: bool
    detail: str


class RecorderProtocol(Protocol):
    def start(self) -> None: ...
    def stop(self) -> Optional[Path]: ...


class AsrProtocol(Protocol):
    def transcribe(self, audio_path: Path) -> str: ...


class CommitterProtocol(Protocol):
    def commit_text(self, text: str) -> tuple[bool, str]: ...


class UiNotifierProtocol(Protocol):
    def send(self, message: str) -> None: ...


REQUIRED_MODEL_FILES = (
    "qwen3_asr_encoder_frontend.int4.onnx",
    "qwen3_asr_encoder_backend.int4.onnx",
    "qwen3_asr_llm.q4_k.gguf",
)


def runtime_dir() -> Path:
    xdg = os.environ.get("XDG_RUNTIME_DIR")
    if xdg and os.access(xdg, os.W_OK | os.X_OK):
        return Path(xdg)
    run_user = Path("/run/user") / str(os.getuid())
    if run_user.exists() and os.access(run_user, os.W_OK | os.X_OK):
        return run_user
    return Path(tempfile.gettempdir())


def expand_path(value: Any, base_dir: Path) -> str:
    expanded = Path(os.path.expandvars(os.path.expanduser(str(value))))
    if not expanded.is_absolute():
        expanded = base_dir / expanded
    return str(expanded)


def load_config(path: Path) -> Config:
    path = path.expanduser()
    if not path.is_absolute():
        path = Path.cwd() / path
    raw = json.loads(path.read_text(encoding="utf-8"))
    base = path.parent
    defaults = dataclasses.asdict(Config.default())
    defaults.update(raw)
    for key in {"recordings_dir", "asr_project_dir", "model_dir", "fcitx_socket", "control_socket", "ui_socket"}:
        if defaults.get(key) is not None:
            defaults[key] = expand_path(defaults[key], base)
    allowed = {field.name for field in dataclasses.fields(Config)}
    return Config(**{key: value for key, value in defaults.items() if key in allowed})


def control_socket_path(cfg: Config) -> Path:
    if cfg.control_socket:
        return Path(cfg.control_socket).expanduser()
    return runtime_dir() / "echoflow-control.sock"


def fcitx_socket_path(cfg: Config) -> Path:
    if cfg.fcitx_socket:
        return Path(cfg.fcitx_socket).expanduser()
    return runtime_dir() / "echoflow-fcitx.sock"


def ui_socket_path(cfg: Config) -> Path:
    if cfg.ui_socket:
        return Path(cfg.ui_socket).expanduser()
    return runtime_dir() / "echoflow-ui.sock"


def log(message: str) -> None:
    stamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print(f"[{stamp}] {message}", flush=True)


def strip_punctuation(text: str) -> str:
    return text.rstrip("。．.，,、！？!?；;：:\n\r\t ")


def notify(cfg: Config, title: str, body: str = "") -> None:
    if not cfg.notify:
        return
    args = ["notify-send", "--app-name", "EchoFlow", "--expire-time", str(cfg.notify_timeout_ms), title]
    if body:
        args.append(body)
    subprocess.run(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)


def command_available(command: str) -> bool:
    if "/" in command:
        path = Path(command).expanduser()
        return path.exists() and os.access(path, os.X_OK)
    return shutil.which(command) is not None


def can_create_directory(path: Path) -> bool:
    candidate = path.expanduser()
    while not candidate.exists() and candidate != candidate.parent:
        candidate = candidate.parent
    return candidate.exists() and os.access(candidate, os.W_OK | os.X_OK)


def model_dir_candidates(model_dir: Path) -> list[Path]:
    candidates = [model_dir]
    if model_dir.name == "model-0.6B":
        candidates.append(model_dir.parent / "model")
    elif model_dir.name == "model":
        candidates.append(model_dir.parent / "model-0.6B")
    return candidates


def resolve_model_dir(cfg: Config) -> Path:
    configured = Path(cfg.model_dir).expanduser()
    for candidate in model_dir_candidates(configured):
        if candidate.exists():
            return candidate
    return configured


def missing_model_files(model_dir: Path) -> list[str]:
    return [
        filename for filename in REQUIRED_MODEL_FILES
        if not (model_dir / filename).exists()
    ]


def qwen_package_path(project_dir: Path) -> Path:
    return project_dir / "qwen_asr_gguf" / "inference"


def llama_library_dir(project_dir: Path) -> Path:
    return qwen_package_path(project_dir) / "bin"


def has_shared_library(directory: Path, pattern: str) -> bool:
    return directory.exists() and any(path.is_file() for path in directory.glob(pattern))


def missing_llama_libraries(project_dir: Path) -> list[str]:
    directory = llama_library_dir(project_dir)
    missing = []
    if not has_shared_library(directory, "libllama*.so*"):
        missing.append("libllama*.so*")
    if not has_shared_library(directory, "libggml*.so*"):
        missing.append("libggml*.so*")
    return missing


def runtime_checks(cfg: Config) -> list[RuntimeCheck]:
    model_dir = resolve_model_dir(cfg)
    project_dir = Path(cfg.asr_project_dir).expanduser()
    package_dir = qwen_package_path(project_dir)
    missing_libs = missing_llama_libraries(project_dir)
    recordings_dir = Path(cfg.recordings_dir).expanduser()
    checks = [
        RuntimeCheck(
            "recordings dir can be created",
            can_create_directory(recordings_dir),
            str(recordings_dir),
        ),
        RuntimeCheck(
            "asr runner available",
            command_available(cfg.asr_runner),
            cfg.asr_runner,
        ),
        RuntimeCheck(
            "asr project dir exists",
            project_dir.exists(),
            str(project_dir),
        ),
        RuntimeCheck(
            "qwen package import path",
            (package_dir / "__init__.py").exists(),
            str(package_dir),
        ),
        RuntimeCheck(
            "model dir exists",
            model_dir.exists(),
            str(model_dir),
        ),
        RuntimeCheck(
            "model files exist",
            model_dir.exists() and not missing_model_files(model_dir),
            "missing: " + ", ".join(missing_model_files(model_dir))
            if model_dir.exists() and missing_model_files(model_dir)
            else str(model_dir),
        ),
        RuntimeCheck(
            "pw-record available",
            shutil.which("pw-record") is not None,
            "pw-record",
        ),
        RuntimeCheck(
            "llama.cpp shared libraries",
            not missing_libs,
            str(llama_library_dir(project_dir)) if not missing_libs
            else f"{llama_library_dir(project_dir)} missing: {', '.join(missing_libs)}",
        ),
    ]
    if cfg.notify:
        checks.append(RuntimeCheck(
            "notify-send available",
            shutil.which("notify-send") is not None,
            "notify-send",
        ))
    checks.extend([
        RuntimeCheck(
            "fcitx commit socket path",
            fcitx_socket_path(cfg).parent.exists(),
            str(fcitx_socket_path(cfg)),
        ),
        RuntimeCheck(
            "ui socket path",
            ui_socket_path(cfg).parent.exists(),
            str(ui_socket_path(cfg)),
        ),
    ])
    return checks


def self_test(cfg: Config) -> int:
    ok = True
    for check in runtime_checks(cfg):
        marker = "OK" if check.passed else "FAIL"
        print(f"[{marker}] {check.name}: {check.detail}")
        ok = ok and check.passed
    return 0 if ok else 1


def transcribe_file(cfg: Config, audio_path: Path) -> int:
    audio_path = audio_path.expanduser()
    if not audio_path.exists():
        print(f"audio file does not exist: {audio_path}", file=sys.stderr)
        return 2
    text = QwenAsrRunner(cfg).transcribe(audio_path)
    print(text)
    return 0


class PipeWireRecorder:
    def __init__(self, cfg: Config, *, clock: Callable[[], float] = time.monotonic) -> None:
        self.cfg = cfg
        self.clock = clock
        self.proc: Optional[subprocess.Popen] = None
        self.path: Optional[Path] = None
        self.started_at = 0.0

    def start(self) -> None:
        if self.proc is not None:
            return
        recordings = Path(self.cfg.recordings_dir)
        recordings.mkdir(parents=True, exist_ok=True)
        self.path = recordings / f"voice-{datetime.now().strftime('%Y%m%d-%H%M%S-%f')}.wav"
        rec = self.cfg.pw_record
        args = [
            "pw-record",
            "--rate",
            str(rec.get("rate", 16000)),
            "--channels",
            str(rec.get("channels", 1)),
            "--format",
            str(rec.get("format", "s16")),
            str(self.path),
        ]
        self.proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        self.started_at = self.clock()
        log(f"recording started: {self.path}")

    def stop(self) -> Optional[Path]:
        if self.proc is None:
            return None
        elapsed = self.clock() - self.started_at
        proc = self.proc
        path = self.path
        self.proc = None
        self.path = None
        proc.send_signal(signal.SIGINT)
        try:
            _stdout, stderr = proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            proc.terminate()
            _stdout, stderr = proc.communicate(timeout=3)
        if elapsed < self.cfg.min_record_seconds:
            log(f"recording too short ({elapsed:.2f}s)")
            return None
        recording_valid = path is not None and path.exists() and path.stat().st_size >= 1024
        if not recording_valid:
            if proc.returncode not in (0, -signal.SIGINT, 130, None):
                log(f"pw-record exited with {proc.returncode}: {(stderr or '').strip()}")
            log("recording missing or too small")
            return None
        return path


class QwenAsrRunner:
    def __init__(self, cfg: Config) -> None:
        self.cfg = cfg

    def transcribe(self, audio_path: Path) -> str:
        args = [
            self.cfg.asr_runner,
            "--project",
            self.cfg.asr_project_dir,
            "--model",
            str(resolve_model_dir(self.cfg)),
            "--model-name",
            self.cfg.model_name,
        ]
        if self.cfg.language:
            args.extend(["--language", self.cfg.language])
        args.append(str(audio_path))
        result = subprocess.run(
            args,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=self.cfg.asr_timeout_seconds,
            check=False,
        )
        if result.returncode != 0:
            raise RuntimeError(f"ASR runner failed: {result.stderr.strip()}")
        text = result.stdout.strip()
        return strip_punctuation(text) if self.cfg.strip_trailing_punctuation else text


class FcitxCommitter:
    def __init__(self, cfg: Config) -> None:
        self.cfg = cfg

    def commit_text(self, text: str) -> tuple[bool, str]:
        if not self.cfg.fcitx_commit:
            return False, "fcitx disabled"
        server = fcitx_socket_path(self.cfg)
        client = runtime_dir() / f"echoflow-client-{os.getpid()}-{time.monotonic_ns()}.sock"
        try:
            client.unlink(missing_ok=True)
            with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as sock:
                sock.settimeout(0.5)
                sock.bind(str(client))
                sock.sendto(b"COMMIT\n" + text.encode("utf-8"), str(server))
                data, _addr = sock.recvfrom(256)
        except (OSError, socket.timeout) as exc:
            return False, f"{type(exc).__name__}: {exc}"
        finally:
            client.unlink(missing_ok=True)
        reply = data.decode("utf-8", errors="replace").strip()
        return (reply == "OK", reply)


class NullUiNotifier:
    def send(self, message: str) -> None:
        return


class UnixDatagramUiNotifier:
    def __init__(self, cfg: Config) -> None:
        self.path = ui_socket_path(cfg)

    def send(self, message: str) -> None:
        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as sock:
                sock.sendto(message.encode("utf-8"), str(self.path))
        except OSError as exc:
            log(f"ui notify failed ({self.path}): {exc}")


class VoiceSession:
    def __init__(
        self,
        cfg: Config,
        *,
        recorder: RecorderProtocol,
        asr: AsrProtocol,
        committer: CommitterProtocol,
        ui: UiNotifierProtocol | None = None,
        clock: Callable[[], float] = time.monotonic,
    ) -> None:
        self.cfg = cfg
        self.recorder = recorder
        self.asr = asr
        self.committer = committer
        self.ui = ui or NullUiNotifier()
        self.clock = clock
        self.state = SessionState.IDLE
        self.tooltip_visible = False
        self.ctrl_down_at: Optional[float] = None

    def handle_command(self, command: str) -> str:
        command = command.strip()
        verb, _sep, argument = command.partition(" ")
        verb = verb.upper()
        if verb == "FOCUS":
            self.tooltip_visible = True
            argument = argument.strip()
            suffix = f" {argument}" if argument else ""
            self.ui.send(f"SHOW_TOOLTIP{suffix} 长按 Ctrl 语音输入")
            return "TOOLTIP show"
        if verb == "BLUR":
            self.tooltip_visible = False
            self.state = SessionState.IDLE
            self.ctrl_down_at = None
            self.ui.send("HIDE_TOOLTIP")
            return "TOOLTIP hide"
        if verb == "CTRL_DOWN":
            if self.state == SessionState.RECORDING:
                return "RECORDING"
            self.ctrl_down_at = self.clock()
            self.state = SessionState.WAITING_FOR_HOLD
            return "WAITING_HOLD"
        if verb == "TICK":
            return self._maybe_start_recording()
        if verb == "CTRL_UP":
            if self.state == SessionState.WAITING_FOR_HOLD:
                self.state = SessionState.IDLE
                self.ctrl_down_at = None
                return "CANCELLED"
            if self.state == SessionState.RECORDING:
                return self._stop_transcribe_commit()
            return "IDLE"
        return "ERR unknown-command"

    def _maybe_start_recording(self) -> str:
        if self.state != SessionState.WAITING_FOR_HOLD or self.ctrl_down_at is None:
            return "IDLE"
        elapsed_ms = (self.clock() - self.ctrl_down_at) * 1000
        if elapsed_ms < self.cfg.hold_threshold_ms:
            return "IDLE"
        self.recorder.start()
        self.ui.send("RECORDING")
        notify(self.cfg, "正在录音", "松开 Ctrl 后转写")
        self.state = SessionState.RECORDING
        return "RECORDING"

    def _stop_transcribe_commit(self) -> str:
        self.state = SessionState.TRANSCRIBING
        self.ui.send("TRANSCRIBING")
        audio_path = self.recorder.stop()
        self.ctrl_down_at = None
        if audio_path is None:
            self.state = SessionState.IDLE
            self.ui.send("IDLE")
            notify(self.cfg, "语音输入已取消", "录音太短或没有音频")
            return "CANCELLED"
        text = self.asr.transcribe(audio_path)
        if not text:
            self.state = SessionState.IDLE
            self.ui.send("IDLE")
            notify(self.cfg, "语音输入完成", "未识别到文字")
            return "EMPTY"
        ok, detail = self.committer.commit_text(text)
        self.state = SessionState.IDLE
        self.ui.send("IDLE")
        notify(self.cfg, "语音输入完成" if ok else "语音输入提交失败", text[:80] if ok else detail[:80])
        return "COMMITTED" if ok else f"ERR {detail}"


ALLOWED_COMMANDS = {"FOCUS", "BLUR", "CTRL_DOWN", "CTRL_UP", "TICK"}


def handle_protocol_message(session: VoiceSession, payload: bytes) -> bytes:
    command = payload.decode("utf-8", errors="replace").strip()
    verb = command.split(maxsplit=1)[0].upper() if command else ""
    if verb not in ALLOWED_COMMANDS:
        return b"ERR unknown-command\n"
    return (session.handle_command(command) + "\n").encode("utf-8")


def serve(cfg: Config) -> int:
    recorder = PipeWireRecorder(cfg)
    asr = QwenAsrRunner(cfg)
    committer = FcitxCommitter(cfg)
    ui = UnixDatagramUiNotifier(cfg)
    session = VoiceSession(cfg, recorder=recorder, asr=asr, committer=committer, ui=ui)
    server = control_socket_path(cfg)
    server.parent.mkdir(parents=True, exist_ok=True)
    server.unlink(missing_ok=True)
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as sock:
        sock.bind(str(server))
        os.chmod(server, 0o600)
        log(f"EchoFlow service listening on {server}")
        while True:
            data, peer = sock.recvfrom(4096)
            reply = handle_protocol_message(session, data)
            if peer:
                try:
                    sock.sendto(reply, peer)
                except FileNotFoundError:
                    pass
                except OSError as exc:
                    log(f"reply to control peer failed ({peer}): {exc}")


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="EchoFlow offline voice input service")
    parser.add_argument("--config", type=Path, default=Path("config.json"))
    parser.add_argument("--print-default-config", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument(
        "--transcribe-file",
        type=Path,
        help="transcribe an existing audio file with the configured Qwen ASR runner and print the result",
    )
    args = parser.parse_args(argv)
    if args.print_default_config:
        print(json.dumps(dataclasses.asdict(Config.default()), ensure_ascii=False, indent=2))
        return 0
    cfg = load_config(args.config) if args.config.exists() else Config.default()
    if args.self_test:
        return self_test(cfg)
    if args.transcribe_file:
        return transcribe_file(cfg, args.transcribe_file)
    return serve(cfg)


if __name__ == "__main__":
    raise SystemExit(main())
