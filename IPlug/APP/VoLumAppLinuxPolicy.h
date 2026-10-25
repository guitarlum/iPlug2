#pragma once

// VoLumAppLinuxPolicy.h: the Linux standalone's decisions that need no X, GDK
// or audio library, kept apart so tests/test_iplug_app_linux_policy.cpp can pin
// them on every platform. IPlugAPP_linux.cpp, IPlugAPP_host.cpp and
// IPlugAPP_dialog.cpp call them inside OS_LINUX.

#include <cstdint>
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

} // namespace iplug
