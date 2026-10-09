/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
 */

#pragma once

/**
 * @file
 * @brief Key filtering rules for ITextEntryControl that need no graphics context.
 * VoLum: kept in a header of its own so the rules can be unit tested.
 */

namespace iplug
{
namespace igraphics
{
/** A comma is the decimal separator on many keyboard layouts: the German numpad types ',' where the
 * US one types '.'. The numeric filter only let '.' through, so "7,5" arrived as "75".
 * Maps the comma to '.', so the entry holds one spelling whichever key was pressed.
 * @param key The character the key press produced
 * @return The character to enter in its place */
inline int NormalizeDecimalSeparatorKey(int key)
{
  return key == ',' ? '.' : key;
}
} // namespace igraphics
} // namespace iplug
