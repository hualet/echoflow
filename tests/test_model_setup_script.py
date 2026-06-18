# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

from pathlib import Path
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


if __name__ == "__main__":
    unittest.main()
