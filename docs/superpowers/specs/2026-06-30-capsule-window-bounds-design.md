# Capsule Window Bounds Design

## Goal

Remove the oversized rounded background around the voice capsule while
preserving the capsule's current shape, material, content, and interaction.
Match dde-osd's approach, where the DTK background blur fills a top-level
window whose bounds match its visual content.

## Root Cause

The capsule is centered inside a top-level window that is 16 pixels wider and
taller than the capsule. Because DTK window blur applies to the top-level
window, the eight-pixel margin on every side is also blurred and appears as a
larger rounded rectangle behind the capsule.

## Design

Set the top-level window width and height directly from the capsule width and
height. Keep the capsule centered in the window and retain its 40-pixel height,
20-pixel radius, dynamic width, DTK blur, borders, colors, animation, and mouse
handling.

The existing bottom-center positioning formulas remain unchanged. With the
extra window margin removed, `targetX` and `targetY` become the capsule's actual
bottom-center anchor, as documented in the QML source.

## Scope

Only the window-size bindings in `qml/EchoFlowTooltip.qml` and focused static
spec assertions change. Do not change service or Fcitx communication,
positioning inputs, capsule dimensions, state transitions, or visual material.

## Verification

Add spec assertions that require the window dimensions to match the capsule and
reject the previous 16-pixel expansion. Run the spec suite, build the project,
and run the full CTest suite. When possible without disrupting an existing UI
instance, verify that the rendered window has no background outside the capsule.
