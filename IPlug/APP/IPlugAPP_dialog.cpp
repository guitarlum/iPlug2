/*
 ==============================================================================
 
 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers. 
 
 See LICENSE.txt for  more info.
 
 ==============================================================================
*/

#include "IPlugAPP_host.h"
#include "config.h"
#include "resource.h"

#ifdef OS_WIN
#include "asio.h"
#include <shellapi.h>
#include "VoLumWinChrome.h"
#define GET_MENU() GetMenu(gHWND)
#elif defined OS_MAC
#define GET_MENU() SWELL_GetCurrentMenu()
#endif

using namespace iplug;

// VoLum: config.h owns the URL so the About card's manual link opens the same page.
static constexpr const char* kVoLumManualURL = VOLUM_MANUAL_URL;

// VoLum: timer id for the main window's audio-status poll. See PollAudioStatus.
static constexpr UINT_PTR kAudioStatusTimerID = 1001;

// VoLum: non-null while Preferences is open. The audio-status poll stands down for as
// long as it is: reopening the stream underneath a dialog the user is editing would
// fight them for the device, and the driver's pending rate keeps until the next tick.
static HWND gPreferencesHWND = NULL;
static bool gAudioAppliedInPreferences = false;

#if !defined NO_IGRAPHICS
#include "IGraphics.h"
using namespace igraphics;
#endif

#if defined OS_MAC
extern int GetTitleBarOffset();
#endif

// check the input and output devices, find matching srs
void IPlugAPPHost::PopulateSampleRateList(HWND hwndDlg, RtAudio::DeviceInfo* inputDevInfo, RtAudio::DeviceInfo* outputDevInfo)
{
  WDL_String buf;

  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_SR,CB_RESETCONTENT,0,0);

  std::vector<int> matchedSRs;

  if (inputDevInfo->probed && outputDevInfo->probed)
  {
    for (int i=0; i<inputDevInfo->sampleRates.size(); i++)
    {
      for (int j=0; j<outputDevInfo->sampleRates.size(); j++)
      {
        if(inputDevInfo->sampleRates[i] == outputDevInfo->sampleRates[j])
          matchedSRs.push_back(inputDevInfo->sampleRates[i]);
      }
    }
  }

  for (int k=0; k<matchedSRs.size(); k++)
  {
    buf.SetFormatted(20, "%i", matchedSRs[k]);
    SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_SR,CB_ADDSTRING,0,(LPARAM)buf.Get());
    SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_SR,CB_SETITEMDATA,k,(LPARAM)matchedSRs[k]);
  }
  
  WDL_String str;
  str.SetFormatted(32, "%i", mState.mAudioSR);

  LRESULT sridx = SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_SR, CB_FINDSTRINGEXACT, -1, (LPARAM) str.Get());
  SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_SR, CB_SETCURSEL, sridx, 0);
}

void IPlugAPPHost::PopulateAudioInputList(HWND hwndDlg, RtAudio::DeviceInfo* info)
{
  if(!info->probed)
    return;

  WDL_String buf;

  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_IN_L,CB_RESETCONTENT,0,0);

  // VoLum: populate every available device input channel (was off-by-one:
  // upstream looped `i < inputChannels - 1` and added the last entry to the
  // R combo only via a "// TEMP" hack, so the visible input combo was always
  // missing the device's last input).
  for (int i = 0; i < (int) info->inputChannels; i++)
  {
    buf.SetFormatted(20, "%i", i+1);
    SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_IN_L,CB_ADDSTRING,0,(LPARAM)buf.Get());
  }

  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_IN_L,CB_SETCURSEL, mState.mAudioInChanL - 1, 0);
  mState.mAudioInChanR = mState.mAudioInChanL;
}

void IPlugAPPHost::PopulateAudioOutputList(HWND hwndDlg, RtAudio::DeviceInfo* info)
{
  if(!info->probed)
    return;

  WDL_String buf;

  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_OUT_L,CB_RESETCONTENT,0,0);
  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_OUT_R,CB_RESETCONTENT,0,0);

  // VoLum: same off-by-one fix as PopulateAudioInputList.
  for (int i = 0; i < (int) info->outputChannels; i++)
  {
    buf.SetFormatted(20, "%i", i+1);
    SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_OUT_L,CB_ADDSTRING,0,(LPARAM)buf.Get());
    SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_OUT_R,CB_ADDSTRING,0,(LPARAM)buf.Get());
  }

  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_OUT_L,CB_SETCURSEL, mState.mAudioOutChanL - 1, 0);
  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_OUT_R,CB_SETCURSEL, mState.mAudioOutChanR - 1, 0);
}

