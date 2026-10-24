/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

// clang-format off
// Order is load-bearing. IGraphicsLinux.h pulls in IGraphics and glad before
// any Xlib macro exists. glad sets __gl_h_, so GL/glx.h skips GL/gl.h, and
// glx.h is parsed while X11 Bool is still defined. IGraphicsLinuxGL.h then
// undefs None/True/False/CurrentTime, hence the k* constants below.
#include "IGraphicsLinux.h"
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <GL/glx.h>
#include "IGraphicsLinuxGL.h"
// clang-format on

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <vector>

#include "IGraphicsLinuxFont.h"
#include "IGraphicsLinuxKeys.h"
#include "IPopupMenuControl.h"
#include "ITextEntryControl.h"
#include "Linux/IPlugLinuxRunLoop.h"

using namespace iplug;
using namespace igraphics;

namespace
{
constexpr unsigned long kNone = 0L;
constexpr unsigned long kCurrentTime = 0L;
constexpr int kTrue = 1;
constexpr int kFalse = 0;
constexpr uint32_t kDisplayTimerMs = 16;
constexpr unsigned long kDoubleClickMs = 400;

__attribute__((format(printf, 1, 2))) void Log(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  fprintf(stderr, "IGraphicsLinux: ");
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
}

// XSetErrorHandler is process-wide. Ours is installed while any editor has a
// connection open. Errors on our connections are logged and survived; errors
// on the host's connections go to the handler that was there before.
std::vector<Display*> sDisplays;
XErrorHandler sPrevErrorHandler = nullptr;
int sXErrorCount = 0;

int OnXError(Display* pDisplay, XErrorEvent* pError)
{
  if (std::find(sDisplays.begin(), sDisplays.end(), pDisplay) == sDisplays.end())
    return sPrevErrorHandler ? sPrevErrorHandler(pDisplay, pError) : 0;

  char text[128] = {};
  XGetErrorText(pDisplay, pError->error_code, text, sizeof(text));
  fprintf(stderr, "IGraphicsLinux: X error %s (request %d.%d, resource 0x%lx)\n", text, pError->request_code, pError->minor_code, pError->resourceid);
  ++sXErrorCount;
  return 0;
}

void AddDisplay(Display* pDisplay)
{
  if (sDisplays.empty())
    sPrevErrorHandler = XSetErrorHandler(OnXError);
  sDisplays.push_back(pDisplay);
}

void RemoveDisplay(Display* pDisplay)
{
  sDisplays.erase(std::remove(sDisplays.begin(), sDisplays.end(), pDisplay), sDisplays.end());
  if (!sDisplays.empty())
    return;
  // Restore the old handler only if nobody installed one on top of ours.
  XErrorHandler current = XSetErrorHandler(sPrevErrorHandler);
  if (current != OnXError)
    XSetErrorHandler(current);
  sPrevErrorHandler = nullptr;
}

IMouseMod ModFromState(unsigned int state, unsigned int pressed, unsigned int released)
{
  const bool left = ((state & Button1Mask) || pressed == Button1) && released != Button1;
  const bool right = ((state & Button3Mask) || pressed == Button3) && released != Button3;
  return IMouseMod(left, right, state & ShiftMask, state & ControlMask, state & Mod1Mask);
}
} // namespace

IGraphicsLinux::IGraphicsLinux(IGEditorDelegate& dlg, int w, int h, int fps, float scale)
: IGRAPHICS_DRAW_CLASS(dlg, w, h, fps, scale)
{
}

IGraphicsLinux::~IGraphicsLinux() { CloseWindow(); }

int IGraphicsLinux::XErrorCount() { return sXErrorCount; }

#pragma mark - Window

