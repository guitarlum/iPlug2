/*
 ==============================================================================
 
 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers. 
 
 See LICENSE.txt for  more info.
 
 ==============================================================================
*/

#include <memory>
#include "wdltypes.h"
#include "wdlstring.h"

#include "IPlugPlatform.h"
#include "IPlugAPP_host.h"

#include "VoLumAppInstanceGuard.h"
#include "VoLumAppShutdown.h"

#include "config.h"
#include "resource.h"

using namespace iplug;

#pragma mark - WINDOWS
#if defined OS_WIN
#include <windows.h>
#include <commctrl.h>

extern WDL_DLGRET MainDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

HWND gHWND;
extern HINSTANCE gHINSTANCE;
UINT gScrollMessage;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdParam, int nShowCmd)
{
  try
  {
#ifndef APP_ALLOW_MULTIPLE_INSTANCES
    HANDLE hMutex = OpenMutex(MUTEX_ALL_ACCESS, 0, BUNDLE_NAME); // BUNDLE_NAME used because it won't have spaces in it

    if (hMutex)
    {
      HWND hWnd = FindWindow(0, BUNDLE_NAME);

      // VoLum: a previous instance holds the mutex but has no window - it is either
      // still shutting down or it is stuck. Upstream called SetForegroundWindow(NULL)
      // and returned 0, so the launch did nothing at all and said nothing about it.
      // That is exactly what a user sees after an audio driver changes its sample rate
      // underneath a running VoLum: "wenn ich VoLum dann schliesse und wieder oeffne,
      // startet es nicht", with no clue that a process must be ended first.
      //
      // Wait a little first, since an orderly shutdown destroys the window before the
      // process exits and a double-click landing in that window is easy.
      const int kZombieWaitMs = 4000;
      const int kZombieStepMs = 100;

      for (int waited = 0; !hWnd && waited < kZombieWaitMs; waited += kZombieStepMs)
      {
        CloseHandle(hMutex);
        Sleep(kZombieStepMs);

        hMutex = OpenMutex(MUTEX_ALL_ACCESS, 0, BUNDLE_NAME);
        if (!hMutex)
          break; // the previous instance finished quitting; this one may start

        hWnd = FindWindow(0, BUNDLE_NAME);
      }

      // The mutex alone does not settle it: it can outlive the process that made it.
      // See VoLumAppInstanceGuard.h.
      const VoLumInstanceStart decision =
        VoLumDecideInstanceStart(hMutex != NULL, hWnd != NULL, hMutex != NULL && VoLumAnotherLiveInstanceExists());

      if (hMutex)
        CloseHandle(hMutex);

      if (decision == VoLumInstanceStart::FocusExisting)
      {
        if (IsIconic(hWnd))
          ShowWindow(hWnd, SW_RESTORE);

        SetForegroundWindow(hWnd);
        return 0;
      }

      if (decision == VoLumInstanceStart::ReportStuckInstance)
      {
        MessageBox(NULL,
                   BUNDLE_NAME " is still running, but its window has closed.\n\n"
                   "A previous session has not finished shutting down. End the " BUNDLE_NAME
                   " process in Task Manager, then start " BUNDLE_NAME " again.",
                   BUNDLE_NAME, MB_OK | MB_ICONWARNING);
        return 0;
      }
    }

    hMutex = CreateMutex(0, 0, BUNDLE_NAME);
#endif
    gHINSTANCE = hInstance;
    
    InitCommonControls();
    gScrollMessage = RegisterWindowMessage("MSWHEEL_ROLLMSG");

    IPlugAPPHost* pAppHost = IPlugAPPHost::Create();
    pAppHost->Init();
    pAppHost->TryToChangeAudio();

    HACCEL hAccel = LoadAccelerators(gHINSTANCE, MAKEINTRESOURCE(IDR_ACCELERATOR1));

    static UINT(WINAPI *__SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);

    double scale = 1.;

    if (!__SetProcessDpiAwarenessContext)
    {
      HINSTANCE h = LoadLibrary("user32.dll");
      if (h) *(void **)&__SetProcessDpiAwarenessContext = GetProcAddress(h, "SetProcessDpiAwarenessContext");
      if (!__SetProcessDpiAwarenessContext)
        *(void **)&__SetProcessDpiAwarenessContext = (void*)(INT_PTR)1;
    }
    if ((UINT_PTR)__SetProcessDpiAwarenessContext > (UINT_PTR)1)
    {
      __SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    // VoLum: IDD_DIALOG_MAIN has no menu bar on Windows; Settings opens Preferences.
    CreateDialog(gHINSTANCE, MAKEINTRESOURCE(IDD_DIALOG_MAIN), GetDesktopWindow(), IPlugAPPHost::MainDlgProc);

    for(;;)
    {
      MSG msg= {0,};
      int vvv = GetMessage(&msg, NULL, 0, 0);
      
      if (!vvv)
        break;
      
      if (vvv < 0)
      {
        Sleep(10);
        continue;
      }
      
      if (!msg.hwnd)
      {
        DispatchMessage(&msg);
        continue;
      }
      
      if (gHWND && (TranslateAccelerator(gHWND, hAccel, &msg) || IsDialogMessage(gHWND, &msg)))
        continue;
      
      // default processing for other dialogs
      HWND hWndParent = NULL;
      HWND temphwnd = msg.hwnd;
      
      do
      {
        if (GetClassLong(temphwnd, GCW_ATOM) == (INT)32770)
        {
          hWndParent = temphwnd;
          if (!(GetWindowLong(temphwnd, GWL_STYLE) & WS_CHILD))
            break; // not a child, exit
        }
      }
      while (temphwnd = GetParent(temphwnd));
      
      if (hWndParent && IsDialogMessage(hWndParent,&msg))
        continue;

      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    
    // in case gHWND didnt get destroyed -- this corresponds to SWELLAPP_DESTROY roughly
    if (gHWND)
      DestroyWindow(gHWND);
    
#ifndef APP_ALLOW_MULTIPLE_INSTANCES
    ReleaseMutex(hMutex);
#endif

    // VoLum: leave here rather than returning into the CRT's exit path. The window
    // is gone, the plugin was destroyed under WM_DESTROY and the settings file is
    // written, so there is nothing left of ours to run - but returning would hand
    // the process to ExitProcess, and with it to the DllMain of every ASIO driver
    // on the machine. One of those is all it takes to strand the process. See
    // VoLumAppShutdown.h.
    VoLumExitProcessNow(0);
  }
  catch(std::exception e)
  {
    DBGMSG("Exception: %s", e.what());
    return 1;
  }
  return 0;
}
#pragma mark - MAC
#elif defined(OS_MAC)
#import <Cocoa/Cocoa.h>
#include "IPlugSWELL.h"
#include "IPlugPaths.h"

HWND gHWND;
extern HMENU SWELL_app_stocksysmenu;

int main(int argc, char *argv[])
{
#if APP_COPY_AUV3
  //if invoked with an argument registerauv3 use plug-in kit to explicitly register auv3 app extension (doesn't happen from debugger)
  if(strcmp(argv[2], "registerauv3"))
  {
    WDL_String appexPath;
    appexPath.SetFormatted(1024, "pluginkit -a %s%s%s.appex", argv[0], "/../../Plugins/", appexPath.get_filepart());
    if(system(appexPath.Get()) > -1)
      NSLog(@"Registered audiounit app extension\n");
    else
      NSLog(@"Failed to register audiounit app extension\n");
  }
#endif
  
  if(AppIsSandboxed())
    DBGMSG("App is sandboxed, file system access etc restricted!\n");
  
  return NSApplicationMain(argc,  (const char **) argv);
}

INT_PTR SWELLAppMain(int msg, INT_PTR parm1, INT_PTR parm2)
{
  IPlugAPPHost* pAppHost = nullptr;
  
  switch (msg)
  {
    case SWELLAPP_ONLOAD:
      pAppHost = IPlugAPPHost::Create();
      pAppHost->Init();
      pAppHost->TryToChangeAudio();
      break;
    case SWELLAPP_LOADED:
    {
      pAppHost = IPlugAPPHost::sInstance.get();

      HMENU menu = SWELL_GetCurrentMenu();

      if (menu)
      {
        // work on a new menu
        menu = SWELL_DuplicateMenu(menu);
        HMENU src = LoadMenu(NULL, MAKEINTRESOURCE(IDR_MENU1));

        for (int x = 0; x < GetMenuItemCount(src)-1; x++)
        {
          HMENU sm = GetSubMenu(src,x);
          
          if (sm)
          {
            char str[1024];
            MENUITEMINFO mii = {sizeof(mii), MIIM_TYPE};
            mii.dwTypeData = str;
            mii.cch = sizeof(str);
            str[0] = 0;
            GetMenuItemInfo(src, x, TRUE, &mii);
            MENUITEMINFO mi= {sizeof(mi), MIIM_STATE|MIIM_SUBMENU|MIIM_TYPE,MFT_STRING, 0, 0, SWELL_DuplicateMenu(sm), NULL, NULL, 0, str};
            InsertMenuItem(menu, x+1, TRUE, &mi);
          }
        }
      }

      if (menu)
      {
        HMENU sm = GetSubMenu(menu, 1);
        DeleteMenu(sm, ID_QUIT, MF_BYCOMMAND); // remove QUIT from our file menu, since it is in the system menu on OSX
        DeleteMenu(sm, ID_PREFERENCES, MF_BYCOMMAND); // remove PREFERENCES from the file menu, since it is in the system menu on OSX

        // remove any trailing separators
        int a = GetMenuItemCount(sm);

        while (a > 0 && GetMenuItemID(sm, a-1) == 0)
          DeleteMenu(sm, --a, MF_BYPOSITION);

        DeleteMenu(menu, 1, MF_BYPOSITION); // delete file menu
      }
#if !defined _DEBUG || defined NO_IGRAPHICS
      if (menu)
      {
        HMENU sm = GetSubMenu(menu, 1);
        DeleteMenu(sm, ID_LIVE_EDIT, MF_BYCOMMAND);
        DeleteMenu(sm, ID_SHOW_DRAWN, MF_BYCOMMAND);
        DeleteMenu(sm, ID_SHOW_FPS, MF_BYCOMMAND);

        // remove any trailing separators
        int a = GetMenuItemCount(sm);

        while (a > 0 && GetMenuItemID(sm, a-1) == 0)
          DeleteMenu(sm, --a, MF_BYPOSITION);

        DeleteMenu(menu, 1, MF_BYPOSITION); // delete debug menu
      }
#else
      SetMenuItemModifier(menu, ID_LIVE_EDIT, MF_BYCOMMAND, 'E', FCONTROL);
      SetMenuItemModifier(menu, ID_SHOW_DRAWN, MF_BYCOMMAND, 'D', FCONTROL);
      SetMenuItemModifier(menu, ID_SHOW_BOUNDS, MF_BYCOMMAND, 'B', FCONTROL);
      SetMenuItemModifier(menu, ID_SHOW_FPS, MF_BYCOMMAND, 'F', FCONTROL);
#endif

      HWND hwnd = CreateDialog(gHINST, MAKEINTRESOURCE(IDD_DIALOG_MAIN), NULL, IPlugAPPHost::MainDlgProc);

      if (menu)
      {
        SetMenu(hwnd, menu); // set the menu for the dialog to our menu (on Windows that menu is set from the .rc, but on SWELL
        SWELL_SetDefaultModalWindowMenu(menu); // other windows will get the stock (bundle) menus
      }

      break;
    }
    case SWELLAPP_ONCOMMAND:
      // this is to catch commands coming from the system menu etc
      if (gHWND && (parm1&0xffff))
        SendMessage(gHWND, WM_COMMAND, parm1 & 0xffff, 0);
      break;
    case SWELLAPP_DESTROY:
      if (gHWND)
        DestroyWindow(gHWND);
      break;
    case SWELLAPP_PROCESSMESSAGE:
      MSG* pMSG = (MSG*) parm1;
      NSView* pContentView = (NSView*) pMSG->hwnd;
      NSEvent* pEvent = (NSEvent*) parm2;
      int etype = (int) [pEvent type];
          
      bool textField = [pContentView isKindOfClass:[NSText class]];
          
      if (!textField && etype == NSKeyDown)
      {
        int flag, code = SWELL_MacKeyToWindowsKey(pEvent, &flag);
        
        if (!(flag&~FVIRTKEY) && (code == VK_RETURN || code == VK_ESCAPE))
        {
          [pContentView keyDown: pEvent];
          return 1;
        }
      }
      break;
  }
  return 0;
}

#define CBS_HASSTRINGS 0
#define SWELL_DLG_SCALE_AUTOGEN 1
#define SET_IDD_DIALOG_PREF_SCALE 1.5
#if PLUG_HOST_RESIZE
#define SWELL_DLG_FLAGS_AUTOGEN SWELL_DLG_WS_FLIPPED|SWELL_DLG_WS_RESIZABLE
#endif
#include "swell-dlggen.h"
#include "resources/main.rc_mac_dlg"
#include "swell-menugen.h"
#include "resources/main.rc_mac_menu"

#pragma mark - LINUX
#elif defined(OS_LINUX)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "IPlugSWELL.h"
#include "IPlugAPP_linux.h"
#include "Linux/IPlugLinuxRunLoop.h"

// swell-internal.h: the modal dialog whose loop owns the thread, if any.
HWND DialogBoxIsActive();

HWND gHWND = nullptr;

namespace
{
// The longest the main loop sleeps, so SWELL's posted messages and timers and
// GDK's events are serviced at least this often.
constexpr int kMainLoopWaitMs = 10;
// A SWELL timer that drives the editor's run loop while a SWELL modal loop
// (Preferences, a message box) owns the thread instead of main().
constexpr UINT_PTR kRunLoopPumpTimerID = 1002;

IPlugLinuxPollRunLoop* gRunLoop = nullptr;
bool gInRunLoop = false;
HWND gCancelledModal = nullptr;

void RunLoopOnce(int timeoutMs)
{
  if (!gRunLoop || gInRunLoop)
    return;
  gInRunLoop = true;
  gRunLoop->RunOnce(timeoutMs);
  gInRunLoop = false;
}

// A quit signal closes the main window the way its close button does, so the
// plug-in and the audio stream tear down and write their settings. An open
// modal dialog is cancelled first: destroying its owner from inside its loop
// would leave that loop running against a deleted host.
void ServiceQuitSignals(bool mayCloseMainWindow)
{
  HWND modal = DialogBoxIsActive();
  switch (VoLumLinuxQuitStepFor(IPlugAPPLinuxQuitSignals(), modal != nullptr))
  {
  case VoLumLinuxQuitStep::CancelModal:
    if (modal != gCancelledModal)
    {
      gCancelledModal = modal;
      SendMessage(modal, WM_COMMAND, IDCANCEL, 0);
    }
    break;
  case VoLumLinuxQuitStep::CloseMainWindow:
    if (mayCloseMainWindow && gHWND)
      SendMessage(gHWND, WM_CLOSE, 0, 0);
    break;
  default:
    break;
  }
}

void PumpRunLoop(HWND, UINT, UINT_PTR, DWORD)
{
  ServiceQuitSignals(false);
  RunLoopOnce(0);
}

void PrintUsage()
{
  std::fputs("Usage: " BUNDLE_NAME " [option]\n"
             "  --list-audio-devices  print the audio drivers, their devices and the MIDI inputs, then exit\n"
             "  --preferences         open Preferences once the window is up\n"
             "  --help                print this text\n"
             "Settings and the library live in $XDG_DATA_HOME/" BUNDLE_NAME " (default ~/.local/share/" BUNDLE_NAME ").\n"
             "SIGINT, SIGTERM and SIGHUP quit as the close button does; a second one leaves at once.\n",
             stdout);
}
} // namespace

int main(int argc, char** argv)
{
  bool openPreferences = false;
  for (int i = 1; i < argc; ++i)
  {
    if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h"))
    {
      PrintUsage();
      return 0;
    }
    if (!std::strcmp(argv[i], "--list-audio-devices"))
    {
      IPlugAPPLinuxPrintDevices(stdout);
      return 0;
    }
    if (!std::strcmp(argv[i], "--preferences"))
      openPreferences = true;
  }

  IPlugAPPLinuxInstallQuitSignals();

  // SWELL keeps its own options next to settings.ini rather than in ~/.libSwell.ini.
  static const std::string swellIni = [] {
    const std::string dir = VoLumLinuxAppDataDir(std::getenv("XDG_DATA_HOME"), std::getenv("HOME"), BUNDLE_NAME);
    return dir.empty() ? dir : dir + "libSwell.ini";
  }();
  SWELL_ExtendedAPI("APPNAME", (void*) BUNDLE_NAME);
  if (!swellIni.empty())
    SWELL_ExtendedAPI("INIFILE", (void*) swellIni.c_str());
  SWELL_initargs(&argc, &argv);
  SWELL_Internal_PostMessage_Init();
  const int gdkFd = IPlugAPPLinuxGdkConnectionFd();
  if (gdkFd < 0)
  {
    std::fputs(BUNDLE_NAME ": cannot open the X display (DISPLAY is unset or unreachable)\n", stderr);
    return 1;
  }

  // Every IPlug timer and the editor's X connection bind to this loop.
  auto runLoop = std::make_unique<IPlugLinuxPollRunLoop>();
  gRunLoop = runLoop.get();
  IPlugLinuxPushedRunLoop pushedRunLoop(std::move(runLoop));

  IPlugAPPHost* pAppHost = IPlugAPPHost::Create();
  pAppHost->Init();
  pAppHost->TryToChangeAudio();

  CreateDialog(NULL, MAKEINTRESOURCE(IDD_DIALOG_MAIN), NULL, IPlugAPPHost::MainDlgProc);

  // Wakes the wait below when GDK has events for the SWELL windows.
  gRunLoop->RegisterFd(gdkFd, [](int, void*) {}, nullptr);

  if (gHWND)
  {
    SetTimer(gHWND, kRunLoopPumpTimerID, kMainLoopWaitMs, PumpRunLoop);
    if (openPreferences)
      PostMessage(gHWND, WM_COMMAND, ID_PREFERENCES, 0);
  }
  else
  {
    std::fputs(BUNDLE_NAME ": could not create the main window\n", stderr);
  }

  while (gHWND)
  {
    ServiceQuitSignals(true);
    SWELL_RunMessageLoop();
    if (gHWND)
      RunLoopOnce(kMainLoopWaitMs);
  }

  // WM_DESTROY released the host, which wrote settings.ini and the plug-in's
  // settings. It is still here only if the window never opened.
  IPlugAPPHost::sInstance = nullptr;
  gRunLoop->UnregisterFd(gdkFd);
  gRunLoop = nullptr;
  pushedRunLoop.Reset();

  // As on Windows, nothing of ours is left to run, and the atexit handlers and
  // static destructors of the audio, GL and GDK libraries are best skipped.
  std::fflush(nullptr);
  VoLumExitProcessNow(0);
}

INT_PTR SWELLAppMain(int msg, INT_PTR parm1, INT_PTR parm2)
{
  return 0;
}

#define CBS_HASSTRINGS 0
#define SWELL_DLG_SCALE_AUTOGEN 1
#define SET_IDD_DIALOG_PREF_SCALE 1.5
#if PLUG_HOST_RESIZE
#define SWELL_DLG_FLAGS_AUTOGEN SWELL_DLG_WS_FLIPPED|SWELL_DLG_WS_RESIZABLE
#endif
#include "swell-dlggen.h"
#include "resources/main.rc_mac_dlg"
#endif