// This has to get called after any change to audio driver/in dev/out dev
void IPlugAPPHost::PopulateDriverSpecificControls(HWND hwndDlg)
{
#ifdef OS_WIN
  int driverType = (int) SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_DRIVER, CB_GETCURSEL, 0, 0);
  if(driverType == kDeviceASIO)
  {
    ComboBox_Enable(GetDlgItem(hwndDlg, IDC_COMBO_AUDIO_IN_DEV), FALSE);
    Button_Enable(GetDlgItem(hwndDlg, IDC_BUTTON_OS_DEV_SETTINGS), TRUE);
  }
  else
  {
    ComboBox_Enable(GetDlgItem(hwndDlg, IDC_COMBO_AUDIO_IN_DEV), TRUE);
    Button_Enable(GetDlgItem(hwndDlg, IDC_BUTTON_OS_DEV_SETTINGS), FALSE);
  }
#endif

  int indevidx = 0;
  int outdevidx = 0;

  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_IN_DEV,CB_RESETCONTENT,0,0);
  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_OUT_DEV,CB_RESETCONTENT,0,0);

  for (int i = 0; i<mAudioInputDevs.size(); i++)
  {
    SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_IN_DEV,CB_ADDSTRING,0,(LPARAM)GetAudioDeviceName(mAudioInputDevs[i]).c_str());

    if(!strcmp(GetAudioDeviceName(mAudioInputDevs[i]).c_str(), mState.mAudioInDev.Get()))
      indevidx = i;
  }

  for (int i = 0; i<mAudioOutputDevs.size(); i++)
  {
    SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_OUT_DEV,CB_ADDSTRING,0,(LPARAM)GetAudioDeviceName(mAudioOutputDevs[i]).c_str());

    if(!strcmp(GetAudioDeviceName(mAudioOutputDevs[i]).c_str(), mState.mAudioOutDev.Get()))
      outdevidx = i;
  }

#ifdef OS_WIN
  // VoLum: under ASIO the runtime opens one driver for both directions (see
  // TryToChangeAudio), so the input side of this dialog has to describe that same
  // *device* - not the same list position. mAudioInputDevs and mAudioOutputDevs are
  // filtered independently and need not agree on order or length, so selecting
  // outdevidx in the input combo could name a third device, or nothing at all when
  // the input list is shorter. Resolve by device id instead.
  if (driverType == kDeviceASIO && mAudioOutputDevs.size())
  {
    for (int i = 0; i < mAudioInputDevs.size(); i++)
    {
      if (mAudioInputDevs[i] == mAudioOutputDevs[outdevidx])
      {
        indevidx = i;
        break;
      }
    }
  }
#endif

  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_IN_DEV,CB_SETCURSEL, indevidx, 0);
  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_OUT_DEV,CB_SETCURSEL, outdevidx, 0);

  RtAudio::DeviceInfo inputDevInfo;
  RtAudio::DeviceInfo outputDevInfo;

  // Output is probed first so the input side can reuse the result when both
  // directions are the same device. That is not just an optimisation: calling
  // getDeviceInfo twice on one ASIO device id is the pattern this fork already
  // blames for driver-side heap corruption (see the comment in InitAudio), and
  // after the fix above the ASIO case always names the same device twice.
  if (mDAC && mAudioOutputDevs.size())
  {
    outputDevInfo = mDAC->getDeviceInfo(mAudioOutputDevs[outdevidx]);
    PopulateAudioOutputList(hwndDlg, &outputDevInfo);
  }

  if (mDAC && mAudioInputDevs.size())
  {
    if (mAudioOutputDevs.size() && mAudioInputDevs[indevidx] == mAudioOutputDevs[outdevidx])
      inputDevInfo = outputDevInfo;
    else
      inputDevInfo = mDAC->getDeviceInfo(mAudioInputDevs[indevidx]);

    PopulateAudioInputList(hwndDlg, &inputDevInfo);
  }

  PopulateSampleRateList(hwndDlg, &inputDevInfo, &outputDevInfo);
}

void IPlugAPPHost::PopulateAudioDialogs(HWND hwndDlg)
{
  PopulateDriverSpecificControls(hwndDlg);

//  if (mState.mAudioInIsMono)
//  {
//    SendDlgItemMessage(hwndDlg,IDC_CB_MONO_INPUT,BM_SETCHECK, BST_CHECKED,0);
//  }
//  else
//  {
//    SendDlgItemMessage(hwndDlg,IDC_CB_MONO_INPUT,BM_SETCHECK, BST_UNCHECKED,0);
//  }

//  Populate buffer size combobox
  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_BUF_SIZE,CB_RESETCONTENT,0,0);
  const auto bufferChoices = VoLumBufferSizeChoices(mState.mBufferSize);
  for (std::size_t i = 0; i < bufferChoices.size(); ++i)
  {
    WDL_String choice;
    choice.SetFormatted(32, "%u", bufferChoices[i]);
    const LRESULT idx = SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_BUF_SIZE,CB_ADDSTRING,0,(LPARAM)choice.Get());
    SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_BUF_SIZE,CB_SETITEMDATA,idx,(LPARAM)bufferChoices[i]);
  }
  
  WDL_String str;
  str.SetFormatted(32, "%i", mState.mBufferSize);

  LRESULT iovsidx = SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_BUF_SIZE, CB_FINDSTRINGEXACT, -1, (LPARAM) str.Get());
  SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_BUF_SIZE, CB_SETCURSEL, iovsidx, 0);
}

