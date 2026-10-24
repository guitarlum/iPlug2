/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#include "IPlugAPP_linux.h"

#include <dlfcn.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "IPlugAPP_host.h"
#include "RtAudio.h"
#include "RtMidi.h"
#include "VoLumAppShutdown.h"

// Last: Xlib's None/Bool/Status macros must not reach the iPlug headers.
#include <gdk/gdk.h>
#include <gdk/gdkx.h>

BEGIN_IPLUG_NAMESPACE

namespace
{
bool LibraryLoads(const char* soname)
{
  // Kept loaded: the RtAudio shim dlopens the same soname when the API is used.
  return dlopen(soname, RTLD_LAZY | RTLD_LOCAL) != nullptr;
}

volatile sig_atomic_t sQuitSignals = 0;

void OnQuitSignal(int)
{
  sQuitSignals = sQuitSignals + 1;
  if (VoLumLinuxQuitStepFor(sQuitSignals, false) == VoLumLinuxQuitStep::ExitNow)
    _exit(kVoLumShutdownWatchdogExitCode);
}

RtAudio::Api RtAudioApiFor(uint32_t driverType)
{
  switch (driverType)
  {
  case kVoLumLinuxDriverJack:
    return RtAudio::UNIX_JACK;
  case kVoLumLinuxDriverPulse:
    return RtAudio::LINUX_PULSE;
  default:
    return RtAudio::LINUX_ALSA;
  }
}

void PrintApiDevices(FILE* out, const VoLumLinuxDriverChoice& choice)
{
  std::fprintf(out, "%s\n", choice.label);
  try
  {
    RtAudio dac(RtAudioApiFor(choice.driverType));
    const unsigned int count = dac.getDeviceCount();
    if (count == 0)
      std::fprintf(out, "  (no devices)\n");
    for (unsigned int i = 0; i < count; ++i)
    {
      const RtAudio::DeviceInfo info = dac.getDeviceInfo(i);
      if (!info.probed)
        continue;
      std::string rates;
      for (unsigned int sr : info.sampleRates)
        rates += (rates.empty() ? "" : ",") + std::to_string(sr);
      std::fprintf(out, "  %s: in %u, out %u, rates %s\n", info.name.c_str(), info.inputChannels, info.outputChannels, rates.c_str());
    }
  }
  catch (RtAudioError& e)
  {
    std::fprintf(out, "  (error: %s)\n", e.what());
  }
}
} // namespace

bool IPlugAPPLinuxDriverLoads(uint32_t driverType)
{
  static const bool jack = LibraryLoads("libjack.so.0") || LibraryLoads("libjack.so");
  static const bool pulse = LibraryLoads("libpulse-simple.so.0") || LibraryLoads("libpulse-simple.so");
  switch (driverType)
  {
  case kVoLumLinuxDriverAlsa:
    return true;
  case kVoLumLinuxDriverJack:
    return jack;
  case kVoLumLinuxDriverPulse:
    return pulse;
  default:
    return false;
  }
}

std::vector<VoLumLinuxDriverChoice> IPlugAPPLinuxDriverChoices() { return VoLumLinuxDriverChoices(IPlugAPPLinuxDriverLoads(kVoLumLinuxDriverJack), IPlugAPPLinuxDriverLoads(kVoLumLinuxDriverPulse)); }

unsigned long IPlugAPPLinuxXWindow(HWND hwnd)
{
  auto* pWindow = static_cast<GdkWindow*>(SWELL_GetOSWindow(hwnd, "GdkWindow"));
  if (!pWindow || !GDK_IS_X11_WINDOW(pWindow))
    return 0;
  // The editor creates its child on its own connection; the server has to
  // know this window by then.
  gdk_display_sync(gdk_window_get_display(pWindow));
  return GDK_WINDOW_XID(pWindow);
}

int IPlugAPPLinuxGdkConnectionFd()
{
  GdkDisplay* pDisplay = gdk_display_get_default();
  if (!pDisplay || !GDK_IS_X11_DISPLAY(pDisplay))
    return -1;
  return ConnectionNumber(gdk_x11_display_get_xdisplay(pDisplay));
}

