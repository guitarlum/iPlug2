/*
 ==============================================================================
 
 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers. 
 
 See LICENSE.txt for  more info.
 
 ==============================================================================
*/

#pragma once

/**
 * @file
 * VST3 MIDI parameter layout and value decoding. Kept free of the VST3 SDK so it can be unit tested.
 */

#include <atomic>
#include <cstdint>

#include "../IPlugConstants.h"
#include "../IPlugMidi.h"

BEGIN_IPLUG_NAMESPACE

/** Parameters per MIDI channel behind kMIDICCParamStartIdx: CC 0-127, aftertouch, pitch bend and
 * program change, i.e. Steinberg's kCountCtrlNumber + 1. */
static constexpr int kVST3MIDIParamsPerChannel = 131;

/** The most MIDI channels one VST3 event bus can carry. */
static constexpr int kVST3MaxMIDIChannels = 16;

/** First of the per-channel kIsProgramChange parameters. Hosts that route MIDI Program Change through
 * IUnitInfo (getUnitByBus, then the unit's program list) send it here, not through IMidiMapping.
 * Placed after the CC block of the maximum channel count, so it never moves with VST3_NUM_CC_CHANS. */
static constexpr int kVST3MIDIProgramParamStartIdx = kMIDICCParamStartIdx + kVST3MaxMIDIChannels * kVST3MIDIParamsPerChannel;

/** Programs in each per-channel program list. The parameter's stepCount is one less. */
static constexpr int kVST3MIDIProgramCount = 128;

/** @return the MIDI channel of a per-channel program-change parameter, or -1 if paramId is not one */
inline int VST3MIDIProgramParamChannel(int paramId, int nChannels)
{
  const int channel = paramId - kVST3MIDIProgramParamStartIdx;
  return (channel >= 0 && channel < nChannels) ? channel : -1;
}

/** Converts a normalized VST3 MIDI value (data byte / 127) back to the data byte, clamped to 0-127.
 * Hosts often compute the value in float, where 1/127 * 127 lands just below 1, so this rounds:
 * truncating would turn Program Change 1 into 0. */
inline int VST3NormalizedToMIDI7Bit(double value)
{
  if (!(value > 0.))
    return 0;
  if (value >= 1.)
    return 127;
  return static_cast<int>(value * 127. + 0.5);
}

/** Keeps a project reload from turning into a Program Change.
 * A host may set the program-change parameter while it restores a project: REAPER restores the last program,
 * Cubase sends program 0. The value is not in the plug-in state, so it cannot be compared with anything; a
 * change that arrives shortly after a state load, with the transport stopped and not rendering offline, is
 * taken as part of the restore. A Program Change from a playing MIDI item or an offline render always passes.
 * Armed on construction and by every setState; disarmed after kWindowSeconds of processed audio or at the
 * first block that plays or renders offline. */
class VST3ProgramRestoreGuard
{
public:
  static constexpr double kWindowSeconds = 0.5;

  /** UI thread: the host restored the plug-in state. */
  void Arm()
  {
    mElapsedSamples.store(0, std::memory_order_relaxed);
    mArmed.store(true, std::memory_order_release);
  }

  bool IsArmed() const { return mArmed.load(std::memory_order_acquire); }

  /** Audio thread: @return true if a program-change parameter value in this block belongs to a restore */
  bool Absorbs(bool transportRunning, bool offline) const
  {
    return IsArmed() && !transportRunning && !offline;
  }

  /** Audio thread: call once per processed block. */
  void EndBlock(int nFrames, double sampleRate, bool transportRunning, bool offline)
  {
    if (!IsArmed())
      return;

    const int64_t elapsed = mElapsedSamples.fetch_add(nFrames, std::memory_order_relaxed) + nFrames;
    if (transportRunning || offline || elapsed >= static_cast<int64_t>(sampleRate * kWindowSeconds))
      mArmed.store(false, std::memory_order_release);
  }

private:
  std::atomic<bool> mArmed{true};
  std::atomic<int64_t> mElapsedSamples{0};
};

enum class EVST3ProgramParamResult
{
  kNotProgramParam,
  kAbsorbed,
  kProgramChange
};

/** Decodes a change of a per-channel program-change parameter into msg. Outside the restore guard every
 * delivered value becomes a Program Change, the same program twice included. */
inline EVST3ProgramParamResult VST3ProgramParamToMidi(int paramId, double value, int offsetSamples, bool transportRunning,
                                                      bool offline, const VST3ProgramRestoreGuard& guard, IMidiMsg& msg)
{
  const int channel = VST3MIDIProgramParamChannel(paramId, kVST3MaxMIDIChannels);
  if (channel < 0)
    return EVST3ProgramParamResult::kNotProgramParam;
  if (guard.Absorbs(transportRunning, offline))
    return EVST3ProgramParamResult::kAbsorbed;
  msg.MakeProgramChange(VST3NormalizedToMIDI7Bit(value), channel, offsetSamples);
  return EVST3ProgramParamResult::kProgramChange;
}

END_IPLUG_NAMESPACE
