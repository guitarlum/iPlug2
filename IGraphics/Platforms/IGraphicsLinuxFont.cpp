/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#include "IGraphicsLinuxFont.h"

#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <memory>

#include "IPlugPaths.h"

namespace iplug
{
namespace igraphics
{
namespace
{
// Fontconfig constants (the enum values from fontconfig.h).
constexpr int kFC_WEIGHT_REGULAR = 80;
constexpr int kFC_WEIGHT_BOLD = 200;
constexpr int kFC_SLANT_ROMAN = 0;
constexpr int kFC_SLANT_ITALIC = 100;
constexpr int kFC_MatchPattern = 0;
constexpr int kFC_ResultMatch = 0;

#define LOAD_FC_SYM(fc, name) fc.name = reinterpret_cast<Fontconfig::fn_##name>(dlsym(fc.handle, "Fc" #name))
} // namespace

LinuxFileFont::LinuxFileFont(const char* filePath, bool isSystem)
  : PlatformFont(isSystem)
{
  if (filePath)
    mPath.Set(filePath);
}

IFontDataPtr LinuxFileFont::GetFontData()
{
  IFontDataPtr fontData(new IFontData());
  FILE* fp = fopen(mPath.Get(), "rb");
  if (!fp)
    return fontData;

  fseek(fp, 0, SEEK_END);
  long fileSize = ftell(fp);
  if (fileSize <= 0)
  {
    fclose(fp);
    return fontData;
  }
  fontData = std::make_unique<IFontData>(static_cast<int>(fileSize));

  if (!fontData->GetSize())
  {
    fclose(fp);
    return fontData;
  }

  fseek(fp, 0, SEEK_SET);
  size_t readSize = fread(fontData->Get(), 1, static_cast<size_t>(fontData->GetSize()), fp);
  fclose(fp);

  if (readSize && readSize == static_cast<size_t>(fontData->GetSize()))
    fontData->SetFaceIdx(0);

  return fontData;
}

LinuxMemFont::LinuxMemFont(const void* pData, int dataSize)
  : PlatformFont(false)
{
  if (pData && dataSize > 0)
    mData.Set(static_cast<const unsigned char*>(pData), dataSize);
}

IFontDataPtr LinuxMemFont::GetFontData() { return IFontDataPtr(new IFontData(mData.Get(), mData.GetSize(), 0)); }

Fontconfig* LoadFontconfig()
{
  static Fontconfig fc;

  if (fc.handle || fc.PatternCreate)
    return fc.handle ? &fc : nullptr;

  fc.handle = dlopen("libfontconfig.so.1", RTLD_NOW);
  if (!fc.handle)
    return nullptr;

#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wcast-function-type"
#elif defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
  LOAD_FC_SYM(fc, PatternCreate);
  LOAD_FC_SYM(fc, PatternAddString);
  LOAD_FC_SYM(fc, PatternAddInteger);
  LOAD_FC_SYM(fc, ConfigSubstitute);
  LOAD_FC_SYM(fc, DefaultSubstitute);
  LOAD_FC_SYM(fc, FontMatch);
  LOAD_FC_SYM(fc, PatternDestroy);
  LOAD_FC_SYM(fc, PatternGetString);
#if defined(__clang__)
  #pragma clang diagnostic pop
#elif defined(__GNUC__)
  #pragma GCC diagnostic pop
#endif

  if (!fc.PatternCreate || !fc.FontMatch || !fc.PatternGetString)
  {
    dlclose(fc.handle);
    fc.handle = nullptr;
    return nullptr;
  }

  return &fc;
}

PlatformFontPtr LoadPlatformFontFromFile(const char* fileNameOrResID)
{
  WDL_String fullPath;
  const EResourceLocation loc = LocateResource(fileNameOrResID, "ttf", fullPath, "", nullptr, nullptr);
  if (loc == kNotFound)
    return nullptr;

  return PlatformFontPtr(new LinuxFileFont(fullPath.Get(), false));
}

PlatformFontPtr LoadPlatformFontFromName(const char* fontName, ETextStyle style)
{
  if (!fontName || !fontName[0])
    return nullptr;

  Fontconfig* fc = LoadFontconfig();
  if (!fc || !fc->PatternAddString || !fc->PatternAddInteger || !fc->ConfigSubstitute || !fc->DefaultSubstitute || !fc->PatternDestroy)
    return nullptr;

  _FcPattern* pattern = fc->PatternCreate();
  if (!pattern)
    return nullptr;

  fc->PatternAddString(pattern, "family", reinterpret_cast<const unsigned char*>(fontName));
  fc->PatternAddInteger(pattern, "weight", style == ETextStyle::Bold ? kFC_WEIGHT_BOLD : kFC_WEIGHT_REGULAR);
  fc->PatternAddInteger(pattern, "slant", style == ETextStyle::Italic ? kFC_SLANT_ITALIC : kFC_SLANT_ROMAN);

  fc->ConfigSubstitute(nullptr, pattern, kFC_MatchPattern);
  fc->DefaultSubstitute(pattern);

  int result = 1; // FcResultNoMatch
  _FcPattern* matched = fc->FontMatch(nullptr, pattern, &result);
  fc->PatternDestroy(pattern);

  if (!matched)
    return nullptr;

  PlatformFontPtr ret;
  unsigned char* path = nullptr;
  if (fc->PatternGetString(matched, "file", 0, &path) == kFC_ResultMatch && path)
    ret = PlatformFontPtr(new LinuxFileFont(reinterpret_cast<const char*>(path), true));

  fc->PatternDestroy(matched);
  return ret;
}

PlatformFontPtr LoadPlatformFontFromMemory(void* pData, int dataSize)
{
  if (!pData || dataSize <= 0)
    return nullptr;
  return PlatformFontPtr(new LinuxMemFont(pData, dataSize));
}

} // namespace igraphics
} // namespace iplug
