# Capsule Live Text Scroll Design

## Goal

Keep the recording capsule at no more than one third of the current screen's
available width. When live recognition text no longer fits, preserve the fixed
waveform and action button while smoothly moving older text left so newly
appended text remains visible on the right.

## Design

The recording capsule computes its natural width from the waveform, spacing,
live text, and action button, then clamps that width to one third of the
window's current screen width. Idle and transcribing states retain their
existing sizing behavior.

The live-text area becomes a clipped viewport occupying the space between the
waveform and action button. A single-line label inside the viewport keeps its
implicit width. While the label fits, it remains at the viewport's left edge.
Once it overflows, its right edge is aligned with the viewport's right edge.
An animation on the label's horizontal position makes each partial-result
update push existing text smoothly left and reveal the new suffix from the
right. When text becomes short enough or recording ends, the offset returns to
zero.

Only the live-text label moves. The waveform, capsule background, and action
button stay fixed, and clipping prevents text from drawing over them.

## Verification

Extend the existing shell specification checks to require:

- a maximum recording width derived from one third of the current screen;
- a clipped live-text viewport;
- right-edge positioning for overflowing text; and
- an animation on the text's horizontal position.

Run the focused specification test, then build and run the full CTest suite.
Manually verify with streaming transcription that the capsule grows until the
limit and subsequent partial results slide left while the newest text remains
visible.
