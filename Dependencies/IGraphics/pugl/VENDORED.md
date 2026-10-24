# Vendored pugl

Upstream: https://github.com/lv2/pugl
Commit: `6cba2b564a7a767cefbd9259c87c0284b0e2acb8` (2026-10-17, "Windows: Strengthen clang warning flags")
Licence: ISC (`COPYING`)

Only the X11 + OpenGL backend is included. Cairo, Vulkan, Mac, and Windows sources are not copied.

## Files

- `COPYING`
- `include/pugl/attributes.h`
- `include/pugl/gl.h`
- `include/pugl/pugl.h`
- `src/attributes.h`
- `src/common.c`
- `src/internal.c`
- `src/internal.h`
- `src/macros.h`
- `src/platform.h`
- `src/stub.h`
- `src/types.h`
- `src/x11.c`
- `src/x11.h`
- `src/x11_gl.c`

## Local changes

1. `XInitThreads` is not called. `PUGL_WORLD_THREADS` is ignored.
   Upstream called it from `puglInitWorldInternals` when the world type was
   `PUGL_PROGRAM` and the flag was set. VoLum never takes that path: the host
   UI thread owns the `Display`, and `puglNewWorld` is called as `PUGL_MODULE`
   with no flags. The call itself is removed so a later edit cannot turn it
   back on by accident. `src/x11.c`, `include/pugl/pugl.h`.

2. GLX 1.2 fallback in `src/x11_gl.c`. If `glXChooseFBConfig` returns no
   configs, the backend uses `glXChooseVisual` + `glXCreateContext` with the
   same attribute list as `IGraphicsLinuxGL` (RGBA, double buffer, 8/8/8,
   depth 24). Some llvmpipe and Xvfb servers expose GLX 1.2 only. When an
   FBConfig exists, the upstream path is unchanged.
