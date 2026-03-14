// Main window message handling, layout, and painting.

#include "main_window.hpp"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <cstdio>
#include <string>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#endif

namespace {

using ::theme_manager::ApplyTitleBarTheme;
using ::theme_manager::BuildPalette;
using ::theme_manager::Palette;

constexpr int kHeaderH = 56;
constexpr int kFooterH = 52;
constexpr int kSliderH = 40;
constexpr int kFooterGap = 4;      // gap between slider bottom and footer top
constexpr int kSliderMarginH = 8;  // horizontal margin each side of slider
constexpr int kBadgeW = 120;       // dB badge area reserved at header right

constexpr const wchar_t* kTitle = L"Bass Booster";

// Upper bound of the slider range; the lower bound is always zero.
constexpr int kSliderMax = 1000;

// Pixel dimensions of the window client area.
struct ClientSize {
  int width;
  int height;
};

// State snapshot for one paint pass; assembled once so helper paint routines
// stay stateless and avoid reaching into `MainWindow`.
struct PaintContext {
  Palette palette;
  LayoutRegions layout;
  const AudioPipelineInterface* audio;  // non-owning; may be null
};

void PaintHeaderBadge(HDC hdc, const PaintContext& ctx) {
  // `swprintf` avoids `std::format` locale overhead for a fixed-size label.
  const double gain_db = ctx.audio != nullptr ? ctx.audio->gain_db() : 0.0;
  constexpr int kDbBufSize = 32;
  wchar_t db_str[kDbBufSize];
  swprintf(db_str, kDbBufSize, L"+%.1f dB", gain_db);

  constexpr int kBadgeFontH = 20;
  constexpr int kBadgePad = 10;  // top offset and right inset of the dB badge
  constexpr int kBadgeBottom = 46;

  HFONT badge_font = CreateFontW(
      /*cHeight=*/kBadgeFontH, /*cWidth=*/0,
      /*cEscapement=*/0, /*cOrientation=*/0,
      /*cWeight=*/FW_BOLD, /*bItalic=*/FALSE,
      /*bUnderline=*/FALSE, /*bStrikeOut=*/FALSE,
      /*iCharSet=*/DEFAULT_CHARSET, /*iOutPrecision=*/OUT_DEFAULT_PRECIS,
      /*iClipPrecision=*/CLIP_DEFAULT_PRECIS, /*iQuality=*/CLEARTYPE_QUALITY,
      /*iPitchAndFamily=*/DEFAULT_PITCH | FF_SWISS,
      /*pszFaceName=*/L"Segoe UI");
  HFONT old_f = static_cast<HFONT>(SelectObject(hdc, badge_font));
  SetTextColor(hdc, ctx.palette.accent);
  RECT badge_rc = {.left = ctx.layout.header.right - kBadgeW,
                   .top = kBadgePad,
                   .right = ctx.layout.header.right - kBadgePad,
                   .bottom = kBadgeBottom};
  DrawTextW(hdc, db_str, /*cchText=*/-1, &badge_rc,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
  SelectObject(hdc, old_f);
  DeleteObject(badge_font);
}

void PaintHeader(HDC hdc, const PaintContext& ctx) {
  HBRUSH surf = CreateSolidBrush(ctx.palette.surface);
  FillRect(hdc, &ctx.layout.header, surf);
  DeleteObject(surf);

  HPEN sep = CreatePen(/*iStyle=*/PS_SOLID, /*cWidth=*/1, ctx.palette.border);
  HPEN old = static_cast<HPEN>(SelectObject(hdc, sep));
  MoveToEx(hdc, ctx.layout.header.left, ctx.layout.header.bottom - 1,
           /*lppt=*/nullptr);
  LineTo(hdc, ctx.layout.header.right, ctx.layout.header.bottom - 1);
  SelectObject(hdc, old);
  DeleteObject(sep);

  constexpr int kTitleFontH = 26;
  constexpr int kSubFontH = 13;
  constexpr int kTextPadL = 16;
  constexpr int kTitleTop = 8;
  constexpr int kTitleBottom = 38;
  constexpr int kSubTop = 34;
  constexpr int kSubBottomPad = 4;  // padding below subtitle to header bottom

  HFONT title_font = CreateFontW(
      /*cHeight=*/kTitleFontH, /*cWidth=*/0,
      /*cEscapement=*/0, /*cOrientation=*/0,
      /*cWeight=*/FW_SEMIBOLD, /*bItalic=*/FALSE,
      /*bUnderline=*/FALSE, /*bStrikeOut=*/FALSE,
      /*iCharSet=*/DEFAULT_CHARSET, /*iOutPrecision=*/OUT_DEFAULT_PRECIS,
      /*iClipPrecision=*/CLIP_DEFAULT_PRECIS, /*iQuality=*/CLEARTYPE_QUALITY,
      /*iPitchAndFamily=*/DEFAULT_PITCH | FF_SWISS,
      /*pszFaceName=*/L"Segoe UI");
  HFONT sub_font = CreateFontW(
      /*cHeight=*/kSubFontH, /*cWidth=*/0,
      /*cEscapement=*/0, /*cOrientation=*/0,
      /*cWeight=*/FW_NORMAL, /*bItalic=*/FALSE,
      /*bUnderline=*/FALSE, /*bStrikeOut=*/FALSE,
      /*iCharSet=*/DEFAULT_CHARSET, /*iOutPrecision=*/OUT_DEFAULT_PRECIS,
      /*iClipPrecision=*/CLIP_DEFAULT_PRECIS, /*iQuality=*/CLEARTYPE_QUALITY,
      /*iPitchAndFamily=*/DEFAULT_PITCH | FF_SWISS,
      /*pszFaceName=*/L"Segoe UI");

  SetBkMode(hdc, TRANSPARENT);

  HFONT old_font = static_cast<HFONT>(SelectObject(hdc, title_font));
  SetTextColor(hdc, ctx.palette.text);
  RECT title_rc = {.left = kTextPadL,
                   .top = kTitleTop,
                   .right = ctx.layout.header.right - kBadgeW,
                   .bottom = kTitleBottom};
  DrawTextW(hdc, kTitle, /*cchText=*/-1, &title_rc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  PaintHeaderBadge(hdc, ctx);

  SelectObject(hdc, sub_font);
  SetTextColor(hdc, ctx.palette.text_muted);
  static const std::wstring kNoDevice = L"No device";
  const std::wstring& dev =
      ctx.audio != nullptr ? ctx.audio->endpoint_name() : kNoDevice;
  RECT sub_rc = {.left = kTextPadL,
                 .top = kSubTop,
                 .right = ctx.layout.header.right - kBadgeW,
                 .bottom = kHeaderH - kSubBottomPad};
  DrawTextW(hdc, dev.c_str(), /*cchText=*/-1, &sub_rc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  SelectObject(hdc, old_font);
  DeleteObject(title_font);
  DeleteObject(sub_font);
}

void PaintSliderLabel(HDC hdc, const PaintContext& ctx) {
  HBRUSH bg_brush = CreateSolidBrush(ctx.palette.background);
  FillRect(hdc, &ctx.layout.slider_label, bg_brush);
  DeleteObject(bg_brush);

  constexpr int kLabelFontH = 12;
  constexpr int kLabelColW = 80;  // left/right label column width

  HFONT font = CreateFontW(
      /*cHeight=*/kLabelFontH, /*cWidth=*/0,
      /*cEscapement=*/0, /*cOrientation=*/0,
      /*cWeight=*/FW_NORMAL, /*bItalic=*/FALSE,
      /*bUnderline=*/FALSE, /*bStrikeOut=*/FALSE,
      /*iCharSet=*/DEFAULT_CHARSET, /*iOutPrecision=*/OUT_DEFAULT_PRECIS,
      /*iClipPrecision=*/CLIP_DEFAULT_PRECIS, /*iQuality=*/CLEARTYPE_QUALITY,
      /*iPitchAndFamily=*/DEFAULT_PITCH | FF_SWISS,
      /*pszFaceName=*/L"Segoe UI");
  HFONT old_f = static_cast<HFONT>(SelectObject(hdc, font));
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, ctx.palette.text_muted);

  RECT left_rc = ctx.layout.slider_label;
  left_rc.left += kSliderMarginH;
  left_rc.right = left_rc.left + kLabelColW;
  RECT right_rc = ctx.layout.slider_label;
  right_rc.right -= kSliderMarginH;
  right_rc.left = right_rc.right - kLabelColW;
  RECT cent_rc = ctx.layout.slider_label;
  cent_rc.left += kLabelColW;
  cent_rc.right -= kLabelColW;

  DrawTextW(hdc, L"FLAT", /*cchText=*/-1, &left_rc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  DrawTextW(hdc, L"BASS BOOST", /*cchText=*/-1, &cent_rc,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  DrawTextW(hdc, L"MORE BASS", /*cchText=*/-1, &right_rc,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

  SelectObject(hdc, old_f);
  DeleteObject(font);
}

void PaintFooter(HDC hdc, const PaintContext& ctx) {
  HBRUSH surf = CreateSolidBrush(ctx.palette.surface);
  FillRect(hdc, &ctx.layout.footer, surf);
  DeleteObject(surf);

  HPEN sep = CreatePen(/*iStyle=*/PS_SOLID, /*cWidth=*/1, ctx.palette.border);
  HPEN old = static_cast<HPEN>(SelectObject(hdc, sep));
  MoveToEx(hdc, ctx.layout.footer.left, ctx.layout.footer.top,
           /*lppt=*/nullptr);
  LineTo(hdc, ctx.layout.footer.right, ctx.layout.footer.top);
  SelectObject(hdc, old);
  DeleteObject(sep);

  constexpr int kFooterFontH = 11;
  constexpr int kFooterPad = 12;  // horizontal text padding in footer

  HFONT font = CreateFontW(
      /*cHeight=*/kFooterFontH, /*cWidth=*/0,
      /*cEscapement=*/0, /*cOrientation=*/0,
      /*cWeight=*/FW_NORMAL, /*bItalic=*/FALSE,
      /*bUnderline=*/FALSE, /*bStrikeOut=*/FALSE,
      /*iCharSet=*/DEFAULT_CHARSET, /*iOutPrecision=*/OUT_DEFAULT_PRECIS,
      /*iClipPrecision=*/CLIP_DEFAULT_PRECIS, /*iQuality=*/CLEARTYPE_QUALITY,
      /*iPitchAndFamily=*/DEFAULT_PITCH | FF_SWISS,
      /*pszFaceName=*/L"Segoe UI");
  HFONT old_f = static_cast<HFONT>(SelectObject(hdc, font));
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, ctx.palette.text_muted);

  RECT text_rc = ctx.layout.footer;
  text_rc.left += kFooterPad;
  text_rc.right -= kFooterPad;
  DrawTextW(hdc, L"Shelf: 100 Hz  |  WASAPI Loopback Capture",
            /*cchText=*/-1, &text_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  SelectObject(hdc, old_f);
  DeleteObject(font);
}

LRESULT OnCtlColor(HDC hdc, const PaintContext& ctx) {
  SetBkColor(hdc, ctx.palette.surface);
  SetTextColor(hdc, ctx.palette.text);
  // Static brush: recreated on theme change; harmless leak of one `HBRUSH`.
  static HBRUSH s_bg_brush = nullptr;
  if (s_bg_brush != nullptr) {
    DeleteObject(s_bg_brush);
  }
  s_bg_brush = CreateSolidBrush(ctx.palette.surface);
  return reinterpret_cast<LRESULT>(s_bg_brush);
}

bool RegisterMainWindowClass(HINSTANCE instance, const wchar_t* class_name,
                             WNDPROC window_proc) {
  INITCOMMONCONTROLSEX ice = {.dwSize = sizeof(ice), .dwICC = ICC_BAR_CLASSES};
  InitCommonControlsEx(&ice);

  WNDCLASSEXW wnd_class = {};
  wnd_class.cbSize = sizeof(wnd_class);
  wnd_class.style = CS_HREDRAW | CS_VREDRAW;
  wnd_class.lpfnWndProc = window_proc;
  wnd_class.cbWndExtra = sizeof(void*);
  wnd_class.hInstance = instance;
  wnd_class.hIcon = LoadIcon(/*hInstance=*/nullptr, IDI_APPLICATION);
  wnd_class.hIconSm = LoadIcon(/*hInstance=*/nullptr, IDI_APPLICATION);
  wnd_class.hCursor = LoadCursor(/*hInstance=*/nullptr, IDC_ARROW);
  wnd_class.hbrBackground = nullptr;
  wnd_class.lpszClassName = class_name;
  return RegisterClassExW(&wnd_class) != 0 ||
         GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

// Creates the horizontal trackbar slider as a child of `parent`. Returns the
// slider's window handle, or nullptr if creation failed.
HWND CreateSliderControl(HWND parent, HINSTANCE instance, ClientSize client) {
  constexpr UINT kSliderControlId = 100;
  // Page step = 5% of the slider range, so one click moves the thumb
  // noticeably.
  constexpr int kPageDivisor = 20;
  constexpr int kSliderThumbLen = 16;  // narrow thumb so it reaches both edges
  const int slider_top = client.height - kFooterH - kFooterGap - kSliderH;

  HWND slider = CreateWindowExW(
      /*dwExStyle=*/0, /*lpClassName=*/TRACKBAR_CLASSW,
      /*lpWindowName=*/nullptr,
      /*dwStyle=*/WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS |
          TBS_FIXEDLENGTH,
      /*x=*/kSliderMarginH, /*y=*/slider_top,
      /*nWidth=*/client.width - (kSliderMarginH * 2), /*nHeight=*/kSliderH,
      /*hWndParent=*/parent,
      /*hMenu=*/reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSliderControlId)),
      /*hInstance=*/instance, /*lpParam=*/nullptr);
  if (slider == nullptr) {
    return nullptr;
  }

  SendMessageW(slider, TBM_SETRANGEMIN, /*fRedraw=*/FALSE, /*minimum=*/0);
  SendMessageW(slider, TBM_SETRANGEMAX, /*fRedraw=*/FALSE,
               /*maximum=*/kSliderMax);
  SendMessageW(slider, TBM_SETPOS, /*fRedraw=*/TRUE, /*position=*/0);
  SendMessageW(slider, TBM_SETPAGESIZE, /*wParam=*/0,
               /*pageSize=*/kSliderMax / kPageDivisor);
  SendMessageW(slider, TBM_SETTHUMBLENGTH, /*length=*/kSliderThumbLen,
               /*lParam=*/0);

  // Empty theme name strips the visual style so `WM_CTLCOLORSCROLLBAR`
  // messages reach the parent, enabling custom track and thumb colors.
  SetWindowTheme(slider, /*pszSubAppName=*/L"", /*pszSubIdList=*/nullptr);
  return slider;
}

// Returns recomputed layout rectangles for the current client size and
// repositions the slider child window to match.
LayoutRegions ComputeLayout(HWND slider_hwnd, ClientSize client) {
  constexpr int kLabelH = 24;  // slider label row height
  const int footer_top = client.height - kFooterH;
  const int slider_top = footer_top - kFooterGap - kSliderH;
  const int label_top = slider_top - kLabelH;

  if (slider_hwnd != nullptr) {
    SetWindowPos(slider_hwnd, /*hWndInsertAfter=*/nullptr,
                 /*x=*/kSliderMarginH, /*y=*/slider_top,
                 /*cx=*/client.width - (kSliderMarginH * 2), /*cy=*/kSliderH,
                 /*uFlags=*/SWP_NOZORDER | SWP_NOACTIVATE);
  }

  return {.header = {.left = 0,
                     .top = 0,
                     .right = client.width,
                     .bottom = kHeaderH},
          .slider_label = {.left = 0,
                           .top = label_top,
                           .right = client.width,
                           .bottom = slider_top},
          .footer = {.left = 0,
                     .top = footer_top,
                     .right = client.width,
                     .bottom = client.height}};
}

// Draws the entire client area into a double-buffered back surface to
// eliminate flicker.
void PaintWindow(HWND hwnd, const PaintContext& ctx) {
  PAINTSTRUCT paint_struct;
  HDC hdc = BeginPaint(hwnd, &paint_struct);

  RECT client_rect;
  GetClientRect(hwnd, &client_rect);
  const int width = client_rect.right;
  const int height = client_rect.bottom;

  // Double-buffer: draw into an off-screen bitmap, then `BitBlt` it to the
  // screen in one shot to eliminate flicker.
  HDC mem_dc = CreateCompatibleDC(hdc);
  HBITMAP mem_bmp = CreateCompatibleBitmap(hdc, width, height);
  HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

  HBRUSH bg_brush = CreateSolidBrush(ctx.palette.background);
  FillRect(mem_dc, &client_rect, bg_brush);
  DeleteObject(bg_brush);

  PaintHeader(mem_dc, ctx);
  PaintSliderLabel(mem_dc, ctx);
  PaintFooter(mem_dc, ctx);

  BitBlt(hdc, /*x=*/0, /*y=*/0, /*cx=*/width, /*cy=*/height,
         /*hdcSrc=*/mem_dc, /*x1=*/0, /*y1=*/0, SRCCOPY);
  SelectObject(mem_dc, old_bmp);
  DeleteObject(mem_bmp);
  DeleteDC(mem_dc);
  EndPaint(hwnd, &paint_struct);
}

// Associates the `MainWindow` instance pointer with the window on creation,
// then delegates all messages to the instance's `HandleMessage`.
LRESULT CALLBACK DispatchWindowMessage(HWND hwnd, UINT msg, WPARAM wparam,
                                       LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    const auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(
        hwnd, GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(create_struct->lpCreateParams));
  }

  auto* self =
      reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self != nullptr) {
    return self->HandleMessage(hwnd, msg, wparam, lparam);
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

MainWindow::MainWindow(AudioPipelineInterface* audio) : audio_(audio) {}

bool MainWindow::Create(HINSTANCE instance, int cmd_show) {
  constexpr const wchar_t* kClassName = L"BassBoosterMain";
  constexpr int kInitialWidth = 620;
  constexpr int kInitialHeight = 240;

  if (!RegisterMainWindowClass(instance, kClassName, DispatchWindowMessage)) {
    return false;
  }

  instance_ = instance;
  palette_ = BuildPalette();

  hwnd_ = CreateWindowExW(
      /*dwExStyle=*/0, /*lpClassName=*/kClassName, /*lpWindowName=*/kTitle,
      /*dwStyle=*/WS_OVERLAPPEDWINDOW,
      /*x=*/CW_USEDEFAULT, /*y=*/CW_USEDEFAULT,
      /*nWidth=*/kInitialWidth, /*nHeight=*/kInitialHeight,
      /*hWndParent=*/nullptr, /*hMenu=*/nullptr,
      /*hInstance=*/instance, /*lpParam=*/this);
  if (hwnd_ == nullptr) {
    return false;
  }

  ApplyTitleBarTheme(hwnd_);
  ShowWindow(hwnd_, cmd_show);
  UpdateWindow(hwnd_);
  return true;
}

LRESULT MainWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam) {
  switch (msg) {
    case WM_NCCREATE:
      hwnd_ = hwnd;
      return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_CREATE: {
      RECT client_rect;
      GetClientRect(hwnd, &client_rect);
      const ClientSize client = {.width = client_rect.right,
                                 .height = client_rect.bottom};
      slider_hwnd_ = CreateSliderControl(hwnd, instance_, client);
      layout_ = ComputeLayout(slider_hwnd_, client);
      InvalidateRect(hwnd, /*lpRect=*/nullptr, /*bErase=*/FALSE);
      return 0;
    }
    case WM_SIZE: {
      const ClientSize client = {.width = LOWORD(lparam),
                                 .height = HIWORD(lparam)};
      layout_ = ComputeLayout(slider_hwnd_, client);
      InvalidateRect(hwnd, /*lpRect=*/nullptr, /*bErase=*/FALSE);
      return 0;
    }
    case WM_GETMINMAXINFO: {
      constexpr int kMinWidth = 480;
      constexpr int kMinHeight = 200;
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      info->ptMinTrackSize = {.x = kMinWidth, .y = kMinHeight};
      return 0;
    }
    case WM_PAINT:
      PaintWindow(hwnd,
                  PaintContext{
                      .palette = palette_, .layout = layout_, .audio = audio_});
      return 0;
    case WM_ERASEBKGND:
      // Return 1 to tell Windows we handled the erase; the actual background
      // is painted in `WM_PAINT` via a back-buffer, so erasing here would
      // cause flicker.
      return 1;
    case WM_HSCROLL: {
      if (reinterpret_cast<HWND>(lparam) != slider_hwnd_) {
        return 0;
      }
      const LRESULT pos =
          SendMessageW(slider_hwnd_, TBM_GETPOS, /*wParam=*/0, /*lParam=*/0);
      if (audio_ != nullptr) {
        audio_->SetBoostLevel(static_cast<double>(pos) /
                              static_cast<double>(kSliderMax));
      }
      InvalidateRect(hwnd, &layout_.header, /*bErase=*/FALSE);
      return 0;
    }
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
      palette_ = BuildPalette();
      ApplyTitleBarTheme(hwnd);
      InvalidateRect(hwnd, /*lpRect=*/nullptr, /*bErase=*/TRUE);
      return 0;
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
      return OnCtlColor(
          reinterpret_cast<HDC>(wparam),
          PaintContext{
              .palette = palette_, .layout = layout_, .audio = audio_});
    case WM_DESTROY:
      if (audio_ != nullptr) {
        audio_->Stop();
      }
      PostQuitMessage(/*nExitCode=*/0);
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
}
