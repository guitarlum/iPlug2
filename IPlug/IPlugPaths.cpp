/*
 ==============================================================================
 
 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers. 
 
 See LICENSE.txt for  more info.
 
 ==============================================================================
*/

/**
 * @file
 * @brief IPlugPaths implementation for Windows and Linux
 */

#include "IPlugPlatform.h"
#include "IPlugConstants.h"
#include "IPlugPaths.h"

#if defined OS_WEB
#include <emscripten/val.h>
#elif defined OS_LINUX
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#elif defined OS_WIN
#include <windows.h>
#include <Shlobj.h>
#include <Shlwapi.h>
#endif

BEGIN_IPLUG_NAMESPACE

#if defined OS_WIN
#pragma mark - OS_WIN

 // Helper for getting a known folder in UTF8
void GetKnownFolder(WDL_String &path, int identifier, int flags = 0)
{
  wchar_t wideBuffer[1024];

  SHGetFolderPathW(NULL, identifier, NULL, flags, wideBuffer);
  UTF16ToUTF8(path, wideBuffer);
}

static void GetModulePath(HMODULE hModule, WDL_String& path)
{
  path.Set("");
  char pathCStr[MAX_WIN32_PATH_LEN];
  pathCStr[0] = '\0';
  if (GetModuleFileName(hModule, pathCStr, MAX_WIN32_PATH_LEN))
  {
    int s = -1;
    for (int i = 0; i < strlen(pathCStr); ++i)
    {
      if (pathCStr[i] == '\\')
      {
        s = i;
      }
    }
    if (s >= 0 && s + 1 < strlen(pathCStr))
    {
      path.Set(pathCStr, s + 1);
    }
  }
}

void HostPath(WDL_String& path, const char* bundleID)
{
  GetModulePath(0, path);
}

void PluginPath(WDL_String& path, HMODULE pExtra)
{
  GetModulePath(pExtra, path);
}

void BundleResourcePath(WDL_String& path, HMODULE pExtra)
{
#ifdef VST3_API
  GetModulePath(pExtra, path);
#ifdef ARCH_64BIT
  path.SetLen(path.GetLength() - strlen("x86_64-win/"));
#else
  path.SetLen(path.GetLength() - strlen("x86-win/"));
#endif
  path.Append("Resources\\");
#endif
}

void DesktopPath(WDL_String& path)
{
  GetKnownFolder(path, CSIDL_DESKTOP);
}

void UserHomePath(WDL_String & path)
{
  GetKnownFolder(path, CSIDL_PROFILE);
}

void AppSupportPath(WDL_String& path, bool isSystem)
{
  GetKnownFolder(path, isSystem ? CSIDL_COMMON_APPDATA : CSIDL_LOCAL_APPDATA);
}

void VST3PresetsPath(WDL_String& path, const char* mfrName, const char* pluginName, bool isSystem)
{
  if (!isSystem)
    GetKnownFolder(path, CSIDL_PERSONAL, SHGFP_TYPE_CURRENT);
  else
    AppSupportPath(path, true);
  
  path.AppendFormatted(MAX_WIN32_PATH_LEN, "\\VST3 Presets\\%s\\%s", mfrName, pluginName);
}

void INIPath(WDL_String& path, const char * pluginName)
{
  GetKnownFolder(path, CSIDL_LOCAL_APPDATA);

  path.AppendFormatted(MAX_WIN32_PATH_LEN, "\\%s", pluginName);
}

void WebViewCachePath(WDL_String& path)
{
  GetKnownFolder(path, CSIDL_APPDATA);
  path.Append("\\iPlug2\\WebViewCache"); // tmp
}

static BOOL EnumResNameProc(HANDLE module, LPCTSTR type, LPTSTR name, LONG_PTR param)
{
  if (IS_INTRESOURCE(name)) return true; // integer resources not wanted
  else {
    WDL_String* search = (WDL_String*)param;
    if (search != 0 && name != 0)
    {
      //strip off extra quotes
      WDL_String strippedName(strlwr(name + 1));
      strippedName.SetLen(strippedName.GetLength() - 1);

      if (strcmp(strlwr(search->Get()), strippedName.Get()) == 0) // if we are looking for a resource with this name
      {
        search->SetFormatted(strippedName.GetLength() + 7, "found: %s", strippedName.Get());
        return false;
      }
    }
  }

  return true; // keep enumerating
}

EResourceLocation LocateResource(const char* name, const char* type, WDL_String& result, const char*, void* pHInstance, const char*)
{
  if (CStringHasContents(name))
  {
    WDL_String search(name);
    WDL_String typeUpper(type);

    HMODULE hInstance = static_cast<HMODULE>(pHInstance);

    EnumResourceNames(hInstance, _strupr(typeUpper.Get()), (ENUMRESNAMEPROC)EnumResNameProc, (LONG_PTR)&search);

    if (strstr(search.Get(), "found: ") != 0)
    {
      result.SetFormatted(MAX_PATH, "\"%s\"", search.Get() + 7, search.GetLength() - 7); // 7 = strlen("found: ")
      return EResourceLocation::kWinBinary;
    }
    else
    {
      if (PathFileExists(name))
      {
        result.Set(name);
        return EResourceLocation::kAbsolutePath;
      }
    }
  }
  return EResourceLocation::kNotFound;
}

