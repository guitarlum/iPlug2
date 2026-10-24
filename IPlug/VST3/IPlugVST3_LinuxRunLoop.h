/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

/**
 * @file
 * IPlugLinuxRunLoop adapters for VST3 hosts on Linux.
 *
 * IPlugVST3RunLoop wraps the Steinberg::Linux::IRunLoop that a host exposes on
 * its IPlugFrame (editor open) or on the component's host context.
 * IPlugVST3FactoryTimers drives timers through Steinberg::Timer, which the SDK
 * binds to the IRunLoop passed to IPluginFactory3::setHostContext.
 */

#include "IPlugPlatform.h"

#if defined OS_LINUX

  #include <atomic>
  #include <memory>
  #include <utility>
  #include <vector>

  #include "base/source/timer.h"
  #include "pluginterfaces/base/funknown.h"
  #include "pluginterfaces/gui/iplugview.h"

  #include "Linux/IPlugLinuxRunLoop.h"

BEGIN_IPLUG_NAMESPACE

inline uint32_t VST3RunLoopNextId(uint32_t& counter)
{
  const uint32_t id = counter++;
  if (!counter)
    counter = 1;
  return id;
}

class IPlugVST3RunLoop final : public IPlugLinuxRunLoop
{
public:
  /** @return An adapter, or nullptr when pUnknown does not implement Linux::IRunLoop. */
  static std::unique_ptr<IPlugVST3RunLoop> Query(Steinberg::FUnknown* pUnknown)
  {
    Steinberg::FUnknownPtr<Steinberg::Linux::IRunLoop> runLoop(pUnknown);
    if (!runLoop)
      return nullptr;
    return std::unique_ptr<IPlugVST3RunLoop>(new IPlugVST3RunLoop(runLoop));
  }

  ~IPlugVST3RunLoop() override
  {
    for (auto& fd : mFds)
    {
      fd->Cancel();
      mRunLoop->unregisterEventHandler(fd);
    }
    for (auto& timer : mTimers)
    {
      timer.second->Cancel();
      mRunLoop->unregisterTimer(timer.second);
    }
  }

  void RegisterFd(int fd, FdHandler handler, void* userData) override
  {
    Steinberg::IPtr<HostFd> hostFd(new HostFd(fd, handler, userData), false);
    if (mRunLoop->registerEventHandler(hostFd, fd) == Steinberg::kResultOk)
      mFds.push_back(hostFd);
  }

  void UnregisterFd(int fd) override
  {
    for (size_t i = 0; i < mFds.size(); ++i)
    {
      if (mFds[i]->mFd != fd)
        continue;
      Steinberg::IPtr<HostFd> hostFd = mFds[i];
      mFds.erase(mFds.begin() + i);
      hostFd->Cancel();
      mRunLoop->unregisterEventHandler(hostFd);
      return;
    }
  }

  uint32_t RegisterTimer(uint32_t intervalMs, TimerHandler handler, void* userData) override
  {
    Steinberg::IPtr<HostTimer> timer(new HostTimer(handler, userData), false);
    if (mRunLoop->registerTimer(timer, intervalMs) != Steinberg::kResultOk)
      return 0;
    const uint32_t id = VST3RunLoopNextId(mNextId);
    mTimers.emplace_back(id, timer);
    return id;
  }

  void UnregisterTimer(uint32_t timerId) override
  {
    for (size_t i = 0; i < mTimers.size(); ++i)
    {
      if (mTimers[i].first != timerId)
        continue;
      Steinberg::IPtr<HostTimer> timer = mTimers[i].second;
      mTimers.erase(mTimers.begin() + i);
      timer->Cancel();
      mRunLoop->unregisterTimer(timer);
      return;
    }
  }

private:
  explicit IPlugVST3RunLoop(Steinberg::IPtr<Steinberg::Linux::IRunLoop> runLoop)
    : mRunLoop(std::move(runLoop))
  {
  }

  template <class Interface>
  class Handler : public Interface
  {
  public:
    virtual ~Handler() = default;

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
    {
      if (Steinberg::FUnknownPrivate::iidEqual(iid, Interface::iid) || Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
      {
        addRef();
        *obj = static_cast<Interface*>(this);
        return Steinberg::kResultOk;
      }
      *obj = nullptr;
      return Steinberg::kNoInterface;
    }

    Steinberg::uint32 PLUGIN_API addRef() override { return ++mRefs; }

    Steinberg::uint32 PLUGIN_API release() override
    {
      const Steinberg::uint32 refs = --mRefs;
      if (!refs)
        delete this;
      return refs;
    }

  private:
    std::atomic<Steinberg::uint32> mRefs{1};
  };

