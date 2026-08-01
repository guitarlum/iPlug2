/*
 ==============================================================================
 
 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers. 
 
 See LICENSE.txt for  more info.
 
 ==============================================================================
*/

#pragma once

/**
 
 IPlug plug-in -> Standalone app wrapper, using Cockos' SWELL
 
 Oli Larkin 2014-2018
 
 Notes:
 
 App settings are stored in a .ini (text) file. The location is as follows:
 
 Windows7: C:\Users\USERNAME\AppData\Local\BUNDLE_NAME\settings.ini
 Windows XP/Vista: C:\Documents and Settings\USERNAME\Local Settings\Application Data\BUNDLE_NAME\settings.ini
 macOS: /Users/USERNAME/Library/Application\ Support/BUNDLE_NAME/settings.ini
 OR
 /Users/USERNAME/Library/Containers/BUNDLE_ID/Data/Library/Application Support/BUNDLE_NAME/settings.ini
 
 */

#include <cstdlib>
#include <string>
#include <vector>
#include <limits>
#include <memory>
#include <chrono>

#include "wdltypes.h"
#include "wdlstring.h"

#include "IPlugPlatform.h"
#include "IPlugConstants.h"

#include "IPlugAPP.h"
#include "VoLumIPlugAPPVectorAccumulator.h"

#include "config.h"

#ifdef OS_WIN
  #include <WindowsX.h>
  #include <commctrl.h>
  #include <shlobj.h>
  #define DEFAULT_INPUT_DEV "Default Device"
  #define DEFAULT_OUTPUT_DEV "Default Device"
#elif defined(OS_MAC)
  #include "IPlugSWELL.h"
  #define SLEEP( milliseconds ) usleep( (unsigned long) (milliseconds * 1000.0) )
  #define DEFAULT_INPUT_DEV "Built-in Input"
  #define DEFAULT_OUTPUT_DEV "Built-in Output"
#elif defined(OS_LINUX)
  #include "IPlugSWELL.h"
#endif

#include "RtAudio.h"
#include "RtMidi.h"

#define OFF_TEXT "off"

extern HWND gHWND;
extern HINSTANCE gHINSTANCE;

BEGIN_IPLUG_NAMESPACE

const int kNumBufferSizeOptions = 10;
const std::string kBufferSizeOptions[kNumBufferSizeOptions] = {"48", "64", "96", "128", "256", "512", "1024", "2048", "4096", "8192" };

inline uint32_t NormalizeAPPBufferSize(uint32_t bufferSize)
{
  for (int i = 0; i < kNumBufferSizeOptions; ++i)
  {
    const uint32_t option = static_cast<uint32_t>(atoi(kBufferSizeOptions[i].c_str()));
    if (bufferSize <= option)
      return option;
  }

  return static_cast<uint32_t>(atoi(kBufferSizeOptions[kNumBufferSizeOptions - 1].c_str()));
}
const int kDeviceDS = 0; const int kDeviceCoreAudio = 0; const int kDeviceAlsa = 0;
const int kDeviceASIO = 1; const int kDeviceJack = 1;
extern UINT gSCROLLMSG;

class IPlugAPP;

/** A class that hosts an IPlug as a standalone app and provides Audio/Midi I/O */
class IPlugAPPHost
{
public:
  
  /** Used to manage changes to app i/o */
  struct AppState
  {
    WDL_String mAudioInDev;
    WDL_String mAudioOutDev;
    WDL_String mMidiInDev;
    WDL_String mMidiOutDev;
    uint32_t mAudioDriverType;
    uint32_t mAudioSR;
    uint32_t mBufferSize;
    uint32_t mMidiInChan;
    uint32_t mMidiOutChan;
    
    uint32_t mAudioInChanL;
    uint32_t mAudioInChanR;
    uint32_t mAudioOutChanL;
    uint32_t mAudioOutChanR;
    
    AppState()
    : mAudioInDev(DEFAULT_INPUT_DEV)
    , mAudioOutDev(DEFAULT_OUTPUT_DEV)
    , mMidiInDev(OFF_TEXT)
    , mMidiOutDev(OFF_TEXT)
    , mAudioDriverType(0) // DirectSound / CoreAudio by default
    , mBufferSize(512)
    , mAudioSR(44100)
    , mMidiInChan(0)
    , mMidiOutChan(0)
    
    , mAudioInChanL(1)
    , mAudioInChanR(2)
    , mAudioOutChanL(1)
    , mAudioOutChanR(2)
    {
    }
    
