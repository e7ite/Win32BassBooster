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

constexpr int kHeaderH = 56;
constexpr int kFooterH = 52;
constexpr int kSliderH = 40;
constexpr int kFooterGap = 4;       // gap between slider bottom and footer top
constexpr int kLabelH = 24;         // slider label row height
constexpr int kSliderMarginH = 40;  // horizontal margin each side of slider
constexpr int kLabelColW = 80;      // left/right label column width
constexpr int kBadgeW = 120;        // dB badge area reserved at header right
constexpr int kTextPadL = 16;       // left text padding in header
constexpr int kFooterPad = 12;      // horizontal text padding in footer

constexpr int kFontTitle = 26;
constexpr int kFontSub = 13;
constexpr int kFontBadge = 20;
constexpr int kFontLabel = 12;
constexpr int kFontFooter = 11;

// Page step = 5 % of the slider range, so one click moves the thumb noticeably.
constexpr int kSliderPageDivisor = 20;

// Vertical bounds for the text rows within the header panel.
constexpr int kTitleTop = 8;
constexpr int kTitleBottom = 38;
constexpr int kSubTop = 34;
constexpr int kSubBottomPad = 4;  // padding below subtitle to header bottom
constexpr int kBadgePad = 10;     // top offset and right inset of the dB badge
constexpr int kBadgeBottom = 46;

struct MainWindowParams {
  const wchar_t* class_name;
  const wchar_t* title;
  int min_width;
  int min_height;
  int initial_width;
  int initial_height;
  int slider_min;
  int slider_max;
  UINT slider_control_id;
};

constexpr MainWindowParams kMainWindowParams = {
    .class_name = L"BassBoosterMain",
    .title = L"Bass Booster",
    .min_width = 480,
    .min_height = 200,
    .initial_width = 620,
    .initial_height = 240,
    .slider_min = 0,
    .slider_max = 1000,
    .slider_control_id = 100,
};

// State snapshot for one paint pass; assembled once so helper paint routines
// stay stateless and avoid reaching into `MainWindow`.
struct PaintContext {
  theme_manager::Palette palette;
  RECT header_rc;
  RECT footer_rc;
  RECT slider_label_rc;
  const AudioPipelineInterface* audio;  // non-owning; may be null
};