void* IGraphicsLinux::OpenWindow(void* pParent)
{
  CloseWindow();

  mDisplay = XOpenDisplay(nullptr);
  if (!mDisplay)
  {
    Log("cannot open the X display %s", getenv("DISPLAY") ? getenv("DISPLAY") : "(DISPLAY unset)");
    return nullptr;
  }
  AddDisplay(mDisplay);

  const float scale = DetectScreenScale();
  const unsigned long parent = pParent ? reinterpret_cast<unsigned long>(pParent) : RootWindow(mDisplay, DefaultScreen(mDisplay));
  if (!CreateGLWindow(parent, static_cast<int>(WindowWidth() * scale), static_cast<int>(WindowHeight() * scale)))
  {
    DestroyGLWindow();
    return nullptr;
  }

  ActivateGLContext();
  if (!LoadGL() || !GLAD_GL_VERSION_2_0)
  {
    Log("OpenGL 2.0 is not available");
    DeactivateGLContext();
    DestroyGLWindow();
    return nullptr;
  }
  OnViewInitialized(nullptr);
  SetScreenScale(scale); // creates the main FBO and tells the host the physical size
  GetDelegate()->LayoutUI(this);
  SetAllControlsDirty();
  GetDelegate()->OnUIOpen();
  DeactivateGLContext();

  mNeedsFullRedraw = true;
  mFdBinding = IPlugLinuxBindFd(ConnectionNumber(mDisplay), [](int, void* p) { static_cast<IGraphicsLinux*>(p)->ProcessX11Events(); }, this);
  mTimerBinding = IPlugLinuxBindTimer(kDisplayTimerMs, [](void* p) { static_cast<IGraphicsLinux*>(p)->OnDisplayTimer(); }, this);
  XFlush(mDisplay);
  return GetWindow();
}

void IGraphicsLinux::CloseWindow()
{
  // Unbind first: after this no run-loop callback can reach X or GL.
  IPlugLinuxUnbindTimer(mTimerBinding);
  IPlugLinuxUnbindFd(mFdBinding);
  mTimerBinding = mFdBinding = 0;

  if (!mDisplay)
    return;

  if (mPlugWnd && mGLContext)
  {
    ActivateGLContext();
    OnViewDestroyed();
    DeactivateGLContext();
  }
  DestroyGLWindow();
}

bool IGraphicsLinux::CreateGLWindow(unsigned long parent, int w, int h)
{
  const int screen = DefaultScreen(mDisplay);
  const int fbAttribs[] = {GLX_X_RENDERABLE, kTrue, GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT, GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
                           GLX_DEPTH_SIZE, 24, GLX_STENCIL_SIZE, 8, GLX_DOUBLEBUFFER, kTrue, 0};

  // The window gets the visual of the FBConfig the context is created from,
  // preferring the parent's depth. The GLX 1.2 path (ticket 05 leaf) is the
  // fallback for servers without a usable FBConfig.
  XVisualInfo* pVisual = nullptr;
  GLXFBConfig fbConfig = nullptr;
  int count = 0;
  if (GLXFBConfig* pConfigs = glXChooseFBConfig(mDisplay, screen, fbAttribs, &count))
  {
    for (int i = 0; i < count; ++i)
    {
      XVisualInfo* pCandidate = glXGetVisualFromFBConfig(mDisplay, pConfigs[i]);
      if (!pCandidate)
        continue;
      if (!pVisual || (pCandidate->depth == DefaultDepth(mDisplay, screen) && pVisual->depth != pCandidate->depth))
      {
        if (pVisual)
          XFree(pVisual);
        pVisual = pCandidate;
        fbConfig = pConfigs[i];
      }
      else
        XFree(pCandidate);
    }
    XFree(pConfigs);
  }
  if (fbConfig)
    mGLContext = glXCreateNewContext(mDisplay, fbConfig, GLX_RGBA_TYPE, nullptr, kTrue);
  if (!mGLContext)
  {
    if (pVisual)
      XFree(pVisual);
    void* pContext = nullptr;
    pVisual = CreateGLContext(mDisplay, &pContext);
    mGLContext = static_cast<GLXContext>(pContext);
  }
  if (!pVisual || !mGLContext)
  {
    if (pVisual)
      XFree(pVisual);
    Log("no GLX visual with RGB8, depth 24 and double buffering");
    return false;
  }

  mColormap = XCreateColormap(mDisplay, RootWindow(mDisplay, screen), pVisual->visual, AllocNone);
  XSetWindowAttributes attrs = {};
  attrs.colormap = mColormap;
  attrs.border_pixel = 0;
  attrs.background_pixmap = kNone;
  attrs.event_mask =
    ExposureMask | StructureNotifyMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask | KeyReleaseMask | EnterWindowMask | LeaveWindowMask | FocusChangeMask;
  const int errors = sXErrorCount;
  mPlugWnd = XCreateWindow(mDisplay, parent, 0, 0, std::max(w, 1), std::max(h, 1), 0, pVisual->depth, InputOutput, pVisual->visual, CWColormap | CWBorderPixel | CWBackPixmap | CWEventMask, &attrs);
  XFree(pVisual);
  XMapWindow(mDisplay, mPlugWnd);
  XSync(mDisplay, kFalse);
  if (sXErrorCount != errors)
  {
    Log("could not create the editor window in parent 0x%lx", parent);
    return false;
  }
  return true;
}

