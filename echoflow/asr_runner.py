#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later
"""Qwen ASR command-line adapter for EchoFlow.

This module is the only place that knows the Qwen3-ASR-GGUF Python API shape.
The long-running EchoFlow service calls it as a subprocess so the service state
machine stays independent from model runtime details.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import os
import sys
from pathlib import Path
from typing import Optional


def load_qwen_classes():
    from qwen_asr_gguf.inference import ASREngineConfig, QwenASREngine

    return ASREngineConfig, QwenASREngine


def configure_project_runtime(project_dir: Path) -> None:
    project_dir = project_dir.expanduser()
    if str(project_dir) not in sys.path:
        sys.path.insert(0, str(project_dir))
    candidate_bin = project_dir / "qwen_asr_gguf" / "inference" / "bin"
    if candidate_bin.exists():
        old_ld = os.environ.get("LD_LIBRARY_PATH", "")
        if str(candidate_bin) not in old_ld.split(":"):
            os.environ["LD_LIBRARY_PATH"] = (
                f"{candidate_bin}:{old_ld}" if old_ld else str(candidate_bin)
            )
    os.environ.setdefault("GGML_VK_DISABLE_F16", "1")


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Transcribe one audio file with Qwen ASR")
    parser.add_argument("audio", type=Path)
    parser.add_argument("--project", type=Path, default=None, help="Qwen3-ASR-GGUF project directory")
    parser.add_argument("--model", required=True, dest="model_dir")
    parser.add_argument("--model-name", default="qwen-asr-0.6b")
    parser.add_argument("--language", default=None)
    args = parser.parse_args(argv)

    audio = args.audio.expanduser()
    if not audio.exists():
        print(f"audio file does not exist: {audio}", file=sys.stderr)
        return 2

    model_dir = Path(args.model_dir).expanduser()
    project_dir = args.project.expanduser() if args.project else model_dir.parent
    configure_project_runtime(project_dir)
    ASREngineConfig, QwenASREngine = load_qwen_classes()
    config = ASREngineConfig(
        model_dir=str(model_dir),
        onnx_provider="CPU",
        llm_use_gpu=True,
        encoder_frontend_fn="qwen3_asr_encoder_frontend.int4.onnx",
        encoder_backend_fn="qwen3_asr_encoder_backend.int4.onnx",
        enable_aligner=False,
        verbose=False,
    )
    engine = QwenASREngine(config=config)
    with contextlib.redirect_stdout(io.StringIO()):
        result = engine.transcribe(
            audio_file=str(audio),
            language=args.language,
            context="",
            start_second=0,
            duration=None,
            temperature=0.4,
        )
    print((result.text or "").strip())
    shutdown = getattr(engine, "shutdown", None)
    if callable(shutdown):
        shutdown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
