#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <iterator>
#include <cstdint>
#include <limits>
#include <utility>

#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#include "ViewReservationPlugin.h"
#include "PanelIndicatorContrast.h"
#include "PanelViewportGeometry.h"
#include "PanelViewportRowIdentity.h"
#include "PanelWaveBarLayout.h"
#include "PanelGlyph.h"
#include <windowsx.h>
#include "PanelStationPresentation.h"
#include "Version.h"
#include "ReservationTime.h"
#include "ReservationSchedule.h"
#include <vector>

namespace {
// TVTest API が返す任意の文字列を安全にコピーする。
std::wstring CopyOptionalText(const wchar_t *text) {
    if (text == nullptr) {
        return L"";
    }
    return text;
}

constexpr int kSettingsCheckPanel = 2101;
constexpr int kSettingsZappingInterval = 2102;
constexpr UINT kBaseDpi = 96;
constexpr wchar_t kPanelClassName[] = L"ViewReservation.PanelWindow";
constexpr wchar_t kPanelHeaderClassName[] = L"ViewReservation.PanelHeader";
constexpr int kPanelHeaderMargin = 4;
constexpr int kPanelHeaderGap = 3;
constexpr int kPanelHeaderHeight = ViewReservationUi::kPanelActionButtonSide;
constexpr wchar_t kPanelViewportClassName[] = L"ViewReservation.PanelViewport";
constexpr wchar_t kAutomationBarClassName[] = L"ViewReservation.AutomationBar";

UINT GetWindowDpiOrDefault(HWND hwnd) {
    using GetDpiForWindowFn = UINT (WINAPI *)(HWND);
    static const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (getDpiForWindow != nullptr && hwnd != nullptr) {
        const UINT dpi = getDpiForWindow(hwnd);
        if (dpi != 0) {
            return dpi;
        }
    }

    HDC dc = GetDC(hwnd);
    if (dc != nullptr) {
        const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(hwnd, dc);
        if (dpi > 0) {
            return static_cast<UINT>(dpi);
        }
    }
    return kBaseDpi;
}

int ScaleForDpi(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), static_cast<int>(kBaseDpi));
}

COLORREF MixColor(COLORREF a, COLORREF b, int bPercent) {
    bPercent = (std::max)(0, (std::min)(100, bPercent));
    const int aPercent = 100 - bPercent;
    return RGB(
        (GetRValue(a) * aPercent + GetRValue(b) * bPercent) / 100,
        (GetGValue(a) * aPercent + GetGValue(b) * bPercent) / 100,
        (GetBValue(a) * aPercent + GetBValue(b) * bPercent) / 100);
}

ViewReservationUi::RgbColor ToRgbColor(COLORREF color) {
    return {
        static_cast<int>(GetRValue(color)),
        static_cast<int>(GetGValue(color)),
        static_cast<int>(GetBValue(color))
    };
}

COLORREF ToColorRef(const ViewReservationUi::RgbColor &color) {
    return RGB(color.Red, color.Green, color.Blue);
}

COLORREF GetPanelIndicatorColor(COLORREF background, COLORREF preferredText) {
    return ToColorRef(ViewReservationUi::CalculatePanelIndicatorColor(
        ToRgbColor(background), ToRgbColor(preferredText)));
}

struct PanelHostPalette {
    COLORREF Back;
    COLORREF Text;
    COLORREF TabBack;
    COLORREF TabBack2;
    COLORREF TabText;
    COLORREF Border;
};

COLORREF ResolvePanelHostColor(TVTest::CTVTestApp *app, LPCWSTR name, COLORREF fallback) {
    const COLORREF value = app != nullptr ? app->GetColor(name) : CLR_INVALID;
    return value == CLR_INVALID ? fallback : value;
}

PanelHostPalette ResolvePanelHostPalette(TVTest::CTVTestApp *app) {
    const COLORREF back = ResolvePanelHostColor(app, L"PanelBack", GetSysColor(COLOR_WINDOW));
    const COLORREF text = ResolvePanelHostColor(app, L"PanelText", GetSysColor(COLOR_WINDOWTEXT));
    const COLORREF tabBack = ResolvePanelHostColor(app, L"PanelTabBack", GetSysColor(COLOR_HIGHLIGHT));
    return {
        back,
        text,
        tabBack,
        ResolvePanelHostColor(app, L"PanelTabBack2", tabBack),
        ResolvePanelHostColor(app, L"PanelTabText", text),
        ResolvePanelHostColor(app, L"PanelTabBorder", MixColor(text, back, 55))
    };
}

void FillSolid(HDC dc, const RECT &rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    if (brush != nullptr) {
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }
}

// Action buttons and row chevrons share the same palette-composited glyph raster.
// Painting and viewport eligibility both use kPanelGlyphCanvasSide.
void DrawPanelGlyph(HDC dc, const RECT &bounds, ViewReservationUi::PanelGlyph glyph,
    COLORREF foreground, COLORREF background) {
    if (dc == nullptr) return;
    constexpr int side = ViewReservationUi::kPanelGlyphCanvasSide;
    if (bounds.right - bounds.left < side || bounds.bottom - bounds.top < side) return;
    const auto pixels = ViewReservationUi::ComposePanelGlyph(
        glyph, ToRgbColor(foreground), ToRgbColor(background));
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = side;
    info.bmiHeader.biHeight = -side;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    const int x = static_cast<int>(bounds.left) + (static_cast<int>(bounds.right - bounds.left) - side) / 2;
    const int y = static_cast<int>(bounds.top) + (static_cast<int>(bounds.bottom - bounds.top) - side) / 2;
    SetDIBitsToDevice(dc, x, y, side, side, 0, 0, 0, side, pixels.data(), &info, DIB_RGB_COLORS);
}

RECT ToWinRect(const ViewReservationUi::Rect &rect) {
    return RECT{
        static_cast<LONG>(rect.Left),
        static_cast<LONG>(rect.Top),
        static_cast<LONG>(rect.Right),
        static_cast<LONG>(rect.Bottom)
    };
}

void FillVerticalGradient(HDC dc, const RECT &rect, COLORREF top, COLORREF bottom) {
    constexpr int kSteps = 12;
    const int height = rect.bottom - rect.top;
    if (height <= 0) return;
    for (int i = 0; i < kSteps; ++i) {
        RECT band = rect;
        band.top = rect.top + height * i / kSteps;
        band.bottom = rect.top + height * (i + 1) / kSteps;
        const int mix = (i * 100) / (kSteps - 1);
        FillSolid(dc, band, MixColor(top, bottom, mix));
    }
}

// 一覧を更新範囲単位でメモリ上に描画し、画面へまとめて転送する。
class BufferedPaintSurface {
public:
    BufferedPaintSurface(HDC target, const RECT &rect)
        : m_target(target), m_rect(rect) {
        const int width = static_cast<int>(rect.right - rect.left);
        const int height = static_cast<int>(rect.bottom - rect.top);
        if (target == nullptr || width <= 0 || height <= 0) return;

        m_dc = CreateCompatibleDC(target);
        if (m_dc == nullptr) return;
        m_bitmap = CreateCompatibleBitmap(target, width, height);
        if (m_bitmap == nullptr) {
            DeleteDC(m_dc);
            m_dc = nullptr;
            return;
        }
        m_oldBitmap = SelectObject(m_dc, m_bitmap);
        SetViewportOrgEx(m_dc, -rect.left, -rect.top, nullptr);
    }

    ~BufferedPaintSurface() {
        if (m_dc == nullptr) return;
        const int width = static_cast<int>(m_rect.right - m_rect.left);
        const int height = static_cast<int>(m_rect.bottom - m_rect.top);
        BitBlt(m_target, m_rect.left, m_rect.top, width, height,
            m_dc, m_rect.left, m_rect.top, SRCCOPY);
        if (m_oldBitmap != nullptr) SelectObject(m_dc, m_oldBitmap);
        if (m_bitmap != nullptr) DeleteObject(m_bitmap);
        DeleteDC(m_dc);
    }

    HDC Get() const { return m_dc != nullptr ? m_dc : m_target; }

private:
    HDC m_target = nullptr;
    HDC m_dc = nullptr;
    HBITMAP m_bitmap = nullptr;
    HGDIOBJ m_oldBitmap = nullptr;
    RECT m_rect{};
};

HFONT CreateSettingsFont(UINT dpi) {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);

    using SystemParametersInfoForDpiFn = BOOL (WINAPI *)(UINT, UINT, PVOID, UINT, UINT);
    static const auto systemParametersInfoForDpi = reinterpret_cast<SystemParametersInfoForDpiFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SystemParametersInfoForDpi"));

    if (systemParametersInfoForDpi != nullptr &&
        systemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi)) {
        return CreateFontIndirectW(&metrics.lfMessageFont);
    }

    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        metrics.lfMessageFont.lfHeight = MulDiv(
            metrics.lfMessageFont.lfHeight, static_cast<int>(dpi), static_cast<int>(kBaseDpi));
        metrics.lfMessageFont.lfWidth = MulDiv(
            metrics.lfMessageFont.lfWidth, static_cast<int>(dpi), static_cast<int>(kBaseDpi));
        return CreateFontIndirectW(&metrics.lfMessageFont);
    }

    return nullptr;
}

struct SettingsDialogState {
    UINT Dpi = kBaseDpi;
    HFONT Font = nullptr;
    int TextHeight = 0;
    bool Accepted = false;
    bool Enabled = false;
    int ZappingIntervalSeconds = ViewReservationSettings::DefaultZappingIntervalSeconds;
};

LRESULT CALLBACK SettingsWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto *state = reinterpret_cast<SettingsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message) {
    case WM_CREATE: {
        const auto *create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = static_cast<SettingsDialogState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

        const UINT dpi = state != nullptr ? state->Dpi : kBaseDpi;
        auto sx = [dpi](int value) { return ScaleForDpi(value, dpi); };

        HFONT font = CreateSettingsFont(dpi);
        if (state != nullptr) {
            state->Font = font;
        }

        int textHeight = sx(16);
        if (font != nullptr) {
            HDC dc = GetDC(hwnd);
            if (dc != nullptr) {
                const HGDIOBJ oldFont = SelectObject(dc, font);
                TEXTMETRICW metrics{};
                if (GetTextMetricsW(dc, &metrics)) {
                    textHeight = metrics.tmHeight;
                }
                SelectObject(dc, oldFont);
                ReleaseDC(hwnd, dc);
            }
        }
        if (state != nullptr) {
            state->TextHeight = textHeight;
        }

        const int margin = sx(20);
        const int checkHeight = std::max(sx(28), textHeight + sx(10));
        const int noteHeight = std::max(sx(24), textHeight + sx(6));
        const int rowHeight = std::max(sx(28), textHeight + sx(10));
        const int buttonHeight = std::max(sx(32), textHeight + sx(12));
        const int buttonWidth = sx(96);
        const int clientWidth = sx(430);
        const int checkTop = sx(18);
        const int noteTop = checkTop + checkHeight + sx(8);
        const int zappingTop = noteTop + noteHeight + sx(10);
        const int buttonTop = zappingTop + rowHeight + sx(18);
        const int comboWidth = sx(110);
        const int labelWidth = sx(150);

        HWND check = CreateWindowExW(0, L"BUTTON", L"パネルを使用する",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            margin, checkTop, clientWidth - margin * 2, checkHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsCheckPanel)), g_hinstDLL, nullptr);
        HWND note = CreateWindowExW(0, L"STATIC", L"パネルの変更はTVTest再起動後に反映されます。",
            WS_CHILD | WS_VISIBLE,
            margin, noteTop, clientWidth - margin * 2, noteHeight, hwnd, nullptr, g_hinstDLL, nullptr);
        HWND zappingLabel = CreateWindowExW(0, L"STATIC", L"ザッピング間隔",
            WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
            margin, zappingTop, labelWidth, rowHeight, hwnd, nullptr, g_hinstDLL, nullptr);
        HWND zappingCombo = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            margin + labelWidth + sx(8), zappingTop, comboWidth, rowHeight * 7, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsZappingInterval)), g_hinstDLL, nullptr);
        HWND ok = CreateWindowExW(0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            clientWidth - margin - buttonWidth * 2 - sx(8), buttonTop, buttonWidth, buttonHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), g_hinstDLL, nullptr);
        HWND cancel = CreateWindowExW(0, L"BUTTON", L"キャンセル",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            clientWidth - margin - buttonWidth, buttonTop, buttonWidth, buttonHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), g_hinstDLL, nullptr);

        for (HWND child : { check, note, zappingLabel, zappingCombo, ok, cancel }) {
            if (child != nullptr && font != nullptr) {
                SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }
        }
        if (state != nullptr && state->Enabled && check != nullptr) {
            SendMessageW(check, BM_SETCHECK, BST_CHECKED, 0);
        }
        if (zappingCombo != nullptr) {
            int selectedIndex = 0;
            int index = 0;
            const int current = state != nullptr ? ViewReservationSettings::NormalizeZappingInterval(state->ZappingIntervalSeconds) : ViewReservationSettings::DefaultZappingIntervalSeconds;
            for (int seconds : ViewReservationSettings::ZappingIntervalsSeconds) {
                const std::wstring label = std::to_wstring(seconds) + L"秒";
                const LRESULT item = SendMessageW(zappingCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
                if (item != CB_ERR && item != CB_ERRSPACE) {
                    SendMessageW(zappingCombo, CB_SETITEMDATA, static_cast<WPARAM>(item), static_cast<LPARAM>(seconds));
                    if (seconds == current) selectedIndex = index;
                    ++index;
                }
            }
            SendMessageW(zappingCombo, CB_SETCURSEL, static_cast<WPARAM>(selectedIndex), 0);
        }
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            if (state != nullptr) {
                state->Enabled = SendDlgItemMessageW(hwnd, kSettingsCheckPanel, BM_GETCHECK, 0, 0) == BST_CHECKED;
                const LRESULT selection = SendDlgItemMessageW(hwnd, kSettingsZappingInterval, CB_GETCURSEL, 0, 0);
                if (selection != CB_ERR) {
                    const LRESULT data = SendDlgItemMessageW(hwnd, kSettingsZappingInterval, CB_GETITEMDATA, static_cast<WPARAM>(selection), 0);
                    if (data != CB_ERR) {
                        state->ZappingIntervalSeconds = ViewReservationSettings::NormalizeZappingInterval(static_cast<int>(data));
                    }
                }
                state->Accepted = true;
            }
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (state != nullptr && state->Font != nullptr) {
            DeleteObject(state->Font);
            state->Font = nullptr;
        }
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}
}

CViewReservationPlugin::CViewReservationPlugin() = default;

bool CViewReservationPlugin::MatchesProgramIdentity(const ReservationItem &item, WORD nid, WORD tsid, WORD sid, WORD eid) {
    const bool currentMatch =
        item.NetworkID == nid &&
        item.TransportStreamID == tsid &&
        item.ServiceID == sid &&
        item.EventID == eid;
    const bool originalMatch =
        item.OriginalNetworkID == nid &&
        item.OriginalTransportStreamID == tsid &&
        item.OriginalServiceID == sid &&
        item.OriginalEventID == eid;
    return currentMatch || originalMatch;
}

const ReservationItem *CViewReservationPlugin::FindMatchingReservation(
    const std::vector<ReservationItem> &items, WORD nid, WORD tsid, WORD sid, WORD eid) {
    const auto it = std::find_if(items.begin(), items.end(), [&](const ReservationItem &item) {
        return MatchesProgramIdentity(item, nid, tsid, sid, eid);
    });
    return it != items.end() ? &*it : nullptr;
}

CViewReservationPlugin::~CViewReservationPlugin() {
    delete m_manager;
    m_manager = nullptr;
}

bool CViewReservationPlugin::GetPluginInfo(TVTest::PluginInfo *pInfo) {
    if (pInfo == nullptr) {
        return false;
    }

    pInfo->Type = TVTest::PLUGIN_TYPE_NORMAL;
    pInfo->Flags = static_cast<TVTest::PluginFlag>(TVTest::PLUGIN_FLAG_ENABLEDEFAULT | TVTest::PLUGIN_FLAG_HASSETTINGS);
    pInfo->pszPluginName = L"ViewReservation";
    pInfo->pszCopyright = L"";
    pInfo->pszDescription = L"視聴予約・録画予約 v" VIEWRESERVATION_VERSION_WSTRING;
    return true;
}

std::wstring CViewReservationPlugin::GetModuleDirectory() const {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(g_hinstDLL, path, MAX_PATH);

    std::wstring result(path);
    const auto pos = result.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        result.resize(pos + 1);
    }
    return result;
}

bool CViewReservationPlugin::Initialize() {
    m_iniPath = GetModuleDirectory() + L"ViewReservation.ini";
    m_manager = new ReservationManager(m_iniPath);
    m_manager->Load();

    if (!m_pApp->SetEventCallback(EventCallback, this)) {
        return false;
    }

    m_pApp->EnableProgramGuideEvent(static_cast<TVTest::ProgramGuideEventFlag>(
        TVTest::PROGRAMGUIDE_EVENT_PROGRAM));

    m_panelEnabled = LoadPanelEnabled();
    m_zappingIntervalSeconds = LoadZappingIntervalSeconds();
    m_exitTimerHours = LoadExitTimerHours();
    if (m_panelEnabled) {
        m_panelRegistered = RegisterPanelItem(); // 登録失敗でも予約本体は継続する
    }

    m_startupTick = GetTickMs();
    m_epgWaitLogged = false;
    m_sessionTriggeredKeys.clear();

    m_stop = false;
    m_worker = std::thread(&CViewReservationPlugin::WorkerLoop, this);

    return true;
}

bool CViewReservationPlugin::Finalize() {
    StopZapping();
    StopExitTimer();
    DestroyPanelWindow();

    m_stop = true;
    m_cv.notify_all();

    if (m_worker.joinable()) {
        m_worker.join();
    }

    if (m_manager != nullptr) {
        m_manager->Save();
    }

    return true;
}

LRESULT CALLBACK CViewReservationPlugin::EventCallback(TVTest::EventCode Event, LPARAM lParam1, LPARAM lParam2, void *pClientData) {
    auto *self = static_cast<CViewReservationPlugin*>(pClientData);
    return self != nullptr ? self->HandleEvent(Event, lParam1, lParam2, pClientData) : 0;
}


bool CViewReservationPlugin::LoadPanelEnabled() const {
    if (m_iniPath.empty()) {
        return false;
    }
    return GetPrivateProfileIntW(L"Settings", L"PanelEnabled", 0, m_iniPath.c_str()) != 0;
}

bool CViewReservationPlugin::SavePanelEnabled(bool enabled) const {
    if (m_iniPath.empty()) {
        return false;
    }
    return WritePrivateProfileStringW(
        L"Settings", L"PanelEnabled", enabled ? L"1" : L"0", m_iniPath.c_str()) != FALSE;
}

int CViewReservationPlugin::LoadZappingIntervalSeconds() const {
    if (m_iniPath.empty()) return ViewReservationSettings::DefaultZappingIntervalSeconds;
    return ViewReservationSettings::NormalizeZappingInterval(GetPrivateProfileIntW(
        L"Settings", L"ZappingIntervalSeconds", ViewReservationSettings::DefaultZappingIntervalSeconds, m_iniPath.c_str()));
}