  // The callback holds a reference to its handler, because the handler may be
  // unregistered and released from inside that callback.
  class HostTimer final : public Handler<Steinberg::Linux::ITimerHandler>
  {
  public:
    HostTimer(TimerHandler handler, void* userData)
      : mHandler(handler)
      , mUserData(userData)
    {
    }

    void PLUGIN_API onTimer() override
    {
      if (!mHandler)
        return;
      Steinberg::IPtr<HostTimer> keepAlive(this);
      mHandler(mUserData);
    }

    void Cancel() { mHandler = nullptr; }

  private:
    TimerHandler mHandler;
    void* mUserData;
  };

  class HostFd final : public Handler<Steinberg::Linux::IEventHandler>
  {
  public:
    HostFd(int fd, FdHandler handler, void* userData)
      : mFd(fd)
      , mHandler(handler)
      , mUserData(userData)
    {
    }

    void PLUGIN_API onFDIsSet(Steinberg::Linux::FileDescriptor fd) override
    {
      if (!mHandler)
        return;
      Steinberg::IPtr<HostFd> keepAlive(this);
      mHandler(fd, mUserData);
    }

    void Cancel() { mHandler = nullptr; }

    const int mFd;

  private:
    FdHandler mHandler;
    void* mUserData;
  };

  Steinberg::IPtr<Steinberg::Linux::IRunLoop> mRunLoop;
  std::vector<Steinberg::IPtr<HostFd>> mFds;
  std::vector<std::pair<uint32_t, Steinberg::IPtr<HostTimer>>> mTimers;
  uint32_t mNextId = 1;
};

class IPlugVST3FactoryTimers final : public IPlugLinuxRunLoop
{
public:
  /** @return Timers on the factory host context's IRunLoop, or nullptr when the host gave none. */
  static std::unique_ptr<IPlugVST3FactoryTimers> Create()
  {
    Entry probe(nullptr, nullptr);
    Steinberg::Timer* pTimer = Steinberg::Timer::create(&probe, 1000);
    if (!pTimer)
      return nullptr;
    pTimer->stop();
    pTimer->release();
    return std::unique_ptr<IPlugVST3FactoryTimers>(new IPlugVST3FactoryTimers());
  }

  void RegisterFd(int, FdHandler, void*) override {}
  void UnregisterFd(int) override {}

  uint32_t RegisterTimer(uint32_t intervalMs, TimerHandler handler, void* userData) override
  {
    mRetired.clear();
    std::unique_ptr<Entry> entry(new Entry(handler, userData));
    entry->mTimer = Steinberg::Timer::create(entry.get(), intervalMs);
    if (!entry->mTimer)
      return 0;
    const uint32_t id = VST3RunLoopNextId(mNextId);
    mEntries.emplace_back(id, std::move(entry));
    return id;
  }

  // The SDK timer may be the one calling us, so it is released on a later call.
  void UnregisterTimer(uint32_t timerId) override
  {
    for (size_t i = 0; i < mEntries.size(); ++i)
    {
      if (mEntries[i].first != timerId)
        continue;
      mEntries[i].second->Cancel();
      mRetired.push_back(std::move(mEntries[i].second));
      mEntries.erase(mEntries.begin() + i);
      return;
    }
  }

private:
  IPlugVST3FactoryTimers() = default;

  class Entry final : public Steinberg::ITimerCallback
  {
  public:
    Entry(TimerHandler handler, void* userData)
      : mHandler(handler)
      , mUserData(userData)
    {
    }

    ~Entry() override
    {
      if (mTimer)
      {
        mTimer->stop();
        mTimer->release();
      }
    }

    void onTimer(Steinberg::Timer*) override
    {
      if (mHandler)
        mHandler(mUserData);
    }

    void Cancel()
    {
      mHandler = nullptr;
      if (mTimer)
        mTimer->stop();
    }

    Steinberg::Timer* mTimer = nullptr;

  private:
    TimerHandler mHandler;
    void* mUserData;
  };

  std::vector<std::pair<uint32_t, std::unique_ptr<Entry>>> mEntries;
  std::vector<std::unique_ptr<Entry>> mRetired;
  uint32_t mNextId = 1;
};

END_IPLUG_NAMESPACE

#endif
