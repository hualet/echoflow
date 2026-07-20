# EchoFlow Onboarding DTK Refinement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the welcome identity into a real DTK title bar and restyle the final setup page as a native DTK settings group without changing onboarding behavior.

**Architecture:** Keep `OnboardingDialog` as the existing non-modal `DDialog`, clear its centered content title/icon, and configure the `DTitlebar` that `DDialog` already owns as a direct child. Keep the slideshow as the sole dialog content widget and preserve the existing setup labels and controller wiring while replacing only the three outlined row frames with row widgets inside one `DBackgroundGroup`.

**Tech Stack:** C++17, Qt 6 Widgets, DTK 6 Widgets, QTest, CMake/CTest

---

## File Structure

- Modify `ui-host/OnboardingDialog.cpp`: configure the dialog's native DTK title bar and native setup group while preserving the current controller/rendering paths.
- Modify `tests/test_onboarding_dialog.cpp`: verify the title bar hierarchy, absence of the old centered title, grouped setup rows, accessibility, and minimum-size error reachability.
- Modify `docs/superpowers/specs/2026-07-18-onboarding-visual-refresh-design.md`: already revised in commit `cc3c086`; no further design change is planned.

### Task 1: Put the welcome identity in a real DTK title bar

A minimal `DDialog` probe establishes the ownership rule for this task: a fresh dialog has no content widgets and already has exactly one direct-child `DTitlebar`. Reuse that title bar; creating another one would duplicate the native title-bar surface and break the dialog content hierarchy.

**Files:**
- Modify: `tests/test_onboarding_dialog.cpp`
- Modify: `ui-host/OnboardingDialog.cpp`

- [ ] **Step 1: Write the failing title-bar test**

Add DTK title-bar coverage to `TestOnboardingDialog::usesApprovedVisualStoryAndAccessibleImages()`:

```cpp
#include <DTitlebar>

const auto titlebars = dialog.findChildren<Dtk::Widget::DTitlebar *>(
    QString(), Qt::FindDirectChildrenOnly);
QCOMPARE(titlebars.size(), 1);
auto *titlebar = titlebars.constFirst();
QCOMPARE(titlebar->objectName(), QStringLiteral("onboardingTitlebar"));

auto *stackedPages =
    dialog.findChild<QStackedWidget *>(QStringLiteral("pages"));
QVERIFY(stackedPages);
QCOMPARE(dialog.contentCount(), 1);
QCOMPARE(dialog.getContent(0), stackedPages->parentWidget());
QVERIFY(dialog.getContent(0) != titlebar);
QCOMPARE(dialog.title(), QString());
QVERIFY(dialog.icon().isNull());

auto *titleLabel = titlebar->findChild<QLabel *>(
    QStringLiteral("onboardingTitleLabel"));
auto *titleIcon = titlebar->findChild<QLabel *>(
    QStringLiteral("onboardingTitleIcon"));
QVERIFY(titleLabel);
QVERIFY(titleIcon);
QCOMPARE(titleLabel->text(), QStringLiteral("欢迎使用 EchoFlow"));
QCOMPARE(titleLabel->accessibleName(), QStringLiteral("欢迎使用 EchoFlow"));
QVERIFY(!titleIcon->pixmap(Qt::ReturnByValue).isNull());
QCOMPARE(titleIcon->pixmap(Qt::ReturnByValue).cacheKey(),
         QApplication::windowIcon().pixmap(20, 20).cacheKey());
QVERIFY(titleIcon->accessibleName().isEmpty());
```

- [ ] **Step 2: Run the focused test and confirm the expected failure**

Run:

```bash
cmake --build /tmp/echoflow-onboarding-dtk-baseline -j2
ctest --test-dir /tmp/echoflow-onboarding-dtk-baseline \
  -R test_onboarding_dialog --output-on-failure
```

Expected: `test_onboarding_dialog` fails because the existing direct-child title bar is not yet configured as `onboardingTitlebar`. Any implementation that adds a second title bar as dialog content also fails the single-titlebar and single-content assertions.

- [ ] **Step 3: Implement the DTK title bar**

In `ui-host/OnboardingDialog.cpp`, include `DTitlebar`, clear the centered `DDialog` title/icon, retrieve the title bar that `DDialog` already created as a direct child, and configure its custom widget:

