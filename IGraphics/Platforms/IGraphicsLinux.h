/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

#include "IPlugPlatform.h"

#include <functional>
#include <string>
#include <vector>

#include "IGraphicsLinuxGLNesting.h"
#include "IGraphics_select.h"

// Xlib stays out of this header: its None/Bool/Status macros would leak into
// every plug-in source. X handles are held as their underlying XID type.
struct _XDisplay;
struct __GLXcontextRec;

BEGIN_IPLUG_NAMESPACE
BEGIN_IGRAPHICS_NAMESPACE

/** IGraphics platform class for Linux: an Xlib child window with a GLX context.
 *
 * Each editor has its own X connection. Its fd and a 16 ms draw timer are
 * bound with IPlugLinuxBindFd/IPlugLinuxBindTimer, so all X and GL work runs
 * on the host UI thread from the host's run loop. Nothing calls XInitThreads.
 * The context is made current before every draw and released afterwards.
 *   @ingroup PlatformClasses */
class IGraphicsLinux final : public IGRAPHICS_DRAW_CLASS
{
public:
  IGraphicsLinux(IGEditorDelegate& dlg, int w, int h, int fps, float scale);
  virtual ~IGraphicsLinux();

  void* OpenWindow(void* pParent) override;
  void CloseWindow() override;
  bool WindowIsOpen() override { return mPlugWnd != 0; }
  void* GetWindow() override { return reinterpret_cast<void*>(mPlugWnd); }
  void PlatformResize(bool parentHasResized) override;
  void DrawResize() override;
  float GetPlatformWindowScale() const override { return GetScreenScale(); }

  void HideMouseCursor(bool hide, bool lock) override;
  void MoveMouseCursor(float x, float y) override;
  ECursor SetMouseCursor(ECursor cursorType) override;
  void GetMouseLocation(float& x, float& y) const override;

  EMsgBoxResult ShowMessageBox(const char* str, const char* caption, EMsgBoxType type, IMsgBoxCompletionHandlerFunc completionHandler) override;
  void ForceEndUserEdit() override;

  const char* GetPlatformAPIStr() override { return "x11"; }

  void UpdateTooltips() override {}

  bool RevealPathInExplorerOrFinder(WDL_String& path, bool select) override;
  void PromptForFile(WDL_String& fileName, WDL_String& path, EFileAction action, const char* ext, IFileDialogCompletionHandlerFunc completionHandler) override;
  void PromptForFiles(WDL_String& path, std::vector<WDL_String>& fileNames, const char* ext) override;
  /** Async multi-select. completion runs on the UI thread with full paths, or an empty list if the user cancels. */
  void PromptForOpenFiles(const char* ext, std::function<void(const std::vector<std::string>& paths)> completion);
  void PromptForDirectory(WDL_String& dir, IFileDialogCompletionHandlerFunc completionHandler) override;
  bool PromptForColor(IColor& color, const char* str, IColorPickerHandlerFunc func) override;

  bool OpenURL(const char* url, const char* msgWindowTitle, const char* confirmMsg, const char* errMsgOnFailure) override;

  static int GetUserOSVersion() { return 0; }
  bool GetTextFromClipboard(WDL_String& str) override;
  bool SetTextInClipboard(const char* str) override;

  PlatformFontPtr LoadPlatformFont(const char* fontID, const char* fileNameOrResID) override;
  PlatformFontPtr LoadPlatformFont(const char* fontID, void* pData, int dataSize) override;
  PlatformFontPtr LoadPlatformFont(const char* fontID, const char* fontName, ETextStyle style) override;
  void CachePlatformFont(const char* fontID, const PlatformFontPtr& font) override {}

  /** Drain pending X events. Called from the X fd handler and the draw timer. */
  void ProcessX11Events();
  /** One display frame: events, animation, and a draw if anything is dirty. */
  void OnDisplayTimer();

  /** X errors logged on any IGraphicsLinux connection since the process started. */
  static int XErrorCount();

protected:
  IPopupMenu* CreatePlatformPopupMenu(IPopupMenu& menu, const IRECT bounds, bool& isAsync) override;
  void CreatePlatformTextEntry(int paramIdx, const IText& text, const IRECT& bounds, int length, const char* str) override;

  void ActivateGLContext() override;
  void DeactivateGLContext() override;

private:
  bool CreateGLWindow(unsigned long parent, int w, int h);
  void DestroyGLWindow();
  float DetectScreenScale(const char*& source) const;
  void DispatchEvent(void* pEvent);
  void FreeCursors();

  _XDisplay* mDisplay = nullptr;
  unsigned long mPlugWnd = 0;
  unsigned long mParentWnd = 0; // the host's embed window; 0 for a top-level window
  unsigned long mColormap = 0;
  __GLXcontextRec* mGLContext = nullptr;

  // ActivateGLContext saves whatever was current; DeactivateGLContext restores it.
  IGraphicsLinuxGLNesting mGLNesting;
  _XDisplay* mPrevDisplay = nullptr;
  unsigned long mPrevDrawable = 0;
  unsigned long mPrevReadable = 0;
  __GLXcontextRec* mPrevContext = nullptr;

  uint32_t mFdBinding = 0;
  uint32_t mTimerBinding = 0;

  unsigned long mCursors[16] = {};
  unsigned long mBlankCursor = 0;
  float mHiddenCursorX = 0.f;
  float mHiddenCursorY = 0.f;

  unsigned long mLastClickTime = 0;
  int mLastClickButton = 0;
  float mLastClickX = 0.f;
  float mLastClickY = 0.f;
  bool mHasFocus = false;
  bool mNeedsFullRedraw = false;
};

END_IGRAPHICS_NAMESPACE
END_IPLUG_NAMESPACE
