#pragma once

// VoLum: dark window chrome for the Windows standalone.
//
// - VoLumApplyDarkCaption: a dark title bar through DWM (VoLumWinChromeModel.h
//   decides which switch this Windows build has).
// - VoLumPrefsSkin*: paints the Preferences dialog in VoLum's palette and font. The
//   dialog keeps every control id and handler, so the audio code and the smoke
//   scripts that drive it by id are unchanged. main.rc keeps its stock control
//   styles: swell_resgen builds the macOS dialog from it, and macOS draws its own.
//
// Windows only. Every DWM / uxtheme entry point is looked up at run time, so the
// app still starts on a Windows that lacks them; it just keeps the light chrome.

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <string>
#include <vector>

#include "VoLumWinChromeModel.h"

namespace iplug
{
/** The real build number. GetVersionEx reports 9200 to an exe without a manifest. */
inline uint32_t VoLumWindowsBuild()
{
  static const uint32_t build = [] {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
      if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")))
        if (fn(&info) == 0)
          return static_cast<uint32_t>(info.dwBuildNumber);
    return 0u;
  }();
  return build;
}

inline HRESULT VoLumDwmSetAttribute(HWND hwnd, DWORD attr, const void* value, DWORD size)
{
  using Fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
  static const Fn fn = [] {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    return dwm ? reinterpret_cast<Fn>(GetProcAddress(dwm, "DwmSetWindowAttribute")) : nullptr;
  }();
  return fn ? fn(hwnd, attr, value, size) : E_NOTIMPL;
}

inline void VoLumApplyDarkCaption(HWND hwnd)
{
  const uint32_t build = VoLumWindowsBuild();
  const uint32_t attr = VoLumDarkCaptionAttribute(build);
  if (!hwnd || !attr)
    return;

  const BOOL dark = TRUE;
  VoLumDwmSetAttribute(hwnd, attr, &dark, sizeof(dark));
  if (VoLumSupportsCaptionColor(build))
  {
    const COLORREF caption = VoLumColorRef(volum_chrome::kBg);
    const COLORREF text = VoLumColorRef(volum_chrome::kTextMed);
    VoLumDwmSetAttribute(hwnd, kVoLumDwmCaptionColorAttr, &caption, sizeof(caption));
    VoLumDwmSetAttribute(hwnd, kVoLumDwmTextColorAttr, &text, sizeof(text));
  }
}

// ---- Preferences skin ------------------------------------------------------

namespace prefs_skin
{
struct Group
{
  RECT rect{};
  std::wstring caption;
};

struct State
{
  HWND dialog = nullptr;
  HFONT body = nullptr;
  HFONT bold = nullptr;
  std::vector<HANDLE> fontResources;
  HBRUSH panel = nullptr;
  HBRUSH well = nullptr;
  std::vector<Group> groups;
  HWND hot = nullptr;
  UINT fieldHeight = 0;
  UINT itemHeight = 0;
  int dpi = 96;
};

inline State& Skin()
{
  static State s;
  return s;
}

inline COLORREF Ref(VoLumRgb c)
{
  return static_cast<COLORREF>(VoLumColorRef(c));
}

inline int Px(int px96)
{
  return MulDiv(px96, Skin().dpi, 96);
}

inline int DpiFor(HWND hwnd)
{
  using Fn = UINT(WINAPI*)(HWND);
  static const Fn fn = [] {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    return user32 ? reinterpret_cast<Fn>(GetProcAddress(user32, "GetDpiForWindow")) : nullptr;
  }();
  if (fn)
    if (const UINT dpi = fn(hwnd))
      return static_cast<int>(dpi);
  HDC dc = GetDC(hwnd);
  const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
  if (dc)
    ReleaseDC(hwnd, dc);
  return dpi > 0 ? dpi : 96;
}

inline void SetTheme(HWND hwnd, const wchar_t* app)
{
  using Fn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
  static const Fn fn = [] {
    HMODULE uxtheme = LoadLibraryW(L"uxtheme.dll");
    return uxtheme ? reinterpret_cast<Fn>(GetProcAddress(uxtheme, "SetWindowTheme")) : nullptr;
  }();
  if (fn)
    fn(hwnd, app, nullptr);
}

inline void LoadFontResource(HINSTANCE inst, const char* name)
{
  if (!name)
    return;
  HRSRC res = FindResourceA(inst, name, "TTF");
  if (!res)
    return;
  HGLOBAL data = LoadResource(inst, res);
  void* bytes = data ? LockResource(data) : nullptr;
  if (!bytes)
    return;
  DWORD count = 0;
  if (HANDLE font = AddFontMemResourceEx(bytes, SizeofResource(inst, res), nullptr, &count))
    Skin().fontResources.push_back(font);
}

inline HFONT MakeFont(int points, int weight)
{
  const int height = -MulDiv(points, Skin().dpi, 72);
  auto make = [&](const wchar_t* face) {
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
  };
  HFONT font = make(L"Josefin Sans");
  // A missing private font is silently swapped for some other face; Segoe UI at
  // least reads like a Windows dialog instead of whatever GDI picked.
  HDC dc = GetDC(nullptr);
  wchar_t face[LF_FACESIZE] = {};
  HGDIOBJ old = SelectObject(dc, font);
  GetTextFaceW(dc, LF_FACESIZE, face);
  SelectObject(dc, old);
  ReleaseDC(nullptr, dc);
  if (wcscmp(face, L"Josefin Sans") != 0)
  {
    DeleteObject(font);
    font = make(L"Segoe UI");
  }
  return font;
}

inline UINT FontHeight(HFONT font)
{
  HDC dc = GetDC(nullptr);
  HGDIOBJ old = SelectObject(dc, font);
  TEXTMETRICW tm{};
  GetTextMetricsW(dc, &tm);
  SelectObject(dc, old);
  ReleaseDC(nullptr, dc);
  return static_cast<UINT>(tm.tmHeight);
}

inline std::wstring WindowText(HWND hwnd)
{
  const int len = GetWindowTextLengthW(hwnd);
  std::wstring text(static_cast<size_t>(len) + 1, L'\0');
  GetWindowTextW(hwnd, text.data(), len + 1);
  text.resize(static_cast<size_t>(len));
  return text;
}

inline std::wstring ComboText(HWND combo, int item)
{
  if (item < 0)
    return {};
  const LRESULT len = SendMessageW(combo, CB_GETLBTEXTLEN, static_cast<WPARAM>(item), 0);
  if (len <= 0)
    return {};
  std::wstring text(static_cast<size_t>(len) + 1, L'\0');
  SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(item), reinterpret_cast<LPARAM>(text.data()));
  text.resize(wcslen(text.c_str()));
  return text;
}

inline void Fill(HDC dc, const RECT& r, VoLumRgb c)
{
  HBRUSH brush = CreateSolidBrush(Ref(c));
  FillRect(dc, &r, brush);
  DeleteObject(brush);
}

inline void RoundBox(HDC dc, const RECT& r, VoLumRgb fill, VoLumRgb border, int radius, bool filled = true)
{
  HPEN pen = CreatePen(PS_SOLID, 1, Ref(border));
  HBRUSH brush = filled ? CreateSolidBrush(Ref(fill)) : static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
  HGDIOBJ oldPen = SelectObject(dc, pen);
  HGDIOBJ oldBrush = SelectObject(dc, brush);
  RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
  SelectObject(dc, oldBrush);
  SelectObject(dc, oldPen);
  DeleteObject(pen);
  if (filled)
    DeleteObject(brush);
}

inline void Text(HDC dc, const std::wstring& text, RECT r, VoLumRgb color, HFONT font, UINT format)
{
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, Ref(color));
  HGDIOBJ old = SelectObject(dc, font);
  DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &r, format | DT_NOPREFIX);
  SelectObject(dc, old);
}

