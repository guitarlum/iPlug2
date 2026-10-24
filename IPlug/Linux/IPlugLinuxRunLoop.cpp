/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#include "IPlugLinuxRunLoop.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <atomic>
#include <thread>

namespace iplug
{
namespace
{

struct Binding
{
  uint32_t id = 0;
  int fd = -1; // >= 0 for an fd binding, -1 for a timer
  uint32_t intervalMs = 0;
  IPlugLinuxRunLoop::FdHandler fdHandler = nullptr;
  IPlugLinuxRunLoop::TimerHandler timerHandler = nullptr;
  void* userData = nullptr;
  IPlugLinuxRunLoop* loop = nullptr; // the loop it is registered with
  uint32_t loopTimerId = 0;
};

struct LoopRef
{
  IPlugLinuxRunLoop* loop = nullptr;
  int refs = 0;
};

struct Registry
{
  std::vector<LoopRef> loops; // bottom first, top last
  std::vector<Binding> bindings;
  uint32_t nextId = 1;
  std::thread::id uiThread;
  bool haveUIThread = false;
  std::atomic<int> offThreadCalls{0};
};

Registry& State()
{
  static Registry registry;
  return registry;
}

void CheckThread(const char* function)
{
  Registry& r = State();
  const std::thread::id self = std::this_thread::get_id();
  if (!r.haveUIThread)
  {
    r.uiThread = self;
    r.haveUIThread = true;
    return;
  }
  if (self == r.uiThread)
    return;
  if (r.offThreadCalls.fetch_add(1) < 20)
    fprintf(stderr, "IPlugLinuxRunLoop: %s called off the UI thread\n", function);
  assert(!"IPlugLinuxRunLoop called off the UI thread");
}

IPlugLinuxRunLoop* Top()
{
  Registry& r = State();
  return r.loops.empty() ? nullptr : r.loops.back().loop;
}

void Attach(Binding& b, IPlugLinuxRunLoop* loop)
{
  b.loop = loop;
  if (!loop)
    return;
  if (b.fd >= 0)
    loop->RegisterFd(b.fd, b.fdHandler, b.userData);
  else if (!(b.loopTimerId = loop->RegisterTimer(b.intervalMs, b.timerHandler, b.userData)))
    fprintf(stderr, "IPlugLinuxRunLoop: the run loop refused a %u ms timer\n", b.intervalMs);
}

void Detach(Binding& b)
{
  if (!b.loop)
    return;
  if (b.fd >= 0)
    b.loop->UnregisterFd(b.fd);
  else if (b.loopTimerId)
    b.loop->UnregisterTimer(b.loopTimerId);
  b.loop = nullptr;
  b.loopTimerId = 0;
}

uint32_t Bind(Binding b)
{
  Registry& r = State();
  b.id = r.nextId++;
  if (r.nextId == 0)
    r.nextId = 1;
  r.bindings.push_back(b);
  Attach(r.bindings.back(), Top());
  return b.id;
}

void Unbind(uint32_t bindingId)
{
  Registry& r = State();
  for (size_t i = 0; i < r.bindings.size(); ++i)
  {
    if (r.bindings[i].id != bindingId)
      continue;
    Detach(r.bindings[i]);
    r.bindings.erase(r.bindings.begin() + i);
    return;
  }
}

} // namespace

void IPlugLinuxAddRunLoop(IPlugLinuxRunLoop* runLoop)
{
  CheckThread("IPlugLinuxAddRunLoop");
  if (!runLoop)
    return;

  Registry& r = State();
  for (auto& ref : r.loops)
  {
    if (ref.loop == runLoop)
    {
      ++ref.refs;
      return;
    }
  }

  r.loops.push_back({runLoop, 1});
  for (size_t i = 0; i < r.bindings.size(); ++i)
  {
    if (!r.bindings[i].loop)
      Attach(r.bindings[i], runLoop);
  }
}

void IPlugLinuxRemoveRunLoop(IPlugLinuxRunLoop* runLoop)
{
  CheckThread("IPlugLinuxRemoveRunLoop");
  Registry& r = State();
  for (size_t i = 0; i < r.loops.size(); ++i)
  {
    if (r.loops[i].loop != runLoop)
      continue;
    if (--r.loops[i].refs > 0)
      return;

    r.loops.erase(r.loops.begin() + i);
    IPlugLinuxRunLoop* top = Top();
    for (size_t j = 0; j < r.bindings.size(); ++j)
    {
      if (r.bindings[j].loop != runLoop)
        continue;
      Detach(r.bindings[j]);
      Attach(r.bindings[j], top);
    }
    return;
  }
}

IPlugLinuxRunLoop* IPlugLinuxGetRunLoop() { return Top(); }

uint32_t IPlugLinuxBindTimer(uint32_t intervalMs, IPlugLinuxRunLoop::TimerHandler handler, void* userData)
{
  CheckThread("IPlugLinuxBindTimer");
  Binding b;
  b.intervalMs = intervalMs;
  b.timerHandler = handler;
  b.userData = userData;
  return handler ? Bind(b) : 0;
}

void IPlugLinuxUnbindTimer(uint32_t bindingId)
{
  CheckThread("IPlugLinuxUnbindTimer");
  if (bindingId)
    Unbind(bindingId);
}

uint32_t IPlugLinuxBindFd(int fd, IPlugLinuxRunLoop::FdHandler handler, void* userData)
{
  CheckThread("IPlugLinuxBindFd");
  Binding b;
  b.fd = fd;
  b.fdHandler = handler;
  b.userData = userData;
  return (fd >= 0 && handler) ? Bind(b) : 0;
}

void IPlugLinuxUnbindFd(uint32_t bindingId)
{
  CheckThread("IPlugLinuxUnbindFd");
  if (bindingId)
    Unbind(bindingId);
}

bool IPlugLinuxIsUIThread()
{
  const Registry& r = State();
  return !r.haveUIThread || r.uiThread == std::this_thread::get_id();
}

int IPlugLinuxOffThreadCalls() { return State().offThreadCalls.load(); }

IPlugLinuxPollRunLoop::~IPlugLinuxPollRunLoop()
{
  for (auto& source : mSources)
  {
    if (source.timerId)
      close(source.fd);
  }
}

IPlugLinuxPollRunLoop::Source* IPlugLinuxPollRunLoop::Find(int fd, uint32_t timerId)
{
  for (auto& source : mSources)
  {
    if (source.timerId == timerId && (timerId || source.fd == fd))
      return &source;
  }
  return nullptr;
}

void IPlugLinuxPollRunLoop::RegisterFd(int fd, FdHandler handler, void* userData)
{
  if (fd < 0 || !handler)
    return;
  if (Source* source = Find(fd, 0))
  {
    source->fdHandler = handler;
    source->userData = userData;
    return;
  }
  Source source;
  source.fd = fd;
  source.fdHandler = handler;
  source.userData = userData;
  mSources.push_back(source);
}

void IPlugLinuxPollRunLoop::UnregisterFd(int fd)
{
  for (size_t i = 0; i < mSources.size(); ++i)
  {
    if (!mSources[i].timerId && mSources[i].fd == fd)
    {
      mSources.erase(mSources.begin() + i);
      return;
    }
  }
}

uint32_t IPlugLinuxPollRunLoop::RegisterTimer(uint32_t intervalMs, TimerHandler handler, void* userData)
{
  if (!handler)
    return 0;
  const int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (fd < 0)
    return 0;

  const uint32_t ms = intervalMs ? intervalMs : 1;
  itimerspec spec = {};
  spec.it_interval.tv_sec = ms / 1000;
  spec.it_interval.tv_nsec = static_cast<long>(ms % 1000) * 1000000L;
  spec.it_value = spec.it_interval;
  if (timerfd_settime(fd, 0, &spec, nullptr) != 0)
  {
    close(fd);
    return 0;
  }

  Source source;
  source.fd = fd;
  source.timerId = mNextTimerId++;
  if (mNextTimerId == 0)
    mNextTimerId = 1;
  source.timerHandler = handler;
  source.userData = userData;
  mSources.push_back(source);
  return source.timerId;
}

void IPlugLinuxPollRunLoop::UnregisterTimer(uint32_t timerId)
{
  for (size_t i = 0; i < mSources.size(); ++i)
  {
    if (timerId && mSources[i].timerId == timerId)
    {
      close(mSources[i].fd);
      mSources.erase(mSources.begin() + i);
      return;
    }
  }
}

int IPlugLinuxPollRunLoop::RunOnce(int timeoutMs)
{
  std::vector<pollfd> fds;
  std::vector<Source> snapshot = mSources;
  fds.reserve(snapshot.size());
  for (const auto& source : snapshot)
    fds.push_back({source.fd, POLLIN, 0});

  if (poll(fds.data(), fds.size(), timeoutMs) < 0)
    return errno == EINTR ? 0 : -1;

  // A callback may register or unregister sources, so every ready entry is
  // looked up again before it is dispatched.
  int dispatched = 0;
  for (size_t i = 0; i < fds.size(); ++i)
  {
    if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR)))
      continue;
    Source* source = Find(snapshot[i].fd, snapshot[i].timerId);
    if (!source)
      continue;
    const Source ready = *source;
    if (ready.timerId)
    {
      uint64_t expirations = 0;
      if (read(ready.fd, &expirations, sizeof(expirations)) != sizeof(expirations))
        continue;
      ready.timerHandler(ready.userData);
    }
    else
    {
      ready.fdHandler(ready.fd, ready.userData);
    }
    ++dispatched;
  }
  return dispatched;
}

} // namespace iplug
