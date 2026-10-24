/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

/**
 * @file
 * X11 keysym to iPlug key translation. The VK table extends KeysymToVK in
 * IGraphicsLinux.cpp of iPlug2#1336 with the keypad, F13-F24 and lock keys.
 */

#include "IPlugPlatform.h"

BEGIN_IPLUG_NAMESPACE
BEGIN_IGRAPHICS_NAMESPACE

/** Windows-style virtual key (kVK_*) for an X11 keysym, or kVK_NONE. */
int KeysymToVK(unsigned long keysym);

/**
 * UTF-8 text a keysym types, as IKeyPress::utf8 expects (at most 4 bytes).
 * @return false (and an empty string) for keys that type nothing.
 */
bool KeysymToUTF8(unsigned long keysym, char utf8[5]);

END_IGRAPHICS_NAMESPACE
END_IPLUG_NAMESPACE
