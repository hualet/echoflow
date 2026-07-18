# EchoFlow Onboarding Visual Refresh Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the text-heavy first-run guide with the approved four-page soft 3D, left-image/right-copy experience without changing setup behavior.

**Architecture:** Add four coordinated transparent PNGs to a dedicated Qt resource bundle, then refactor only `OnboardingDialog` presentation around a shared two-column page shell. Keep `OnboardingSetupController`, activation, persistence, download, retry, settings, and tray flows unchanged; widget and spec tests enforce content, accessibility, resource availability, and behavior.

**Tech Stack:** C++17, Qt 6 Widgets and resources, DTK 6 Widgets, QTest, shell spec tests, built-in image generation with local chroma-key removal.

---

## File Map

- Create `ui-host/onboarding.qrc`: embeds the four illustration resources under `/onboarding`.
- Create `ui-host/onboarding/intro.png`: microphone and local voice illustration.
- Create `ui-host/onboarding/shortcut.png`: right Ctrl keycap and waveform illustration.
- Create `ui-host/onboarding/settings.png`: tray bubble and settings controls illustration.
- Create `ui-host/onboarding/setup.png`: download and service-node illustration.
- Modify `ui-host/CMakeLists.txt`: compile `onboarding.qrc` into `echoflow-ui`.
- Modify `ui-host/OnboardingDialog.h`: declare shared visual-page and indicator helpers.
- Modify `ui-host/OnboardingDialog.cpp`: build the approved B2 layout and updated copy.
- Modify `tests/test_onboarding_dialog.cpp`: cover images, copy, accessibility, page dots, fallback, and unchanged setup behavior.
- Modify `tests/CMakeLists.txt`: compile `onboarding.qrc` into the focused dialog test.
- Modify `tests/spec/run_spec.sh`: assert that every asset is bundled and the UI target contains the resource.

### Task 1: Generate and bundle the coordinated illustration set

**Files:**
- Create: `ui-host/onboarding/intro.png`
- Create: `ui-host/onboarding/shortcut.png`
- Create: `ui-host/onboarding/settings.png`
- Create: `ui-host/onboarding/setup.png`
- Create: `ui-host/onboarding.qrc`
- Modify: `ui-host/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `tests/spec/run_spec.sh`

- [ ] **Step 1: Add failing resource assertions**

Append these assertions near the existing `icons.qrc` checks in
`tests/spec/run_spec.sh`:

```bash
assert_contains "$ROOT/ui-host/CMakeLists.txt" "onboarding.qrc" \
  "ui-host embeds onboarding illustration resources"
for asset in intro shortcut settings setup; do
  assert_contains "$ROOT/ui-host/onboarding.qrc" \
    "onboarding/${asset}.png" "onboarding bundles ${asset} illustration"
  [[ -s "$ROOT/ui-host/onboarding/${asset}.png" ]] || \
    fail "onboarding ${asset} illustration exists and is non-empty"