bool CViewReservationPlugin::SaveZappingIntervalSeconds(int seconds) const {
    if (m_iniPath.empty()) return false;
    const std::wstring value = std::to_wstring(ViewReservationSettings::NormalizeZappingInterval(seconds));
    return WritePrivateProfileStringW(
        L"Settings", L"ZappingIntervalSeconds", value.c_str(), m_iniPath.c_str()) != FALSE;
}

int CViewReservationPlugin::LoadExitTimerHours() const {
    if (m_iniPath.empty()) return ViewReservationSettings::DefaultExitTimerHours;
    return ViewReservationSettings::NormalizeExitTimerHours(GetPrivateProfileIntW(
        L"Settings", L"ExitTimerHours", ViewReservationSettings::DefaultExitTimerHours, m_iniPath.c_str()));
}

bool CViewReservationPlugin::SaveExitTimerHours(int hours) const {
    if (m_iniPath.empty()) return false;
    const std::wstring value = std::to_wstring(ViewReservationSettings::NormalizeExitTimerHours(hours));
    return WritePrivateProfileStringW(
        L"Settings", L"ExitTimerHours", value.c_str(), m_iniPath.c_str()) != FALSE;
}

bool CViewReservationPlugin::ShowPanelSettingsDialog(HWND hwndOwner, bool *enabled, int *zappingIntervalSeconds) {
    if (enabled == nullptr || zappingIntervalSeconds == nullptr) {
        return false;
    }

    static constexpr wchar_t kSettingsClassName[] = L"ViewReservation.SettingsWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SettingsWindowProc;
    wc.hInstance = g_hinstDLL;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kSettingsClassName;
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    SettingsDialogState state{};
    state.Enabled = *enabled;
    state.ZappingIntervalSeconds = ViewReservationSettings::NormalizeZappingInterval(*zappingIntervalSeconds);
    state.Dpi = GetWindowDpiOrDefault(hwndOwner);

    const int width = ScaleForDpi(430, state.Dpi);
    const int height = ScaleForDpi(230, state.Dpi);

    RECT ownerRect{};
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (hwndOwner != nullptr && GetWindowRect(hwndOwner, &ownerRect)) {
        x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
        y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    }

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kSettingsClassName, L"ViewReservation 設定",
        WS_CAPTION | WS_SYSMENU | WS_POPUP,
        x, y, width, height, hwndOwner, nullptr, g_hinstDLL, &state);
    if (hwnd == nullptr) {
        return false;
    }

    if (hwndOwner != nullptr) EnableWindow(hwndOwner, FALSE);
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    MSG msg{};
    bool quitRequested = false;
    int quitCode = 0;
    while (IsWindow(hwnd)) {
        const BOOL result = GetMessageW(&msg, nullptr, 0, 0);
        if (result <= 0) {
            if (result == 0) {
                quitRequested = true;
                quitCode = static_cast<int>(msg.wParam);
            }
            break;
        }
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (hwndOwner != nullptr) EnableWindow(hwndOwner, TRUE);
    if (quitRequested) PostQuitMessage(quitCode);
    UnregisterClassW(kSettingsClassName, g_hinstDLL);

    if (!state.Accepted) {
        return false;
    }
    *enabled = state.Enabled;
    *zappingIntervalSeconds = ViewReservationSettings::NormalizeZappingInterval(state.ZappingIntervalSeconds);
    return true;
}

bool CViewReservationPlugin::OnPluginSettings(HWND hwndOwner) {
    bool enabled = LoadPanelEnabled();
    int zappingIntervalSeconds = LoadZappingIntervalSeconds();
    if (!ShowPanelSettingsDialog(hwndOwner, &enabled, &zappingIntervalSeconds)) {
        return false;
    }
    if (!SavePanelEnabled(enabled) || !SaveZappingIntervalSeconds(zappingIntervalSeconds)) {
        MessageBoxW(hwndOwner, L"設定を保存できませんでした。", L"ViewReservation", MB_OK | MB_ICONERROR);
        return false;
    }
    m_panelEnabled = enabled;
    {
        std::lock_guard<std::mutex> lock(m_automationMutex);
        m_zappingIntervalSeconds = ViewReservationSettings::NormalizeZappingInterval(zappingIntervalSeconds);
        if (m_zappingActive) {
            m_zappingNextTick = GetTickMs() + static_cast<ULONGLONG>(m_zappingIntervalSeconds) * 1000ULL;
        }
    }
    m_cv.notify_all();
    return true;
}

bool CViewReservationPlugin::RegisterPanelItem() {
    TVTest::PanelItemInfo info{};
    info.Size = sizeof(info);
    info.Flags = TVTest::PANEL_ITEM_FLAG_NONE;
    info.Style = TVTest::PANEL_ITEM_STYLE_NONE;
    info.ID = kPanelItemId;
    info.pszIDText = L"ViewReservation";
    info.pszTitle = L"ViewReservation";
    info.hbmIcon = nullptr;
    if (!m_pApp->RegisterPanelItem(&info)) {
        return false;
    }

    TVTest::PanelItemSetInfo setInfo{};
    setInfo.Size = sizeof(setInfo);
    setInfo.Mask = TVTest::PANEL_ITEM_SET_INFO_MASK_STATE;
    setInfo.ID = kPanelItemId;
    setInfo.StateMask = TVTest::PANEL_ITEM_STATE_ENABLED;
    setInfo.State = TVTest::PANEL_ITEM_STATE_ENABLED;
    return m_pApp->SetPanelItem(&setInfo);
}

bool CViewReservationPlugin::OnPanelItemNotify(TVTest::PanelItemEventInfo *pInfo) {
    if (!m_panelRegistered || pInfo == nullptr || pInfo->ID != kPanelItemId) {
        return false;
    }

    switch (pInfo->Event) {
    case TVTest::PANEL_ITEM_EVENT_CREATE:
        return CreatePanelWindow(reinterpret_cast<TVTest::PanelItemCreateEventInfo*>(pInfo));

    case TVTest::PANEL_ITEM_EVENT_ACTIVATE:
        if (m_panelWindow != nullptr) {
            SetTimer(m_panelWindow, kPanelRefreshTimerId, kPanelRefreshIntervalMs, nullptr);
        }
        RefreshPanelRows(false);
        UpdatePanelViewingProjection(true);
        return true;

    case TVTest::PANEL_ITEM_EVENT_DEACTIVATE:
        m_panelHoverRow = -1;
        m_panelMarqueeActive = false;
        m_panelMouseTracking = false;
        if (m_panelWindow != nullptr) {
            KillTimer(m_panelWindow, kPanelRefreshTimerId);
            KillTimer(m_panelWindow, kPanelMarqueeTimerId);
            KillTimer(m_panelWindow, kPanelMarqueeDelayTimerId);
        }
        return true;

    case TVTest::PANEL_ITEM_EVENT_DISABLE:
        m_panelHoverRow = -1;
        m_panelMarqueeActive = false;
        m_panelMouseTracking = false;
        if (m_panelWindow != nullptr) {
            KillTimer(m_panelWindow, kPanelRefreshTimerId);
            KillTimer(m_panelWindow, kPanelMarqueeTimerId);
            KillTimer(m_panelWindow, kPanelMarqueeDelayTimerId);
        }
        return true;

    case TVTest::PANEL_ITEM_EVENT_ENABLE:
        return true;

    case TVTest::PANEL_ITEM_EVENT_STYLECHANGED:
    case TVTest::PANEL_ITEM_EVENT_FONTCHANGED:
        UpdatePanelFont();
        if (m_panelWindow != nullptr) {
            LayoutPanel();
            InvalidateRect(m_panelWindow, nullptr, FALSE);
        }
        return true;
    }

    return false;
}

