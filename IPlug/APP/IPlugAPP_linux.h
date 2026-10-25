/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

/**
 * @file
 * Linux standalone glue around the SWELL-GDK windows: the X window an editor
 * embeds into, which RtAudio APIs can load, and the quit signals.
 *
 * The main window and the Preferences dialog are SWELL-GDK windows on GDK's X
 * connection. IGraphicsLinux opens its own connection and embeds a child of the
 * main window's X window, driven by the IPlugLinuxPollRunLoop the app pushes.
 */

#include <cstdint>
#include <cstdio>
#include <vector>

#include "IPlugPlatform.h"
#include "IPlugSWELL.h"
#include "VoLumAppLinuxPolicy.h"

BEGIN_IPLUG_NAMESPACE

/** True when the library behind driverType loads: always for ALSA (linked),
 * libjack.so.0 for JACK, libpulse-simple.so.0 for PulseAudio. These are the
 * sonames projects/linux/VoLumJackPulseDlopen.cpp loads. Probed once. */
bool IPlugAPPLinuxDriverLoads(uint32_t driverType);

/** VoLumLinuxDriverChoices for the libraries that load on this machine. */
std::vector<VoLumLinuxDriverChoice> IPlugAPPLinuxDriverChoices();

/** The X window id of a top-level SWELL window, after a round trip on GDK's
 * connection so another connection can parent to it. 0 when it has none yet. */
unsigned long IPlugAPPLinuxXWindow(HWND hwnd);

/** GDK's X connection fd, or -1 before SWELL_initargs. */
int IPlugAPPLinuxGdkConnectionFd();

/** SIGINT, SIGTERM and SIGHUP request a quit; see VoLumLinuxQuitStepFor. */
void IPlugAPPLinuxInstallQuitSignals();
/** Quit signals received so far. */
int IPlugAPPLinuxQuitSignals();

/** `VoLum --list-audio-devices`: every driver Preferences can offer with its
 * devices, the drivers whose library is missing, and the RtMidi ALSA inputs. */
void IPlugAPPLinuxPrintDevices(FILE* out);

/** Real-device gate only. plan is decided in IPlugAPPHost::Init, before the
 *  audio thread exists. phase, lastMono and seen belong to that thread.
 *  Writes a -24 dBFS 440 Hz sine, silence, or one zeroed buffer and returns
 *  true so the plug-in does not run. Off returns false and writes nothing. */
bool IPlugAPPLinuxGateTone(
  double* output, int channels, uint32_t frames, double sampleRate, unsigned int status, double streamTime, const VoLumLinuxGateTonePlan& plan, double& phase, double& lastMono, unsigned& seen);

/** Times one AudioCallback. enabled false does not read the clock. */
class IPlugAPPLinuxCallbackSpan
{
public:
  IPlugAPPLinuxCallbackSpan(bool enabled, uint32_t frames, double sampleRate);
  ~IPlugAPPLinuxCallbackSpan();
  IPlugAPPLinuxCallbackSpan(const IPlugAPPLinuxCallbackSpan&) = delete;
  IPlugAPPLinuxCallbackSpan& operator=(const IPlugAPPLinuxCallbackSpan&) = delete;

private:
  bool mOn = false;
  uint32_t mFrames = 0;
  uint32_t mRate = 0;
  uint64_t mStartNs = 0;
};

/** Drops samples from a previous stream. Main thread, before the audio thread exists. */
void IPlugAPPLinuxResetCallbackTiming();

/** Prints the timing log. Main thread, after the audio thread has stopped. */
void IPlugAPPLinuxDumpCallbackTiming(FILE* out);

END_IPLUG_NAMESPACE
