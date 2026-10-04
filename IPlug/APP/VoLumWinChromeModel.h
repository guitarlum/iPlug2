#pragma once

// VoLum: the decisions behind the standalone's dark window chrome on Windows, kept
// free of <windows.h> so test_volum_win_chrome.cpp can check them without a window.
//
// - Which DWM attribute turns a title bar dark depends on the Windows build: 1809
//   shipped it as an undocumented 19, 20H1 renumbered it to the documented
//   DWMWA_USE_IMMERSIVE_DARK_MODE (20), and anything older has no dark title bar.
// - Only Windows 11 lets an app paint the caption in an exact colour.
// - GDI has no alpha, so VoLum's translucent brass frames are pre-blended over the
//   panel colour they sit on.

#include <cstdint>

namespace iplug
{
inline constexpr uint32_t kVoLumDwmDarkModeAttrLegacy = 19;
inline constexpr uint32_t kVoLumDwmDarkModeAttr = 20;
inline constexpr uint32_t kVoLumDwmCaptionColorAttr = 35;
inline constexpr uint32_t kVoLumDwmTextColorAttr = 36;

inline constexpr uint32_t kVoLumFirstLegacyDarkBuild = 17763; // Windows 10 1809
inline constexpr uint32_t kVoLumFirstDarkBuild = 18985; // Windows 10 20H1 preview
inline constexpr uint32_t kVoLumFirstCaptionColorBuild = 22000; // Windows 11

/** The DWM attribute that darkens the title bar on `build`, or 0 when there is none. */
inline constexpr uint32_t VoLumDarkCaptionAttribute(uint32_t build)
{
  if (build >= kVoLumFirstDarkBuild)
    return kVoLumDwmDarkModeAttr;
  if (build >= kVoLumFirstLegacyDarkBuild)
    return kVoLumDwmDarkModeAttrLegacy;
  return 0;
}

inline constexpr bool VoLumSupportsCaptionColor(uint32_t build)
{
  return build >= kVoLumFirstCaptionColorBuild;
}

struct VoLumRgb
{
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

inline constexpr bool operator==(VoLumRgb a, VoLumRgb b)
{
  return a.r == b.r && a.g == b.g && a.b == b.b;
}

/** COLORREF layout: 0x00BBGGRR. */
inline constexpr uint32_t VoLumColorRef(VoLumRgb c)
{
  return static_cast<uint32_t>(c.r) | (static_cast<uint32_t>(c.g) << 8) | (static_cast<uint32_t>(c.b) << 16);
}

/** `fg` at `alpha` (0..255) over an opaque `bg`, rounded like a compositor would. */
inline constexpr VoLumRgb VoLumBlend(VoLumRgb fg, uint8_t alpha, VoLumRgb bg)
{
  auto mix = [](int f, int b, int a) { return static_cast<uint8_t>((f * a + b * (255 - a) + 127) / 255); };
  return {mix(fg.r, bg.r, alpha), mix(fg.g, bg.g, alpha), mix(fg.b, bg.b, alpha)};
}

// The VoLumColors entries the chrome draws with (VoLumColorHelpers.h). The test pins
// each one to its source so a palette change cannot leave the dialog behind.
namespace volum_chrome
{
inline constexpr VoLumRgb kBg{17, 17, 24}; // VoLumColors::BG
inline constexpr VoLumRgb kPanel{23, 23, 31}; // VoLumColors::PANEL_TOP
inline constexpr VoLumRgb kWell{9, 9, 14}; // VoLumColors::WELL_DARK
inline constexpr VoLumRgb kTextDim{232, 218, 200}; // VoLumColors::TEXT_DIM
inline constexpr VoLumRgb kTextMed{245, 232, 218}; // VoLumColors::TEXT_MED
inline constexpr VoLumRgb kTextBright{255, 248, 238}; // VoLumColors::TEXT_BRIGHT
inline constexpr VoLumRgb kGold{252, 222, 145}; // VoLumColors::GOLD
inline constexpr VoLumRgb kBrass{200, 162, 78}; // VoLumColors::CORNER
inline constexpr VoLumRgb kSelBorder{226, 156, 112}; // VoLumColors::SEL_BORDER

inline constexpr uint8_t kFrameAlpha = 72; // VoLumColors::FRAME
inline constexpr uint8_t kSelBgAlpha = 40; // VoLumColors::SEL_BG
inline constexpr uint8_t kHoverAlpha = 20; // VoLumColors::SEL_BG_SOFT
inline constexpr uint8_t kDisabledTextAlpha = 110;

inline constexpr VoLumRgb kFrame = VoLumBlend(kBrass, kFrameAlpha, kPanel);
inline constexpr VoLumRgb kWellFrame = VoLumBlend(kBrass, kFrameAlpha, kWell);
inline constexpr VoLumRgb kSelBg = VoLumBlend(kBrass, kSelBgAlpha, kWell);
inline constexpr VoLumRgb kHoverBg = VoLumBlend(kBrass, kHoverAlpha, kWell);
inline constexpr VoLumRgb kButtonHoverBg = VoLumBlend(kBrass, kSelBgAlpha, kPanel);
inline constexpr VoLumRgb kButtonPressedBg = VoLumBlend(kBrass, 2 * kSelBgAlpha, kPanel);
inline constexpr VoLumRgb kDisabledText = VoLumBlend(kTextDim, kDisabledTextAlpha, kPanel);
} // namespace volum_chrome
} // namespace iplug