    AppState (const AppState& obj)
    : mAudioInDev(obj.mAudioInDev.Get())
    , mAudioOutDev(obj.mAudioOutDev.Get())
    , mMidiInDev(obj.mMidiInDev.Get())
    , mMidiOutDev(obj.mMidiOutDev.Get())
    , mAudioDriverType(obj.mAudioDriverType)
    , mBufferSize(obj.mBufferSize)
    , mAudioSR(obj.mAudioSR)
    , mMidiInChan(obj.mMidiInChan)
    , mMidiOutChan(obj.mMidiOutChan)
    
    , mAudioInChanL(obj.mAudioInChanL)
    , mAudioInChanR(obj.mAudioInChanR)
    , mAudioOutChanL(obj.mAudioOutChanL)
    , mAudioOutChanR(obj.mAudioOutChanR)
    {
    }
    
    bool operator==(const AppState& rhs) const {
      return (rhs.mAudioDriverType == mAudioDriverType &&
              rhs.mBufferSize == mBufferSize &&
              rhs.mAudioSR == mAudioSR &&
              rhs.mMidiInChan == mMidiInChan &&
              rhs.mMidiOutChan == mMidiOutChan &&
              (strcmp(rhs.mAudioInDev.Get(), mAudioInDev.Get()) == 0) &&
              (strcmp(rhs.mAudioOutDev.Get(), mAudioOutDev.Get()) == 0) &&
              (strcmp(rhs.mMidiInDev.Get(), mMidiInDev.Get()) == 0) &&
              (strcmp(rhs.mMidiOutDev.Get(), mMidiOutDev.Get()) == 0) &&

              rhs.mAudioInChanL == mAudioInChanL &&
              rhs.mAudioInChanR == mAudioInChanR &&
              rhs.mAudioOutChanL == mAudioOutChanL &&
              rhs.mAudioOutChanR == mAudioOutChanR

      );
    }
    bool operator!=(const AppState& rhs) const { return !operator==(rhs); }
  };
  
  static IPlugAPPHost* Create();
  static std::unique_ptr<IPlugAPPHost> sInstance;
  
  void PopulateSampleRateList(HWND hwndDlg, RtAudio::DeviceInfo* pInputDevInfo, RtAudio::DeviceInfo* pOutputDevInfo);
  void PopulateAudioInputList(HWND hwndDlg, RtAudio::DeviceInfo* pInfo);
  void PopulateAudioOutputList(HWND hwndDlg, RtAudio::DeviceInfo* pInfo);
  void PopulateDriverSpecificControls(HWND hwndDlg);
  void PopulateAudioDialogs(HWND hwndDlg);
  bool PopulateMidiDialogs(HWND hwndDlg);
  void PopulatePreferencesDialog(HWND hwndDlg);
  
  IPlugAPPHost();
  ~IPlugAPPHost();
  
  bool OpenWindow(HWND pParent);
  void CloseWindow();

  bool Init();
  bool InitState();
  void UpdateINI();
  
  /** Returns the name of the audio device at idx
   * @param idx The index RTAudio has given the audio device
   * @return The device name. Core Audio device names are truncated. */
  std::string GetAudioDeviceName(int idx) const;
  // returns the rtaudio device ID, based on the (truncated) device name
  
  /** Returns the audio device index linked to a particular name
  * @param name The name of the audio device to test
  * @return The integer index RTAudio has given the audio device */
  int GetAudioDeviceIdx(const char* name) const;
  
  /** @param direction Either kInput or kOutput
   * @param name The name of the midi device
   * @return An integer specifying the output port number, where 0 means any */
  int GetMIDIPortNumber(ERoute direction, const char* name) const;
  
  /** find out which devices have input channels & which have output channels, add their ids to the lists */
  void ProbeAudioIO();
  void ProbeMidiIO();
  bool InitMidi();
  void CloseAudio();
  bool InitAudio(uint32_t inId, uint32_t outId, uint32_t sr, uint32_t iovs);
  bool AudioSettingsInStateAreEqual(AppState& os, AppState& ns);
  bool MIDISettingsInStateAreEqual(AppState& os, AppState& ns);
  bool RestoreActiveAudioStateAfterFailure(const char* message);

  bool TryToChangeAudioDriverType();
  bool TryToChangeAudio();
  bool SelectMIDIDevice(ERoute direction, const char* portName);

  /** VoLum: polled from the main window's timer, once the window exists.
   *
   * Two jobs, both of which need a window and so cannot be done during Init():
   *
   *  - Report an audio failure that happened at startup. RestoreActiveAudioStateAfterFailure
   *    posts its MessageBox to gHWND, which is still NULL that early, so a stored sample
   *    rate the device no longer supports used to fail silently: the app came up with no
   *    audio, overwrote settings.ini with defaults, and said nothing.
   *  - Follow the driver when it changes the sample rate on its own, which happens when
   *    the user picks a new rate in their interface's control panel. RtAudio stops the
   *    stream in that case; without this the stream is never reopened and the app is
   *    silent until it is restarted. */
  void PollAudioStatus();

