#pragma once

// VoLumAppLinuxPolicy.h: the Linux standalone's decisions that need no X, GDK
// or audio library, kept apart so tests/test_iplug_app_linux_policy.cpp can pin
// them on every platform. IPlugAPP_linux.cpp, IPlugAPP_host.cpp and
// IPlugAPP_dialog.cpp call them inside OS_LINUX.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace iplug
{

// The driver types IPlugAPP_host.h numbers on Linux (kDeviceAlsa, kDeviceJack,
// kDevicePulse). settings.ini stores these numbers, so they never move.
inline constexpr uint32_t kVoLumLinuxDriverAlsa = 0;
inline constexpr uint32_t kVoLumLinuxDriverJack = 1;
inline constexpr uint32_t kVoLumLinuxDriverPulse = 2;

struct VoLumLinuxDriverChoice
{
  uint32_t driverType;
  const char* label;
};

/** The Preferences driver list. ALSA is linked and always offered; JACK and
 * PulseAudio are loaded at runtime, so a missing library removes only that entry. */
inline std::vector<VoLumLinuxDriverChoice> VoLumLinuxDriverChoices(bool jackLoads, bool pulseLoads)
{
  std::vector<VoLumLinuxDriverChoice> choices{{kVoLumLinuxDriverAlsa, "ALSA"}};
  if (jackLoads)
    choices.push_back({kVoLumLinuxDriverJack, "JACK"});
  if (pulseLoads)
    choices.push_back({kVoLumLinuxDriverPulse, "PulseAudio"});
  return choices;
}

/** Combo position of driverType, or -1 when it is not offered. The position is
 * not the driver type once an entry is missing. */
inline int VoLumLinuxDriverComboIndex(const std::vector<VoLumLinuxDriverChoice>& choices, uint32_t driverType)
{
  for (std::size_t i = 0; i < choices.size(); ++i)
    if (choices[i].driverType == driverType)
      return static_cast<int>(i);
  return -1;
}

/** The driver to open at startup: the stored one while its library loads,
 * otherwise ALSA. Without this a settings.ini naming JACK on a machine without
 * libjack opens an API with no devices and starts silent. */
inline uint32_t VoLumLinuxStartupDriver(uint32_t storedDriverType, bool jackLoads, bool pulseLoads)
{
  const auto choices = VoLumLinuxDriverChoices(jackLoads, pulseLoads);
  return VoLumLinuxDriverComboIndex(choices, storedDriverType) >= 0 ? storedDriverType : kVoLumLinuxDriverAlsa;
}

/** The one XDG data directory. VoLumPaths.h LinuxVoLumDataDir calls this, and
 * the standalone writes settings.ini here, so the app and the library cannot
 * drift. $XDG_DATA_HOME/<bundle>/ when XDG_DATA_HOME is absolute (the XDG spec
 * says a relative value is invalid and is ignored), else
 * $HOME/.local/share/<bundle>/. Empty when neither is usable. The string ends
 * with a slash. */
inline std::string VoLumLinuxAppDataDir(const char* xdgDataHome, const char* home, const char* bundleName)
{
  if (xdgDataHome && xdgDataHome[0] == '/')
    return std::string(xdgDataHome) + "/" + bundleName + "/";
  if (home && home[0])
    return std::string(home) + "/.local/share/" + bundleName + "/";
  return {};
}

enum class VoLumLinuxQuitStep
{
  None,
  /** A modal dialog (Preferences, a message box) is open: cancel it first, so
   * the main window is not destroyed underneath the dialog's own loop. */
  CancelModal,
  /** Close the main window the way its close button does, which writes settings. */
  CloseMainWindow,
  /** A second SIGINT/SIGTERM/SIGHUP while the first is still being handled:
   * leave at once with the watchdog's exit code. */
  ExitNow
};

/** What the main loop (and the signal handler, for ExitNow) does after
 * signalsReceived quit signals. */
inline VoLumLinuxQuitStep VoLumLinuxQuitStepFor(int signalsReceived, bool modalOpen)
{
  if (signalsReceived <= 0)
    return VoLumLinuxQuitStep::None;
  if (signalsReceived >= 2)
    return VoLumLinuxQuitStep::ExitNow;
  return modalOpen ? VoLumLinuxQuitStep::CancelModal : VoLumLinuxQuitStep::CloseMainWindow;
}

/** What VOLUM_LINUX_GATE_TONE asks the standalone to play instead of the amp.
 * Read once on the main thread before the audio stream exists. */
enum class VoLumLinuxGateToneMode : uint8_t
{
  Off,
  Sine,
  Mute,
  Drop
};

struct VoLumLinuxGateTonePlan
{
  VoLumLinuxGateToneMode mode = VoLumLinuxGateToneMode::Off;
  /** Callback index of the one zeroed buffer when mode is Drop. */
  unsigned dropAt = 400;
};

/** tone is VOLUM_LINUX_GATE_TONE, dropAt is VOLUM_LINUX_GATE_DROP_AT. Empty or
 * missing tone is off. Anything other than mute or drop is the sine. A drop
 * index that is not a positive integer keeps 400. */
inline VoLumLinuxGateTonePlan VoLumLinuxReadGateTone(const char* tone, const char* dropAt)
{
  VoLumLinuxGateTonePlan plan;
  if (!tone || !tone[0])
    return plan;
  if (std::strcmp(tone, "mute") == 0)
    plan.mode = VoLumLinuxGateToneMode::Mute;
  else if (std::strcmp(tone, "drop") == 0)
    plan.mode = VoLumLinuxGateToneMode::Drop;
  else
    plan.mode = VoLumLinuxGateToneMode::Sine;
  if (!dropAt || !dropAt[0])
    return plan;
  unsigned n = 0;
  for (const char* p = dropAt; *p; ++p)
  {
    if (*p < '0' || *p > '9')
      return plan;
    n = n * 10u + static_cast<unsigned>(*p - '0');
  }
  if (n > 0)
    plan.dropAt = n;
  return plan;
}

/** VOLUM_CALLBACK_TIMING. Missing, empty or "0" is off. Only "1" is on, so a
 * typo does not time the callback. Read once in Init, before the audio thread. */
inline bool VoLumLinuxCallbackTimingEnabled(const char* env) { return env != nullptr && std::strcmp(env, "1") == 0; }

/** Callback period in microseconds, rounded up. 256 frames at 48000 Hz is
 * 5333.333 us, so the period is 5334 us and a callback of that length has not
 * exceeded it. */
inline uint32_t VoLumLinuxCallbackPeriodUs(uint32_t frames, uint32_t sampleRate)
{
  if (frames == 0 || sampleRate == 0)
    return 0;
  return static_cast<uint32_t>((static_cast<uint64_t>(frames) * 1000000ull + sampleRate - 1ull) / sampleRate);
}

/** Thousandths of the period. 800 is 80 percent. Box windows peaked at 3161 us
 * (59 percent of 5334) and the GitHub runner at 3905 us (73 percent), so 70
 * percent (3734 us) fails CI. 80 percent is 4267 us. See reports/18-callback-budget.md. */
inline constexpr uint32_t kVoLumLinuxCallbackBudgetThousandths = 800;

inline uint32_t VoLumLinuxCallbackBudgetUs(uint32_t frames, uint32_t sampleRate)
{
  return static_cast<uint32_t>((static_cast<uint64_t>(VoLumLinuxCallbackPeriodUs(frames, sampleRate)) *
                                kVoLumLinuxCallbackBudgetThousandths) /
                               1000ull);
}

enum class VoLumLinuxCallbackVerdict
{
  /** No jackd xrun, and every callback in the window was inside the budget and the period. */
  Pass,
  /** jackd reported an xrun and every callback stayed inside the budget. The box
   * prints this and does not fail. CI (--strict-xruns) does not use this verdict. */
  DriverXrun,
  /** A callback exceeded the period or the budget. Fails on the box and in CI. */
  FailCallback,
  /** CI only: a jackd xrun while callbacks stayed inside the budget. */
  FailStrictJack,
  /** The timing log or the window bounds were missing. Fails closed. */
  FailMissing
};

/** strict is the CI switch (smoke --strict-xruns). The box passes false.
 * jackdXruns is the count in the measured window. maxCallbackUs is the slowest
 * callback whose end time fell in that window. */
inline VoLumLinuxCallbackVerdict VoLumLinuxClassifyCallbackWindow(uint32_t maxCallbackUs, uint32_t frames, uint32_t sampleRate, int jackdXruns, bool strict, bool timingComplete)
{
  if (!timingComplete || frames == 0 || sampleRate == 0)
    return VoLumLinuxCallbackVerdict::FailMissing;
  const uint32_t period = VoLumLinuxCallbackPeriodUs(frames, sampleRate);
  const uint32_t budget = VoLumLinuxCallbackBudgetUs(frames, sampleRate);
  if (maxCallbackUs > period || maxCallbackUs > budget)
    return VoLumLinuxCallbackVerdict::FailCallback;
  if (jackdXruns > 0)
    return strict ? VoLumLinuxCallbackVerdict::FailStrictJack : VoLumLinuxCallbackVerdict::DriverXrun;
  return VoLumLinuxCallbackVerdict::Pass;
}

} // namespace iplug
