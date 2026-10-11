// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace echoflow {

// Whether the top-level window under the pointer is the window manager's
// active window (_NET_ACTIVE_WINDOW). Returns true when this cannot be
// determined, e.g. outside X11, so the check never blocks on its own.
bool pointerInActiveWindow();

} // namespace echoflow
