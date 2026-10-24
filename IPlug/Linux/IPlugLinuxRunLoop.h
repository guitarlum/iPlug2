/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

/**
 * @file
 * Host UI-thread run loop for Linux timers and X11 file descriptors.
 *
 * Every X11 and GL call stays on the host UI thread. This header does not
 * start a thread and does not call XInitThreads (that is the flaw in the
 * timer thread from iPlug2#1336). Ticket 07 installs an implementation:
 * the VST3 editor adapts Steinberg IRunLoop (the X connection fd plus a
 * ~16 ms timer), and the standalone app drives the same callbacks itself.
 *
 * RegisterFd, RegisterTimer, and IPlugLinuxSetRunLoop run on the UI thread.
 * UnregisterTimer must be safe from inside its own callback. Timer::Create
 * binds here. A timer created before a loop is installed is kept and
 * attached when IPlugLinuxSetRunLoop gets one.
 */

#include <stdint.h>

#include "IPlugPlatform.h"

BEGIN_IPLUG_NAMESPACE

class IPlugLinuxRunLoop
{
public:
  using FdHandler = void (*)(int fd, void* userData);
  using TimerHandler = void (*)(void* userData);

  virtual ~IPlugLinuxRunLoop() = default;

  /** Call handler on the UI thread when fd is readable. */
  virtual void RegisterFd(int fd, FdHandler handler, void* userData) = 0;
  virtual void UnregisterFd(int fd) = 0;

  /**
   * Periodic UI-thread callback.
   * @return An id that UnregisterTimer accepts. The id is chosen by the host.
   */
  virtual uint32_t RegisterTimer(uint32_t intervalMs, TimerHandler handler, void* userData) = 0;
  virtual void UnregisterTimer(uint32_t timerId) = 0;
};

/** Install or clear the process-wide loop. Pass nullptr to detach. */
void IPlugLinuxSetRunLoop(IPlugLinuxRunLoop* runLoop);
IPlugLinuxRunLoop* IPlugLinuxGetRunLoop();

/**
 * Remember a timer and register it with the current loop when one exists.
 * @return A non-zero binding id for IPlugLinuxUnbindTimer.
 */
uint32_t IPlugLinuxBindTimer(uint32_t intervalMs, IPlugLinuxRunLoop::TimerHandler handler, void* userData);
void IPlugLinuxUnbindTimer(uint32_t bindingId);

END_IPLUG_NAMESPACE
