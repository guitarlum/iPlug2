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

END_IPLUG_NAMESPACE
