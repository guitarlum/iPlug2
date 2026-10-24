/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#include "IPlugVST3_Linux.h"

#if defined OS_LINUX

#include <atomic>
#include <cstdint>
#include <utility>
#include <vector>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/gui/iplugview.h"

namespace iplug
{
namespace
{

class Vst3RunLoop final : public IPlugLinuxRunLoop
{
public:
  explicit Vst3RunLoop(Steinberg::Linux::IRunLoop* host)
  : mHost(host)
  {
  }

  ~Vst3RunLoop() override
  {
    for (auto& timer : mTimers)
      ReleaseBridge(timer.bridge, true);
    mTimers.clear();
    for (auto& fd : mFds)
      ReleaseBridge(fd.bridge, false);
    mFds.clear();
    if (mHost)
      mHost->release();
  }

  Steinberg::Linux::IRunLoop* Host() const { return mHost; }

  void RegisterFd(int fd, FdHandler handler, void* userData) override
  {
    if (!mHost || fd < 0 || !handler)
      return;
    UnregisterFd(fd);
    auto* bridge = new FdBridge(handler, userData);
    if (mHost->registerEventHandler(bridge, fd) != Steinberg::kResultOk)
    {
      bridge->release();
      return;
    }
    mFds.push_back(FdSlot{fd, bridge});
  }

  void UnregisterFd(int fd) override
  {
    for (auto it = mFds.begin(); it != mFds.end(); ++it)
    {
      if (it->fd != fd)
        continue;
      ReleaseBridge(it->bridge, false);
      mFds.erase(it);
      return;
    }
  }

  uint32_t RegisterTimer(uint32_t intervalMs, TimerHandler handler, void* userData) override
  {
    if (!mHost || !handler)
      return 0;
    if (intervalMs == 0)
      intervalMs = 1;
    auto* bridge = new TimerBridge(handler, userData);
    if (mHost->registerTimer(bridge, intervalMs) != Steinberg::kResultOk)
    {
      bridge->release();
      return 0;
    }
    const uint32_t id = mNextId++;
    if (mNextId == 0)
      mNextId = 1;
    mTimers.push_back(TimerSlot{id, bridge});
    return id;
  }

  void UnregisterTimer(uint32_t timerId) override
  {
    for (auto it = mTimers.begin(); it != mTimers.end(); ++it)
    {
      if (it->id != timerId)
        continue;
      ReleaseBridge(it->bridge, true);
      mTimers.erase(it);
      return;
    }
  }

private:
  // Not an FUnknown itself: the timer and fd bridges already are, through
  // Steinberg::Linux::ITimerHandler / IEventHandler. A second FUnknown base
  // would make queryInterface ambiguous.
  struct BridgeBase
  {
    std::atomic<Steinberg::uint32> refs{1};
    bool firing = false;
    bool dead = false;
    virtual ~BridgeBase() = default;

    Steinberg::uint32 addRef() { return ++refs; }
    Steinberg::uint32 release()
    {
      const Steinberg::uint32 left = --refs;
      if (left == 0)
        delete this;
      return left;
    }
  };

  struct TimerBridge final : BridgeBase, public Steinberg::Linux::ITimerHandler
  {
    IPlugLinuxRunLoop::TimerHandler handler = nullptr;
    void* user = nullptr;

    TimerBridge(IPlugLinuxRunLoop::TimerHandler fn, void* userData)
    : handler(fn)
    , user(userData)
    {
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
    {
      if (!obj)
        return Steinberg::kInvalidArgument;
      QUERY_INTERFACE(iid, obj, Steinberg::Linux::ITimerHandler::iid, Steinberg::Linux::ITimerHandler)
      QUERY_INTERFACE(iid, obj, Steinberg::FUnknown::iid, Steinberg::FUnknown)
      *obj = nullptr;
      return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return BridgeBase::addRef(); }
    Steinberg::uint32 PLUGIN_API release() override { return BridgeBase::release(); }

    void PLUGIN_API onTimer() override
    {
      firing = true;
      if (handler)
        handler(user);
      firing = false;
      if (dead)
        BridgeBase::release();
    }
  };

  struct FdBridge final : BridgeBase, public Steinberg::Linux::IEventHandler
  {
    IPlugLinuxRunLoop::FdHandler handler = nullptr;
    void* user = nullptr;

    FdBridge(IPlugLinuxRunLoop::FdHandler fn, void* userData)
    : handler(fn)
    , user(userData)
    {
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
    {
      if (!obj)
        return Steinberg::kInvalidArgument;
      QUERY_INTERFACE(iid, obj, Steinberg::Linux::IEventHandler::iid, Steinberg::Linux::IEventHandler)
      QUERY_INTERFACE(iid, obj, Steinberg::FUnknown::iid, Steinberg::FUnknown)
      *obj = nullptr;
      return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return BridgeBase::addRef(); }
    Steinberg::uint32 PLUGIN_API release() override { return BridgeBase::release(); }

    void PLUGIN_API onFDIsSet(Steinberg::Linux::FileDescriptor fd) override
    {
      firing = true;
      if (handler)
        handler(static_cast<int>(fd), user);
      firing = false;
      if (dead)
        BridgeBase::release();
    }
  };

