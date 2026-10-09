/*
 ==============================================================================
 
 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers. 
 
 See LICENSE.txt for  more info.
 
 ==============================================================================
*/

#pragma once

/**
 * @file
 * The VST3 processor's parameter-change pass: plug-in parameters go to a callback, the VST3 MIDI parameters
 * (per-channel CC block and program lists) become MIDI messages. Needs only the VST3 SDK interfaces, so it can
 * be unit tested with real ProcessData.
 */

#include <atomic>
#include <chrono>
#include <cstdint>

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include "../IPlugMidi.h"
#include "IPlugVST3_MidiParams.h"

BEGIN_IPLUG_NAMESPACE

/** Routes one block's parameter changes and keeps a project reload from turning into a Program Change.
 *
 * A host may set a program-change parameter while it restores a project: REAPER restores the last program,
 * Cubase sends program 0. The value is not in the plug-in state, so it cannot be compared with anything.
 * The restore guard is armed on construction and by every setState (Arm). While armed, a Program Change
 * arriving in a block whose transport is stopped and which is not rendered offline is absorbed. A block
 * without a ProcessContext counts as stopped. The guard disarms at the first playing or offline block,
 * after kWindowSeconds of processed audio, or kWindowSeconds of wall-clock time after the first block it
 * sees, so zero-frame parameter flushes or suspended processing cannot keep it armed. Outside the guard
 * every delivered value becomes a Program Change, the same program twice included.
 *
 * Arm may run on any thread while a block is processed. The guard state is one atomic word carrying a
 * generation, and a block only disarms the generation it started with, so a re-arm during a block survives
 * that block. The audio thread never waits: one load per decision and at most one compare-exchange per block.
 */
class VST3MidiParamRouter
{
public:
  static constexpr double kWindowSeconds = 0.5;

  enum class ERoute
  {
    kNotMidiParam,
    kAbsorbed,
    kMidi
  };

  static int64_t NowNs()
  {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }

  /** Any thread: the host restored the plug-in state. */
  void Arm()
  {
    uint64_t state = mState.load(std::memory_order_relaxed);
    while (!mState.compare_exchange_weak(state, Pack(Generation(state) + 1, 0, true), std::memory_order_acq_rel, std::memory_order_relaxed))
    {
    }
  }

  bool IsArmed() const { return IsArmed(mState.load(std::memory_order_acquire)); }

  /** Audio thread: one block's parameter changes. Plug-in parameters go to onParam(id, value, offsetSamples),
   * MIDI messages to onMidi(msg). Uses the last point of each queue, as iPlug applies parameters per block. */
  template <class FParam, class FMidi>
  void ProcessParameterChanges(const Steinberg::Vst::ProcessData& data, bool renderingOffline, int64_t nowNs, FParam&& onParam, FMidi&& onMidi)
  {
    BeginBlock(data, renderingOffline, nowNs);

    Steinberg::Vst::IParameterChanges* paramChanges = data.inputParameterChanges;
    if (!paramChanges)
      return;

    const Steinberg::int32 numParamsChanged = paramChanges->getParameterCount();
    for (Steinberg::int32 i = 0; i < numParamsChanged; i++)
    {
      Steinberg::Vst::IParamValueQueue* paramQueue = paramChanges->getParameterData(i);
      if (!paramQueue)
        continue;

      Steinberg::int32 offsetSamples;
      Steinberg::Vst::ParamValue value;
      if (paramQueue->getPoint(paramQueue->getPointCount() - 1, offsetSamples, value) != Steinberg::kResultTrue)
        continue;

      const int idx = static_cast<int>(paramQueue->getParameterId());
      IMidiMsg msg;
      switch (Route(idx, value, offsetSamples, msg))
      {
        case ERoute::kNotMidiParam: onParam(idx, value, offsetSamples); break;
        case ERoute::kMidi: onMidi(msg); break;
        case ERoute::kAbsorbed: break;
      }
    }
  }

  /** Audio thread: call after the block's audio. */
  void EndBlock(const Steinberg::Vst::ProcessData& data, double sampleRate)
  {
    uint64_t state = mState.load(std::memory_order_acquire);
    if (!IsArmed(state) || Generation(state) != mBlockGeneration)
      return;

    const uint64_t frames = Frames(state) + static_cast<uint64_t>(data.numSamples > 0 ? data.numSamples : 0);
    const bool expired = mBlockPlaying || mBlockOffline || static_cast<double>(frames) >= sampleRate * kWindowSeconds;
    mState.compare_exchange_strong(state, Pack(mBlockGeneration, frames, !expired), std::memory_order_acq_rel, std::memory_order_relaxed);
  }