inline void Chevron(HDC dc, const RECT& box, VoLumRgb color)
{
  const int cx = (box.left + box.right) / 2;
  const int cy = (box.top + box.bottom) / 2;
  const int w = Px(4);
  const int h = Px(2);
  POINT pts[3] = {{cx - w, cy - h}, {cx + w, cy - h}, {cx, cy + h + 1}};
  HPEN pen = CreatePen(PS_SOLID, 1, Ref(color));
  HBRUSH brush = CreateSolidBrush(Ref(color));
  HGDIOBJ oldPen = SelectObject(dc, pen);
  HGDIOBJ oldBrush = SelectObject(dc, brush);
  Polygon(dc, pts, 3);
  SelectObject(dc, oldBrush);
  SelectObject(dc, oldPen);
  DeleteObject(brush);
  DeleteObject(pen);
}

// A combo box closed on its value: recessed well, brass frame, gold chevron. It is
// drawn whole here, because the stock themed frame would not take VoLum's colours.
inline void PaintCombo(HWND combo, HDC dc)
{
  State& s = Skin();
  RECT rc;
  GetClientRect(combo, &rc);
  const bool enabled = IsWindowEnabled(combo) != FALSE;
  const bool focused = GetFocus() == combo;
  const bool dropped = SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0) != 0;
  const bool hot = enabled && s.hot == combo;

  Fill(dc, rc, volum_chrome::kPanel);
  const VoLumRgb fill = hot || dropped ? volum_chrome::kHoverBg : volum_chrome::kWell;
  const VoLumRgb border = enabled && (focused || dropped) ? volum_chrome::kSelBorder : volum_chrome::kWellFrame;
  RoundBox(dc, rc, fill, border, Px(6));

  const int arrowW = Px(18);
  RECT arrow{rc.right - arrowW, rc.top, rc.right, rc.bottom};
  Chevron(dc, arrow, enabled ? volum_chrome::kGold : volum_chrome::kDisabledText);

  RECT textR{rc.left + Px(8), rc.top, arrow.left, rc.bottom};
  const int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  Text(dc, ComboText(combo, sel), textR, enabled ? volum_chrome::kTextBright : volum_chrome::kDisabledText, s.body,
       DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
}

