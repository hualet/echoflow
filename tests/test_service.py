# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

import dataclasses
import io
import json
import os
import socket
import sys
import tempfile
import time
import unittest
import subprocess
from pathlib import Path
from unittest import mock

from echoflow import service


class FakeClock:
    def __init__(self):
        self.now = 100.0

    def monotonic(self):
        return self.now

    def advance(self, seconds):
        self.now += seconds


class FakeRecorder:
    def __init__(self):
        self.started = 0
        self.stopped = 0
        self.path = Path("/tmp/echoflow-test.wav")

    def start(self):
        self.started += 1

    def stop(self):
        self.stopped += 1
        return self.path


class FakeAsr:
    def __init__(self, text="你好 deepin"):
        self.text = text
        self.audio_paths = []

    def transcribe(self, audio_path):
        self.audio_paths.append(audio_path)
        return self.text


class FakeCommitter:
    def __init__(self):
        self.texts = []

    def commit_text(self, text):
        self.texts.append(text)
        return True, "OK"


class FakeUiNotifier:
    def __init__(self):
        self.messages = []

    def send(self, message):
        self.messages.append(message)


class FakeRecorderProcess:
    def __init__(self, returncode=1, stderr="recorded.wav"):
        self.returncode = returncode
        self.stderr = stderr
        self.signals = []

    def send_signal(self, signal_number):
        self.signals.append(signal_number)

    def communicate(self, timeout=None):
        return "", self.stderr