void IGraphicsLinux::DestroyGLWindow()
{
  if (mGLContext)
  {
    if (glXGetCurrentContext() == mGLContext)
      glXMakeCurrent(mDisplay, kNone, nullptr);
    glXDestroyContext(mDisplay, mGLContext);
    mGLContext = nullptr;
  }
  mGLDepth = 0;
  FreeCursors();
  if (mPlugWnd)
    XDestroyWindow(mDisplay, mPlugWnd);
  if (mColormap)
    XFreeColormap(mDisplay, mColormap);
  mPlugWnd = mColormap = 0;
  XSync(mDisplay, kFalse); // report errors from the teardown while our handler is still installed
  RemoveDisplay(mDisplay);
  XCloseDisplay(mDisplay);
  mDisplay = nullptr;
}

float IGraphicsLinux::DetectScreenScale() const
{
  // Fallbacks for hosts that never call IPlugView::setContentScaleFactor:
  // Xft.dpi from the X resource database, then GDK_SCALE.
  float scale = 0.f;
  if (const char* pResources = XResourceManagerString(mDisplay))
  {
    XrmInitialize();
    if (XrmDatabase db = XrmGetStringDatabase(pResources))
    {
      char* pType = nullptr;
      XrmValue value = {};
      if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &pType, &value) && value.addr)
        scale = static_cast<float>(atof(value.addr) / 96.0);
      XrmDestroyDatabase(db);
    }
  }
  if (scale <= 0.f)
  {
    if (const char* pGdkScale = getenv("GDK_SCALE"))
      scale = static_cast<float>(atoi(pGdkScale));
  }
  return Clip(std::round(scale * 4.f) / 4.f, 1.f, 4.f);
}

void IGraphicsLinux::PlatformResize(bool parentHasResized)
{
  if (!mPlugWnd)
    return;
  XResizeWindow(mDisplay, mPlugWnd, static_cast<int>(WindowWidth() * GetScreenScale()), static_cast<int>(WindowHeight() * GetScreenScale()));
  XFlush(mDisplay);
  mNeedsFullRedraw = true;
}

void IGraphicsLinux::DrawResize()
{
  ActivateGLContext();
  IGRAPHICS_DRAW_CLASS::DrawResize();
  DeactivateGLContext();
}

void IGraphicsLinux::ActivateGLContext()
{
  if (!mGLContext)
    return;
  if (mGLDepth++ == 0)
  {
    mPrevContext = glXGetCurrentContext();
    mPrevDisplay = glXGetCurrentDisplay();
    mPrevDrawable = glXGetCurrentDrawable();
    mPrevReadable = glXGetCurrentReadDrawable();
  }
  glXMakeCurrent(mDisplay, mPlugWnd, mGLContext);
}

void IGraphicsLinux::DeactivateGLContext()
{
  if (!mGLContext || mGLDepth == 0 || --mGLDepth > 0)
    return;
  // Hand the thread back to whatever context the host (or another editor) had current.
  if (mPrevContext && mPrevContext != mGLContext)
    glXMakeContextCurrent(mPrevDisplay, mPrevDrawable, mPrevReadable, mPrevContext);
  else if (!mPrevContext)
    glXMakeCurrent(mDisplay, kNone, nullptr);
}

#pragma mark - Run loop callbacks

void IGraphicsLinux::ProcessX11Events()
{
  if (!mDisplay)
    return;
  ActivateGLContext(); // input handlers may load bitmaps or fonts
  while (mDisplay && XPending(mDisplay))
  {
    XEvent event;
    XNextEvent(mDisplay, &event);
    DispatchEvent(&event);
  }
  DeactivateGLContext();
}

