/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

/**
 * @file
 * Nesting of IGraphicsLinux::ActivateGLContext / DeactivateGLContext, without
 * any GL or X so it can be tested on its own.
 *
 * Every Enter is matched by exactly one Leave, whether or not a context still
 * exists at Leave time: a CloseWindow inside an event handler destroys the
 * context while the handler's own scope is still open.
 */

namespace iplug
{
namespace igraphics
{

class IGraphicsLinuxGLNesting
{
public:
  /** @return true on the outermost Enter with a live context: save the thread's current context now. */
  bool Enter(bool hasContext)
  {
    if (mDepth++ > 0)
      return false;
    mRestore = hasContext;
    return mRestore;
  }

  /** @return true on the outermost Leave after a saving Enter: restore the saved context now. */
  bool Leave()
  {
    if (mDepth == 0)
      return false;
    if (--mDepth > 0)
      return false;
    const bool restore = mRestore;
    mRestore = false;
    return restore;
  }

  int Depth() const { return mDepth; }

private:
  int mDepth = 0;
  bool mRestore = false;
};

} // namespace igraphics
} // namespace iplug
