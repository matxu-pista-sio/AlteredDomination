#pragma once

class QQmlApplicationEngine;

namespace ad::client {

/// Headless test driver, armed by AD_DRIVE=1 (see the share-screenshot
/// skill): reads line commands from stdin and answers on stdout, so the
/// running client can be screenshotted and driven on hosts with no window
/// system tooling at all - grabWindow() renders through whatever
/// scenegraph backend is live, and input is posted straight to the
/// QQuickWindow.
///
///     shot <path>      render the window and save it as an image
///     click <x> <y>    synthesize a left click at window coordinates
///     rclick <x> <y>   right click
///     move <x> <y>     move the pointer (hover)
///     wheel <x> <y> <delta>
///     key <name>       synthesize a key press (QKeySequence names)
///     type <text...>   text into whatever has focus
///     size <w> <h>     pin the window geometry
///     float <w> <h> <x> <y>  take the window from the WM and park it
///     page <name>      jump the StackView to a page (Main.qml's devPage())
///     eval <js>        run JavaScript in the root context, print the result
///     wait <ms>        answer after a delay (lets animations settle)
///     quit             exit the application
///
/// Every command answers "ok ..." or "err ..." on stdout when it has run.
void installDevDrive(QQmlApplicationEngine& engine);

} // namespace ad::client
