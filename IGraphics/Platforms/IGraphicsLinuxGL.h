/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

/**
 * @file
 * GLX 1.2 context setup used by IGraphicsLinux::CreateGLContext in iPlug2#1336.
 *
 * The .cpp includes glad, then GL/glx.h, while X11 Bool is still defined.
 * There is no render thread and no XInitThreads. The caller makes the
 * context current on the host UI thread, then calls LoadGL() once.
 *
 * CreateGLContext returns the visual. The caller XFree()s it after creating
 * the window. *glxContext receives the GLXContext.
 */

#include "IPlugPlatform.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifdef None
#undef None
#endif
#ifdef Bool
#undef Bool
#endif
#ifdef Status
#undef Status
#endif
#ifdef True
#undef True
#endif
#ifdef False
#undef False
#endif
#ifdef Success
#undef Success
#endif
#ifdef Complex
#undef Complex
#endif
#ifdef Above
#undef Above
#endif
#ifdef Below
#undef Below
#endif
#ifdef Convex
#undef Convex
#endif
#ifdef CurrentTime
#undef CurrentTime
#endif

BEGIN_IPLUG_NAMESPACE
BEGIN_IGRAPHICS_NAMESPACE

XVisualInfo* CreateGLContext(Display* display, void** glxContext);
void DestroyGLContext(Display* display, void* glxContext);
void ActivateGLContext(Display* display, Window drawable, void* glxContext);
void DeactivateGLContext(Display* display);

/** gladLoadGL(). The GLX context must already be current. */
bool LoadGL();

END_IGRAPHICS_NAMESPACE
END_IPLUG_NAMESPACE