inline void TrackHot(HWND hwnd, UINT msg)
{
  State& s = Skin();
  if (msg == WM_MOUSEMOVE && s.hot != hwnd)
  {
    if (s.hot)
      InvalidateRect(s.hot, nullptr, FALSE);
    s.hot = hwnd;
    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&tme);
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  else if (msg == WM_MOUSELEAVE && s.hot == hwnd)
  {
    s.hot = nullptr;
    InvalidateRect(hwnd, nullptr, FALSE);
  }
}

inline LRESULT CALLBACK ComboProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR)
{
  switch (msg)
  {
    case WM_PAINT:
    {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      PaintCombo(hwnd, dc);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_PRINTCLIENT:
      PaintCombo(hwnd, reinterpret_cast<HDC>(wp));
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
      InvalidateRect(hwnd, nullptr, FALSE);
      break;
    case WM_MOUSEMOVE:
    case WM_MOUSELEAVE:
      TrackHot(hwnd, msg);
      break;
    case WM_NCDESTROY:
      if (Skin().hot == hwnd)
        Skin().hot = nullptr;
      RemoveWindowSubclass(hwnd, ComboProc, id);
      break;
  }
  return DefSubclassProc(hwnd, msg, wp, lp);
}

inline LRESULT CALLBACK ButtonProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR)
{
  switch (msg)
  {
    case WM_MOUSEMOVE:
    case WM_MOUSELEAVE:
      TrackHot(hwnd, msg);
      break;
    case WM_NCDESTROY:
      if (Skin().hot == hwnd)
        Skin().hot = nullptr;
      RemoveWindowSubclass(hwnd, ButtonProc, id);
      break;
  }
  return DefSubclassProc(hwnd, msg, wp, lp);
}

// The stock dropdown-list combo cannot take VoLum's highlight colour, so each one is
// rebuilt owner-drawn in place: same id, class, styles, Unicode-ness, size, font and
// tab position. Everything that drives it by id keeps working.
inline void RebuildCombo(HWND dialog, HWND old)
{
  State& s = Skin();
  const int id = GetDlgCtrlID(old);
  const LONG_PTR style = GetWindowLongPtrW(old, GWL_STYLE);
  const LONG_PTR exStyle = GetWindowLongPtrW(old, GWL_EXSTYLE);
  const bool unicode = IsWindowUnicode(old) != FALSE;

  RECT closed;
  GetWindowRect(old, &closed);
  MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&closed), 2);
  RECT dropped{};
  SendMessageW(old, CB_GETDROPPEDCONTROLRECT, 0, reinterpret_cast<LPARAM>(&dropped));
  const int closedH = closed.bottom - closed.top;
  // The rows are taller than the stock ones, so the .rc drop height would show
  // four of the ten buffer sizes. Room for ten, and a scrollbar past that.
  const int tenRows = closedH + static_cast<int>(s.itemHeight) * 10 + Px(2);
  const int listH = (std::max)(static_cast<int>(dropped.bottom - dropped.top), tenRows);
  HWND prev = GetWindow(old, GW_HWNDPREV);
  DestroyWindow(old);

  const DWORD newStyle =
    static_cast<DWORD>((style & ~CBS_OWNERDRAWVARIABLE) | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL);
  const int w = closed.right - closed.left;
  s.fieldHeight = static_cast<UINT>((std::max)(1, closedH - Px(6)));
  HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE));
  HMENU menuId = reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
  HWND combo = unicode ? CreateWindowExW(static_cast<DWORD>(exStyle), L"COMBOBOX", L"", newStyle, closed.left,
                                         closed.top, w, listH, dialog, menuId, inst, nullptr)
                       : CreateWindowExA(static_cast<DWORD>(exStyle), "COMBOBOX", "", newStyle, closed.left,
                                         closed.top, w, listH, dialog, menuId, inst, nullptr);
  if (!combo)
    return;

  SetWindowPos(combo, prev ? prev : HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  SendMessageW(combo, WM_SETFONT, reinterpret_cast<WPARAM>(s.body), FALSE);
  // Device names outrun the 100-unit field; the open list may be wider than it.
  SendMessageW(combo, CB_SETDROPPEDWIDTH, static_cast<WPARAM>((std::max)(w, Px(280))), 0);

  // The closed height follows the selection-field item height; settle it on the
  // height the .rc gave the stock control so the layout does not shift.
  RECT now;
  GetWindowRect(combo, &now);
  const int drift = closedH - (now.bottom - now.top);
  if (drift != 0)
    SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1),
                 (std::max)(LPARAM{1}, static_cast<LPARAM>(s.fieldHeight) + drift));

  COMBOBOXINFO info{sizeof(info)};
  if (GetComboBoxInfo(combo, &info) && info.hwndList)
    SetTheme(info.hwndList, L"DarkMode_Explorer"); // a dark scrollbar where Windows has one

  SetWindowSubclass(combo, ComboProc, 1, 0);
}