bool IPlugAPPHost::PopulateMidiDialogs(HWND hwndDlg)
{
  if ( !mMidiIn || !mMidiOut )
    return false;
  else
  {
    SendDlgItemMessage(hwndDlg,IDC_COMBO_MIDI_IN_DEV,CB_RESETCONTENT,0,0);
    for (int i=0; i<mMidiInputDevNames.size(); i++ )
    {
      SendDlgItemMessage(hwndDlg,IDC_COMBO_MIDI_IN_DEV,CB_ADDSTRING,0,(LPARAM)mMidiInputDevNames[i].c_str());
    }

    LRESULT indevidx = GetMIDIPortNumber(ERoute::kInput, mState.mMidiInDev.Get(), mState.mMidiInDevNameIsStable);
    if (indevidx == -1)
      indevidx = 0;

    SendDlgItemMessage(hwndDlg,IDC_COMBO_MIDI_IN_DEV,CB_SETCURSEL, indevidx, 0);

    // VoLum: Preferences only offers the MIDI input port. VoLum sends no MIDI, and
    // MIDICallback never filters on the input channel, so those three combos are gone
    // from main.rc. Their stored values stay as they are.
    if (!GetDlgItem(hwndDlg, IDC_COMBO_MIDI_OUT_DEV))
      return true;

    SendDlgItemMessage(hwndDlg,IDC_COMBO_MIDI_OUT_DEV,CB_RESETCONTENT,0,0);
    for (int i=0; i<mMidiOutputDevNames.size(); i++ )
    {
      SendDlgItemMessage(hwndDlg,IDC_COMBO_MIDI_OUT_DEV,CB_ADDSTRING,0,(LPARAM)mMidiOutputDevNames[i].c_str());
    }

    LRESULT outdevidx = GetMIDIPortNumber(ERoute::kOutput, mState.mMidiOutDev.Get(), mState.mMidiOutDevNameIsStable);
    if (outdevidx == -1)
      outdevidx = 0;

    SendDlgItemMessage(hwndDlg,IDC_COMBO_MIDI_OUT_DEV,CB_SETCURSEL, outdevidx, 0);

    // Populate MIDI channel dialogs

    SendDlgItemMessage(hwndDlg,IDC_COMBO_MIDI_IN_CHAN,CB_ADDSTRING,0,(LPARAM)"all");
    SendDlgItemMessage(hwndDlg,IDC_COMBO_MIDI_OUT_CHAN,CB_ADDSTRING,0,(LPARAM)"all");

    WDL_String buf;

    for (int i=0; i<16; i++)
    {
      buf.SetFormatted(20, "%i", i+1);
      SendDlgItemMessage(hwndDlg,IDC_COMBO_MIDI_IN_CHAN,CB_ADDSTRING,0,(LPARAM)buf.Get());
      SendDlgItemMessage(hwndDlg,IDC_COMBO_MIDI_OUT_CHAN,CB_ADDSTRING,0,(LPARAM)buf.Get());
    }

    SendDlgItemMessage(hwndDlg,IDC_COMBO_MIDI_IN_CHAN,CB_SETCURSEL, (LPARAM)mState.mMidiInChan, 0);
    SendDlgItemMessage(hwndDlg,IDC_COMBO_MIDI_OUT_CHAN,CB_SETCURSEL, (LPARAM)mState.mMidiOutChan, 0);

    return true;
  }
}

#ifdef OS_WIN
void IPlugAPPHost::PopulatePreferencesDialog(HWND hwndDlg)
{
  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_DRIVER,CB_ADDSTRING,0,(LPARAM)"DirectSound");
  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_DRIVER,CB_ADDSTRING,0,(LPARAM)"ASIO");
  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_DRIVER,CB_SETCURSEL, mState.mAudioDriverType, 0);

  PopulateAudioDialogs(hwndDlg);
  PopulateMidiDialogs(hwndDlg);
}

#elif defined OS_MAC
void IPlugAPPHost::PopulatePreferencesDialog(HWND hwndDlg)
{
  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_DRIVER,CB_ADDSTRING,0,(LPARAM)"CoreAudio");
  //SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_DRIVER,CB_ADDSTRING,0,(LPARAM)"Jack");
  SendDlgItemMessage(hwndDlg,IDC_COMBO_AUDIO_DRIVER,CB_SETCURSEL, mState.mAudioDriverType, 0);

  PopulateAudioDialogs(hwndDlg);
  PopulateMidiDialogs(hwndDlg);
}
#else
  #error NOT IMPLEMENTED
#endif