void IGraphicsLinux::OnDisplayTimer()
{
  ProcessX11Events();
  if (!mPlugWnd)
    return;

  ActivateGLContext();
  IRECTList rects;
  const bool dirty = IsDirty(rects); // runs every tick: animation, drag-resize, display tick
  if (dirty || mNeedsFullRedraw)
  {
    if (mNeedsFullRedraw)
    {
      rects.Clear();
      rects.Add(GetBounds());
      mNeedsFullRedraw = false;
    }
    SetAllControlsClean();
    Draw(rects);
    glXSwapBuffers(mDisplay, mPlugWnd);
  }
  DeactivateGLContext();
  XFlush(mDisplay);
}

void IGraphicsLinux::DispatchEvent(void* pEvent)
{
  XEvent& event = *static_cast<XEvent*>(pEvent);
  if (event.xany.window != mPlugWnd)
    return;

  const float scale = GetTotalScale();
  switch (event.type)
  {
    case Expose:
      if (event.xexpose.count == 0)
        mNeedsFullRedraw = true;
      break;
    case ButtonPress:
    {
      const XButtonEvent& be = event.xbutton;
      const float x = be.x / scale;
      const float y = be.y / scale;
      const IMouseMod mod = ModFromState(be.state, be.button, 0);
      if (be.button == Button4 || be.button == Button5)
      {
        OnMouseWheel(x, y, mod, be.button == Button4 ? 1.f : -1.f);
        break;
      }
      if (be.button > Button5) // horizontal wheel: no IGraphics event
        break;
      if (!mHasFocus)
        XSetInputFocus(mDisplay, mPlugWnd, RevertToParent, be.time);

      // X11 has no double-click event. Like WM_LBUTTONDBLCLK, the second press
      // of a quick pair arrives as a double click instead of a mouse down.
      const bool doubleClick = be.button == static_cast<unsigned int>(mLastClickButton) && be.time - mLastClickTime < kDoubleClickMs && std::fabs(x - mLastClickX) < 4.f
                               && std::fabs(y - mLastClickY) < 4.f;
      mLastClickTime = doubleClick ? 0 : be.time;
      mLastClickButton = static_cast<int>(be.button);
      mLastClickX = mCursorX = x;
      mLastClickY = mCursorY = y;
      if (doubleClick)
        OnMouseDblClick(x, y, mod);
      else
        OnMouseDown({IMouseInfo{x, y, 0.f, 0.f, mod}});
      break;
    }
    case ButtonRelease:
    {
      const XButtonEvent& be = event.xbutton;
      if (be.button >= Button4)
        break;
      const float x = be.x / scale;
      const float y = be.y / scale;
      OnMouseUp({IMouseInfo{x, y, 0.f, 0.f, ModFromState(be.state, 0, be.button)}});
      break;
    }
    case MotionNotify:
    {
      // Only the newest of a run of queued motions matters.
      while (XEventsQueued(mDisplay, QueuedAlready) > 0)
      {
        XEvent next;
        XPeekEvent(mDisplay, &next);
        if (next.type != MotionNotify || next.xmotion.window != mPlugWnd)
          break;
        XNextEvent(mDisplay, &event);
      }
      const XMotionEvent& me = event.xmotion;
      const float x = me.x / scale;
      const float y = me.y / scale;
      const IMouseMod mod = ModFromState(me.state, 0, 0);
      if (me.state & (Button1Mask | Button3Mask))
      {
        const float dX = x - mCursorX;
        const float dY = y - mCursorY;
        mCursorX = x;
        mCursorY = y;
        if (dX || dY)
          OnMouseDrag({IMouseInfo{x, y, dX, dY, mod}});
        if (mCursorLock)
          MoveMouseCursor(mHiddenCursorX, mHiddenCursorY);
      }
      else
      {
        mCursorX = x;
        mCursorY = y;
        OnMouseOver(x, y, mod);
      }
      break;
    }
    case EnterNotify: OnSetCursor(); break;
    case LeaveNotify:
      if (event.xcrossing.mode == NotifyNormal && !(event.xcrossing.state & (Button1Mask | Button2Mask | Button3Mask)))
        OnMouseOut();
      break;
    case FocusIn: mHasFocus = true; break;
    case FocusOut: mHasFocus = false; break;
    case KeyPress:
    case KeyRelease:
    {
      XKeyEvent& ke = event.xkey;
      // Auto-repeat arrives as release+press with one timestamp. Windows sends
      // only repeated key downs, so the release is dropped.
      if (event.type == KeyRelease && XEventsQueued(mDisplay, QueuedAfterReading) > 0)
      {
        XEvent next;
        XPeekEvent(mDisplay, &next);
        if (next.type == KeyPress && next.xkey.keycode == ke.keycode && next.xkey.time == ke.time)
          break;
      }
      char latin1[16] = {};
      KeySym keysym = 0;
      XLookupString(&ke, latin1, sizeof(latin1) - 1, &keysym, nullptr);
      char utf8[5] = {};
      KeysymToUTF8(keysym, utf8);
      const IKeyPress keyPress(utf8, KeysymToVK(keysym), ke.state & ShiftMask, ke.state & ControlMask, ke.state & Mod1Mask);
      if (event.type == KeyPress)
        OnKeyDown(ke.x / scale, ke.y / scale, keyPress);
      else
        OnKeyUp(ke.x / scale, ke.y / scale, keyPress);
      break;
    }
    default: break;
  }
}

