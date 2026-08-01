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

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

namespace iplug
{

/** How long to wait, in total, for the audio callback to acknowledge the fade
 * before concluding it is not running any more. */
inline constexpr int kVoLumFadeWaitMs = 10;
inline constexpr int kVoLumMaxFadeWaits = 200; // 2 s

/** Grace period for the AUDIO teardown, and nothing else. The fade wait above can
 * legitimately consume 2 s of it before any driver call is made, so this has to
 * exceed that plus an allowance for ASIOStop / ASIODisposeBuffers /
 * removeCurrentDriver on a healthy driver. It must not be stretched to cover
 * plugin teardown: that joins the model loader, which can be in the middle of a
 * multi-second capture load, and writes the settings file - work with no bound
 * that has no business being killed. Arm around CloseAudio(), disarm after. */
inline constexpr int kVoLumShutdownWatchdogMs = 5000;

/** Grace period for PLUGIN teardown, which runs after the audio teardown and does
 * the work the budget above deliberately excludes: joining the model loader, which
 * may be several seconds into a capture load, and writing the settings file.
 *
 * Unbounded was the wrong answer too. Whatever the plugin destructor waits on, a
 * process that never finishes it is a windowless VoLum holding the audio device,
 * which is the same symptom the audio watchdog exists to prevent - the next launch
 * cannot start and the user has to find the process in Task Manager. So: long
 * enough that no legitimate teardown is ever cut short, short enough that a wedge
 * ends by itself. */
inline constexpr int kVoLumPluginTeardownWatchdogMs = 20000;

/** Exit code used when the watchdog fires. Not 0: a wedged shutdown must not look
 * like a clean one to the end-to-end scripts or an installer that waits on the
 * process. */
inline constexpr int kVoLumShutdownWatchdogExitCode = 3;

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

/** Set to false once the work the watchdog guards has finished, so the timer
 * becomes a no-op instead of killing a process that is merely still tidying up. */
inline std::atomic<bool>& VoLumShutdownWatchdogArmed()
{
  static std::atomic<bool> armed{false};
  return armed;
}

/** Starts a detached thread that forces the process out if the guarded work has
 * not finished within timeoutMs. A normal teardown disarms it long before it
 * wakes, so it only ever fires on a wedged driver.
 *
 * Deliberately does no logging and touches nothing but the flag: its one job is
 * to be the thing that cannot itself get stuck. Bracket the teardown with log
 * lines instead - an unmatched "begin" in volum.log is the diagnosis.
 *
 * Called from a destructor, which is implicitly noexcept: a thread that cannot be
 * created must not turn resource exhaustion at quit into std::terminate. */
inline void VoLumArmShutdownWatchdog(int timeoutMs = kVoLumShutdownWatchdogMs)
{
  VoLumShutdownWatchdogArmed().store(true);
  try
  {
    std::thread watchdog([timeoutMs] {
      std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
      if (VoLumShutdownWatchdogArmed().load())
        std::_Exit(kVoLumShutdownWatchdogExitCode);
    });
    watchdog.detach();
  }
  catch (...)
  {
    VoLumShutdownWatchdogArmed().store(false);
  }
}

/** Disarm once the guarded work has returned. */
inline void VoLumDisarmShutdownWatchdog()
{
  VoLumShutdownWatchdogArmed().store(false);
}

} // namespace iplug