```cpp
#include <DTitlebar>

setWindowIcon(QApplication::windowIcon());
setTitle({});
setIcon({});

auto *titlebar = findChild<Dtk::Widget::DTitlebar *>(
    QString(), Qt::FindDirectChildrenOnly);
Q_ASSERT(titlebar);
titlebar->setObjectName(QStringLiteral("onboardingTitlebar"));
titlebar->setBackgroundTransparent(true);
titlebar->setSeparatorVisible(false);

auto *titleWidget = new QWidget(titlebar);
auto *titleLayout = new QHBoxLayout(titleWidget);
titleLayout->setContentsMargins(0, 0, 0, 0);
titleLayout->setSpacing(8);

auto *titleIcon = new QLabel(titleWidget);
titleIcon->setObjectName(QStringLiteral("onboardingTitleIcon"));
titleIcon->setAccessibleName({});
titleIcon->setPixmap(QApplication::windowIcon().pixmap(20, 20));
titleIcon->setFixedSize(20, 20);

auto *titleLabel = new QLabel(QStringLiteral("欢迎使用 EchoFlow"), titleWidget);
titleLabel->setObjectName(QStringLiteral("onboardingTitleLabel"));
titleLabel->setAccessibleName(titleLabel->text());
QFont titleFont = titleLabel->font();
titleFont.setBold(true);
titleLabel->setFont(titleFont);

titleLayout->addWidget(titleIcon);
titleLayout->addWidget(titleLabel);
titlebar->setCustomWidget(titleWidget);
```

Do not allocate another `DTitlebar` and do not pass a title bar to `addContent()`. Keep the existing `addContent(content)` call so the slideshow remains the sole dialog content widget; `setWindowIcon(QApplication::windowIcon())` retains the application icon for the window manager and task switcher.

- [ ] **Step 4: Run the title-bar test**

Run:

```bash
cmake --build /tmp/echoflow-onboarding-dtk-baseline -j2
ctest --test-dir /tmp/echoflow-onboarding-dtk-baseline \
  -R test_onboarding_dialog --output-on-failure
```

Expected: `test_onboarding_dialog` passes and the existing four-page/navigation assertions remain green.

- [ ] **Step 5: Commit the title-bar change**

```bash
git add ui-host/OnboardingDialog.cpp tests/test_onboarding_dialog.cpp
git commit -m "fix(onboarding): reuse dialog title bar" \
  -m "Configure DDialog's existing native title bar instead of adding a second one as content. Keep the slideshow as the sole content widget and mark the adjacent application icon as decorative for accessibility."
```

### Task 2: Restyle the setup page as one native DTK settings group

**Files:**
- Modify: `tests/test_onboarding_dialog.cpp`
- Modify: `ui-host/OnboardingDialog.cpp`

- [ ] **Step 1: Write the failing setup-group test**

Add a focused test slot named `usesNativeDtkSetupGroup()` and implement it as follows:

```cpp
#include <DBackgroundGroup>

void TestOnboardingDialog::usesNativeDtkSetupGroup()
{
    QTemporaryDir dir;
    OnboardingState state(dir.filePath(QStringLiteral("ui-state.ini")));
    FakeModelSource model;
    FakeCommandRunner runner;
    OnboardingSetupController controller(&model, &runner, &state);
    finishInitialNotReady(runner);
    OnboardingDialog dialog(&controller);

    auto *group = dialog.findChild<Dtk::Widget::DBackgroundGroup *>(
        QStringLiteral("setupStatusGroup"));
    QVERIFY(group);
    QCOMPARE(group->backgroundRole(), QPalette::Base);

    for (const QString &name : {
             QStringLiteral("modelSetupRow"),
             QStringLiteral("serviceSetupRow"),
             QStringLiteral("fcitxSetupRow")}) {
        auto *row = dialog.findChild<QWidget *>(name);
        QVERIFY2(row, qPrintable(name));
        QCOMPARE(row->parentWidget(), group);
        QVERIFY(!qobject_cast<QFrame *>(row));
    }

    QCOMPARE(group->findChildren<QFrame *>(QString(),
                                            Qt::FindDirectChildrenOnly)
                 .size(),
             0);
}
```

- [ ] **Step 2: Run the focused test and confirm the expected failure**

Run:

```bash
cmake --build /tmp/echoflow-onboarding-dtk-baseline -j2
ctest --test-dir /tmp/echoflow-onboarding-dtk-baseline \
  -R test_onboarding_dialog --output-on-failure
```

Expected: failure because `setupStatusGroup` does not exist.

- [ ] **Step 3: Replace outlined frames with `DBackgroundGroup` rows**

Include `DBackgroundGroup`. In `createSetupPage()`, create one group and a vertical group layout before the row-builder lambda:

```cpp
#include <DBackgroundGroup>

auto *groupLayout = new QVBoxLayout;
groupLayout->setContentsMargins(0, 0, 0, 0);
groupLayout->setSpacing(0);
auto *statusGroup = new Dtk::Widget::DBackgroundGroup(
    groupLayout, setupContent);
statusGroup->setObjectName(QStringLiteral("setupStatusGroup"));
statusGroup->setBackgroundRole(QPalette::Base);
statusGroup->setItemMargins(QMargins(12, 8, 12, 8));
statusGroup->setItemSpacing(0);
```

