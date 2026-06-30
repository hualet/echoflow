# Capsule Notification Material Design

## Goal

Align the voice capsule's surface treatment with the deepin notification
components while preserving its existing capsule shape, dimensions, layout,
animation, and interaction behavior.

## Design

Keep the capsule as a 40-pixel-high container with its existing 20-pixel
radius and dynamic width. Replace its opaque fill and single `Rectangle`
border with DTK's public compositing primitives:

- Enable window blur through `D.DWindow.enableBlurWindow`.
- Fill the capsule with a rounded `D.StyledBehindWindowBlur` whose blend
  color follows the current light or dark capsule background.
- Draw `D.OutsideBoxBorder` above the blur using the notification component's
  light/dark outer-border colors.
- Draw `D.InsideBoxBorder` above the blur using the notification component's
  light/dark inner-highlight colors.

The border colors match `NotifyItemBackground`: in the light theme, a
10%-opaque black outside border and 20%-opaque white inside border; in the
dark theme, a 60%-opaque black outside border and 10%-opaque white inside
border. The existing theme bridge remains the source of the capsule's blend
color and content colors.

Do not import or instantiate `org.deepin.ds.notification.NotifyItemBackground`.
That module is a dde-shell implementation dependency, while the four DTK
primitives above are public and provide the reusable parts needed here.

## Scope

Only the capsule presentation in `qml/EchoFlowTooltip.qml` and its focused
spec assertions change. Service state, Fcitx integration, positioning, size
calculation, controls, text, and animations remain unchanged. No notification
drop shadow is added because it would change the capsule's existing visual
footprint.

## Verification

Add source-level spec assertions for window blur and both DTK border layers,
then run the spec suite, build, and QML lint. Verify at runtime in both light
and dark themes when the current desktop session permits launching the UI
without disrupting an existing instance.