  /** VoLum: clamp a stored sample rate to something the open devices actually offer.
   * Returns 0 when the device list cannot be trusted, in which case the caller keeps
   * what it had. */
  uint32_t NearestSupportedSampleRate(uint32_t desiredSR, int inputID, int outputID);
  
  static int AudioCallback(void* pOutputBuffer, void* pInputBuffer, uint32_t nFrames, double streamTime, RtAudioStreamStatus status, void* pUserData);
  static void MIDICallback(double deltatime, std::vector<uint8_t>* pMsg, void* pUserData);
  static void ErrorCallback(RtAudioError::Type type, const std::string& errorText);

  static WDL_DLGRET PreferencesDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
  static WDL_DLGRET MainDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

  IPlugAPP* GetPlug() { return mIPlug.get(); }

  /** VoLum: the active audio stream's I/O block size, in frames. */
  uint32_t GetIOBufferSize() const { return mBufferSize; }

  /** VoLum: the device-reported input+output latency of the active stream, in frames,
   * or 0 when the backend does not report one. ASIO drivers report a figure that
   * already includes their own buffering; DirectSound/WASAPI often report nothing.
   * Used by the standalone Settings page to show a real round trip instead of only
   * the plugin's algorithmic delay. */
  long GetStreamLatencyFrames() const
  {
    if (!mDAC || !mDAC->isStreamOpen())
      return 0;
    const long l = mDAC->getStreamLatency();
    return l > 0 ? l : 0;
  }

private:
  std::unique_ptr<IPlugAPP> mIPlug = nullptr;
  std::unique_ptr<RtAudio> mDAC = nullptr;
  std::unique_ptr<RtMidiIn> mMidiIn = nullptr;
  std::unique_ptr<RtMidiOut> mMidiOut = nullptr;
  int mMidiOutChannel = -1;
  int mMidiInChannel = -1;
  
  /**  */
  AppState mState;
  /** When the preferences dialog is opened the existing state is cached here, and restored if cancel is pressed */
  AppState mTempState;
  /** When the audio driver is started the current state is copied here so that if OK is pressed after APPLY nothing is changed */
  AppState mActiveState;
  
  double mSampleRate = 44100.;
  uint32_t mSamplesElapsed = 0;
  uint32_t mVecWait = 0;
  uint32_t mBufferSize = 512;
  VoLumIPlugAPPVectorAccumulator mAudioVectorAccumulator;
  bool mExiting = false;
  bool mAudioEnding = false;
  bool mAudioDone = false;

  // VoLum: physical channel routing for the active audio stream.
  // *Offset are 0-based device-channel offsets that the AudioCallback uses
  // to map the user's Audio Settings selection (mAudioInChanL/R, mAudioOutChanL/R,
  // 1-based) onto the raw RtAudio non-interleaved buffer. *Chans is the total
  // number of device channels the stream was opened with (used for striding /
  // memset). Without these, the host always opened firstChannel=0/nChannels=
  // MaxNChannels and the dialog selection had no effect (broken on multi-channel
  // ASIO interfaces such as the RME Babyface Pro FS).
  int mActiveInOffset = 0;
  int mActiveOutOffsetL = 0;
  int mActiveOutOffsetR = 1;
  int mActiveDeviceInChans = 0;
  int mActiveDeviceOutChans = 0;

  /** The index of the operating systems default input device, -1 if not detected */
  int32_t mDefaultInputDev = -1;
  /** The index of the operating systems default output device, -1 if not detected */
  int32_t mDefaultOutputDev = -1;
    
  WDL_String mINIPath;

  /** VoLum: an audio failure that happened before there was a window to report it in.
   * Shown once by PollAudioStatus and then cleared. */
  WDL_String mDeferredAudioError;

  /** VoLum: brake on the driver-follow logic in PollAudioStatus. */
  bool mFollowDriverChanges = true;
  int mAutoReopenCount = 0;
  std::chrono::steady_clock::time_point mLastAutoReopen = std::chrono::steady_clock::now();
  
  std::vector<uint32_t> mAudioInputDevs;
  std::vector<uint32_t> mAudioOutputDevs;
  std::vector<std::string> mAudioIDDevNames;
  std::vector<std::string> mMidiInputDevNames;
  std::vector<std::string> mMidiOutputDevNames;
  
  WDL_PtrList<double> mInputBufPtrs;
  WDL_PtrList<double> mOutputBufPtrs;

  friend class IPlugAPP;
};

END_IPLUG_NAMESPACE