#pragma mark - Cursor

ECursor IGraphicsLinux::SetMouseCursor(ECursor cursorType)
{
  static const unsigned int kShapes[] = {XC_left_ptr, XC_xterm, XC_watch, XC_crosshair, XC_sb_up_arrow, XC_bottom_right_corner, XC_bottom_left_corner, XC_sb_h_double_arrow,
                                         XC_sb_v_double_arrow, XC_fleur, XC_X_cursor, XC_hand2, XC_watch, XC_question_arrow};
  static_assert(sizeof(kShapes) / sizeof(kShapes[0]) == static_cast<int>(ECursor::HELP) + 1, "one X cursor per ECursor");
  static_assert(sizeof(kShapes) / sizeof(kShapes[0]) <= sizeof(mCursors) / sizeof(mCursors[0]), "cursor cache too small");

  const int idx = static_cast<int>(cursorType);
  if (mPlugWnd && !mCursorHidden && idx >= 0 && idx <= static_cast<int>(ECursor::HELP))
  {
    if (!mCursors[idx])
      mCursors[idx] = XCreateFontCursor(mDisplay, kShapes[idx]);
    XDefineCursor(mDisplay, mPlugWnd, mCursors[idx]);
  }
  return IGraphics::SetMouseCursor(cursorType);
}

void IGraphicsLinux::HideMouseCursor(bool hide, bool lock)
{
  if (!mPlugWnd || mCursorHidden == hide)
    return;

  if (hide)
  {
    if (!mBlankCursor)
    {
      const char empty[1] = {0};
      Pixmap pixmap = XCreateBitmapFromData(mDisplay, mPlugWnd, empty, 1, 1);
      XColor black = {};
      mBlankCursor = XCreatePixmapCursor(mDisplay, pixmap, pixmap, &black, &black, 0, 0);
      XFreePixmap(mDisplay, pixmap);
    }
    mHiddenCursorX = mCursorX;
    mHiddenCursorY = mCursorY;
    XDefineCursor(mDisplay, mPlugWnd, mBlankCursor);
    mCursorHidden = true;
    mCursorLock = lock;
  }
  else
  {
    if (mCursorLock)
      MoveMouseCursor(mHiddenCursorX, mHiddenCursorY);
    mCursorHidden = false;
    mCursorLock = false;
    OnSetCursor();
  }
}

void IGraphicsLinux::MoveMouseCursor(float x, float y)
{
  if (!mPlugWnd)
    return;
  const float scale = GetTotalScale();
  XWarpPointer(mDisplay, kNone, mPlugWnd, 0, 0, 0, 0, static_cast<int>(std::round(x * scale)), static_cast<int>(std::round(y * scale)));
  mHiddenCursorX = mCursorX = x;
  mHiddenCursorY = mCursorY = y;
}