// VoLum: an ASIO driver is free to open at a rate other than the one it was asked
// for, and several do - the clock is set in their own control panel, or the device
// was left somewhere else by another application. Preferences shows the rate that
// was actually opened, which is the truth but reads as a broken control: you choose
// 96000, the box goes to 88200, and nothing explains it. Say it out loud instead.
static void ReportSampleRateSubstitution(HWND hwndDlg, IPlugAPPHost* pAppHost)
{
  uint32_t requested = 0, actual = 0;
  if (!pAppHost || !pAppHost->TakeSampleRateSubstitution(requested, actual))
    return;

  WDL_String msg;
  msg.SetFormatted(512,
                   "The audio driver did not accept %u Hz.\n\n"
                   "It opened the device at %u Hz instead, and " BUNDLE_NAME " is now running at that rate. "
                   "Sample rates on ASIO devices are usually set in the driver's own control panel - "
                   "try Device Settings if you need a different one.",
                   requested, actual);

  // Captioned like the other audio notices ("Audio Error", "Graphics Error") rather
  // than BUNDLE_NAME, which the main window already uses and which would make this
  // box indistinguishable from it to anything enumerating windows, tests included.
  MessageBox(hwndDlg, msg.Get(), "Sample Rate", MB_OK | MB_ICONINFORMATION);
}

WDL_DLGRET IPlugAPPHost::PreferencesDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  IPlugAPPHost* _this = sInstance.get();
  AppState& mState = _this->mState;
  AppState& mTempState = _this->mTempState;
  AppState& mActiveState = _this->mActiveState;

  auto getComboString = [&](WDL_String& str, int item, WPARAM idx) {
    long len = (long) SendDlgItemMessage(hwndDlg, item, CB_GETLBTEXTLEN, idx, 0) + 1;
    std::string tempString(static_cast<std::size_t>(len), '\0');
    const LRESULT copied = SendDlgItemMessage(hwndDlg, item, CB_GETLBTEXT, idx, (LPARAM) tempString.data());
    if (copied >= 0)
      tempString.resize(static_cast<std::size_t>(copied));
    str.Set(tempString.c_str());
  };
  
#ifdef OS_WIN
  // VoLum: paints the dialog in VoLum's palette; see VoLumWinChrome.h.
  INT_PTR skinResult = 0;
  if (VoLumPrefsSkinMessage(hwndDlg, uMsg, wParam, lParam, skinResult))
    return skinResult;
