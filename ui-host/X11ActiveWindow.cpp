// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "X11ActiveWindow.h"

#include <cstdlib>
#include <cstring>
#include <memory>

#include <QGuiApplication>

#include <xcb/xcb.h>

namespace echoflow {

namespace {

struct FreeDeleter {
    void operator()(void *pointer) const { std::free(pointer); }
};

template <typename T>
using Reply = std::unique_ptr<T, FreeDeleter>;

xcb_window_t activeWindow(xcb_connection_t *connection, xcb_window_t root) {
    constexpr const char kName[] = "_NET_ACTIVE_WINDOW";
    Reply<xcb_intern_atom_reply_t> atom(xcb_intern_atom_reply(
        connection, xcb_intern_atom(connection, 1, std::strlen(kName), kName), nullptr));
    if (!atom || atom->atom == XCB_ATOM_NONE) {
        return XCB_WINDOW_NONE;
    }
    Reply<xcb_get_property_reply_t> property(xcb_get_property_reply(
        connection,
        xcb_get_property(connection, 0, root, atom->atom, XCB_ATOM_WINDOW, 0, 1),
        nullptr));
    if (!property || xcb_get_property_value_length(property.get()) < 4) {
        return XCB_WINDOW_NONE;
    }
    return *static_cast<xcb_window_t *>(xcb_get_property_value(property.get()));
}

// The active window is a client window; with a reparenting window manager
// the root's direct child is its frame.
xcb_window_t topLevelAncestor(xcb_connection_t *connection, xcb_window_t root,
                              xcb_window_t window) {
    for (int depth = 0; window != XCB_WINDOW_NONE && depth < 16; ++depth) {
        Reply<xcb_query_tree_reply_t> tree(
            xcb_query_tree_reply(connection, xcb_query_tree(connection, window), nullptr));
        if (!tree) {
            return XCB_WINDOW_NONE;
        }
        if (tree->parent == root) {
            return window;
        }
        window = tree->parent;
    }
    return XCB_WINDOW_NONE;
}

} // namespace

bool pointerInActiveWindow() {
    auto *x11 = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QX11Application>()
                        : nullptr;
    xcb_connection_t *connection = x11 ? x11->connection() : nullptr;
    if (!connection) {
        return true;
    }
    const xcb_setup_t *setup = xcb_get_setup(connection);
    const xcb_window_t root = xcb_setup_roots_iterator(setup).data->root;

    const xcb_window_t active = activeWindow(connection, root);
    if (active == XCB_WINDOW_NONE) {
        return true;
    }
    Reply<xcb_query_pointer_reply_t> pointer(
        xcb_query_pointer_reply(connection, xcb_query_pointer(connection, root), nullptr));
    if (!pointer) {
        return true;
    }
    return pointer->child != XCB_WINDOW_NONE
        && pointer->child == topLevelAncestor(connection, root, active);
}

} // namespace echoflow
