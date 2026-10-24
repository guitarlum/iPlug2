/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#include "IGraphicsLinuxDialogs.h"

#include "../../IPlug/Linux/IPlugLinuxRunLoop.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

extern char** environ;

using iplug::IPlugLinuxBindFd;
using iplug::IPlugLinuxBindTimer;
using iplug::IPlugLinuxCheckUIThread;
using iplug::IPlugLinuxUnbindFd;
using iplug::IPlugLinuxUnbindTimer;

namespace iplug
{
namespace igraphics
{
namespace linux_dialogs
{
namespace
{

enum class Stage
{
  PortalCall,
  PortalWatch,
  Zenity,
  Kdialog,
  Deliver
};

struct Job
{
  Request request;
  Completion completion;
  void* owner = nullptr;
  Stage stage = Stage::PortalCall;
  pid_t pid = -1;
  int readFd = -1;
  uint32_t fdBinding = 0;
  uint32_t timerBinding = 0;
  bool open = false;
  std::string output;
  std::string requestPath;
  Result result;
};

std::string gPathOverride;
std::vector<Job*> gJobs;

struct Reap
{
  pid_t pid = -1;
  uint32_t timer = 0;
};

void OnReap(void* userData)
{
  auto* reap = static_cast<Reap*>(userData);
  int status = 0;
  if (waitpid(reap->pid, &status, WNOHANG) == 0)
    return;
  IPlugLinuxUnbindTimer(reap->timer);
  delete reap;
}

void DetachReap(pid_t pid)
{
  if (pid <= 0)
    return;
  int status = 0;
  if (waitpid(pid, &status, WNOHANG) != 0)
    return;
  auto* reap = new Reap;
  reap->pid = pid;
  reap->timer = IPlugLinuxBindTimer(100, OnReap, reap);
  if (reap->timer == 0)
  {
    kill(pid, SIGKILL);
    waitpid(pid, &status, WNOHANG);
    delete reap;
  }
}

std::string Which(const char* name)
{
  if (!name || !*name)
    return {};
  const char* pathEnv = gPathOverride.empty() ? std::getenv("PATH") : gPathOverride.c_str();
  if (!pathEnv)
    return {};
  std::string path = pathEnv;
  std::size_t start = 0;
  while (start <= path.size())
  {
    const std::size_t colon = path.find(':', start);
    const std::size_t end = colon == std::string::npos ? path.size() : colon;
    std::string dir = path.substr(start, end - start);
    if (dir.empty())
      dir = ".";
    const std::string candidate = dir + "/" + name;
    struct stat st{};
    if (::stat(candidate.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(candidate.c_str(), X_OK) == 0)
      return candidate;
    if (colon == std::string::npos)
      break;
    start = colon + 1;
  }
  return {};
}

std::vector<std::string> ChildEnv()
{
  std::vector<std::string> env;
  bool sawPath = false;
  for (char** item = environ; item && *item; ++item)
  {
    std::string entry = *item;
    if (!gPathOverride.empty() && entry.rfind("PATH=", 0) == 0)
    {
      entry = "PATH=" + gPathOverride;
      sawPath = true;
    }
    env.push_back(std::move(entry));
  }
  if (!gPathOverride.empty() && !sawPath)
    env.push_back("PATH=" + gPathOverride);
  return env;
}

// glibc 2.34 closes every inherited fd on exec except the ones file actions
// just opened, so a dialog cannot hold the host's X socket.
#ifndef POSIX_SPAWN_CLOEXEC_DEFAULT
  #define POSIX_SPAWN_CLOEXEC_DEFAULT 0
#endif

bool Spawn(const std::string& program, const std::vector<std::string>& args, pid_t& pid, int& readFd)
{
  int pipeFd[2] = {-1, -1};
  if (pipe2(pipeFd, O_CLOEXEC) != 0)
    return false;
  if (fcntl(pipeFd[0], F_SETFL, O_NONBLOCK) != 0)
  {
    close(pipeFd[0]);
    close(pipeFd[1]);
    return false;
  }

  posix_spawn_file_actions_t actions;
  posix_spawnattr_t attr;
  posix_spawn_file_actions_init(&actions);
  posix_spawnattr_init(&attr);
  sigset_t mask;
  sigemptyset(&mask);
  posix_spawnattr_setsigmask(&attr, &mask);
  short flags = POSIX_SPAWN_SETSIGMASK;
  if (POSIX_SPAWN_CLOEXEC_DEFAULT != 0)
    flags = static_cast<short>(flags | POSIX_SPAWN_CLOEXEC_DEFAULT);
  posix_spawnattr_setflags(&attr, flags);
  posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_adddup2(&actions, pipeFd[1], STDOUT_FILENO);
  posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_addclose(&actions, pipeFd[0]);
  posix_spawn_file_actions_addclose(&actions, pipeFd[1]);

  std::vector<char*> argv;
  argv.push_back(const_cast<char*>(program.c_str()));
  for (std::size_t i = 1; i < args.size(); ++i)
    argv.push_back(const_cast<char*>(args[i].c_str()));
  argv.push_back(nullptr);

  const std::vector<std::string> envStore = ChildEnv();
  std::vector<char*> envp;
  envp.reserve(envStore.size() + 1);
  for (const std::string& entry : envStore)
    envp.push_back(const_cast<char*>(entry.c_str()));
  envp.push_back(nullptr);

  pid = -1;
  const int rc = posix_spawn(&pid, program.c_str(), &actions, &attr, argv.data(), envp.data());
  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attr);
  close(pipeFd[1]);
  if (rc != 0)
  {
    close(pipeFd[0]);
    pid = -1;
    return false;
  }
  readFd = pipeFd[0];
  return true;
}

std::string GVariantQuote(const std::string& text, bool bytes)
{
  std::string out = bytes ? "b'" : "'";
  for (unsigned char c : text)
  {
    if (c == '\'' || c == '\\')
    {
      out.push_back('\\');
      out.push_back(static_cast<char>(c));
    }
    else if (c < 0x20 || (bytes && c > 0x7e))
    {
      char hex[8];
      std::snprintf(hex, sizeof(hex), "\\x%02x", c);
      out += hex;
    }
    else
      out.push_back(static_cast<char>(c));
  }
  out.push_back('\'');
  return out;
}

std::vector<std::string> Extensions(const std::string& ext)
{
  std::vector<std::string> out;
  std::size_t start = 0;
  while (start < ext.size())
  {
    while (start < ext.size() && ext[start] == ' ')
      ++start;
    if (start >= ext.size())
      break;
    std::size_t end = ext.find(' ', start);
    if (end == std::string::npos)
      end = ext.size();
    std::string one = ext.substr(start, end - start);
    if (!one.empty() && one[0] == '.')
      one.erase(one.begin());
    if (!one.empty() && one[0] != '*')
      one = "*." + one;
    if (!one.empty())
      out.push_back(one);
    start = end;
  }
  return out;
}

std::string ParentDir(const std::string& path)
{
  if (path.empty())
    return {};
  const std::size_t slash = path.find_last_of('/');
  if (slash == std::string::npos)
    return {};
  if (slash == 0)
    return "/";
  return path.substr(0, slash);
}

std::string JoinPath(const std::string& dir, const std::string& name)
{
  if (dir.empty())
    return name;
  if (name.empty())
    return dir;
  if (dir.back() == '/')
    return dir + name;
  return dir + "/" + name;
}

int HexNibble(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

std::string PercentDecode(const std::string& text)
{
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i)
  {
    if (text[i] == '%' && i + 2 < text.size())
    {
      const int hi = HexNibble(text[i + 1]);
      const int lo = HexNibble(text[i + 2]);
      if (hi >= 0 && lo >= 0)
      {
        out.push_back(static_cast<char>(hi * 16 + lo));
        i += 2;
        continue;
      }
    }
    out.push_back(text[i]);
  }
  return out;
}

std::string FileUriToPath(const std::string& uri)
{
  const std::string prefix = "file://";
  if (uri.compare(0, prefix.size(), prefix) != 0)
    return {};
  std::string rest = PercentDecode(uri.substr(prefix.size()));
  if (rest.rfind("localhost/", 0) == 0)
    rest = rest.substr(std::strlen("localhost"));
  if (rest.empty() || rest[0] != '/')
    return {};
  return rest;
}

std::vector<std::string> FileUris(const std::string& text)
{
  std::vector<std::string> paths;
  for (std::size_t at = 0; at < text.size();)
  {
    const std::size_t found = text.find("file://", at);
    if (found == std::string::npos)
      break;
    std::size_t end = found;
    while (end < text.size())
    {
      const char c = text[end];
      if (c == '\'' || c == '"' || c == '>' || c == ' ' || c == '\n' || c == '\r' || c == ')')
        break;
      ++end;
    }
    const std::string path = FileUriToPath(text.substr(found, end - found));
    if (!path.empty())
      paths.push_back(path);
    at = end;
  }
  return paths;
}

std::string PortalOptions(const Request& request)
{
  static unsigned token = 0;
  const std::string handle = "volum" + std::to_string(++token);
  std::string options = "{'handle_token': <'" + handle + "'>";
  const bool directory = request.kind == Kind::Directory;
  const bool multiple = request.kind == Kind::OpenFiles;
  if (multiple)
    options += ", 'multiple': <true>";
  if (directory)
    options += ", 'directory': <true>";
  const std::string folder = request.kind == Kind::SaveFile ? (request.path.empty() ? ParentDir(request.fileName) : request.path) : request.path;
  if (!folder.empty())
    options += ", 'current_folder': <" + GVariantQuote(folder, true) + ">";
  if (request.kind == Kind::SaveFile && !request.fileName.empty())
  {
    const std::string base = request.fileName.find('/') == std::string::npos ? request.fileName : request.fileName.substr(request.fileName.find_last_of('/') + 1);
    options += ", 'current_name': <" + GVariantQuote(base, false) + ">";
  }
  const std::vector<std::string> exts = Extensions(request.extensions);
  if (!exts.empty())
  {
    options += ", 'filters': <[('Files', [";
    for (std::size_t i = 0; i < exts.size(); ++i)
    {
      if (i)
        options += ", ";
      options += "(0, " + GVariantQuote(exts[i], false) + ")";
    }
    options += "])]>";
  }
  options += "}";
  return options;
}

std::vector<std::string> PortalArgv(const Request& request)
{
  const char* method = request.kind == Kind::SaveFile ? "org.freedesktop.portal.FileChooser.SaveFile" : "org.freedesktop.portal.FileChooser.OpenFile";
  const std::string title = request.title.empty() ? (request.kind == Kind::SaveFile ? "Save" : "Open") : request.title;
  return {"gdbus",
          "call",
          "--session",
          "--dest",
          "org.freedesktop.portal.Desktop",
          "--object-path",
          "/org/freedesktop/portal/desktop",
          "--method",
          method,
          "''",
          GVariantQuote(title, false),
          PortalOptions(request)};
}

std::vector<std::string> MonitorArgv(const std::string& objectPath) { return {"gdbus", "monitor", "--session", "--dest", "org.freedesktop.portal.Desktop", "--object-path", objectPath}; }

std::string ZenityFilename(const Request& request)
{
  if (request.kind == Kind::SaveFile)
    return JoinPath(request.path, request.fileName);
  if (!request.path.empty())
    return request.path.back() == '/' || request.kind == Kind::Directory ? request.path : request.path + "/";
  return {};
}

std::vector<std::string> ZenityArgv(const Request& request)
{
  std::vector<std::string> args = {"zenity"};
  if (request.kind == Kind::MessageBox)
  {
    switch (request.messageType)
    {
    case 1:
      args.insert(args.end(), {"--question", "--ok-label=OK", "--cancel-label=Cancel"});
      break;
    case 3:
      args.insert(args.end(), {"--question", "--ok-label=Yes", "--cancel-label=No", "--extra-button=Cancel"});
      break;
    case 4:
      args.insert(args.end(), {"--question", "--ok-label=Yes", "--cancel-label=No"});
      break;
    case 5:
      args.insert(args.end(), {"--question", "--ok-label=Retry", "--cancel-label=Cancel"});
      break;
    default:
      args.push_back("--info");
      break;
    }
    if (!request.title.empty())
      args.push_back("--title=" + request.title);
    args.push_back("--text=" + request.message);
    return args;
  }

  args.push_back("--file-selection");
  if (request.kind == Kind::SaveFile)
    args.insert(args.end(), {"--save", "--confirm-overwrite"});
  if (request.kind == Kind::Directory)
    args.push_back("--directory");
  if (request.kind == Kind::OpenFiles)
  {
    args.push_back("--multiple");
    args.push_back(std::string("--separator=\n"));
  }
  const std::string filename = ZenityFilename(request);
  if (!filename.empty())
    args.push_back("--filename=" + filename);
  const std::string title = request.title.empty() ? "Open" : request.title;
  args.push_back("--title=" + title);
  const std::vector<std::string> exts = Extensions(request.extensions);
  if (!exts.empty())
  {
    std::string filter = "--file-filter=";
    for (std::size_t i = 0; i < exts.size(); ++i)
    {
      if (i)
        filter.push_back(' ');
      filter += exts[i];
    }
    args.push_back(filter);
  }
  return args;
}

std::vector<std::string> KdialogArgv(const Request& request)
{
  std::vector<std::string> args = {"kdialog"};
  if (!request.title.empty())
    args.push_back("--title=" + request.title);
  if (request.kind == Kind::MessageBox)
  {
    switch (request.messageType)
    {
    case 4:
      args.insert(args.end(), {"--yes-label", "Yes", "--no-label", "No", "--yesno", request.message});
      break;
    case 1:
      args.insert(args.end(), {"--yes-label", "OK", "--no-label", "Cancel", "--yesno", request.message});
      break;
    case 3:
    case 5:
      args.insert(args.end(), {"--yesnocancel", request.message});
      break;
    default:
      args.insert(args.end(), {"--msgbox", request.message});
      break;
    }
    return args;
  }

  std::string filter;
  const std::vector<std::string> exts = Extensions(request.extensions);
  if (!exts.empty())
  {
    for (std::size_t i = 0; i < exts.size(); ++i)
    {
      if (i)
        filter.push_back(' ');
      filter += exts[i];
    }
    filter += "|Files";
  }
  const std::string start = request.kind == Kind::SaveFile ? JoinPath(request.path, request.fileName) : request.path;
  if (request.kind == Kind::OpenFiles)
    args.insert(args.end(), {"--multiple", "--separate-output"});
  if (request.kind == Kind::Directory)
    args.push_back("--getexistingdirectory");
  else if (request.kind == Kind::SaveFile)
    args.push_back("--getsavefilename");
  else
    args.push_back("--getopenfilename");
  args.push_back(start.empty() ? std::string(".") : start);
  if (!filter.empty() && request.kind != Kind::Directory)
    args.push_back(filter);
  return args;
}

std::vector<std::string> LinesOf(const std::string& text)
{
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start <= text.size())
  {
    std::size_t end = text.find('\n', start);
    if (end == std::string::npos)
      end = text.size();
    std::string line = text.substr(start, end - start);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (!line.empty())
      lines.push_back(line);
    if (end == text.size())
      break;
    start = end + 1;
  }
  return lines;
}

std::string WithSlash(std::string path)
{
  if (!path.empty() && path.back() != '/')
    path.push_back('/');
  return path;
}

int ExitCode(int status)
{
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  return -1;
}

int MessageResult(int type, int exitCode, const std::string& output, bool kdialog)
{
  if (kdialog && (type == 3 || type == 5))
  {
    if (exitCode == 0)
      return type == 5 ? 4 : 6; // Retry or Yes
    if (exitCode == 2)
      return 2;               // Cancel
    return type == 5 ? 2 : 7; // Cancel or No
  }
  if (type == 3)
  {
    if (exitCode == 0)
      return 6;
    if (output.find("Cancel") != std::string::npos)
      return 2;
    return 7;
  }
  if (type == 4)
    return exitCode == 0 ? 6 : 7;
  if (type == 5)
    return exitCode == 0 ? 4 : 2;
  if (type == 1)
    return exitCode == 0 ? 1 : 2;
  return exitCode == 0 ? 1 : 2;
}

std::string ObjectPath(const std::string& text)
{
  const std::size_t at = text.find("'/");
  if (at == std::string::npos)
    return {};
  const std::size_t end = text.find('\'', at + 1);
  if (end == std::string::npos)
    return {};
  return text.substr(at + 1, end - at - 1);
}

int ResponseCode(const std::string& text)
{
  const std::size_t at = text.find("uint32 ");
  if (at == std::string::npos)
    return -1;
  return std::atoi(text.c_str() + at + 7);
}

void DestroyJob(Job* job)
{
  gJobs.erase(std::remove(gJobs.begin(), gJobs.end(), job), gJobs.end());
  delete job;
}

void StopWatch(Job* job)
{
  job->open = false;
  if (job->fdBinding)
  {
    IPlugLinuxUnbindFd(job->fdBinding);
    job->fdBinding = 0;
  }
  if (job->timerBinding)
  {
    IPlugLinuxUnbindTimer(job->timerBinding);
    job->timerBinding = 0;
  }
  if (job->readFd >= 0)
  {
    close(job->readFd);
    job->readFd = -1;
  }
  DetachReap(job->pid);
  job->pid = -1;
}

void OnFd(int fd, void* userData);
void OnTimer(void* userData);
void Settle(Job* job, int status);

bool Arm(Job* job, const std::string& program, const std::vector<std::string>& args)
{
  int readFd = -1;
  pid_t pid = -1;
  if (!Spawn(program, args, pid, readFd))
    return false;
  job->pid = pid;
  job->readFd = readFd;
  job->output.clear();
  job->open = true;
  job->fdBinding = IPlugLinuxBindFd(readFd, OnFd, job);
  // A host that only wakes on POLLIN misses the empty-stdout exit (POLLHUP).
  // The timer reaps the child either way, without blocking the UI thread.
  job->timerBinding = IPlugLinuxBindTimer(50, OnTimer, job);
  if (job->fdBinding != 0)
    return true;
  DetachReap(job->pid);
  close(job->readFd);
  job->pid = -1;
  job->readFd = -1;
  job->open = false;
  return false;
}

void DeliverNow(Job* job)
{
  int pipeFd[2] = {-1, -1};
  if (pipe2(pipeFd, O_CLOEXEC) != 0)
  {
    Completion done = std::move(job->completion);
    Result result = job->result;
    DestroyJob(job);
    if (done)
      done(result);
    return;
  }
  fcntl(pipeFd[0], F_SETFL, O_NONBLOCK);
  const char mark = 1;
  if (write(pipeFd[1], &mark, 1) != 1)
  {
    close(pipeFd[0]);
    close(pipeFd[1]);
    Completion done = std::move(job->completion);
    Result result = job->result;
    DestroyJob(job);
    if (done)
      done(result);
    return;
  }
  close(pipeFd[1]);
  job->pid = -1;
  job->readFd = pipeFd[0];
  job->open = true;
  job->stage = Stage::Deliver;
  job->fdBinding = IPlugLinuxBindFd(pipeFd[0], OnFd, job);
  if (job->fdBinding == 0)
  {
    close(pipeFd[0]);
    job->readFd = -1;
    Completion done = std::move(job->completion);
    Result result = job->result;
    DestroyJob(job);
    if (done)
      done(result);
  }
}

void Finish(Job* job)
{
  Completion done = std::move(job->completion);
  Result result = std::move(job->result);
  DestroyJob(job);
  if (done)
    done(result);
}

bool TryKdialog(Job* job);
bool TryZenity(Job* job);

bool TryPortal(Job* job)
{
  const std::string program = Which("gdbus");
  if (program.empty())
    return TryZenity(job);
  job->stage = Stage::PortalCall;
  const std::vector<std::string> args = PortalArgv(job->request);
  if (!Arm(job, program, args))
    return TryZenity(job);
  return true;
}

bool TryZenity(Job* job)
{
  const std::string program = Which("zenity");
  if (program.empty())
    return TryKdialog(job);
  job->stage = Stage::Zenity;
  if (!Arm(job, program, ZenityArgv(job->request)))
    return TryKdialog(job);
  return true;
}

bool TryKdialog(Job* job)
{
  const std::string program = Which("kdialog");
  if (program.empty())
  {
    DeliverNow(job);
    return false;
  }
  job->stage = Stage::Kdialog;
  if (!Arm(job, program, KdialogArgv(job->request)))
  {
    DeliverNow(job);
    return false;
  }
  return true;
}

void ApplyFileOutput(Job* job)
{
  if (job->request.kind == Kind::MessageBox)
    return;
  std::vector<std::string> paths = LinesOf(job->output);
  if (job->request.kind == Kind::Directory)
  {
    for (std::string& path : paths)
      path = WithSlash(path);
  }
  job->result.paths = std::move(paths);
  job->result.accepted = !job->result.paths.empty();
}

void Settle(Job* job, int status)
{
  if (!job->open && job->stage != Stage::Deliver)
    return;
  const Stage stage = job->stage;
  const std::string output = job->output;
  StopWatch(job);

  if (stage == Stage::Deliver)
  {
    Finish(job);
    return;
  }

  if (stage == Stage::PortalCall)
  {
    job->requestPath = ExitCode(status) == 0 ? ObjectPath(output) : std::string();
    if (job->requestPath.empty())
    {
      TryZenity(job);
      return;
    }
    const std::string program = Which("gdbus");
    job->stage = Stage::PortalWatch;
    if (program.empty() || !Arm(job, program, MonitorArgv(job->requestPath)))
      TryZenity(job);
    return;
  }

  if (stage == Stage::PortalWatch)
  {
    const int code = ResponseCode(output);
    if (code == 0)
    {
      job->result.tool = "portal";
      job->result.paths = FileUris(output);
      if (job->request.kind == Kind::Directory)
      {
        for (std::string& path : job->result.paths)
          path = WithSlash(path);
      }
      job->result.accepted = !job->result.paths.empty();
      Finish(job);
      return;
    }
    if (code == 1)
    {
      job->result.tool = "portal";
      job->result.accepted = false;
      Finish(job);
      return;
    }
    TryZenity(job);
    return;
  }

  const bool kdialog = stage == Stage::Kdialog;
  job->result.tool = kdialog ? "kdialog" : "zenity";
  if (job->request.kind == Kind::MessageBox)
  {
    // A missing binary never reaches Settle: spawn failure tries the next tool.
    // A non-zero exit is the user dismissing the box, not a reason to open another.
    job->result.messageResult = MessageResult(job->request.messageType, ExitCode(status), output, kdialog);
    job->result.accepted = job->result.messageResult != 0 && job->result.messageResult != 2;
    Finish(job);
    return;
  }
  if (ExitCode(status) == 0)
  {
    job->output = output;
    ApplyFileOutput(job);
  }
  Finish(job);
}

void Drain(Job* job)
{
  if (job->readFd < 0)
    return;
  char buf[4096];
  for (;;)
  {
    const ssize_t n = read(job->readFd, buf, sizeof(buf));
    if (n > 0)
    {
      job->output.append(buf, static_cast<std::size_t>(n));
      continue;
    }
    if (n < 0 && errno == EINTR)
      continue;
    break;
  }
}

bool PortalWatchReady(const Job* job) { return job->stage == Stage::PortalWatch && ResponseCode(job->output) >= 0; }

void OnFd(int, void* userData)
{
  auto* job = static_cast<Job*>(userData);
  if (!job->open)
    return;
  Drain(job);
  if (PortalWatchReady(job))
  {
    if (job->pid > 0)
      kill(job->pid, SIGTERM);
    Settle(job, 0);
    return;
  }
  if (job->readFd < 0)
    return;
  char probe;
  const ssize_t n = read(job->readFd, &probe, 1);
  if (n > 0)
  {
    job->output.push_back(probe);
    Drain(job);
    if (PortalWatchReady(job))
    {
      if (job->pid > 0)
        kill(job->pid, SIGTERM);
      Settle(job, 0);
    }
    return;
  }
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    return;
  int status = 0;
  if (job->pid > 0)
  {
    const pid_t got = waitpid(job->pid, &status, WNOHANG);
    if (got == 0)
      return;
    if (got < 0)
      status = -1;
  }
  Settle(job, status);
}

void OnTimer(void* userData)
{
  auto* job = static_cast<Job*>(userData);
  if (!job->open || job->pid <= 0)
    return;
  // Read even while the child is alive, so a portal response is not stuck
  // behind a host that never reports POLLHUP.
  Drain(job);
  if (PortalWatchReady(job))
  {
    kill(job->pid, SIGTERM);
    Settle(job, 0);
    return;
  }
  int status = 0;
  const pid_t got = waitpid(job->pid, &status, WNOHANG);
  if (got == 0)
    return;
  Drain(job);
  Settle(job, got < 0 ? -1 : status);
}

} // namespace

void SetPathOverride(const char* pathOrNull) { gPathOverride = pathOrNull ? pathOrNull : ""; }

void Start(const Request& request, Completion completion, void* owner)
{
  IPlugLinuxCheckUIThread("linux_dialogs::Start");
  auto* job = new Job;
  job->request = request;
  job->completion = std::move(completion);
  job->owner = owner;
  gJobs.push_back(job);
  if (request.kind == Kind::MessageBox)
    TryZenity(job);
  else
    TryPortal(job);
}

void CancelOwner(void* owner)
{
  if (!owner)
    return;
  IPlugLinuxCheckUIThread("linux_dialogs::CancelOwner");
  std::vector<Job*> mine;
  for (Job* job : gJobs)
    if (job->owner == owner)
      mine.push_back(job);
  for (Job* job : mine)
  {
    job->completion = nullptr;
    if (job->open || job->pid > 0)
    {
      if (job->pid > 0)
        kill(job->pid, SIGTERM);
      StopWatch(job);
    }
    else if (job->fdBinding || job->readFd >= 0)
      StopWatch(job);
    DestroyJob(job);
  }
}

bool Launch(const std::string& target)
{
  IPlugLinuxCheckUIThread("linux_dialogs::Launch");
  if (target.empty())
    return false;
  const std::string program = Which("xdg-open");
  if (program.empty())
    return false;
  int readFd = -1;
  pid_t pid = -1;
  const std::vector<std::string> args = {"xdg-open", target};
  if (!Spawn(program, args, pid, readFd))
    return false;
  close(readFd);
  DetachReap(pid);
  return true;
}

} // namespace linux_dialogs
} // namespace igraphics
} // namespace iplug