bool CViewReservationPlugin::CreatePanelWindow(TVTest::PanelItemCreateEventInfo *pInfo) {
    if (pInfo == nullptr || pInfo->hwndParent == nullptr) {
        return false;
    }

    WNDCLASSEXW panelClass{};
    panelClass.cbSize = sizeof(panelClass);
    panelClass.lpfnWndProc = PanelWindowProc;
    panelClass.hInstance = g_hinstDLL;
    panelClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    panelClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    panelClass.lpszClassName = kPanelClassName;
    if (RegisterClassExW(&panelClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    WNDCLASSEXW headerClass{};
    headerClass.cbSize = sizeof(headerClass);
    headerClass.lpfnWndProc = PanelHeaderProc;
    headerClass.hInstance = g_hinstDLL;
    headerClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    headerClass.lpszClassName = kPanelHeaderClassName;
    if (RegisterClassExW(&headerClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        UnregisterClassW(kPanelClassName, g_hinstDLL);
        return false;
    }

    WNDCLASSEXW viewportClass{};
    viewportClass.cbSize = sizeof(viewportClass);
    viewportClass.lpfnWndProc = PanelViewportProc;
    viewportClass.hInstance = g_hinstDLL;
    viewportClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    viewportClass.style = CS_DBLCLKS;
    viewportClass.lpszClassName = kPanelViewportClassName;
    if (RegisterClassExW(&viewportClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        UnregisterClassW(kPanelHeaderClassName, g_hinstDLL);
        UnregisterClassW(kPanelClassName, g_hinstDLL);
        return false;
    }

    WNDCLASSEXW automationClass{};
    automationClass.cbSize = sizeof(automationClass);
    automationClass.lpfnWndProc = AutomationBarProc;
    automationClass.hInstance = g_hinstDLL;
    automationClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    automationClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    automationClass.lpszClassName = kAutomationBarClassName;
    if (RegisterClassExW(&automationClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        UnregisterClassW(kPanelViewportClassName, g_hinstDLL);
        UnregisterClassW(kPanelHeaderClassName, g_hinstDLL);
        UnregisterClassW(kPanelClassName, g_hinstDLL);
        return false;
    }

    const int width = (std::max)(1L, pInfo->ItemRect.right - pInfo->ItemRect.left);
    const int height = (std::max)(1L, pInfo->ItemRect.bottom - pInfo->ItemRect.top);

    m_panelWindow = CreateWindowExW(
        0,
        kPanelClassName,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        pInfo->ItemRect.left,
        pInfo->ItemRect.top,
        width,
        height,
        pInfo->hwndParent,
        nullptr,
        g_hinstDLL,
        this);

    if (m_panelWindow == nullptr) {
        UnregisterClassW(kAutomationBarClassName, g_hinstDLL);
        UnregisterClassW(kPanelViewportClassName, g_hinstDLL);
        UnregisterClassW(kPanelHeaderClassName, g_hinstDLL);
        UnregisterClassW(kPanelClassName, g_hinstDLL);
        return false;
    }

    pInfo->hwndItem = m_panelWindow;
    RefreshPanelRows(true);
    UpdatePanelViewingProjection(true);
    return true;
}

void CViewReservationPlugin::DestroyPanelWindow() {
    if (m_panelWindow != nullptr) {
        DestroyWindow(m_panelWindow);
    } else {
        ReleasePanelWindowResources(nullptr);
    }
    UnregisterClassW(kAutomationBarClassName, g_hinstDLL);
    UnregisterClassW(kPanelViewportClassName, g_hinstDLL);
    UnregisterClassW(kPanelHeaderClassName, g_hinstDLL);
    UnregisterClassW(kPanelClassName, g_hinstDLL);
}

void CViewReservationPlugin::ReleasePanelWindowResources(HWND destroyedWindow) {
    if (destroyedWindow != nullptr) {
        KillTimer(destroyedWindow, kPanelRefreshTimerId);
        KillTimer(destroyedWindow, kPanelMarqueeTimerId);
        KillTimer(destroyedWindow, kPanelMarqueeDelayTimerId);
    }
    if (m_panelFont != nullptr) {
        DeleteObject(m_panelFont);
        m_panelFont = nullptr;
    }
    if (m_panelAutomationFont != nullptr) {
        DeleteObject(m_panelAutomationFont);
        m_panelAutomationFont = nullptr;
    }
    if (m_panelBackBrush != nullptr) {
        DeleteObject(m_panelBackBrush);
        m_panelBackBrush = nullptr;
        m_panelBackBrushColor = CLR_INVALID;
    }
    if (destroyedWindow == nullptr || m_panelWindow == destroyedWindow) {
        m_panelWindow = nullptr;
        m_panelAutomationBar = nullptr;
        m_panelHeader = nullptr;
        m_panelHeaderPressed = ViewReservationUi::PanelWaveBarLayout::Item::None;
        m_panelHeaderFocused = ViewReservationUi::PanelWaveBarLayout::Item::Terrestrial;
        m_panelViewport = nullptr;
        m_panelStatus = nullptr;
        m_panelViewportScrollY = 0;
        m_panelSelectedRow = -1;
        m_panelWheelRemainder = 0;
        m_panelRows.clear();
        m_panelExpandedChannels.clear();
        m_panelViewingProjection = PanelViewingProjection{};
        m_panelWave = PanelWave::Terrestrial;
        m_panelRowsWave = PanelWave::Terrestrial;
        m_panelCatvAvailable = false;
    }
}

HBRUSH CViewReservationPlugin::EnsurePanelBackBrush(COLORREF color) {
    if (m_panelBackBrush == nullptr || m_panelBackBrushColor != color) {
        if (m_panelBackBrush != nullptr) DeleteObject(m_panelBackBrush);
        m_panelBackBrush = CreateSolidBrush(color);
        m_panelBackBrushColor = m_panelBackBrush != nullptr ? color : CLR_INVALID;
    }
    return m_panelBackBrush;
}

void CViewReservationPlugin::LayoutPanel() {
    if (m_panelWindow == nullptr) {
        return;
    }

    RECT client{};
    if (!GetClientRect(m_panelWindow, &client)) {
        return;
    }

    const int width = static_cast<int>(client.right - client.left);
    const int height = static_cast<int>(client.bottom - client.top);
    constexpr int margin = kPanelHeaderMargin;
    constexpr int gap = kPanelHeaderGap;
    constexpr int waveHeight = kPanelHeaderHeight;
    const int listTop = margin + waveHeight + gap;
    const UINT dpi = GetWindowDpiOrDefault(m_panelWindow);
    const int automationBarHeight = (std::max)(24, ScaleForDpi(28, dpi));
    const int automationTop = (std::max)(listTop + gap, height - margin - automationBarHeight);
    const int listHeight = (std::max)(0, automationTop - gap - listTop);

    // One header window is laid out and repainted as a single surface. Its
    // native child rectangles cannot collide during repeated WM_SIZE messages.
    if (m_panelHeader != nullptr) {
        SetWindowPos(m_panelHeader, nullptr, margin, margin,
            (std::max)(0, width - 2 * margin), waveHeight,
            SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(m_panelHeader, nullptr, FALSE);
    }

    const int contentWidth = (std::max)(20, width - margin * 2);
    if (m_panelViewport != nullptr) {
        const auto prior = GetPanelViewportGeometry().CaptureAnchor();
        ResetPanelMarqueeForLayoutChange();
        SetWindowPos(m_panelViewport, nullptr, margin, listTop, contentWidth, listHeight,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
        const auto resized = GetPanelViewportGeometry();
        SetPanelViewportScrollY(resized.RestoreAnchor(prior, prior.Row));
        RedrawWindow(m_panelViewport, nullptr, nullptr,
            RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    if (m_panelStatus != nullptr) {
        const int statusHeight = (std::min)(listHeight, (std::min)(48, (std::max)(28, listHeight / 5)));
        MoveWindow(m_panelStatus, margin, listTop, contentWidth, statusHeight, TRUE);
    }

    if (m_panelAutomationBar != nullptr) {
        MoveWindow(m_panelAutomationBar, margin, automationTop, contentWidth, automationBarHeight, TRUE);
        InvalidateRect(m_panelAutomationBar, nullptr, FALSE);
    }
}

ViewReservationUi::AutomationBarLayout CViewReservationPlugin::CalculateAutomationBarLayout(HDC dc) const {
    if (m_panelAutomationBar == nullptr) {
        return {};
    }

    RECT client{};
    if (!GetClientRect(m_panelAutomationBar, &client)) {
        return {};
    }

    HDC measureDc = dc;
    bool ownsDc = false;
    if (measureDc == nullptr) {
        measureDc = GetDC(m_panelAutomationBar);
        ownsDc = measureDc != nullptr;
    }

    const UINT dpi = GetWindowDpiOrDefault(m_panelAutomationBar);
    HFONT font = m_panelAutomationFont != nullptr
        ? m_panelAutomationFont
        : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ oldFont = measureDc != nullptr && font != nullptr
        ? SelectObject(measureDc, font)
        : nullptr;

    auto textWidth = [&](const wchar_t *sample) {
        SIZE size{};
        if (measureDc != nullptr && sample != nullptr &&
            GetTextExtentPoint32W(measureDc, sample, lstrlenW(sample), &size)) {
            return static_cast<int>(size.cx);
        }
        return 0;
    };

    ViewReservationUi::AutomationBarMetrics metrics{};
    metrics.ClientWidth = static_cast<int>(client.right - client.left);
    metrics.ClientHeight = static_cast<int>(client.bottom - client.top);
    metrics.SidePadding = ScaleForDpi(8, dpi);
    metrics.ControlHeight = ScaleForDpi(22, dpi);
    metrics.LabelGap = ScaleForDpi(7, dpi);
    metrics.StatusGap = ScaleForDpi(9, dpi);
    metrics.ControlGap = ScaleForDpi(9, dpi);
    metrics.CenterGap = ScaleForDpi(8, dpi);
    metrics.RightGroupPadding = ScaleForDpi(9, dpi);
    metrics.SeparatorWidth = (std::max)(1, ScaleForDpi(1, dpi));

    metrics.ZappingLabelWidth = textWidth(L"ザッピング");
    metrics.ExitLabelWidth = textWidth(L"終了");

    const int zappingStatusTextWidth = (std::max)(textWidth(L"停止中"), textWidth(L"巡回中"));
    const int toggleTextWidth = (std::max)(textWidth(L"開始"), textWidth(L"停止"));
    const int exitHoursTextWidth = textWidth(L"6h▼");

    metrics.ZappingStatusWidth = (std::max)(
        ScaleForDpi(42, dpi),
        zappingStatusTextWidth + ScaleForDpi(14, dpi));
    metrics.ZappingToggleWidth = (std::max)(
        ScaleForDpi(44, dpi),
        toggleTextWidth + ScaleForDpi(16, dpi));
    metrics.ExitHoursWidth = (std::max)(
        ScaleForDpi(45, dpi),
        exitHoursTextWidth + ScaleForDpi(10, dpi));
    metrics.ExitToggleWidth = (std::max)(
        ScaleForDpi(38, dpi),
        toggleTextWidth + ScaleForDpi(12, dpi));

    if (oldFont != nullptr) {
        SelectObject(measureDc, oldFont);
    }
    if (ownsDc) {
        ReleaseDC(m_panelAutomationBar, measureDc);
    }

    return ViewReservationUi::CalculateAutomationBarLayout(metrics);
}

void CViewReservationPlugin::UpdatePanelFont() {
    if (m_panelWindow == nullptr || m_pApp == nullptr) {
        return;
    }

    HFONT nextFont = nullptr;
    HFONT nextAutomationFont = nullptr;
    LOGFONTW logFont{};
    const int dpi = static_cast<int>(GetWindowDpiOrDefault(m_panelWindow));
    if (m_pApp->GetFont(L"PanelFont", &logFont, dpi)) {
        nextFont = CreateFontIndirectW(&logFont);

        // 下端操作は番組表より一段小さい文字を正本とする。
        // 同じPanelFontから派生させ、DPIを揃えたままサイズだけを抑える。
        LOGFONTW automationLogFont = logFont;
        automationLogFont.lfHeight = MulDiv(logFont.lfHeight, kPanelAutomationFontPercent, 100);
        if (automationLogFont.lfHeight == 0) {
            automationLogFont.lfHeight = logFont.lfHeight < 0 ? -1 : 1;
        }
        nextAutomationFont = CreateFontIndirectW(&automationLogFont);
    }

    HFONT applyFont = nextFont != nullptr ? nextFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    for (const int id : { kControlViewport, kControlStatus }) {
        HWND control = GetDlgItem(m_panelWindow, id);
        if (control != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(applyFont), TRUE);
        }
    }

    if (m_panelFont != nullptr) DeleteObject(m_panelFont);
    if (m_panelAutomationFont != nullptr) DeleteObject(m_panelAutomationFont);
    m_panelFont = nextFont;
    m_panelAutomationFont = nextAutomationFont;

    if (m_panelHeader != nullptr) InvalidateRect(m_panelHeader, nullptr, FALSE);
    if (m_panelAutomationBar != nullptr) {
        InvalidateRect(m_panelAutomationBar, nullptr, FALSE);
    }
}

bool CViewReservationPlugin::BuildPanelPrograms(
    const DriverChannelEntry &channel,
    ReservationItem *current,
    std::vector<ReservationItem> *future) const {
    if (current == nullptr || future == nullptr || m_pApp == nullptr) {
        return false;
    }

    *current = ReservationItem{};
    future->clear();

    TVTest::EpgEventList list{};
    list.NetworkID = channel.NetworkID;
    list.TransportStreamID = channel.TransportStreamID;
    list.ServiceID = channel.ServiceID;
    if (!m_pApp->GetEpgEventList(&list)) {
        return false;
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);
    const ULONGLONG nowValue = ViewReservationTime::FileTimeValue(now);
    const ULONGLONG horizonValue = nowValue + kPanelFutureHorizonSec * ViewReservationTime::TicksPerSecond;
    bool foundCurrent = false;

    auto makeItem = [&](const TVTest::EpgEventInfo *ev) {
        ReservationItem item{};
        item.NetworkID = channel.NetworkID;
        item.TransportStreamID = channel.TransportStreamID;
        item.ServiceID = channel.ServiceID;
        item.EventID = ev->EventID;
        item.OriginalNetworkID = item.NetworkID;
        item.OriginalTransportStreamID = item.TransportStreamID;
        item.OriginalServiceID = item.ServiceID;
        item.OriginalEventID = item.EventID;
        item.StartTime = ev->StartTime;
        item.Duration = ev->Duration;
        item.EventName = CopyOptionalText(ev->pszEventName);
        item.LastDriverName = channel.DriverName;
        if (item.EventName.empty()) {
            item.EventName = L"(番組名なし)";
        }
        return item;
    };

    for (WORD i = 0; i < list.NumEvents; ++i) {
        const TVTest::EpgEventInfo *ev = list.EventList[i];
        if (ev == nullptr) {
            continue;
        }

        const ULONGLONG start = ViewReservationTime::FileTimeValue(ev->StartTime);
        const ULONGLONG endTime = start + static_cast<ULONGLONG>(ev->Duration) * ViewReservationTime::TicksPerSecond;
        if (start <= nowValue && nowValue < endTime) {
            *current = makeItem(ev);
            foundCurrent = true;
            continue;
        }

        if (start > nowValue && start < horizonValue) {
            future->push_back(makeItem(ev));
        }
    }

    std::stable_sort(future->begin(), future->end(), [](const ReservationItem &a, const ReservationItem &b) {
        return ViewReservationTime::FileTimeValue(a.StartTime) < ViewReservationTime::FileTimeValue(b.StartTime);
    });

    m_pApp->FreeEpgEventList(&list);
    return foundCurrent;
}

CViewReservationPlugin::PanelWave CViewReservationPlugin::ResolvePanelWave(const DriverChannelEntry &entry) {
    switch (ResolveChannelDisplayWave(entry)) {
    case ChannelDisplayWave::Bs: return PanelWave::Bs;
    case ChannelDisplayWave::Cs: return PanelWave::Cs;
    case ChannelDisplayWave::Catv: return PanelWave::Catv;
    default: return PanelWave::Terrestrial;
    }
}

CViewReservationPlugin::PanelViewingProjection CViewReservationPlugin::CapturePanelViewingProjection() const {
    PanelViewingProjection projection{};
    if (m_pApp == nullptr) return projection;

    TVTest::ChannelInfo current{};
    current.Size = sizeof(current);
    if (!m_pApp->GetCurrentChannelInfo(&current)) return projection;

    DriverChannelEntry entry{};
    entry.NetworkID = current.NetworkID;
    entry.TransportStreamID = current.TransportStreamID;
    entry.ServiceID = current.ServiceID;
    projection.ChannelKey = MakeChannelServiceKey(entry);
    if (projection.ChannelKey == 0) return projection;

    projection.HasCurrentService = true;
    projection.Wave = ResolvePanelWave(entry);
    bool zapping = false;
    {
        std::lock_guard<std::mutex> lock(m_automationMutex);
        zapping = m_zappingActive && m_zappingWave == projection.Wave;
    }
    projection.Emphasis = zapping
        ? ViewReservationUi::PanelStationEmphasis::Zapping
        : ViewReservationUi::PanelStationEmphasis::Viewing;
    return projection;
}

void CViewReservationPlugin::CenterPanelViewingChannel(ULONGLONG channelKey) {
    if (m_panelViewport == nullptr || channelKey == 0) return;
    int targetIndex = -1;
    for (size_t i = 0; i < m_panelRows.size(); ++i) {
        if (!m_panelRows[i].IsFuture && m_panelRows[i].ChannelKey == channelKey) {
            targetIndex = static_cast<int>(i);
            break;
        }
    }
    if (targetIndex >= 0) {
        SetPanelViewportScrollY(GetPanelViewportGeometry().Scroll(ViewReservationUi::PanelViewportGeometry::ScrollAction::Center, targetIndex));
    }
}

ViewReservationUi::PanelViewportGeometry CViewReservationPlugin::GetPanelViewportGeometry() const {
    ViewReservationUi::PanelViewportGeometry geometry;
    geometry.Heights.reserve(m_panelRows.size());
    for (const auto &row : m_panelRows) {
        geometry.Heights.push_back(row.IsFuture ? kPanelFutureRowHeight : kPanelCurrentRowHeight);
    }
    RECT client{};
    if (m_panelViewport != nullptr && GetClientRect(m_panelViewport, &client)) {
        geometry.Height = static_cast<int>(client.bottom - client.top);
    }
    geometry.ScrollY = geometry.ClampScroll(m_panelViewportScrollY);
    return geometry;
}

bool CViewReservationPlugin::GetPanelViewportRowRect(int index, RECT *rect) const {
    if (rect == nullptr || m_panelViewport == nullptr) return false;
    const auto geometry = GetPanelViewportGeometry();
    int top = 0, bottom = 0;
    if (!geometry.RowRect(index, &top, &bottom)) return false;
    RECT client{};
    GetClientRect(m_panelViewport, &client);
    *rect = {client.left, top, client.right, bottom};
    return true;
}

void CViewReservationPlugin::InvalidatePanelViewportRow(int index) const {
    RECT rect{};
    if (GetPanelViewportRowRect(index, &rect)) {
        InvalidateRect(m_panelViewport, &rect, FALSE);
    }
}

void CViewReservationPlugin::SetPanelViewportScrollY(int scrollY) {
    if (m_panelViewport == nullptr) return;
    const auto geometry = GetPanelViewportGeometry();
    const int next = geometry.ClampScroll(scrollY);
    if (next != m_panelViewportScrollY) {
        m_panelViewportScrollY = next;
        ResetPanelMarqueeForLayoutChange();
        InvalidateRect(m_panelViewport, nullptr, FALSE);
    }
    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = (std::max)(0, geometry.TotalHeight() - 1);
    info.nPage = static_cast<UINT>((std::max)(0, geometry.Height));
    info.nPos = m_panelViewportScrollY;
    SetScrollInfo(m_panelViewport, SB_VERT, &info, TRUE);
}

void CViewReservationPlugin::UpdatePanelViewingProjection(bool centerCurrent) {
    const PanelViewingProjection next = CapturePanelViewingProjection();
    const bool changed =
        next.HasCurrentService != m_panelViewingProjection.HasCurrentService ||
        next.ChannelKey != m_panelViewingProjection.ChannelKey ||
        next.Wave != m_panelViewingProjection.Wave ||
        next.Emphasis != m_panelViewingProjection.Emphasis;

    m_panelViewingProjection = next;
    if (changed && m_panelViewport != nullptr)
        InvalidateRect(m_panelViewport, nullptr, FALSE);

    if (centerCurrent && next.HasCurrentService && next.Wave == m_panelWave)
        CenterPanelViewingChannel(next.ChannelKey);
}

void CViewReservationPlugin::RefreshPanelRows(bool forceDriverCache) {
    if (m_panelViewport == nullptr || m_pApp == nullptr) {
        return;
    }

    // 更新表示は初回の空一覧だけに限定する。定期更新で重ね直してちらつかせない。
    const bool showStatus = m_panelRows.empty() && m_panelStatus != nullptr;
    if (showStatus) {
        SetWindowTextW(m_panelStatus, L"番組情報を更新しています…");
        ShowWindow(m_panelStatus, SW_SHOWNOACTIVATE);
        SetWindowPos(m_panelStatus, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    const int oldSelection = m_panelSelectedRow;
    ULONGLONG oldChannelKey = 0;
    WORD oldEventId = 0;
    bool oldSelectionWasFuture = false;
    if (oldSelection >= 0 && static_cast<size_t>(oldSelection) < m_panelRows.size()) {
        const auto &oldRow = m_panelRows[static_cast<size_t>(oldSelection)];
        oldChannelKey = oldRow.ChannelKey;
        oldEventId = oldRow.Program.EventID;
        oldSelectionWasFuture = oldRow.IsFuture;
    }

    const auto oldAnchorCandidate = GetPanelViewportGeometry().CaptureAnchor();

    m_driverCache.RebuildIfNeeded(m_pApp, forceDriverCache);
    auto channels = m_driverCache.SnapshotDisplayServices();

    const bool catvAvailable = std::any_of(channels.begin(), channels.end(), [](const DriverChannelEntry &entry) {
        return ResolveChannelDisplayWave(entry) == ChannelDisplayWave::Catv;
    });
    if (catvAvailable != m_panelCatvAvailable) {
        m_panelCatvAvailable = catvAvailable;
        if (!m_panelCatvAvailable && m_panelWave == PanelWave::Catv) {
            m_panelWave = PanelWave::Terrestrial;
        }
        if (m_panelWindow != nullptr) {
            LayoutPanel();
        }
    }

    const bool sameWave = m_panelRowsWave == m_panelWave;
    const auto oldAnchor = sameWave ? oldAnchorCandidate
        : ViewReservationUi::PanelViewportGeometry::Anchor{};
    const int oldTopIndex = oldAnchor.Row;
    ULONGLONG oldTopChannelKey = 0;
    WORD oldTopEventId = 0;
    bool oldTopWasFuture = false;
    if (oldTopIndex >= 0 && static_cast<size_t>(oldTopIndex) < m_panelRows.size()) {
        const auto &topRow = m_panelRows[static_cast<size_t>(oldTopIndex)];
        oldTopChannelKey = topRow.ChannelKey;
        oldTopEventId = topRow.Program.EventID;
        oldTopWasFuture = topRow.IsFuture;
    }

    // TVTestで有効な局だけを、TVTest / BonDriverの順序のまま表示する。
    // 同一NID/TSID/SIDはDriverCache側の表示正本で1局に統合済み。
    const auto reservations = m_manager != nullptr ? m_manager->Snapshot() : std::vector<ReservationItem>{};
    auto reservationKindOf = [&](const ReservationItem &program) {
        if (program.EventID == 0) return PanelReservationKind::None;
        const ReservationItem *existing = FindMatchingReservation(
            reservations, program.NetworkID, program.TransportStreamID, program.ServiceID, program.EventID);
        if (existing == nullptr) return PanelReservationKind::None;
        return existing->RecordOnTrigger ? PanelReservationKind::Record : PanelReservationKind::View;
    };

    std::vector<PanelRow> rows;
    for (auto channel : channels) {
        const PanelWave channelWave = ResolvePanelWave(channel);
        if (channelWave != m_panelWave) {
            continue;
        }

        const ULONGLONG key = MakeChannelServiceKey(channel);

        ReservationItem current{};
        std::vector<ReservationItem> future;
        const bool hasCurrent = BuildPanelPrograms(channel, &current, &future);
        if (!hasCurrent) {
            current.NetworkID = channel.NetworkID;
            current.TransportStreamID = channel.TransportStreamID;
            current.ServiceID = channel.ServiceID;
            current.OriginalNetworkID = channel.NetworkID;
            current.OriginalTransportStreamID = channel.TransportStreamID;
            current.OriginalServiceID = channel.ServiceID;
            current.EventName = L"番組情報なし";
            current.LastDriverName = channel.DriverName;
        }

        PanelRow header{};
        header.Channel = channel;
        header.Program = current;
        header.ChannelKey = key;
        header.IsFuture = false;
        header.HasProgram = hasCurrent;
        header.HasFuture = !future.empty();
        header.ReservationKind = reservationKindOf(header.Program);
        header.DisplayText = channel.ChannelName + L" " + current.EventName;
        rows.push_back(std::move(header));

        if (m_panelExpandedChannels.find(key) != m_panelExpandedChannels.end()) {
            for (auto &program : future) {
                PanelRow futureRow{};
                futureRow.Channel = channel;
                futureRow.Program = std::move(program);
                futureRow.ChannelKey = key;
                futureRow.IsFuture = true;
                futureRow.HasProgram = true;
                futureRow.HasFuture = false;
                futureRow.ReservationKind = reservationKindOf(futureRow.Program);
                futureRow.DisplayText = futureRow.Program.EventName;
                rows.push_back(std::move(futureRow));
            }
        }
    }

    auto sameRow = [&](const PanelRow &a, const PanelRow &b) {
        return a.ChannelKey == b.ChannelKey &&
            a.IsFuture == b.IsFuture &&
            a.HasProgram == b.HasProgram &&
            a.HasFuture == b.HasFuture &&
            a.ReservationKind == b.ReservationKind &&
            a.Channel.ChannelName == b.Channel.ChannelName &&
            a.Program.EventID == b.Program.EventID &&
            ViewReservationTime::FileTimeValue(a.Program.StartTime) == ViewReservationTime::FileTimeValue(b.Program.StartTime) &&
            a.Program.Duration == b.Program.Duration &&
            a.Program.EventName == b.Program.EventName;
    };
    bool rowsChanged = rows.size() != m_panelRows.size();
    if (!rowsChanged) {
        for (size_t i = 0; i < rows.size(); ++i) {
            if (!sameRow(rows[i], m_panelRows[i])) {
                rowsChanged = true;
                break;
            }
        }
    }
    if (!rowsChanged) {
        if (!sameWave) {
            m_panelSelectedRow = -1;
            SetPanelViewportScrollY(0);
        }
        m_panelRowsWave = m_panelWave;
        if (showStatus) ShowWindow(m_panelStatus, SW_HIDE);
        return;
    }

    m_panelRows.swap(rows);
    m_panelRowsWave = m_panelWave;
    m_panelHoverRow = -1;
    m_panelMarqueeActive = false;
    m_panelMouseTracking = false;
    if (m_panelWindow != nullptr) {
        KillTimer(m_panelWindow, kPanelMarqueeTimerId);
        KillTimer(m_panelWindow, kPanelMarqueeDelayTimerId);
    }
    m_panelMarqueeEpochTick = GetTickMs();
    const auto identityAt = [&](size_t i) -> ViewReservationUi::PanelViewportRowIdentity {
        const auto &row = m_panelRows[i];
        return {row.ChannelKey, row.Program.EventID, row.IsFuture};
    };
    const int selectIndex = sameWave
        ? ViewReservationUi::FindPanelViewportRow(m_panelRows.size(),
            {oldChannelKey, oldEventId, oldSelectionWasFuture}, oldSelection, identityAt) : -1;
    int topIndex = sameWave
        ? ViewReservationUi::FindPanelViewportRow(m_panelRows.size(),
            {oldTopChannelKey, oldTopEventId, oldTopWasFuture}, oldTopIndex, identityAt) : -1;
    if (topIndex < 0 && sameWave && oldTopIndex >= 0 && !m_panelRows.empty())
        topIndex = (std::min)(oldTopIndex, static_cast<int>(m_panelRows.size()) - 1);
    m_panelSelectedRow = selectIndex;
    const auto refreshedGeometry = GetPanelViewportGeometry();
    SetPanelViewportScrollY(refreshedGeometry.RestoreAnchor(oldAnchor, topIndex));
    InvalidateRect(m_panelViewport, nullptr, FALSE);
    if (showStatus) {
        ShowWindow(m_panelStatus, SW_HIDE);
    }
}

void CViewReservationPlugin::RefreshPanelDisplay(bool forceDriverCache) {
    RefreshPanelRows(forceDriverCache);
    UpdateAutomationControls();
    UpdatePanelViewingProjection(false);
}

void CViewReservationPlugin::SetPanelWave(PanelWave wave) {
    if (wave == m_panelWave) {
        UpdatePanelViewingProjection(true);
        return;
    }
    m_panelWave = wave;
    m_panelWheelRemainder = 0;
    m_panelHoverRow = -1;
    m_panelMarqueeActive = false;
    m_panelMouseTracking = false;
    if (m_panelWindow != nullptr) {
        KillTimer(m_panelWindow, kPanelMarqueeTimerId);
        KillTimer(m_panelWindow, kPanelMarqueeDelayTimerId);
    }
    m_panelMarqueeEpochTick = GetTickMs();
    if (m_panelHeader != nullptr) InvalidateRect(m_panelHeader, nullptr, FALSE);
    RefreshPanelRows(false);
    UpdatePanelViewingProjection(true);
}

void CViewReservationPlugin::TogglePanelChannelExpanded(size_t rowIndex) {
    if (rowIndex >= m_panelRows.size()) {
        return;
    }
    const PanelRow &row = m_panelRows[rowIndex];
    if (row.IsFuture || !row.HasFuture) {
        return;
    }

    const bool wasExpanded = m_panelExpandedChannels.find(row.ChannelKey) != m_panelExpandedChannels.end();
    m_panelExpandedChannels.clear();
    if (!wasExpanded) {
        m_panelExpandedChannels.insert(row.ChannelKey);
    }
    m_panelSelectedRow = -1;
    m_panelHoverRow = -1;
    m_panelMarqueeActive = false;
    m_panelMouseTracking = false;
    if (m_panelWindow != nullptr) {
        KillTimer(m_panelWindow, kPanelMarqueeTimerId);
        KillTimer(m_panelWindow, kPanelMarqueeDelayTimerId);
    }
    m_panelMarqueeEpochTick = GetTickMs();
    RefreshPanelRows(false);
}

bool CViewReservationPlugin::ApplyPanelReservation(const ReservationItem &source, bool record) {
    if (m_manager == nullptr || !source.EventID || source.Duration == 0) {
        return false;
    }
    ReservationItem item = source;
    item.RecordOnTrigger = record;
    std::wstring message;
    if (!m_manager->AddOrUpdateReservation(item, &message)) {
        Log(message);
        ShowReservationError(message);
        return false;
    }
    Log(message);
    RefreshPanelRows(false);
    m_cv.notify_all();
    return true;
}

bool CViewReservationPlugin::RemovePanelReservation(const ReservationItem &item) {
    if (m_manager == nullptr || !item.EventID) {
        return false;
    }
    std::wstring ignored;
    if (!m_manager->RemoveReservation(item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID, &ignored)) {
        return false;
    }
    Log(L"予約を解除しました: " + item.EventName);
    RefreshPanelRows(false);
    m_cv.notify_all();
    return true;
}

void CViewReservationPlugin::ShowPanelReservationMenu(size_t rowIndex, POINT screenPoint) {
    if (rowIndex >= m_panelRows.size() || m_manager == nullptr) {
        return;
    }
    const PanelRow &row = m_panelRows[rowIndex];
    if (!row.HasProgram || row.Program.EventID == 0 || row.Program.Duration == 0) {
        return;
    }

    const auto reservations = m_manager->Snapshot();
    const ReservationItem *existingReservation = FindMatchingReservation(
        reservations, row.Program.NetworkID, row.Program.TransportStreamID,
        row.Program.ServiceID, row.Program.EventID);
    const bool reserved = existingReservation != nullptr;
    const bool recordReserved = reserved && existingReservation->RecordOnTrigger;

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    constexpr UINT kReserveView = 1;
    constexpr UINT kReserveRecord = 2;
    constexpr UINT kRemoveReservation = 3;
    if (!reserved) {
        AppendMenuW(menu, MF_STRING, kReserveView, L"視聴予約");
        AppendMenuW(menu, MF_STRING, kReserveRecord, L"録画予約");
    } else if (recordReserved) {
        AppendMenuW(menu, MF_STRING, kReserveView, L"視聴予約に変更");
        AppendMenuW(menu, MF_STRING, kRemoveReservation, L"録画予約を解除");
    } else {
        AppendMenuW(menu, MF_STRING, kReserveRecord, L"録画予約に変更");
        AppendMenuW(menu, MF_STRING, kRemoveReservation, L"視聴予約を解除");
    }

    const UINT command = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        screenPoint.x, screenPoint.y, 0, m_panelWindow, nullptr);
    DestroyMenu(menu);

    if (command == kReserveView) {
        ApplyPanelReservation(row.Program, false);
    } else if (command == kReserveRecord) {
        ApplyPanelReservation(row.Program, true);
    } else if (command == kRemoveReservation) {
        RemovePanelReservation(row.Program);
    }
    InvalidateRect(m_panelViewport, nullptr, FALSE);
}

void CViewReservationPlugin::ActivatePanelCurrentProgram(size_t rowIndex) {
    if (rowIndex >= m_panelRows.size()) {
        return;
    }

    const PanelRow &row = m_panelRows[rowIndex];
    if (row.IsFuture) {
        return;
    }

    ReservationItem item = row.Program;
    item.NetworkID = row.Channel.NetworkID;
    item.TransportStreamID = row.Channel.TransportStreamID;
    item.ServiceID = row.Channel.ServiceID;
    item.OriginalNetworkID = row.Channel.NetworkID;
    item.OriginalTransportStreamID = row.Channel.TransportStreamID;
    item.OriginalServiceID = row.Channel.ServiceID;
    item.RecordOnTrigger = false;
    item.LastDriverName = row.Channel.DriverName;
    if (item.EventName.empty()) {
        item.EventName = row.Channel.ChannelName;
    }

    StopZapping();
    std::lock_guard<std::mutex> operationLock(m_zappingOperationMutex);
    if (TrySwitchToItem(item, L"視聴"))
        PostPanelUiStateRefresh(true);
}

ViewReservationUi::PanelWaveBarLayout CViewReservationPlugin::GetPanelHeaderLayout() const {
    RECT client{};
    if (m_panelHeader != nullptr) GetClientRect(m_panelHeader, &client);
    return ViewReservationUi::PanelWaveBarLayout::Calculate(
        static_cast<int>(client.right - client.left), 0, kPanelHeaderGap,
        kPanelHeaderHeight, m_panelCatvAvailable);
}

int CViewReservationPlugin::PanelHeaderCommand(ViewReservationUi::PanelWaveBarLayout::Item item) {
    using Item = ViewReservationUi::PanelWaveBarLayout::Item;
    switch (item) {
    case Item::Terrestrial: return kControlWaveGr;
    case Item::Bs: return kControlWaveBs;
    case Item::Cs: return kControlWaveCs;
    case Item::Catv: return kControlWaveCatv;
    case Item::Refresh: return kControlRefresh;
    case Item::Power: return kControlPower;
    default: return 0;
    }
}

void CViewReservationPlugin::DrawPanelHeader(HDC dc, const RECT &client) const {
    const PanelHostPalette palette = ResolvePanelHostPalette(m_pApp);
    FillSolid(dc, client, palette.Back);
    const auto layout = GetPanelHeaderLayout();
    for (const auto item : ViewReservationUi::PanelWaveBarLayout::Items) {
        const auto segment = layout.Get(item);
        if (segment.Width <= 0) continue;
        RECT bounds{segment.X, 0, segment.X + segment.Width, kPanelHeaderHeight};
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
        DrawPanelHeaderItem(dc, bounds, item);
        if (GetFocus() == m_panelHeader && item == m_panelHeaderFocused) {
            RECT focus = bounds;
            InflateRect(&focus, -2, -2);
            if (focus.right > focus.left && focus.bottom > focus.top) DrawFocusRect(dc, &focus);
        }
        if (saved != 0) RestoreDC(dc, saved);
    }
}

void CViewReservationPlugin::DrawPanelHeaderItem(
    HDC dc, const RECT &rect, ViewReservationUi::PanelWaveBarLayout::Item item) const {
    using Item = ViewReservationUi::PanelWaveBarLayout::Item;
    const bool isPower = item == Item::Power;
    const bool isRefresh = item == Item::Refresh;
    const bool isAction = isPower || isRefresh;
    PanelWave wave = PanelWave::Terrestrial;
    const wchar_t *label = L"";
    switch (item) {
    case Item::Terrestrial: label = L"地上波"; break;
    case Item::Bs: wave = PanelWave::Bs; label = L"BS"; break;
    case Item::Cs: wave = PanelWave::Cs; label = L"CS"; break;
    case Item::Catv: wave = PanelWave::Catv; label = L"CATV"; break;
    case Item::Refresh: case Item::Power: break;
    default: return;
    }

    const PanelHostPalette palette = ResolvePanelHostPalette(m_pApp);
    const COLORREF panelBack = palette.Back;
    const COLORREF panelText = palette.Text;
    const COLORREF tab1 = palette.TabBack;
    const COLORREF tab2 = palette.TabBack2;
    const COLORREF tabText = palette.TabText;
    const COLORREF border = palette.Border;
    const bool active = !isAction && wave == m_panelWave;

    if (active) {
        FillVerticalGradient(dc, rect, tab1, tab2);
    } else {
        FillSolid(dc, rect, MixColor(panelBack, tab1, 8));
    }

    if (isAction) {
        const COLORREF actionBack = MixColor(panelBack, tab1, 8);
        const COLORREF iconColor = GetPanelIndicatorColor(actionBack, panelText);
        DrawPanelGlyph(dc, rect,
            isPower ? ViewReservationUi::PanelGlyph::Power : ViewReservationUi::PanelGlyph::Refresh,
            iconColor, actionBack);
    }

    // Draw the shared button border last so the full DIB never erases it.
    RECT edge = rect;
    HBRUSH brush = CreateSolidBrush(MixColor(border, panelBack, 25));
    if (brush != nullptr) {
        FrameRect(dc, &edge, brush);
        DeleteObject(brush);
    }
    if (isAction) return;

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, active ? tabText : panelText);
    HFONT font = m_panelFont != nullptr ? m_panelFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ oldFont = font != nullptr ? SelectObject(dc, font) : nullptr;
    RECT text = rect;
    if (text.right - text.left >= 12)
        DrawTextW(dc, label, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (oldFont != nullptr) SelectObject(dc, oldFont);
}

void CViewReservationPlugin::DrawAutomationBar(HDC dc) const {
    if (dc == nullptr || m_panelAutomationBar == nullptr) return;

    RECT rc{};
    GetClientRect(m_panelAutomationBar, &rc);
    const auto layout = CalculateAutomationBarLayout(dc);

    const PanelHostPalette palette = ResolvePanelHostPalette(m_pApp);
    const COLORREF panelBack = palette.Back;
    const COLORREF panelText = palette.Text;
    const COLORREF tabBack = palette.TabBack;
    const COLORREF tabText = palette.TabText;
    const COLORREF border = palette.Border;

    FillSolid(dc, rc, panelBack);

    RECT separator = rc;
    separator.bottom = (std::min)(separator.bottom, separator.top + 1);
    FillSolid(dc, separator, MixColor(panelText, panelBack, 82));

    if (!layout.ExitSeparator.Empty()) {
        FillSolid(dc, ToWinRect(layout.ExitSeparator), MixColor(panelText, panelBack, 76));
    }

    bool zappingActive = false;
    bool exitActive = false;
    int exitHours = ViewReservationSettings::DefaultExitTimerHours;
    ULONGLONG exitDeadlineTick = 0;
    {
        std::lock_guard<std::mutex> lock(m_automationMutex);
        zappingActive = m_zappingActive;
        exitActive = m_exitTimerActive;
        exitHours = m_exitTimerHours;
        exitDeadlineTick = m_exitDeadlineTick;
    }

    int displayHours = exitHours;
    if (exitActive) {
        constexpr ULONGLONG kHourMs = 60ULL * 60ULL * 1000ULL;
        const ULONGLONG now = GetTickMs();
        const ULONGLONG remaining = exitDeadlineTick > now ? exitDeadlineTick - now : 0;
        displayHours = static_cast<int>((remaining + kHourMs - 1) / kHourMs);
        displayHours = (std::max)(ViewReservationSettings::ExitTimerMinHours, (std::min)(ViewReservationSettings::ExitTimerMaxHours, displayHours));
    }

    HFONT font = m_panelAutomationFont != nullptr
        ? m_panelAutomationFont
        : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ oldFont = font != nullptr ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);

    auto drawText = [&](const ViewReservationUi::Rect &layoutRect, const wchar_t *label, COLORREF color, UINT format) {
        if (layoutRect.Empty() || label == nullptr) return;
        RECT textRect = ToWinRect(layoutRect);
        SetTextColor(dc, color);
        DrawTextW(dc, label, -1, &textRect, format | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    };

    auto drawButton = [&](const ViewReservationUi::Rect &layoutRect, const wchar_t *label, bool active, bool enabled) {
        if (layoutRect.Empty()) return;
        const RECT rect = ToWinRect(layoutRect);
        const COLORREF buttonBack = active
            ? MixColor(panelBack, tabBack, 26)
            : MixColor(panelBack, tabBack, 5);
        FillSolid(dc, rect, buttonBack);
        HBRUSH brush = CreateSolidBrush(MixColor(border, panelBack, active ? 38 : 62));
        if (brush != nullptr) {
            FrameRect(dc, &rect, brush);
            DeleteObject(brush);
        }
        const COLORREF textColor = !enabled
            ? MixColor(panelText, panelBack, 58)
            : (active ? tabText : MixColor(panelText, panelBack, 8));
        drawText(layoutRect, label, textColor, DT_CENTER);
    };

    if (layout.ShowLabels()) {
        drawText(layout.ZappingLabel, L"ザッピング", MixColor(panelText, panelBack, 18), DT_LEFT);
        drawText(layout.ExitLabel, L"終了", MixColor(panelText, panelBack, 18), DT_LEFT);
    }

    if (layout.ShowStatus() && !layout.ZappingStatus.Empty()) {
        const RECT pill = ToWinRect(layout.ZappingStatus);
        const COLORREF fill = zappingActive ? MixColor(panelBack, tabBack, 20) : MixColor(panelBack, panelText, 6);
        const COLORREF edge = zappingActive ? MixColor(border, panelBack, 42) : MixColor(border, panelBack, 62);
        HBRUSH fillBrush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, edge);
        HGDIOBJ oldBrush = fillBrush != nullptr ? SelectObject(dc, fillBrush) : nullptr;
        HGDIOBJ oldPen = pen != nullptr ? SelectObject(dc, pen) : nullptr;
        RoundRect(dc, pill.left, pill.top, pill.right, pill.bottom,
            pill.bottom - pill.top, pill.bottom - pill.top);
        if (oldPen != nullptr) SelectObject(dc, oldPen);
        if (oldBrush != nullptr) SelectObject(dc, oldBrush);
        if (pen != nullptr) DeleteObject(pen);
        if (fillBrush != nullptr) DeleteObject(fillBrush);
        drawText(layout.ZappingStatus, zappingActive ? L"巡回中" : L"停止中",
            zappingActive ? MixColor(panelText, tabBack, 8) : MixColor(panelText, panelBack, 10), DT_CENTER);
    }

    drawButton(layout.ZappingToggle, zappingActive ? L"停止" : L"開始", zappingActive, true);
    const std::wstring hourText = std::to_wstring(displayHours) + (exitActive ? L"h" : L"h▼");
    drawButton(layout.ExitHours, hourText.c_str(), false, !exitActive);
    drawButton(layout.ExitToggle, exitActive ? L"停止" : L"開始", exitActive, true);

    if (oldFont != nullptr) SelectObject(dc, oldFont);
}

CViewReservationPlugin::PanelRowLayout CViewReservationPlugin::CalculatePanelRowLayout(
    const RECT &itemRect, HDC dc) const {
    PanelRowLayout layout{};

    // Win32 RECT は LONG、レイアウト定数は int なので、
    // 以後の計算は最初に int へ正規化して std::min/max の型を一本化する。
    const int itemLeft = static_cast<int>(itemRect.left);
    const int itemRight = static_cast<int>(itemRect.right);

    const int rcLeft = itemLeft + 5;
    const int rcRight = itemRight - 4;

    layout.Content.left = rcLeft;
    layout.Content.top = itemRect.top;
    layout.Content.right = (std::max)(
        rcLeft,
        rcRight - kPanelArrowVisualWidth - kPanelReservationVisualWidth);
    layout.Content.bottom = itemRect.bottom;

    layout.Arrow.left = (std::max)(
        rcLeft,
        rcRight - kPanelArrowVisualWidth + 1);
    layout.Arrow.top = itemRect.top;
    layout.Arrow.right = rcRight;
    layout.Arrow.bottom = itemRect.bottom;

    layout.Reservation.right = (std::max)(
        rcLeft,
        rcRight - kPanelArrowVisualWidth);
    layout.Reservation.left = (std::max)(
        rcLeft,
        static_cast<int>(layout.Reservation.right) - kPanelReservationVisualWidth);
    layout.Reservation.top = itemRect.top;
    layout.Reservation.bottom = itemRect.bottom;

    SIZE timeSize{};
    constexpr wchar_t kTimeSample[] = L"00:00-00:00";
    int measuredTimeWidth = 0;
    if (dc != nullptr &&
        GetTextExtentPoint32W(dc, kTimeSample, 11, &timeSize)) {
        measuredTimeWidth = static_cast<int>(timeSize.cx);
    }

    const int desiredTimeWidth = (std::max)(
        1,
        measuredTimeWidth + kPanelTimeColumnPadding);
    const int contentWidth = (std::max)(
        0,
        static_cast<int>(layout.Content.right) - static_cast<int>(layout.Content.left));
    layout.TimeWidth = (std::min)(desiredTimeWidth, contentWidth);
    layout.TitleLeft = (std::min)(
        static_cast<int>(layout.Content.right),
        static_cast<int>(layout.Content.left) + layout.TimeWidth + kPanelTitleGap);
    return layout;
}

void CViewReservationPlugin::ResetPanelMarqueeForLayoutChange() {
    m_panelHoverRow = -1;
    m_panelMarqueeActive = false;
    m_panelMouseTracking = false;
    m_panelMarqueeEpochTick = GetTickMs();
    if (m_panelWindow != nullptr) {
        KillTimer(m_panelWindow, kPanelMarqueeTimerId);
        KillTimer(m_panelWindow, kPanelMarqueeDelayTimerId);
    }
}

void CViewReservationPlugin::DrawPanelRow(HDC dc, const RECT &rowRect,
    const ViewReservationUi::PanelViewportGeometry &geometry, size_t rowIndex) const {
    if (dc == nullptr || rowIndex >= m_panelRows.size()) return;

    const PanelRow &row = m_panelRows[rowIndex];

    const PanelHostPalette palette = ResolvePanelHostPalette(m_pApp);
    const COLORREF panelBack = palette.Back;
    const COLORREF panelText = palette.Text;
    const COLORREF accentTop = palette.TabBack;
    const COLORREF border = palette.Border;

    // 選択操作とは独立して実際の視聴状態を描画する。
    // 実際のTVTest選局状態だけを正本に、通常視聴とザッピングを色分けする。
    ViewReservationUi::PanelStationEmphasis emphasis = ViewReservationUi::PanelStationEmphasis::None;
    if (!row.IsFuture && m_panelViewingProjection.HasCurrentService &&
        m_panelViewingProjection.Wave == m_panelWave &&
        m_panelViewingProjection.ChannelKey == row.ChannelKey) {
        emphasis = m_panelViewingProjection.Emphasis;
    }

    COLORREF rowBack = panelBack;
    if (emphasis != ViewReservationUi::PanelStationEmphasis::None) {
        rowBack = ToColorRef(ViewReservationUi::ResolvePanelStationBackground(emphasis));
        FillSolid(dc, rowRect, rowBack);
    } else if (row.IsFuture) {
        rowBack = MixColor(panelBack, accentTop, 10);
        FillSolid(dc, rowRect, rowBack);
    } else {
        FillSolid(dc, rowRect, panelBack);
    }

    const auto &presentation = ViewReservationUi::kPanelPresentationColors;
    const COLORREF stationText = ToColorRef(presentation.StationText);
    const COLORREF timeText = ToColorRef(presentation.TimeText);
    const COLORREF currentProgramText = ToColorRef(presentation.CurrentProgramText);
    const COLORREF futureProgramText = ToColorRef(presentation.FutureProgramText);
    const COLORREF programText = row.IsFuture ? futureProgramText : currentProgramText;

    SetBkMode(dc, TRANSPARENT);
    HFONT font = m_panelFont != nullptr ? m_panelFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ oldFont = nullptr;
    if (font != nullptr) {
        oldFont = SelectObject(dc, font);
    }

    // 右端の予約状態列・展開列・時刻列を全描画経路で同じ計算に統一する。
    const PanelRowLayout layout = CalculatePanelRowLayout(rowRect, dc);
    RECT content = layout.Content;

    auto formatTime = [](const ReservationItem &item) {
        if (item.Duration == 0) return std::wstring{};
        FILETIME ft{};
        SYSTEMTIME end{};
        if (!SystemTimeToFileTime(&item.StartTime, &ft)) return std::wstring{};
        ULARGE_INTEGER value{};
        value.LowPart = ft.dwLowDateTime;
        value.HighPart = ft.dwHighDateTime;
        value.QuadPart += static_cast<ULONGLONG>(item.Duration) * ViewReservationTime::TicksPerSecond;
        ft.dwLowDateTime = value.LowPart;
        ft.dwHighDateTime = value.HighPart;
        if (!FileTimeToSystemTime(&ft, &end)) return std::wstring{};
        wchar_t text[24]{};
        swprintf_s(text, L"%02u:%02u-%02u:%02u", item.StartTime.wHour, item.StartTime.wMinute, end.wHour, end.wMinute);
        return std::wstring(text);
    };

    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    const int fontHeight = (std::max)(1, static_cast<int>(tm.tmHeight));
    const int lineHeight = (std::max)(14, fontHeight);
    const auto wholeTextLine = [&](const RECT &rect) {
        return geometry.ContainsCenteredLine(rect.top, rect.bottom, lineHeight);
    };
    const int timeWidth = layout.TimeWidth;
    const int titleStart = layout.TitleLeft;

    RECT titleRect{};
    if (!row.IsFuture) {
        RECT stationRect = content;
        stationRect.bottom = (std::min)(stationRect.bottom, stationRect.top + lineHeight + 3);
        const std::wstring station = row.Channel.ChannelName.empty() ? L"(局名未取得)" : row.Channel.ChannelName;
        SetTextColor(dc, stationText);
        if (wholeTextLine(stationRect)) {
            DrawTextW(dc, station.c_str(), -1, &stationRect,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        }

        RECT second = content;
        second.top = stationRect.bottom;
        const std::wstring time = formatTime(row.Program);
        RECT timeRect = second;
        timeRect.right = (std::min)(second.right, second.left + timeWidth);
        SetTextColor(dc, timeText);
        if (wholeTextLine(timeRect)) {
            DrawTextW(dc, time.c_str(), -1, &timeRect,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
        titleRect = second;
        titleRect.left = titleStart;
    } else {
        // 未来番組は上下にわずかな余白を持たせ、展開時の詰まり感を抑える。
        content.top += 2;
        content.bottom -= 2;
        const std::wstring time = formatTime(row.Program);
        RECT timeRect = content;
        timeRect.right = (std::min)(content.right, timeRect.left + timeWidth);
        SetTextColor(dc, timeText);
        if (wholeTextLine(timeRect)) {
            DrawTextW(dc, time.c_str(), -1, &timeRect,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
        titleRect = content;
        titleRect.left = titleStart;
    }

    const std::wstring &title = row.Program.EventName;
    row.NeedsMarquee = false;
    if (!title.empty() && titleRect.right > titleRect.left && wholeTextLine(titleRect)) {
        SetTextColor(dc, programText);
        SIZE size{};
        GetTextExtentPoint32W(dc, title.c_str(), static_cast<int>(title.size()), &size);
        const int titlePixelWidth = static_cast<int>(size.cx);
        const int available = static_cast<int>(titleRect.right - titleRect.left);
        row.NeedsMarquee = titlePixelWidth > available;
        const int y = static_cast<int>(titleRect.top)
            + (static_cast<int>(titleRect.bottom - titleRect.top) - lineHeight) / 2;
        RECT clip = titleRect;
        if (titlePixelWidth <= available) {
            ExtTextOutW(dc, titleRect.left, y, ETO_CLIPPED, &clip,
                title.c_str(), static_cast<UINT>(title.size()), nullptr);
        } else if (static_cast<int>(rowIndex) == m_panelHoverRow && !m_panelMarqueeActive) {
            // ホバー待機中は通常位置の全文をそのままクリップ表示する。
            // 省略記号付き表示から移動描画へ直接切り替えず、開始位置を連続させる。
            ExtTextOutW(dc, titleRect.left, y, ETO_CLIPPED, &clip,
                title.c_str(), static_cast<UINT>(title.size()), nullptr);
        } else if (static_cast<int>(rowIndex) == m_panelHoverRow && m_panelMarqueeActive) {
            // 見切れタイトルはホバー中の1行だけ、右から左へ滑らかに循環する。
            const int gap = (std::max)(28, available / 3);
            const int travel = (std::max)(1, titlePixelWidth + gap);
            const ULONGLONG nowTick = GetTickMs();
            const ULONGLONG elapsed = nowTick >= m_panelMarqueeEpochTick ? nowTick - m_panelMarqueeEpochTick : 0;
            const int phase = static_cast<int>(
                ((elapsed * static_cast<ULONGLONG>(kPanelMarqueePixelsPerSecond)) / 1000ULL)
                % static_cast<ULONGLONG>(travel));
            const int x1 = titleRect.left - phase;
            const int x2 = x1 + titlePixelWidth + gap;
            if (x1 < static_cast<int>(titleRect.right) && x1 + titlePixelWidth > static_cast<int>(titleRect.left)) {
                ExtTextOutW(dc, x1, y, ETO_CLIPPED, &clip,
                    title.c_str(), static_cast<UINT>(title.size()), nullptr);
            }
            if (x2 < static_cast<int>(titleRect.right) && x2 + titlePixelWidth > static_cast<int>(titleRect.left)) {
                ExtTextOutW(dc, x2, y, ETO_CLIPPED, &clip,
                    title.c_str(), static_cast<UINT>(title.size()), nullptr);
            }
        } else {
            DrawTextW(dc, title.c_str(), -1, &titleRect,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
    }

    if (row.ReservationKind != PanelReservationKind::None) {
        RECT reservationRect = layout.Reservation;
        const bool recordReserved = row.ReservationKind == PanelReservationKind::Record;
        const COLORREF reservationColor = ToColorRef(
            ViewReservationUi::ResolvePanelReservationMarkColor(recordReserved));
        SetTextColor(dc, reservationColor);
        const wchar_t *reservationMark = recordReserved ? L"●" : L"▶";
        if (wholeTextLine(reservationRect)) {
            DrawTextW(dc, reservationMark, -1, &reservationRect,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        SetTextColor(dc, programText);
    }

    if (!row.IsFuture && row.HasFuture) {
        const bool expanded = m_panelExpandedChannels.find(row.ChannelKey) != m_panelExpandedChannels.end();
        const COLORREF arrowColor = GetPanelIndicatorColor(rowBack, panelText);
        if (geometry.ContainsCenteredLine(layout.Arrow.top, layout.Arrow.bottom,
            ViewReservationUi::kPanelGlyphCanvasSide)) {
            DrawPanelGlyph(dc, layout.Arrow,
                expanded ? ViewReservationUi::PanelGlyph::ChevronUp
                         : ViewReservationUi::PanelGlyph::ChevronDown,
                arrowColor, rowBack);
        }
        SetTextColor(dc, programText);
    }

    RECT separator = rowRect;
    separator.top = separator.bottom - 1;
    const bool expandedHeader = !row.IsFuture
        && m_panelExpandedChannels.find(row.ChannelKey) != m_panelExpandedChannels.end();
    FillSolid(dc, separator, MixColor(border, rowBack, expandedHeader ? 68 : 35));

    if (oldFont != nullptr) {
        SelectObject(dc, oldFont);
    }
}

LRESULT CALLBACK CViewReservationPlugin::PanelViewportProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto *self = reinterpret_cast<CViewReservationPlugin*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto *create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<CViewReservationPlugin*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self == nullptr) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        if (dc != nullptr) {
            RECT client{};
            GetClientRect(hwnd, &client);
            const RECT dirty = paint.rcPaint;
            BufferedPaintSurface surface(dc, dirty);
            HDC target = surface.Get();
            FillSolid(target, dirty, ResolvePanelHostPalette(self->m_pApp).Back);
            const int savedDc = SaveDC(target);
            IntersectClipRect(target, dirty.left, dirty.top, dirty.right, dirty.bottom);
            const auto geometry = self->GetPanelViewportGeometry();
            geometry.ForEachVisible([&](int index, int y, int height) {
                RECT rowRect{client.left, y, client.right, y + height};
                RECT overlap{};
                if (!IntersectRect(&overlap, &rowRect, &dirty)) return;
                self->DrawPanelRow(target, rowRect, geometry, static_cast<size_t>(index));
            });
            if (savedDc != 0) RestoreDC(target, savedDc);
        }
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_VSCROLL: {
        int top = self->m_panelViewportScrollY;
        const auto geometry = self->GetPanelViewportGeometry();
        using ScrollAction = ViewReservationUi::PanelViewportGeometry::ScrollAction;
        switch (LOWORD(wParam)) {
        case SB_LINEUP: top = geometry.Scroll(ScrollAction::Line, -1); break;
        case SB_LINEDOWN: top = geometry.Scroll(ScrollAction::Line, 1); break;
        case SB_PAGEUP: top = geometry.Scroll(ScrollAction::Page, -1); break;
        case SB_PAGEDOWN: top = geometry.Scroll(ScrollAction::Page, 1); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = SIF_TRACKPOS;
            if (GetScrollInfo(hwnd, SB_VERT, &info)) top = geometry.Scroll(ScrollAction::Absolute, info.nTrackPos);
            break;
        }
        case SB_TOP: top = geometry.Scroll(ScrollAction::Top); break;
        case SB_BOTTOM: top = geometry.Scroll(ScrollAction::Bottom); break;
        default: return 0;
        }
        if (top != self->m_panelViewportScrollY) self->m_panelSelectedRow = -1;
        self->SetPanelViewportScrollY(top);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        const UINT lines = [] { UINT value = 3; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &value, 0); return value; }();
        if (lines == 0) return 0;
        self->m_panelWheelRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
        const int steps = self->m_panelWheelRemainder / WHEEL_DELTA;
        self->m_panelWheelRemainder %= WHEEL_DELTA;
        if (steps != 0) {
            const auto geometry = self->GetPanelViewportGeometry();
            using ScrollAction = ViewReservationUi::PanelViewportGeometry::ScrollAction;
            const int next = lines == WHEEL_PAGESCROLL
                ? geometry.Scroll(ScrollAction::Page, -steps)
                : geometry.Scroll(ScrollAction::Line,
                    static_cast<int>((std::max)(static_cast<int64_t>((std::numeric_limits<int>::min)()),
                        (std::min)(-static_cast<int64_t>(steps) * lines,
                            static_cast<int64_t>((std::numeric_limits<int>::max)())))));
            if (next != self->m_panelViewportScrollY) self->m_panelSelectedRow = -1;
            self->SetPanelViewportScrollY(next);
        }
        return 0;
    }
    case WM_KEYDOWN: {
        if (self->m_panelRows.empty()) return 0;
        const auto geometry = self->GetPanelViewportGeometry();
        using ScrollAction = ViewReservationUi::PanelViewportGeometry::ScrollAction;
        int current = self->m_panelSelectedRow;
        if (current < 0 || current >= geometry.RowCount() ||
            !geometry.RowRect(current, nullptr, nullptr))
            current = geometry.IndexAtOffset(geometry.ScrollY);
        int next = current;
        int nextScroll = geometry.ScrollY;
        switch (wParam) {
        case VK_UP: next = (std::max)(0, current - 1); nextScroll = geometry.Scroll(ScrollAction::Reveal, next); break;
        case VK_DOWN: next = (std::min)(geometry.RowCount() - 1, current + 1); nextScroll = geometry.Scroll(ScrollAction::Reveal, next); break;
        case VK_PRIOR: nextScroll = geometry.Scroll(ScrollAction::Page, -1); next = geometry.IndexAtOffset(nextScroll); break;
        case VK_NEXT: nextScroll = geometry.Scroll(ScrollAction::Page, 1); next = geometry.IndexAtOffset(nextScroll); break;
        case VK_HOME: next = 0; nextScroll = geometry.Scroll(ScrollAction::Top); break;
        case VK_END: next = geometry.RowCount() - 1; nextScroll = geometry.Scroll(ScrollAction::Bottom); break;
        case VK_RETURN:
            if (!self->m_panelRows[static_cast<size_t>(current)].IsFuture)
                self->ActivatePanelCurrentProgram(static_cast<size_t>(current));
            return 0;
        default: return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        self->m_panelSelectedRow = next;
        self->SetPanelViewportScrollY(nextScroll);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!self->m_panelMouseTracking) {
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd;
            if (TrackMouseEvent(&track)) self->m_panelMouseTracking = true;
        }
        const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT client{};
        GetClientRect(hwnd, &client);
        const int hover = PtInRect(&client, pt) ? self->GetPanelViewportGeometry().HitTest(pt.y) : -1;
        if (hover != self->m_panelHoverRow) {
            const int previous = self->m_panelHoverRow;
            self->m_panelHoverRow = hover;
            self->m_panelMarqueeActive = false;
            self->m_panelMarqueeEpochTick = GetTickMs();
            if (self->m_panelWindow != nullptr) {
                KillTimer(self->m_panelWindow, kPanelMarqueeTimerId);
                KillTimer(self->m_panelWindow, kPanelMarqueeDelayTimerId);
            }
            self->InvalidatePanelViewportRow(previous);
            self->InvalidatePanelViewportRow(hover);
            if (self->m_panelWindow != nullptr && hover >= 0 &&
                self->m_panelRows[static_cast<size_t>(hover)].NeedsMarquee) {
                SetTimer(self->m_panelWindow, kPanelMarqueeDelayTimerId, kPanelMarqueeHoverDelayMs, nullptr);
            }
        }
        return 0;
    }
    case WM_MOUSELEAVE: {
        const int previous = self->m_panelHoverRow;
        self->m_panelMouseTracking = false;
        self->m_panelHoverRow = -1;
        self->m_panelMarqueeActive = false;
        if (self->m_panelWindow != nullptr) {
            KillTimer(self->m_panelWindow, kPanelMarqueeTimerId);
            KillTimer(self->m_panelWindow, kPanelMarqueeDelayTimerId);
        }
        self->InvalidatePanelViewportRow(previous);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONUP: {
        const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT client{};
        GetClientRect(hwnd, &client);
        if (!PtInRect(&client, pt)) return 0;
        const int index = self->GetPanelViewportGeometry().HitTest(pt.y);
        if (index < 0) return 0;
        if (message == WM_LBUTTONDOWN) {
            SetFocus(hwnd);
            RECT itemRect{};
            if (!self->GetPanelViewportRowRect(index, &itemRect)) return 0;
            const auto &row = self->m_panelRows[static_cast<size_t>(index)];
            const int arrowLeft = static_cast<int>(itemRect.right) - 4 - kPanelArrowHitWidth;
            const auto geometry = self->GetPanelViewportGeometry();
            const int64_t fullRowTop = static_cast<int64_t>(geometry.RowStart(index)) - geometry.ScrollY;
            const bool arrowVisible = geometry.ContainsCenteredLine(fullRowTop,
                fullRowTop + geometry.RowHeight(index), ViewReservationUi::kPanelGlyphCanvasSide);
            if (!row.IsFuture && row.HasFuture && arrowVisible && pt.x >= arrowLeft) {
                self->TogglePanelChannelExpanded(static_cast<size_t>(index));
                return 0;
            }
            self->m_panelSelectedRow = index;
        } else if (message == WM_LBUTTONDBLCLK) {
            if (!self->m_panelRows[static_cast<size_t>(index)].IsFuture)
                self->ActivatePanelCurrentProgram(static_cast<size_t>(index));
        } else {
            self->m_panelSelectedRow = index;
            POINT screen = pt;
            ClientToScreen(hwnd, &screen);
            self->ShowPanelReservationMenu(static_cast<size_t>(index), screen);
        }
        return 0;
    }
    default: break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK CViewReservationPlugin::AutomationBarProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto *self = reinterpret_cast<CViewReservationPlugin*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto *cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<CViewReservationPlugin*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self != nullptr) self->m_panelAutomationBar = hwnd;
    }

    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    switch (message) {
    case WM_CREATE:
        // AutomationBar自身が描画とクリック判定を持つ。子コントロールは作らない。
        return 0;

    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        self->DrawAutomationBar(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONUP: {
        const int x = static_cast<int>(static_cast<short>(LOWORD(lParam)));
        const int y = static_cast<int>(static_cast<short>(HIWORD(lParam)));

        HDC dc = GetDC(hwnd);
        const auto layout = self->CalculateAutomationBarLayout(dc);
        if (dc != nullptr) {
            ReleaseDC(hwnd, dc);
        }

        if (layout.ZappingToggle.Contains(x, y)) {
            bool active = false;
            { std::lock_guard<std::mutex> lock(self->m_automationMutex); active = self->m_zappingActive; }
            if (active) self->StopZapping(); else self->StartZapping();
            return 0;
        }
        if (layout.ExitHours.Contains(x, y)) {
            bool active = false;
            { std::lock_guard<std::mutex> lock(self->m_automationMutex); active = self->m_exitTimerActive; }
            if (!active) self->ShowExitHoursMenu();
            return 0;
        }
        if (layout.ExitToggle.Contains(x, y)) {
            bool active = false;
            { std::lock_guard<std::mutex> lock(self->m_automationMutex); active = self->m_exitTimerActive; }
            if (active) self->StopExitTimer(); else self->StartExitTimer();
            return 0;
        }
        break;
    }

    case WM_DESTROY:
        if (self->m_panelAutomationBar == hwnd) self->m_panelAutomationBar = nullptr;
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK CViewReservationPlugin::PanelHeaderProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto *self = reinterpret_cast<CViewReservationPlugin*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto *cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<CViewReservationPlugin*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self == nullptr) return DefWindowProcW(hwnd, message, wParam, lParam);

    using Item = ViewReservationUi::PanelWaveBarLayout::Item;
    switch (message) {
    case WM_ERASEBKGND:
        return 1; // WM_PAINT supplies the complete background and all segments.
    case WM_SIZE:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT client{};
        GetClientRect(hwnd, &client);
        const int width = (std::max)(0, static_cast<int>(client.right - client.left));
        const int height = (std::max)(0, static_cast<int>(client.bottom - client.top));
        if (width > 0 && height > 0) {
            HDC buffer = CreateCompatibleDC(dc);
            HBITMAP bitmap = buffer != nullptr ? CreateCompatibleBitmap(dc, width, height) : nullptr;
            if (buffer != nullptr && bitmap != nullptr) {
                HGDIOBJ old = SelectObject(buffer, bitmap);
                self->DrawPanelHeader(buffer, client);
                BitBlt(dc, 0, 0, width, height, buffer, 0, 0, SRCCOPY);
                SelectObject(buffer, old);
            } else {
                self->DrawPanelHeader(dc, client);
            }
            if (bitmap != nullptr) DeleteObject(bitmap);
            if (buffer != nullptr) DeleteDC(buffer);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const auto item = self->GetPanelHeaderLayout().HitTest(GET_X_LPARAM(lParam));
        if (item == Item::None || GET_Y_LPARAM(lParam) < 0 || GET_Y_LPARAM(lParam) >= kPanelHeaderHeight) return 0;
        SetFocus(hwnd); // Explicit user click only; never on a passive refresh.
        self->m_panelHeaderFocused = item;
        self->m_panelHeaderPressed = item;
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (GetCapture() != hwnd) return 0;
        const auto pressed = self->m_panelHeaderPressed;
        const auto released = self->GetPanelHeaderLayout().HitTest(GET_X_LPARAM(lParam));
        ReleaseCapture();
        self->m_panelHeaderPressed = Item::None;
        InvalidateRect(hwnd, nullptr, FALSE);
        if (pressed != Item::None && pressed == released &&
            GET_Y_LPARAM(lParam) >= 0 && GET_Y_LPARAM(lParam) < kPanelHeaderHeight) {
            const int id = PanelHeaderCommand(pressed);
            if (id != 0) SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(hwnd));
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        self->m_panelHeaderPressed = Item::None;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN: {
        const auto layout = self->GetPanelHeaderLayout();
        if (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_HOME || wParam == VK_END) {
            const auto &items = ViewReservationUi::PanelWaveBarLayout::Items;
            int current = 0;
            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                if (items[static_cast<size_t>(i)] == self->m_panelHeaderFocused) { current = i; break; }
            }
            int next = current;
            if (wParam == VK_HOME) next = 0;
            if (wParam == VK_END) next = static_cast<int>(items.size()) - 1;
            const int direction = wParam == VK_LEFT ? -1 : 1;
            for (int steps = 0; steps < static_cast<int>(items.size()); ++steps) {
                if (wParam == VK_LEFT || wParam == VK_RIGHT)
                    next = (next + direction + static_cast<int>(items.size())) % static_cast<int>(items.size());
                if (layout.Get(items[static_cast<size_t>(next)]).Width > 0) {
                    self->m_panelHeaderFocused = items[static_cast<size_t>(next)];
                    InvalidateRect(hwnd, nullptr, FALSE);
                    break;
                }
                if (wParam == VK_HOME || wParam == VK_END) next += wParam == VK_HOME ? 1 : -1;
                if (next < 0 || next >= static_cast<int>(items.size())) break;
            }
            return 0;
        }
        if (wParam == VK_RETURN || wParam == VK_SPACE) {
            if (layout.Get(self->m_panelHeaderFocused).Width > 0) {
                const int id = PanelHeaderCommand(self->m_panelHeaderFocused);
                if (id != 0) SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(hwnd));
            }
            return 0;
        }
        break;
    }
    case WM_DESTROY:
        if (self->m_panelHeader == hwnd) self->m_panelHeader = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK CViewReservationPlugin::PanelWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto *self = reinterpret_cast<CViewReservationPlugin*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto *cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<CViewReservationPlugin*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    switch (message) {
    case WM_CREATE: {
        self->m_panelHeader = CreateWindowExW(
            0, kPanelHeaderClassName, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlHeaderBar)), g_hinstDLL, self);
        if (self->m_panelHeader == nullptr) return -1;

        self->m_panelAutomationBar = CreateWindowExW(
            0,
            kAutomationBarClassName,
            L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlAutomationBar)),
            g_hinstDLL,
            self);
        if (self->m_panelAutomationBar == nullptr) {
            return -1;
        }

        self->m_panelViewport = CreateWindowExW(0, kPanelViewportClassName, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_CLIPCHILDREN,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlViewport)),
            g_hinstDLL, self);
        if (self->m_panelViewport == nullptr) return -1;

        self->m_panelStatus = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlStatus)), g_hinstDLL, nullptr);

        self->m_panelMarqueeEpochTick = GetTickMs();
        self->UpdatePanelFont();
        self->UpdateAutomationControls();

        return 0;
    }


    case WM_CTLCOLORSTATIC: {
        HWND control = reinterpret_cast<HWND>(lParam);
        if (control == self->m_panelStatus) {
            HDC dc = reinterpret_cast<HDC>(wParam);
            const PanelHostPalette palette = ResolvePanelHostPalette(self->m_pApp);
            SetBkColor(dc, palette.Back);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, palette.Text);
            return reinterpret_cast<LRESULT>(self->EnsurePanelBackBrush(palette.Back));
        }
        break;
    }

    case WM_SIZE:
        self->LayoutPanel();
        return 0;

    case kPanelUiStateMessage:
        self->UpdateAutomationControls();
        self->UpdatePanelViewingProjection(wParam != 0);
        return 0;

    case WM_TIMER:
        if (wParam == kPanelRefreshTimerId) {
            self->RefreshPanelDisplay(false);
            return 0;
        }
        if (wParam == kPanelMarqueeDelayTimerId) {
            KillTimer(hwnd, kPanelMarqueeDelayTimerId);
            if (self->m_panelViewport != nullptr && self->m_panelHoverRow >= 0 &&
                static_cast<size_t>(self->m_panelHoverRow) < self->m_panelRows.size() &&
                self->m_panelRows[static_cast<size_t>(self->m_panelHoverRow)].NeedsMarquee) {
                self->m_panelMarqueeActive = true;
                self->m_panelMarqueeEpochTick = GetTickMs();
                RECT item{};
                if (self->GetPanelViewportRowRect(self->m_panelHoverRow, &item)) {
                    InvalidateRect(self->m_panelViewport, &item, FALSE);
                }
                SetTimer(hwnd, kPanelMarqueeTimerId, kPanelMarqueeIntervalMs, nullptr);
            }
            return 0;
        }
        if (wParam == kPanelMarqueeTimerId) {
            if (self->m_panelMarqueeActive && self->m_panelViewport != nullptr && self->m_panelHoverRow >= 0 &&
                static_cast<size_t>(self->m_panelHoverRow) < self->m_panelRows.size() &&
                self->m_panelRows[static_cast<size_t>(self->m_panelHoverRow)].NeedsMarquee) {
                RECT item{};
                if (self->GetPanelViewportRowRect(self->m_panelHoverRow, &item)) {
                    // ホバー中の1行について、通常描画と同じレイアウト計算からタイトル列だけを更新する。
                    HDC layoutDc = GetDC(self->m_panelViewport);
                    HFONT layoutFont = self->m_panelFont != nullptr ? self->m_panelFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                    HGDIOBJ oldLayoutFont = nullptr;
                    if (layoutDc != nullptr && layoutFont != nullptr) {
                        oldLayoutFont = SelectObject(layoutDc, layoutFont);
                    }
                    const PanelRowLayout layout = self->CalculatePanelRowLayout(item, layoutDc);
                    if (layoutDc != nullptr && oldLayoutFont != nullptr) {
                        SelectObject(layoutDc, oldLayoutFont);
                    }
                    if (layoutDc != nullptr) {
                        ReleaseDC(self->m_panelViewport, layoutDc);
                    }

                    RECT dirty = layout.Content;
                    dirty.left = layout.TitleLeft;
                    if (dirty.right > dirty.left) {
                        InvalidateRect(self->m_panelViewport, &dirty, FALSE);
                    }
                }
            } else {
                self->m_panelMarqueeActive = false;
                KillTimer(hwnd, kPanelMarqueeTimerId);
            }
            return 0;
        }
        break;

    case WM_ERASEBKGND: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillSolid(dc, rc, ResolvePanelHostPalette(self->m_pApp).Back);
        return 1;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int notify = HIWORD(wParam);
        if (id == kControlWaveGr && notify == BN_CLICKED) {
            self->SetPanelWave(PanelWave::Terrestrial);
            return 0;
        }
        if (id == kControlWaveBs && notify == BN_CLICKED) {
            self->SetPanelWave(PanelWave::Bs);
            return 0;
        }
        if (id == kControlWaveCs && notify == BN_CLICKED) {
            self->SetPanelWave(PanelWave::Cs);
            return 0;
        }
        if (id == kControlWaveCatv && notify == BN_CLICKED && self->m_panelCatvAvailable) {
            self->SetPanelWave(PanelWave::Catv);
            return 0;
        }
        if (id == kControlRefresh && notify == BN_CLICKED) {
            self->RefreshPanelDisplay(true);
            return 0;
        }
        if (id == kControlPower && notify == BN_CLICKED) {
            self->RequestTvTestClose();
            return 0;
        }
        break;
    }

    case WM_DESTROY:
        self->ReleasePanelWindowResources(hwnd);
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

std::vector<DriverChannelEntry> CViewReservationPlugin::GetDisplayChannelsForWave(PanelWave wave, bool forceDriverCache) {
    if (m_pApp == nullptr) return {};
    m_driverCache.RebuildIfNeeded(m_pApp, forceDriverCache);
    auto channels = m_driverCache.SnapshotDisplayServices();
    channels.erase(std::remove_if(channels.begin(), channels.end(), [wave](const DriverChannelEntry &entry) {
        return ResolvePanelWave(entry) != wave;
    }), channels.end());
    return channels;
}

void CViewReservationPlugin::PostPanelUiStateRefresh(bool centerCurrent) const {
    HWND hwnd = m_panelWindow;
    if (hwnd != nullptr) PostMessageW(hwnd, kPanelUiStateMessage, centerCurrent ? 1 : 0, 0);
}

void CViewReservationPlugin::UpdateAutomationControls() {
    if (m_panelAutomationBar == nullptr) return;
    InvalidateRect(m_panelAutomationBar, nullptr, FALSE);
}

void CViewReservationPlugin::ShowExitHoursMenu() {
    if (m_panelWindow == nullptr || m_panelAutomationBar == nullptr) return;

    int current = ViewReservationSettings::DefaultExitTimerHours;
    bool active = false;
    {
        std::lock_guard<std::mutex> lock(m_automationMutex);
        current = m_exitTimerHours;
        active = m_exitTimerActive;
    }
    HDC dc = GetDC(m_panelAutomationBar);
    const auto layout = CalculateAutomationBarLayout(dc);
    if (dc != nullptr) {
        ReleaseDC(m_panelAutomationBar, dc);
    }
    if (active || layout.ExitHours.Empty()) return;

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    for (int value = ViewReservationSettings::ExitTimerMinHours; value <= ViewReservationSettings::ExitTimerMaxHours; ++value) {
        const std::wstring label = std::to_wstring(value) + L"h";
        UINT flags = MF_STRING;
        if (value == current) flags |= MF_CHECKED;
        AppendMenuW(menu, flags, static_cast<UINT_PTR>(value), label.c_str());
    }

    POINT anchor{
        static_cast<LONG>(layout.ExitHours.Left),
        static_cast<LONG>(layout.ExitHours.Bottom)
    };
    ClientToScreen(m_panelAutomationBar, &anchor);
    const int command = TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
        anchor.x, anchor.y, 0, m_panelWindow, nullptr);
    DestroyMenu(menu);

    const int value = command;
    if (ViewReservationSettings::IsSupportedExitTimerHours(value)) {
        {
            std::lock_guard<std::mutex> lock(m_automationMutex);
            m_exitTimerHours = value;
        }
        SaveExitTimerHours(value);
        UpdateAutomationControls();
    }
}

void CViewReservationPlugin::StartZapping() {
    const PanelWave wave = m_panelWave;
    auto channels = GetDisplayChannelsForWave(wave, true);
    if (channels.size() < 2) {
        Log(L"ザッピング対象の局が2局以上ありません。");
        PostPanelUiStateRefresh();
        return;
    }

    TVTest::ChannelInfo current{};
    current.Size = sizeof(current);
    ULONGLONG currentKey = 0;
    if (m_pApp != nullptr && m_pApp->GetCurrentChannelInfo(&current)) {
        currentKey = (static_cast<ULONGLONG>(current.NetworkID) << 32) |
            (static_cast<ULONGLONG>(current.TransportStreamID) << 16) |
            static_cast<ULONGLONG>(current.ServiceID);
    }

    const ULONGLONG now = GetTickMs();
    {
        std::lock_guard<std::mutex> lock(m_automationMutex);
        ++m_zappingGeneration;
        m_zappingActive = true;
        m_zappingHasSwitched = false;
        m_zappingWave = wave;
        m_zappingLastChannelKey = currentKey;
        m_zappingNextTick = now + static_cast<ULONGLONG>(m_zappingIntervalSeconds) * 1000ULL;
    }
    PostPanelUiStateRefresh(true);
    m_cv.notify_all();
}

void CViewReservationPlugin::StopZapping() {
    {
        std::lock_guard<std::mutex> lock(m_automationMutex);
        ++m_zappingGeneration;
        m_zappingActive = false;
        m_zappingHasSwitched = false;
        m_zappingNextTick = 0;
        m_zappingLastChannelKey = 0;
    }
    PostPanelUiStateRefresh();
    m_cv.notify_all();
}

void CViewReservationPlugin::StartExitTimer() {
    const ULONGLONG now = GetTickMs();
    {
        std::lock_guard<std::mutex> lock(m_automationMutex);
        ++m_exitTimerGeneration;
        m_exitTimerActive = true;
        m_exitWaitingForRecording = false;
        m_exitDeadlineTick = now + static_cast<ULONGLONG>(m_exitTimerHours) * 60ULL * 60ULL * 1000ULL;
    }
    PostPanelUiStateRefresh();
    m_cv.notify_all();
}

void CViewReservationPlugin::StopExitTimer() {
    {
        std::lock_guard<std::mutex> lock(m_automationMutex);
        ++m_exitTimerGeneration;
        m_exitTimerActive = false;
        m_exitWaitingForRecording = false;
        m_exitDeadlineTick = 0;
    }
    PostPanelUiStateRefresh();
    m_cv.notify_all();
}

void CViewReservationPlugin::RunZappingTick(ULONGLONG nowTick) {
    PanelWave wave = PanelWave::Terrestrial;
    int intervalSeconds = ViewReservationSettings::DefaultZappingIntervalSeconds;
    bool hasSwitched = false;
    ULONGLONG generation = 0;
    {
        std::lock_guard<std::mutex> lock(m_automationMutex);
        if (!m_zappingActive || nowTick < m_zappingNextTick) return;
        wave = m_zappingWave;
        intervalSeconds = m_zappingIntervalSeconds;
        hasSwitched = m_zappingHasSwitched;
        generation = m_zappingGeneration;
    }

    auto postpone = [&]() {
        std::lock_guard<std::mutex> lock(m_automationMutex);
        if (m_zappingActive && m_zappingGeneration == generation) {
            m_zappingNextTick = GetTickMs() + static_cast<ULONGLONG>(intervalSeconds) * 1000ULL;
        }
    };

    if (IsRecordingActive() || HasPendingRecording()) {
        postpone();
        return;
    }
    if (m_manager != nullptr) {
        const auto reservations = m_manager->Snapshot();
        for (const auto &item : reservations) {
            if (!item.Triggered && IsReservationTriggerWindow(item)) {
                postpone();
                return;
            }
        }
    }

    std::lock_guard<std::mutex> operationLock(m_zappingOperationMutex);
    {
        std::lock_guard<std::mutex> lock(m_automationMutex);
        if (!m_zappingActive || m_zappingGeneration != generation) return;
    }

    auto channels = GetDisplayChannelsForWave(wave, false);
    if (channels.size() < 2) {
        StopZapping();
        return;
    }

    TVTest::ChannelInfo current{};
    current.Size = sizeof(current);
    ULONGLONG currentKey = 0;
    bool haveCurrent = m_pApp != nullptr && m_pApp->GetCurrentChannelInfo(&current);
    if (haveCurrent) {
        currentKey = (static_cast<ULONGLONG>(current.NetworkID) << 32) |
            (static_cast<ULONGLONG>(current.TransportStreamID) << 16) |
            static_cast<ULONGLONG>(current.ServiceID);
        DriverChannelEntry currentEntry{};
        currentEntry.NetworkID = current.NetworkID;
        currentEntry.TransportStreamID = current.TransportStreamID;
        currentEntry.ServiceID = current.ServiceID;
        const PanelWave currentWave = ResolvePanelWave(currentEntry);
        if (hasSwitched && current.NetworkID != 0 && currentWave != wave) {
            StopZapping();
            return;
        }
    }

    ULONGLONG lastKey = 0;
    {
        std::lock_guard<std::mutex> lock(m_automationMutex);
        if (!m_zappingActive || m_zappingGeneration != generation) return;
        lastKey = m_zappingLastChannelKey;
    }

    int lastIndex = -1;
    if (currentKey != 0) {
        for (size_t i = 0; i < channels.size(); ++i) {
            if (MakeChannelServiceKey(channels[i]) == currentKey) { lastIndex = static_cast<int>(i); break; }
        }
    }
    if (lastIndex < 0 && lastKey != 0) {
        for (size_t i = 0; i < channels.size(); ++i) {
            if (MakeChannelServiceKey(channels[i]) == lastKey) { lastIndex = static_cast<int>(i); break; }
        }
    }

    const size_t nextIndex = static_cast<size_t>((lastIndex + 1) % static_cast<int>(channels.size()));
    const DriverChannelEntry &target = channels[nextIndex];
    ReservationItem item{};
    item.NetworkID = target.NetworkID;
    item.TransportStreamID = target.TransportStreamID;
    item.ServiceID = target.ServiceID;
    item.OriginalNetworkID = target.NetworkID;
    item.OriginalTransportStreamID = target.TransportStreamID;
    item.OriginalServiceID = target.ServiceID;
    item.EventName = target.ChannelName;
    item.LastDriverName = target.DriverName;

    {
        std::lock_guard<std::mutex> lock(m_automationMutex);
        if (!m_zappingActive || m_zappingGeneration != generation) return;
    }
    if (!TrySwitchToItem(item, L"ザッピング")) {
        StopZapping();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_automationMutex);
        if (!m_zappingActive || m_zappingGeneration != generation) return;
        m_zappingHasSwitched = true;
        m_zappingLastChannelKey = MakeChannelServiceKey(target);
        m_zappingNextTick = GetTickMs() + static_cast<ULONGLONG>(intervalSeconds) * 1000ULL;
    }
    PostPanelUiStateRefresh(true);
}

void CViewReservationPlugin::RunExitTimerTick(ULONGLONG nowTick) {
    ULONGLONG generation = 0;
    {
        std::lock_guard<std::mutex> lock(m_automationMutex);
        if (!m_exitTimerActive || nowTick < m_exitDeadlineTick) return;
        generation = m_exitTimerGeneration;
    }

    if (IsRecordingActive() || HasPendingRecording()) {
        std::lock_guard<std::mutex> lock(m_automationMutex);
        if (m_exitTimerActive && m_exitTimerGeneration == generation) {
            m_exitWaitingForRecording = true;
        }
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_automationMutex);
        if (!m_exitTimerActive || m_exitTimerGeneration != generation) return;
        m_exitTimerActive = false;
        m_exitWaitingForRecording = false;
        m_exitDeadlineTick = 0;
    }
    RequestTvTestClose();
}

void CViewReservationPlugin::RequestTvTestClose() {
    StopZapping();
    PostPanelUiStateRefresh();
    std::lock_guard<std::mutex> operationLock(m_zappingOperationMutex);
    HWND appWindow = m_pApp != nullptr ? m_pApp->GetAppWindow() : nullptr;
    if (appWindow != nullptr) PostMessageW(appWindow, WM_CLOSE, 0, 0);
}

void CViewReservationPlugin::RunAutomationTick() {
    const ULONGLONG now = GetTickMs();
    RunExitTimerTick(now);
    RunZappingTick(now);
}

void CViewReservationPlugin::Log(const std::wstring &text) {
    if (m_pApp != nullptr) {
        m_pApp->AddLog(text.c_str());
    }
}

void CViewReservationPlugin::ShowReservationError(const std::wstring &message) const {
    if (message.empty()) {
        return;
    }
    HWND owner = m_pApp != nullptr ? m_pApp->GetAppWindow() : nullptr;
    MessageBoxW(owner, message.c_str(), L"ViewReservation", MB_OK | MB_ICONWARNING);
}

std::wstring CViewReservationPlugin::QueryEventName(const TVTest::ProgramGuideProgramInfo &program) const {
    TVTest::EpgEventQueryInfo query{};
    query.NetworkID = program.NetworkID;
    query.TransportStreamID = program.TransportStreamID;
    query.ServiceID = program.ServiceID;
    query.Type = TVTest::EPG_EVENT_QUERY_EVENTID;
    query.Flags = TVTest::EPG_EVENT_QUERY_FLAG_NONE;
    query.EventID = program.EventID;

    TVTest::EpgEventInfo *eventInfo = m_pApp->GetEpgEventInfo(&query);
    if (eventInfo == nullptr) {
        return L"";
    }

    const std::wstring name = CopyOptionalText(eventInfo->pszEventName);
    m_pApp->FreeEpgEventInfo(eventInfo);
    return name;
}

ReservationItem CViewReservationPlugin::MakeReservationItem(const TVTest::ProgramGuideProgramInfo &program) const {
    ReservationItem item{};
    item.NetworkID = program.NetworkID;
    item.TransportStreamID = program.TransportStreamID;
    item.ServiceID = program.ServiceID;
    item.EventID = program.EventID;
    item.OriginalNetworkID = program.NetworkID;
    item.OriginalTransportStreamID = program.TransportStreamID;
    item.OriginalServiceID = program.ServiceID;
    item.OriginalEventID = program.EventID;
    item.StartTime = program.StartTime;
    item.Duration = program.Duration;
    item.EventName = QueryEventName(program);

    if (item.EventName.empty()) {
        item.EventName = L"(番組名未取得)";
    }

    return item;
}

int CViewReservationPlugin::OnProgramGuideProgramInitializeMenu(
    const TVTest::ProgramGuideProgramInfo *pProgramInfo,
    const TVTest::ProgramGuideProgramInitializeMenuInfo *pInfo) {
    if (pProgramInfo == nullptr || pInfo == nullptr || m_manager == nullptr) {
        return 0;
    }

    const auto reservations = m_manager->Snapshot();
    const ReservationItem *existingReservation = FindMatchingReservation(
        reservations, pProgramInfo->NetworkID, pProgramInfo->TransportStreamID,
        pProgramInfo->ServiceID, pProgramInfo->EventID);
    const bool isReserved = existingReservation != nullptr;
    const bool isRecord = isReserved && existingReservation->RecordOnTrigger;

    int count = 0;

    if (!isReserved) {
        // 未予約：両方表示
        AppendMenuW(pInfo->hmenu, MF_STRING,
            pInfo->Command + kMenuCommandReserve, L"視聴予約");
        AppendMenuW(pInfo->hmenu, MF_STRING,
            pInfo->Command + kMenuCommandRecord, L"録画予約");
        count = 2;
    } else if (!isRecord) {
        // 予約済みメニューはCommand+0の1項目で解除する。
        AppendMenuW(pInfo->hmenu, MF_STRING | MF_CHECKED,
            pInfo->Command + kMenuCommandReserve, L"視聴予約解除");
        count = 1;
    } else {
        // 予約済みメニューはCommand+0の1項目で解除する。
        AppendMenuW(pInfo->hmenu, MF_STRING | MF_CHECKED,
            pInfo->Command + kMenuCommandReserve, L"録画予約解除");
        count = 1;
    }

    return count;
}

bool CViewReservationPlugin::OnProgramGuideProgramMenuSelected(
    const TVTest::ProgramGuideProgramInfo *pProgramInfo,
    UINT Command) {
    if (pProgramInfo == nullptr || m_manager == nullptr) {
        return false;
    }

    if (Command != kMenuCommandReserve && Command != kMenuCommandRecord) {
        return false;
    }

    const auto reservations = m_manager->Snapshot();
    const ReservationItem *existingReservation = FindMatchingReservation(
        reservations, pProgramInfo->NetworkID, pProgramInfo->TransportStreamID,
        pProgramInfo->ServiceID, pProgramInfo->EventID);
    const bool isReservedCmd = existingReservation != nullptr;
    const bool isRecordCmd = isReservedCmd && existingReservation->RecordOnTrigger;

    ReservationItem item = MakeReservationItem(*pProgramInfo);

    if (isReservedCmd) {
        item.RecordOnTrigger = isRecordCmd;
    } else {
        if (Command == kMenuCommandRecord) {
            item.RecordOnTrigger = true;
        }
    }

    std::wstring message;

    bool foundInDb = false;
    if (existingReservation != nullptr && existingReservation->RecordOnTrigger == item.RecordOnTrigger) {
        item.NetworkID = existingReservation->NetworkID;
        item.TransportStreamID = existingReservation->TransportStreamID;
        item.ServiceID = existingReservation->ServiceID;
        item.EventID = existingReservation->EventID;
        foundInDb = true;
    }

    bool reservationChanged = false;
    if (foundInDb) {
        reservationChanged = m_manager->RemoveReservation(
            item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID, &message);
    } else {
        reservationChanged = m_manager->AddOrUpdateReservation(item, &message);
    }

    if (!message.empty()) {
        Log(message);
    }
    if (!reservationChanged && !foundInDb) {
        ShowReservationError(message);
        return true;
    }

    if (m_manager->IsReserved(item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID)) {
        const ULONGLONG secUntil = SecondsUntil(item.StartTime);
        if (secUntil == 0) {
            const std::wstring triggerKey = MakeTriggerKey(item);
            if (ClaimTriggerKey(triggerKey)) {
                const ReservationTriggerResult result = TrySwitchReservation(item, triggerKey);
                if (result == ReservationTriggerResult::Completed) {
                    m_manager->MarkTriggered(item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID);
                } else if (result == ReservationTriggerResult::Failed ||
                           result == ReservationTriggerResult::WaitingForRecording) {
                    ReleaseTriggerKey(triggerKey);
                }
            }
        }
    }

    RefreshPanelRows(false);
    m_cv.notify_all();
    return true;
}

void CViewReservationPlugin::WorkerLoop() {
    std::unique_lock<std::mutex> lock(m_cvMutex);

    while (!m_stop.load()) {
        lock.unlock();
        RunMaintenanceTick();
        const ULONGLONG waitMs = ComputeNextWakeDelayMs();
        lock.lock();

        m_cv.wait_for(lock, std::chrono::milliseconds(waitMs));
    }
}

ULONGLONG CViewReservationPlugin::ComputeNextWakeDelayMs() const {
    const ULONGLONG nowTick = GetTickMs();
    ULONGLONG best = kIdleSleepMs;

    if (HasPendingRecording()) {
        best = kMinSleepMs;
    }

    {
        std::lock_guard<std::mutex> lock(m_automationMutex);
        if (m_zappingActive) {
            const ULONGLONG due = m_zappingNextTick <= nowTick ? 0 : m_zappingNextTick - nowTick;
            best = (std::min)(best, due);
        }
        if (m_exitTimerActive) {
            const ULONGLONG due = m_exitDeadlineTick <= nowTick ? 0 : m_exitDeadlineTick - nowTick;
            best = (std::min)(best, m_exitWaitingForRecording ? kMinSleepMs : due);
        }
    }

    if (m_manager != nullptr) {
        const auto list = m_manager->Snapshot();
        for (const auto &item : list) {
            const ULONGLONG secUntil = SecondsUntil(item.StartTime);
            if (!item.Triggered) {
                if (secUntil <= kBeforeSwitchSec) {
                    best = (std::min)(best, kMinSleepMs);
                } else {
                    best = (std::min)(best, (secUntil - kBeforeSwitchSec) * 1000ULL);
                }
            }

            const ULONGLONG syncIntervalMs = ViewReservationSchedule::EpgSyncIntervalMs(secUntil);
            const ULONGLONG nextSyncMs = item.LastEpgSyncTick == 0 ? 0 :
                (item.LastEpgSyncTick + syncIntervalMs <= nowTick ? 0 : item.LastEpgSyncTick + syncIntervalMs - nowTick);
            best = (std::min)(best, nextSyncMs);
        }
    }

    if (best < kMinSleepMs) best = kMinSleepMs;
    if (best > kMaxSleepMs) best = kMaxSleepMs;
    return best;
}

bool CViewReservationPlugin::IsStartupEpgWaitActive() const {
    if (m_startupTick == 0) {
        return false;
    }
    const ULONGLONG elapsed = GetTickMs() - m_startupTick;
    return elapsed < 180ULL * 1000ULL;
}

bool CViewReservationPlugin::IsSameLocalDate(const SYSTEMTIME &a, const SYSTEMTIME &b) const {
    return a.wYear == b.wYear && a.wMonth == b.wMonth && a.wDay == b.wDay;
}


bool CViewReservationPlugin::IsReservationActiveNow(const ReservationItem &item) const {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const ULONGLONG nowFt = ViewReservationTime::FileTimeValue(now);
    const ULONGLONG startFt = ViewReservationTime::FileTimeValue(item.StartTime);
    const ULONGLONG endFt = startFt + static_cast<ULONGLONG>(item.Duration) * ViewReservationTime::TicksPerSecond;
    return nowFt >= startFt && nowFt < endFt;
}

bool CViewReservationPlugin::IsReservationTriggerWindow(const ReservationItem &item) const {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const ULONGLONG nowFt = ViewReservationTime::FileTimeValue(now);
    const ULONGLONG startFt = ViewReservationTime::FileTimeValue(item.StartTime);
    const ULONGLONG triggerFt = startFt - kBeforeSwitchSec * ViewReservationTime::TicksPerSecond;
    const ULONGLONG endFt = startFt + static_cast<ULONGLONG>(item.Duration) * ViewReservationTime::TicksPerSecond;
    return nowFt >= triggerFt && nowFt < endFt;
}

int CViewReservationPlugin::SelectSingleReservationIndexToTrigger(const std::vector<ReservationItem> &items) const {
    int bestActive = -1;
    int bestUpcoming = -1;

    for (size_t i = 0; i < items.size(); ++i) {
        const auto &item = items[i];
        if (!IsReservationTriggerWindow(item))
            continue;

        if (IsReservationActiveNow(item)) {
            if (bestActive < 0) {
                bestActive = static_cast<int>(i);
            } else {
                const ULONGLONG currentStart = ViewReservationTime::FileTimeValue(item.StartTime);
                const ULONGLONG bestStart = ViewReservationTime::FileTimeValue(items[bestActive].StartTime);
                if (currentStart > bestStart || (currentStart == bestStart && static_cast<int>(i) > bestActive)) {
                    bestActive = static_cast<int>(i);
                }
            }
        } else {
            if (bestUpcoming < 0) {
                bestUpcoming = static_cast<int>(i);
            } else {
                const ULONGLONG currentStart = ViewReservationTime::FileTimeValue(item.StartTime);
                const ULONGLONG bestStart = ViewReservationTime::FileTimeValue(items[bestUpcoming].StartTime);
                if (currentStart < bestStart || (currentStart == bestStart && static_cast<int>(i) > bestUpcoming)) {
                    bestUpcoming = static_cast<int>(i);
                }
            }
        }
    }

    // Upcoming（次の番組）をActiveNow（現在放送中）より優先する。
    // 録画中は既存録画を停止せず、後続の録画要求は開始しない。
    return bestUpcoming >= 0 ? bestUpcoming : bestActive;
}


std::wstring CViewReservationPlugin::MakeTriggerKey(const ReservationItem &item) const {
    return std::to_wstring(item.OriginalNetworkID ? item.OriginalNetworkID : item.NetworkID) + L":" +
           std::to_wstring(item.OriginalTransportStreamID ? item.OriginalTransportStreamID : item.TransportStreamID) + L":" +
           std::to_wstring(item.OriginalServiceID ? item.OriginalServiceID : item.ServiceID) + L":" +
           std::to_wstring(item.OriginalEventID ? item.OriginalEventID : item.EventID) + L":" +
           std::to_wstring(item.StartTime.wYear) + L"/" +
           std::to_wstring(item.StartTime.wMonth) + L"/" +
           std::to_wstring(item.StartTime.wDay) + L" " +
           std::to_wstring(item.StartTime.wHour) + L":" +
           std::to_wstring(item.StartTime.wMinute);
}

bool CViewReservationPlugin::ClaimTriggerKey(const std::wstring &key) {
    std::lock_guard<std::mutex> lock(m_sessionTriggeredMutex);
    return m_sessionTriggeredKeys.insert(key).second;
}

void CViewReservationPlugin::ReleaseTriggerKey(const std::wstring &key) {
    std::lock_guard<std::mutex> lock(m_sessionTriggeredMutex);
    m_sessionTriggeredKeys.erase(key);
}


void CViewReservationPlugin::RunMaintenanceTick() {
    if (m_pApp == nullptr) return;

    try {
        RunAutomationTick();
        if (m_manager == nullptr) return;
        // チャンネル切替後は、目的サービスへ到達したことを確認してから録画する。
        TryCompletePendingRecording();

        m_manager->RemoveExpired();

        if (IsStartupEpgWaitActive()) {
            if (!m_epgWaitLogged) {
                m_epgWaitLogged = true;
            }
        } else {
            if (m_epgWaitLogged) {
                m_epgWaitLogged = false;
            }
            ReconcileByEpg();
        }

        const auto list = m_manager->Snapshot();
        const int index = SelectSingleReservationIndexToTrigger(list);
        if (index < 0)
            return;

        ReservationItem item = list[static_cast<size_t>(index)];
        if (item.TransportStreamID == 0 && item.ServiceID == 0)
            return;

        const std::wstring triggerKey = MakeTriggerKey(item);

        // INIに保存されたTriggeredフラグも確認（再起動後の再実行防止）
        if (item.Triggered)
            return;

        if (!ClaimTriggerKey(triggerKey))
            return;

        const ReservationTriggerResult result = TrySwitchReservation(item, triggerKey);
        if (result == ReservationTriggerResult::Completed) {
            m_manager->MarkTriggered(item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID);
        } else if (result == ReservationTriggerResult::Failed ||
                   result == ReservationTriggerResult::WaitingForRecording) {
            ReleaseTriggerKey(triggerKey);
        }
    } catch (...) {
        Log(L"[視聴予約] 予約処理中にエラーが発生しました");
    }
}

ULONGLONG CViewReservationPlugin::GetTickMs() {
    return GetTickCount64();
}

ULONGLONG CViewReservationPlugin::SecondsUntil(const SYSTEMTIME &st) {
    SYSTEMTIME now{};
    GetLocalTime(&now);

    const ULONGLONG a = ViewReservationTime::FileTimeValue(now);
    const ULONGLONG b = ViewReservationTime::FileTimeValue(st);

    if (b <= a) {
        return 0;
    }

    return (b - a) / ViewReservationTime::TicksPerSecond;
}

std::wstring CViewReservationPlugin::ToLowerSimple(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return text;
}

int CViewReservationPlugin::ComputeNameSimilarityScore(const std::wstring &a, const std::wstring &b) {
    const std::wstring la = ToLowerSimple(a);
    const std::wstring lb = ToLowerSimple(b);

    if (la.empty() || lb.empty()) {
        return 0;
    }
    if (la == lb) {
        return 100;
    }
    if (la.find(lb) != std::wstring::npos || lb.find(la) != std::wstring::npos) {
        return 80;
    }

    int common = 0;
    for (wchar_t ch : la) {
        if (lb.find(ch) != std::wstring::npos) {
            ++common;
        }
    }

    const int maxLen = static_cast<int>((std::max)(la.size(), lb.size()));
    if (maxLen <= 0) {
        return 0;
    }

    return (common * 60) / maxLen;
}

int CViewReservationPlugin::ComputeCandidateScore(const ReservationItem &base, const ReservationItem &candidate) {
    int score = ComputeNameSimilarityScore(base.EventName, candidate.EventName);

    const ULONGLONG baseTime = ViewReservationTime::FileTimeValue(base.StartTime);
    const ULONGLONG candTime = ViewReservationTime::FileTimeValue(candidate.StartTime);
    const ULONGLONG diffSec = (baseTime > candTime ? (baseTime - candTime) : (candTime - baseTime)) / ViewReservationTime::TicksPerSecond;

    if (diffSec <= 60) {
        score += 60;
    } else if (diffSec <= 5 * 60) {
        score += 40;
    } else if (diffSec <= 30 * 60) {
        score += 20;
    } else if (diffSec <= 2 * 60 * 60) {
        score += 10;
    }

    const DWORD baseDur = base.Duration;
    const DWORD candDur = candidate.Duration;
    const DWORD durDiff = baseDur > candDur ? (baseDur - candDur) : (candDur - baseDur);

    if (durDiff == 0) {
        score += 30;
    } else if (durDiff <= 60) {
        score += 20;
    } else if (durDiff <= 10 * 60) {
        score += 10;
    }

    if (base.NetworkID == candidate.NetworkID) {
        score += 15;
    }
    if (base.TransportStreamID == candidate.TransportStreamID) {
        score += 10;
    }
    if (base.ServiceID == candidate.ServiceID) {
        score += 10;
    }

    return score;
}

bool CViewReservationPlugin::IsSyncDue(const ReservationItem &item) {
    const ULONGLONG now = GetTickMs();
    if (item.LastEpgSyncTick == 0) {
        return true;
    }

    const ULONGLONG intervalMs = ViewReservationSchedule::EpgSyncIntervalMs(SecondsUntil(item.StartTime));
    return (now - item.LastEpgSyncTick) >= intervalMs;
}

bool CViewReservationPlugin::IsRescueDue(const ReservationItem &item) {
    const ULONGLONG now = GetTickMs();
    if (item.LastRescueTick == 0) {
        return true;
    }

    ULONGLONG intervalMs = 30 * 1000ULL;
    const ULONGLONG secUntil = SecondsUntil(item.StartTime);

    if (secUntil > 6 * 60 * 60) {
        intervalMs = 10 * 60 * 1000ULL;
    } else if (secUntil > 60 * 60) {
        intervalMs = 5 * 60 * 1000ULL;
    } else if (secUntil > 10 * 60) {
        intervalMs = 60 * 1000ULL;
    }

    return (now - item.LastRescueTick) >= intervalMs;
}

bool CViewReservationPlugin::TryDirectEpgSync(ReservationItem &item, std::wstring *message) {
    TVTest::EpgEventQueryInfo query{};
    query.NetworkID = item.NetworkID;
    query.TransportStreamID = item.TransportStreamID;
    query.ServiceID = item.ServiceID;
    query.Type = TVTest::EPG_EVENT_QUERY_EVENTID;
    query.Flags = TVTest::EPG_EVENT_QUERY_FLAG_NONE;
    query.EventID = item.EventID;

    TVTest::EpgEventInfo *eventInfo = m_pApp->GetEpgEventInfo(&query);
    item.LastEpgSyncTick = GetTickMs();
    m_manager->TouchEpgSync(item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID, item.LastEpgSyncTick);

    if (eventInfo == nullptr) {
        return false;
    }

    const std::wstring name = CopyOptionalText(eventInfo->pszEventName);
    const bool changed = m_manager->UpdateTimingByIds(
        item.NetworkID,
        item.TransportStreamID,
        item.ServiceID,
        item.EventID,
        eventInfo->StartTime,
        eventInfo->Duration,
        &name,
        message);

    item.StartTime = eventInfo->StartTime;
    item.Duration = eventInfo->Duration;
    if (!name.empty()) {
        item.EventName = name;
    }
    item.RescueState = 0;

    m_pApp->FreeEpgEventInfo(eventInfo);
    return changed;
}

bool CViewReservationPlugin::TryRescueInService(WORD nid, WORD tsid, WORD sid, const ReservationItem &item, ReservationItem *replacement, int *scoreOut) {
    TVTest::EpgEventList list{};
    list.NetworkID = nid;
    list.TransportStreamID = tsid;
    list.ServiceID = sid;

    if (!m_pApp->GetEpgEventList(&list)) {
        return false;
    }

    bool found = false;
    ReservationItem best{};
    int bestScore = -1;

    for (WORD i = 0; i < list.NumEvents; ++i) {
        const TVTest::EpgEventInfo *ev = list.EventList[i];
        if (ev == nullptr) {
            continue;
        }

        ReservationItem candidate{};
        candidate.NetworkID = nid;
        candidate.TransportStreamID = tsid;
        candidate.ServiceID = sid;
        candidate.EventID = ev->EventID;
        candidate.OriginalNetworkID = item.OriginalNetworkID;
        candidate.OriginalTransportStreamID = item.OriginalTransportStreamID;
        candidate.OriginalServiceID = item.OriginalServiceID;
        candidate.OriginalEventID = item.OriginalEventID;
        candidate.StartTime = ev->StartTime;
        candidate.Duration = ev->Duration;
        candidate.EventName = CopyOptionalText(ev->pszEventName);
        candidate.LastDriverName = item.LastDriverName;

        const int score = ComputeCandidateScore(item, candidate);
        if (score > bestScore) {
            bestScore = score;
            best = candidate;
            found = true;
        }
    }

    m_pApp->FreeEpgEventList(&list);

    if (!found || bestScore < 120) {
        return false;
    }

    if (replacement != nullptr) {
        *replacement = best;
    }
    if (scoreOut != nullptr) {
        *scoreOut = bestScore;
    }
    return true;
}

bool CViewReservationPlugin::TryRescueByEventList(const ReservationItem &item, ReservationItem *replacement) {
    ReservationItem best{};
    int bestScore = -1;

    int score = -1;
    if (TryRescueInService(item.NetworkID, item.TransportStreamID, item.ServiceID, item, &best, &score)) {
        bestScore = score;
    }

    m_driverCache.RebuildIfNeeded(m_pApp, false);

    std::vector<DriverChannelEntry> candidates = m_driverCache.FindByNetwork(item.NetworkID);
    if (candidates.empty()) {
        candidates = m_driverCache.Snapshot();
    }

    for (const auto &entry : candidates) {
        if (entry.NetworkID == item.NetworkID &&
            entry.TransportStreamID == item.TransportStreamID &&
            entry.ServiceID == item.ServiceID) {
            continue;
        }

        ReservationItem candidate{};
        int candidateScore = -1;
        if (TryRescueInService(entry.NetworkID, entry.TransportStreamID, entry.ServiceID, item, &candidate, &candidateScore)) {
            candidate.LastDriverName = entry.DriverName;
            if (candidateScore > bestScore) {
                best = candidate;
                bestScore = candidateScore;
            }
        }
    }

    if (bestScore < 120) {
        return false;
    }

    if (replacement != nullptr) {
        *replacement = best;
    }
    return true;
}

void CViewReservationPlugin::ReconcileByEpg() {
    if (m_manager == nullptr) {
        return;
    }

    auto list = m_manager->Snapshot();
    for (auto &item : list) {
        if (!IsSyncDue(item)) {
            continue;
        }

        std::wstring updateMessage;
        if (TryDirectEpgSync(item, &updateMessage)) {
            if (!updateMessage.empty()) {
                Log(updateMessage);
            }
            continue;
        }

        if (!IsRescueDue(item)) {
            continue;
        }

        item.LastRescueTick = GetTickMs();
        m_manager->TouchRescue(item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID, item.LastRescueTick);

        ReservationItem replacement{};
        if (TryRescueByEventList(item, &replacement)) {
            std::wstring replaceMessage;
            if (m_manager->ReplaceIdentity(
                    item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID,
                    replacement, &replaceMessage)) {
                Log(replaceMessage);
            }
            continue;
        }

        const ULONGLONG secUntil = SecondsUntil(item.StartTime);
        SYSTEMTIME now{};
        GetLocalTime(&now);
        const bool isToday = IsSameLocalDate(now, item.StartTime);
        const bool protectNear = secUntil <= 30 * 60;

        if (IsStartupEpgWaitActive() || isToday || protectNear) {
            continue;
        }

        if (secUntil <= 10 * 60) {
            if (m_manager->RemoveReservation(item.NetworkID, item.TransportStreamID, item.ServiceID, item.EventID)) {
                Log(L"[視聴予約] 番組表で番組を確認できなかったため予約を解除しました: " + item.EventName);
            }
        } else {
        }
    }
}

bool CViewReservationPlugin::IsCurrentDriverPreferred(const ReservationItem &item) const {
    if (item.LastDriverName.empty() || m_pApp == nullptr) {
        return true;
    }

    wchar_t currentDriver[MAX_PATH]{};
    if (m_pApp->GetDriverName(currentDriver, MAX_PATH) <= 0) {
        return true;
    }
    return _wcsicmp(currentDriver, item.LastDriverName.c_str()) == 0;
}

bool CViewReservationPlugin::TrySelectOnCurrentDriver(const ReservationItem &item) {
    if (!IsCurrentDriverPreferred(item)) {
        return false;
    }

    TVTest::ChannelSelectInfo info{};
    info.Size = sizeof(info);
    info.Flags = static_cast<TVTest::ChannelSelectFlag>(
        TVTest::CHANNEL_SELECT_FLAG_STRICTSERVICE |
        TVTest::CHANNEL_SELECT_FLAG_ALLOWDISABLED);
    info.pszTuner = nullptr;
    info.Space = -1;
    info.Channel = -1;
    info.NetworkID = item.NetworkID;
    info.TransportStreamID = item.TransportStreamID;
    info.ServiceID = item.ServiceID;
    return m_pApp->SelectChannel(&info);
}

bool CViewReservationPlugin::TrySelectByEnumeratingDrivers(ReservationItem &item) {
    m_driverCache.RebuildIfNeeded(m_pApp, false);

    DriverChannelEntry found{};
    auto findTarget = [&]() {
        if (!item.LastDriverName.empty() &&
            m_driverCache.FindExactOnDriver(
                item.NetworkID, item.TransportStreamID, item.ServiceID, item.LastDriverName, &found)) {
            return true;
        }
        return m_driverCache.FindExact(item.NetworkID, item.TransportStreamID, item.ServiceID, &found);
    };

    if (!findTarget()) {
        m_driverCache.RebuildIfNeeded(m_pApp, true);
        if (!findTarget()) {
            return false;
        }
    }

    TVTest::ChannelSelectInfo info{};
    info.Size = sizeof(info);
    info.Flags = static_cast<TVTest::ChannelSelectFlag>(
        TVTest::CHANNEL_SELECT_FLAG_STRICTSERVICE |
        TVTest::CHANNEL_SELECT_FLAG_ALLOWDISABLED);
    info.pszTuner = found.DriverName.c_str();
    info.Space = found.Space;
    info.Channel = found.Channel;
    info.NetworkID = item.NetworkID;
    info.TransportStreamID = item.TransportStreamID;
    info.ServiceID = item.ServiceID;

    if (!m_pApp->SelectChannel(&info)) {
        m_driverCache.Invalidate();
        return false;
    }

    item.LastDriverName = found.DriverName;
    return true;
}

CViewReservationPlugin::ReservationTriggerResult CViewReservationPlugin::TrySwitchReservation(
    ReservationItem &item,
    const std::wstring &triggerKey) {
    const wchar_t *actionLabel = item.RecordOnTrigger ? L"録画予約" : L"視聴予約";
    const std::wstring prefix = L"[" + std::wstring(actionLabel) + L"] ";

    // 自動予約では30秒前から準備するが、現在録画中はチャンネル切替を要求しない。
    // 録画終了後の次回監視で同じ予約を再評価し、そこから視聴/録画の切替を行う。
    // 手動の即時チャンネル切替は TrySwitchToItem() を直接使うため、この待機規則の対象外。
    if (IsRecordingActive()) {
        return ReservationTriggerResult::WaitingForRecording;
    }

    // 視聴予約はチャンネル切替成功を完了条件とする。
    if (!item.RecordOnTrigger) {
        return TrySwitchToItem(item, actionLabel)
            ? ReservationTriggerResult::Completed
            : ReservationTriggerResult::Failed;
    }

    // 録画予約は StartRecord 成功だけを完了条件とする。
    // 既存録画には触れず、実行済みにもしない。

    TVTest::ChannelInfo currentChannel{};
    currentChannel.Size = sizeof(currentChannel);
    const bool onSameService =
        m_pApp->GetCurrentChannelInfo(&currentChannel) &&
        IsCurrentDriverPreferred(item) &&
        currentChannel.NetworkID == item.NetworkID &&
        currentChannel.TransportStreamID == item.TransportStreamID &&
        currentChannel.ServiceID == item.ServiceID;

    if (onSameService) {
        return TryStartRecording(item, actionLabel)
            ? ReservationTriggerResult::Completed
            : ReservationTriggerResult::Failed;
    }

    if (!BeginPendingRecording(item, actionLabel, triggerKey)) {
        Log(prefix + L"別の録画予約を処理中のため開始できません: " + item.EventName);
        return ReservationTriggerResult::Failed;
    }

    if (TrySelectOnCurrentDriver(item)) {
        m_cv.notify_all();
        return ReservationTriggerResult::Pending;
    }

    if (TrySelectByEnumeratingDrivers(item)) {
        m_cv.notify_all();
        return ReservationTriggerResult::Pending;
    }

    ClearPendingRecording();
    Log(prefix + L"チャンネルを切り替えられませんでした: " + item.EventName);
    return ReservationTriggerResult::Failed;
}

bool CViewReservationPlugin::TrySwitchToItem(ReservationItem &item, const wchar_t *actionLabel) {
    const std::wstring prefix = L"[" + std::wstring(actionLabel != nullptr ? actionLabel : L"操作") + L"] ";

    // 現在局は NID / TSID / SID の3点で照合する。
    TVTest::ChannelInfo currentChannel{};
    currentChannel.Size = sizeof(currentChannel);
    const bool onSameService =
        m_pApp->GetCurrentChannelInfo(&currentChannel) &&
        IsCurrentDriverPreferred(item) &&
        currentChannel.NetworkID == item.NetworkID &&
        currentChannel.TransportStreamID == item.TransportStreamID &&
        currentChannel.ServiceID == item.ServiceID;

    if (onSameService) {
        return true;
    }

    if (TrySelectOnCurrentDriver(item)) {
        return true;
    }

    if (TrySelectByEnumeratingDrivers(item)) {
        return true;
    }

    Log(prefix + L"チャンネルを切り替えられませんでした: " + item.EventName);
    return false;
}

bool CViewReservationPlugin::IsRecordingActive() const {
    TVTest::RecordStatusInfo statusInfo{};
    statusInfo.Size = sizeof(statusInfo);
    return m_pApp != nullptr &&
           m_pApp->GetRecordStatus(&statusInfo) &&
           statusInfo.Status == TVTest::RECORD_STATUS_RECORDING;
}

bool CViewReservationPlugin::BeginPendingRecording(
    const ReservationItem &item,
    const wchar_t *actionLabel,
    const std::wstring &triggerKey) {
    std::lock_guard<std::mutex> lock(m_pendingRecordMutex);
    if (m_pendingRecord.Active) {
        return false;
    }
    m_pendingRecord.Active = true;
    m_pendingRecord.StartedTick = GetTickMs();
    m_pendingRecord.Item = item;
    m_pendingRecord.Label = actionLabel != nullptr ? actionLabel : L"録画";
    m_pendingRecord.TriggerKey = triggerKey;
    m_cv.notify_all();
    return true;
}

void CViewReservationPlugin::ClearPendingRecording() {
    std::lock_guard<std::mutex> lock(m_pendingRecordMutex);
    m_pendingRecord = PendingRecordRequest{};
}

bool CViewReservationPlugin::HasPendingRecording() const {
    std::lock_guard<std::mutex> lock(m_pendingRecordMutex);
    return m_pendingRecord.Active;
}

void CViewReservationPlugin::TryCompletePendingRecording() {
    PendingRecordRequest snapshot{};
    {
        std::lock_guard<std::mutex> lock(m_pendingRecordMutex);
        if (!m_pendingRecord.Active) {
            return;
        }
        snapshot = m_pendingRecord;
    }

    const ULONGLONG elapsed = GetTickMs() - snapshot.StartedTick;
    if (elapsed >= kRecordSwitchTimeoutMs) {
        bool expired = false;
        {
            std::lock_guard<std::mutex> lock(m_pendingRecordMutex);
            if (m_pendingRecord.Active && m_pendingRecord.StartedTick == snapshot.StartedTick) {
                m_pendingRecord = PendingRecordRequest{};
                expired = true;
            }
        }
        if (expired) {
            Log(L"[" + snapshot.Label + L"] 予約先のチャンネルへ切り替えられなかったため録画を開始できません: " + snapshot.Item.EventName);
            ReleaseTriggerKey(snapshot.TriggerKey);
        }
        return;
    }

    TVTest::ChannelInfo current{};
    current.Size = sizeof(current);
    if (m_pApp == nullptr ||
        !m_pApp->GetCurrentChannelInfo(&current) ||
        current.NetworkID != snapshot.Item.NetworkID ||
        current.TransportStreamID != snapshot.Item.TransportStreamID ||
        current.ServiceID != snapshot.Item.ServiceID) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_pendingRecordMutex);
        if (!m_pendingRecord.Active || m_pendingRecord.StartedTick != snapshot.StartedTick) {
            return;
        }
        m_pendingRecord = PendingRecordRequest{};
    }

    if (IsRecordingActive()) {
        Log(L"[" + snapshot.Label + L"] 録画中のため開始できません: " + snapshot.Item.EventName);
        ReleaseTriggerKey(snapshot.TriggerKey);
        return;
    }

    if (TryStartRecording(snapshot.Item, snapshot.Label.c_str())) {
        m_manager->MarkTriggered(
            snapshot.Item.NetworkID,
            snapshot.Item.TransportStreamID,
            snapshot.Item.ServiceID,
            snapshot.Item.EventID);
    } else {
        ReleaseTriggerKey(snapshot.TriggerKey);
    }
}