void IGraphicsLinux::GetMouseLocation(float& x, float& y) const
{
  x = y = 0.f;
  if (!mPlugWnd)
    return;
  Window root, child;
  int rootX, rootY, winX, winY;
  unsigned int mask;
  if (XQueryPointer(mDisplay, mPlugWnd, &root, &child, &rootX, &rootY, &winX, &winY, &mask))
  {
    x = winX / GetTotalScale();
    y = winY / GetTotalScale();
  }
}

void IGraphicsLinux::FreeCursors()
{
  for (auto& cursor : mCursors)
  {
    if (cursor)
      XFreeCursor(mDisplay, cursor);
    cursor = 0;
  }
  if (mBlankCursor)
    XFreeCursor(mDisplay, mBlankCursor);
  mBlankCursor = 0;
  mCursorHidden = mCursorLock = false;
}

#pragma mark - Text entry and menus (drawn by IGraphics)

void IGraphicsLinux::CreatePlatformTextEntry(int paramIdx, const IText& text, const IRECT& bounds, int length, const char* str)
{
  AttachTextEntryControl();
  GetTextEntryControl()->CreateTextEntry(paramIdx, text, bounds, length, str);
}

IPopupMenu* IGraphicsLinux::CreatePlatformPopupMenu(IPopupMenu& menu, const IRECT bounds, bool& isAsync)
{
  if (!GetPopupMenuControl())
    AttachPopupMenuControl();
  GetPopupMenuControl()->CreatePopupMenu(menu, bounds);
  isAsync = true;
  return nullptr;
}

void IGraphicsLinux::ForceEndUserEdit()
{
  if (ITextEntryControl* pTextEntry = GetTextEntryControl(); pTextEntry && pTextEntry->EditInProgress())
    pTextEntry->DismissEdit();
}

#pragma mark - Dialogs, clipboard and URLs (ticket 09)

EMsgBoxResult IGraphicsLinux::ShowMessageBox(const char* str, const char* caption, EMsgBoxType type, IMsgBoxCompletionHandlerFunc completionHandler)
{
  Log("message box not implemented: %s", str ? str : "");
  if (completionHandler)
    completionHandler(kNoResult);
  return kNoResult;
}

bool IGraphicsLinux::RevealPathInExplorerOrFinder(WDL_String& path, bool select)
{
  Log("reveal path not implemented: %s", path.Get());
  return false;
}

void IGraphicsLinux::PromptForFile(WDL_String& fileName, WDL_String& path, EFileAction action, const char* ext, IFileDialogCompletionHandlerFunc completionHandler)
{
  Log("file dialog not implemented (%s)", ext ? ext : "");
  fileName.Set("");
  path.Set("");
  if (completionHandler)
    completionHandler(fileName, path);
}

void IGraphicsLinux::PromptForDirectory(WDL_String& dir, IFileDialogCompletionHandlerFunc completionHandler)
{
  Log("directory dialog not implemented");
  dir.Set("");
  if (completionHandler)
    completionHandler(dir, dir);
}

bool IGraphicsLinux::PromptForColor(IColor& color, const char* str, IColorPickerHandlerFunc func)
{
  Log("color picker not implemented");
  return false;
}

bool IGraphicsLinux::OpenURL(const char* url, const char* msgWindowTitle, const char* confirmMsg, const char* errMsgOnFailure)
{
  Log("open URL not implemented: %s", url ? url : "");
  return false;
}

bool IGraphicsLinux::GetTextFromClipboard(WDL_String& str)
{
  Log("clipboard not implemented");
  return false;
}

bool IGraphicsLinux::SetTextInClipboard(const char* str)
{
  Log("clipboard not implemented");
  return false;
}

#pragma mark - Fonts

PlatformFontPtr IGraphicsLinux::LoadPlatformFont(const char*, const char* fileNameOrResID) { return LoadPlatformFontFromFile(fileNameOrResID); }

PlatformFontPtr IGraphicsLinux::LoadPlatformFont(const char*, void* pData, int dataSize) { return LoadPlatformFontFromMemory(pData, dataSize); }

PlatformFontPtr IGraphicsLinux::LoadPlatformFont(const char*, const char* fontName, ETextStyle style) { return LoadPlatformFontFromName(fontName, style); }
