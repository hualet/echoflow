# SPDX-FileCopyrightText: 2026 HarryLoong
# SPDX-License-Identifier: GPL-3.0-or-later

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class InstallScriptTests(unittest.TestCase):
    def test_install_user_sets_up_all_runtime_components(self):
        script = (ROOT / "install-user.sh").read_text(encoding="utf-8")

        self.assertIn("uv venv", script)
        self.assertIn("uv pip", script)
        self.assertIn("cmake -S \"$ROOT_DIR/fcitx-addon\"", script)
        self.assertIn("cmake -S \"$ROOT_DIR/ui-host\"", script)
        self.assertIn("systemctl --user daemon-reload", script)
        self.assertIn("systemctl --user enable --now echoflow.service echoflow-ui.service", script)
        self.assertIn("Library=${ADDON_LIB%.so}", script)

    def test_install_user_can_install_without_starting_services(self):
        script = (ROOT / "install-user.sh").read_text(encoding="utf-8")

        self.assertIn("--no-start", script)
        self.assertIn("START_SERVICES=0", script)
        self.assertIn("if [[ \"$START_SERVICES\" == \"1\" ]]; then", script)
        self.assertIn("systemctl --user daemon-reload", script)
        self.assertIn("systemctl --user enable --now echoflow.service echoflow-ui.service", script)
        self.assertIn("systemctl --user enable echoflow.service echoflow-ui.service", script)
        self.assertIn("systemctl --user start echoflow.service echoflow-ui.service", script)

    def test_systemd_service_uses_installed_uv_venv_entrypoints(self):
        service = (ROOT / "systemd" / "user" / "echoflow.service").read_text(encoding="utf-8")
        ui_service = (ROOT / "systemd" / "user" / "echoflow-ui.service").read_text(encoding="utf-8")

        self.assertIn("%h/.local/share/echoflow/.venv/bin", service)
        self.assertIn("%h/.local/share/echoflow/.venv/bin/echoflow-service", service)
        self.assertNotIn("uv run", service)
        self.assertIn("%h/.local/bin/echoflow-ui", ui_service)

    def test_python_project_installs_qwen_asr_runtime_dependencies(self):
        pyproject = (ROOT / "pyproject.toml").read_text(encoding="utf-8")
        install_script = (ROOT / "install-user.sh").read_text(encoding="utf-8")

        for dependency in ["numpy", "onnxruntime", "gguf", "soundfile", "srt"]:
            self.assertIn(dependency, pyproject)
        self.assertIn("uv pip install --python \"$STATE_DIR/.venv/bin/python\" \"$ROOT_DIR\"", install_script)

    def test_install_user_rewrites_default_asr_runner_to_installed_entrypoint(self):
        script = (ROOT / "install-user.sh").read_text(encoding="utf-8")

        self.assertIn("ASR_RUNNER=\"$STATE_DIR/.venv/bin/qwen-asr-transcribe\"", script)
        self.assertIn("if data.get(\"asr_runner\") in (None, \"qwen-asr-transcribe\"):", script)
        self.assertIn("data[\"asr_runner\"] = runner", script)


if __name__ == "__main__":
    unittest.main()
