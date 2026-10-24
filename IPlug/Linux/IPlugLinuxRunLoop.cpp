/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#include "Linux/IPlugLinuxRunLoop.h"

#include <cassert>
#include <thread>
#include <utility>
#include <vector>

#include <poll.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace iplug
{
namespace
{

struct TimerBinding
{
  uint32_t id = 0;
  uint32_t intervalMs = 0;
  IPlugLinuxRunLoop::TimerHandler handler = nullptr;
  void* userData = nullptr;
  IPlugLinuxRunLoop* loop = nullptr;
  bool registered = false;
  uint32_t hostId = 0;
};

struct LoopSlot
{
  IPlugLinuxRunLoop* loop = nullptr;
  int refs = 0;
};

struct RunLoopState
{
  std::vector<LoopSlot> loops;
  std::vector<TimerBinding> bindings;
  uint32_t nextId = 1;
  IPlugLinuxRunLoop* setLoop = nullptr;
  std::thread::id uiThread{};
  bool haveUiThread = false;
};

RunLoopState& State()
{
  static RunLoopState state;
  return state;
}

void TouchUiThread()
{
  RunLoopState& state = State();
  if (!state.haveUiThread)
  {
    state.uiThread = std::this_thread::get_id();
    state.haveUiThread = true;
  }
#ifndef NDEBUG
  assert(std::this_thread::get_id() == state.uiThread && "IPlugLinux run loop used off the UI thread");
#endif
}

IPlugLinuxRunLoop* TopLoop()
{
  RunLoopState& state = State();
  if (state.loops.empty())
    return nullptr;
  return state.loops.back().loop;
}

void AttachPending(IPlugLinuxRunLoop* loop)
{
  if (!loop)
    return;
  for (auto& binding : State().bindings)
  {
    if (binding.registered || !binding.handler)
      continue;
    binding.hostId = loop->RegisterTimer(binding.intervalMs, binding.handler, binding.userData);
    binding.registered = binding.hostId != 0;
    binding.loop = binding.registered ? loop : nullptr;
  }
}

void MigrateOff(IPlugLinuxRunLoop* loop)
{
  if (!loop)
    return;
  IPlugLinuxRunLoop* next = TopLoop();
  for (auto& binding : State().bindings)
  {
    if (binding.loop != loop || !binding.registered)
      continue;
    loop->UnregisterTimer(binding.hostId);
    binding.registered = false;
    binding.hostId = 0;
    binding.loop = nullptr;
    if (!next || !binding.handler)
      continue;
    binding.hostId = next->RegisterTimer(binding.intervalMs, binding.handler, binding.userData);
    binding.registered = binding.hostId != 0;
    binding.loop = binding.registered ? next : nullptr;
  }
}

struct PollFd
{
  int fd = -1;
  IPlugLinuxRunLoop::FdHandler handler = nullptr;
  void* user = nullptr;
};

struct PollTimer
{
  uint32_t id = 0;
  int tfd = -1;
  IPlugLinuxRunLoop::TimerHandler handler = nullptr;
  void* user = nullptr;
};

} // namespace

void IPlugLinuxAssertUiThread() { TouchUiThread(); }

bool IPlugLinuxBindingsAreOnThisThread()
{
  RunLoopState& state = State();
  return !state.haveUiThread || std::this_thread::get_id() == state.uiThread;
}

void IPlugLinuxRetainRunLoop(IPlugLinuxRunLoop* runLoop)
{
  TouchUiThread();
  if (!runLoop)
    return;

  RunLoopState& state = State();
  for (auto& slot : state.loops)
  {
    if (slot.loop == runLoop)
    {
      ++slot.refs;
      return;
    }
  }

  state.loops.push_back(LoopSlot{runLoop, 1});
  AttachPending(runLoop);
}

bool IPlugLinuxReleaseRunLoop(IPlugLinuxRunLoop* runLoop)
{
  TouchUiThread();
  if (!runLoop)
    return false;

  RunLoopState& state = State();
  for (auto it = state.loops.begin(); it != state.loops.end(); ++it)
  {
    if (it->loop != runLoop)
      continue;
    if (--it->refs > 0)
      return false;
    // Drop the slot before migrating so TopLoop() is the loop that remains.
    state.loops.erase(it);
    MigrateOff(runLoop);
    return true;
  }
  return false;
}

void IPlugLinuxSetRunLoop(IPlugLinuxRunLoop* runLoop)
{
  TouchUiThread();
  RunLoopState& state = State();
  if (state.setLoop == runLoop)
    return;

  IPlugLinuxRunLoop* previous = state.setLoop;
  state.setLoop = runLoop;
  if (runLoop)
    IPlugLinuxRetainRunLoop(runLoop);
  if (previous)
    IPlugLinuxReleaseRunLoop(previous);
}

IPlugLinuxRunLoop* IPlugLinuxGetRunLoop() { return TopLoop(); }

uint32_t IPlugLinuxBindTimer(uint32_t intervalMs, IPlugLinuxRunLoop::TimerHandler handler, void* userData)
{
  TouchUiThread();
  RunLoopState& state = State();
  TimerBinding binding;
  binding.id = state.nextId++;
  if (state.nextId == 0)
    state.nextId = 1;
  binding.intervalMs = intervalMs;
  binding.handler = handler;
  binding.userData = userData;
  binding.loop = TopLoop();
  if (binding.loop && handler)
  {
    binding.hostId = binding.loop->RegisterTimer(intervalMs, handler, userData);
    binding.registered = binding.hostId != 0;
    if (!binding.registered)
      binding.loop = nullptr;
  }
  state.bindings.push_back(binding);
  return binding.id;
}

void IPlugLinuxUnbindTimer(uint32_t bindingId)
{
  TouchUiThread();
  if (!bindingId)
    return;

  RunLoopState& state = State();
  for (auto it = state.bindings.begin(); it != state.bindings.end(); ++it)
  {
    if (it->id != bindingId)
      continue;
    if (it->registered && it->loop)
      it->loop->UnregisterTimer(it->hostId);
    state.bindings.erase(it);
    return;
  }
}

struct IPlugLinuxPollRunLoop::Impl
{
  std::vector<PollFd> fds;
  std::vector<PollTimer> timers;
  uint32_t nextId = 1;
  bool polling = false;

  bool TimerAlive(uint32_t id) const
  {
    for (const auto& timer : timers)
    {
      if (timer.id == id)
        return true;
    }
    return false;
  }

  bool FdAlive(int fd) const
  {
    for (const auto& slot : fds)
    {
      if (slot.fd == fd)
        return true;
    }
    return false;
  }
};

IPlugLinuxPollRunLoop::IPlugLinuxPollRunLoop()
: mImpl(new Impl)
{
}

IPlugLinuxPollRunLoop::~IPlugLinuxPollRunLoop()
{
  for (auto& timer : mImpl->timers)
  {
    if (timer.tfd >= 0)
      close(timer.tfd);
  }
  delete mImpl;
  mImpl = nullptr;
}

void IPlugLinuxPollRunLoop::RegisterFd(int fd, FdHandler handler, void* userData)
{
  if (fd < 0 || !handler)
    return;
  UnregisterFd(fd);
  mImpl->fds.push_back(PollFd{fd, handler, userData});
}

void IPlugLinuxPollRunLoop::UnregisterFd(int fd)
{
  auto& fds = mImpl->fds;
  for (auto it = fds.begin(); it != fds.end(); ++it)
  {
    if (it->fd != fd)
      continue;
    fds.erase(it);
    return;
  }
}

uint32_t IPlugLinuxPollRunLoop::RegisterTimer(uint32_t intervalMs, TimerHandler handler, void* userData)
{
  if (!handler)
    return 0;
  if (intervalMs == 0)
    intervalMs = 1;

  const int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  if (tfd < 0)
    return 0;

  const uint64_t ms = intervalMs;
  itimerspec spec{};
  spec.it_interval.tv_sec = static_cast<time_t>(ms / 1000);
  spec.it_interval.tv_nsec = static_cast<long>((ms % 1000) * 1000000);
  spec.it_value = spec.it_interval;
  if (timerfd_settime(tfd, 0, &spec, nullptr) != 0)
  {
    close(tfd);
    return 0;
  }

  const uint32_t id = mImpl->nextId++;
  if (mImpl->nextId == 0)
    mImpl->nextId = 1;
  mImpl->timers.push_back(PollTimer{id, tfd, handler, userData});
  return id;
}

void IPlugLinuxPollRunLoop::UnregisterTimer(uint32_t timerId)
{
  auto& timers = mImpl->timers;
  for (auto it = timers.begin(); it != timers.end(); ++it)
  {
    if (it->id != timerId)
      continue;
    if (it->tfd >= 0)
      close(it->tfd);
    timers.erase(it);
    return;
  }
}

int IPlugLinuxPollRunLoop::Poll(int timeoutMs)
{
  if (!mImpl || mImpl->polling)
    return 0;
  mImpl->polling = true;

  std::vector<pollfd> pfds;
  pfds.reserve(mImpl->fds.size() + mImpl->timers.size());
  for (const auto& slot : mImpl->fds)
    pfds.push_back(pollfd{slot.fd, POLLIN, 0});
  const size_t timerBegin = pfds.size();
  for (const auto& timer : mImpl->timers)
    pfds.push_back(pollfd{timer.tfd, POLLIN, 0});

  const int waited = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), timeoutMs < 0 ? -1 : timeoutMs);
  int fired = 0;
  if (waited > 0)
  {
    struct DueFd
    {
      int fd;
      IPlugLinuxRunLoop::FdHandler handler;
      void* user;
    };
    struct DueTimer
    {
      uint32_t id;
      IPlugLinuxRunLoop::TimerHandler handler;
      void* user;
    };
    std::vector<DueFd> dueFds;
    std::vector<DueTimer> dueTimers;

    for (size_t i = 0; i < mImpl->fds.size() && i < timerBegin; ++i)
    {
      if (pfds[i].revents & (POLLIN | POLLERR | POLLHUP))
        dueFds.push_back(DueFd{mImpl->fds[i].fd, mImpl->fds[i].handler, mImpl->fds[i].user});
    }
    for (size_t i = 0; i < mImpl->timers.size(); ++i)
    {
      const size_t index = timerBegin + i;
      if (index >= pfds.size() || !(pfds[index].revents & POLLIN))
        continue;
      uint64_t expirations = 0;
      const ssize_t n = read(mImpl->timers[i].tfd, &expirations, sizeof expirations);
      (void)n;
      dueTimers.push_back(DueTimer{mImpl->timers[i].id, mImpl->timers[i].handler, mImpl->timers[i].user});
    }

    for (const auto& due : dueFds)
    {
      if (!mImpl->FdAlive(due.fd) || !due.handler)
        continue;
      due.handler(due.fd, due.user);
      ++fired;
    }
    for (const auto& due : dueTimers)
    {
      if (!mImpl->TimerAlive(due.id) || !due.handler)
        continue;
      due.handler(due.user);
      ++fired;
    }
  }

  mImpl->polling = false;
  return fired;
}

} // namespace iplug