const void* LoadWinResource(const char* resid, const char* type, int& sizeInBytes, void* pHInstance)
{
  WDL_String typeUpper(type);

  HMODULE hInstance = static_cast<HMODULE>(pHInstance);

  HRSRC hResource = FindResource(hInstance, resid, _strupr(typeUpper.Get()));

  if (!hResource)
    return NULL;

  DWORD size = SizeofResource(hInstance, hResource);

  if (size < 8)
    return NULL;

  HGLOBAL res = LoadResource(hInstance, hResource);

  const void* pResourceData = LockResource(res);

  if (!pResourceData)
  {
    sizeInBytes = 0;
    return NULL;
  }
  else
  {
    sizeInBytes = size;
    return pResourceData;
  }
}

#elif defined OS_LINUX
#pragma mark - OS_LINUX

// XDG locations and dladdr resource lookup, ported from iPlug2#1336.
// HostPath, PluginPath, BundleResourcePath, UserHomePath, INIPath and
// WebViewCachePath are the header symbols that PR left to the Windows side;
// they use the same XDG root and dladdr module directory.

void HostPath(WDL_String& path, const char*)
{
  path.Set("");
  char buf[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len <= 0)
    return;
  buf[len] = '\0';
  path.Set(buf);
  path.remove_filepart(true);
}

void PluginPath(WDL_String& path, PluginIDType)
{
  path.Set("");
  Dl_info info = {};
  if (dladdr((void*)PluginPath, &info) && info.dli_fname)
  {
    path.Set(info.dli_fname);
    path.remove_filepart(true);
  }
}

void BundleResourcePath(WDL_String& path, PluginIDType pExtra)
{
  PluginPath(path, pExtra);
#ifdef VST3_API
  // Contents/<arch>/VoLum.so -> Contents/Resources/
  path.Append("../Resources/");
#endif
}

void UserHomePath(WDL_String& path)
{
  path.Set("");
  const char* home = getenv("HOME");
  if (home && home[0])
    path.Set(home);
}

void AppSupportPath(WDL_String& path, bool isSystem)
{
  if (isSystem)
  {
    path.Set("/etc");
  }
  else
  {
    const char* xdgConfig = getenv("XDG_CONFIG_HOME");
    if (xdgConfig && xdgConfig[0])
      path.Set(xdgConfig);
    else
    {
      const char* home = getenv("HOME");
      if (!home || !home[0])
        return;
      path.SetFormatted(PATH_MAX, "%s/.config", home);
    }
  }
}

void DesktopPath(WDL_String& path)
{
  const char* home = getenv("HOME");
  if (!home || !home[0])
    return;
  path.SetFormatted(PATH_MAX, "%s/Desktop", home);
}

void VST3PresetsPath(WDL_String& path, const char* mfrName, const char* pluginName, bool isSystem)
{
  if (isSystem)
    path.SetFormatted(PATH_MAX, "/usr/share/vst3/presets/%s/%s", mfrName, pluginName);
  else
  {
    const char* home = getenv("HOME");
    if (!home || !home[0])
      return;
    path.SetFormatted(PATH_MAX, "%s/.vst3/presets/%s/%s", home, mfrName, pluginName);
  }
}

void INIPath(WDL_String& path, const char* pluginName)
{
  path.Set("");
  AppSupportPath(path, false);
  if (path.GetLength() && pluginName && pluginName[0])
    path.AppendFormatted(PATH_MAX, "/%s", pluginName);
}

void WebViewCachePath(WDL_String& path)
{
  path.Set("");
  const char* xdgCache = getenv("XDG_CACHE_HOME");
  if (xdgCache && xdgCache[0])
    path.Set(xdgCache);
  else
  {
    const char* home = getenv("HOME");
    if (!home || !home[0])
      return;
    path.SetFormatted(PATH_MAX, "%s/.cache", home);
  }
  path.Append("/iPlug2/WebViewCache");
}

