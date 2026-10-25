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

#include <atomic>
#include <cmath>
#include <cstdint>
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
                           double streamTime, const VoLumLinuxGateTonePlan& plan, double& phase, double& lastMono,
                           unsigned& seen)
{
  if (plan.mode == VoLumLinuxGateToneMode::Off)
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

  const bool hole = plan.mode == VoLumLinuxGateToneMode::Drop && seen == plan.dropAt;
  if (hole)
  {
    std::fprintf(stderr, "gate-drop callback=%u\n", seen);
    std::fflush(stderr);
  }
  constexpr double kAmp = 0.063095734448; // -24 dBFS
  constexpr double kTwoPi = 6.28318530717958647692;
  const double step = kTwoPi * 440.0 / rate;
  const bool silent = plan.mode == VoLumLinuxGateToneMode::Mute || hole;
  for (uint32_t i = 0; i < frames; ++i)
  {
    const double s = silent ? 0.0 : kAmp * std::sin(phase);
    phase += step;
    if (phase > kTwoPi)
      phase -= kTwoPi;
    for (int c = 0; c < channels; ++c)
      output[static_cast<size_t>(c) * frames + i] = s;
  }
  ++seen;
  return true;
}

namespace
{
struct CallbackSample
{
  uint64_t endNs = 0;
  uint32_t durUs = 0;
};

// About six minutes at 256 frames / 48 kHz. The smoke's window is inside this.
constexpr uint32_t kCallbackTimingCap = 65536;
CallbackSample gCallbackSamples[kCallbackTimingCap];
std::atomic<uint32_t> gCallbackWrite{0};
std::atomic<uint32_t> gCallbackMaxUs{0};
std::atomic<uint32_t> gCallbackFrames{0};
std::atomic<uint32_t> gCallbackRate{0};
std::atomic<uint32_t> gCallbackOverflow{0};

void RecordCallback(uint64_t endNs, uint32_t durUs, uint32_t frames, uint32_t rate)
{
  gCallbackFrames.store(frames, std::memory_order_relaxed);
  gCallbackRate.store(rate, std::memory_order_relaxed);
  uint32_t prev = gCallbackMaxUs.load(std::memory_order_relaxed);
  while (durUs > prev && !gCallbackMaxUs.compare_exchange_weak(prev, durUs, std::memory_order_relaxed))
  {
  }
  const uint32_t index = gCallbackWrite.fetch_add(1, std::memory_order_relaxed);
  if (index < kCallbackTimingCap)
  {
    gCallbackSamples[index].endNs = endNs;
    gCallbackSamples[index].durUs = durUs;
  }
  else
    gCallbackOverflow.store(1, std::memory_order_relaxed);
}
} // namespace

IPlugAPPLinuxCallbackSpan::IPlugAPPLinuxCallbackSpan(bool enabled, uint32_t frames, double sampleRate)
: mOn(enabled), mFrames(frames), mRate(sampleRate > 0.0 ? static_cast<uint32_t>(sampleRate) : 0)
{
  if (!mOn)
    return;
  timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
  {
    mOn = false;
    return;
  }
  mStartNs = static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
}

IPlugAPPLinuxCallbackSpan::~IPlugAPPLinuxCallbackSpan()
{
  if (!mOn)
    return;
  timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    return;
  const uint64_t endNs = static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
  const uint32_t durUs = static_cast<uint32_t>((endNs - mStartNs) / 1000ull);
  RecordCallback(endNs, durUs, mFrames, mRate);
}

void IPlugAPPLinuxResetCallbackTiming()
{
  gCallbackWrite.store(0, std::memory_order_relaxed);
  gCallbackMaxUs.store(0, std::memory_order_relaxed);
  gCallbackFrames.store(0, std::memory_order_relaxed);
  gCallbackRate.store(0, std::memory_order_relaxed);
  gCallbackOverflow.store(0, std::memory_order_relaxed);
}

void IPlugAPPLinuxDumpCallbackTiming(FILE* out)
{
  if (!out)
    return;
  // closeStream has joined the callback thread, so the relaxed stores are visible.
  const uint32_t count = gCallbackWrite.load(std::memory_order_relaxed);
  const uint32_t stored = count < kCallbackTimingCap ? count : kCallbackTimingCap;
  std::fprintf(out, "cb-timing enabled=1 count=%u max_us=%u frames=%u rate=%u overflow=%u\n", count,
               gCallbackMaxUs.load(std::memory_order_relaxed), gCallbackFrames.load(std::memory_order_relaxed),
               gCallbackRate.load(std::memory_order_relaxed), gCallbackOverflow.load(std::memory_order_relaxed));
  for (uint32_t i = 0; i < stored; ++i)
    std::fprintf(out, "cb-timing-row end_ns=%llu dur_us=%u\n",
                 static_cast<unsigned long long>(gCallbackSamples[i].endNs), gCallbackSamples[i].durUs);
  std::fflush(out);
}

END_IPLUG_NAMESPACE
