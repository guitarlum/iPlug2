/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

/**
 * @file
 * X11 CLIPBOARD selection for IGraphicsLinux.
 *
 * The header does not include X11: its macros must not leak into plug-in
 * sources. Get waits at most kWaitMs for SelectionNotify and leaves every
 * other event queued for the graphics loop. A selection this window owns is
 * answered from the cache, with no round trip.
 */

#include <string>

struct _XDisplay;

namespace iplug
{
namespace igraphics
{
namespace linux_clipboard
{

/** Upper bound on a clipboard read, in milliseconds. Not a dialog wait. */
constexpr int kWaitMs = 100;

bool SetText(_XDisplay* display, unsigned long window, const char* text);
bool GetText(_XDisplay* display, unsigned long window, std::string& out);

/** SelectionRequest / SelectionClear. Returns true when the event was ours. */
bool HandleEvent(_XDisplay* display, unsigned long window, void* xevent);

void ForgetWindow(unsigned long window);

} // namespace linux_clipboard
} // namespace igraphics
} // namespace iplug
