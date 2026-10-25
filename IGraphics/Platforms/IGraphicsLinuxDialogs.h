/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

/**
 * @file
 * Out-of-process file dialogs, message boxes and xdg-open for Linux.
 *
 * No GTK is linked. File pickers try the desktop portal (gdbus), then zenity,
 * then kdialog. Children are posix_spawn'd with an argv array. The plug-in
 * thread never waits for them: the completion runs later, on the UI thread,
 * from the run loop (the child's stdout fd, plus a short timer so a host that
 * only reports POLLIN still notices the exit).
 */

#include <functional>
#include <string>
#include <vector>

namespace iplug
{
namespace igraphics
{
namespace linux_dialogs
{

enum class Kind
{
  OpenFile,
  SaveFile,
  OpenFiles,
  Directory,
  MessageBox
};

struct Request
{
  Kind kind = Kind::OpenFile;
  std::string path;
  std::string fileName;
  std::string extensions; // space-separated, with or without a leading dot
  std::string title;
  std::string message;
  int messageType = 0; // EMsgBoxType
};

struct Result
{
  bool accepted = false;
  std::vector<std::string> paths;
  int messageResult = 0; // EMsgBoxResult; 0 is kNoResult
  std::string tool;      // "portal", "zenity", "kdialog", or empty
};

using Completion = std::function<void(const Result&)>;

/**
 * Replace PATH for the tools this unit spawns. nullptr uses the process PATH.
 * Tests point it at a directory of fake gdbus, zenity, kdialog and xdg-open.
 */
void SetPathOverride(const char* pathOrNull);

/**
 * Start a dialog. Returns before the tool exits. completion runs on the UI
 * thread, including when every tool is missing. owner is the IGraphicsLinux
 * that must not be called after it is destroyed: CancelOwner drops those
 * completions instead of running them.
 */
void Start(const Request& request, Completion completion, void* owner = nullptr);

/** Drop every dialog started with this owner. Does not run the completions. */
void CancelOwner(void* owner);

/**
 * posix_spawn xdg-open on target and return whether the process was created.
 * The opener is reaped from the run loop; this does not wait for it.
 */
bool Launch(const std::string& target);

} // namespace linux_dialogs
} // namespace igraphics
} // namespace iplug
