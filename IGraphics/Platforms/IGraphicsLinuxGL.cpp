/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#if !defined(IGRAPHICS_GL2) && !defined(IGRAPHICS_GL3)
  #error IGraphicsLinuxGL.cpp requires IGRAPHICS_GL2 or IGRAPHICS_GL3
#endif

// clang-format off
// Order is load-bearing. glad sets __gl_h_ before GL/glx.h so glx.h does not
// include GL/gl.h. This header undefs X11 Bool after glx.h has been parsed.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <glad/glad.h>
#include <GL/glx.h>
#include "IGraphicsLinuxGL.h"
// clang-format on

namespace iplug
{
namespace igraphics
{

// Classic GLX 1.2 API — compatible with Mesa software renderers (llvmpipe,
// softpipe) on Xwayland, where the GLX 1.3 FBConfig path fails. Same
// attribute list as IGraphicsLinux::CreateGLContext in iPlug2#1336.
// 0 replaces X11 None, which this header undefs.

XVisualInfo* CreateGLContext(Display* display, void** glxContext)
{
  if (!display || !glxContext)
    return nullptr;
  *glxContext = nullptr;

  int screen = DefaultScreen(display);
  int attribs[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_DEPTH_SIZE, 24, 0};

  XVisualInfo* visual = glXChooseVisual(display, screen, attribs);
  if (!visual)
    return nullptr;

  GLXContext context = glXCreateContext(display, visual, nullptr, GL_TRUE);
  if (!context)
  {
    XFree(visual);
    return nullptr;
  }

  *glxContext = static_cast<void*>(context);
  return visual;
}

void DestroyGLContext(Display* display, void* glxContext)
{
  if (!display || !glxContext)
    return;
  glXMakeCurrent(display, 0, nullptr);
  glXDestroyContext(display, static_cast<GLXContext>(glxContext));
}

void ActivateGLContext(Display* display, Window drawable, void* glxContext)
{
  if (!display || !glxContext)
    return;
  (void)glXMakeCurrent(display, drawable, static_cast<GLXContext>(glxContext));
}

void DeactivateGLContext(Display* display)
{
  if (!display)
    return;
  glXMakeCurrent(display, 0, nullptr);
}

bool LoadGL() { return gladLoadGL() != 0; }

} // namespace igraphics
} // namespace iplug