  /** Audio thread: the MIDI message a VST3 MIDI parameter value stands for in the current block. */
  ERoute Route(int paramId, double value, int offsetSamples, IMidiMsg& msg) const
  {
    const int programChannel = VST3MIDIProgramParamChannel(paramId, kVST3MaxMIDIChannels);
    if (programChannel >= 0)
    {
      if (AbsorbsProgramChange())
        return ERoute::kAbsorbed;
      msg.MakeProgramChange(VST3NormalizedToMIDI7Bit(value), programChannel, offsetSamples);
      return ERoute::kMidi;
    }

    if (paramId < kMIDICCParamStartIdx || paramId >= kVST3MIDIProgramParamStartIdx)
      return ERoute::kNotMidiParam;

    const int index = paramId - kMIDICCParamStartIdx;
    const int channel = index / kVST3MIDIParamsPerChannel;
    const int ctrlr = index % kVST3MIDIParamsPerChannel;

    if (ctrlr == Steinberg::Vst::kAfterTouch)
      msg.MakeChannelATMsg(VST3NormalizedToMIDI7Bit(value), offsetSamples, channel);
    else if (ctrlr == Steinberg::Vst::kPitchBend)
      msg.MakePitchWheelMsg((value * 2.) - 1., channel, offsetSamples);
    else if (ctrlr == Steinberg::Vst::kCtrlProgramChange)
    {
      if (AbsorbsProgramChange())
        return ERoute::kAbsorbed;
      msg.MakeProgramChange(VST3NormalizedToMIDI7Bit(value), channel, offsetSamples);
    }
    else
      msg = IMidiMsg(offsetSamples, static_cast<uint8_t>((IMidiMsg::kControlChange << 4) | channel), static_cast<uint8_t>(ctrlr), static_cast<uint8_t>(VST3NormalizedToMIDI7Bit(value)));
    return ERoute::kMidi;
  }

private:
  // Bit 0: armed. Bits 1-31: frames processed since Arm. Bits 32-63: generation, bumped by Arm.
  static constexpr uint64_t kArmedBit = 1;
  static constexpr uint64_t kMaxFrames = (uint64_t(1) << 31) - 1;

  static uint64_t Pack(uint64_t generation, uint64_t frames, bool armed)
  {
    return ((generation & 0xFFFFFFFFu) << 32) | ((frames < kMaxFrames ? frames : kMaxFrames) << 1) | (armed ? kArmedBit : 0);
  }
  static uint64_t Generation(uint64_t state) { return state >> 32; }
  static uint64_t Frames(uint64_t state) { return (state >> 1) & kMaxFrames; }
  static bool IsArmed(uint64_t state) { return (state & kArmedBit) != 0; }

  void BeginBlock(const Steinberg::Vst::ProcessData& data, bool renderingOffline, int64_t nowNs)
  {
    mBlockPlaying = data.processContext && (data.processContext->state & Steinberg::Vst::ProcessContext::kPlaying);
    mBlockOffline = renderingOffline || data.processMode == Steinberg::Vst::kOffline;

    uint64_t state = mState.load(std::memory_order_acquire);
    mBlockGeneration = Generation(state);
    if (!IsArmed(state))
      return;

    if (!mHasFirstBlock || mFirstBlockGeneration != mBlockGeneration)
    {
      mHasFirstBlock = true;
      mFirstBlockGeneration = mBlockGeneration;
      mFirstBlockNs = nowNs;
    }
    else if (static_cast<double>(nowNs - mFirstBlockNs) >= kWindowSeconds * 1e9)
    {
      mState.compare_exchange_strong(state, Pack(mBlockGeneration, Frames(state), false), std::memory_order_acq_rel, std::memory_order_relaxed);
    }
  }

  bool AbsorbsProgramChange() const { return IsArmed() && !mBlockPlaying && !mBlockOffline; }

  static_assert(std::atomic<uint64_t>::is_always_lock_free, "the restore guard must stay lock-free on the audio thread");
  std::atomic<uint64_t> mState{Pack(0, 0, true)};

  // Audio thread only.
  bool mBlockPlaying = false;
  bool mBlockOffline = false;
  uint64_t mBlockGeneration = 0;
  bool mHasFirstBlock = false;
  uint64_t mFirstBlockGeneration = 0;
  int64_t mFirstBlockNs = 0;
};

END_IPLUG_NAMESPACE
