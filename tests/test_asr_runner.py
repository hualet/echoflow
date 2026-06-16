# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

import io
import os
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock

from echoflow import asr_runner


class RunnerTests(unittest.TestCase):
    def test_main_loads_qwen_engine_and_prints_transcription(self):
        engine = mock.Mock()
        engine.transcribe.return_value = types.SimpleNamespace(text="  声流输入\n")
        engine_cls = mock.Mock(return_value=engine)
        config_cls = mock.Mock(return_value="config")

        with tempfile.TemporaryDirectory() as tmp:
            audio = Path(tmp) / "input.wav"
            audio.write_bytes(b"RIFF")

            out = io.StringIO()
            with (
                mock.patch.object(asr_runner, "load_qwen_classes", return_value=(config_cls, engine_cls)),
                mock.patch("sys.stdout", out),
            ):
                code = asr_runner.main([
                    "--project",
                    "/models/Qwen3-ASR-GGUF",
                    "--model",
                    "/models/qwen-asr-0.6b",
                    "--model-name",
                    "qwen-asr-0.6b",
                    "--language",
                    "Chinese",
                    str(audio),
                ])

        self.assertEqual(code, 0)
        self.assertEqual(out.getvalue(), "声流输入\n")
        config_cls.assert_called_once()
        self.assertEqual(config_cls.call_args.kwargs["model_dir"], "/models/qwen-asr-0.6b")
        engine.transcribe.assert_called_once_with(
            audio_file=str(audio),
            language="Chinese",
            context="",
            start_second=0,
            duration=None,
            temperature=0.4,
        )

    def test_main_suppresses_qwen_streaming_stdout(self):
        engine = mock.Mock()

        def transcribe(**kwargs):
            print("甚至出现交易几乎停滞的情况。", end="")
            return types.SimpleNamespace(text="甚至出现交易几乎停滞的情况。")

        engine.transcribe.side_effect = transcribe
        engine_cls = mock.Mock(return_value=engine)
        config_cls = mock.Mock(return_value="config")

        with tempfile.TemporaryDirectory() as tmp:
            audio = Path(tmp) / "input.wav"
            audio.write_bytes(b"RIFF")

            out = io.StringIO()
            with (
                mock.patch.object(asr_runner, "load_qwen_classes", return_value=(config_cls, engine_cls)),
                mock.patch("sys.stdout", out),
            ):
                code = asr_runner.main([
                    "--project",
                    "/models/Qwen3-ASR-GGUF",
                    "--model",
                    "/models/qwen-asr-0.6b",
                    str(audio),
                ])

        self.assertEqual(code, 0)
        self.assertEqual(out.getvalue(), "甚至出现交易几乎停滞的情况。\n")

    def test_main_rejects_missing_audio(self):
        err = io.StringIO()
        with mock.patch("sys.stderr", err):
            code = asr_runner.main(["--model", "/models/qwen-asr-0.6b", "/tmp/missing.wav"])

        self.assertEqual(code, 2)
        self.assertIn("audio file does not exist", err.getvalue())

    def test_configure_project_runtime_adds_python_and_llama_paths(self):
        with tempfile.TemporaryDirectory() as tmp:
            project = Path(tmp) / "Qwen3-ASR-GGUF"
            bin_dir = project / "qwen_asr_gguf" / "inference" / "bin"
            bin_dir.mkdir(parents=True)

            with (
                mock.patch.object(asr_runner.sys, "path", []),
                mock.patch.dict(os.environ, {}, clear=True),
            ):
                asr_runner.configure_project_runtime(project)

                self.assertEqual(asr_runner.sys.path[0], str(project))
                self.assertEqual(os.environ["LD_LIBRARY_PATH"], str(bin_dir))
                self.assertEqual(os.environ["GGML_VK_DISABLE_F16"], "1")


if __name__ == "__main__":
    unittest.main()