done
```

- [ ] **Step 2: Run the spec test and verify it fails**

Run:

```bash
bash tests/spec/run_spec.sh
```

Expected: failure because `ui-host/onboarding.qrc` and the four PNGs do not yet
exist.

- [ ] **Step 3: Generate four source illustrations with one visual system**

Use the built-in image generation tool once per asset. Every prompt must use
this shared art direction:

```text
Use case: desktop application onboarding illustration.
Style: premium soft 3D clay render, rounded forms, blue and violet materials,
subtle translucent accents, soft studio lighting from upper left, gentle
ambient shadow, orthographic three-quarter camera, centered object, consistent
object scale across a four-image set.
Canvas: square, one isolated composition on a perfectly flat chroma-key green
background (#00ff00), generous clear margin around the object.
Constraints: no text, no letters, no numbers, no logo, no watermark, no UI
caption, no border, no cropped object, no photorealistic people.
```

Add exactly one subject paragraph for each call:

```text
intro: A friendly desktop microphone capsule with two small curved sound-wave
ribbons and a subtle shield-shaped translucent halo, conveying local private
voice recognition.

shortcut: A single raised computer keyboard keycap identified visually by its
wide Ctrl-key proportions, accompanied by a short floating waveform; the
keycap surface must remain completely blank because native UI text will name
the key.

settings: A compact system-tray speech bubble beside three abstract rounded
settings sliders, communicating that preferences and the guide live in the
tray without depicting a literal screenshot.

setup: A rounded download arrow entering a small local device, connected to
three softly glowing service nodes, communicating model download and local
service preparation.
```

- [ ] **Step 4: Remove chroma key and normalize assets**

Copy each generated result into a temporary project path, then run the shipped
helper with auto key sampling, soft matte, and despill. Use `--help` first to
confirm the installed helper's current flag names:

```bash
python3 /home/hualet/.codex/skills/.system/imagegen/scripts/remove_chroma_key.py --help
```

Write the final transparent outputs to the four exact paths under
`ui-host/onboarding/`. Inspect all four files and verify transparent corners,
clean antialiased edges, consistent scale, and absence of accidental glyphs.
Normalize oversized outputs to a common square canvas only if necessary; do
not stretch the objects.

- [ ] **Step 5: Add the Qt resource file and build wiring**

Create `ui-host/onboarding.qrc`:

```xml
<!DOCTYPE RCC>
<RCC version="1.0">
  <qresource prefix="/onboarding">
    <file alias="intro.png">onboarding/intro.png</file>
    <file alias="shortcut.png">onboarding/shortcut.png</file>
    <file alias="settings.png">onboarding/settings.png</file>
    <file alias="setup.png">onboarding/setup.png</file>
  </qresource>
</RCC>
```

Add `onboarding.qrc` immediately after `icons.qrc` in `ui-host/CMakeLists.txt`.
Add `../ui-host/onboarding.qrc` to both standalone and top-level
`test_onboarding_dialog` targets in `tests/CMakeLists.txt` so focused tests load
the real assets.

- [ ] **Step 6: Verify resource tests and standalone compilation**

Run:

```bash
bash tests/spec/run_spec.sh
cmake -S ui-host -B /tmp/echoflow-onboarding-art-ui \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build /tmp/echoflow-onboarding-art-ui -j2
```

Expected: all spec assertions pass and `echoflow-ui` links with the new
resource.

- [ ] **Step 7: Commit the asset set**

```bash
git add ui-host/onboarding.qrc ui-host/onboarding/*.png \
  ui-host/CMakeLists.txt tests/CMakeLists.txt tests/spec/run_spec.sh
git commit -m "feat(onboarding): add soft 3D illustration set" \
  -m "Bundle a coordinated set of accessible, text-free illustrations for the
first-run guide and cover the resource wiring in focused and spec tests."
```

### Task 2: Build the shared B2 information-page layout

**Files:**
- Modify: `tests/test_onboarding_dialog.cpp`
- Modify: `ui-host/OnboardingDialog.h`
- Modify: `ui-host/OnboardingDialog.cpp`

- [ ] **Step 1: Replace the old copy test with a failing visual-layout test**

Add a slot named `usesApprovedVisualStoryAndAccessibleImages()` and implement
the core assertions:

```cpp
const QList<std::tuple<QString, QString, QString, QString>> pages = {
    {QStringLiteral("introIllustration"), QStringLiteral("说话，就能输入"),
     QStringLiteral("introHeading"), QStringLiteral("本机离线语音识别示意图")},
    {QStringLiteral("shortcutIllustration"),
     QStringLiteral("按右 Ctrl，开始说话"), QStringLiteral("shortcutHeading"),
     QStringLiteral("右 Ctrl 语音输入快捷键示意图")},
    {QStringLiteral("settingsIllustration"),
     QStringLiteral("需要调整？都在托盘里"), QStringLiteral("settingsHeading"),
     QStringLiteral("托盘与设置入口示意图")},
};
for (const auto &[imageName, title, titleName, accessibleName] : pages) {
    auto *image = dialog.findChild<QLabel *>(imageName);
    QVERIFY2(image, qPrintable(imageName));
    QVERIFY2(!image->pixmap().isNull(), qPrintable(imageName));
    QCOMPARE(image->accessibleName(), accessibleName);
    auto *heading = dialog.findChild<QLabel *>(titleName);
    QVERIFY2(heading, qPrintable(titleName));
    QCOMPARE(heading->text(), title);
}
QCOMPARE(dialog.minimumWidth(), 680);
QVERIFY(dialog.minimumHeight() >= 500);
```

Also assert the exact body copy and semantic tag text approved in the visual
spec:

```cpp
QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("introDescriptionLabel"))->text(),
         QStringLiteral("语音识别在本机完成，录音不会离开你的设备。"));
QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("introTagLabel"))->text(),
         QStringLiteral("离线 · 隐私 · 快速"));
QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("shortcutDescriptionLabel"))->text(),
         QStringLiteral("再按一次结束，识别文字会直接进入当前输入框。"));
QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("settingsDescriptionLabel"))->text(),
         QStringLiteral("切换模型、语言、麦克风，也可以随时重播这份指引。"));
```

Retain the existing palette propagation and window-icon assertions.

- [ ] **Step 2: Run the focused test and verify it fails**

Run:

```bash
cmake -S tests -B /tmp/echoflow-onboarding-visual-tests \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build /tmp/echoflow-onboarding-visual-tests \
  --target test_onboarding_dialog -j2
ctest --test-dir /tmp/echoflow-onboarding-visual-tests \
  -R '^test_onboarding_dialog$' --output-on-failure
```

Expected: failure because the illustration labels and approved headings do not
exist.

- [ ] **Step 3: Add shared page-builder declarations**

In `OnboardingDialog.h`, add private helpers while keeping controller members
unchanged:

```cpp
QWidget *createVisualPage(const QString &imagePath,
                          const QString &imageObjectName,
                          const QString &imageAccessibleName,
                          const QString &title,
                          const QString &titleObjectName,
                          const QString &body,
                          const QString &bodyObjectName,
                          const QString &tag = {},
                          const QString &tagObjectName = {});
QLabel *createIllustration(const QString &path, const QString &objectName,
                           const QString &accessibleName, QWidget *parent);
```

- [ ] **Step 4: Implement the shared two-column shell**

In `OnboardingDialog.cpp`, make `createIllustration()` load and scale the
resource without becoming the sole carrier of information:

```cpp
auto *label = new QLabel(parent);
label->setObjectName(objectName);
label->setAccessibleName(accessibleName);
label->setAlignment(Qt::AlignCenter);
label->setMinimumSize(260, 300);
label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
const QPixmap source(path);
if (!source.isNull()) {
    label->setPixmap(source.scaled(260, 300, Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation));
}
return label;
```

`createVisualPage()` creates a `QHBoxLayout`, puts the illustration on the left,
and places a vertically centered title, body, and optional rounded semantic tag
on the right. Use layout stretch factors 45 and 55, a 24-pixel gap, native
palette roles, and no fixed background color.

- [ ] **Step 5: Replace the first three pages with approved content**

Use these resource and copy mappings:

```cpp
createVisualPage(QStringLiteral(":/onboarding/intro.png"),
                 QStringLiteral("introIllustration"),
                 QStringLiteral("本机离线语音识别示意图"),
                 QStringLiteral("说话，就能输入"),
                 QStringLiteral("introHeading"),
                 QStringLiteral("语音识别在本机完成，录音不会离开你的设备。"),
                 QStringLiteral("introDescriptionLabel"),
                 QStringLiteral("离线 · 隐私 · 快速"),
                 QStringLiteral("introTagLabel"));

createVisualPage(QStringLiteral(":/onboarding/shortcut.png"),
                 QStringLiteral("shortcutIllustration"),
                 QStringLiteral("右 Ctrl 语音输入快捷键示意图"),
                 QStringLiteral("按右 Ctrl，开始说话"),
                 QStringLiteral("shortcutHeading"),
                 QStringLiteral("再按一次结束，识别文字会直接进入当前输入框。"),
                 QStringLiteral("shortcutDescriptionLabel"),
                 QStringLiteral("右 Ctrl · 开始 / 结束"),
                 QStringLiteral("shortcutTagLabel"));

createVisualPage(QStringLiteral(":/onboarding/settings.png"),
                 QStringLiteral("settingsIllustration"),
                 QStringLiteral("托盘与设置入口示意图"),
                 QStringLiteral("需要调整？都在托盘里"),
                 QStringLiteral("settingsHeading"),
                 QStringLiteral("切换模型、语言、麦克风，也可以随时重播这份指引。"),
                 QStringLiteral("settingsDescriptionLabel"),
                 QStringLiteral("托盘 · 设置 · 使用指引"),
                 QStringLiteral("settingsTagLabel"));
```

Remove the old bullet labels and `informationPage()` helper. Set the dialog
minimum size to `680, 500`.

- [ ] **Step 6: Run the focused test and verify it passes**

Run the Task 2 Step 2 commands again.

Expected: `test_onboarding_dialog` passes, including existing navigation and
setup behavior cases.

- [ ] **Step 7: Commit the shared visual layout**

```bash
git add ui-host/OnboardingDialog.h ui-host/OnboardingDialog.cpp \
  tests/test_onboarding_dialog.cpp
git commit -m "feat(onboarding): adopt image-led guide layout" \
  -m "Present the first three guide pages with the approved left-image,
right-copy composition while preserving native text and accessibility."
```

### Task 3: Integrate the setup page and accessible dot indicator

**Files:**
- Modify: `tests/test_onboarding_dialog.cpp`
- Modify: `ui-host/OnboardingDialog.h`
- Modify: `ui-host/OnboardingDialog.cpp`

- [ ] **Step 1: Add failing setup-layout and indicator assertions**

Extend `hasFourPagesAndBoundedNavigation()` and the visual test:

```cpp
auto *dots = dialog.findChild<QWidget *>(QStringLiteral("pageDots"));
QVERIFY(dots);
QCOMPARE(dots->accessibleName(), QStringLiteral("第 1 页，共 4 页"));

auto *setupImage =
    dialog.findChild<QLabel *>(QStringLiteral("setupIllustration"));
QVERIFY(setupImage);
QVERIFY(!setupImage->pixmap().isNull());
QCOMPARE(setupImage->accessibleName(), QStringLiteral("模型下载与服务准备示意图"));
QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("setupHeading"))->text(),
         QStringLiteral("准备好，就可以开始"));
```

After moving to pages 2 through 4, assert `pageDots` keeps the accessible page
description synchronized. Retain assertions for the model, service, Fcitx,
progress, errors, retry, and completion controls.

- [ ] **Step 2: Run the focused test and verify it fails**

Run the Task 2 focused build and CTest command.

Expected: failure because `pageDots`, the setup illustration, and updated
heading are missing.

- [ ] **Step 3: Implement the dot indicator without removing screen-reader text**

Replace the visible textual counter with a widget named `pageDots` containing
four small circular `QLabel`s. Store them in `QList<QLabel *> pageDots_` and
update their palette/stylesheet in `setCurrentPage()`. Keep `pageIndicator_`
as a visually hidden or zero-width accessible label only if Qt accessibility
requires it; otherwise set the accessible name directly on the dot container:

```cpp
const QString progress = QStringLiteral("第 %1 页，共 %2 页")
                             .arg(page + 1)
                             .arg(pages_->count());
pageDotsWidget_->setAccessibleName(progress);
for (int i = 0; i < pageDots_.size(); ++i) {
    pageDots_[i]->setProperty("active", i == page);
}
```

Use the current palette highlight color for the active dot and a muted palette
color for inactive dots. Do not encode progress by color alone: the container's
accessible name carries the exact textual position.

- [ ] **Step 4: Rebuild the setup page inside the two-column shell**

Create the setup page with `:/onboarding/setup.png` on the left. On the right,
keep the existing title, description, model row and progress bar, combined
service row, Fcitx row, aggregate error, and all existing object names. Reduce
row padding and illustration bounds enough that long error text can expand
without pushing the footer outside the 500-pixel minimum height. Do not modify
`renderSetup()`, `renderProgress()`, or controller calls except for references
needed by the new container layout.

- [ ] **Step 5: Add and verify image-independent operation**

Clear the intro illustration in the widget test, show the dialog, and verify
that native content and navigation remain usable:

```cpp
auto *introImage =
    dialog.findChild<QLabel *>(QStringLiteral("introIllustration"));
QVERIFY(introImage);
introImage->clear();
dialog.show();
QApplication::processEvents();
QVERIFY(dialog.findChild<QLabel *>(QStringLiteral("introHeading"))->isVisible());
auto *next = button(dialog, "nextButton");
QVERIFY(next->isVisible());
QVERIFY(next->isEnabled());
QTest::mouseClick(next, Qt::LeftButton);
QCOMPARE(dialog.currentPage(), 1);
```

The implementation must not add fallback prose that duplicates native copy;
the heading, body, and controls already provide the complete experience.

- [ ] **Step 6: Run all focused tests**

```bash
cmake --build /tmp/echoflow-onboarding-visual-tests -j2
ctest --test-dir /tmp/echoflow-onboarding-visual-tests --output-on-failure
```

Expected: 5/5 focused tests pass.

- [ ] **Step 7: Commit setup-page integration**

```bash
git add ui-host/OnboardingDialog.h ui-host/OnboardingDialog.cpp \
  tests/test_onboarding_dialog.cpp
git commit -m "feat(onboarding): unify setup page visuals" \
  -m "Place live setup progress beside the coordinated illustration and add an
accessible dot indicator without changing setup state transitions."
```

### Task 4: Real-window visual verification and final regression pass

**Files:**
- Modify only if verification exposes a scoped visual defect.

- [ ] **Step 1: Build the standalone UI from a clean directory**

```bash
cmake -S ui-host -B /tmp/echoflow-onboarding-visual-final \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr
cmake --build /tmp/echoflow-onboarding-visual-final -j2
```

Expected: `echoflow-ui` links successfully with all onboarding resources.

- [ ] **Step 2: Launch a disposable first-run session**

Use a temporary `XDG_CONFIG_HOME` and a distinct runtime/socket environment so
the installed EchoFlow state is not changed. Launch the real UI with
`--activate`, open all four pages, and capture screenshots at their actual
window size. Do not start a real model download merely to inspect the layout;
use the focused test/fake controller path for running and error states if
needed.

- [ ] **Step 3: Inspect light and dark theme renderings**

Verify each screenshot for transparent image edges, balanced 45/55 layout,
consistent object scale, readable native text, focus visibility, no clipping,
and no footer movement. Exercise model progress, multiline setup error, Retry,
and completed states using the offscreen test harness or a small temporary test
driver linked to the existing fake controller boundaries.

- [ ] **Step 4: Run final automated verification**

```bash
ctest --test-dir /tmp/echoflow-onboarding-visual-tests --output-on-failure
bash tests/spec/run_spec.sh
bash -n install-user.sh uninstall-user.sh tests/spec/*.sh
sh -n run.sh
git diff --check
```

Expected: 5/5 focused tests pass, all spec assertions pass, all shell syntax
checks pass, and `git diff --check` produces no output.

- [ ] **Step 5: Request independent code and visual review**

Dispatch a reviewer with the design, plan, diff, test output, and screenshots.
The reviewer must report Critical, Important, and Minor findings separately and
verify that no setup or activation behavior changed.

- [ ] **Step 6: Commit only scoped review fixes**

If review finds a defect, add a failing test when practical, make the minimal
fix, rerun Step 4, and commit with an appropriate Conventional Commit subject
and body. If there are no defects, do not create an empty commit.
