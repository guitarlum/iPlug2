/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#include "IGraphicsLinuxKeys.h"

#include <X11/keysym.h>

#include "IPlugConstants.h"

namespace iplug
{
namespace igraphics
{

int KeysymToVK(unsigned long ks)
{
  switch (ks)
  {
  case XK_BackSpace:
    return kVK_BACK;
  case XK_Tab:
  case XK_ISO_Left_Tab:
    return kVK_TAB;
  case XK_Clear:
    return kVK_CLEAR;
  case XK_Return:
  case XK_KP_Enter:
    return kVK_RETURN;
  case XK_Pause:
    return kVK_PAUSE;
  case XK_Caps_Lock:
    return kVK_CAPITAL;
  case XK_Escape:
    return kVK_ESCAPE;
  case XK_space:
  case XK_KP_Space:
    return kVK_SPACE;
  case XK_Prior:
  case XK_KP_Prior:
    return kVK_PRIOR;
  case XK_Next:
  case XK_KP_Next:
    return kVK_NEXT;
  case XK_End:
  case XK_KP_End:
    return kVK_END;
  case XK_Home:
  case XK_KP_Home:
    return kVK_HOME;
  case XK_Left:
  case XK_KP_Left:
    return kVK_LEFT;
  case XK_Up:
  case XK_KP_Up:
    return kVK_UP;
  case XK_Right:
  case XK_KP_Right:
    return kVK_RIGHT;
  case XK_Down:
  case XK_KP_Down:
    return kVK_DOWN;
  case XK_Select:
    return kVK_SELECT;
  case XK_Print:
    return kVK_SNAPSHOT;
  case XK_Insert:
  case XK_KP_Insert:
    return kVK_INSERT;
  case XK_Delete:
  case XK_KP_Delete:
    return kVK_DELETE;
  case XK_Help:
    return kVK_HELP;
  case XK_Super_L:
  case XK_Super_R:
    return kVK_LWIN;
  case XK_KP_Multiply:
    return kVK_MULTIPLY;
  case XK_KP_Add:
    return kVK_ADD;
  case XK_KP_Separator:
    return kVK_SEPARATOR;
  case XK_KP_Subtract:
    return kVK_SUBTRACT;
  case XK_KP_Decimal:
    return kVK_DECIMAL;
  case XK_KP_Divide:
    return kVK_DIVIDE;
  case XK_Num_Lock:
    return kVK_NUMLOCK;
  case XK_Scroll_Lock:
    return kVK_SCROLL;
  case XK_Shift_L:
  case XK_Shift_R:
    return kVK_SHIFT;
  case XK_Control_L:
  case XK_Control_R:
    return kVK_CONTROL;
  case XK_Alt_L:
  case XK_Alt_R:
  case XK_Meta_L:
  case XK_Meta_R:
  case XK_ISO_Level3_Shift:
    return kVK_MENU;
  default:
    break;
  }

  if (ks >= XK_0 && ks <= XK_9)
    return kVK_0 + static_cast<int>(ks - XK_0);
  if (ks >= XK_a && ks <= XK_z)
    return kVK_A + static_cast<int>(ks - XK_a);
  if (ks >= XK_A && ks <= XK_Z)
    return kVK_A + static_cast<int>(ks - XK_A);
  if (ks >= XK_KP_0 && ks <= XK_KP_9)
    return kVK_NUMPAD0 + static_cast<int>(ks - XK_KP_0);
  if (ks >= XK_F1 && ks <= XK_F24)
    return kVK_F1 + static_cast<int>(ks - XK_F1);
  return kVK_NONE;
}

bool KeysymToUTF8(unsigned long ks, char utf8[5])
{
  unsigned long cp = 0;
  if ((ks >= 0x20 && ks <= 0x7e) || (ks >= 0xa0 && ks <= 0xff)) // Latin-1 keysyms are their code point
    cp = ks;
  else if ((ks & 0xff000000UL) == 0x01000000UL) // Unicode keysyms
    cp = ks & 0x00ffffffUL;
  else if (ks >= XK_KP_0 && ks <= XK_KP_9)
    cp = '0' + (ks - XK_KP_0);
  else
  {
    switch (ks)
    {
    case XK_KP_Space:
      cp = ' ';
      break;
    case XK_KP_Multiply:
      cp = '*';
      break;
    case XK_KP_Add:
      cp = '+';
      break;
    case XK_KP_Subtract:
      cp = '-';
      break;
    case XK_KP_Decimal:
      cp = '.';
      break;
    case XK_KP_Divide:
      cp = '/';
      break;
    case XK_KP_Equal:
      cp = '=';
      break;
    case XK_EuroSign:
      cp = 0x20ac;
      break;
    default:
      break;
    }
  }

  int n = 0;
  if (cp >= 0x20 && cp < 0x80)
    utf8[n++] = static_cast<char>(cp);
  else if (cp >= 0x80 && cp < 0x800)
  {
    utf8[n++] = static_cast<char>(0xc0 | (cp >> 6));
    utf8[n++] = static_cast<char>(0x80 | (cp & 0x3f));
  }
  else if (cp >= 0x800 && cp < 0x10000 && (cp < 0xd800 || cp > 0xdfff))
  {
    utf8[n++] = static_cast<char>(0xe0 | (cp >> 12));
    utf8[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
    utf8[n++] = static_cast<char>(0x80 | (cp & 0x3f));
  }
  else if (cp >= 0x10000 && cp < 0x110000)
  {
    utf8[n++] = static_cast<char>(0xf0 | (cp >> 18));
    utf8[n++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
    utf8[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
    utf8[n++] = static_cast<char>(0x80 | (cp & 0x3f));
  }
  utf8[n] = '\0';
  return n > 0;
}

} // namespace igraphics
} // namespace iplug
