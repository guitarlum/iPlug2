/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#include "Linux/IPlugLinuxRunLoop.h"

#include <vector>

namespace iplug
{

struct TimerBinding
{
  uint32_t id = 0;
  uint32_t intervalMs = 0;
  IPlugLinuxRunLoop::TimerHandler handler = nullptr;
  void* userData = nullptr;
  bool registered = false;
  uint32_t hostId = 0;
};

struct RunLoopState
{
  IPlugLinuxRunLoop* runLoop = nullptr;
  std::vector<TimerBinding> bindings;
  uint32_t nextId = 1;
};

RunLoopState& State()
{
  static RunLoopState state;
  return state;
}

void IPlugLinuxSetRunLoop(IPlugLinuxRunLoop* runLoop)
{
  RunLoopState& state = State();
  if (state.runLoop == runLoop)
    return;

  if (state.runLoop)
  {
    for (auto& binding : state.bindings)
    {
      if (!binding.registered)
        continue;
      state.runLoop->UnregisterTimer(binding.hostId);
      binding.registered = false;
    }
  }

  state.runLoop = runLoop;
  if (!state.runLoop)
    return;

  for (auto& binding : state.bindings)
  {
    if (!binding.handler)
      continue;
    binding.hostId = state.runLoop->RegisterTimer(binding.intervalMs, binding.handler, binding.userData);
    binding.registered = true;
  }
}

IPlugLinuxRunLoop* IPlugLinuxGetRunLoop() { return State().runLoop; }

uint32_t IPlugLinuxBindTimer(uint32_t intervalMs, IPlugLinuxRunLoop::TimerHandler handler, void* userData)
{
  RunLoopState& state = State();
  TimerBinding binding;
  binding.id = state.nextId++;
  if (state.nextId == 0)
    state.nextId = 1;
  binding.intervalMs = intervalMs;
  binding.handler = handler;
  binding.userData = userData;
  if (state.runLoop && handler)
  {
    binding.hostId = state.runLoop->RegisterTimer(intervalMs, handler, userData);
    binding.registered = true;
  }
  state.bindings.push_back(binding);
  return binding.id;
}

void IPlugLinuxUnbindTimer(uint32_t bindingId)
{
  if (!bindingId)
    return;

  RunLoopState& state = State();
  for (auto it = state.bindings.begin(); it != state.bindings.end(); ++it)
  {
    if (it->id != bindingId)
      continue;
    if (it->registered && state.runLoop)
      state.runLoop->UnregisterTimer(it->hostId);
    state.bindings.erase(it);
    return;
  }
}

} // namespace iplug