inline void DrawButton(const DRAWITEMSTRUCT& dis)
{
  State& s = Skin();
  const bool pressed = (dis.itemState & ODS_SELECTED) != 0;
  const bool disabled = (dis.itemState & ODS_DISABLED) != 0;
  const bool focused = (dis.itemState & ODS_FOCUS) != 0;
  const bool hot = !disabled && s.hot == dis.hwndItem;
  const bool isDefault = dis.CtlID == IDOK;

  Fill(dis.hDC, dis.rcItem, volum_chrome::kPanel);
  const VoLumRgb fill =
    pressed ? volum_chrome::kButtonPressedBg : (hot ? volum_chrome::kButtonHoverBg : volum_chrome::kPanel);
  const VoLumRgb border = !disabled && (focused || isDefault) ? volum_chrome::kSelBorder : volum_chrome::kFrame;
  RoundBox(dis.hDC, dis.rcItem, fill, border, Px(6));

  const VoLumRgb ink =
    disabled ? volum_chrome::kDisabledText : (isDefault ? volum_chrome::kGold : volum_chrome::kTextBright);
  Text(dis.hDC, WindowText(dis.hwndItem), dis.rcItem, ink, s.bold, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
}

inline void DrawComboItem(const DRAWITEMSTRUCT& dis)
{
  State& s = Skin();
  if (dis.itemState & ODS_COMBOBOXEDIT)
  {
    // The closed field: ComboProc paints it with the frame, so let it.
    InvalidateRect(dis.hwndItem, nullptr, FALSE);
    return;
  }
  const bool selected = (dis.itemState & ODS_SELECTED) != 0;
  Fill(dis.hDC, dis.rcItem, selected ? volum_chrome::kSelBg : volum_chrome::kWell);
  if (dis.itemID == static_cast<UINT>(-1))
    return;
  RECT textR = dis.rcItem;
  textR.left += Px(8);
  textR.right -= Px(4);
  Text(dis.hDC, ComboText(dis.hwndItem, static_cast<int>(dis.itemID)), textR,
       selected ? volum_chrome::kTextBright : volum_chrome::kTextDim, s.body,
       DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
}

// Group boxes are hidden and drawn here: a themed group box ignores the text colour
// and strokes its frame in the system grey.
inline void PaintGroups(HWND dialog, HDC dc)
{
  State& s = Skin();
  (void)dialog;
  const int pad = Px(8);
  const int capH = static_cast<int>(FontHeight(s.bold));
  for (const Group& g : s.groups)
  {
    RECT frame = g.rect;
    frame.top += capH / 2;
    RoundBox(dc, frame, volum_chrome::kPanel, volum_chrome::kFrame, Px(10), false);

    HGDIOBJ old = SelectObject(dc, s.bold);
    SIZE ext{};
    GetTextExtentPoint32W(dc, g.caption.c_str(), static_cast<int>(g.caption.size()), &ext);
    SelectObject(dc, old);
    RECT cap{g.rect.left + pad, g.rect.top, g.rect.left + pad + ext.cx + 2 * Px(4), g.rect.top + capH};
    Fill(dc, cap, volum_chrome::kPanel);
    Text(dc, g.caption, cap, volum_chrome::kGold, s.bold, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
  }
}
} // namespace prefs_skin

/** Call first thing in WM_INITDIALOG, before the combos are populated. */
inline void VoLumPrefsSkinAttach(HWND dialog, HINSTANCE resources, const char* bodyFont, const char* boldFont)
{
  using namespace prefs_skin;
  State& s = Skin();
  s = State{};
  s.dialog = dialog;
  s.dpi = DpiFor(dialog);
  LoadFontResource(resources, bodyFont);
  LoadFontResource(resources, boldFont);
  s.body = MakeFont(10, FW_NORMAL);
  s.bold = MakeFont(10, FW_BOLD);
  s.panel = CreateSolidBrush(Ref(volum_chrome::kPanel));
  s.well = CreateSolidBrush(Ref(volum_chrome::kWell));
  s.itemHeight = FontHeight(s.body) + static_cast<UINT>(Px(6));

  std::vector<HWND> children;
  for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
    children.push_back(child);

  for (HWND child : children)
  {
    wchar_t cls[32] = {};
    GetClassNameW(child, cls, 32);
    const LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
    if (_wcsicmp(cls, L"ComboBox") == 0)
    {
      RebuildCombo(dialog, child);
    }
    else if (_wcsicmp(cls, L"Button") == 0 && (style & BS_TYPEMASK) == BS_GROUPBOX)
    {
      Group g;
      GetWindowRect(child, &g.rect);
      MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&g.rect), 2);
      g.caption = WindowText(child);
      s.groups.push_back(g);
      ShowWindow(child, SW_HIDE);
    }
    else if (_wcsicmp(cls, L"Button") == 0
             && ((style & BS_TYPEMASK) == BS_PUSHBUTTON || (style & BS_TYPEMASK) == BS_DEFPUSHBUTTON))
    {
      SetWindowLongPtrW(child, GWL_STYLE, (style & ~static_cast<LONG_PTR>(BS_TYPEMASK)) | BS_OWNERDRAW);
      SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(s.bold), FALSE);
      SetWindowSubclass(child, ButtonProc, 1, 0);
    }
    else
    {
      SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(s.body), FALSE);
    }
  }
  InvalidateRect(dialog, nullptr, TRUE);
}

