/*
 ==============================================================================
 
 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers. 
 
 See LICENSE.txt for  more info.
 
 ==============================================================================
*/

#pragma once

#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "base/source/fstring.h"

#include "IPlugParameter.h"
#include "IPlugVST3_MidiParams.h"

BEGIN_IPLUG_NAMESPACE

/** VST3 parameter helper */
class IPlugVST3Parameter : public Steinberg::Vst::Parameter
{
public:
  IPlugVST3Parameter(IParam* pParam, Steinberg::Vst::ParamID tag, Steinberg::Vst::UnitID unitID)
  : mIPlugParam(pParam)
  {
    Steinberg::UString(info.title, str16BufferSize(Steinberg::Vst::String128)).assign(pParam->GetName());
    Steinberg::UString(info.units, str16BufferSize(Steinberg::Vst::String128)).assign(pParam->GetLabel());

    precision = pParam->GetDisplayPrecision();

    if (pParam->Type() != IParam::kTypeDouble)
      info.stepCount = pParam->GetRange();
    else
      info.stepCount = 0; // continuous

    Steinberg::int32 flags = 0;

    if (pParam->GetCanAutomate()) flags |= Steinberg::Vst::ParameterInfo::kCanAutomate;
    if (pParam->Type() == IParam::kTypeEnum) flags |= Steinberg::Vst::ParameterInfo::kIsList;

    info.defaultNormalizedValue = valueNormalized = pParam->ToNormalized(pParam->GetDefault());
    info.flags = flags;
    info.id = tag;
    info.unitId = unitID;
  }

  void toString(Steinberg::Vst::ParamValue valueNormalized, Steinberg::Vst::String128 string) const override
  {
    WDL_String display;
    mIPlugParam->GetDisplay(valueNormalized, true, display);
    Steinberg::UString(string, 128).fromAscii(display.Get());
  }

  bool fromString(const Steinberg::Vst::TChar* string, Steinberg::Vst::ParamValue& valueNormalized) const override
  {
    Steinberg::String str((Steinberg::Vst::TChar*) string);
    valueNormalized = mIPlugParam->ToNormalized(mIPlugParam->StringToValue(str.text8()));

    return true;
  }

  Steinberg::Vst::ParamValue toPlain(Steinberg::Vst::ParamValue normValue) const override
  {
    return mIPlugParam->FromNormalized(normValue);
  }

  Steinberg::Vst::ParamValue toNormalized(Steinberg::Vst::ParamValue plainValue) const override
  {
    return mIPlugParam->ToNormalized(plainValue);
  }

  OBJ_METHODS(IPlugVST3Parameter, Parameter)

protected:
  IParam* mIPlugParam = nullptr;
};

/** VST3 preset parameter helper */
class IPlugVST3PresetParameter : public Steinberg::Vst::Parameter
{
public:
  IPlugVST3PresetParameter(int nPresets)
  : Steinberg::Vst::Parameter(STR16("Preset"), kPresetParam, STR16(""), 0, nPresets - 1, Steinberg::Vst::ParameterInfo::kIsProgramChange)
  {}
  
  Steinberg::Vst::ParamValue toPlain(Steinberg::Vst::ParamValue valueNormalized) const override
  {
    return std::round(valueNormalized * info.stepCount);
  }
  
  Steinberg::Vst::ParamValue toNormalized(Steinberg::Vst::ParamValue plainValue) const override
  {
    return plainValue / info.stepCount;
  }
  
  OBJ_METHODS(IPlugVST3PresetParameter, Steinberg::Vst::Parameter)
};

/** VST3 per-MIDI-channel program change parameter. It is the parameter of that channel's program list,
 * which is how hosts deliver MIDI Program Change to a VST3 plug-in. It is not an iPlug preset. */
class IPlugVST3MIDIProgramParameter : public Steinberg::Vst::Parameter
{
public:
  IPlugVST3MIDIProgramParameter(int channel, Steinberg::Vst::UnitID unitID)
  {
    WDL_String title;
    title.SetFormatted(32, "MIDI Program CH%i", channel + 1);
    Steinberg::UString(info.title, str16BufferSize(Steinberg::Vst::String128)).fromAscii(title.Get());
    info.id = kVST3MIDIProgramParamStartIdx + channel;
    info.stepCount = kVST3MIDIProgramCount - 1;
    info.defaultNormalizedValue = 0.;
    // Not automatable: the VST3 spec forbids automating a program-change parameter that changes automatable parameters.
    info.flags = Steinberg::Vst::ParameterInfo::kIsList | Steinberg::Vst::ParameterInfo::kIsProgramChange;
    info.unitId = unitID;
  }

  void toString(Steinberg::Vst::ParamValue valueNormalized, Steinberg::Vst::String128 string) const override
  {
    WDL_String display;
    display.SetFormatted(32, "Program %i", VST3NormalizedToMIDI7Bit(valueNormalized));
    Steinberg::UString(string, 128).fromAscii(display.Get());
  }

  Steinberg::Vst::ParamValue toPlain(Steinberg::Vst::ParamValue valueNormalized) const override
  {
    return VST3NormalizedToMIDI7Bit(valueNormalized);
  }

  Steinberg::Vst::ParamValue toNormalized(Steinberg::Vst::ParamValue plainValue) const override
  {
    return plainValue / info.stepCount;
  }

  OBJ_METHODS(IPlugVST3MIDIProgramParameter, Steinberg::Vst::Parameter)
};

/** VST3 bypass parameter helper */
class IPlugVST3BypassParameter : public Steinberg::Vst::StringListParameter
{
public:
  IPlugVST3BypassParameter()
  : Steinberg::Vst::StringListParameter(STR16("Bypass"), kBypassParam, 0, Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsBypass | Steinberg::Vst::ParameterInfo::kIsList)
  {
    appendString(STR16("off"));
    appendString(STR16("on"));
  }
  
  OBJ_METHODS(IPlugVST3BypassParameter, StringListParameter)
};

END_IPLUG_NAMESPACE

