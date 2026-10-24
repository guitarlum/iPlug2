/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

// X11 before IGraphicsLinuxGL.h, which undefs Bool after the headers are parsed.
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>

// Undef X11 Bool/None/Status before any IGraphics or pugl header sees them.
#include "IGraphicsLinuxGL.h"
#include "IGraphicsLinux.h"
#include "Linux/IPlugLinuxRunLoop.h"

#define PUGL_NO_INCLUDE_GL_H
#include <pugl/gl.h>
#include <pugl/pugl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace iplug;
using namespace igraphics;

namespace
{

int OnXError(Display* display, XErrorEvent* error)
{
  char text[256] = {};
  if (display && error)
    XGetErrorText(display, error->error_code, text, sizeof text);
  std::fprintf(stderr, "IPlugLinux X error: %s request %u minor %u\n", text,
               error ? error->request_code : 0, error ? error->minor_code : 0);
  return 0;
}

void InstallXErrorHandler()
{
  static bool installed = false;
  if (installed)
    return;
  XSetErrorHandler(OnXError);
  installed = true;
}

void LogDeferred(const char* what)
{
  std::fprintf(stderr, "IPlugLinux: %s is not available until ticket 09\n", what);
}

int VkFromPuglKey(uint32_t key)
{
  if (key >= 'a' && key <= 'z')
    return static_cast<int>(key - 'a' + kVK_A);
  if (key >= 'A' && key <= 'Z')
    return static_cast<int>(key);
  if (key >= '0' && key <= '9')
    return static_cast<int>(key);

  switch (key)
  {
    case PUGL_KEY_BACKSPACE: return kVK_BACK;
    case PUGL_KEY_TAB: return kVK_TAB;
    case PUGL_KEY_ENTER: return kVK_RETURN;
    case PUGL_KEY_ESCAPE: return kVK_ESCAPE;
    case PUGL_KEY_DELETE: return kVK_DELETE;
    case PUGL_KEY_SPACE: return kVK_SPACE;
    case PUGL_KEY_PAGE_UP: return kVK_PRIOR;
    case PUGL_KEY_PAGE_DOWN: return kVK_NEXT;
    case PUGL_KEY_END: return kVK_END;
    case PUGL_KEY_HOME: return kVK_HOME;
    case PUGL_KEY_LEFT: return kVK_LEFT;
    case PUGL_KEY_UP: return kVK_UP;
    case PUGL_KEY_RIGHT: return kVK_RIGHT;
    case PUGL_KEY_DOWN: return kVK_DOWN;
    case PUGL_KEY_PRINT_SCREEN: return kVK_SNAPSHOT;
    case PUGL_KEY_INSERT: return kVK_INSERT;
    case PUGL_KEY_PAUSE: return kVK_PAUSE;
    case PUGL_KEY_NUM_LOCK: return kVK_NUMLOCK;
    case PUGL_KEY_SCROLL_LOCK: return kVK_SCROLL;
    case PUGL_KEY_CAPS_LOCK: return kVK_CAPITAL;
    case PUGL_KEY_SHIFT_L:
    case PUGL_KEY_SHIFT_R: return kVK_SHIFT;
    case PUGL_KEY_CTRL_L:
    case PUGL_KEY_CTRL_R: return kVK_CONTROL;
    case PUGL_KEY_ALT_L:
    case PUGL_KEY_ALT_R: return kVK_MENU;
    case PUGL_KEY_PAD_0: return kVK_NUMPAD0;
    case PUGL_KEY_PAD_1: return kVK_NUMPAD1;
    case PUGL_KEY_PAD_2: return kVK_NUMPAD2;
    case PUGL_KEY_PAD_3: return kVK_NUMPAD3;
    case PUGL_KEY_PAD_4: return kVK_NUMPAD4;
    case PUGL_KEY_PAD_5: return kVK_NUMPAD5;
    case PUGL_KEY_PAD_6: return kVK_NUMPAD6;
    case PUGL_KEY_PAD_7: return kVK_NUMPAD7;
    case PUGL_KEY_PAD_8: return kVK_NUMPAD8;
    case PUGL_KEY_PAD_9: return kVK_NUMPAD9;
    case PUGL_KEY_PAD_ENTER: return kVK_RETURN;
    case PUGL_KEY_PAD_MULTIPLY: return kVK_MULTIPLY;
    case PUGL_KEY_PAD_ADD: return kVK_ADD;
    case PUGL_KEY_PAD_SEPARATOR: return kVK_SEPARATOR;
    case PUGL_KEY_PAD_SUBTRACT: return kVK_SUBTRACT;
    case PUGL_KEY_PAD_DECIMAL: return kVK_DECIMAL;
    case PUGL_KEY_PAD_DIVIDE: return kVK_DIVIDE;
    case PUGL_KEY_F1: return kVK_F1;
    case PUGL_KEY_F2: return kVK_F2;
    case PUGL_KEY_F3: return kVK_F3;
    case PUGL_KEY_F4: return kVK_F4;
    case PUGL_KEY_F5: return kVK_F5;
    case PUGL_KEY_F6: return kVK_F6;
    case PUGL_KEY_F7: return kVK_F7;
    case PUGL_KEY_F8: return kVK_F8;
    case PUGL_KEY_F9: return kVK_F9;
    case PUGL_KEY_F10: return kVK_F10;
    case PUGL_KEY_F11: return kVK_F11;
    case PUGL_KEY_F12: return kVK_F12;
    default: break;
  }
  return kVK_NONE;
}

void EncodeUtf8(uint32_t codepoint, char out[5])
{
  out[0] = 0;
  if (codepoint == 0 || codepoint >= 0xE000)
    return;
  if (codepoint < 0x80)
  {
    out[0] = static_cast<char>(codepoint);
    out[1] = 0;
    return;
  }
  if (codepoint < 0x800)
  {
    out[0] = static_cast<char>(0xC0 | (codepoint >> 6));
    out[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
    out[2] = 0;
    return;
  }
  if (codepoint < 0x10000)
  {
    out[0] = static_cast<char>(0xE0 | (codepoint >> 12));
    out[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
    out[3] = 0;
  }
}

PuglCursor ToPuglCursor(ECursor cursor)
{
  switch (cursor)
  {
    case ECursor::IBEAM: return PUGL_CURSOR_CARET;
    case ECursor::CROSS: return PUGL_CURSOR_CROSSHAIR;
    case ECursor::HAND: return PUGL_CURSOR_HAND;
    case ECursor::INO: return PUGL_CURSOR_NO;
    case ECursor::SIZEWE: return PUGL_CURSOR_LEFT_RIGHT;
    case ECursor::SIZENS: return PUGL_CURSOR_UP_DOWN;
    case ECursor::SIZENWSE: return PUGL_CURSOR_UP_LEFT_DOWN_RIGHT;
    case ECursor::SIZENESW: return PUGL_CURSOR_UP_RIGHT_DOWN_LEFT;
    case ECursor::SIZEALL: return PUGL_CURSOR_ALL_SCROLL;
    default: return PUGL_CURSOR_ARROW;
  }
}

float ScaleFromXftOrGdk(Display* display)
{
  if (display)
  {
    XrmInitialize();
    const char* resources = XResourceManagerString(display);
    if (resources)
    {
      XrmDatabase database = XrmGetStringDatabase(resources);
      if (database)
      {
        XrmValue value{};
        char* type = nullptr;
        float scale = 0.f;
        if (XrmGetResource(database, "Xft.dpi", "Xft.Dpi", &type, &value) && value.addr)
        {
          const double dpi = std::strtod(value.addr, nullptr);
          if (dpi > 0.0)
            scale = static_cast<float>(dpi / 96.0);
        }
        XrmDestroyDatabase(database);
        if (scale > 0.f)
          return scale;
      }
    }
  }

  if (const char* gdk = std::getenv("GDK_SCALE"))
  {
    const int factor = std::atoi(gdk);
    if (factor > 0)
      return static_cast<float>(factor);
  }
  return 1.f;
}

float gNextHostScale = 0.f;
bool gHaveNextHostScale = false;

} // namespace

struct IGraphicsLinux::Platform
{
  IGraphicsLinux* self = nullptr;
  PuglWorld* world = nullptr;
  PuglView* view = nullptr;
  void* native = nullptr;
  bool open = false;
  bool inGlEvent = false;
  bool inUpdate = false;
  bool glLoaded = false;
  bool needsPaint = false;
  bool cursorHidden = false;
  bool cursorLocked = false;
  float mouseX = 0.f;
  float mouseY = 0.f;
  float lastClickX = 0.f;
  float lastClickY = 0.f;
  double lastClickTime = -1.0;
  int lastClickButton = -1;
  int buttons = 0;
  float openScale = 1.f;
  Cursor blank = 0;

  Display* GetDisplay() const
  {
    if (!world)
      return nullptr;
    return static_cast<Display*>(puglGetNativeWorld(world));
  }

  Window NativeWindow() const { return view ? static_cast<Window>(puglGetNativeView(view)) : 0; }

  void Logical(double px, double py, float& x, float& y)
  {
    const float scale = self->GetTotalScale();
    const float divisor = scale > 0.f ? scale : 1.f;
    x = mouseX = static_cast<float>(px) / divisor;
    y = mouseY = static_cast<float>(py) / divisor;
  }

  IMouseMod MouseMod(PuglMods state, bool left, bool right) const
  {
    return IMouseMod(left, right, (state & PUGL_MOD_SHIFT) != 0, (state & PUGL_MOD_CTRL) != 0, (state & PUGL_MOD_ALT) != 0);
  }

  void Paint()
  {
    if (!open || !view)
      return;
    // pugl has already made the GL context current for PUGL_EXPOSE.
    // Enter again so this instance is current even if a sibling drew last.
    puglEnterContext(view);
    inGlEvent = true;
    IRECTList rects;
    rects.Add(IRECT(0.f, 0.f, static_cast<float>(self->Width()), static_cast<float>(self->Height())));
    self->Draw(rects);
    needsPaint = false;
    inGlEvent = false;
  }

  void OnRealize()
  {
    inGlEvent = true;
    puglEnterContext(view);
    if (!glLoaded)
      glLoaded = LoadGL();
    self->OnViewInitialized(nullptr);
    self->SetScreenScale(openScale);
    self->GetDelegate()->LayoutUI(self);
    self->SetAllControlsDirty();
    needsPaint = true;
    inGlEvent = false;
  }

  void OnKey(const PuglKeyEvent& key, bool down)
  {
    const bool ctrl = (key.state & PUGL_MOD_CTRL) != 0;
    const bool alt = (key.state & PUGL_MOD_ALT) != 0;
    const bool printable = key.key >= 32 && key.key < 0xE000 && key.key != PUGL_KEY_DELETE;
    if (down && printable && !ctrl && !alt)
      return;

    char utf8[5] = {};
    if (printable)
      EncodeUtf8(key.key, utf8);
    const IKeyPress press(utf8, VkFromPuglKey(key.key), (key.state & PUGL_MOD_SHIFT) != 0, ctrl, alt);
    float x = 0.f;
    float y = 0.f;
    Logical(key.x, key.y, x, y);
    if (down)
      self->OnKeyDown(x, y, press);
    else
      self->OnKeyUp(x, y, press);
  }

  void OnText(const PuglTextEvent& text)
  {
    char utf8[5] = {};
    for (int i = 0; i < 4 && text.string[i]; ++i)
      utf8[i] = text.string[i];
    const IKeyPress press(utf8, VkFromPuglKey(text.character), (text.state & PUGL_MOD_SHIFT) != 0,
                          (text.state & PUGL_MOD_CTRL) != 0, (text.state & PUGL_MOD_ALT) != 0);
    float x = 0.f;
    float y = 0.f;
    Logical(text.x, text.y, x, y);
    self->OnKeyDown(x, y, press);
  }

  PuglStatus Handle(const PuglEvent* event)
  {
    if (!self || !event)
      return PUGL_SUCCESS;
    switch (event->type)
    {
      case PUGL_REALIZE: OnRealize(); break;
      case PUGL_UNREALIZE:
        inGlEvent = true;
        if (glLoaded)
          self->OnViewDestroyed();
        glLoaded = false;
        inGlEvent = false;
        break;
      case PUGL_EXPOSE: Paint(); break;
      case PUGL_BUTTON_PRESS:
      case PUGL_BUTTON_RELEASE:
      {
        const PuglButtonEvent& button = event->button;
        float x = 0.f;
        float y = 0.f;
        Logical(button.x, button.y, x, y);
        const bool down = event->type == PUGL_BUTTON_PRESS;
        const int bit = button.button < 3 ? (1 << button.button) : 0;
        if (down)
          buttons |= bit;
        else
          buttons &= ~bit;
        const bool left = button.button == 0;
        const bool right = button.button == 1;
        IMouseInfo info;
        info.x = x;
        info.y = y;
        info.ms = MouseMod(button.state, down ? button.button == 0 : left, down ? button.button == 1 : right);
        std::vector<IMouseInfo> points{info};
        if (down)
        {
          const double now = world ? puglGetTime(world) : 0.0;
          const bool repeat = button.button == lastClickButton && (now - lastClickTime) < 0.4 &&
                              std::fabs(x - lastClickX) < 5.f && std::fabs(y - lastClickY) < 5.f;
          lastClickTime = now;
          lastClickX = x;
          lastClickY = y;
          lastClickButton = static_cast<int>(button.button);
          if (repeat)
            self->OnMouseDblClick(x, y, info.ms);
          else
            self->OnMouseDown(points);
        }
        else
        {
          self->OnMouseUp(points);
        }
        break;
      }
      case PUGL_MOTION:
      {
        const PuglMotionEvent& motion = event->motion;
        const float previousX = mouseX;
        const float previousY = mouseY;
        float x = 0.f;
        float y = 0.f;
        Logical(motion.x, motion.y, x, y);
        IMouseInfo info;
        info.x = x;
        info.y = y;
        info.dX = x - previousX;
        info.dY = y - previousY;
        info.ms = MouseMod(motion.state, (buttons & 1) != 0, (buttons & 2) != 0);
        if (buttons != 0)
        {
          std::vector<IMouseInfo> points{info};
          self->OnMouseDrag(points);
        }
        else
        {
          self->OnMouseOver(x, y, info.ms);
        }
        break;
      }
      case PUGL_SCROLL:
      {
        const PuglScrollEvent& scroll = event->scroll;
        float x = 0.f;
        float y = 0.f;
        Logical(scroll.x, scroll.y, x, y);
        float delta = static_cast<float>(scroll.dy);
        if (std::fabs(delta) < 0.01f)
        {
          if (scroll.direction == PUGL_SCROLL_UP)
            delta = 1.f;
          else if (scroll.direction == PUGL_SCROLL_DOWN)
            delta = -1.f;
        }
        self->OnMouseWheel(x, y, MouseMod(scroll.state, (buttons & 1) != 0, (buttons & 2) != 0), delta);
        break;
      }
      case PUGL_KEY_PRESS: OnKey(event->key, true); break;
      case PUGL_KEY_RELEASE: OnKey(event->key, false); break;
      case PUGL_TEXT: OnText(event->text); break;
      case PUGL_POINTER_OUT: self->OnMouseOut(); break;
      case PUGL_POINTER_IN: self->OnSetCursor(); break;
      default: break;
    }
    return PUGL_SUCCESS;
  }

  static PuglStatus OnEvent(PuglView* view, const PuglEvent* event)
  {
    auto* platform = static_cast<Platform*>(puglGetHandle(view));
    if (!platform || !platform->open)
    {
      if (platform && event && (event->type == PUGL_REALIZE || event->type == PUGL_UNREALIZE))
        return platform->Handle(event);
      return PUGL_SUCCESS;
    }
    return platform->Handle(event);
  }
};

IGraphicsLinux::IGraphicsLinux(IGEditorDelegate& dlg, int w, int h, int fps, float scale)
: IGRAPHICS_DRAW_CLASS(dlg, w, h, fps, scale)
{
}

IGraphicsLinux::~IGraphicsLinux()
{
  CloseWindow();
  delete mPlatform;
  mPlatform = nullptr;
}

void IGraphicsLinux::SetHostScaleForOpen(float scale)
{
  if (scale > 0.f)
  {
    gNextHostScale = scale;
    gHaveNextHostScale = true;
  }
  else
  {
    gNextHostScale = 0.f;
    gHaveNextHostScale = false;
  }
}

void* IGraphicsLinux::OpenWindow(void* pParent)
{
  IPlugLinuxAssertUiThread();
  CloseWindow();

  const auto parent = static_cast<PuglNativeView>(reinterpret_cast<uintptr_t>(pParent));
  if (!parent)
    return nullptr;

  InstallXErrorHandler();
  if (!mPlatform)
    mPlatform = new Platform;
  mPlatform->self = this;

  const bool haveHost = gHaveNextHostScale;
  const float hostScale = gNextHostScale;
  gHaveNextHostScale = false;
  gNextHostScale = 0.f;

  // PUGL_MODULE and no PUGL_WORLD_THREADS: pugl must not call XInitThreads.
  mPlatform->world = puglNewWorld(PUGL_MODULE, 0);
  if (!mPlatform->world)
    return nullptr;
  puglSetWorldString(mPlatform->world, PUGL_CLASS_NAME, "VoLum");

  Display* display = mPlatform->GetDisplay();
  mPlatform->openScale = haveHost && hostScale > 0.f ? hostScale : ScaleFromXftOrGdk(display);

  mPlatform->view = puglNewView(mPlatform->world);
  if (!mPlatform->view)
  {
    CloseWindow();
    return nullptr;
  }
  puglSetHandle(mPlatform->view, mPlatform);
  puglSetBackend(mPlatform->view, puglGlBackend());
  puglSetEventFunc(mPlatform->view, &Platform::OnEvent);
  puglSetParent(mPlatform->view, parent);
  puglSetViewHint(mPlatform->view, PUGL_CONTEXT_VERSION_MAJOR, 2);
  puglSetViewHint(mPlatform->view, PUGL_CONTEXT_VERSION_MINOR, 0);
  puglSetViewHint(mPlatform->view, PUGL_CONTEXT_PROFILE, PUGL_OPENGL_COMPATIBILITY_PROFILE);
  puglSetViewHint(mPlatform->view, PUGL_RED_BITS, 8);
  puglSetViewHint(mPlatform->view, PUGL_GREEN_BITS, 8);
  puglSetViewHint(mPlatform->view, PUGL_BLUE_BITS, 8);
  puglSetViewHint(mPlatform->view, PUGL_ALPHA_BITS, 8);
  puglSetViewHint(mPlatform->view, PUGL_DEPTH_BITS, 24);
  puglSetViewHint(mPlatform->view, PUGL_STENCIL_BITS, 8);
  puglSetViewHint(mPlatform->view, PUGL_DOUBLE_BUFFER, 1);
  puglSetViewHint(mPlatform->view, PUGL_SWAP_INTERVAL, 0);
  puglSetViewHint(mPlatform->view, PUGL_RESIZABLE, 0);

  const unsigned pixelW = static_cast<unsigned>(std::max(1, static_cast<int>(std::lround(WindowWidth() * mPlatform->openScale))));
  const unsigned pixelH = static_cast<unsigned>(std::max(1, static_cast<int>(std::lround(WindowHeight() * mPlatform->openScale))));
  puglSetSizeHint(mPlatform->view, PUGL_DEFAULT_SIZE, pixelW, pixelH);

  mPlatform->open = true;
  const PuglStatus realized = puglRealize(mPlatform->view);
  if (realized != PUGL_SUCCESS)
  {
    std::fprintf(stderr, "IPlugLinux: puglRealize failed: %s\n", puglStrerror(realized));
    CloseWindow();
    return nullptr;
  }

  puglShow(mPlatform->view, true);
  mPlatform->native = reinterpret_cast<void*>(static_cast<uintptr_t>(puglGetNativeView(mPlatform->view)));
  puglObscureView(mPlatform->view);
  DispatchPlatformEvents();
  return mPlatform->native;
}

void IGraphicsLinux::PrepareToClose()
{
  if (!mPlatform)
    return;
  mPlatform->open = false;
  mPlatform->native = nullptr;
}

void IGraphicsLinux::CloseWindow()
{
  IPlugLinuxAssertUiThread();
  if (!mPlatform)
    return;

  PrepareToClose();
  Display* display = mPlatform->GetDisplay();
  if (display && mPlatform->cursorLocked)
    XUngrabPointer(display, 0);
  mPlatform->cursorLocked = false;
  mPlatform->cursorHidden = false;
  mPlatform->buttons = 0;
  if (display && mPlatform->blank)
    XFreeCursor(display, mPlatform->blank);
  mPlatform->blank = 0;

  if (mPlatform->view)
  {
    puglFreeView(mPlatform->view);
    mPlatform->view = nullptr;
  }
  if (mPlatform->world)
  {
    puglFreeWorld(mPlatform->world);
    mPlatform->world = nullptr;
  }
  mPlatform->glLoaded = false;
}

bool IGraphicsLinux::WindowIsOpen() { return GetWindow() != nullptr; }

void* IGraphicsLinux::GetWindow() { return mPlatform ? mPlatform->native : nullptr; }

int IGraphicsLinux::XConnectionFd() const
{
  if (!mPlatform || !mPlatform->open)
    return -1;
  Display* display = mPlatform->GetDisplay();
  if (!display)
    return -1;
  return ConnectionNumber(display);
}

void IGraphicsLinux::DispatchPlatformEvents()
{
  IPlugLinuxAssertUiThread();
  if (!mPlatform || !mPlatform->open || !mPlatform->world || mPlatform->inUpdate)
    return;
  mPlatform->inUpdate = true;
  puglUpdate(mPlatform->world, 0.0);
  mPlatform->inUpdate = false;
}

void IGraphicsLinux::OnDisplayTimer()
{
  IPlugLinuxAssertUiThread();
  if (!mPlatform || !mPlatform->open || !mPlatform->view)
    return;
  IRECTList rects;
  if (IsDirty(rects) || mPlatform->needsPaint)
    puglObscureView(mPlatform->view);
  DispatchPlatformEvents();
}

void IGraphicsLinux::PlatformResize(bool)
{
  if (!mPlatform || !mPlatform->open || !mPlatform->view)
    return;
  const unsigned pixelW = static_cast<unsigned>(std::max(1, static_cast<int>(std::lround(WindowWidth() * GetScreenScale()))));
  const unsigned pixelH = static_cast<unsigned>(std::max(1, static_cast<int>(std::lround(WindowHeight() * GetScreenScale()))));
  puglSetSizeHint(mPlatform->view, PUGL_CURRENT_SIZE, pixelW, pixelH);
}

void IGraphicsLinux::DrawResize()
{
  const bool enter = mPlatform && mPlatform->view && !mPlatform->inGlEvent;
  if (enter)
    puglEnterContext(mPlatform->view);
  IGRAPHICS_DRAW_CLASS::DrawResize();
  if (enter)
    puglLeaveContext(mPlatform->view);
}

void IGraphicsLinux::GetMouseLocation(float& x, float& y) const
{
  x = mPlatform ? mPlatform->mouseX : 0.f;
  y = mPlatform ? mPlatform->mouseY : 0.f;
}

void IGraphicsLinux::MoveMouseCursor(float x, float y)
{
  if (!mPlatform || !mPlatform->open)
    return;
  Display* display = mPlatform->GetDisplay();
  Window window = mPlatform->NativeWindow();
  if (!display || !window)
    return;
  const float scale = GetTotalScale() > 0.f ? GetTotalScale() : 1.f;
  XWarpPointer(display, 0, window, 0, 0, 0, 0, static_cast<int>(x * scale), static_cast<int>(y * scale));
}

void IGraphicsLinux::HideMouseCursor(bool hide, bool lock)
{
  if (!mPlatform || !mPlatform->view || !mPlatform->open)
    return;
  mPlatform->cursorHidden = hide;
  Display* display = mPlatform->GetDisplay();
  Window window = mPlatform->NativeWindow();
  if (hide)
  {
    if (display && window && !mPlatform->blank)
    {
      const char bits[1] = {0};
      Pixmap pixmap = XCreateBitmapFromData(display, window, bits, 1, 1);
      XColor color{};
      mPlatform->blank = XCreatePixmapCursor(display, pixmap, pixmap, &color, &color, 0, 0);
      XFreePixmap(display, pixmap);
    }
    if (display && window && mPlatform->blank)
      XDefineCursor(display, window, mPlatform->blank);
  }
  else
  {
    puglSetCursor(mPlatform->view, ToPuglCursor(mCursorType));
  }

  if (lock && hide && display && window && !mPlatform->cursorLocked)
  {
    if (XGrabPointer(display, window, 0, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, window, mPlatform->blank, 0) == GrabSuccess)
      mPlatform->cursorLocked = true;
  }
  else if ((!lock || !hide) && mPlatform->cursorLocked && display)
  {
    XUngrabPointer(display, 0);
    mPlatform->cursorLocked = false;
  }
}

ECursor IGraphicsLinux::SetMouseCursor(ECursor cursorType)
{
  const ECursor previous = IGraphics::SetMouseCursor(cursorType);
  if (mPlatform && mPlatform->view && mPlatform->open && !mPlatform->cursorHidden)
    puglSetCursor(mPlatform->view, ToPuglCursor(cursorType));
  return previous;
}

EMsgBoxResult IGraphicsLinux::ShowMessageBox(const char*, const char*, EMsgBoxType, IMsgBoxCompletionHandlerFunc completionHandler)
{
  LogDeferred("message box");
  if (completionHandler)
    completionHandler(kNoResult);
  return kNoResult;
}

void IGraphicsLinux::ForceEndUserEdit() {}

const char* IGraphicsLinux::GetPlatformAPIStr() { return "Linux"; }

void IGraphicsLinux::UpdateTooltips() {}

bool IGraphicsLinux::RevealPathInExplorerOrFinder(WDL_String&, bool)
{
  LogDeferred("reveal path");
  return false;
}

void IGraphicsLinux::PromptForFile(WDL_String& fileName, WDL_String& path, EFileAction, const char*, IFileDialogCompletionHandlerFunc completionHandler)
{
  LogDeferred("file dialog");
  if (completionHandler)
    completionHandler(fileName, path);
}

void IGraphicsLinux::PromptForDirectory(WDL_String& dir, IFileDialogCompletionHandlerFunc completionHandler)
{
  LogDeferred("directory dialog");
  if (completionHandler)
    completionHandler(dir, dir);
}

bool IGraphicsLinux::PromptForColor(IColor&, const char*, IColorPickerHandlerFunc)
{
  LogDeferred("color dialog");
  return false;
}

bool IGraphicsLinux::OpenURL(const char*, const char*, const char*, const char*) { return false; }

int IGraphicsLinux::GetUserOSVersion() { return 0; }

bool IGraphicsLinux::GetTextFromClipboard(WDL_String&)
{
  LogDeferred("clipboard");
  return false;
}

IPopupMenu* IGraphicsLinux::CreatePlatformPopupMenu(IPopupMenu&, const IRECT, bool& isAsync)
{
  isAsync = false;
  LogDeferred("platform menu");
  return nullptr;
}

void IGraphicsLinux::CreatePlatformTextEntry(int, const IText&, const IRECT&, int, const char*)
{
  // VoLum attaches ITextEntryControl, which IGraphics draws. This is the
  // fallback when that control is absent: no native X entry widget.
  LogDeferred("native text entry");
}

PlatformFontPtr IGraphicsLinux::LoadPlatformFont(const char*, const char* fileNameOrResID)
{
  return LoadPlatformFontFromFile(fileNameOrResID);
}

PlatformFontPtr IGraphicsLinux::LoadPlatformFont(const char*, void* pData, int dataSize)
{
  return LoadPlatformFontFromMemory(pData, dataSize);
}

PlatformFontPtr IGraphicsLinux::LoadPlatformFont(const char*, const char* fontName, ETextStyle style)
{
  return LoadPlatformFontFromName(fontName, style);
}

void IGraphicsLinux::CachePlatformFont(const char*, const PlatformFontPtr&) {}
