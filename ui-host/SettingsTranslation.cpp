// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QObject>
#include <QString>

namespace echoflow {

// DSettings renders schema names and combobox values through QObject::tr
// (empty translate context), but lupdate cannot extract strings from JSON.
// This shim lists every user-visible string from settings-schema.json plus
// the combobox values set programmatically in EchoFlowSettings so they land
// in the .ts files under the QObject context. Keep it in sync with the
// schema; it has no runtime effect.
void generateSettingsTranslate() {
    auto use = [](const QString&) {};
    use(QObject::tr("Getting Started"));
    use(QObject::tr("Usage Guide"));
    use(QObject::tr("Basic Settings"));
    use(QObject::tr("Recognition Model"));
    use(QObject::tr("Model"));
    use(QObject::tr("Download Mirror"));
    use(QObject::tr("Recognition Options"));
    use(QObject::tr("Recognition Language"));
    use(QObject::tr("Enable streaming transcription"));
    use(QObject::tr("Polish Model"));
    use(QObject::tr("Refine recognition results with a local language model "
                    "before committing (removes filler words and "
                    "self-corrections, adds punctuation)"));
    use(QObject::tr("Recording"));
    use(QObject::tr("Microphone"));
    use(QObject::tr("Minimum recording time (s)"));
    use(QObject::tr("Sample rate (Hz)"));
    use(QObject::tr("Channels"));
    use(QObject::tr("Sample format"));
    use(QObject::tr("Interaction"));
    use(QObject::tr("Press and hold the left mouse button in a text field to talk"));
    use(QObject::tr("Show a voice hint when a text field gains focus"));
    use(QObject::tr("Advanced Settings"));
    use(QObject::tr("Runtime"));
    use(QObject::tr("Recognition timeout (s)"));
    use(QObject::tr("Fcitx"));
    use(QObject::tr("Commit text through Fcitx"));
    use(QObject::tr("Storage"));
    use(QObject::tr("Recordings directory"));
    use(QObject::tr("Save streaming debug recordings"));
    // Combobox display values set in EchoFlowSettings::populateComboBoxes.
    use(QObject::tr("System default"));
    use(QObject::tr("English"));
    use(QObject::tr("Chinese"));
    use(QObject::tr("Spanish"));
    use(QObject::tr("Portuguese"));
    use(QObject::tr("French"));
    use(QObject::tr("Italian"));
}

}  // namespace echoflow
