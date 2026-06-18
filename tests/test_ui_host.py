# SPDX-FileCopyrightText: 2026 Hualet Wang
# SPDX-License-Identifier: GPL-3.0-or-later

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class UiHostStructureTests(unittest.TestCase):
    def test_python_project_does_not_provide_qml_binding_host(self):
        pyproject = (ROOT / "pyproject.toml").read_text(encoding="utf-8")

        self.assertNotIn("PySide", pyproject)
        self.assertNotIn("PyQt", pyproject)
        self.assertNotIn("echoflow-ui", pyproject)
        self.assertFalse((ROOT / "echoflow" / "ui.py").exists())

    def test_cpp_ui_host_builds_against_qt_qml(self):
        cmake = (ROOT / "ui-host" / "CMakeLists.txt").read_text(encoding="utf-8")
        main_cpp = (ROOT / "ui-host" / "main.cpp").read_text(encoding="utf-8")

        self.assertIn("Qt6", cmake)
        self.assertIn("Qml", cmake)
        self.assertIn("Quick", cmake)
        self.assertIn("${CMAKE_INSTALL_FULL_DATADIR}/echoflow/qml", cmake)
        self.assertNotIn('ECHOFLOW_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../qml"', cmake)
        self.assertIn("QSocketNotifier", main_cpp)
        self.assertIn("SHOW_TOOLTIP", main_cpp)
        self.assertIn("hasPosition", main_cpp)
        self.assertIn("moveY", main_cpp)
        self.assertIn("TRANSCRIBING", main_cpp)

    def test_ui_host_pins_capsule_to_fixed_bottom_center(self):
        main_cpp = (ROOT / "ui-host" / "main.cpp").read_text(encoding="utf-8")

        self.assertIn("fixedCapsulePosition", main_cpp)

    def test_fcitx_addon_only_unlinks_owned_commit_socket(self):
        addon_cpp = (ROOT / "fcitx-addon" / "echoflow.cpp").read_text(encoding="utf-8")

        self.assertIn("unlinkOwnedCommitSocket", addon_cpp)
        self.assertIn("fstat(fd_, &fdStat)", addon_cpp)
        self.assertIn("stat(commitSocketPath_.c_str(), &pathStat)", addon_cpp)
        self.assertIn("fdStat.st_ino == pathStat.st_ino", addon_cpp)

    def test_fcitx_addon_obeys_right_ctrl_toggle_without_consuming_it(self):
        addon_cpp = (ROOT / "fcitx-addon" / "echoflow.cpp").read_text(encoding="utf-8")

        self.assertIn("isRightCtrl", addon_cpp)
        self.assertIn("FcitxKey_Control_R", addon_cpp)
        self.assertIn("CTRL_DOWN", addon_cpp)
        self.assertNotIn("isPlainCtrl", addon_cpp)
        self.assertNotIn("Control_L", addon_cpp)
        self.assertNotIn("CTRL_UP", addon_cpp)
        self.assertNotIn("filterAndAccept", addon_cpp)
        self.assertNotIn("->accept()", addon_cpp)

    def test_fcitx_addon_dedupes_right_ctrl_auto_repeat(self):
        addon_cpp = (ROOT / "fcitx-addon" / "echoflow.cpp").read_text(encoding="utf-8")

        self.assertIn("rightCtrlDown_", addon_cpp)

    def test_fcitx_addon_declutters_capsule_on_typing(self):
        addon_cpp = (ROOT / "fcitx-addon" / "echoflow.cpp").read_text(encoding="utf-8")

        self.assertIn("TYPED", addon_cpp)
        self.assertIn("isModifier()", addon_cpp)


if __name__ == "__main__":
    unittest.main()
