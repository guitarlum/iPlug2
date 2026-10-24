/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

/**
 * @file
 * Host UI-thread run loops for Linux timers and file descriptors.
 *
 * Every X11 and GL call stays on the host UI thread. Nothing here starts a
 * thread or calls XInitThreads.
 *
 * Loops form a stack. The VST3 view pushes the IRunLoop of its IPlugFrame in
 * attached() and pops it in removed(); IPlugVST3 pushes the host-context loop
 * for the editor-closed case; the standalone app pushes IPlugLinuxPollRunLoop.
 * A binding (Timer::Create, the editor's X fd and draw timer) registers with
 * the loop on top of the stack. When a loop is popped, only the bindings it
 * carried move to the new top, so closing one editor never detaches another
 * instance's timers. Pushing the same loop twice counts references.
 *
 * Every function runs on the host UI thread (the first thread that calls into
 * this file). There is no lock: an off-thread call asserts in Debug and is
 * counted and logged to stderr in every build.
 */

#include <stdint.h>

#include <memory>
#include <utility>
#include <vector>

#include "../IPlugPlatform.h"

BEGIN_IPLUG_NAMESPACE

class IPlugLinuxRunLoop
{
public:
  using FdHandler = void (*)(int fd, void* userData);
  using TimerHandler = void (*)(void* userData);

  virtual ~IPlugLinuxRunLoop() = default;

  /** Call handler on the UI thread while fd is readable. One handler per fd. */
  virtual void RegisterFd(int fd, FdHandler handler, void* userData) = 0;
  virtual void UnregisterFd(int fd) = 0;

  /**
   * Periodic UI-thread callback.
   * @return A non-zero id for UnregisterTimer, or 0 if the loop refused it.
   */
  virtual uint32_t RegisterTimer(uint32_t intervalMs, TimerHandler handler, void* userData) = 0;
  /** Safe from inside the timer's own callback. */
  virtual void UnregisterTimer(uint32_t timerId) = 0;
};

/** Push a loop, or add a reference to one already on the stack. */
void IPlugLinuxAddRunLoop(IPlugLinuxRunLoop* runLoop);
/** Drop a reference. At zero the loop leaves the stack and its bindings move to the new top. */
void IPlugLinuxRemoveRunLoop(IPlugLinuxRunLoop* runLoop);
/** The loop on top of the stack, or nullptr. */
IPlugLinuxRunLoop* IPlugLinuxGetRunLoop();

/**
 * Remember a timer and register it with the top loop, now or when one is pushed.
 * @return A non-zero binding id for IPlugLinuxUnbindTimer.
 */
uint32_t IPlugLinuxBindTimer(uint32_t intervalMs, IPlugLinuxRunLoop::TimerHandler handler, void* userData);
void IPlugLinuxUnbindTimer(uint32_t bindingId);

/** As IPlugLinuxBindTimer, for a readable file descriptor. */
uint32_t IPlugLinuxBindFd(int fd, IPlugLinuxRunLoop::FdHandler handler, void* userData);
void IPlugLinuxUnbindFd(uint32_t bindingId);

/** Owns a loop while it is on the stack: pushed on construction, popped by Reset() or the destructor. */
class IPlugLinuxPushedRunLoop
{
public:
  IPlugLinuxPushedRunLoop() = default;
  explicit IPlugLinuxPushedRunLoop(std::unique_ptr<IPlugLinuxRunLoop> runLoop)
    : mRunLoop(std::move(runLoop))
  {
    IPlugLinuxAddRunLoop(mRunLoop.get());
  }
  ~IPlugLinuxPushedRunLoop() { Reset(); }
  IPlugLinuxPushedRunLoop(const IPlugLinuxPushedRunLoop&) = delete;
  IPlugLinuxPushedRunLoop& operator=(const IPlugLinuxPushedRunLoop&) = delete;
  IPlugLinuxPushedRunLoop& operator=(IPlugLinuxPushedRunLoop&& other) noexcept
  {
    Reset();
    mRunLoop = std::move(other.mRunLoop);
    return *this;
  }

  explicit operator bool() const { return mRunLoop != nullptr; }

  void Reset()
  {
    if (!mRunLoop)
      return;
    IPlugLinuxRemoveRunLoop(mRunLoop.get());
    mRunLoop.reset();
  }

private:
  std::unique_ptr<IPlugLinuxRunLoop> mRunLoop;
};

/** True on the thread that first called into the run-loop registry. */
bool IPlugLinuxIsUIThread();
/** Registry calls made from any other thread since the process started. */
int IPlugLinuxOffThreadCalls();

/**
 * poll(2) loop with one timerfd per timer, for the standalone app.
 * The app pushes it with IPlugLinuxAddRunLoop and calls RunOnce until it quits.
 */
class IPlugLinuxPollRunLoop final : public IPlugLinuxRunLoop
{
public:
  IPlugLinuxPollRunLoop() = default;
  ~IPlugLinuxPollRunLoop() override;
  IPlugLinuxPollRunLoop(const IPlugLinuxPollRunLoop&) = delete;
  IPlugLinuxPollRunLoop& operator=(const IPlugLinuxPollRunLoop&) = delete;

  void RegisterFd(int fd, FdHandler handler, void* userData) override;
  void UnregisterFd(int fd) override;
  uint32_t RegisterTimer(uint32_t intervalMs, TimerHandler handler, void* userData) override;
  void UnregisterTimer(uint32_t timerId) override;

  /**
   * Wait up to timeoutMs (-1 waits forever) and dispatch what is ready.
   * @return The number of callbacks dispatched, or -1 if poll failed.
   */
  int RunOnce(int timeoutMs);

private:
  struct Source
  {
    int fd = -1;
    uint32_t timerId = 0; // 0 for a plain fd
    FdHandler fdHandler = nullptr;
    TimerHandler timerHandler = nullptr;
    void* userData = nullptr;
  };

  Source* Find(int fd, uint32_t timerId);

  std::vector<Source> mSources;
  uint32_t mNextTimerId = 1;
};

END_IPLUG_NAMESPACE