#endif

  int v = 0;
  switch(uMsg)
  {
    case WM_INITDIALOG:
      gPreferencesHWND = hwndDlg;
      gAudioAppliedInPreferences = false;
#ifdef OS_WIN
      // VoLum: before Populate - the skin rebuilds the combos it fills.
      VoLumApplyDarkCaption(hwndDlg);
      VoLumPrefsSkinAttach(hwndDlg, gHINSTANCE, JOSEFINSANS_FN, JOSEFINSANS_BOLD_HEAVY_FN);
#endif
      mTempState = mState;
      // MIDI devices can appear and disappear while VoLum is running.
      _this->ProbeMidiIO();
      _this->PopulatePreferencesDialog(hwndDlg);
      
      return TRUE;

    case WM_DESTROY:
      gPreferencesHWND = NULL;
      gAudioAppliedInPreferences = false;
#ifdef OS_WIN
      VoLumPrefsSkinDetach(hwndDlg);
#endif
      return 0;

    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDOK:
        {
          const auto audioPlan = VoLumPlanDialogAudio(
            _this->AudioSettingsInStateAreEqual(mActiveState, mState),
            _this->AudioSettingsInStateAreEqual(mTempState, mState),
            _this->AudioSettingsInStateAreEqual(mActiveState, mTempState),
            gAudioAppliedInPreferences);
          if (audioPlan.restartOnOK)
          {
            _this->TryToChangeAudio(true);
            ReportSampleRateSubstitution(hwndDlg, _this);
          }
          if (_this->GetMIDIPortNumber(ERoute::kInput, mState.mMidiInDev.Get(), mState.mMidiInDevNameIsStable) > 0
              && _this->mMidiIn && !_this->mMidiIn->isPortOpen())
            _this->SelectMIDIDevice(ERoute::kInput, mState.mMidiInDev.Get(), mState.mMidiInDevNameIsStable);

          gPreferencesHWND = NULL;
          gAudioAppliedInPreferences = false;
          EndDialog(hwndDlg, IDOK); // INI file will be changed see MainDialogProc
          break;
        }
        case IDAPPLY:
          _this->TryToChangeAudio(true);
          gAudioAppliedInPreferences = true;
          // VoLum: the driver has the last word on the sample rate, so show what it
          // actually opened at rather than leaving the requested rate on screen.
          _this->PopulateAudioDialogs(hwndDlg);
          ReportSampleRateSubstitution(hwndDlg, _this);
          break;
        case IDCANCEL:
        {
          const auto audioPlan = VoLumPlanDialogAudio(
            _this->AudioSettingsInStateAreEqual(mActiveState, mState),
            _this->AudioSettingsInStateAreEqual(mTempState, mState),
            _this->AudioSettingsInStateAreEqual(mActiveState, mTempState),
            gAudioAppliedInPreferences);
          const bool midiChanged = !_this->MIDISettingsInStateAreEqual(mState, mTempState);
          gPreferencesHWND = NULL;
          gAudioAppliedInPreferences = false;
          EndDialog(hwndDlg, IDCANCEL);

          mState = mTempState;
          if (audioPlan.restartOnCancel)
          {
            _this->TryToChangeAudioDriverType();
            _this->ProbeAudioIO();
            _this->TryToChangeAudio();
          }
          if (midiChanged)
          {
            _this->SelectMIDIDevice(ERoute::kInput, mState.mMidiInDev.Get(), mState.mMidiInDevNameIsStable);
            _this->SelectMIDIDevice(ERoute::kOutput, mState.mMidiOutDev.Get(), mState.mMidiOutDevNameIsStable);
          }

          break;
        }

        case IDC_COMBO_AUDIO_DRIVER:
          if (HIWORD(wParam) == CBN_SELCHANGE)
          {
            v = (int) SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_DRIVER, CB_GETCURSEL, 0, 0);

            if(v != mState.mAudioDriverType)
            {
              mState.mAudioDriverType = v;

              if (!_this->TryToChangeAudioDriverType())
              {
                _this->RestoreActiveAudioStateAfterFailure("Audio driver is not available. Reverting to the previous working settings.");
                SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_DRIVER, CB_SETCURSEL, mState.mAudioDriverType, 0);
                _this->PopulateAudioDialogs(hwndDlg);
                break;
              }
              _this->ProbeAudioIO();

              if (!_this->mAudioInputDevs.size() && !_this->mAudioOutputDevs.size())
              {
                _this->RestoreActiveAudioStateAfterFailure("No audio devices are available for this driver. Reverting to the previous working settings.");
                SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_DRIVER, CB_SETCURSEL, mState.mAudioDriverType, 0);
                _this->PopulateAudioDialogs(hwndDlg);
                break;
              }

              if (_this->mAudioInputDevs.size())
                mState.mAudioInDev.Set(_this->GetAudioDeviceName(_this->mAudioInputDevs[0]).c_str());

              if (_this->mAudioOutputDevs.size())
                mState.mAudioOutDev.Set(_this->GetAudioDeviceName(_this->mAudioOutputDevs[0]).c_str());

              // Reset IO
              mState.mAudioOutChanL = 1;
              mState.mAudioOutChanR = 2;

              _this->PopulateAudioDialogs(hwndDlg);
            }
          }
          break;

        case IDC_COMBO_AUDIO_IN_DEV:
          if (HIWORD(wParam) == CBN_SELCHANGE)
          {
            int idx = (int) SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_IN_DEV, CB_GETCURSEL, 0, 0);
            getComboString(mState.mAudioInDev, IDC_COMBO_AUDIO_IN_DEV, idx);

            // Reset IO
            mState.mAudioInChanL = 1;
            mState.mAudioInChanR = 1;

            _this->PopulateDriverSpecificControls(hwndDlg);
          }
          break;

        case IDC_COMBO_AUDIO_OUT_DEV:
          if (HIWORD(wParam) == CBN_SELCHANGE)
          {
            int idx = (int) SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_OUT_DEV, CB_GETCURSEL, 0, 0);
            getComboString(mState.mAudioOutDev, IDC_COMBO_AUDIO_OUT_DEV, idx);

            // Reset IO
            mState.mAudioOutChanL = 1;
            mState.mAudioOutChanR = 2;

            _this->PopulateDriverSpecificControls(hwndDlg);
          }
          break;

        case IDC_COMBO_AUDIO_IN_L:
          if (HIWORD(wParam) == CBN_SELCHANGE)
          {
            // VoLum: the app consumes one mono guitar input and mirrors it to
            // all plugin inputs, so expose a single input-channel selector.
            mState.mAudioInChanL = (int) SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_IN_L, CB_GETCURSEL, 0, 0) + 1;
            mState.mAudioInChanR = mState.mAudioInChanL;
          }
          break;

        case IDC_COMBO_AUDIO_IN_R:
          if (HIWORD(wParam) == CBN_SELCHANGE)
          {
            mState.mAudioInChanR = mState.mAudioInChanL;
          }
          break;

        case IDC_COMBO_AUDIO_OUT_L:
          if (HIWORD(wParam) == CBN_SELCHANGE)
          {
            // VoLum: independent L/R output selection (same rationale as
            // IDC_COMBO_AUDIO_IN_L above).
            mState.mAudioOutChanL = (int) SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_OUT_L, CB_GETCURSEL, 0, 0) + 1;
          }
          break;

        case IDC_COMBO_AUDIO_OUT_R:
          // VoLum: same brace/scoping fix as IDC_COMBO_AUDIO_IN_R.
          if (HIWORD(wParam) == CBN_SELCHANGE)
          {
            mState.mAudioOutChanR = (int) SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_OUT_R, CB_GETCURSEL, 0, 0) + 1;
          }
          break;

