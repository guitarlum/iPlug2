#pragma once

// VoLumAppShutdown.h: the standalone app's audio-teardown policy and its
// shutdown watchdog, kept out of IPlugAPPHost so the policy can be unit-tested
// without an audio driver (see tests/test_iplug_app_shutdown.cpp).
//
// Why this exists
// ---------------
// RtApiAsio::abortStream() is not an abort. Its drain-skipping lines are
// commented out upstream, so it forwards straight to stopStream(), which does:
//
//     if ( handle->drainCounter == 0 ) {
//       handle->drainCounter = 2;
//       WaitForSingleObject( handle->condition, INFINITE );  // block until signaled
//     }
//
// Only the audio callback ever signals handle->condition. So the drain is safe
// exactly while the callback is still running, and is an unbounded wait when it
// is not - a driver wedged inside its own I/O, a device pulled mid-session, a
// second process that grabbed the hardware. CloseAudio() runs on the UI thread
// inside WM_DESTROY, so such a wait strands a process with no window that still
// holds the audio device, and the next launch cannot open it. That was the
// 1.2.1 "it will not start until I kill it in Task Manager" report.
//
// RtApiAsio::closeStream() stops a running stream on its own
// (`if (state == STREAM_RUNNING) { state = STOPPED; ASIOStop(); }`) and never
// waits on the callback, so when the callback is gone the right move is to skip
// the drain and close directly. The previous code did the opposite: it detected
// the dead callback with a bounded fade wait, logged the timeout, and then
// called abortStream() anyway.
//
// The watchdog covers what the policy cannot: closeStream() still calls into
// third-party ASIO code (ASIOStop, ASIODisposeBuffers, removeCurrentDriver), and
// a driver that is wedged can block there too. Once shutdown has begun the
// window is already destroyed and VoLum's settings were persisted during the
// session, so forcing the process out costs nothing and guarantees the reported
// symptom cannot recur whichever driver call hangs.

#include <chrono>
#include <cstdlib>
#include <thread>

namespace iplug
{

/** How long to wait, in total, for the audio callback to acknowledge the fade
 * before concluding it is not running any more. */
inline constexpr int kVoLumFadeWaitMs = 10;
inline constexpr int kVoLumMaxFadeWaits = 200; // 2 s

/** Grace period for the whole teardown once shutdown has begun. Audio teardown
 * is milliseconds of work; anything approaching this is a wedged driver. */
inline constexpr int kVoLumShutdownWatchdogMs = 5000;

struct VoLumAudioTeardownPlan
{
  /** Number of fade polls that elapsed. */
  int fadeWaits = 0;
  /** The audio callback acknowledged the fade, so it is still running. */
  bool callbackFaded = false;
  /** Safe to let the driver drain (abortStream/stopStream) before closing. */
  bool drainBeforeClose = false;
  /** The stream needs closing. */
  bool closeStream = false;
};

/** Waits, bounded, for the audio callback to acknowledge the fade, then decides
 * whether the driver drain may be attempted.
 *
 * isCallbackFaded and sleepMs are injected so the policy is testable without a
 * driver or real time; IPlugAPPHost::CloseAudio passes the real ones. */
template <typename IsCallbackFadedFn, typename SleepMsFn>
VoLumAudioTeardownPlan VoLumRunAudioTeardown(bool streamOpen, bool streamRunning, IsCallbackFadedFn isCallbackFaded,
                                             SleepMsFn sleepMs, int maxFadeWaits = kVoLumMaxFadeWaits,
                                             int fadeWaitMs = kVoLumFadeWaitMs)
{
  VoLumAudioTeardownPlan plan;

  if (!streamOpen)
    return plan;

  plan.closeStream = true;

  // A stream that is open but not running has no callback to fade and no drain
  // to wait for.
  if (!streamRunning)
    return plan;

  while (plan.fadeWaits < maxFadeWaits && !isCallbackFaded())
  {
    sleepMs(fadeWaitMs);
    ++plan.fadeWaits;
  }

  plan.callbackFaded = isCallbackFaded();

  // The load-bearing line: never hand the stream to the driver's drain unless
  // the callback that has to signal it is demonstrably alive.
  plan.drainBeforeClose = plan.callbackFaded;

  return plan;
}

/** Starts a detached thread that forces the process out if shutdown has not
 * completed within timeoutMs. A normal exit tears this thread down long before
 * it wakes, so it only ever fires on a stuck teardown.
 *
 * Deliberately does no logging and touches no shared state: its one job is to
 * be the thing that cannot itself get stuck. Bracket the teardown with log
 * lines instead - an unmatched "begin" in volum.log is the diagnosis. */
inline void VoLumArmShutdownWatchdog(int timeoutMs = kVoLumShutdownWatchdogMs)
{
  std::thread watchdog([timeoutMs] {
    std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
    std::_Exit(0);
  });
  watchdog.detach();
}

} // namespace iplug
