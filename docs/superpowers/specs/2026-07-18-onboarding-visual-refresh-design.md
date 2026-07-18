# EchoFlow Onboarding Visual Refresh Design

Date: 2026-07-18
Status: approved

## Context

The first-run onboarding is functionally complete, but its first three pages
are mostly headings and paragraphs. The result reads like a settings form
rather than a product introduction. This refresh keeps the existing activation,
setup, persistence, retry, settings, and tray behavior while replacing the
presentation with an image-led slideshow.

## Visual Direction

The approved direction is a soft 3D illustration style in a left-image,
right-copy layout:

- blue and violet materials with soft depth, rounded forms, and restrained
  highlights;
- one clear visual metaphor per page rather than decorative background art;
- an approximately 45/55 split between illustration and content;
- a dialog target size of approximately 680 by 500 logical pixels; and
- adaptive DTK surfaces and text colors around the illustrations.

The illustrations contain no words, numbers, buttons, status text, or product
screenshots. All meaningful text remains native Qt content so it stays sharp,
selectable, accessible, translatable, and theme-aware. Each illustration is a
transparent PNG stored in the UI resource bundle. Blue-violet midtones are
chosen to remain legible on both light and dark DTK surfaces without requiring
separate theme variants.

## Four-Page Storyboard

### 1. Say it, type it

The illustration is a dimensional microphone surrounded by a soft sound form.
The heading is `说话，就能输入`. The supporting copy states that recognition
runs locally and recordings do not leave the device. A compact semantic tag
summarizes `离线 · 隐私 · 快速`.

### 2. Right Ctrl gesture

The illustration is a raised right Ctrl keycap with a short waveform. The
heading is `按右 Ctrl，开始说话`. The supporting copy explains that the second
press stops recording and commits the transcript to the focused input field.
The visual reinforces the specific key but does not replace the text.

### 3. Tray and settings

The illustration combines a tray bubble and settings controls in the same 3D
language without reproducing a literal screenshot. The heading is
`需要调整？都在托盘里`. The supporting copy mentions models, language,
microphone selection, and replaying the guide.

### 4. Prepare EchoFlow

The illustration uses a download symbol and small service nodes. The right
side retains the real model progress, service state, Fcitx state, error text,
and retry action from the current implementation. The heading is
`准备好，就可以开始`. Setup state remains the dominant information on this
page; the illustration is smaller than on the first three pages.

## Layout and Interaction

`OnboardingDialog` keeps its `QStackedWidget` and existing navigation signals.
The three information pages use a shared page builder that accepts an image
resource, accessible image description, heading, body, and optional semantic
tag. The setup page uses the same two-column shell but supplies its current
status widgets as the right-side content.

The footer keeps Back, Next, retry, and completion behavior. The textual page
counter is replaced visually by four compact dots while retaining an
accessible progress description such as `第 2 页，共 4 页`. Back and primary
actions retain visible text and keyboard focus. No swipe-only navigation or
automatic page advance is added.

If an illustration cannot be loaded, its label collapses without hiding the
heading, body, or setup controls. Image failure cannot prevent setup or change
onboarding completion state.

## Asset Production

Four illustrations are generated as a coordinated set, not as unrelated
images. Prompts use the same material, lighting, camera angle, blue-violet
palette, object scale, and empty background treatment. Assets are generated
without text or logos on a flat removable chroma-key background, converted to
transparent PNG locally, inspected for edge artifacts, and copied into
`ui-host/onboarding/`.

Source images are normalized to a common square canvas. Runtime rendering uses
smooth aspect-ratio-preserving scaling and fixed layout bounds so page changes
do not resize the dialog. Assets are compressed losslessly enough to avoid an
unnecessary package-size increase while preserving clean antialiased edges.

## Scope Boundaries

This change does not alter:

- first-run or replay routing;
- the onboarding version or state file;
- model selection, download, resume, or cancellation;
- systemd or Fcitx commands;
- setup state reconstruction, timeouts, retry, or completion rules; or
- settings and tray entry points.

There is no animation in this refresh. Motion may be considered separately
after the static composition is proven in the real DTK window.

## Testing and Verification

Focused widget tests verify that every page exposes its image label, heading,
body, accessible description, and expected object names. Existing navigation
and setup tests continue to verify behavior. A resource/spec test prevents
missing or unbundled illustrations. Tests must remain independent of display
hardware, network access, services, and model weights.

Manual visual verification uses the real standalone `echoflow-ui` build on the
desktop. It covers all four pages in light and dark themes, keyboard focus,
100% and enlarged text scaling, image aspect ratio, setup progress, multiline
errors, retry, and missing-image fallback. The final screenshots must show no
clipped text, unexpected white image backgrounds, layout jumps, or controls
below the visible window bounds.

## Acceptance Criteria

- The first three pages are image-led and no longer resemble a list of labels.
- All four pages use a consistent soft 3D blue-violet visual language.
- The approved left-image, right-copy composition remains balanced at the
  dialog's minimum size.
- The final page preserves all real setup progress, errors, retry, and
  completion behavior.
- Dark and light themes use native DTK surfaces with no baked rectangular
  background around illustrations.
- The guide remains fully understandable and operable if images are missing or
  unavailable to accessibility tools.
- Focused onboarding tests, resource checks, standalone UI build, and manual
  visual inspection pass.