//        case IDC_CB_MONO_INPUT:
//          if (SendDlgItemMessage(hwndDlg,IDC_CB_MONO_INPUT, BM_GETCHECK, 0, 0) == BST_CHECKED)
//            mState.mAudioInIsMono = 1;
//          else
//            mState.mAudioInIsMono = 0;
//          break;

        case IDC_COMBO_AUDIO_BUF_SIZE: // follow through
          if (HIWORD(wParam) == CBN_SELCHANGE)
          {
            int iovsidx = (int) SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_BUF_SIZE, CB_GETCURSEL, 0, 0);
            mState.mBufferSize = static_cast<uint32_t>(
              SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_BUF_SIZE, CB_GETITEMDATA, iovsidx, 0));
          }
          break;
        case IDC_COMBO_AUDIO_SR:
          if (HIWORD(wParam) == CBN_SELCHANGE)
          {
            int idx = (int) SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_SR, CB_GETCURSEL, 0, 0);
            mState.mAudioSR = (uint32_t) SendDlgItemMessage(hwndDlg, IDC_COMBO_AUDIO_SR, CB_GETITEMDATA, idx, 0);
          }
          break;

        case IDC_BUTTON_OS_DEV_SETTINGS:
          if (HIWORD(wParam) == BN_CLICKED) {
            #ifdef OS_WIN
            // VoLum: mDAC is legitimately null when the ASIO driver failed to
            // instantiate (TryToChangeAudioDriverType leaves it null, and
            // RestoreActiveAudioStateAfterFailure returns without recreating it),
            // while PopulateDriverSpecificControls enables this button purely
            // from the driver combo. Dereferencing it there crashed the app in
            // exactly the situation the user opened Preferences to repair.
            if( (_this->mState.mAudioDriverType == kDeviceASIO) && _this->mDAC && (_this->mDAC->isStreamRunning() == true)) // TODO: still not right
              ASIOControlPanel();
            #elif defined OS_MAC
            if(SWELL_GetOSXVersion() >= 0x1200) {
              system("open \"/System/Applications/Utilities/Audio MIDI Setup.app\"");
            } else {
              system("open \"/Applications/Utilities/Audio MIDI Setup.app\"");
            }
            #else
              #error NOT IMPLEMENTED
            #endif
          }
          break;

        case IDC_COMBO_MIDI_IN_DEV:
          if (HIWORD(wParam) == CBN_SELCHANGE)
          {
            int idx = (int) SendDlgItemMessage(hwndDlg, IDC_COMBO_MIDI_IN_DEV, CB_GETCURSEL, 0, 0);
            getComboString(mState.mMidiInDev, IDC_COMBO_MIDI_IN_DEV, idx);
            mState.mMidiInDevNameIsStable = true;
            _this->SelectMIDIDevice(ERoute::kInput, mState.mMidiInDev.Get(), true);
          }
          break;

        case IDC_COMBO_MIDI_OUT_DEV:
          if (HIWORD(wParam) == CBN_SELCHANGE)
          {
            int idx = (int) SendDlgItemMessage(hwndDlg, IDC_COMBO_MIDI_OUT_DEV, CB_GETCURSEL, 0, 0);
            getComboString(mState.mMidiOutDev, IDC_COMBO_MIDI_OUT_DEV, idx);
            mState.mMidiOutDevNameIsStable = true;
            _this->SelectMIDIDevice(ERoute::kOutput, mState.mMidiOutDev.Get(), true);
          }
          break;

        case IDC_COMBO_MIDI_IN_CHAN:
          if (HIWORD(wParam) == CBN_SELCHANGE)
            mState.mMidiInChan = (int) SendDlgItemMessage(hwndDlg, IDC_COMBO_MIDI_IN_CHAN, CB_GETCURSEL, 0, 0);
          break;

        case IDC_COMBO_MIDI_OUT_CHAN:
          if (HIWORD(wParam) == CBN_SELCHANGE)
            mState.mMidiOutChan = (int) SendDlgItemMessage(hwndDlg, IDC_COMBO_MIDI_OUT_CHAN, CB_GETCURSEL, 0, 0);
          break;

        default:
          break;
      }
      break;
    default:
      return FALSE;
  }
  return TRUE;
}

static void ClientResize(HWND hWnd, int nWidth, int nHeight)
{
  RECT rcClient, rcWindow;
  POINT ptDiff;
  int screenwidth, screenheight;
  int x, y;
  
  screenwidth  = GetSystemMetrics(SM_CXSCREEN);
  screenheight = GetSystemMetrics(SM_CYSCREEN);
  x = (screenwidth / 2) - (nWidth / 2);
  y = (screenheight / 2) - (nHeight / 2);
  
  GetClientRect(hWnd, &rcClient);
  GetWindowRect(hWnd, &rcWindow);

  ptDiff.x = (rcWindow.right - rcWindow.left) - rcClient.right;
  ptDiff.y = (rcWindow.bottom - rcWindow.top) - rcClient.bottom;
  
  SetWindowPos(hWnd, 0, x, y, nWidth + ptDiff.x, nHeight + ptDiff.y, 0);
}