void PaintHeaderBadge(HDC hdc, const PaintContext& ctx) {
  // `swprintf` avoids `std::format` locale overhead for a fixed-size label.
  const double gain_db = ctx.audio != nullptr ? ctx.audio->gain_db() : 0.0;
  constexpr int kDbBufSize = 32;
  wchar_t db_str[kDbBufSize];
  swprintf(db_str, kDbBufSize, L"+%.1f dB", gain_db);

  HFONT badge_font =
      CreateFontW(kFontBadge, /*cWidth=*/0, /*cEscapement=*/0,
                  /*cOrientation=*/0, FW_BOLD, FALSE, FALSE, FALSE,
                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  HFONT old_f = static_cast<HFONT>(SelectObject(hdc, badge_font));
  SetTextColor(hdc, ctx.palette.accent);
  RECT badge_rc = {ctx.header_rc.right - kBadgeW, kBadgePad,
                   ctx.header_rc.right - kBadgePad, kBadgeBottom};
  DrawTextW(hdc, db_str, -1, &badge_rc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
  SelectObject(hdc, old_f);
  DeleteObject(badge_font);
}

void PaintHeader(HDC hdc, const PaintContext& ctx) {
  HBRUSH surf = CreateSolidBrush(ctx.palette.surface);
  FillRect(hdc, &ctx.header_rc, surf);
  DeleteObject(surf);

  HPEN sep = CreatePen(PS_SOLID, 1, ctx.palette.border);
  HPEN old = static_cast<HPEN>(SelectObject(hdc, sep));
  MoveToEx(hdc, ctx.header_rc.left, ctx.header_rc.bottom - 1,
           /*lppt=*/nullptr);
  LineTo(hdc, ctx.header_rc.right, ctx.header_rc.bottom - 1);
  SelectObject(hdc, old);
  DeleteObject(sep);

  HFONT title_font =
      CreateFontW(kFontTitle, /*cWidth=*/0, /*cEscapement=*/0,
                  /*cOrientation=*/0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  HFONT sub_font =
      CreateFontW(kFontSub, /*cWidth=*/0, /*cEscapement=*/0,
                  /*cOrientation=*/0, FW_NORMAL, FALSE, FALSE, FALSE,
                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

  SetBkMode(hdc, TRANSPARENT);

  HFONT old_font = static_cast<HFONT>(SelectObject(hdc, title_font));
  SetTextColor(hdc, ctx.palette.text);
  RECT title_rc = {kTextPadL, kTitleTop, ctx.header_rc.right - kBadgeW,
                   kTitleBottom};
  DrawTextW(hdc, kMainWindowParams.title, -1, &title_rc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  PaintHeaderBadge(hdc, ctx);

  SelectObject(hdc, sub_font);
  SetTextColor(hdc, ctx.palette.text_muted);
  static const std::wstring kNoDevice = L"No device";
  const std::wstring& dev =
      ctx.audio != nullptr ? ctx.audio->endpoint_name() : kNoDevice;
  RECT sub_rc = {kTextPadL, kSubTop, ctx.header_rc.right - kBadgeW,
                 kHeaderH - kSubBottomPad};
  DrawTextW(hdc, dev.c_str(), -1, &sub_rc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  SelectObject(hdc, old_font);
  DeleteObject(title_font);
  DeleteObject(sub_font);
}

void PaintSliderLabel(HDC hdc, const PaintContext& ctx) {
  HBRUSH bg_brush = CreateSolidBrush(ctx.palette.background);
  FillRect(hdc, &ctx.slider_label_rc, bg_brush);
  DeleteObject(bg_brush);

  HFONT font =
      CreateFontW(kFontLabel, /*cWidth=*/0, /*cEscapement=*/0,
                  /*cOrientation=*/0, FW_NORMAL, FALSE, FALSE, FALSE,
                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  HFONT old_f = static_cast<HFONT>(SelectObject(hdc, font));
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, ctx.palette.text_muted);

  RECT left_rc = ctx.slider_label_rc;
  left_rc.right = left_rc.left + kLabelColW;
  RECT right_rc = ctx.slider_label_rc;
  right_rc.left = right_rc.right - kLabelColW;
  RECT cent_rc = ctx.slider_label_rc;
  cent_rc.left += kLabelColW;
  cent_rc.right -= kLabelColW;

  DrawTextW(hdc, L"MORE BASS", -1, &left_rc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  DrawTextW(hdc, L"BASS BOOST", -1, &cent_rc,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  DrawTextW(hdc, L"FLAT", -1, &right_rc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

  SelectObject(hdc, old_f);
  DeleteObject(font);
}

void PaintFooter(HDC hdc, const PaintContext& ctx) {
  HBRUSH surf = CreateSolidBrush(ctx.palette.surface);
  FillRect(hdc, &ctx.footer_rc, surf);
  DeleteObject(surf);

  HPEN sep = CreatePen(PS_SOLID, 1, ctx.palette.border);
  HPEN old = static_cast<HPEN>(SelectObject(hdc, sep));
  MoveToEx(hdc, ctx.footer_rc.left, ctx.footer_rc.top, /*lppt=*/nullptr);
  LineTo(hdc, ctx.footer_rc.right, ctx.footer_rc.top);
  SelectObject(hdc, old);
  DeleteObject(sep);

  HFONT font =
      CreateFontW(kFontFooter, /*cWidth=*/0, /*cEscapement=*/0,
                  /*cOrientation=*/0, FW_NORMAL, FALSE, FALSE, FALSE,
                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  HFONT old_f = static_cast<HFONT>(SelectObject(hdc, font));
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, ctx.palette.text_muted);

  RECT text_rc = ctx.footer_rc;
  text_rc.left += kFooterPad;
  text_rc.right -= kFooterPad;
  DrawTextW(hdc,
            L"Shelf: 100 Hz  |  Exciter: 100 Hz  |  WASAPI Loopback Capture",
            -1, &text_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

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

bool RegisterMainWindowClass(HINSTANCE instance, const MainWindowParams& params,
                             WNDPROC window_proc) {
  INITCOMMONCONTROLSEX ice = {sizeof(ice), ICC_BAR_CLASSES};
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
  wnd_class.lpszClassName = params.class_name;
  return RegisterClassExW(&wnd_class) != 0 ||
         GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

double SliderPositionToBoostLevel(int slider_pos,
                                  const MainWindowParams& params) {
  return 1.0 - static_cast<double>(slider_pos) /
                   static_cast<double>(params.slider_max);
}

}  // namespace

MainWindow::MainWindow(std::unique_ptr<AudioPipelineInterface> engine)
    : audio_(std::move(engine)) {}
MainWindow::~MainWindow() {
  if (audio_ != nullptr) {
    audio_->Stop();
  }
}

bool MainWindow::Create(HINSTANCE instance, int cmd_show) {
  if (!RegisterMainWindowClass(instance, kMainWindowParams, WndProc)) {
    return false;
  }

  instance_ = instance;
  palette_ = theme_manager::BuildPalette();

  hwnd_ = CreateWindowExW(
      /*dwExStyle=*/0, kMainWindowParams.class_name, kMainWindowParams.title,
      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
      kMainWindowParams.initial_width, kMainWindowParams.initial_height,
      /*hWndParent=*/nullptr, /*hMenu=*/nullptr, instance, this);
  if (hwnd_ == nullptr) {
    return false;
  }

  theme_manager::ApplyTitleBarTheme(hwnd_);
  ShowWindow(hwnd_, cmd_show);
  UpdateWindow(hwnd_);
  return true;
}

int MainWindow::Run() {
  MSG msg = {};
  while (GetMessageW(&msg, /*hWnd=*/nullptr, /*wMsgFilterMin=*/0,
                     /*wMsgFilterMax=*/0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                     LPARAM lparam) {
  MainWindow* self = nullptr;
  if (msg == WM_NCCREATE) {
    const auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = reinterpret_cast<MainWindow*>(create_struct->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->hwnd_ = hwnd;
  } else {
    self =
        reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (self != nullptr) {
    return self->HandleMessage(hwnd, msg, wparam, lparam);
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT MainWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam) {
  switch (msg) {
    case WM_CREATE:
      OnCreate(hwnd);
      return 0;
    case WM_SIZE:
      OnSize(LOWORD(lparam), HIWORD(lparam));
      return 0;
    case WM_GETMINMAXINFO: {
      auto* min_max_info = reinterpret_cast<MINMAXINFO*>(lparam);
      min_max_info->ptMinTrackSize = {kMainWindowParams.min_width,
                                      kMainWindowParams.min_height};
      return 0;
    }
    case WM_PAINT:
      OnPaint();
      return 0;
    case WM_ERASEBKGND:
      // Return 1 to tell Windows we handled the erase; the actual background
      // is painted in `WM_PAINT` via a back-buffer, so erasing here would cause
      // flicker.
      return 1;
    case WM_HSCROLL:
      OnHScroll(reinterpret_cast<HWND>(lparam));
      return 0;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
      OnThemeChange();
      return 0;
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
      return OnCtlColor(
          reinterpret_cast<HDC>(wparam),
          {palette_, header_rc_, footer_rc_, slider_label_rc_, audio_.get()});
    case WM_DESTROY:
      OnDestroy();
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
}

void MainWindow::OnCreate(HWND hwnd) {
  hwnd_ = hwnd;

  RECT client_rect;
  GetClientRect(hwnd, &client_rect);
  const int width = client_rect.right;
  const int height = client_rect.bottom;

  const int slider_top = height - kFooterH - kFooterGap - kSliderH;
  slider_hwnd_ = CreateWindowExW(
      /*dwExStyle=*/0, TRACKBAR_CLASSW, /*lpWindowName=*/nullptr,
      WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS | TBS_BOTH, kSliderMarginH,
      slider_top, width - (kSliderMarginH * 2), kSliderH, hwnd,
      reinterpret_cast<HMENU>(
          static_cast<INT_PTR>(kMainWindowParams.slider_control_id)),
      instance_, /*lpParam=*/nullptr);

  SendMessageW(slider_hwnd_, TBM_SETRANGEMIN, FALSE,
               kMainWindowParams.slider_min);
  SendMessageW(slider_hwnd_, TBM_SETRANGEMAX, FALSE,
               kMainWindowParams.slider_max);
  SendMessageW(slider_hwnd_, TBM_SETPOS, TRUE, kMainWindowParams.slider_max);
  SendMessageW(slider_hwnd_, TBM_SETPAGESIZE, /*wParam=*/0,
               kMainWindowParams.slider_max / kSliderPageDivisor);

  // Empty theme name strips the visual style so `WM_CTLCOLORSCROLLBAR` messages
  // reach the parent, enabling custom track and thumb colors.
  SetWindowTheme(slider_hwnd_, L"", /*pszSubIdList=*/nullptr);

  OnSize(width, height);
}

void MainWindow::OnSize(int width, int height) {
  const int footer_top = height - kFooterH;
  const int slider_top = footer_top - kFooterGap - kSliderH;
  const int label_top = slider_top - kLabelH;

  header_rc_ = {0, 0, width, kHeaderH};
  slider_label_rc_ = {0, label_top, width, slider_top};
  footer_rc_ = {0, footer_top, width, height};

  if (slider_hwnd_ != nullptr) {
    SetWindowPos(slider_hwnd_, /*hWndInsertAfter=*/nullptr, kSliderMarginH,
                 slider_top, width - (kSliderMarginH * 2), kSliderH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }

  InvalidateRect(hwnd_, /*lpRect=*/nullptr, FALSE);
}

void MainWindow::OnPaint() {
  PAINTSTRUCT paint_struct;
  HDC hdc = BeginPaint(hwnd_, &paint_struct);

  RECT client_rect;
  GetClientRect(hwnd_, &client_rect);
  const int width = client_rect.right;
  const int height = client_rect.bottom;

  // Double-buffer: draw into an off-screen bitmap, then `BitBlt` it to the
  // screen in one shot to eliminate flicker.
  HDC mem_dc = CreateCompatibleDC(hdc);
  HBITMAP mem_bmp = CreateCompatibleBitmap(hdc, width, height);
  HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

  HBRUSH bg_brush = CreateSolidBrush(palette_.background);
  FillRect(mem_dc, &client_rect, bg_brush);
  DeleteObject(bg_brush);

  const PaintContext ctx = {palette_, header_rc_, footer_rc_, slider_label_rc_,
                            audio_.get()};
  PaintHeader(mem_dc, ctx);
  PaintSliderLabel(mem_dc, ctx);
  PaintFooter(mem_dc, ctx);

  BitBlt(hdc, /*x=*/0, /*y=*/0, width, height, mem_dc, /*x1=*/0, /*y1=*/0,
         SRCCOPY);
  SelectObject(mem_dc, old_bmp);
  DeleteObject(mem_bmp);
  DeleteDC(mem_dc);
  EndPaint(hwnd_, &paint_struct);
}

void MainWindow::OnHScroll(HWND ctrl) {
  if (ctrl != slider_hwnd_) {
    return;
  }
  const LRESULT pos =
      SendMessageW(slider_hwnd_, TBM_GETPOS, /*wParam=*/0, /*lParam=*/0);
  if (audio_ != nullptr) {
    audio_->SetBoostLevel(
        SliderPositionToBoostLevel(static_cast<int>(pos), kMainWindowParams));
  }
  InvalidateRect(hwnd_, &header_rc_, FALSE);
}

void MainWindow::OnThemeChange() {
  palette_ = theme_manager::BuildPalette();
  theme_manager::ApplyTitleBarTheme(hwnd_);
  InvalidateRect(hwnd_, /*lpRect=*/nullptr, TRUE);
}

void MainWindow::OnDestroy() {
  if (audio_ != nullptr) {
    audio_->Stop();
  }
}
