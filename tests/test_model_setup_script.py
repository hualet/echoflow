# SPDX-FileCopyrightText: 2026 HarryLoong
# SPDX-License-Identifier: GPL-3.0-or-later

from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ModelSetupScriptTests(unittest.TestCase):
    def test_setup_script_downloads_qwen_asr_06b_outside_repo(self):
        script = (ROOT / "scripts" / "setup-qwen-asr-0.6b.sh").read_text(encoding="utf-8")

        self.assertIn("Qwen3-ASR-GGUF.git", script)
        self.assertIn("Qwen3-ASR-0.6B-gguf.zip", script)
        self.assertIn("model-0.6B", script)
        self.assertIn("curl -L", script)
        self.assertIn("unzip -o", script)
        self.assertIn("ln -sf qwen3_asr_llm.q4_k.gguf qwen3_asr_llm.q5_k.gguf", script)
        self.assertIn("${MODEL_ROOT:-$HOME/AI/Model}", script)
        self.assertNotIn("/home/hualet/projects/hualet/echoflow/model", script)

    def test_install_llama_runtime_copies_required_shared_libraries(self):
        script = (ROOT / "scripts" / "install-llama-runtime.sh").read_text(encoding="utf-8")

        self.assertIn("LLAMA_BUILD_DIR", script)
        self.assertIn("QWEN_ASR_PROJECT_DIR", script)
        self.assertIn("qwen_asr_gguf/inference/bin", script)
        self.assertIn("libllama*.so*", script)
        self.assertIn("libggml*.so*", script)
        self.assertIn("find_required_library", script)
        self.assertIn("cp -a", script)
        self.assertNotIn("/home/hualet/projects/hualet/echoflow/model", script)

    def test_install_llama_runtime_preserves_loader_symlinks(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            qwen = root / "Qwen3-ASR-GGUF"
            package = qwen / "qwen_asr_gguf" / "inference"
            build = root / "llama.cpp-build" / "build" / "bin"
            package.mkdir(parents=True)
            build.mkdir(parents=True)
            (build / "libllama.so.0.0.1").write_text("llama", encoding="utf-8")
            (build / "libggml.so.0.11.1").write_text("ggml", encoding="utf-8")
            (build / "libggml-base.so.0.11.1").write_text("base", encoding="utf-8")
            (build / "libllama.so").symlink_to("libllama.so.0.0.1")
            (build / "libggml.so").symlink_to("libggml.so.0.11.1")
            (build / "libggml-base.so").symlink_to("libggml-base.so.0.11.1")

            env = os.environ.copy()
            env.update({
                "QWEN_ASR_PROJECT_DIR": str(qwen),
                "LLAMA_BUILD_DIR": str(root / "llama.cpp-build" / "build"),
            })
            subprocess.run(
                [str(ROOT / "scripts" / "install-llama-runtime.sh")],
                check=True,
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )

            target = package / "bin"
            self.assertTrue((target / "libllama.so").is_symlink())
            self.assertTrue((target / "libggml.so").is_symlink())
            self.assertTrue((target / "libggml-base.so").is_symlink())


if __name__ == "__main__":
    unittest.main()