class ConfigTests(unittest.TestCase):
    def test_default_config_targets_qwen_asr_06b(self):
        cfg = service.Config.default()

        self.assertEqual(cfg.model_name, "qwen-asr-0.6b")
        self.assertEqual(cfg.asr_runner, "qwen-asr-transcribe")
        self.assertEqual(cfg.asr_project_dir, str(Path.home() / "AI/Model/Qwen3-ASR-GGUF"))
        self.assertEqual(cfg.hold_threshold_ms, 350)
        self.assertTrue(cfg.fcitx_commit)

    def test_qwen_runner_invokes_configured_command(self):
        cfg = dataclasses.replace(
            service.Config.default(),
            asr_runner="/opt/echoflow/bin/qwen-asr-transcribe",
            asr_project_dir="/models/Qwen3-ASR-GGUF",
            model_dir="/models/qwen-asr-0.6b",
            language="Chinese",
        )

        with mock.patch.object(
            service.subprocess,
            "run",
            return_value=subprocess.CompletedProcess(
                [cfg.asr_runner],
                0,
                "  你好 deepin。\n",
                "",
            ),
        ) as run:
            text = service.QwenAsrRunner(cfg).transcribe(Path("/tmp/a.wav"))

        self.assertEqual(text, "你好 deepin。")
        run.assert_called_once_with(
            [
                "/opt/echoflow/bin/qwen-asr-transcribe",
                "--project",
                "/models/Qwen3-ASR-GGUF",
                "--model",
                "/models/qwen-asr-0.6b",
                "--model-name",
                "qwen-asr-0.6b",
                "--language",
                "Chinese",
                "/tmp/a.wav",
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=120,
            check=False,
        )

    def test_runtime_self_test_reports_ready_dependencies(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            project = root / "Qwen3-ASR-GGUF"
            model = project / "model-0.6B"
            package = project / "qwen_asr_gguf" / "inference"
            bin_dir = package / "bin"
            recordings = root / "echoflow-recordings"
            model.mkdir(parents=True)
            package.mkdir(parents=True)
            bin_dir.mkdir()
            for filename in service.REQUIRED_MODEL_FILES:
                (model / filename).write_text("x", encoding="utf-8")
            (package / "__init__.py").write_text("", encoding="utf-8")
            (bin_dir / "libllama.so").write_text("x", encoding="utf-8")
            (bin_dir / "libggml.so").write_text("x", encoding="utf-8")
            cfg = dataclasses.replace(
                service.Config.default(),
                recordings_dir=str(recordings),
                asr_project_dir=str(project),
                model_dir=str(model),
                asr_runner="qwen-asr-transcribe",
            )

            with (
                mock.patch.object(service.shutil, "which", side_effect=lambda command: f"/usr/bin/{command}"),
                mock.patch.object(service, "runtime_dir", return_value=root),
            ):
                checks = service.runtime_checks(cfg)

        self.assertTrue(all(check.passed for check in checks))
        self.assertIn("asr runner available", [check.name for check in checks])
        self.assertIn("asr project dir exists", [check.name for check in checks])
        self.assertIn("qwen package import path", [check.name for check in checks])
        self.assertIn("model dir exists", [check.name for check in checks])
        self.assertIn("llama.cpp shared libraries", [check.name for check in checks])
        self.assertIn("pw-record available", [check.name for check in checks])

    def test_runtime_self_test_accepts_creatable_recordings_dir(self):
        cfg = dataclasses.replace(
            service.Config.default(),
            recordings_dir="/tmp/echoflow/missing/recordings",
        )

        def fake_exists(path):
            return str(path) in {"/tmp", cfg.model_dir}

        def fake_access(path, mode):
            return str(path) == "/tmp"

        with (
            mock.patch.object(service.shutil, "which", return_value="/usr/bin/tool"),
            mock.patch.object(service.Path, "exists", fake_exists),
            mock.patch.object(service.os, "access", fake_access),
        ):
            checks = service.runtime_checks(cfg)

        recordings_check = next(
            check for check in checks
            if check.name == "recordings dir can be created"
        )
        self.assertTrue(recordings_check.passed)
        self.assertEqual(recordings_check.detail, "/tmp/echoflow/missing/recordings")

    def test_runtime_self_test_discovers_legacy_06b_model_dir(self):
        cfg = dataclasses.replace(
            service.Config.default(),
            model_dir="/models/Qwen3-ASR-GGUF/model-0.6B",
        )
        existing = {
            "/models/Qwen3-ASR-GGUF/model",
            "/models/Qwen3-ASR-GGUF/model/qwen3_asr_encoder_frontend.int4.onnx",
            "/models/Qwen3-ASR-GGUF/model/qwen3_asr_encoder_backend.int4.onnx",
            "/models/Qwen3-ASR-GGUF/model/qwen3_asr_llm.q4_k.gguf",
            "/tmp",
            "/run/user/1000",
        }

        def fake_exists(path):
            return str(path) in existing

        with (
            mock.patch.object(service.shutil, "which", return_value="/usr/bin/tool"),
            mock.patch.object(service.Path, "exists", fake_exists),
            mock.patch.object(service.os, "access", return_value=True),
            mock.patch.object(service, "runtime_dir", return_value=Path("/run/user/1000")),
        ):
            checks = service.runtime_checks(cfg)

        model_check = next(check for check in checks if check.name == "model dir exists")
        files_check = next(check for check in checks if check.name == "model files exist")
        self.assertTrue(model_check.passed)
        self.assertEqual(model_check.detail, "/models/Qwen3-ASR-GGUF/model")
        self.assertTrue(files_check.passed)

    def test_runtime_self_test_reports_missing_model_files(self):
        cfg = dataclasses.replace(
            service.Config.default(),
            model_dir="/models/Qwen3-ASR-GGUF/model-0.6B",
        )
        existing = {
            "/models/Qwen3-ASR-GGUF/model-0.6B",
            "/models/Qwen3-ASR-GGUF/model-0.6B/qwen3_asr_encoder_frontend.int4.onnx",
            "/tmp",
            "/run/user/1000",
        }

        def fake_exists(path):
            return str(path) in existing

        with (
            mock.patch.object(service.shutil, "which", return_value="/usr/bin/tool"),
            mock.patch.object(service.Path, "exists", fake_exists),
            mock.patch.object(service.os, "access", return_value=True),
            mock.patch.object(service, "runtime_dir", return_value=Path("/run/user/1000")),
        ):
            checks = service.runtime_checks(cfg)

        files_check = next(check for check in checks if check.name == "model files exist")
        self.assertFalse(files_check.passed)
        self.assertIn("qwen3_asr_encoder_backend.int4.onnx", files_check.detail)
        self.assertIn("qwen3_asr_llm.q4_k.gguf", files_check.detail)

    def test_runtime_self_test_reports_qwen_project_runtime_dependencies(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            project = root / "Qwen3-ASR-GGUF"
            model = project / "model-0.6B"
            package = project / "qwen_asr_gguf" / "inference"
            bin_dir = package / "bin"
            model.mkdir(parents=True)
            package.mkdir(parents=True)
            bin_dir.mkdir()
            for filename in service.REQUIRED_MODEL_FILES:
                (model / filename).write_text("x", encoding="utf-8")
            (package / "__init__.py").write_text("", encoding="utf-8")
            (bin_dir / "libllama.so").write_text("x", encoding="utf-8")
            (bin_dir / "libggml.so").write_text("x", encoding="utf-8")
            cfg = dataclasses.replace(
                service.Config.default(),
                asr_project_dir=str(project),
                model_dir=str(model),
            )

            with (
                mock.patch.object(service.shutil, "which", return_value="/usr/bin/tool"),
                mock.patch.object(service, "runtime_dir", return_value=Path("/tmp")),
            ):
                checks = service.runtime_checks(cfg)

        qwen_check = next(check for check in checks if check.name == "qwen package import path")
        llama_check = next(check for check in checks if check.name == "llama.cpp shared libraries")
        self.assertTrue(qwen_check.passed)
        self.assertTrue(llama_check.passed)

    def test_runtime_self_test_reports_missing_qwen_project_runtime_dependencies(self):
        with tempfile.TemporaryDirectory() as tmp:
            project = Path(tmp) / "Qwen3-ASR-GGUF"
            model = project / "model-0.6B"
            bin_dir = project / "qwen_asr_gguf" / "inference" / "bin"
            model.mkdir(parents=True)
            bin_dir.mkdir(parents=True)
            for filename in service.REQUIRED_MODEL_FILES:
                (model / filename).write_text("x", encoding="utf-8")
            (bin_dir / "libllama.so").write_text("x", encoding="utf-8")
            cfg = dataclasses.replace(
                service.Config.default(),
                asr_project_dir=str(project),
                model_dir=str(model),
            )

            with (
                mock.patch.object(service.shutil, "which", return_value="/usr/bin/tool"),
                mock.patch.object(service, "runtime_dir", return_value=Path("/tmp")),
            ):
                checks = service.runtime_checks(cfg)

        qwen_check = next(check for check in checks if check.name == "qwen package import path")
        llama_check = next(check for check in checks if check.name == "llama.cpp shared libraries")
        self.assertFalse(qwen_check.passed)
        self.assertIn("qwen_asr_gguf/inference", qwen_check.detail)
        self.assertFalse(llama_check.passed)
        self.assertIn("libggml", llama_check.detail)

    def test_self_test_returns_nonzero_for_missing_model(self):
        cfg = dataclasses.replace(service.Config.default(), model_dir="/missing/model")
        out = io.StringIO()

        with (
            mock.patch.object(service.shutil, "which", return_value="/usr/bin/tool"),
            mock.patch.object(service.Path, "exists", return_value=False),
            mock.patch("sys.stdout", out),
        ):
            code = service.self_test(cfg)

        self.assertEqual(code, 1)
        self.assertIn("model dir exists", out.getvalue())


class VoiceSessionTests(unittest.TestCase):
    def test_focus_shows_tooltip_until_ctrl_hold_starts_recording(self):
        clock = FakeClock()
        ui = FakeUiNotifier()
        session = service.VoiceSession(
            service.Config.default(),
            recorder=FakeRecorder(),
            asr=FakeAsr(),
            committer=FakeCommitter(),
            ui=ui,
            clock=clock.monotonic,
        )

        self.assertEqual(session.handle_command("FOCUS"), "TOOLTIP show")
        self.assertEqual(ui.messages, ["SHOW_TOOLTIP 长按 Ctrl 语音输入"])
        self.assertEqual(session.handle_command("CTRL_DOWN"), "WAITING_HOLD")

        clock.advance(0.34)
        self.assertEqual(session.handle_command("TICK"), "IDLE")
        self.assertEqual(session.state, service.SessionState.WAITING_FOR_HOLD)

        clock.advance(0.02)
        self.assertEqual(session.handle_command("TICK"), "RECORDING")
        self.assertEqual(session.recorder.started, 1)
        self.assertEqual(session.tooltip_visible, True)
        self.assertEqual(ui.messages[-1], "RECORDING")

    def test_focus_with_cursor_rect_forwards_tooltip_position(self):
        ui = FakeUiNotifier()
        session = service.VoiceSession(
            service.Config.default(),
            recorder=FakeRecorder(),
            asr=FakeAsr(),
            committer=FakeCommitter(),
            ui=ui,
        )

        self.assertEqual(session.handle_command("FOCUS 120 240 2 18"), "TOOLTIP show")
        self.assertEqual(ui.messages, ["SHOW_TOOLTIP 120 240 2 18 长按 Ctrl 语音输入"])

    def test_ctrl_release_after_hold_transcribes_and_commits(self):
        clock = FakeClock()
        recorder = FakeRecorder()
        asr = FakeAsr("离线语音输入")
        committer = FakeCommitter()
        ui = FakeUiNotifier()
        session = service.VoiceSession(
            service.Config.default(),
            recorder=recorder,
            asr=asr,
            committer=committer,
            ui=ui,
            clock=clock.monotonic,
        )

        session.handle_command("FOCUS")
        session.handle_command("CTRL_DOWN")
        clock.advance(0.36)
        session.handle_command("TICK")

        self.assertEqual(session.handle_command("CTRL_UP"), "COMMITTED")
        self.assertEqual(recorder.stopped, 1)
        self.assertEqual(asr.audio_paths, [recorder.path])
        self.assertEqual(committer.texts, ["离线语音输入"])
        self.assertEqual(session.state, service.SessionState.IDLE)
        self.assertEqual(ui.messages[-2:], ["TRANSCRIBING", "IDLE"])

    def test_short_ctrl_tap_does_not_record_or_commit(self):
        clock = FakeClock()
        recorder = FakeRecorder()
        committer = FakeCommitter()
        ui = FakeUiNotifier()
        session = service.VoiceSession(
            service.Config.default(),
            recorder=recorder,
            asr=FakeAsr(),
            committer=committer,
            ui=ui,
            clock=clock.monotonic,
        )

        session.handle_command("CTRL_DOWN")
        clock.advance(0.1)

        self.assertEqual(session.handle_command("CTRL_UP"), "CANCELLED")
        self.assertEqual(recorder.started, 0)
        self.assertEqual(recorder.stopped, 0)
        self.assertEqual(committer.texts, [])
        self.assertNotIn("RECORDING", ui.messages)

    def test_blur_hides_tooltip(self):
        ui = FakeUiNotifier()
        session = service.VoiceSession(
            service.Config.default(),
            recorder=FakeRecorder(),
            asr=FakeAsr(),
            committer=FakeCommitter(),
            ui=ui,
        )

        session.handle_command("FOCUS")

        self.assertEqual(session.handle_command("BLUR"), "TOOLTIP hide")
        self.assertEqual(ui.messages[-1], "HIDE_TOOLTIP")


class PipeWireRecorderTests(unittest.TestCase):
    def test_stop_accepts_valid_recording_even_when_pw_record_returns_one(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "recording.wav"
            path.write_bytes(b"0" * 2048)
            recorder = service.PipeWireRecorder(
                dataclasses.replace(
                    service.Config.default(),
                    recordings_dir=tmp,
                    min_record_seconds=0.25,
                ),
                clock=lambda: 10.0,
            )
            recorder.proc = FakeRecorderProcess(returncode=1)
            recorder.path = path
            recorder.started_at = 9.0

            with mock.patch.object(service, "log") as log:
                result = recorder.stop()

        self.assertEqual(result, path)
        self.assertFalse(any("pw-record exited" in call.args[0] for call in log.call_args_list))


class ServerProtocolTests(unittest.TestCase):
    def test_protocol_routes_focus_and_ctrl_commands(self):
        session = mock.Mock()
        session.handle_command.return_value = "OK"

        self.assertEqual(service.handle_protocol_message(session, b"FOCUS\n"), b"OK\n")
        session.handle_command.assert_called_once_with("FOCUS")

    def test_protocol_routes_focus_with_cursor_rect(self):
        session = mock.Mock()
        session.handle_command.return_value = "OK"

        self.assertEqual(service.handle_protocol_message(session, b"FOCUS 120 240 2 18\n"), b"OK\n")
        session.handle_command.assert_called_once_with("FOCUS 120 240 2 18")

    def test_protocol_rejects_unknown_commands(self):
        session = mock.Mock()

        self.assertEqual(service.handle_protocol_message(session, b"NOPE\n"), b"ERR unknown-command\n")
        session.handle_command.assert_not_called()

    def test_service_survives_fire_and_forget_addon_datagrams(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            control_socket = root / "control.sock"
            ui_socket = root / "ui.sock"
            config = root / "config.json"
            config.write_text(
                json.dumps({
                    "control_socket": str(control_socket),
                    "ui_socket": str(ui_socket),
                    "notify": False,
                    "fcitx_commit": False,
                }),
                encoding="utf-8",
            )
            proc = subprocess.Popen(
                [
                    sys.executable,
                    "-m",
                    "echoflow.service",
                    "--config",
                    str(config),
                ],
                cwd=str(Path(__file__).resolve().parents[1]),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            output = ""
            try:
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline and not control_socket.exists():
                    if proc.poll() is not None:
                        stdout, stderr = proc.communicate(timeout=1)
                        self.fail(f"service exited early: {stdout}{stderr}")
                    time.sleep(0.02)
                self.assertTrue(control_socket.exists())

                client_path = root / "addon-client.sock"
                with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as sock:
                    sock.bind(str(client_path))
                    sock.sendto(b"FOCUS", str(control_socket))
                os.unlink(client_path)

                time.sleep(0.2)

                self.assertIsNone(proc.poll())
            finally:
                proc.terminate()
                try:
                    stdout, stderr = proc.communicate(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    stdout, stderr = proc.communicate(timeout=3)
                output = f"{stdout}{stderr}"
            self.assertNotIn("reply to control peer failed", output)


class ConfigLoadingTests(unittest.TestCase):
    def test_load_config_expands_paths_from_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.json"
            path.write_text(
                '{"recordings_dir":"records","model_dir":"~/models/qwen-asr-0.6b"}',
                encoding="utf-8",
            )

            with mock.patch.dict(service.os.environ, {"HOME": "/home/tester"}):
                cfg = service.load_config(path)

        self.assertEqual(cfg.recordings_dir, str(Path(tmp) / "records"))
        self.assertEqual(cfg.model_dir, "/home/tester/models/qwen-asr-0.6b")


class MainCliTests(unittest.TestCase):
    def test_transcribe_file_prints_asr_text_without_starting_service(self):
        with tempfile.TemporaryDirectory() as tmp:
            audio = Path(tmp) / "sample.wav"
            audio.write_bytes(b"RIFF....WAVE")
            out = io.StringIO()

            runner = mock.Mock()
            runner.transcribe.return_value = "你好 deepin"
            with (
                mock.patch.object(service, "QwenAsrRunner", return_value=runner) as runner_cls,
                mock.patch.object(service, "serve") as serve,
                mock.patch("sys.stdout", out),
            ):
                code = service.main(["--transcribe-file", str(audio)])

        self.assertEqual(code, 0)
        runner_cls.assert_called_once()
        runner.transcribe.assert_called_once_with(audio)
        serve.assert_not_called()
        self.assertEqual(out.getvalue(), "你好 deepin\n")

    def test_transcribe_file_rejects_missing_audio(self):
        missing = Path("/tmp/echoflow-missing-audio.wav")
        err = io.StringIO()

        with (
            mock.patch.object(service, "serve") as serve,
            mock.patch("sys.stderr", err),
        ):
            code = service.main(["--transcribe-file", str(missing)])

        self.assertEqual(code, 2)
        serve.assert_not_called()
        self.assertIn("audio file does not exist", err.getvalue())


if __name__ == "__main__":
    unittest.main()