/** Call from WM_DESTROY. */
inline void VoLumPrefsSkinDetach(HWND dialog)
{
  using namespace prefs_skin;
  State& s = Skin();
  if (s.dialog != dialog)
    return;
  if (s.body)
    DeleteObject(s.body);
  if (s.bold)
    DeleteObject(s.bold);
  if (s.panel)
    DeleteObject(s.panel);
  if (s.well)
    DeleteObject(s.well);
  for (HANDLE font : s.fontResources)
    RemoveFontMemResourceEx(font);
  s = State{};
}

/** First thing in the dialog proc. True when the skin answered the message. */
inline bool VoLumPrefsSkinMessage(HWND dialog, UINT msg, WPARAM wp, LPARAM lp, INT_PTR& result)
{
  using namespace prefs_skin;
  State& s = Skin();
  if (!s.dialog || s.dialog != dialog)
    return false;

  switch (msg)
  {
    case WM_CTLCOLORDLG:
      result = reinterpret_cast<INT_PTR>(s.panel);
      return true;
    case WM_CTLCOLORSTATIC:
    {
      HDC dc = reinterpret_cast<HDC>(wp);
      SetTextColor(dc, Ref(volum_chrome::kTextDim));
      SetBkColor(dc, Ref(volum_chrome::kPanel));
      result = reinterpret_cast<INT_PTR>(s.panel);
      return true;
    }
    case WM_CTLCOLORLISTBOX:
    {
      HDC dc = reinterpret_cast<HDC>(wp);
      SetTextColor(dc, Ref(volum_chrome::kTextDim));
      SetBkColor(dc, Ref(volum_chrome::kWell));
      result = reinterpret_cast<INT_PTR>(s.well);
      return true;
    }
    case WM_MEASUREITEM:
    {
      auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
      if (!mis || mis->CtlType != ODT_COMBOBOX)
        return false;
      mis->itemHeight = mis->itemID == static_cast<UINT>(-1) ? s.fieldHeight : s.itemHeight;
      result = TRUE;
      return true;
    }
    case WM_DRAWITEM:
    {
      const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
      if (!dis)
        return false;
      if (dis->CtlType == ODT_BUTTON)
        DrawButton(*dis);
      else if (dis->CtlType == ODT_COMBOBOX)
        DrawComboItem(*dis);
      else
        return false;
      result = TRUE;
      return true;
    }
    case WM_PAINT:
    {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(dialog, &ps);
      PaintGroups(dialog, dc);
      EndPaint(dialog, &ps);
      result = TRUE;
      return true;
    }
  }
  (void)wp;
  return false;
}
} // namespace iplug