  struct TimerSlot
  {
    uint32_t id = 0;
    TimerBridge* bridge = nullptr;
  };
  struct FdSlot
  {
    int fd = -1;
    FdBridge* bridge = nullptr;
  };

  void ReleaseBridge(BridgeBase* bridge, bool timer)
  {
    if (!bridge || !mHost)
      return;
    if (timer)
      mHost->unregisterTimer(static_cast<TimerBridge*>(bridge));
    else
      mHost->unregisterEventHandler(static_cast<FdBridge*>(bridge));
    if (bridge->firing)
      bridge->dead = true;
    else
      bridge->release();
  }

  Steinberg::Linux::IRunLoop* mHost = nullptr;
  std::vector<TimerSlot> mTimers;
  std::vector<FdSlot> mFds;
  uint32_t mNextId = 1;
};

std::vector<Vst3RunLoop*>& Adapters()
{
  static std::vector<Vst3RunLoop*> adapters;
  return adapters;
}

Vst3RunLoop* AsVst3(IPlugLinuxRunLoop* adapter)
{
  for (auto* known : Adapters())
  {
    if (known == adapter)
      return known;
  }
  return nullptr;
}

Vst3RunLoop* FindOrCreate(Steinberg::FUnknown* context)
{
  if (!context)
    return nullptr;
  Steinberg::Linux::IRunLoop* host = nullptr;
  if (context->queryInterface(Steinberg::Linux::IRunLoop::iid, reinterpret_cast<void**>(&host)) != Steinberg::kResultOk || !host)
    return nullptr;

  for (auto* adapter : Adapters())
  {
    if (adapter->Host() == host)
    {
      host->release();
      return adapter;
    }
  }

  auto* created = new Vst3RunLoop(host);
  Adapters().push_back(created);
  return created;
}

struct InstanceHold
{
  void* instance = nullptr;
  Vst3RunLoop* adapter = nullptr;
};

std::vector<InstanceHold>& Instances()
{
  static std::vector<InstanceHold> holds;
  return holds;
}

Vst3RunLoop* gFactoryAdapter = nullptr;

} // namespace

void IPlugLinuxOnFactoryHostContext(Steinberg::FUnknown* context)
{
  if (gFactoryAdapter)
    return;
  gFactoryAdapter = FindOrCreate(context);
  if (gFactoryAdapter)
    IPlugLinuxRetainRunLoop(gFactoryAdapter);
}

void IPlugLinuxReleaseFactoryHostContext()
{
  if (!gFactoryAdapter)
    return;
  Vst3RunLoop* adapter = gFactoryAdapter;
  gFactoryAdapter = nullptr;
  if (IPlugLinuxReleaseRunLoop(adapter))
    IPlugLinuxDeleteVst3Adapter(adapter);
}

void IPlugLinuxAdoptComponentContext(void* instance, Steinberg::FUnknown* context)
{
  if (!instance)
    return;
  IPlugLinuxReleaseComponentContext(instance);
  Vst3RunLoop* adapter = FindOrCreate(context);
  if (!adapter)
    return;
  IPlugLinuxRetainRunLoop(adapter);
  Instances().push_back(InstanceHold{instance, adapter});
}

void IPlugLinuxReleaseComponentContext(void* instance)
{
  auto& holds = Instances();
  for (auto it = holds.begin(); it != holds.end(); ++it)
  {
    if (it->instance != instance)
      continue;
    Vst3RunLoop* adapter = it->adapter;
    holds.erase(it);
    if (adapter && IPlugLinuxReleaseRunLoop(adapter))
      IPlugLinuxDeleteVst3Adapter(adapter);
    return;
  }
}

IPlugLinuxRunLoop* IPlugLinuxVst3Adapter(Steinberg::FUnknown* context) { return FindOrCreate(context); }

void IPlugLinuxDeleteVst3Adapter(IPlugLinuxRunLoop* adapter)
{
  Vst3RunLoop* concrete = AsVst3(adapter);
  if (!concrete)
    return;
  auto& adapters = Adapters();
  for (auto it = adapters.begin(); it != adapters.end(); ++it)
  {
    if (*it != concrete)
      continue;
    adapters.erase(it);
    break;
  }
  if (gFactoryAdapter == concrete)
    gFactoryAdapter = nullptr;
  delete concrete;
}

} // namespace iplug

#endif