Change the row builder to create a plain `QWidget` parented to `statusGroup`, assign the explicit row object name, and add the row widget to `groupLayout`. Keep returning its `QVBoxLayout *` so model progress remains inserted beneath the model heading:

```cpp
auto addRow = [groupLayout, statusGroup](
                  const QString &rowName, const QString &name,
                  const QString &statusName, const QString &errorName,
                  QLabel **status, QLabel **error) {
    auto *rowWidget = new QWidget(statusGroup);
    rowWidget->setObjectName(rowName);
    auto *row = new QVBoxLayout(rowWidget);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);

    auto *heading = new QHBoxLayout;
    auto *nameLabel = new QLabel(name, rowWidget);
    QFont font = nameLabel->font();
    font.setBold(true);
    nameLabel->setFont(font);
    *status = new QLabel(rowWidget);
    (*status)->setObjectName(statusName);
    (*status)->setAccessibleName(name + QStringLiteral("状态"));
    heading->addWidget(nameLabel);
    heading->addStretch();
    heading->addWidget(*status);
    row->addLayout(heading);

    *error = wrappedLabel({}, rowWidget);
    (*error)->setObjectName(errorName);
    (*error)->setAccessibleName(name + QStringLiteral("错误"));
    (*error)->hide();
    row->addWidget(*error);
    groupLayout->addWidget(rowWidget);
    return row;
};
```

Call the builder with `modelSetupRow`, `serviceSetupRow`, and `fcitxSetupRow`, then add `statusGroup` once to the outer setup layout. Do not change `renderSetup()`, `renderProgress()`, retry handling, or controller connections.

- [ ] **Step 4: Run setup behavior and layout tests**

Run:

```bash
cmake --build /tmp/echoflow-onboarding-dtk-baseline -j2
ctest --test-dir /tmp/echoflow-onboarding-dtk-baseline \
  -R 'test_onboarding_(dialog|setup_controller)' --output-on-failure
```

Expected: both tests pass, including the existing long-error scroll reachability test.

- [ ] **Step 5: Commit the setup-group change**

```bash
git add ui-host/OnboardingDialog.cpp tests/test_onboarding_dialog.cpp
git commit -m "fix(onboarding): align setup page with DTK settings" \
  -m "Replace individually outlined setup panels with one theme-aware DTK background group. Keep progress, errors, retries, and setup state rendering attached to their existing labels."
```

### Task 3: Verify the real rendered dialog and full focused suite

**Files:**
- Modify only if visual verification exposes a concrete defect: `ui-host/OnboardingDialog.cpp`, `tests/test_onboarding_dialog.cpp`

- [ ] **Step 1: Build the standalone UI and focused tests from clean directories**

Run:

```bash
cmake -S tests -B /tmp/echoflow-onboarding-dtk-final-tests \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build /tmp/echoflow-onboarding-dtk-final-tests -j2
ctest --test-dir /tmp/echoflow-onboarding-dtk-final-tests --output-on-failure

cmake -S ui-host -B /tmp/echoflow-onboarding-dtk-final-ui \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build /tmp/echoflow-onboarding-dtk-final-ui -j2
```

Expected: 5/5 focused tests pass and `echoflow-ui` links successfully.

- [ ] **Step 2: Run spec, syntax, and diff hygiene checks**

Run:

```bash
bash tests/spec/run_spec.sh
bash -n install-user.sh uninstall-user.sh tests/spec/*.sh
sh -n run.sh
git diff --check cc3c086..HEAD
git status --short
```

Expected: 201 spec checks pass, syntax and diff checks return zero, and only intentional plan/source/test changes are listed.

- [ ] **Step 3: Capture the actual light and dark dialog**

Reuse the production `OnboardingDialog` visual probe with fake setup dependencies. Capture pages 1 and 4, plus page 4 with long per-item errors, at the current desktop device-pixel ratio in both DTK light and dark palettes.

Expected visual result:

- the title bar is the first row of the window and contains the app icon, welcome text, and native close affordance;
- no centered `欢迎使用 EchoFlow` remains above the slideshow page;
- page 1 gains usable vertical space without moving its footer;
- page 4 shows one rounded DTK group rather than three black outlined boxes;
- state text, progress, long errors, dots, and footer buttons remain visible and theme-correct.

- [ ] **Step 4: Run a final accessibility/layout review**

Confirm with the existing QTest assertions and screenshots that keyboard navigation is unchanged, the title icon/text have accessible names, each status and error remains announced by its existing name, focus states remain visible, and neither theme introduces low-contrast borders or clipped text.

- [ ] **Step 5: Commit only evidence-driven visual corrections, if needed**

If Step 3 reveals a defect, add a regression assertion first, implement the minimum spacing/palette correction, rerun Steps 1–4, and commit only the affected source and test files with a Conventional Commit subject and explanatory body. If no defect appears, make no additional source commit.
