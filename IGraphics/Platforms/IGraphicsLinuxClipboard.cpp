/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#include "IGraphicsLinuxClipboard.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <chrono>
#include <cstring>
#include <poll.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace iplug
{
namespace igraphics
{
namespace linux_clipboard
{
namespace
{

struct Clip
{
  std::string text;
  bool owned = false;
  Atom utf8 = None;
  Atom targets = None;
  Atom clipboard = None;
  Atom property = None;
  Atom incr = None;
};

std::unordered_map<unsigned long, Clip> gClips;

Clip& State(Display* display, Window window)
{
  Clip& clip = gClips[window];
  if (clip.clipboard == None)
  {
    clip.utf8 = XInternAtom(display, "UTF8_STRING", False);
    clip.targets = XInternAtom(display, "TARGETS", False);
    clip.clipboard = XInternAtom(display, "CLIPBOARD", False);
    clip.property = XInternAtom(display, "VOLUM_CLIP", False);
    clip.incr = XInternAtom(display, "INCR", False);
  }
  return clip;
}

bool WaitForNotify(Display* display, Window window, XEvent& event, std::chrono::steady_clock::time_point deadline)
{
  const int xfd = ConnectionNumber(display);
  for (;;)
  {
    if (XCheckTypedWindowEvent(display, window, SelectionNotify, &event))
      return true;
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    if (left <= 0)
      return false;
    pollfd pfd;
    pfd.fd = xfd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    poll(&pfd, 1, static_cast<int>(left));
  }
}

bool ReadProperty(Display* display, Window window, Atom property, Atom incr, std::string& out)
{
  Atom actual = None;
  int format = 0;
  unsigned long count = 0;
  unsigned long remaining = 0;
  unsigned char* data = nullptr;
  if (XGetWindowProperty(display, window, property, 0, 1 << 20, True, AnyPropertyType, &actual, &format, &count, &remaining, &data) != Success)
    return false;
  if (!data)
    return false;
  if (actual == incr || format != 8)
  {
    XFree(data);
    return false;
  }
  out.assign(reinterpret_cast<char*>(data), count);
  XFree(data);
  return true;
}

void Answer(Display* display, const Clip& clip, const XSelectionRequestEvent& request)
{
  XSelectionEvent notify;
  std::memset(&notify, 0, sizeof(notify));
  notify.type = SelectionNotify;
  notify.display = display;
  notify.requestor = request.requestor;
  notify.selection = request.selection;
  notify.target = request.target;
  notify.property = None;
  notify.time = request.time;

  const Atom dest = request.property == None ? request.target : request.property;
  if (request.target == clip.targets)
  {
    long atoms[3];
    atoms[0] = static_cast<long>(clip.targets);
    atoms[1] = static_cast<long>(clip.utf8);
    atoms[2] = XA_STRING;
    XChangeProperty(display, request.requestor, dest, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char*>(atoms), 3);
    notify.property = dest;
  }
  else if (request.target == clip.utf8 || request.target == XA_STRING)
  {
    XChangeProperty(display, request.requestor, dest, request.target, 8, PropModeReplace, reinterpret_cast<const unsigned char*>(clip.text.data()), static_cast<int>(clip.text.size()));
    notify.property = dest;
  }
  XSendEvent(display, request.requestor, False, 0, reinterpret_cast<XEvent*>(&notify));
  XFlush(display);
}

} // namespace

bool SetText(_XDisplay* display, unsigned long window, const char* text)
{
  if (!display || window == 0)
    return false;
  auto* xdisplay = reinterpret_cast<Display*>(display);
  Clip& clip = State(xdisplay, window);
  clip.text = text ? text : "";
  XSetSelectionOwner(xdisplay, clip.clipboard, window, CurrentTime);
  clip.owned = XGetSelectionOwner(xdisplay, clip.clipboard) == window;
  XFlush(xdisplay);
  return clip.owned;
}

bool GetText(_XDisplay* display, unsigned long window, std::string& out)
{
  out.clear();
  if (!display || window == 0)
    return false;
  auto* xdisplay = reinterpret_cast<Display*>(display);
  Clip& clip = State(xdisplay, window);
  if (clip.owned && XGetSelectionOwner(xdisplay, clip.clipboard) == window)
  {
    out = clip.text;
    return true;
  }
  clip.owned = false;

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kWaitMs);
  const Atom targets[2] = {clip.utf8, XA_STRING};
  for (Atom target : targets)
  {
    if (std::chrono::steady_clock::now() >= deadline)
      break;
    XConvertSelection(xdisplay, clip.clipboard, target, clip.property, window, CurrentTime);
    XFlush(xdisplay);
    XEvent event;
    if (!WaitForNotify(xdisplay, window, event, deadline))
      return false;
    if (event.xselection.property == None)
      continue;
    if (ReadProperty(xdisplay, window, event.xselection.property, clip.incr, out))
      return true;
  }
  return false;
}

bool HandleEvent(_XDisplay* display, unsigned long window, void* xevent)
{
  if (!display || window == 0 || !xevent)
    return false;
  auto* xdisplay = reinterpret_cast<Display*>(display);
  auto* event = static_cast<XEvent*>(xevent);
  if (event->type != SelectionRequest && event->type != SelectionClear)
    return false;
  if (event->xany.window != window)
    return false;
  Clip& clip = State(xdisplay, window);
  if (event->type == SelectionClear)
  {
    if (event->xselectionclear.selection == clip.clipboard)
      clip.owned = false;
    return true;
  }
  if (event->xselectionrequest.selection != clip.clipboard || !clip.owned)
  {
    // Refuse immediately. Dropping the event leaves the requestor waiting.
    XSelectionEvent notify;
    std::memset(&notify, 0, sizeof(notify));
    notify.type = SelectionNotify;
    notify.display = xdisplay;
    notify.requestor = event->xselectionrequest.requestor;
    notify.selection = event->xselectionrequest.selection;
    notify.target = event->xselectionrequest.target;
    notify.property = None;
    notify.time = event->xselectionrequest.time;
    XSendEvent(xdisplay, notify.requestor, False, 0, reinterpret_cast<XEvent*>(&notify));
    XFlush(xdisplay);
    return true;
  }
  Answer(xdisplay, clip, event->xselectionrequest);
  return true;
}

void ForgetWindow(unsigned long window) { gClips.erase(window); }

} // namespace linux_clipboard
} // namespace igraphics
} // namespace iplug
