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
 * Retain and release run on the UI thread. A timer stays on the loop it
 * bound to, so closing one editor does not detach another editor's timers.
 * UnregisterTimer must be safe from inside its own callback. Timer::Create
 * binds here. A timer created before a loop is retained stays pending until
 * one is. Debug builds assert that every bind stays on the first thread.
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

/**
 * Retain a loop. The most recently retained loop receives new timers.
 * The same pointer twice counts twice; release it twice.
 */
void IPlugLinuxRetainRunLoop(IPlugLinuxRunLoop* runLoop);
/**
 * Drop one retain. Timers bound to this loop move to the loop that remains.
 * @return true when the last retain is gone.
 */
bool IPlugLinuxReleaseRunLoop(IPlugLinuxRunLoop* runLoop);

/** Install or clear one loop. Other retained loops are left alone. */
void IPlugLinuxSetRunLoop(IPlugLinuxRunLoop* runLoop);
IPlugLinuxRunLoop* IPlugLinuxGetRunLoop();

/** Debug builds assert that this is the thread that first bound the table. */
void IPlugLinuxAssertUiThread();
bool IPlugLinuxBindingsAreOnThisThread();

/**
 * poll() + timerfd loop for the standalone app. Ticket 10 drives it.
 * UnregisterTimer is safe from inside that timer's callback.
 */
class IPlugLinuxPollRunLoop final : public IPlugLinuxRunLoop
{
public:
  IPlugLinuxPollRunLoop();
  ~IPlugLinuxPollRunLoop() override;
  IPlugLinuxPollRunLoop(const IPlugLinuxPollRunLoop&) = delete;
  IPlugLinuxPollRunLoop& operator=(const IPlugLinuxPollRunLoop&) = delete;

  void RegisterFd(int fd, FdHandler handler, void* userData) override;
  void UnregisterFd(int fd) override;
  uint32_t RegisterTimer(uint32_t intervalMs, TimerHandler handler, void* userData) override;
  void UnregisterTimer(uint32_t timerId) override;

  /** Wait up to timeoutMs for fds and timers. Returns how many callbacks ran. */
  int Poll(int timeoutMs);

private:
  struct Impl;
  Impl* mImpl;
};

/**
 * Remember a timer and register it with the current loop when one exists.
 * @return A non-zero binding id for IPlugLinuxUnbindTimer.
 */
uint32_t IPlugLinuxBindTimer(uint32_t intervalMs, IPlugLinuxRunLoop::TimerHandler handler, void* userData);
void IPlugLinuxUnbindTimer(uint32_t bindingId);

END_IPLUG_NAMESPACE
