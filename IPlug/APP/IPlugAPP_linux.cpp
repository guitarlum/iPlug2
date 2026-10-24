/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#include "IPlugAPP_linux.h"

#include <dlfcn.h>
#include <signal.h>
#include <unistd.h>

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

END_IPLUG_NAMESPACE
