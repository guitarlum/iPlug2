/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

// TICKET 06 LINK STUB.
// IGraphicsLinux reports no window. Ticket 07 replaces this file wholesale.
// Do not grow it into a real X11/GLX editor.

#include "IGraphicsLinux.h"

#include "IGraphicsLinuxFont.h"

using namespace iplug;
using namespace igraphics;

IGraphicsLinux::IGraphicsLinux(IGEditorDelegate& dlg, int w, int h, int fps, float scale)
  : IGRAPHICS_DRAW_CLASS(dlg, w, h, fps, scale)
{
}

IGraphicsLinux::~IGraphicsLinux() = default;

void* IGraphicsLinux::OpenWindow(void*) { return nullptr; }

void IGraphicsLinux::CloseWindow() {}

bool IGraphicsLinux::WindowIsOpen() { return false; }

void* IGraphicsLinux::GetWindow() { return nullptr; }

void IGraphicsLinux::GetMouseLocation(float& x, float& y) const
{
  x = 0.f;
  y = 0.f;
}

void IGraphicsLinux::PlatformResize(bool) {}

void IGraphicsLinux::HideMouseCursor(bool, bool) {}

void IGraphicsLinux::MoveMouseCursor(float, float) {}

EMsgBoxResult IGraphicsLinux::ShowMessageBox(const char*, const char*, EMsgBoxType, IMsgBoxCompletionHandlerFunc completionHandler)
{
  if (completionHandler)
    completionHandler(kNoResult);
  return kNoResult;
}

void IGraphicsLinux::ForceEndUserEdit() {}

const char* IGraphicsLinux::GetPlatformAPIStr() { return "Linux"; }

void IGraphicsLinux::UpdateTooltips() {}

bool IGraphicsLinux::RevealPathInExplorerOrFinder(WDL_String&, bool) { return false; }

void IGraphicsLinux::PromptForFile(WDL_String& fileName, WDL_String& path, EFileAction, const char*, IFileDialogCompletionHandlerFunc completionHandler)
{
  if (completionHandler)
    completionHandler(fileName, path);
}

void IGraphicsLinux::PromptForDirectory(WDL_String& dir, IFileDialogCompletionHandlerFunc completionHandler)
{
  if (completionHandler)
    completionHandler(dir, dir);
}

bool IGraphicsLinux::PromptForColor(IColor&, const char*, IColorPickerHandlerFunc) { return false; }

bool IGraphicsLinux::OpenURL(const char*, const char*, const char*, const char*) { return false; }

int IGraphicsLinux::GetUserOSVersion() { return 0; }

bool IGraphicsLinux::GetTextFromClipboard(WDL_String&) { return false; }

IPopupMenu* IGraphicsLinux::CreatePlatformPopupMenu(IPopupMenu&, const IRECT, bool& isAsync)
{
  isAsync = false;
  return nullptr;
}

void IGraphicsLinux::CreatePlatformTextEntry(int, const IText&, const IRECT&, int, const char*) {}

PlatformFontPtr IGraphicsLinux::LoadPlatformFont(const char*, const char* fileNameOrResID) { return LoadPlatformFontFromFile(fileNameOrResID); }

PlatformFontPtr IGraphicsLinux::LoadPlatformFont(const char*, void* pData, int dataSize) { return LoadPlatformFontFromMemory(pData, dataSize); }

PlatformFontPtr IGraphicsLinux::LoadPlatformFont(const char*, const char* fontName, ETextStyle style) { return LoadPlatformFontFromName(fontName, style); }

void IGraphicsLinux::CachePlatformFont(const char*, const PlatformFontPtr&) {}
