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

#include "../IPlugConstants.h"

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

END_IPLUG_NAMESPACE
