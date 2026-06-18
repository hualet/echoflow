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

    def test_wrapper_cmake_uses_submodule_source(self):
        cmake = (ROOT / "llama-runtime" / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("add_subdirectory", cmake)
        self.assertIn("third_party/llama.cpp", cmake)

    def test_wrapper_cmake_supports_backend_selection(self):
        cmake = (ROOT / "llama-runtime" / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("ECHOFLOW_LLM_BACKEND", cmake)
        self.assertIn("find_package(Vulkan", cmake)
        self.assertIn("GGML_VULKAN", cmake)
        self.assertIn("GGML_NATIVE", cmake)
        self.assertIn("GGML_CUDA", cmake)

    def test_wrapper_cmake_installs_to_qwen_inference_bin(self):
        cmake = (ROOT / "llama-runtime" / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("QWEN_ASR_PROJECT_DIR", cmake)
        self.assertIn("qwen_asr_gguf/inference/bin", cmake)
        self.assertIn("libllama*.so*", cmake)
        self.assertIn("libggml*.so*", cmake)
        # cp -a preserves the libllama.so -> libllama.so.0 symlink chain that
        # the dynamic loader expects; file(COPY) would dereference symlinks.
        self.assertIn("cp -a", cmake)

    def test_install_llama_runtime_script_is_removed(self):
        # The script itself must be gone — its job is now done by
        # llama-runtime/CMakeLists.txt's install target.
        self.assertFalse(
            (ROOT / "scripts" / "install-llama-runtime.sh").exists(),
            "scripts/install-llama-runtime.sh should be removed; "
            "its job is now done by llama-runtime/CMakeLists.txt",
        )


if __name__ == "__main__":
    unittest.main()