EResourceLocation LocateResource(const char* name, const char* type, WDL_String& result, const char*, void*, const char*)
{
  if (!name || !name[0])
    return EResourceLocation::kNotFound;

  // If it's already an absolute path that exists, use it directly
  struct stat st;
  if (name[0] == '/' && stat(name, &st) == 0)
  {
    result.Set(name);
    return EResourceLocation::kAbsolutePath;
  }

  const char* fileNameOnly = strrchr(name, '/');
  fileNameOnly = fileNameOnly ? fileNameOnly + 1 : name;

  WDL_String candidate;

  // Use dladdr to find the directory of this plugin's own shared library (or executable).
  // This correctly handles VST3/CLAP plugins loaded into a host — /proc/self/exe would
  // give the host's path, not the plugin's.
  Dl_info dlInfo = {};
  if (dladdr((void*)LocateResource, &dlInfo) && dlInfo.dli_fname)
  {
    WDL_String soDir;
    soDir.Set(dlInfo.dli_fname);
    soDir.remove_filepart(true); // keep trailing slash

    // Try <so_dir>/resources/<type>/<filename>  (APP, flat CLAP layout)
    candidate.SetFormatted(PATH_MAX, "%sresources/%s/%s", soDir.Get(), type, fileNameOnly);
    if (stat(candidate.Get(), &st) == 0)
    {
      result.Set(candidate.Get());
      return EResourceLocation::kAbsolutePath;
    }

    // Try <so_dir>/../Resources/<type>/<filename>  (VST3 bundle: Contents/arch-linux/ -> Contents/Resources/)
    candidate.SetFormatted(PATH_MAX, "%s../Resources/%s/%s", soDir.Get(), type, fileNameOnly);
    if (stat(candidate.Get(), &st) == 0)
    {
      result.Set(candidate.Get());
      return EResourceLocation::kAbsolutePath;
    }

    // Try <so_dir>/../Resources/<filename>
    candidate.SetFormatted(PATH_MAX, "%s../Resources/%s", soDir.Get(), fileNameOnly);
    if (stat(candidate.Get(), &st) == 0)
    {
      result.Set(candidate.Get());
      return EResourceLocation::kAbsolutePath;
    }

    // Try <so_dir>/resources/<filename>
    candidate.SetFormatted(PATH_MAX, "%sresources/%s", soDir.Get(), fileNameOnly);
    if (stat(candidate.Get(), &st) == 0)
    {
      result.Set(candidate.Get());
      return EResourceLocation::kAbsolutePath;
    }
  }

  // Fallback: look next to the host executable (covers cases where dladdr is unavailable)
  char buf[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len > 0)
  {
    buf[len] = '\0';
    WDL_String exePath;
    exePath.Set(buf);
    exePath.remove_filepart(true);

    candidate.SetFormatted(PATH_MAX, "%sresources/%s/%s", exePath.Get(), type, fileNameOnly);
    if (stat(candidate.Get(), &st) == 0)
    {
      result.Set(candidate.Get());
      return EResourceLocation::kAbsolutePath;
    }

    candidate.SetFormatted(PATH_MAX, "%sresources/%s", exePath.Get(), fileNameOnly);
    if (stat(candidate.Get(), &st) == 0)
    {
      result.Set(candidate.Get());
      return EResourceLocation::kAbsolutePath;
    }
  }

  // Try path as-is relative to cwd
  if (stat(name, &st) == 0)
  {
    result.Set(name);
    return EResourceLocation::kAbsolutePath;
  }

  return EResourceLocation::kNotFound;
}

#elif defined OS_WEB
#pragma mark - OS_WEB

void AppSupportPath(WDL_String& path, bool isSystem)
{
  path.Set("Settings");
}

void DesktopPath(WDL_String& path)
{
  path.Set("");
}

void VST3PresetsPath(WDL_String& path, const char* mfrName, const char* pluginName, bool isSystem)
{
  path.Set("Presets");
}

EResourceLocation LocateResource(const char* name, const char* type, WDL_String& result, const char*, void*, const char*)
{
  if (CStringHasContents(name))
  {
    WDL_String plusSlash;
    WDL_String path(name);
    const char* file = path.get_filepart();
      
    bool foundResource = false;
    
    //TODO: FindResource is not sufficient here
    
    if(strcmp(type, "png") == 0) { //TODO: lowercase/uppercase png
      plusSlash.SetFormatted(strlen("/resources/img/") + strlen(file) + 1, "/resources/img/%s", file);
      foundResource = emscripten::val::global("preloadedImages").call<bool>("hasOwnProperty", std::string(plusSlash.Get()));
    }
    else if(strcmp(type, "ttf") == 0) { //TODO: lowercase/uppercase ttf
      plusSlash.SetFormatted(strlen("/resources/fonts/") + strlen(file) + 1, "/resources/fonts/%s", file);
      foundResource = true; // TODO: check ttf
    }
    else if(strcmp(type, "svg") == 0) { //TODO: lowercase/uppercase svg
      plusSlash.SetFormatted(strlen("/resources/img/") + strlen(file) + 1, "/resources/img/%s", file);
      foundResource = true; // TODO: check svg
    }
    
    if(foundResource)
    {
      result.Set(plusSlash.Get());
      return EResourceLocation::kAbsolutePath;
    }
  }
  return EResourceLocation::kNotFound;
}

#endif

END_IPLUG_NAMESPACE
