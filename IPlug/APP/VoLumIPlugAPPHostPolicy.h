#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace iplug
{

inline std::string VoLumWinMMMidiPortBaseName(const std::string& name)
{
  const auto space = name.find_last_of(' ');
  if (space == std::string::npos || space + 1 == name.size())
    return name;

  for (std::size_t i = space + 1; i < name.size(); ++i)
    if (!std::isdigit(static_cast<unsigned char>(name[i])))
      return name;

  return name.substr(0, space);
}

inline std::vector<std::string> VoLumStableMidiPortNames(const std::vector<std::string>& rawNames, bool winMM)
{
  if (!winMM)
    return rawNames;

  std::vector<std::string> bases;
  bases.reserve(rawNames.size());
  for (const auto& name : rawNames)
    bases.push_back(VoLumWinMMMidiPortBaseName(name));

  std::vector<std::string> stable;
  stable.reserve(rawNames.size());
  for (std::size_t i = 0; i < bases.size(); ++i)
  {
    const auto duplicates = static_cast<int>(std::count(bases.begin(), bases.end(), bases[i]));
    if (duplicates == 1)
    {
      stable.push_back(bases[i]);
      continue;
    }

    const auto ordinal = static_cast<int>(std::count(bases.begin(), bases.begin() + i, bases[i])) + 1;
    stable.push_back(bases[i] + " [" + std::to_string(ordinal) + "]");
  }
  return stable;
}

inline int VoLumResolveMidiPort(const std::string& savedName, const std::vector<std::string>& stableNames, bool winMM, bool stableNameVersion = false)
{
  if (!winMM || stableNameVersion)
  {
    const auto exact = std::find(stableNames.begin(), stableNames.end(), savedName);
    if (exact != stableNames.end())
      return static_cast<int>(std::distance(stableNames.begin(), exact));
    return -1;
  }

  // Migration from RtMidi's former "<device name> <volatile global index>"
  // spelling is safe only when the base name is unique. Duplicate devices become
  // stable once selected with the "[ordinal]" spelling; guessing an old duplicate
  // would silently connect the wrong controller.
  const std::string legacyBase = VoLumWinMMMidiPortBaseName(savedName);
  int match = -1;
  for (std::size_t i = 0; i < stableNames.size(); ++i)
  {
    std::string stableBase = stableNames[i];
    const auto ordinal = stableBase.rfind(" [");
    if (ordinal != std::string::npos && stableBase.back() == ']')
      stableBase.resize(ordinal);
    if (stableBase != legacyBase)
      continue;
    if (match != -1)
      return -1;
    match = static_cast<int>(i);
  }
  return match;
}

inline bool VoLumStableMidiNameHasOrdinal(const std::string& name)
{
  const auto ordinal = name.rfind(" [");
  if (ordinal == std::string::npos || name.back() != ']')
    return false;

  if (ordinal + 2 == name.size() - 1)
    return false;

  for (std::size_t i = ordinal + 2; i + 1 < name.size(); ++i)
    if (!std::isdigit(static_cast<unsigned char>(name[i])))
      return false;
  return true;
}

inline std::string VoLumLegacyMidiNameForStable(const std::string& stableName, const std::vector<std::string>& stableNames, const std::vector<std::string>& legacyNames, const std::string& fallback)
{
  if (stableName == "off")
    return stableName;

  const auto stable = std::find(stableNames.begin(), stableNames.end(), stableName);
  if (stable == stableNames.end())
    return fallback;

  const auto index = static_cast<std::size_t>(std::distance(stableNames.begin(), stable));
  if (VoLumStableMidiNameHasOrdinal(stableName) || index >= legacyNames.size())
    return "off";
  return legacyNames[index];
}

struct VoLumStoredMidiNamePlan
{
  std::string selectedName;
  bool nameIsStable = false;
  bool changed = false;
};

inline VoLumStoredMidiNamePlan VoLumReconcileStoredMidiName(
  const std::string& legacyName, const std::string& stableName, const std::vector<std::string>& stableNames, const std::vector<std::string>& legacyNames, bool winMM)
{
  if (stableName.empty())
    return {legacyName, false, false};

  if (stableName == "off")
    return legacyName == "off" ? VoLumStoredMidiNamePlan{stableName, true, false} : VoLumStoredMidiNamePlan{legacyName, false, true};

  const auto stable = std::find(stableNames.begin(), stableNames.end(), stableName);
  if (stable == stableNames.end())
    return {stableName, true, false};

  const std::string expectedLegacy = VoLumLegacyMidiNameForStable(stableName, stableNames, legacyNames, legacyName);
  if (legacyName == expectedLegacy)
    return {stableName, true, false};

  if (legacyName == "off")
    return {legacyName, true, true};

  const auto exactLegacy = std::find(legacyNames.begin(), legacyNames.end(), legacyName);
  if (exactLegacy != legacyNames.end())
  {
    const auto index = static_cast<std::size_t>(std::distance(legacyNames.begin(), exactLegacy));
    if (index < stableNames.size())
      return {stableNames[index], true, true};
  }

  const int migrated = VoLumResolveMidiPort(legacyName, stableNames, winMM, false);
  if (migrated >= 0)
    return {stableNames[static_cast<std::size_t>(migrated)], true, true};

  return {stableName, true, false};
}

inline bool VoLumStandaloneAcceptsMidiStatus(uint8_t status)
{
  const uint8_t kind = status & 0xF0;
  return kind == 0xB0 || kind == 0xC0;
}

struct VoLumDialogAudioPlan
{
  bool restartOnOK = false;
  bool restartOnCancel = false;
};

inline VoLumDialogAudioPlan VoLumPlanDialogAudio(bool activeEqualsCurrent, bool tempEqualsCurrent, bool activeEqualsTemp, bool appliedThisDialog)
{
  return {!activeEqualsCurrent && (!tempEqualsCurrent || appliedThisDialog), !activeEqualsTemp || !tempEqualsCurrent};
}

struct VoLumProbeChannelPlan
{
  uint32_t runtimeChannel = 1;
  uint32_t savedChannel = 1;
  bool corrected = false;
};

inline VoLumProbeChannelPlan VoLumPlanProbeChannel(uint32_t savedChannel, int availableChannels)
{
  if (availableChannels <= 0)
    return {1, savedChannel, false};

  const uint32_t channel = std::clamp(savedChannel, 1u, static_cast<uint32_t>(availableChannels));
  return {channel, channel, channel != savedChannel};
}

struct VoLumFailureRestorePlan
{
  bool restoreActiveState = false;
  bool persistActiveState = false;
  bool restoreSavedFallbackRequest = false;
};

inline VoLumFailureRestorePlan VoLumPlanFailureRestore(bool haveWorkingState, bool stateEqualsActive, bool activeIsRuntimeFallback)
{
  const bool restore = haveWorkingState && !stateEqualsActive;
  return {restore, restore && !activeIsRuntimeFallback, restore && activeIsRuntimeFallback};
}

inline uint32_t VoLumClampStoredBufferSize(int storedSize) { return static_cast<uint32_t>(std::clamp(storedSize, 48, 8192)); }

struct VoLumStereoRoute
{
  double left = 0.0;
  double right = 0.0;
  bool shared = false;
};

inline VoLumStereoRoute VoLumRouteStereoSample(double left, double right, bool sameDestination)
{
  if (sameDestination)
    return {(left + right) * 0.5, 0.0, true};
  return {left, right, false};
}

// The notice shown when the device opened at a rate other than the one asked for.
// Only an ASIO driver has its own control panel to change the clock in; pointing a
// DirectSound user at "Device Settings" sent them to a button that is disabled for
// that driver, and left the real reason (the driver does not offer that rate) unsaid.
inline std::string VoLumSampleRateNoticeText(bool asioDriver, uint32_t requested, uint32_t actual, const char* appName)
{
  const std::string app = appName ? appName : "";
  if (asioDriver)
    return "The audio driver did not accept " + std::to_string(requested) + " Hz.\n\nIt opened the device at "
           + std::to_string(actual) + " Hz instead, and " + app
           + " is now running at that rate. Sample rates on ASIO devices are usually set in the driver's own control "
             "panel - try Device Settings if you need a different one.";
  return "The audio device does not offer " + std::to_string(requested) + " Hz in this driver.\n\n" + app + " opened it at " + std::to_string(actual) + " Hz instead and is now running at that rate.";
}

inline std::vector<uint32_t> VoLumBufferSizeChoices(uint32_t activeSize)
{
  static constexpr uint32_t kStandard[] = {48, 64, 96, 128, 256, 512, 1024, 2048, 4096, 8192};
  std::vector<uint32_t> choices(std::begin(kStandard), std::end(kStandard));
  if (activeSize > 0 && std::find(choices.begin(), choices.end(), activeSize) == choices.end())
  {
    choices.push_back(activeSize);
    std::sort(choices.begin(), choices.end());
  }
  return choices;
}

} // namespace iplug