void IPlugAPPLinuxInstallQuitSignals()
{
  struct sigaction action = {};
  action.sa_handler = OnQuitSignal;
  // Audio, MIDI and loader threads can take the signal too; their blocking
  // calls restart. The main loop's poll still wakes with EINTR.
  action.sa_flags = SA_RESTART;
  sigemptyset(&action.sa_mask);
  for (int sig : {SIGINT, SIGTERM, SIGHUP})
    sigaction(sig, &action, nullptr);
}

int IPlugAPPLinuxQuitSignals() { return sQuitSignals; }

void IPlugAPPLinuxPrintDevices(FILE* out)
{
  const auto choices = IPlugAPPLinuxDriverChoices();
  for (const auto& choice : choices)
    PrintApiDevices(out, choice);
  for (const auto& all : VoLumLinuxDriverChoices(true, true))
  {
    if (VoLumLinuxDriverComboIndex(choices, all.driverType) < 0)
      std::fprintf(out, "%s\n  (library not found)\n", all.label);
  }

  std::fprintf(out, "MIDI inputs (ALSA)\n");
  try
  {
    RtMidiIn midiIn(RtMidi::LINUX_ALSA);
    const unsigned int count = midiIn.getPortCount();
    if (count == 0)
      std::fprintf(out, "  (no ports)\n");
    for (unsigned int i = 0; i < count; ++i)
      std::fprintf(out, "  %s\n", midiIn.getPortName(i).c_str());
  }
  catch (RtMidiError& e)
  {
    std::fprintf(out, "  (unavailable: %s)\n", e.getMessage().c_str());
  }
}

bool IPlugAPPLinuxGateTone(double* output, int channels, uint32_t frames, double sampleRate, unsigned int status,
                           double streamTime)
{
  // Decided once. A normal launch pays one getenv, then this returns false
  // without touching the buffer.
  static int decided = 0;
  static int enabled = 0;
  static int mute = 0;
  static int drop = 0;
  static unsigned dropAt = 400;
  static unsigned seen = 0;
  static double phase = 0.0;
  static double lastMono = -1.0;
  if (!decided)
  {
    decided = 1;
    const char* env = std::getenv("VOLUM_LINUX_GATE_TONE");
    enabled = env && env[0] ? 1 : 0;
    if (enabled)
    {
      mute = std::strcmp(env, "mute") == 0;
      drop = std::strcmp(env, "drop") == 0;
      if (const char* at = std::getenv("VOLUM_LINUX_GATE_DROP_AT"))
      {
        const int n = std::atoi(at);
        if (n > 0)
          dropAt = static_cast<unsigned>(n);
      }
      std::fprintf(stderr, "gate-tone mode=%s amp_dbfs=-24\n", mute ? "mute" : (drop ? "drop" : "sine"));
      std::fflush(stderr);
    }
  }
  if (!enabled)
    return false;
  if (!output || channels <= 0 || frames == 0)
    return true;

  const double rate = sampleRate > 1.0 ? sampleRate : 48000.0;
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
  {
    const double now = static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
    // pa_simple_write blocks for about one period. A gap past 50 ms is a stall,
    // not that wait. Short dropouts are measured on the recording instead.
    if (lastMono >= 0.0 && now - lastMono > 0.050)
    {
      std::fprintf(stderr, "gate-xrun gap_s=%.4f stream_time=%.3f\n", now - lastMono, streamTime);
      std::fflush(stderr);
    }
    lastMono = now;
  }
  if (status != 0)
  {
    std::fprintf(stderr, "gate-xrun status=%u stream_time=%.3f\n", status, streamTime);
    std::fflush(stderr);
  }

  const bool hole = drop && seen == dropAt;
  if (hole)
  {
    std::fprintf(stderr, "gate-drop callback=%u\n", seen);
    std::fflush(stderr);
  }
  constexpr double kAmp = 0.063095734448; // -24 dBFS
  constexpr double kTwoPi = 6.28318530717958647692;
  const double step = kTwoPi * 440.0 / rate;
  for (uint32_t i = 0; i < frames; ++i)
  {
    const double s = (mute || hole) ? 0.0 : kAmp * std::sin(phase);
    phase += step;
    if (phase > kTwoPi)
      phase -= kTwoPi;
    for (int c = 0; c < channels; ++c)
      output[static_cast<size_t>(c) * frames + i] = s;
  }
  ++seen;
  return true;
}

END_IPLUG_NAMESPACE
