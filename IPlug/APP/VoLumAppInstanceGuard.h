#pragma once

// VoLumAppInstanceGuard.h: deciding whether a launch is a second instance.
//
// Why this is not just "does the named mutex exist"
// -------------------------------------------------
// Upstream's single-instance guard opens the mutex by name and treats success as
// proof that VoLum is already running. A named kernel object outlives its creator
// for as long as the process object survives, and on Windows a process object can
// outlive the process. If a loaded DLL wedges the exit - see VoLumAppShutdown.h
// for the asio4all64 case that prompted this - the process stays in the table with
// its handle table intact, the mutex stays alive with it, and every launch from
// then on decides that VoLum is already running. The user gets an app that will
// not start and a Task Manager entry that refuses to be ended, because Windows has
// already marked the process exited.
//
// So the question the guard actually wants answered is not "does the mutex exist"
// but "is another VoLum still alive". The decision is a pure function of three
// observations so it can be tested without spawning anything; only the probe that
// gathers them touches Win32.

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string.h>
#include <tlhelp32.h>
#endif

namespace iplug
{

enum class VoLumInstanceStart
{
  /** No other instance is alive; this one owns the app. */
  Start,
  /** Another instance is up and showing its window; bring that one forward. */
  FocusExisting,
  /** Another instance is alive but has shown no window in the grace period. */
  ReportStuckInstance,
};

/** windowFound is the result of looking for the main window, and liveInstance of
 * asking the OS whether another VoLum process is actually still running.
 *
 * The order matters. A visible window is the strongest evidence of a healthy
 * instance and settles it immediately. Only when there is none does the liveness
 * of the process decide, and a mutex with no live owner is a leftover, not an
 * instance: start. */
inline VoLumInstanceStart VoLumDecideInstanceStart(bool mutexExists, bool windowFound, bool liveInstance)
{
  if (!mutexExists)
    return VoLumInstanceStart::Start;

  if (windowFound)
    return VoLumInstanceStart::FocusExisting;

  return liveInstance ? VoLumInstanceStart::ReportStuckInstance : VoLumInstanceStart::Start;
}

#ifdef _WIN32
/** True if some other process is running this same executable and has not exited.
 *
 * GetExitCodeProcess is the load-bearing call: it is what tells a process that is
 * merely still listed from one that is still running. A process that cannot be
 * opened or queried is counted as alive, so an unexpected failure here falls back
 * to the old, cautious behaviour of refusing to start a second instance. */
inline bool VoLumAnotherLiveInstanceExists()
{
  char selfPath[MAX_PATH] = {0};
  if (!::GetModuleFileNameA(NULL, selfPath, MAX_PATH))
    return true;

  const char* selfName = strrchr(selfPath, '\\');
  selfName = selfName ? selfName + 1 : selfPath;

  HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE)
    return true;

  const DWORD self = ::GetCurrentProcessId();
  bool live = false;

  PROCESSENTRY32 entry = {0};
  entry.dwSize = sizeof(entry);

  for (BOOL more = ::Process32First(snapshot, &entry); more && !live; more = ::Process32Next(snapshot, &entry))
  {
    if (entry.th32ProcessID == self || _stricmp(entry.szExeFile, selfName) != 0)
      continue;

    HANDLE proc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
    if (!proc)
      continue; // gone between the snapshot and here, or not ours to look at

    DWORD exitCode = 0;
    if (::GetExitCodeProcess(proc, &exitCode) && exitCode == STILL_ACTIVE)
      live = true;

    ::CloseHandle(proc);
  }

  ::CloseHandle(snapshot);
  return live;
}
#endif

} // namespace iplug