#ifdef OS_WIN 
extern float GetScaleForHWND(HWND hWnd);
#endif

//static
WDL_DLGRET IPlugAPPHost::MainDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  IPlugAPPHost* pAppHost = IPlugAPPHost::sInstance.get();

  int width = 0;
  int height = 0;

  switch (uMsg)
  {
    case WM_INITDIALOG:
    {
      gHWND = hwndDlg;
      IPlugAPP* pPlug = pAppHost->GetPlug();

      if (!pAppHost->OpenWindow(gHWND))
        DBGMSG("couldn't attach gui\n");

      width = pPlug->GetEditorWidth();
      height = pPlug->GetEditorHeight();

      ClientResize(hwndDlg, width, height);

#ifdef OS_WIN
      VoLumApplyDarkCaption(hwndDlg);
#endif
      ShowWindow(hwndDlg, SW_SHOW);

      // VoLum: drives IPlugAPPHost::PollAudioStatus - reports a startup audio failure
      // that had no window to appear in, and reopens the stream when the driver changes
      // the sample rate from its own control panel. Half a second is well under the
      // point where silence reads as a hang, and the poll does nothing at all in the
      // usual case.
      SetTimer(hwndDlg, kAudioStatusTimerID, 500, NULL);
      return 1;
    }
    case WM_TIMER:
      // pAppHost is null once WM_DESTROY has released the instance, and a timer
      // message posted before KillTimer can still be waiting in the queue.
      if (wParam == kAudioStatusTimerID && gPreferencesHWND == NULL && pAppHost)
        pAppHost->PollAudioStatus();
      return 0;
    case WM_DESTROY:
      KillTimer(hwndDlg, kAudioStatusTimerID);
      pAppHost->CloseWindow();
      gHWND = NULL;
      IPlugAPPHost::sInstance = nullptr;
      
      #ifdef OS_WIN
      PostQuitMessage(0);
      #else
      SWELL_PostQuitMessage(hwndDlg);
      #endif

      return 0;
    case WM_CLOSE:
      DestroyWindow(hwndDlg);
      return 0;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case ID_QUIT:
        {
          DestroyWindow(hwndDlg);
          return 0;
        }
        case ID_ABOUT:
        {
          IPlugAPP* pPlug = pAppHost->GetPlug();
          
          bool pluginOpensAboutBox = pPlug->OnHostRequestingAboutBox();
          
          if (pluginOpensAboutBox == false)
          {
            WDL_String info;
            info.Append(PLUG_COPYRIGHT_STR"\nBuilt on " __DATE__);
            MessageBox(hwndDlg, info.Get(), PLUG_NAME, MB_OK);
          }

          return 0;
        }
        case ID_HELP:
        {
          IPlugAPP* pPlug = pAppHost->GetPlug();

          bool pluginOpensHelp = pPlug->OnHostRequestingProductHelp();

          if (pluginOpensHelp == false)
          {
#ifdef OS_WIN
            const auto result = ShellExecuteA(hwndDlg, "open", kVoLumManualURL, nullptr, nullptr, SW_SHOWNORMAL);
            if ((INT_PTR) result <= 32)
              MessageBox(hwndDlg, "Could not open the manual in your browser.", PLUG_NAME, MB_OK);
#elif defined OS_MAC
            WDL_String command;
            command.SetFormatted(1024, "open \"%s\"", kVoLumManualURL);
            system(command.Get());
#else
            MessageBox(hwndDlg, kVoLumManualURL, PLUG_NAME, MB_OK);
#endif
          }
          return 0;
        }
        case ID_PREFERENCES:
        {
          INT_PTR ret = DialogBox(gHINSTANCE, MAKEINTRESOURCE(IDD_DIALOG_PREF), hwndDlg, IPlugAPPHost::PreferencesDlgProc);
          gPreferencesHWND = NULL;

          if(ret == IDOK)
            pAppHost->UpdateINI();

          return 0;
        }
#if defined _DEBUG && !defined NO_IGRAPHICS
        case ID_LIVE_EDIT:
        {
          IGEditorDelegate* pPlug = dynamic_cast<IGEditorDelegate*>(pAppHost->GetPlug());
        
          if(pPlug)
          {
            IGraphics* pGraphics = pPlug->GetUI();
            
            if(pGraphics)
            {
              bool enabled = pGraphics->LiveEditEnabled();
              pGraphics->EnableLiveEdit(!enabled);
              CheckMenuItem(GET_MENU(), ID_LIVE_EDIT, (MF_BYCOMMAND | enabled) ? MF_UNCHECKED : MF_CHECKED);
            }
          }
          
          return 0;
        }
        case ID_SHOW_DRAWN:
        {
          IGEditorDelegate* pPlug = dynamic_cast<IGEditorDelegate*>(pAppHost->GetPlug());
          
          if(pPlug)
          {
            IGraphics* pGraphics = pPlug->GetUI();
            
            if(pGraphics)
            {
              bool enabled = pGraphics->ShowAreaDrawnEnabled();
              pGraphics->ShowAreaDrawn(!enabled);
              CheckMenuItem(GET_MENU(), ID_SHOW_DRAWN, (MF_BYCOMMAND | enabled) ? MF_UNCHECKED : MF_CHECKED);
            }
          }
          
          return 0;
        }
        case ID_SHOW_BOUNDS:
        {
          IGEditorDelegate* pPlug = dynamic_cast<IGEditorDelegate*>(pAppHost->GetPlug());
          
          if(pPlug)
          {
            IGraphics* pGraphics = pPlug->GetUI();
            
            if(pGraphics)
            {
              bool enabled = pGraphics->ShowControlBoundsEnabled();
              pGraphics->ShowControlBounds(!enabled);
              CheckMenuItem(GET_MENU(), ID_SHOW_BOUNDS, (MF_BYCOMMAND | enabled) ? MF_UNCHECKED : MF_CHECKED);
            }
          }
          
          return 0;
        }
        case ID_SHOW_FPS:
        {
          IGEditorDelegate* pPlug = dynamic_cast<IGEditorDelegate*>(pAppHost->GetPlug());
          
          if(pPlug)
          {
            IGraphics* pGraphics = pPlug->GetUI();
            
            if(pGraphics)
            {
              bool enabled = pGraphics->ShowingFPSDisplay();
              pGraphics->ShowFPSDisplay(!enabled);
              CheckMenuItem(GET_MENU(), ID_SHOW_FPS, (MF_BYCOMMAND | enabled) ? MF_UNCHECKED : MF_CHECKED);
            }
          }
          
          return 0;
        }
#endif
      }
      return 0;
    case WM_GETMINMAXINFO:
    {
      if(!pAppHost)
        return 1;
      
      IPlugAPP* pPlug = pAppHost->GetPlug();

      MINMAXINFO* mmi = (MINMAXINFO*) lParam;
      mmi->ptMinTrackSize.x = pPlug->GetMinWidth();
      mmi->ptMinTrackSize.y = pPlug->GetMinHeight();
      mmi->ptMaxTrackSize.x = pPlug->GetMaxWidth();
      mmi->ptMaxTrackSize.y = pPlug->GetMaxHeight();

#ifdef OS_WIN 
      float scale = GetScaleForHWND(hwndDlg);
      mmi->ptMinTrackSize.x = static_cast<LONG>(static_cast<float>(mmi->ptMinTrackSize.x) * scale);
      mmi->ptMinTrackSize.y = static_cast<LONG>(static_cast<float>(mmi->ptMinTrackSize.y) * scale);
      mmi->ptMaxTrackSize.x = static_cast<LONG>(static_cast<float>(mmi->ptMaxTrackSize.x) * scale);
      mmi->ptMaxTrackSize.y = static_cast<LONG>(static_cast<float>(mmi->ptMaxTrackSize.y) * scale);
#endif
      
      return 0;
    }
