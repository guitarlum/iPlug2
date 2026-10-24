/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

#include "IPlugPlatform.h"

#if defined OS_LINUX

#include "Linux/IPlugLinuxRunLoop.h"

namespace Steinberg
{
class FUnknown;
}

BEGIN_IPLUG_NAMESPACE

/** Called from IPluginFactory3::setHostContext before any instance exists. */
void IPlugLinuxOnFactoryHostContext(Steinberg::FUnknown* context);
void IPlugLinuxReleaseFactoryHostContext();

/** One retain per component, released from terminate(). */
void IPlugLinuxAdoptComponentContext(void* instance, Steinberg::FUnknown* context);
void IPlugLinuxReleaseComponentContext(void* instance);

/**
 * Shared adapter for a host IRunLoop. Does not retain the binding table;
 * the caller retains and, when release drops the last ref, deletes it.
 */
IPlugLinuxRunLoop* IPlugLinuxVst3Adapter(Steinberg::FUnknown* context);
void IPlugLinuxDeleteVst3Adapter(IPlugLinuxRunLoop* adapter);

END_IPLUG_NAMESPACE

#endif
