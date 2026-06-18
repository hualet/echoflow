# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

import subprocess
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]

# The llama.cpp submodule is pinned at this commit (tag b9106), verified
# against Qwen3-ASR-GGUF with GGML_VULKAN=ON + GGML_NATIVE=ON.
LLAMA_CPP_PINNED_COMMIT = "dd9280a6643d2c4931df7c9246b2f344c0a0513a"


class LlamaRuntimeBuildTests(unittest.TestCase):
    def test_gitmodules_pins_llama_cpp_submodule(self):
        gitmodules = (ROOT / ".gitmodules").read_text(encoding="utf-8")

        self.assertIn('[submodule "third_party/llama.cpp"]', gitmodules)
        self.assertIn("path = third_party/llama.cpp", gitmodules)
        self.assertIn("url = https://github.com/ggml-org/llama.cpp.git", gitmodules)

        # .gitmodules only declares path+url; the actual pin is the gitlink
        # commit recorded in the parent tree. Verify both so a repin (or a
        # .gitmodules/gitlink mismatch) is caught.
        ls_tree = subprocess.check_output(
            ["git", "ls-tree", "HEAD", "third_party/llama.cpp"],
            cwd=ROOT,
            text=True,
        )
        self.assertIn(LLAMA_CPP_PINNED_COMMIT, ls_tree)


if __name__ == "__main__":
    unittest.main()