#ifdef OS_WIN
    case WM_DPICHANGED:
    {
      WORD dpi = HIWORD(wParam);
      RECT* rect = (RECT*)lParam;
      float scale = GetScaleForHWND(hwndDlg);

      POINT ptDiff;
      RECT rcClient;
      RECT rcWindow;

      GetClientRect(hwndDlg, &rcClient);
      GetWindowRect(hwndDlg, &rcWindow);

      ptDiff.x = (rcWindow.right - rcWindow.left) - rcClient.right;
      ptDiff.y = (rcWindow.bottom - rcWindow.top) - rcClient.bottom;

#ifndef NO_IGRAPHICS
      IGEditorDelegate* pPlug = dynamic_cast<IGEditorDelegate*>(pAppHost->GetPlug());

      if (pPlug)
      {
        IGraphics* pGraphics = pPlug->GetUI();

        if (pGraphics)
        {
          pGraphics->SetScreenScale(scale);
        }
      }
#else
      IEditorDelegate* pPlug = dynamic_cast<IEditorDelegate*>(pAppHost->GetPlug());
#endif

      int w = pPlug->GetEditorWidth(); 
      int h = pPlug->GetEditorHeight();

      SetWindowPos(hwndDlg, 0, rect->left, rect->top, w + ptDiff.x, h + ptDiff.y, 0);

      return 0;
    }
#endif
    case WM_SIZE:
    {
      IPlugAPP* pPlug = pAppHost->GetPlug();

      switch (LOWORD(wParam))
      {
      case SIZE_RESTORED:
      case SIZE_MAXIMIZED:
      {
        RECT r;
        GetClientRect(hwndDlg, &r);
        float scale = 1.f;
        #ifdef OS_WIN 
        scale = GetScaleForHWND(hwndDlg);
        #endif
        pPlug->OnParentWindowResize(static_cast<int>(r.right / scale), static_cast<int>(r.bottom / scale));
        return 1;
      }
      default:
        return 0;
      }
    }
  }
  return 0;
}