bool CViewReservationPlugin::OnChannelChange() {
    PostPanelUiStateRefresh(true);
    m_cv.notify_all();
    return true;
}

bool CViewReservationPlugin::OnServiceChange() {
    PostPanelUiStateRefresh(true);
    m_cv.notify_all();
    return true;
}

bool CViewReservationPlugin::OnDriverChange() {
    m_driverCache.Invalidate();
    if (m_panelWindow != nullptr) {
        RefreshPanelRows(true);
        PostPanelUiStateRefresh(true);
    }
    m_cv.notify_all();
    return true;
}

bool CViewReservationPlugin::TryStartRecording(const ReservationItem &item, const wchar_t *actionLabel) {
    const std::wstring prefix = L"[" + std::wstring(actionLabel != nullptr ? actionLabel : L"録画") + L"] ";
    // 既存録画は停止しない。開始直前にも再確認する。
    if (IsRecordingActive()) {
        Log(prefix + L"録画中のため開始できません: " + item.EventName);
        return false;
    }

    // 終了時刻 = 開始時刻 + Duration (秒)
    TVTest::RecordInfo info{};
    info.Size = sizeof(info);

    // 開始: 即時
    info.Mask = TVTest::RECORD_MASK_STOPTIME;
    info.StartTimeSpec = TVTest::RECORD_START_NOTSPECIFIED;

    // 停止: 現在時刻から番組終了時刻までの残り時間（ms）
    const ULONGLONG msUntilEnd = [&]() -> ULONGLONG {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        FILETIME ftNow{}, ftStart{};
        SystemTimeToFileTime(&now, &ftNow);
        SystemTimeToFileTime(&item.StartTime, &ftStart);
        ULARGE_INTEGER uiStart{}, uiNow{};
        uiStart.LowPart = ftStart.dwLowDateTime; uiStart.HighPart = ftStart.dwHighDateTime;
        uiNow.LowPart   = ftNow.dwLowDateTime;   uiNow.HighPart   = ftNow.dwHighDateTime;
        const ULONGLONG endValue = uiStart.QuadPart + static_cast<ULONGLONG>(item.Duration) * ViewReservationTime::TicksPerSecond;
        if (endValue <= uiNow.QuadPart) return 0ULL;
        return (endValue - uiNow.QuadPart) / 10000ULL; // 100ns → ms
    }();

    if (msUntilEnd == 0) {
        Log(prefix + L"番組が終了しているため録画を開始できません: " + item.EventName);
        return false;
    }

    info.StopTimeSpec = TVTest::RECORD_STOP_DURATION;
    info.StopTime.Duration = msUntilEnd;

    if (m_pApp->StartRecord(&info)) {
        Log(prefix + L"録画開始: " + item.EventName);
        return true;
    }

    Log(prefix + L"録画を開始できませんでした: " + item.EventName);
    return false;
}

TVTest::CTVTestPlugin *CreatePluginClass() {
    return new CViewReservationPlugin();
}
