/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

/**
 * @file
 * Font lookup ported from IGraphicsLinux.cpp in iPlug2#1336.
 *
 * Embedded TTFs go through LinuxFileFont (a path from LocateResource) or
 * LinuxMemFont (bytes already in memory). A family name is resolved with
 * LoadFontconfig(), which dlopens libfontconfig.so.1 and does not require
 * the library at link time. GetFontData() reads the file bytes; NanoVG's
 * FreeType (or stb) backend turns those bytes into glyphs. Named lookup
 * is the fallback when the face is not one of the embedded TTFs.
 */

#include "IGraphicsConstants.h"
#include "IGraphicsPrivate.h"

BEGIN_IPLUG_NAMESPACE
BEGIN_IGRAPHICS_NAMESPACE

/** Loads a TTF from a file path. GetFontData() reads the bytes on demand. */
class LinuxFileFont final : public PlatformFont
{
public:
  LinuxFileFont(const char* filePath, bool isSystem);
  IFontDataPtr GetFontData() override;

private:
  WDL_String mPath;
};

/** Wraps pre-loaded raw font bytes. Takes a copy so the caller can free its buffer. */
class LinuxMemFont final : public PlatformFont
{
public:
  explicit LinuxMemFont(const void* pData, int dataSize);
  IFontDataPtr GetFontData() override;

private:
  WDL_TypedBuf<unsigned char> mData;
};

struct _FcPattern;
struct _FcConfig;

/** dlopen'd fontconfig. Null when libfontconfig.so.1 is missing. */
struct Fontconfig
{
  void* handle = nullptr;

  using fn_PatternCreate = _FcPattern* (*)();
  using fn_PatternAddString = int (*)(_FcPattern*, const char*, const unsigned char*);
  using fn_PatternAddInteger = int (*)(_FcPattern*, const char*, int);
  using fn_ConfigSubstitute = int (*)(_FcConfig*, _FcPattern*, int);
  using fn_DefaultSubstitute = void (*)(_FcPattern*);
  using fn_FontMatch = _FcPattern* (*)(_FcConfig*, _FcPattern*, int*);
  using fn_PatternDestroy = void (*)(_FcPattern*);
  using fn_PatternGetString = int (*)(_FcPattern*, const char*, int, unsigned char**);

  fn_PatternCreate PatternCreate = nullptr;
  fn_PatternAddString PatternAddString = nullptr;
  fn_PatternAddInteger PatternAddInteger = nullptr;
  fn_ConfigSubstitute ConfigSubstitute = nullptr;
  fn_DefaultSubstitute DefaultSubstitute = nullptr;
  fn_FontMatch FontMatch = nullptr;
  fn_PatternDestroy PatternDestroy = nullptr;
  fn_PatternGetString PatternGetString = nullptr;
};

Fontconfig* LoadFontconfig();

/** IGraphicsLinux::LoadPlatformFont for a resource name or absolute path. */
PlatformFontPtr LoadPlatformFontFromFile(const char* fileNameOrResID);
/** IGraphicsLinux::LoadPlatformFont for a fontconfig family name. */
PlatformFontPtr LoadPlatformFontFromName(const char* fontName, ETextStyle style);
/** IGraphicsLinux::LoadPlatformFont for embedded bytes. */
PlatformFontPtr LoadPlatformFontFromMemory(void* pData, int dataSize);

END_IGRAPHICS_NAMESPACE
END_IPLUG_NAMESPACE
