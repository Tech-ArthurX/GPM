#include "gpm_ui.h"
#include "resource.h"

#include <winhttp.h>
#include <shellapi.h>
#include <shcore.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#define WM_APP_WS_MESSAGE (WM_APP + 42)
#define WM_APP_WS_DISCONNECTED (WM_APP + 43)

using namespace gpm_ui;

namespace {

constexpr int kBaseW = 1280;
constexpr int kBaseH = 760;
constexpr int kTitleH = 32;
constexpr int kSidebarExpandedW = 218;
constexpr int kSidebarCollapsedW = 64;
constexpr int kFooterH = 54;
constexpr int kToolbarH = 52;
constexpr int kShadowPad = 12;
constexpr int kNavCount = 5;
constexpr UINT_PTR kUiTimerId = 5;
constexpr UINT kUiTimerMs = 8;
constexpr float kSidebarAnimMs = 230.0f;
constexpr float kThemeTransitionMs = 230.0f;
constexpr float kMenuAnimMs = 120.0f;
constexpr DWORD kSplashHoldMs = 1350;
constexpr float kSplashIntroMs = 420.0f;
constexpr float kSplashOutroMs = 520.0f;
constexpr float kSplashMinAlpha = 0.18f;
constexpr int kAutoDpiFloor = 100;
constexpr int kAutoDpiCeil = 300;
constexpr float kBodyRadius = 8.0f;
constexpr wchar_t kIconFontFamily[] = L"Segoe Fluent Icons";
constexpr DWORD kFixedStyle = GPMWND_STYLE_CLOSE | GPMWND_STYLE_MINIMIZE |
    GPMWND_STYLE_MOVEABLE | GPMWND_STYLE_CENTER | GPMWND_STYLE_MAINWINDOW;

enum class Page {
    Market,
    Installed,
    Versions,
    Settings,
    About
};

struct PackageVersion {
    std::wstring name;
    std::wstring version;
    std::wstring author;
    std::wstring category;
    std::wstring description;
    std::wstring url;
    long long size = 0;
};

struct PackageGroup {
    std::wstring name;
    std::wstring latestVersion;
    std::wstring author;
    std::wstring category;
    std::wstring description;
    long long size = 0;
    int versionCount = 0;
    std::vector<PackageVersion> versions;
    std::wstring installedVersion;
    bool selected = false;
};

struct InstalledItem {
    std::wstring name;
    std::wstring version;
    std::wstring author;
    std::wstring description;
    std::wstring installDate;
    bool selected = false;
};

struct InstallQueueItem {
    std::wstring name;
    std::wstring version;
    std::wstring author;
    std::wstring category;
    long long size = 0;
    bool selected = false;
};

struct QueueExitAnim {
    InstallQueueItem item;
    float y = 0.0f;
    ULONGLONG started = 0;
};

struct LogEntry {
    std::wstring level;
    std::wstring message;
    std::wstring time;
};

struct ProgressInfo {
    std::wstring id;
    std::wstring packageName;
    std::wstring status;
    std::wstring stage;
    std::wstring error;
    long long downloaded = 0;
    long long total = 0;
    long long speed = 0;
    int threads = 0;
    int percent = 0;
    bool active = false;
};

struct DialogInfo {
    std::wstring id;
    std::wstring title;
    std::wstring message;
    std::vector<std::wstring> options;
    bool active = false;
};

struct Toast {
    std::wstring title;
    std::wstring message;
    std::wstring severity;
    ULONGLONG created = 0;
    std::wstring time;
};

struct Spark {
    float x = 0;
    float y = 0;
    float vx = 0;
    float vy = 0;
    ULONGLONG created = 0;
    COLORREF color = RGB(255, 255, 255);
};

struct RectF {
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
    bool Contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

struct ButtonHit {
    std::wstring id;
    RectF rc;
};

struct RowHit {
    bool installed = false;
    int index = -1;
    RectF rowRc;
    RectF checkRc;
    RectF actionRc;
};

struct ColumnResizeHit {
    bool installed = false;
    int column = -1;
    RectF rc;
};

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return L"";
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string out(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring Trim(const std::wstring& text) {
    size_t a = 0;
    while (a < text.size() && iswspace(text[a])) ++a;
    size_t b = text.size();
    while (b > a && iswspace(text[b - 1])) --b;
    return text.substr(a, b - a);
}

std::wstring Lower(std::wstring text) {
    for (auto& ch : text) ch = static_cast<wchar_t>(towlower(ch));
    return text;
}

bool IEquals(const std::wstring& a, const std::wstring& b) {
    return Lower(a) == Lower(b);
}

std::wstring NowTime() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[16];
    swprintf_s(buf, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::wstring EscapeJson(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size() + 8);
    for (wchar_t ch : text) {
        switch (ch) {
        case L'\\': out += L"\\\\"; break;
        case L'"': out += L"\\\""; break;
        case L'\n': out += L"\\n"; break;
        case L'\r': out += L"\\r"; break;
        case L'\t': out += L"\\t"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

std::wstring GetExeDir() {
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return L"";
    std::wstring full(path);
    size_t pos = full.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L"";
    return full.substr(0, pos);
}

bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool WriteBinaryFile(const std::wstring& path, const void* data, DWORD size) {
    if (path.empty() || !data || size == 0) return false;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, data, size, &written, nullptr);
    CloseHandle(h);
    return ok && written == size;
}

std::wstring ReadUtf8File(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(h);
        return L"";
    }
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    BOOL ok = ReadFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(h);
    if (!ok) return L"";
    bytes.resize(read);
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }
    return Utf8ToWide(bytes);
}

    bool WriteUtf8File(const std::wstring& path, const std::wstring& text) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::string bytes = WideToUtf8(text);
    DWORD written = 0;
    BOOL ok = WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(h);
    return ok && written == bytes.size();
}

ID2D1Bitmap* LoadBitmapFile(ID2D1RenderTarget* rt, const std::wstring& path) {
    if (!rt || path.empty()) return nullptr;
    IWICImagingFactory* wicFactory = ExD2DFactory::GetWICFactory();
    if (!wicFactory) return nullptr;

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = wicFactory->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr) || !decoder) return nullptr;

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        decoder->Release();
        return nullptr;
    }

    IWICFormatConverter* converter = nullptr;
    hr = wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) {
        frame->Release();
        decoder->Release();
        return nullptr;
    }

    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut);
    ID2D1Bitmap* bitmap = nullptr;
    if (SUCCEEDED(hr)) hr = rt->CreateBitmapFromWicBitmap(converter, nullptr, &bitmap);

    converter->Release();
    frame->Release();
    decoder->Release();
    return SUCCEEDED(hr) ? bitmap : nullptr;
}

bool RegisterPrivateFonts() {
    HMODULE module = GetModuleHandleW(nullptr);
    if (!module) return false;
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_FLUENT_ICON_FONT), RT_RCDATA);
    if (!resource) return false;
    DWORD size = SizeofResource(module, resource);
    if (size == 0) return false;
    HGLOBAL loaded = LoadResource(module, resource);
    if (!loaded) return false;
    const void* data = LockResource(loaded);
    if (!data) return false;

    wchar_t tempDir[MAX_PATH] = {};
    DWORD dirLen = GetTempPathW(MAX_PATH, tempDir);
    if (dirLen == 0 || dirLen >= MAX_PATH) {
        return ExFont::RegisterFontMemory(data, size);
    }

    std::wstring fontDir = std::wstring(tempDir) + L"gpm-ui";
    CreateDirectoryW(fontDir.c_str(), nullptr);
    std::wstring fontPath = fontDir + L"\\SegoeFluentIcons.ttf";

    bool needsWrite = true;
    HANDLE existing = CreateFileW(fontPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (existing != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER existingSize{};
        if (GetFileSizeEx(existing, &existingSize) && existingSize.QuadPart == size) {
            needsWrite = false;
        }
        CloseHandle(existing);
    }
    if (needsWrite && !WriteBinaryFile(fontPath, data, size)) {
        return ExFont::RegisterFontMemory(data, size);
    }

    if (ExFont::RegisterFontFile(fontPath)) return true;
    return ExFont::RegisterFontMemory(data, size);
}

std::vector<std::wstring> ListJsonStems(const std::wstring& dir) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\*.json").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring name = fd.cFileName;
        if (name.size() > 5 && Lower(name.substr(name.size() - 5)) == L".json") {
            out.push_back(name.substr(0, name.size() - 5));
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const std::wstring& a, const std::wstring& b) {
        return Lower(a) < Lower(b);
    });
    return out;
}

void EnableDpiAwareness() {
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32) {
        using SetCtx = BOOL(WINAPI*)(HANDLE);
        auto fn = reinterpret_cast<SetCtx>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (fn && fn(reinterpret_cast<HANDLE>(-4))) {
            FreeLibrary(user32);
            return;
        }
        FreeLibrary(user32);
    }
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        using SetAwareness = HRESULT(WINAPI*)(int);
        auto fn = reinterpret_cast<SetAwareness>(GetProcAddress(shcore, "SetProcessDpiAwareness"));
        if (fn) fn(2);
        FreeLibrary(shcore);
    } else {
        SetProcessDPIAware();
    }
}

float DetectScale() {
    HDC hdc = GetDC(nullptr);
    int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSX) : 96;
    if (hdc) ReleaseDC(nullptr, hdc);
    if (dpi <= 0) dpi = 96;
    float scale = static_cast<float>(dpi) / 96.0f;
    if (scale < 1.0f) scale = 1.0f;
    if (scale > 3.0f) scale = 3.0f;
    return scale;
}

RECT WorkAreaRect() {
    RECT rc{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0)) {
        rc.left = 0;
        rc.top = 0;
        rc.right = GetSystemMetrics(SM_CXSCREEN);
        rc.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    return rc;
}

float FitScaleToWorkArea(float desired) {
    RECT rc = WorkAreaRect();
    int workW = rc.right - rc.left;
    int workH = rc.bottom - rc.top;
    float maxFit = (std::min)(
        static_cast<float>(workW) / static_cast<float>(kBaseW),
        static_cast<float>(workH) / static_cast<float>(kBaseH));
    if (maxFit >= 1.25f && desired > maxFit) desired = 1.25f;
    if (desired < 1.0f) desired = 1.0f;
    if (desired > 3.0f) desired = 3.0f;
    return desired;
}

void ClampWindowToWorkArea(HWND hwnd, int w, int h, bool recenter) {
    RECT wa = WorkAreaRect();
    RECT rc{};
    GetWindowRect(hwnd, &rc);
    int x = rc.left;
    int y = rc.top;
    if (recenter) {
        x = wa.left + ((wa.right - wa.left) - w) / 2;
        y = wa.top + ((wa.bottom - wa.top) - h) / 2;
    }
    if (w <= wa.right - wa.left) {
        if (x < wa.left) x = wa.left;
        if (x + w > wa.right) x = wa.right - w;
    } else {
        x = wa.left;
    }
    if (h <= wa.bottom - wa.top) {
        if (y < wa.top) y = wa.top;
        if (y + h > wa.bottom) y = wa.bottom - h;
    } else {
        y = wa.top;
    }
    SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

int CompareVersions(const std::wstring& a, const std::wstring& b) {
    auto parse = [](std::wstring value) {
        if (!value.empty() && (value[0] == L'v' || value[0] == L'V')) value.erase(value.begin());
        std::vector<int> parts;
        std::wstringstream ss(value);
        std::wstring token;
        while (std::getline(ss, token, L'.')) {
            try {
                parts.push_back(std::stoi(token));
            } catch (...) {
                parts.push_back(0);
            }
        }
        return parts;
    };
    auto pa = parse(a);
    auto pb = parse(b);
    size_t n = (std::max)(pa.size(), pb.size());
    for (size_t i = 0; i < n; ++i) {
        int va = i < pa.size() ? pa[i] : 0;
        int vb = i < pb.size() ? pb[i] : 0;
        if (va > vb) return 1;
        if (va < vb) return -1;
    }
    return 0;
}

std::wstring FormatSize(long long size) {
    if (size <= 0) return L"-";
    const wchar_t* units[] = { L"B", L"KiB", L"MiB", L"GiB" };
    double value = static_cast<double>(size);
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    wchar_t buf[64];
    if (unit == 0) swprintf_s(buf, L"%lld %s", size, units[unit]);
    else swprintf_s(buf, L"%.1f %s", value, units[unit]);
    return buf;
}

std::wstring FormatTransferSize(long long size) {
    return size > 0 ? FormatSize(size) : L"-";
}

std::wstring SeverityText(std::wstring value) {
    value = Lower(Trim(value));
    if (value == L"success" || value == L"ok" || value == L"done") return L"success";
    if (value == L"warn" || value == L"warning") return L"warning";
    if (value == L"error" || value == L"err" || value == L"fatal") return L"error";
    return L"info";
}

COLORREF Mix(COLORREF a, COLORREF b, float t) {
    t = (std::max)(0.0f, (std::min)(1.0f, t));
    int r = static_cast<int>(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t + 0.5f);
    int g = static_cast<int>(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t + 0.5f);
    int bl = static_cast<int>(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t + 0.5f);
    return RGB(r, g, bl);
}

float Clamp01(float v) {
    return (std::max)(0.0f, (std::min)(1.0f, v));
}

float EaseOutCubic(float v) {
    v = Clamp01(v);
    float t = 1.0f - v;
    return 1.0f - t * t * t;
}

float EaseInOutCubic(float v) {
    v = Clamp01(v);
    if (v < 0.5f) return 4.0f * v * v * v;
    float t = -2.0f * v + 2.0f;
    return 1.0f - (t * t * t) * 0.5f;
}

float FrameBlend(float referenceBlend, float dtMs) {
    referenceBlend = Clamp01(referenceBlend);
    dtMs = (std::max)(0.0f, (std::min)(50.0f, dtMs));
    if (referenceBlend >= 1.0f) return 1.0f;
    if (referenceBlend <= 0.0f || dtMs <= 0.0f) return 0.0f;
    return 1.0f - std::pow(1.0f - referenceBlend, dtMs / 16.6667f);
}

float ColorLuma(COLORREF c) {
    return GetRValue(c) * 0.2126f + GetGValue(c) * 0.7152f + GetBValue(c) * 0.0722f;
}

namespace Palette {
    COLORREF Window = RGB(245, 248, 251);
    COLORREF Surface = RGB(255, 255, 255);
    COLORREF SurfaceAlt = RGB(238, 244, 249);
    COLORREF Sidebar = RGB(36, 45, 58);
    COLORREF SidebarHover = RGB(48, 61, 79);
    COLORREF Text = RGB(28, 34, 44);
    COLORREF TextSoft = RGB(74, 86, 103);
    COLORREF TextMuted = RGB(119, 132, 150);
    COLORREF Border = RGB(214, 225, 236);
    COLORREF BorderStrong = RGB(178, 196, 216);
    COLORREF Primary = RGB(44, 94, 173);
    COLORREF PrimaryHover = RGB(21, 145, 220);
    COLORREF PrimarySoft = RGB(196, 226, 245);
    COLORREF Success = RGB(42, 145, 94);
    COLORREF Warning = RGB(184, 126, 38);
    COLORREF Error = RGB(194, 65, 58);
    COLORREF Info = RGB(75, 184, 250);
    COLORREF ProgressTrack = RGB(224, 233, 242);
    COLORREF RowHover = RGB(247, 251, 254);
    COLORREF RowBorder = RGB(229, 237, 245);
}

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::wstring stringValue;
    std::vector<JsonValue> arrayValue;
    std::map<std::wstring, JsonValue> objectValue;

    const JsonValue* Get(const std::wstring& key) const {
        auto it = objectValue.find(key);
        return it == objectValue.end() ? nullptr : &it->second;
    }

    std::wstring String(const std::wstring& fallback = L"") const {
        if (type == Type::String) return stringValue;
        if (type == Type::Number) {
            long long v = static_cast<long long>(numberValue);
            return std::to_wstring(v);
        }
        if (type == Type::Bool) return boolValue ? L"true" : L"false";
        return fallback;
    }

    long long Int64(long long fallback = 0) const {
        if (type == Type::Number) return static_cast<long long>(numberValue);
        if (type == Type::String) {
            try { return std::stoll(stringValue); } catch (...) { return fallback; }
        }
        return fallback;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::wstring& text) : text_(text) {}

    JsonValue Parse() {
        pos_ = 0;
        return ParseValue();
    }

private:
    JsonValue ParseValue() {
        SkipWs();
        if (pos_ >= text_.size()) return {};
        wchar_t ch = text_[pos_];
        if (ch == L'"') return ParseString();
        if (ch == L'{') return ParseObject();
        if (ch == L'[') return ParseArray();
        if (ch == L't' || ch == L'f') return ParseBool();
        if (ch == L'n') return ParseNull();
        return ParseNumber();
    }

    JsonValue ParseNull() {
        JsonValue v;
        if (text_.compare(pos_, 4, L"null") == 0) pos_ += 4;
        return v;
    }

    JsonValue ParseBool() {
        JsonValue v;
        v.type = JsonValue::Type::Bool;
        if (text_.compare(pos_, 4, L"true") == 0) {
            v.boolValue = true;
            pos_ += 4;
        } else if (text_.compare(pos_, 5, L"false") == 0) {
            v.boolValue = false;
            pos_ += 5;
        }
        return v;
    }

    JsonValue ParseNumber() {
        JsonValue v;
        v.type = JsonValue::Type::Number;
        size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == L'-') ++pos_;
        while (pos_ < text_.size() && iswdigit(text_[pos_])) ++pos_;
        if (pos_ < text_.size() && text_[pos_] == L'.') {
            ++pos_;
            while (pos_ < text_.size() && iswdigit(text_[pos_])) ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == L'e' || text_[pos_] == L'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == L'+' || text_[pos_] == L'-')) ++pos_;
            while (pos_ < text_.size() && iswdigit(text_[pos_])) ++pos_;
        }
        try {
            v.numberValue = std::stod(text_.substr(start, pos_ - start));
        } catch (...) {
            v.numberValue = 0;
        }
        return v;
    }

    JsonValue ParseString() {
        JsonValue v;
        v.type = JsonValue::Type::String;
        if (pos_ >= text_.size() || text_[pos_] != L'"') return v;
        ++pos_;
        while (pos_ < text_.size()) {
            wchar_t ch = text_[pos_++];
            if (ch == L'"') break;
            if (ch == L'\\' && pos_ < text_.size()) {
                wchar_t esc = text_[pos_++];
                switch (esc) {
                case L'"': v.stringValue.push_back(L'"'); break;
                case L'\\': v.stringValue.push_back(L'\\'); break;
                case L'/': v.stringValue.push_back(L'/'); break;
                case L'b': v.stringValue.push_back(L'\b'); break;
                case L'f': v.stringValue.push_back(L'\f'); break;
                case L'n': v.stringValue.push_back(L'\n'); break;
                case L'r': v.stringValue.push_back(L'\r'); break;
                case L't': v.stringValue.push_back(L'\t'); break;
                case L'u':
                    v.stringValue.push_back(ParseUnicodeEscape());
                    break;
                default:
                    v.stringValue.push_back(esc);
                    break;
                }
            } else {
                v.stringValue.push_back(ch);
            }
        }
        return v;
    }

    wchar_t ParseUnicodeEscape() {
        int value = 0;
        for (int i = 0; i < 4 && pos_ < text_.size(); ++i) {
            wchar_t ch = text_[pos_++];
            value <<= 4;
            if (ch >= L'0' && ch <= L'9') value += ch - L'0';
            else if (ch >= L'a' && ch <= L'f') value += ch - L'a' + 10;
            else if (ch >= L'A' && ch <= L'F') value += ch - L'A' + 10;
        }
        return static_cast<wchar_t>(value);
    }

    JsonValue ParseArray() {
        JsonValue v;
        v.type = JsonValue::Type::Array;
        ++pos_;
        SkipWs();
        if (pos_ < text_.size() && text_[pos_] == L']') {
            ++pos_;
            return v;
        }
        while (pos_ < text_.size()) {
            v.arrayValue.push_back(ParseValue());
            SkipWs();
            if (pos_ < text_.size() && text_[pos_] == L',') {
                ++pos_;
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == L']') {
                ++pos_;
                break;
            }
        }
        return v;
    }

    JsonValue ParseObject() {
        JsonValue v;
        v.type = JsonValue::Type::Object;
        ++pos_;
        SkipWs();
        if (pos_ < text_.size() && text_[pos_] == L'}') {
            ++pos_;
            return v;
        }
        while (pos_ < text_.size()) {
            SkipWs();
            JsonValue key = ParseString();
            SkipWs();
            if (pos_ < text_.size() && text_[pos_] == L':') ++pos_;
            JsonValue value = ParseValue();
            v.objectValue[key.stringValue] = value;
            SkipWs();
            if (pos_ < text_.size() && text_[pos_] == L',') {
                ++pos_;
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == L'}') {
                ++pos_;
                break;
            }
        }
        return v;
    }

    void SkipWs() {
        while (pos_ < text_.size() && iswspace(text_[pos_])) ++pos_;
    }

    const std::wstring& text_;
    size_t pos_ = 0;
};

float LoadConfiguredScale(const std::wstring& exeDir, float fallback) {
    if (exeDir.empty()) return fallback;
    JsonValue config = JsonParser(ReadUtf8File(exeDir + L"\\config.json")).Parse();
    const JsonValue* v = config.Get(L"dpi");
    if (!v) return fallback;
    int pct = static_cast<int>(v->Int64(static_cast<long long>(fallback * 100.0f + 0.5f)));
    if (pct < 100) pct = 100;
    if (pct > 300) pct = 300;
    return pct / 100.0f;
}

int DetectAutoDpiPercent() {
    float scale = DetectScale();
    int pct = static_cast<int>(scale * 100.0f + 0.5f);
    if (pct < kAutoDpiFloor) pct = kAutoDpiFloor;
    if (pct > kAutoDpiCeil) pct = kAutoDpiCeil;
    return pct;
}

bool ParseHexColor(const std::wstring& value, COLORREF& out) {
    std::wstring s = Trim(value);
    if (s.empty()) return false;
    if (s[0] == L'#') s.erase(s.begin());
    if (s.size() != 6) return false;
    auto hex = [](wchar_t ch) -> int {
        if (ch >= L'0' && ch <= L'9') return ch - L'0';
        if (ch >= L'a' && ch <= L'f') return ch - L'a' + 10;
        if (ch >= L'A' && ch <= L'F') return ch - L'A' + 10;
        return -1;
    };
    int vals[6]{};
    for (int i = 0; i < 6; ++i) {
        vals[i] = hex(s[i]);
        if (vals[i] < 0) return false;
    }
    out = RGB(vals[0] * 16 + vals[1], vals[2] * 16 + vals[3], vals[4] * 16 + vals[5]);
    return true;
}

bool ParseRgbColor(const std::wstring& value, COLORREF& out) {
    std::wstring s = Lower(Trim(value));
    if (s.rfind(L"rgb(", 0) != 0 || s.empty() || s.back() != L')') return false;
    s = s.substr(4, s.size() - 5);
    std::vector<int> parts;
    std::wstringstream ss(s);
    std::wstring token;
    while (std::getline(ss, token, L',')) {
        try {
            int v = std::stoi(Trim(token));
            parts.push_back((std::max)(0, (std::min)(255, v)));
        } catch (...) {
            return false;
        }
    }
    if (parts.size() != 3) return false;
    out = RGB(parts[0], parts[1], parts[2]);
    return true;
}

COLORREF ParseColorValue(const JsonValue* v, COLORREF fallback) {
    if (!v) return fallback;
    COLORREF out = fallback;
    if (v->type == JsonValue::Type::String) {
        std::wstring s = v->String();
        if (ParseHexColor(s, out) || ParseRgbColor(s, out)) return out;
    }
    if (v->type == JsonValue::Type::Array && v->arrayValue.size() >= 3) {
        int r = static_cast<int>(v->arrayValue[0].Int64());
        int g = static_cast<int>(v->arrayValue[1].Int64());
        int b = static_cast<int>(v->arrayValue[2].Int64());
        return RGB((std::max)(0, (std::min)(255, r)),
            (std::max)(0, (std::min)(255, g)),
            (std::max)(0, (std::min)(255, b)));
    }
    return fallback;
}

void ApplyDefaultTheme(bool dark) {
    if (dark) {
        Palette::Window = RGB(32, 31, 30);
        Palette::Surface = RGB(41, 40, 39);
        Palette::SurfaceAlt = RGB(50, 49, 48);
        Palette::Sidebar = RGB(37, 36, 35);
        Palette::SidebarHover = RGB(51, 49, 48);
        Palette::Text = RGB(243, 242, 241);
        Palette::TextSoft = RGB(201, 198, 196);
        Palette::TextMuted = RGB(161, 159, 157);
        Palette::Border = RGB(72, 70, 68);
        Palette::BorderStrong = RGB(96, 94, 92);
        Palette::Primary = RGB(0, 120, 212);
        Palette::PrimaryHover = RGB(16, 110, 190);
        Palette::PrimarySoft = RGB(0, 90, 158);
        Palette::Success = RGB(16, 124, 16);
        Palette::Warning = RGB(255, 185, 0);
        Palette::Error = RGB(209, 52, 56);
        Palette::Info = RGB(0, 153, 188);
        Palette::ProgressTrack = RGB(59, 58, 57);
        Palette::RowHover = RGB(50, 49, 48);
        Palette::RowBorder = RGB(61, 59, 58);
    } else {
        Palette::Window = RGB(250, 249, 248);
        Palette::Surface = RGB(255, 255, 255);
        Palette::SurfaceAlt = RGB(243, 242, 241);
        Palette::Sidebar = RGB(250, 249, 248);
        Palette::SidebarHover = RGB(243, 242, 241);
        Palette::Text = RGB(32, 31, 30);
        Palette::TextSoft = RGB(96, 94, 92);
        Palette::TextMuted = RGB(138, 136, 134);
        Palette::Border = RGB(225, 223, 221);
        Palette::BorderStrong = RGB(200, 198, 196);
        Palette::Primary = RGB(0, 120, 212);
        Palette::PrimaryHover = RGB(16, 110, 190);
        Palette::PrimarySoft = RGB(199, 224, 244);
        Palette::Success = RGB(16, 124, 16);
        Palette::Warning = RGB(255, 185, 0);
        Palette::Error = RGB(209, 52, 56);
        Palette::Info = RGB(0, 153, 188);
        Palette::ProgressTrack = RGB(237, 235, 233);
        Palette::RowHover = RGB(243, 242, 241);
        Palette::RowBorder = RGB(237, 235, 233);
    }
}

void ApplyThemeObject(const JsonValue& root) {
    bool dark = false;
    if (auto v = root.Get(L"dark")) dark = (v->type == JsonValue::Type::Bool && v->boolValue);
    ApplyDefaultTheme(dark);
    auto set = [&root](const std::wstring& key, COLORREF& target) {
        target = ParseColorValue(root.Get(key), target);
    };
    set(L"window", Palette::Window);
    set(L"surface", Palette::Surface);
    set(L"surfaceAlt", Palette::SurfaceAlt);
    set(L"sidebar", Palette::Sidebar);
    set(L"sidebarHover", Palette::SidebarHover);
    set(L"text", Palette::Text);
    set(L"textSoft", Palette::TextSoft);
    set(L"textMuted", Palette::TextMuted);
    set(L"border", Palette::Border);
    set(L"borderStrong", Palette::BorderStrong);
    set(L"primary", Palette::Primary);
    set(L"primaryHover", Palette::PrimaryHover);
    set(L"primarySoft", Palette::PrimarySoft);
    set(L"success", Palette::Success);
    set(L"warning", Palette::Warning);
    set(L"error", Palette::Error);
    set(L"info", Palette::Info);
    set(L"progressTrack", Palette::ProgressTrack);
    set(L"rowHover", Palette::RowHover);
    set(L"rowBorder", Palette::RowBorder);
}

void ApplyMiniGpmTheme() {
    GpmThemeColors theme = GetOneDarkProTheme();
    theme.bgWindow = Palette::Window;
    theme.bgTitleBar = Palette::Window;
    theme.fgPrimary = Palette::Text;
    theme.fgSecondary = Palette::TextSoft;
    theme.fgAccent = Palette::Primary;
    theme.border = Palette::Border;
    theme.borderFocus = Palette::Primary;
    theme.borderHover = Palette::BorderStrong;
    theme.btnBg = Palette::Surface;
    theme.btnBgHover = Palette::SurfaceAlt;
    theme.btnBgDown = Palette::PrimarySoft;
    theme.closeHover = Palette::Error;
    SetTheme(theme);
}

COLORREF HoverFill() {
    return Mix(Palette::Surface, Palette::SurfaceAlt, ColorLuma(Palette::Surface) < 128.0f ? 0.78f : 0.64f);
}

COLORREF HoverBorder() {
    return Mix(Palette::Border, Palette::BorderStrong, 0.62f);
}

struct PaletteSnapshot {
    COLORREF Window;
    COLORREF Surface;
    COLORREF SurfaceAlt;
    COLORREF Sidebar;
    COLORREF SidebarHover;
    COLORREF Text;
    COLORREF TextSoft;
    COLORREF TextMuted;
    COLORREF Border;
    COLORREF BorderStrong;
    COLORREF Primary;
    COLORREF PrimaryHover;
    COLORREF PrimarySoft;
    COLORREF Success;
    COLORREF Warning;
    COLORREF Error;
    COLORREF Info;
    COLORREF ProgressTrack;
    COLORREF RowHover;
    COLORREF RowBorder;
};

PaletteSnapshot CapturePalette() {
    return {
        Palette::Window,
        Palette::Surface,
        Palette::SurfaceAlt,
        Palette::Sidebar,
        Palette::SidebarHover,
        Palette::Text,
        Palette::TextSoft,
        Palette::TextMuted,
        Palette::Border,
        Palette::BorderStrong,
        Palette::Primary,
        Palette::PrimaryHover,
        Palette::PrimarySoft,
        Palette::Success,
        Palette::Warning,
        Palette::Error,
        Palette::Info,
        Palette::ProgressTrack,
        Palette::RowHover,
        Palette::RowBorder,
    };
}

void ApplyPaletteSnapshot(const PaletteSnapshot& p) {
    Palette::Window = p.Window;
    Palette::Surface = p.Surface;
    Palette::SurfaceAlt = p.SurfaceAlt;
    Palette::Sidebar = p.Sidebar;
    Palette::SidebarHover = p.SidebarHover;
    Palette::Text = p.Text;
    Palette::TextSoft = p.TextSoft;
    Palette::TextMuted = p.TextMuted;
    Palette::Border = p.Border;
    Palette::BorderStrong = p.BorderStrong;
    Palette::Primary = p.Primary;
    Palette::PrimaryHover = p.PrimaryHover;
    Palette::PrimarySoft = p.PrimarySoft;
    Palette::Success = p.Success;
    Palette::Warning = p.Warning;
    Palette::Error = p.Error;
    Palette::Info = p.Info;
    Palette::ProgressTrack = p.ProgressTrack;
    Palette::RowHover = p.RowHover;
    Palette::RowBorder = p.RowBorder;
}

PaletteSnapshot MixPaletteSnapshot(const PaletteSnapshot& a, const PaletteSnapshot& b, float t) {
    t = Clamp01(t);
    return {
        Mix(a.Window, b.Window, t),
        Mix(a.Surface, b.Surface, t),
        Mix(a.SurfaceAlt, b.SurfaceAlt, t),
        Mix(a.Sidebar, b.Sidebar, t),
        Mix(a.SidebarHover, b.SidebarHover, t),
        Mix(a.Text, b.Text, t),
        Mix(a.TextSoft, b.TextSoft, t),
        Mix(a.TextMuted, b.TextMuted, t),
        Mix(a.Border, b.Border, t),
        Mix(a.BorderStrong, b.BorderStrong, t),
        Mix(a.Primary, b.Primary, t),
        Mix(a.PrimaryHover, b.PrimaryHover, t),
        Mix(a.PrimarySoft, b.PrimarySoft, t),
        Mix(a.Success, b.Success, t),
        Mix(a.Warning, b.Warning, t),
        Mix(a.Error, b.Error, t),
        Mix(a.Info, b.Info, t),
        Mix(a.ProgressTrack, b.ProgressTrack, t),
        Mix(a.RowHover, b.RowHover, t),
        Mix(a.RowBorder, b.RowBorder, t),
    };
}

class GpmPanel;
GpmPanel* g_panel = nullptr;

class GpmClient {
public:
    ~GpmClient() { Stop(); }

    void SetNotifyHwnd(HWND hwnd) {
        notifyHwnd_ = hwnd;
    }

    void Start() {
        if (running_) return;
        running_ = true;
        worker_ = std::thread([this]() { Loop(); });
    }

    void Stop() {
        notifyHwnd_ = nullptr;
        running_ = false;
        CloseSocket();
        if (worker_.joinable()) worker_.join();
        connected_ = false;
    }

    bool IsConnected() const {
        return connected_;
    }

    void SendCommand(const std::string& cmd, const std::wstring& params = L"{}") {
        if (!connected_ || !hWebSocket_) return;
        std::wstring payloadW = L"{\"command\":\"" + Utf8ToWide(cmd) + L"\",\"params\":" + params + L"}";
        std::string payload = WideToUtf8(payloadW);
        std::lock_guard<std::mutex> lock(sendMutex_);
        if (hWebSocket_) {
            WinHttpWebSocketSend(hWebSocket_, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                reinterpret_cast<PVOID>(const_cast<char*>(payload.data())),
                static_cast<DWORD>(payload.size()));
        }
    }

private:
    bool WaitForReconnect(DWORD ms) {
        constexpr DWORD kSlice = 30;
        DWORD waited = 0;
        while (running_ && waited < ms) {
            DWORD chunk = (std::min)(kSlice, ms - waited);
            Sleep(chunk);
            waited += chunk;
        }
        return running_;
    }

    void Loop() {
        while (running_) {
            if (!Connect()) {
                PostDisconnected();
                if (!WaitForReconnect(1800)) break;
                continue;
            }
            ReceiveLoop();
            PostDisconnected();
            if (!WaitForReconnect(1800)) break;
        }
    }

    bool Connect() {
        CloseSocket();
        hSession_ = WinHttpOpen(L"GPM", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession_) return false;

        hConnect_ = WinHttpConnect(hSession_, L"127.0.0.1", 8080, 0);
        if (!hConnect_) return false;

        hRequest_ = WinHttpOpenRequest(hConnect_, L"GET", L"/ws", nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!hRequest_) return false;

        if (!WinHttpSetOption(hRequest_, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
            return false;
        }

        if (!WinHttpSendRequest(hRequest_, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0)) {
            return false;
        }
        if (!WinHttpReceiveResponse(hRequest_, nullptr)) return false;

        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest_, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);
        if (statusCode != 101) return false;

        hWebSocket_ = WinHttpWebSocketCompleteUpgrade(hRequest_, 0);
        WinHttpCloseHandle(hRequest_);
        hRequest_ = nullptr;
        if (!hWebSocket_) return false;

        connected_ = true;
        SendCommand("get_index");
        SendCommand("get_installed");
        return true;
    }

    void ReceiveLoop() {
        std::vector<BYTE> buffer(1 << 20);
        while (running_ && hWebSocket_) {
            DWORD bytesRead = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
            DWORD err = WinHttpWebSocketReceive(hWebSocket_, buffer.data(),
                static_cast<DWORD>(buffer.size()), &bytesRead, &type);
            if (err != ERROR_SUCCESS || type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
            if (bytesRead == 0) continue;

            auto* msg = new std::wstring(Utf8ToWide(std::string(
                reinterpret_cast<char*>(buffer.data()), bytesRead)));
            if (notifyHwnd_) PostMessageW(notifyHwnd_, WM_APP_WS_MESSAGE, reinterpret_cast<WPARAM>(msg), 0);
            else delete msg;
        }
        connected_ = false;
        CloseSocket();
    }

    void PostDisconnected() {
        connected_ = false;
        if (notifyHwnd_) PostMessageW(notifyHwnd_, WM_APP_WS_DISCONNECTED, 0, 0);
    }

    void CloseSocket() {
        std::lock_guard<std::mutex> lock(sendMutex_);
        if (hWebSocket_) {
            WinHttpWebSocketClose(hWebSocket_, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
            WinHttpCloseHandle(hWebSocket_);
            hWebSocket_ = nullptr;
        }
        if (hRequest_) { WinHttpCloseHandle(hRequest_); hRequest_ = nullptr; }
        if (hConnect_) { WinHttpCloseHandle(hConnect_); hConnect_ = nullptr; }
        if (hSession_) { WinHttpCloseHandle(hSession_); hSession_ = nullptr; }
    }

    std::atomic<bool> running_{ false };
    std::atomic<bool> connected_{ false };
    HWND notifyHwnd_ = nullptr;
    std::thread worker_;
    std::mutex sendMutex_;
    HINTERNET hSession_ = nullptr;
    HINTERNET hConnect_ = nullptr;
    HINTERNET hRequest_ = nullptr;
    HINTERNET hWebSocket_ = nullptr;
};

class GpmPanel : public UIElement {
public:
    explicit GpmPanel(float initialScale) {
        m_id = 100;
        m_x = 0;
        m_y = 0;
        dpi_ = DetectScale();
        userScale_ = initialScale;
        chosenScale_ = ClampScale(userScale_);
        exeDir_ = GetExeDir();
        EnsureDefaultConfigFiles();
        LoadConfig();
        watermarkPath_ = exeDir_ + L"\\assets\\glim.ico";
        m_width = Scale(kBaseW + kShadowPad * 2);
        m_height = Scale(kBaseH + kShadowPad * 2);
        AddLog(L"INFO", L"GPM frontend starting");
    }

    ~GpmPanel() override {
        client_.SetNotifyHwnd(nullptr);
        watermarkBitmap_.Reset();
        watermarkRenderTarget_ = nullptr;
        watermarkLoadFailed_ = false;
        client_.Stop();
    }

    void AttachWindow(GpmWindow* wnd) {
        m_parentWnd = wnd;
        m_hWnd = wnd ? wnd->GetWindowHandle() : nullptr;
        client_.SetNotifyHwnd(m_hWnd);
        client_.Start();
        timeBeginPeriod(1);
        SetTimer(m_hWnd, kUiTimerId, kUiTimerMs, nullptr);
        backendLaunchRequested_ = StartBackend();
    }

    void OnBackendMessage(const std::wstring& msg) {
        std::lock_guard<std::recursive_mutex> lock(mu_);
        JsonValue root = JsonParser(msg).Parse();
        std::wstring type = root.Get(L"type") ? root.Get(L"type")->String() : L"";
        if (type == L"index_data" || type == L"search_result") {
            const JsonValue* data = root.Get(L"data");
            indexData_.clear();
            int categorized = 0;
            if (data && data->type == JsonValue::Type::Array) {
                for (const auto& item : data->arrayValue) {
                    PackageVersion p;
                    if (auto v = item.Get(L"name")) p.name = v->String();
                    if (auto v = item.Get(L"version")) p.version = v->String();
                    if (auto v = item.Get(L"author")) p.author = v->String();
                    p.category = CategoryFromJsonItem(item);
                    if (auto v = item.Get(L"description")) p.description = v->String();
                    if (auto v = item.Get(L"url")) p.url = v->String();
                    if (auto v = item.Get(L"size")) p.size = v->Int64();
                    if (!p.name.empty()) {
                        if (!Trim(p.category).empty()) ++categorized;
                        indexData_.push_back(p);
                    }
                }
            }
            RebuildGroups();
            UpdateStats();
            AddLog(L"INFO", L"Index data loaded: " + std::to_wstring(static_cast<int>(indexData_.size())) +
                L" items, " + std::to_wstring(categorized) + L" categorized");
            if (seenIndexData_) AddToast(Tr(L"toast.index", L"Index"), Tr(L"toast.indexLoaded", L"Package index loaded"), L"success");
            seenIndexData_ = true;
        } else if (type == L"installed_data") {
            const JsonValue* data = root.Get(L"data");
            installed_.clear();
            if (data && data->type == JsonValue::Type::Array) {
                for (const auto& item : data->arrayValue) {
                    InstalledItem p;
                    if (auto v = item.Get(L"name")) p.name = v->String();
                    if (auto v = item.Get(L"version")) p.version = v->String();
                    if (auto v = item.Get(L"author")) p.author = v->String();
                    if (auto v = item.Get(L"description")) p.description = v->String();
                    if (auto v = item.Get(L"install_date")) p.installDate = v->String();
                    if (!p.name.empty()) installed_.push_back(p);
                }
            }
            std::sort(installed_.begin(), installed_.end(), [](const InstalledItem& a, const InstalledItem& b) {
                return Lower(a.name) < Lower(b.name);
            });
            RebuildGroups();
            UpdateStats();
        } else if (type == L"progress") {
            ProgressInfo p;
            const JsonValue* obj = root.Get(L"progress");
            if (!obj) obj = root.Get(L"data");
            if (obj) {
                if (auto v = obj->Get(L"id")) p.id = v->String();
                if (auto v = obj->Get(L"package")) p.packageName = v->String();
                if (auto v = obj->Get(L"status")) p.status = v->String();
                if (auto v = obj->Get(L"stage")) p.stage = v->String();
                if (auto v = obj->Get(L"percent")) p.percent = static_cast<int>(v->Int64());
                if (auto v = obj->Get(L"downloaded")) p.downloaded = v->Int64();
                if (auto v = obj->Get(L"total")) p.total = v->Int64();
                if (auto v = obj->Get(L"speed")) p.speed = v->Int64();
                if (auto v = obj->Get(L"threads")) p.threads = static_cast<int>(v->Int64());
                if (auto v = obj->Get(L"error")) p.error = v->String();
            }
            p.active = true;
            if (p.percent < 0) p.percent = 0;
            if (p.percent > 100) p.percent = 100;
            progress_ = p;
            float targetProgress = static_cast<float>(p.percent) / 100.0f;
            if (!progressVisualInitialized_) {
                progressVisual_ = targetProgress;
                progressVisualInitialized_ = true;
            } else if (targetProgress + 0.015f < progressVisual_) {
                progressVisual_ = targetProgress;
            }
            progressTarget_ = targetProgress;
            std::wstring line = L"[" + p.stage + L"] " + (p.packageName.empty() ? p.id : p.packageName) +
                L" " + std::to_wstring(p.percent) + L"%";
            AddLog(L"INFO", line);
            if (Lower(p.stage) == L"done" || Lower(p.status) == L"done") {
                progress_.active = false;
                progress_.percent = 100;
                progressTarget_ = 1.0f;
                AddToast(Tr(L"toast.operationComplete", L"Operation complete"), p.packageName.empty() ? p.status : p.packageName, L"success");
                CompleteQueueItem(p.packageName);
                client_.SendCommand("get_installed");
            } else if (Lower(p.stage) == L"cancel" || Lower(p.status) == L"cancelled") {
                progress_.active = false;
                progress_.percent = 0;
                progressTarget_ = 0.0f;
                queueInstalling_ = false;
                AddToast(Tr(L"toast.operationCancelled", L"Operation cancelled"), p.packageName.empty() ? p.status : p.packageName, L"warning");
            } else if (Lower(p.stage) == L"error" || Lower(p.status) == L"error") {
                queueInstalling_ = false;
                AddToast(Tr(L"toast.operationFailed", L"Operation failed"), p.error.empty() ? p.status : p.error, L"error");
            }
        } else if (type == L"log") {
            const JsonValue* obj = root.Get(L"log");
            std::wstring level = L"INFO";
            std::wstring message;
            std::wstring time = NowTime();
            if (obj) {
                if (auto v = obj->Get(L"level")) level = v->String();
                if (auto v = obj->Get(L"message")) message = v->String();
                if (auto v = obj->Get(L"time")) time = v->String();
            }
            AddLog(level, message, time);
            std::wstring sev = SeverityText(level);
            if (sev != L"info") AddToast(level, message, sev);
        } else if (type == L"dialog") {
            const JsonValue* obj = root.Get(L"dialog");
            if (obj) {
                dialog_ = {};
                dialog_.active = true;
                if (auto v = obj->Get(L"id")) dialog_.id = v->String();
                if (auto v = obj->Get(L"title")) dialog_.title = v->String();
                if (auto v = obj->Get(L"message")) dialog_.message = v->String();
                if (auto v = obj->Get(L"options")) {
                    for (const auto& opt : v->arrayValue) dialog_.options.push_back(opt.String());
                }
                AddToast(dialog_.title, dialog_.message, L"warning");
            }
        }
        PruneToasts();
        RedrawAll();
    }

    void OnDisconnected() {
        std::lock_guard<std::recursive_mutex> lock(mu_);
        AddLog(L"WARN", L"Backend disconnected; reconnecting...");
        AddToast(Tr(L"label.backend", L"Backend"), Tr(L"toast.backendDisconnected", L"Disconnected; reconnecting..."), L"warning");
        backendLaunchRequested_ = false;
        RedrawAll();
    }

    void OnTimerTick() {
        std::lock_guard<std::recursive_mutex> lock(mu_);
        bool needsRedraw = false;
        PruneToasts();
        size_t sparkCount = sparks_.size();
        PruneSparks();
        if (sparkCount != sparks_.size() || !sparks_.empty()) needsRedraw = true;
        ULONGLONG now = GetTickCount64();
        float dtMs = lastUiTickAt_ == 0 ? 16.6667f : static_cast<float>(now - lastUiTickAt_);
        lastUiTickAt_ = now;
        float target = static_cast<float>(static_cast<int>(currentPage_));
        float navDelta = target - navAnim_;
        if (std::fabs(navDelta) > 0.0008f) {
            navAnim_ += navDelta * FrameBlend(0.28f, dtMs);
            if (std::fabs(target - navAnim_) <= 0.0008f) navAnim_ = target;
            needsRedraw = true;
        }
        int currentNav = static_cast<int>(currentPage_);
        int hoverTargetIndex = (hoverNav_ >= 0 && hoverNav_ < kNavCount && hoverNav_ != currentNav) ? hoverNav_ : -1;
        if (hoverTargetIndex >= 0) navHoverTarget_ = static_cast<float>(hoverTargetIndex);
        float hoverPosDelta = navHoverTarget_ - navHoverAnim_;
        if (std::fabs(hoverPosDelta) > 0.001f) {
            navHoverAnim_ += hoverPosDelta * FrameBlend(0.36f, dtMs);
            if (std::fabs(navHoverTarget_ - navHoverAnim_) <= 0.001f) navHoverAnim_ = navHoverTarget_;
            needsRedraw = true;
        }
        float hoverAlphaTarget = hoverTargetIndex >= 0 ? 1.0f : 0.0f;
        float hoverAlphaDelta = hoverAlphaTarget - navHoverAlpha_;
        if (std::fabs(hoverAlphaDelta) > 0.002f) {
            navHoverAlpha_ += hoverAlphaDelta * FrameBlend(0.42f, dtMs);
            if (std::fabs(hoverAlphaTarget - navHoverAlpha_) <= 0.002f) navHoverAlpha_ = hoverAlphaTarget;
            needsRedraw = true;
        }
        StepSidebarAnimation(now, needsRedraw);
        StepThemeTransition(now, needsRedraw);
        StepMenuAnimation(dtMs, needsRedraw);
        if (!splashDismissed_) {
            ULONGLONG splashBase = SplashClockStart();
            ULONGLONG splashAge = splashBase == 0 ? 0 : now - splashBase;
            if (splashAge >= kSplashHoldMs) {
                splashDismissed_ = true;
                splashRevealAt_ = now;
                needsRedraw = true;
            } else {
                needsRedraw = true;
            }
        }
        if (splashRevealAt_ != 0 && now - splashRevealAt_ < static_cast<ULONGLONG>(kSplashOutroMs)) needsRedraw = true;
        if (now - pageSwitchAt_ < 240) needsRedraw = true;
        if (now - statusPanelToggledAt_ < 220) needsRedraw = true;
        if (now - navIndicatorStartAt_ < 480) needsRedraw = true;
        if (windowAnimMode_ != 0) {
            if (now - windowAnimStartedAt_ >= 120) {
                FinishWindowAnimation();
                return;
            }
            needsRedraw = true;
        }
        if (!client_.IsConnected() && !backendLaunchRequested_ && now - lastBackendLaunchAt_ > 2200) {
            backendLaunchRequested_ = StartBackend();
            lastBackendLaunchAt_ = now;
        }
        if (progress_.active) {
            progressPhase_ += 0.035f * ((std::max)(0.0f, (std::min)(50.0f, dtMs)) / 16.6667f);
            if (progressPhase_ > 1.0f) progressPhase_ -= 1.0f;
            needsRedraw = true;
        }
        if (progressVisualInitialized_) {
            float delta = progressTarget_ - progressVisual_;
            if (std::fabs(delta) > 0.0007f) {
                float follow = FrameBlend(progress_.active ? 0.085f : 0.16f, dtMs);
                progressVisual_ += delta * follow;
                if (std::fabs(progressTarget_ - progressVisual_) <= 0.0007f) progressVisual_ = progressTarget_;
                progressVisual_ = Clamp01(progressVisual_);
                needsRedraw = true;
            }
        }
        if (!queueExitAnims_.empty()) {
            queueExitAnims_.erase(std::remove_if(queueExitAnims_.begin(), queueExitAnims_.end(),
                [now](const QueueExitAnim& anim) { return now - anim.started >= 260; }), queueExitAnims_.end());
            needsRedraw = true;
        }
        bool nextCaret = ((now / 530) % 2) == 0;
        if (searchFocused_ && nextCaret != caretOn_) needsRedraw = true;
        caretOn_ = nextCaret;
        if (needsRedraw) RedrawAll();
    }

    void OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) override {
        std::lock_guard<std::recursive_mutex> lock(mu_);
        BeginSplashPaint();
        rowHits_.clear();
        columnResizeHits_.clear();
        buttonHits_.clear();
        menuHits_.clear();
        dialogButtonHits_.clear();
        titleButtonHits_.clear();

        DrawBackground(rt, rc);
        if (!ShowSplashOnly()) {
            DrawTopBar(rt);
            DrawSidebar(rt);
            DrawPage(rt);
            DrawFooter(rt);
            DrawWatermark(rt);
        }
        DrawSplash(rt);
        DrawSparks(rt);
        if (dialog_.active) DrawDialog(rt);
        DrawWindowTransition(rt);
    }

    void OnMouseMove(int x, int y) override {
        float lx = Unscale(x) - kShadowPad;
        float ly = Unscale(y) - kShadowPad;
        if (resizingColumn_) {
            if ((GetKeyState(VK_LBUTTON) & 0x8000) == 0) {
                resizingColumn_ = false;
            } else {
                ResizeColumnTo(lx);
                RedrawAll();
                return;
            }
        }
        if (tableDragging_) {
            if ((GetKeyState(VK_LBUTTON) & 0x8000) == 0) {
                tableDragging_ = false;
            } else {
                int next = dragStartScroll_ + static_cast<int>(dragStartY_ - ly);
                if (std::abs(ly - dragStartY_) > 3.0f) tableDragged_ = true;
                if (currentPage_ == Page::Market) marketScroll_ = next;
                else if (currentPage_ == Page::Installed) installedScroll_ = next;
                ClampScroll();
                RedrawAll();
                return;
            }
        }
        std::wstring oldHoverButton = hoverButton_;
        int oldHoverRow = hoverRow_;
        int oldHoverNav = hoverNav_;
        int oldHoverDialogButton = hoverDialogButton_;
        bool oldHoverColumnResize = hoverColumnResize_;
        int oldHoverColumnResizeIndex = hoverColumnResizeIndex_;
        std::wstring oldHoverMenuItem = hoverMenuItem_;
        hoverButton_.clear();
        hoverMenuItem_.clear();
        hoverRow_ = -1;
        hoverNav_ = -1;
        hoverDialogButton_ = -1;
        hoverColumnResize_ = false;
        hoverColumnResizeIndex_ = -1;
        hoverColumnResizeInstalled_ = false;
        if (HamburgerRect().Contains(lx, ly)) hoverButton_ = L"sidebar_toggle";
        for (int i = 0; i < kNavCount; ++i) {
            if (NavRect(i).Contains(lx, ly)) hoverNav_ = i;
        }
        for (const auto& h : columnResizeHits_) {
            if (h.rc.Contains(lx, ly)) {
                hoverColumnResize_ = true;
                hoverColumnResizeInstalled_ = h.installed;
                hoverColumnResizeIndex_ = h.column;
                break;
            }
        }
        for (auto it = buttonHits_.rbegin(); it != buttonHits_.rend(); ++it) {
            const auto& b = *it;
            if (b.rc.Contains(lx, ly)) hoverButton_ = b.id;
        }
        for (const auto& h : menuHits_) {
            if (h.rc.Contains(lx, ly)) {
                hoverMenuItem_ = h.id;
                for (int i = 0; i < static_cast<int>(menuItems_.size()); ++i) {
                    if (menuItems_[i].id == h.id) {
                        menuHoverTargetY_ = static_cast<float>(i) * 36.0f;
                        break;
                    }
                }
                break;
            }
        }
        for (const auto& r : rowHits_) {
            if (r.rowRc.Contains(lx, ly)) hoverRow_ = r.index;
        }
        for (int i = 0; i < static_cast<int>(dialogButtonHits_.size()); ++i) {
            if (dialogButtonHits_[i].rc.Contains(lx, ly)) hoverDialogButton_ = i;
        }
        for (const auto& b : titleButtonHits_) {
            if (b.rc.Contains(lx, ly)) hoverButton_ = b.id;
        }
        if (oldHoverButton != hoverButton_ || oldHoverRow != hoverRow_ ||
            oldHoverNav != hoverNav_ || oldHoverDialogButton != hoverDialogButton_ ||
            oldHoverColumnResize != hoverColumnResize_ ||
            oldHoverColumnResizeIndex != hoverColumnResizeIndex_ ||
            oldHoverMenuItem != hoverMenuItem_) {
            SetCursor(LoadCursorW(nullptr, (hoverColumnResize_ || resizingColumn_) ? IDC_SIZEWE : IDC_ARROW));
            RedrawAll();
        }
    }

    void OnMouseLeave() override {
        hoverButton_.clear();
        hoverMenuItem_.clear();
        menuHoverAlpha_ = 0.0f;
        hoverRow_ = -1;
        hoverNav_ = -1;
        hoverDialogButton_ = -1;
        hoverColumnResize_ = false;
        hoverColumnResizeInstalled_ = false;
        hoverColumnResizeIndex_ = -1;
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        RedrawAll();
    }

    void OnLButtonDown(int x, int y) override {
        float lx = Unscale(x) - kShadowPad;
        float ly = Unscale(y) - kShadowPad;
        tableDragging_ = false;
        tableDragged_ = false;
        resizingColumn_ = false;
        for (const auto& h : columnResizeHits_) {
            if (h.rc.Contains(lx, ly)) {
                resizingColumn_ = true;
                resizingInstalled_ = h.installed;
                resizingColumnIndex_ = h.column;
                resizeStartX_ = lx;
                const auto& widths = h.installed ? installedColumnWidths_ : marketColumnWidths_;
                if (h.column >= 0 && h.column + 1 < static_cast<int>(widths.size())) {
                    resizeStartLeftW_ = widths[h.column];
                    resizeStartRightW_ = widths[h.column + 1];
                }
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return;
            }
        }
        bool interactiveRowTarget = false;
        for (const auto& h : rowHits_) {
            if (h.checkRc.Contains(lx, ly) || h.actionRc.Contains(lx, ly)) {
                interactiveRowTarget = true;
                break;
            }
        }
        if (!interactiveRowTarget && (currentPage_ == Page::Market || currentPage_ == Page::Installed) &&
            lx >= ContentX() && lx <= ContentX() + ContentW() &&
            ly >= TableHeaderBottom() && ly <= ContentBottom()) {
            tableDragging_ = true;
            dragStartY_ = ly;
            dragStartScroll_ = currentPage_ == Page::Market ? marketScroll_ : installedScroll_;
        }
    }

    void OnLButtonUp(int x, int y) override {
        float lx = Unscale(x) - kShadowPad;
        float ly = Unscale(y) - kShadowPad;
        {
            std::lock_guard<std::recursive_mutex> lock(mu_);
            bool wasDraggingTable = tableDragging_;
            bool wasDraggedTable = tableDragged_;
            tableDragging_ = false;
            tableDragged_ = false;
            if (HamburgerRect().Contains(lx, ly)) {
                ToggleSidebar();
                statusPanelOpen_ = false;
                AddClickSparks(lx, ly, Palette::Primary);
                RedrawAll();
                return;
            }
            if (resizingColumn_) {
                resizingColumn_ = false;
                RedrawAll();
                return;
            }
            if (wasDraggingTable && wasDraggedTable) {
                RedrawAll();
                return;
            }
            if (dialog_.active) {
                for (int i = 0; i < static_cast<int>(dialogButtonHits_.size()); ++i) {
                    if (dialogButtonHits_[i].rc.Contains(lx, ly)) {
                        std::wstring response = dialogButtonHits_[i].id;
                        std::wstring id = dialog_.id;
                        dialog_.active = false;
                        client_.SendCommand("dialog_response",
                            L"{\"id\":\"" + EscapeJson(id) + L"\",\"response\":\"" + EscapeJson(response) + L"\"}");
                        RedrawAll();
                        return;
                    }
                }
            }

            for (int i = 0; i < kNavCount; ++i) {
                if (NavRect(i).Contains(lx, ly)) {
                    AddClickSparks(lx, ly, Palette::Primary);
                    SwitchPage(static_cast<Page>(i));
                    RedrawAll();
                    return;
                }
            }

            if (!activeMenu_.empty()) {
                bool hitMenu = false;
                for (const auto& h : menuHits_) {
                    if (h.rc.Contains(lx, ly)) {
                        SelectMenuItem(activeMenu_, h.id);
                        hitMenu = true;
                        break;
                    }
                }
                if (!hitMenu) CloseMenu();
                RedrawAll();
                return;
            }

            for (const auto& b : titleButtonHits_) {
                if (b.rc.Contains(lx, ly)) {
                    if (b.id == L"window_close") {
                        StartWindowAnimation(2);
                    } else if (b.id == L"window_min") {
                        StartWindowAnimation(1);
                    }
                    return;
                }
            }

            for (const auto& h : rowHits_) {
                if (h.checkRc.Contains(lx, ly)) {
                    ToggleRowSelection(h);
                    RedrawAll();
                    return;
                }
                if (h.actionRc.Contains(lx, ly)) {
                    RunRowAction(h);
                    RedrawAll();
                    return;
                }
                if (!h.installed && h.rowRc.Contains(lx, ly)) {
                    if (IsMarketRowDoubleClick(h.index)) {
                        OpenVersionHistory(h.index);
                        RedrawAll();
                        return;
                    }
                    return;
                }
            }

            for (const auto& b : buttonHits_) {
                if (b.rc.Contains(lx, ly)) {
                    AddClickSparks(lx, ly, b.id == L"batch_install" || b.id == L"batch_uninstall" || b.id == L"search" ? Palette::Primary : Palette::Info);
                    HandleButton(b.id);
                    RedrawAll();
                    return;
                }
            }
            if (statusPanelOpen_ && lx >= ContentX() && lx <= ContentX() + ContentW() &&
                ly >= StatusPanelY() && ly <= BaseH() - kFooterH - 10) {
                RedrawAll();
                return;
            } else if (statusPanelOpen_) {
                statusPanelOpen_ = false;
                statusPanelToggledAt_ = GetTickCount64();
                RedrawAll();
                return;
            }
        }
    }

    void OnMouseWheel(int x, int, int delta) override {
        std::lock_guard<std::recursive_mutex> lock(mu_);
        if (statusPanelOpen_) {
            statusPanelScroll_ -= delta / 4;
            if (statusPanelScroll_ < 0) statusPanelScroll_ = 0;
        } else if (currentPage_ == Page::Market) {
            marketScroll_ -= delta / 4;
            ClampScroll();
        } else if (currentPage_ == Page::Installed) {
            installedScroll_ -= delta / 4;
            ClampScroll();
        } else if (currentPage_ == Page::Versions) {
            float lx = Unscale(x) - kShadowPad;
            float gap = 16.0f;
            float leftW = ContentW() * 0.62f - gap;
            float rightX = ContentX() + leftW + gap;
            if (lx >= rightX) queueScroll_ -= delta / 4;
            else versionScroll_ -= delta / 4;
            if (versionScroll_ < 0) versionScroll_ = 0;
            if (queueScroll_ < 0) queueScroll_ = 0;
        } else {
            logScroll_ -= delta / 4;
            if (logScroll_ < 0) logScroll_ = 0;
        }
        RedrawAll();
    }

    void OnKeyDown(UINT vk) override {
        std::lock_guard<std::recursive_mutex> lock(mu_);
        if (!activeMenu_.empty()) {
            if (vk == VK_ESCAPE) {
                CloseMenu();
                RedrawAll();
                return;
            }
            if (vk == VK_UP) {
                MoveMenuFocus(-1);
                RedrawAll();
                return;
            }
            if (vk == VK_DOWN) {
                MoveMenuFocus(1);
                RedrawAll();
                return;
            }
            if (vk == VK_RETURN) {
                if (menuFocusIndex_ >= 0 && menuFocusIndex_ < static_cast<int>(menuItems_.size())) {
                    SelectMenuItem(activeMenu_, menuItems_[menuFocusIndex_].id);
                    RedrawAll();
                    return;
                }
            }
        }
        if (searchFocused_) {
            bool handled = true;
            if (vk == VK_BACK) {
                if (searchCaretIndex_ > 0 && !search_.empty()) {
                    search_.erase(searchCaretIndex_ - 1, 1);
                    --searchCaretIndex_;
                    ApplySearch();
                    EnsureSearchCaretVisible(288.0f);
                }
            } else if (vk == VK_DELETE) {
                if (searchCaretIndex_ < search_.size()) {
                    search_.erase(searchCaretIndex_, 1);
                    ApplySearch();
                    EnsureSearchCaretVisible(288.0f);
                }
            } else if (vk == VK_LEFT) {
                if (searchCaretIndex_ > 0) --searchCaretIndex_;
                EnsureSearchCaretVisible(288.0f);
            } else if (vk == VK_RIGHT) {
                if (searchCaretIndex_ < search_.size()) ++searchCaretIndex_;
                EnsureSearchCaretVisible(288.0f);
            } else if (vk == VK_HOME) {
                searchCaretIndex_ = 0;
                EnsureSearchCaretVisible(288.0f);
            } else if (vk == VK_END) {
                searchCaretIndex_ = search_.size();
                EnsureSearchCaretVisible(288.0f);
            } else if ((GetKeyState(VK_CONTROL) & 0x8000) && vk == 'V') {
                PasteSearchText();
            } else if (vk == VK_RETURN) {
                HandleButton(L"search");
            } else if ((GetKeyState(VK_CONTROL) & 0x8000) && vk == 'A') {
                searchCaretIndex_ = search_.size();
                EnsureSearchCaretVisible(288.0f);
            } else if (vk == VK_ESCAPE) {
                searchFocused_ = false;
            } else {
                handled = false;
            }
            if (handled) RedrawAll();
        }
    }

    void OnChar(wchar_t ch) override {
        std::lock_guard<std::recursive_mutex> lock(mu_);
        if (searchFocused_ && ch >= 32 && ch != 127) {
            InsertSearchText(std::wstring(1, ch));
            RedrawAll();
        }
    }

    bool ShouldStartWindowDrag(int x, int y) const {
        float lx = static_cast<float>(x) / userScale_ - kShadowPad;
        float ly = static_cast<float>(y) / userScale_ - kShadowPad;
        return ly >= 0.0f && ly <= static_cast<float>(kTitleH) && lx >= 0.0f && lx < BaseW() - 96.0f;
    }

private:
    int Scale(int value) const {
        return static_cast<int>(value * userScale_ + 0.5f);
    }

    float ScaleF(float value) const {
        return value * userScale_;
    }

    float Unscale(int value) const {
        return static_cast<float>(value) / userScale_;
    }

    float ClampScale(float scale) const {
        if (scale < 1.0f) scale = 1.0f;
        if (scale > 3.0f) scale = 3.0f;
        return scale;
    }

    float SplashOverlayAlpha() const {
        ULONGLONG start = SplashClockStart();
        if (start == 0) return 0.0f;
        ULONGLONG now = GetTickCount64();
        if (!splashDismissed_) {
            float intro = Clamp01(static_cast<float>(now - start) / kSplashIntroMs);
            return kSplashMinAlpha + (0.985f - kSplashMinAlpha) * EaseOutCubic(intro);
        }
        if (splashRevealAt_ == 0) return 0.0f;
        float outro = Clamp01(static_cast<float>(now - splashRevealAt_) / kSplashOutroMs);
        return (1.0f - EaseOutCubic(outro)) * 0.98f;
    }

    float SplashSidebarReveal() const {
        if (SplashClockStart() == 0 || (splashDismissed_ && splashRevealAt_ == 0)) return 1.0f;
        if (!splashDismissed_ || splashRevealAt_ == 0) return 0.0f;
        float t = Clamp01(static_cast<float>(GetTickCount64() - splashRevealAt_) / kSplashOutroMs);
        return EaseOutCubic(t);
    }

    ULONGLONG SplashClockStart() const {
        return splashFirstPaintAt_ != 0 ? splashFirstPaintAt_ : splashStartedAt_;
    }

    void BeginSplashPaint() {
        if (splashDismissed_ || splashFirstPaintAt_ != 0) return;
        splashFirstPaintAt_ = GetTickCount64();
        splashStartedAt_ = splashFirstPaintAt_;
    }

    float WindowAnimProgress() const {
        if (windowAnimMode_ == 0 || windowAnimStartedAt_ == 0) return 0.0f;
        float t = Clamp01(static_cast<float>(GetTickCount64() - windowAnimStartedAt_) / 120.0f);
        return EaseOutCubic(t);
    }

    float SidebarAnimValue(ULONGLONG now) const {
        if (sidebarAnimStartedAt_ == 0) return sidebarAnim_;
        float t = Clamp01(static_cast<float>(now - sidebarAnimStartedAt_) / kSidebarAnimMs);
        return sidebarAnimStart_ + (sidebarAnimTarget_ - sidebarAnimStart_) * EaseOutCubic(t);
    }

    void StepSidebarAnimation(ULONGLONG now, bool& needsRedraw) {
        if (sidebarAnimStartedAt_ == 0) return;
        float t = Clamp01(static_cast<float>(now - sidebarAnimStartedAt_) / kSidebarAnimMs);
        sidebarAnim_ = sidebarAnimStart_ + (sidebarAnimTarget_ - sidebarAnimStart_) * EaseOutCubic(t);
        needsRedraw = true;
        if (t >= 1.0f) {
            sidebarAnim_ = sidebarAnimTarget_;
            sidebarAnimStart_ = sidebarAnimTarget_;
            sidebarAnimStartedAt_ = 0;
        }
    }

    void StepThemeTransition(ULONGLONG now, bool& needsRedraw) {
        if (!themeTransitionActive_) return;
        float t = Clamp01(static_cast<float>(now - themeTransitionStartAt_) / kThemeTransitionMs);
        float eased = EaseInOutCubic(t);
        ApplyPaletteSnapshot(MixPaletteSnapshot(themeFrom_, themeTo_, eased));
        ApplyMiniGpmTheme();
        needsRedraw = true;
        if (t >= 1.0f) {
            ApplyPaletteSnapshot(themeTo_);
            ApplyMiniGpmTheme();
            themeTransitionActive_ = false;
        }
    }

    void StepMenuAnimation(float dtMs, bool& needsRedraw) {
        if (activeMenu_.empty() && menuOpenProgress_ <= 0.0f) {
            menuClosing_ = false;
            return;
        }
        float target = menuClosing_ ? 0.0f : 1.0f;
        float delta = target - menuOpenProgress_;
        if (std::fabs(delta) > 0.001f) {
            menuOpenProgress_ += delta * FrameBlend(0.42f, dtMs);
            if (std::fabs(target - menuOpenProgress_) <= 0.001f) menuOpenProgress_ = target;
            needsRedraw = true;
        }
        if (menuClosing_ && menuOpenProgress_ <= 0.001f) {
            menuClosing_ = false;
            activeMenu_.clear();
            menuItems_.clear();
            menuFocusIndex_ = -1;
            hoverMenuItem_.clear();
        }
        if (!menuHoverInitialized_) {
            menuHoverY_ = menuHoverTargetY_;
            menuHoverInitialized_ = true;
        }
        float hoverDelta = menuHoverTargetY_ - menuHoverY_;
        if (std::fabs(hoverDelta) > 0.001f) {
            menuHoverY_ += hoverDelta * FrameBlend(0.52f, dtMs);
            if (std::fabs(menuHoverTargetY_ - menuHoverY_) <= 0.001f) menuHoverY_ = menuHoverTargetY_;
            needsRedraw = true;
        }
        float hoverAlphaTarget = (hoverMenuItem_.empty() && menuFocusIndex_ < 0) ? 0.0f : 1.0f;
        float hoverAlphaDelta = hoverAlphaTarget - menuHoverAlpha_;
        if (std::fabs(hoverAlphaDelta) > 0.001f) {
            menuHoverAlpha_ += hoverAlphaDelta * FrameBlend(0.38f, dtMs);
            if (std::fabs(hoverAlphaTarget - menuHoverAlpha_) <= 0.001f) menuHoverAlpha_ = hoverAlphaTarget;
            needsRedraw = true;
        }
    }

    void ToggleSidebar() {
        ULONGLONG now = GetTickCount64();
        sidebarAnim_ = SidebarAnimValue(now);
        sidebarCollapsed_ = !sidebarCollapsed_;
        sidebarAnimStart_ = sidebarAnim_;
        sidebarAnimTarget_ = sidebarCollapsed_ ? 0.0f : 1.0f;
        sidebarAnimStartedAt_ = now;
        if (std::fabs(sidebarAnimTarget_ - sidebarAnimStart_) <= 0.001f) {
            sidebarAnim_ = sidebarAnimTarget_;
            sidebarAnimStart_ = sidebarAnimTarget_;
            sidebarAnimStartedAt_ = 0;
        }
    }

    void StartWindowAnimation(int mode) {
        if (!m_hWnd || windowAnimMode_ != 0) return;
        windowAnimMode_ = mode;
        windowAnimStartedAt_ = GetTickCount64();
        RedrawAll();
    }

    void FinishWindowAnimation() {
        if (!m_hWnd || windowAnimMode_ == 0) return;
        int mode = windowAnimMode_;
        windowAnimMode_ = 0;
        windowAnimStartedAt_ = 0;
        if (mode == 1) {
            ShowWindow(m_hWnd, SW_MINIMIZE);
            return;
        }
        if (mode == 2) PostMessageW(m_hWnd, WM_CLOSE, 0, 0);
    }

    bool ShowSplashOnly() const {
        return splashStartedAt_ != 0 && !splashDismissed_;
    }

    int BaseW() const { return kBaseW; }
    int BaseH() const { return kBaseH; }
    int WindowW() const { return kBaseW + kShadowPad * 2; }
    int WindowH() const { return kBaseH + kShadowPad * 2; }
    float SidebarW() const { return static_cast<float>(kSidebarCollapsedW) + (kSidebarExpandedW - kSidebarCollapsedW) * sidebarAnim_; }
    float ContentX() const { return SidebarW() + 28.0f; }
    float ContentW() const { return static_cast<float>(kBaseW) - SidebarW() - 56.0f; }
    float ContentBottom() const { return static_cast<float>(kBaseH - kFooterH - 18); }
    float TableTop() const { return currentPage_ == Page::Market ? 208.0f : 184.0f; }
    float TableHeaderBottom() const { return TableTop() + 42.0f; }
    float StatusPanelY() const { return static_cast<float>(kBaseH - kFooterH - 268); }

    RectF NavRect(int idx) const {
        static constexpr float y0 = 136.0f;
        float w = SidebarW();
        return { 12.0f, y0 + idx * 48.0f, (std::max)(40.0f, w - 24.0f), 38.0f };
    }

    RectF HamburgerRect() const {
        return { 14.0f, 36.0f, 34.0f, 30.0f };
    }

    void RedrawAll() {
        if (!m_parentWnd) return;
        HWND hwnd = m_parentWnd->GetWindowHandle();
        if (hwnd && IsWindow(hwnd)) {
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    void PasteSearchText() {
        if (!OpenClipboard(m_hWnd)) return;
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            const wchar_t* ptr = static_cast<const wchar_t*>(GlobalLock(h));
            if (ptr) {
                std::wstring text = ptr;
                text.erase(std::remove(text.begin(), text.end(), L'\r'), text.end());
                std::replace(text.begin(), text.end(), L'\n', L' ');
                GlobalUnlock(h);
                InsertSearchText(text);
            }
        }
        CloseClipboard();
    }

    void ResizeWindowForScale(bool recenter) {
        if (!m_hWnd) return;
        int w = Scale(kBaseW + kShadowPad * 2);
        int h = Scale(kBaseH + kShadowPad * 2);
        m_width = w;
        m_height = h;
        SetRect(0, 0, w, h, false);
        ClampWindowToWorkArea(m_hWnd, w, h, recenter);
        RedrawAll();
    }

    void SetScale(const std::wstring& label) {
        int pct = 100;
        try {
            pct = std::stoi(label);
        } catch (...) {
            pct = 100;
        }
        if (pct < 100) pct = 100;
        if (pct > 300) pct = 300;
        userScale_ = pct / 100.0f;
        chosenScale_ = userScale_;
        m_width = Scale(kBaseW + kShadowPad * 2);
        m_height = Scale(kBaseH + kShadowPad * 2);
        watermarkBitmap_.Reset();
        watermarkRenderTarget_ = nullptr;
        watermarkLoadFailed_ = false;
        splashDismissed_ = true;
        splashRevealAt_ = 0;
        CloseMenu();
        SaveConfig();
        AddLog(L"INFO", L"DPI scale changed to " + std::to_wstring(pct) + L"%");
        ResizeWindowForScale(false);
    }

    void SwitchPage(Page next) {
        if (currentPage_ == next) return;
        previousPage_ = currentPage_;
        int previousIndex = static_cast<int>(currentPage_);
        int nextIndex = static_cast<int>(next);
        navDirection_ = nextIndex > previousIndex ? 1.0f : -1.0f;
        navIndicatorFrom_ = navAnim_;
        navIndicatorTo_ = static_cast<float>(nextIndex);
        navIndicatorStartAt_ = GetTickCount64();
        currentPage_ = next;
        pageSwitchAt_ = GetTickCount64();
        searchFocused_ = false;
        CloseMenu();
        tableDragging_ = false;
        statusPanelOpen_ = false;
    }

    bool StartBackend() {
        std::wstring dir = GetExeDir();
        wchar_t fullPath[MAX_PATH] = {};
        struct Candidate { std::wstring path; std::wstring args; };
        std::vector<Candidate> candidates = {
            { dir + L"\\..\\Gpm\\gpm-gui.exe", L"--gui --port 8080 --no-gui-spawn" },
            { dir + L"\\..\\Gpm\\gpm.exe", L"--gui --port 8080 --no-gui-spawn" },
            { dir + L"\\gpm-gui.exe", L"--gui --port 8080 --no-gui-spawn" },
            { dir + L"\\gpm.exe", L"--gui --port 8080 --no-gui-spawn" },
            { dir + L"\\..\\reference\\Gpm\\gpm-gui.exe", L"--gui --port 8080 --no-gui-spawn" },
            { dir + L"\\..\\reference\\Gpm\\gpm.exe", L"--gui --port 8080 --no-gui-spawn" },
            { dir + L"\\gpm-debug.exe", L"--gui --port 8080 --no-gui-spawn" },
            { dir + L"\\..\\Gpm\\gpm-debug.exe", L"--gui --port 8080 --no-gui-spawn" }
        };
        for (auto c : candidates) {
            if (GetFullPathNameW(c.path.c_str(), MAX_PATH, fullPath, nullptr) > 0) c.path = fullPath;
            if (!FileExists(c.path)) continue;
            HINSTANCE result = ShellExecuteW(nullptr, L"open", c.path.c_str(), c.args.c_str(), dir.c_str(), SW_HIDE);
            if (reinterpret_cast<INT_PTR>(result) > 32) {
                AddLog(L"INFO", L"Backend started: " + c.path);
                backendLaunchRequested_ = true;
                return true;
            }
        }
        AddLog(L"WARN", L"Backend executable not found; waiting for localhost:8080");
        AddToast(Tr(L"label.backend", L"Backend"), Tr(L"toast.backendStart", L"Start gpm.exe --gui on localhost:8080"), L"warning");
        return false;
    }

    void AddLog(const std::wstring& level, const std::wstring& message, const std::wstring& time = L"") {
        if (message.empty()) return;
        logs_.push_back({ level, message, time.empty() ? NowTime() : time });
        if (logs_.size() > 100) logs_.erase(logs_.begin());
    }

    void AddToast(const std::wstring& title, const std::wstring& message, const std::wstring& severity) {
        if (message.empty()) return;
        ULONGLONG now = GetTickCount64();
        std::wstring normalizedSeverity = SeverityText(severity);
        if (normalizedSeverity == L"info" || normalizedSeverity == L"success") {
            statusLine_ = message;
            return;
        }
        for (auto& toast : toasts_) {
            if (toast.title == (title.empty() ? L"GPM" : title) &&
                toast.message == message &&
                toast.severity == normalizedSeverity &&
                now - toast.created < 2200) {
                toast.created = now;
                toast.time = NowTime();
                return;
            }
        }
        toasts_.push_back({ title.empty() ? L"GPM" : title, message, normalizedSeverity, now, NowTime() });
        statusLine_ = message;
        if (toasts_.size() > 80) toasts_.erase(toasts_.begin(), toasts_.end() - 80);
    }

    void PruneToasts() {
        if (toasts_.size() > 80) toasts_.erase(toasts_.begin(), toasts_.end() - 80);
    }

    void AddClickSparks(float x, float y, COLORREF color) {
        ULONGLONG now = GetTickCount64();
        static const float dirs[][2] = {
            { 1.0f, 0.0f }, { 0.55f, 0.85f }, { -0.55f, 0.85f },
            { -1.0f, 0.0f }, { -0.55f, -0.85f }, { 0.55f, -0.85f }
        };
        for (auto& d : dirs) {
            sparks_.push_back({ x, y, d[0] * 1.35f, d[1] * 1.35f, now, color });
        }
        if (sparks_.size() > 48) sparks_.erase(sparks_.begin(), sparks_.end() - 48);
    }

    void PruneSparks() {
        ULONGLONG now = GetTickCount64();
        sparks_.erase(std::remove_if(sparks_.begin(), sparks_.end(), [now](const Spark& s) {
            return now - s.created > 460;
        }), sparks_.end());
    }

    std::wstring Tr(const std::wstring& key, const std::wstring& fallback) const {
        auto it = lang_.find(key);
        return it == lang_.end() || it->second.empty() ? fallback : it->second;
    }

    void EnsureDefaultConfigFiles() {
        if (exeDir_.empty()) return;
        std::wstring themeDir = exeDir_ + L"\\themes";
        std::wstring langDir = exeDir_ + L"\\lang";
        CreateDirectoryW(themeDir.c_str(), nullptr);
        CreateDirectoryW(langDir.c_str(), nullptr);
        if (!FileExists(themeDir + L"\\default.json")) {
            WriteUtf8File(themeDir + L"\\default.json",
                L"{\n"
                L"  \"name\": \"Default Blue\",\n"
                L"  \"dark\": false,\n"
                L"  \"primary\": \"rgb(44, 94, 173)\",\n"
                L"  \"primaryHover\": \"rgb(21, 145, 220)\",\n"
                L"  \"info\": \"rgb(75, 184, 250)\",\n"
                L"  \"primarySoft\": \"rgb(196, 226, 245)\",\n"
                L"  \"window\": \"#F5F8FB\",\n"
                L"  \"surface\": \"#FFFFFF\",\n"
                L"  \"surfaceAlt\": \"#EEF4F9\",\n"
                L"  \"sidebar\": \"#242D3A\",\n"
                L"  \"sidebarHover\": \"#303D4F\",\n"
                L"  \"text\": \"#1C222C\",\n"
                L"  \"textSoft\": \"#4A5667\",\n"
                L"  \"textMuted\": \"#778496\",\n"
                L"  \"border\": \"#D6E1EC\",\n"
                L"  \"borderStrong\": \"#B2C4D8\",\n"
                L"  \"progressTrack\": \"#E0E9F2\",\n"
                L"  \"rowHover\": \"#F7FBFE\",\n"
                L"  \"rowBorder\": \"#E5EDF5\"\n"
                L"}\n");
        }
        if (!FileExists(themeDir + L"\\dark.json")) {
            WriteUtf8File(themeDir + L"\\dark.json",
                L"{\n"
                L"  \"name\": \"Default Dark\",\n"
                L"  \"dark\": true,\n"
                L"  \"primary\": \"rgb(44, 94, 173)\",\n"
                L"  \"primaryHover\": \"rgb(21, 145, 220)\",\n"
                L"  \"info\": \"rgb(75, 184, 250)\",\n"
                L"  \"primarySoft\": \"#274865\"\n"
                L"}\n");
        }
        if (!FileExists(langDir + L"\\en-US.json")) {
            WriteUtf8File(langDir + L"\\en-US.json", DefaultLangEn());
        }
        if (!FileExists(langDir + L"\\zh-CN.json")) {
            WriteUtf8File(langDir + L"\\zh-CN.json", DefaultLangZh());
        }
        if (!FileExists(exeDir_ + L"\\config.json")) {
            WriteUtf8File(exeDir_ + L"\\config.json",
                L"{\n"
                L"  \"dpi\": " + std::to_wstring(DetectAutoDpiPercent()) + L",\n"
                L"  \"theme\": \"default\",\n"
                L"  \"language\": \"auto\"\n"
                L"}\n");
        }
    }

    std::wstring DefaultLangEn() const {
        return
            L"{\n"
            L"  \"nav.market\":\"Market\", \"nav.installed\":\"Installed\", \"nav.versions\":\"Versions\", \"nav.settings\":\"Settings\", \"nav.about\":\"About\",\n"
            L"  \"state.connected\":\"Connected\", \"state.waiting\":\"Waiting backend\", \"state.idle\":\"Idle\", \"app.title\":\"GPM\",\n"
            L"  \"page.market\":\"Market\", \"page.market.subtitle\":\"{count} packages available\", \"page.installed\":\"Installed\", \"page.installed.subtitle\":\"{count} packages installed\", \"page.versions\":\"Version Management\", \"page.versions.subtitle\":\"Click a package header to expand or collapse its versions\",\n"
            L"  \"page.settings\":\"Settings\", \"page.settings.subtitle\":\"DPI scale, language and theme\", \"page.about\":\"About\", \"page.about.subtitle\":\"Windows package management frontend\",\n"
            L"  \"action.refresh\":\"Refresh\", \"action.updateIndex\":\"Update Index\", \"action.search\":\"Search\", \"action.batchInstall\":\"Batch Install\", \"action.batchUninstall\":\"Batch Uninstall\", \"action.addToQueue\":\"Add\", \"action.addSelected\":\"Add selected\", \"action.clearSelection\":\"Clear selection\", \"action.installQueue\":\"Install Queue\", \"action.removeSelected\":\"Remove selected\", \"action.clearQueue\":\"Clear\", \"action.exportLogs\":\"Export logs\",\n"
            L"  \"action.install\":\"Install\", \"action.reinstall\":\"Reinstall\", \"action.upgrade\":\"Upgrade\", \"action.uninstall\":\"Uninstall\", \"action.details\":\"Details\", \"action.collapse\":\"Collapse\", \"action.cancelTask\":\"Cancel\",\n"
            L"  \"label.available\":\"Available\", \"label.installed\":\"Installed\", \"label.updates\":\"Updates\", \"label.status\":\"Status\", \"label.category\":\"Category\", \"label.packages\":\"Packages\", \"label.packageCount\":\"{count} packages\", \"label.selectedCount\":\"{count} selected\", \"label.quickActions\":\"Quick actions\", \"label.dpi\":\"DPI Scale\", \"label.theme\":\"Theme\", \"label.language\":\"Language\", \"label.history\":\"Version history\", \"label.pendingInstall\":\"Pending install\", \"label.historyCount\":\"{count} versions\", \"label.queueCount\":\"{count} queued\",\n"
            L"  \"hint.search\":\"Search packages...\", \"hint.dpi\":\"The window is physically resized when DPI changes. Range: 100% to 300%.\", \"hint.theme\":\"Theme files are loaded from the themes folder.\", \"hint.language\":\"Language files are loaded from the lang folder.\",\n"
            L"  \"text.backendReady\":\"Backend connected. Package data is ready to refresh or update.\", \"text.backendWaiting\":\"Waiting for backend on 127.0.0.1:8080.\", \"text.quickActions\":\"Use Market for batch install and Installed for batch uninstall. Logs are hidden in release builds.\",\n"
            L"  \"empty.packages\":\"No packages to display\", \"empty.installed\":\"No installed packages\", \"empty.logs\":\"No server logs yet\", \"empty.warnings\":\"No warnings or errors\", \"empty.queue\":\"No versions selected\", \"empty.history\":\"No package selected\",\n"
            L"  \"table.name\":\"Name\", \"table.version\":\"Version\", \"table.author\":\"Author\", \"table.category\":\"Category\", \"table.status\":\"Status\", \"table.size\":\"Size\", \"table.action\":\"Action\", \"table.installDate\":\"Install Date\",\n"
            L"  \"category.all\":\"All\", \"category.uncategorized\":\"Uncategorized\",\n"
            L"  \"status.new\":\"New\", \"status.installed\":\"Installed\", \"status.upgrade\":\"Upgrade\", \"status.downgrade\":\"Downgrade\",\n"
            L"  \"panel.warnings\":\"Warnings and errors\", \"panel.warnings.subtitle\":\"Recent backend warnings and failed operations stay here instead of popping over the UI.\", \"footer.counts\":\"{warn} warnings  {error} errors\",\n"
            L"  \"progress.idle\":\"No active operation\", \"progress.stage.idle\":\"Ready\", \"progress.stage.download\":\"Downloading\", \"progress.stage.install\":\"Installing\", \"progress.stage.update\":\"Updating index\", \"progress.stage.verify\":\"Verifying\", \"progress.stage.done\":\"Completed\", \"progress.stage.error\":\"Failed\", \"progress.stage.cancel\":\"Cancelled\", \"progress.threads\":\"{count} threads\", \"progress.speed\":\"{speed}/s\",\n"
            L"  \"about.title\":\"GPM\", \"about.developer\":\"Developer: ArthurX\", \"about.body\":\"Windows package management frontend.\",\n"
            L"  \"splash.title\":\"GPM\", \"splash.body\":\"Windows package management, redrawn for a cleaner desktop flow.\",\n"
            L"  \"toast.index\":\"Index\", \"toast.indexLoaded\":\"Package index loaded\", \"toast.indexUpdating\":\"Updating package index\", \"toast.operationComplete\":\"Operation complete\", \"toast.operationFailed\":\"Operation failed\", \"toast.operationCancelled\":\"Operation cancelled\", \"toast.cancelRequested\":\"Cancellation requested\", \"toast.backendDisconnected\":\"Disconnected; reconnecting...\", \"toast.backendStart\":\"Start gpm.exe --gui on localhost:8080\", \"toast.refreshRequested\":\"Requested package data\", \"toast.selection\":\"Selection\", \"toast.selectPackages\":\"Select packages in the first column first\", \"toast.selectInstalled\":\"Select installed packages first\", \"toast.selectVersions\":\"Select versions first\", \"toast.selectQueue\":\"Select queue items first\", \"toast.selectionCleared\":\"Selection cleared\", \"toast.packagesQueued\":\"{count} packages queued\", \"toast.versionsQueued\":\"{count} versions added to queue\", \"toast.queueQueued\":\"{count} versions queued\", \"toast.queueRemoved\":\"{count} queue items removed\", \"toast.queueCleared\":\"Queue cleared\", \"toast.showingAll\":\"Showing all packages\"\n"
            L"}\n";
    }

    std::wstring DefaultLangZh() const {
        return
            L"{\n"
            L"  \"nav.market\":\"市场\", \"nav.installed\":\"已安装\", \"nav.versions\":\"版本管理\", \"nav.settings\":\"设置\", \"nav.about\":\"关于\",\n"
            L"  \"state.connected\":\"已连接\", \"state.waiting\":\"等待后端\", \"state.idle\":\"空闲\", \"app.title\":\"GPM\",\n"
            L"  \"page.market\":\"市场\", \"page.market.subtitle\":\"共 {count} 个可用包\", \"page.installed\":\"已安装\", \"page.installed.subtitle\":\"已安装 {count} 个包\", \"page.versions\":\"版本管理\", \"page.versions.subtitle\":\"点击软件包标题可展开或折叠历史版本\",\n"
            L"  \"page.settings\":\"设置\", \"page.settings.subtitle\":\"DPI、语言和主题\", \"page.about\":\"关于\", \"page.about.subtitle\":\"Windows 包管理前端\",\n"
            L"  \"action.refresh\":\"刷新\", \"action.updateIndex\":\"更新索引\", \"action.search\":\"搜索\", \"action.batchInstall\":\"批量安装\", \"action.batchUninstall\":\"批量卸载\", \"action.addToQueue\":\"加入\", \"action.addSelected\":\"加入所选\", \"action.clearSelection\":\"取消选择\", \"action.installQueue\":\"安装队列\", \"action.removeSelected\":\"删除所选\", \"action.clearQueue\":\"清空\", \"action.exportLogs\":\"导出日志\",\n"
            L"  \"action.install\":\"安装\", \"action.reinstall\":\"重装\", \"action.upgrade\":\"升级\", \"action.uninstall\":\"卸载\", \"action.details\":\"详情\", \"action.collapse\":\"收起\", \"action.cancelTask\":\"打断\",\n"
            L"  \"label.available\":\"可用\", \"label.installed\":\"已安装\", \"label.updates\":\"更新\", \"label.status\":\"状态\", \"label.category\":\"分类\", \"label.packages\":\"软件包\", \"label.packageCount\":\"共 {count} 个包\", \"label.selectedCount\":\"已选 {count} 个\", \"label.quickActions\":\"快捷操作\", \"label.dpi\":\"DPI 缩放\", \"label.theme\":\"主题\", \"label.language\":\"语言\", \"label.history\":\"版本历史\", \"label.pendingInstall\":\"待安装\", \"label.historyCount\":\"{count} 个版本\", \"label.queueCount\":\"队列 {count} 个\",\n"
            L"  \"hint.search\":\"搜索软件包...\", \"hint.dpi\":\"切换 DPI 时窗口会物理改变大小。范围：100% 到 300%。\", \"hint.theme\":\"主题文件从 themes 文件夹读取。\", \"hint.language\":\"语言文件从 lang 文件夹读取。\",\n"
            L"  \"text.backendReady\":\"后端已连接，可以刷新或更新软件包数据。\", \"text.backendWaiting\":\"正在等待 127.0.0.1:8080 后端。\", \"text.quickActions\":\"在市场页批量安装，在已安装页批量卸载。Release 版本首页隐藏日志。\",\n"
            L"  \"empty.packages\":\"没有可显示的软件包\", \"empty.installed\":\"没有已安装的软件包\", \"empty.logs\":\"暂无服务日志\", \"empty.warnings\":\"没有警告或错误\", \"empty.queue\":\"还没有选择版本\", \"empty.history\":\"尚未选择软件包\",\n"
            L"  \"table.name\":\"名称\", \"table.version\":\"版本\", \"table.author\":\"作者\", \"table.category\":\"分类\", \"table.status\":\"状态\", \"table.size\":\"大小\", \"table.action\":\"操作\", \"table.installDate\":\"安装日期\",\n"
            L"  \"category.all\":\"全部\", \"category.uncategorized\":\"未分类\",\n"
            L"  \"status.new\":\"新包\", \"status.installed\":\"已安装\", \"status.upgrade\":\"可升级\", \"status.downgrade\":\"可降级\",\n"
            L"  \"panel.warnings\":\"警告和错误\", \"panel.warnings.subtitle\":\"后端警告和失败操作会留在这里，不再弹到界面上方。\", \"footer.counts\":\"{warn} 个警告  {error} 个错误\",\n"
            L"  \"progress.idle\":\"当前没有任务\", \"progress.stage.idle\":\"就绪\", \"progress.stage.download\":\"正在下载\", \"progress.stage.install\":\"正在安装\", \"progress.stage.update\":\"正在更新索引\", \"progress.stage.verify\":\"正在校验\", \"progress.stage.done\":\"已完成\", \"progress.stage.error\":\"失败\", \"progress.stage.cancel\":\"已打断\", \"progress.threads\":\"{count} 线程\", \"progress.speed\":\"{speed}/秒\",\n"
            L"  \"about.title\":\"GPM\", \"about.developer\":\"开发者：ArthurX\", \"about.body\":\"Windows 包管理前端。\",\n"
            L"  \"splash.title\":\"GPM\", \"splash.body\":\"Windows 包管理，重新绘制为更干净的桌面工作流。\",\n"
            L"  \"toast.index\":\"索引\", \"toast.indexLoaded\":\"软件包索引已加载\", \"toast.indexUpdating\":\"正在更新软件包索引\", \"toast.operationComplete\":\"操作完成\", \"toast.operationFailed\":\"操作失败\", \"toast.operationCancelled\":\"操作已打断\", \"toast.cancelRequested\":\"已请求打断\", \"toast.backendDisconnected\":\"后端已断开，正在重连...\", \"toast.backendStart\":\"请在 localhost:8080 启动 gpm.exe --gui\", \"toast.refreshRequested\":\"已请求软件包数据\", \"toast.selection\":\"选择\", \"toast.selectPackages\":\"请先在第一列选择软件包\", \"toast.selectInstalled\":\"请先选择已安装的软件包\", \"toast.selectVersions\":\"请先选择版本\", \"toast.selectQueue\":\"请先选择队列项\", \"toast.selectionCleared\":\"已取消选择\", \"toast.packagesQueued\":\"已加入 {count} 个软件包\", \"toast.versionsQueued\":\"已加入 {count} 个版本到队列\", \"toast.queueQueued\":\"已加入 {count} 个版本\", \"toast.queueRemoved\":\"已删除 {count} 个队列项\", \"toast.queueCleared\":\"队列已清空\", \"toast.showingAll\":\"显示全部软件包\"\n"
            L"}\n";
    }

    void LoadConfig() {
        themeFiles_ = ListJsonStems(exeDir_ + L"\\themes");
        langFiles_ = ListJsonStems(exeDir_ + L"\\lang");
        JsonValue config = JsonParser(ReadUtf8File(exeDir_ + L"\\config.json")).Parse();
        bool hasDpi = false;
        if (auto v = config.Get(L"dpi")) {
            int pct = static_cast<int>(v->Int64(static_cast<long long>(chosenScale_ * 100.0f + 0.5f)));
            if (pct < 100) pct = 100;
            if (pct > 300) pct = 300;
            float cfgScale = ClampScale(pct / 100.0f);
            userScale_ = cfgScale;
            chosenScale_ = cfgScale;
            hasDpi = true;
        }
        if (!hasDpi) {
            float autoScale = ClampScale(DetectScale());
            userScale_ = autoScale;
            chosenScale_ = autoScale;
        }
        if (auto v = config.Get(L"theme")) themeName_ = v->String(themeName_);
        if (auto v = config.Get(L"language")) {
            std::wstring lang = v->String();
            if (!lang.empty()) languageName_ = lang;
        }
        if (std::find(themeFiles_.begin(), themeFiles_.end(), themeName_) == themeFiles_.end()) {
            themeName_ = !themeFiles_.empty() ? themeFiles_.front() : L"default";
        }
        if (languageName_.empty() || Lower(languageName_) == L"auto") DetectLanguage();
        if (std::find(langFiles_.begin(), langFiles_.end(), languageName_) == langFiles_.end()) {
            languageName_ = !langFiles_.empty() ? langFiles_.front() : L"en-US";
        }
        LoadTheme(themeName_);
        LoadLanguage(languageName_);
    }

    // ExportLogsToFile pops a save-file dialog and writes the GUI log
    // history plus the warnings/errors toast list to a single UTF-8
    // text file. Line-oriented so operators can grep / tail it.
    void ExportLogsToFile() {
        if (m_hWnd == nullptr) return;
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t fname[MAX_PATH] = {};
        swprintf_s(fname, L"gpm-log-%04u%02u%02u-%02u%02u%02u.txt",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

        OPENFILENAMEW ofn{};
        wchar_t fileBuf[MAX_PATH] = {};
        wcscpy_s(fileBuf, fname);
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = m_hWnd;
        ofn.lpstrFile = fileBuf;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = L"Text files (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        ofn.lpstrDefExt = L"txt";
        if (!GetSaveFileNameW(&ofn)) return;

        std::wstring out;
        out.reserve(4096);
        out += L"GPM log export\r\n";
        wchar_t tsBuf[64];
        swprintf_s(tsBuf, L"Generated: %04u-%02u-%02u %02u:%02u:%02u\r\n\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        out += tsBuf;

        out += L"[GUI log history]\r\n";
        for (const auto& entry : logs_) {
            out += L"[";
            out += entry.time;
            out += L"] [";
            out += entry.level;
            out += L"] ";
            out += entry.message;
            out += L"\r\n";
        }

        out += L"\r\n[Warnings and errors]\r\n";
        for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it) {
            if (it->severity != L"warning" && it->severity != L"error") continue;
            out += L"[";
            out += it->time;
            out += L"] [";
            out += it->severity;
            out += L"] ";
            out += it->title;
            out += L" - ";
            out += it->message;
            out += L"\r\n";
        }

        HANDLE h = CreateFileW(fileBuf, GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            AddToast(L"Export", L"Failed to open output file", L"error");
            return;
        }
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, out.c_str(),
            static_cast<int>(out.size()), nullptr, 0, nullptr, nullptr);
        if (utf8Len > 0) {
            std::string utf8(utf8Len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, out.c_str(),
                static_cast<int>(out.size()), utf8.data(), utf8Len, nullptr, nullptr);
            DWORD wrote = 0;
            WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &wrote, nullptr);
        }
        CloseHandle(h);
        AddToast(L"Export", L"Logs exported to " + std::wstring(fileBuf), L"success");
    }

    void DetectLanguage() {
        wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
        if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0) {
            std::wstring locale = name;
            if (Lower(locale).find(L"zh") == 0) languageName_ = L"zh-CN";
            else languageName_ = L"en-US";
        }
    }

    void LoadTheme(const std::wstring& name, bool animated = false) {
        std::wstring target = name.empty() ? L"default" : name;
        PaletteSnapshot before = CapturePalette();
        std::wstring text = ReadUtf8File(exeDir_ + L"\\themes\\" + target + L".json");
        if (text.empty() && target != L"default") {
            target = L"default";
            text = ReadUtf8File(exeDir_ + L"\\themes\\default.json");
        }
        if (!text.empty()) ApplyThemeObject(JsonParser(text).Parse());
        else ApplyDefaultTheme(false);
        PaletteSnapshot after = CapturePalette();
        if (animated && target != themeName_) {
            themeFrom_ = before;
            themeTo_ = after;
            themeTransitionStartAt_ = GetTickCount64();
            themeTransitionActive_ = true;
            ApplyPaletteSnapshot(themeFrom_);
        }
        ApplyMiniGpmTheme();
        themeName_ = target;
    }

    void LoadLanguage(const std::wstring& name) {
        std::wstring target = name.empty() ? L"en-US" : name;
        std::wstring text = ReadUtf8File(exeDir_ + L"\\lang\\" + target + L".json");
        if (text.empty() && target != L"en-US") {
            target = L"en-US";
            text = ReadUtf8File(exeDir_ + L"\\lang\\en-US.json");
        }
        lang_.clear();
        JsonValue defaults = JsonParser(Lower(target).find(L"zh") == 0 ? DefaultLangZh() : DefaultLangEn()).Parse();
        for (const auto& kv : defaults.objectValue) {
            lang_[kv.first] = kv.second.String();
        }
        JsonValue root = JsonParser(text).Parse();
        for (const auto& kv : root.objectValue) {
            lang_[kv.first] = kv.second.String();
        }
        languageName_ = target;
    }

    void SaveConfig() {
        if (exeDir_.empty()) return;
        WriteUtf8File(exeDir_ + L"\\config.json",
            L"{\n"
            L"  \"dpi\": " + std::to_wstring(static_cast<int>(chosenScale_ * 100 + 0.5f)) + L",\n"
            L"  \"theme\": \"" + EscapeJson(themeName_) + L"\",\n"
            L"  \"language\": \"" + EscapeJson(languageName_) + L"\"\n"
            L"}\n");
    }

    void OpenMenu(const std::wstring& kind) {
        menuClosing_ = false;
        activeMenu_.clear();
        menuItems_.clear();
        if (kind == L"dpi") {
            for (int value : std::vector<int>{ 100, 125, 150, 175, 200, 225, 250, 275, 300 }) {
                menuItems_.push_back({ std::to_wstring(value), std::to_wstring(value) + L"%" });
            }
        } else if (kind == L"theme") {
            if (themeFiles_.empty()) themeFiles_ = { L"default", L"dark" };
            for (const auto& name : themeFiles_) menuItems_.push_back({ name, name });
        } else if (kind == L"language") {
            if (langFiles_.empty()) langFiles_ = { L"en-US", L"zh-CN" };
            for (const auto& name : langFiles_) menuItems_.push_back({ name, name });
        } else if (kind == L"category") {
            if (categories_.empty()) categories_ = { L"all" };
            for (const auto& category : categories_) {
                menuItems_.push_back({ category, CategoryLabel(category) });
            }
        }
        activeMenu_ = kind;
        menuFocusIndex_ = CurrentMenuIndex();
        menuOpenProgress_ = 0.0f;
        menuHoverAlpha_ = menuFocusIndex_ >= 0 ? 1.0f : 0.0f;
        menuHoverInitialized_ = false;
        menuHoverTargetY_ = menuFocusIndex_ >= 0 ? static_cast<float>(menuFocusIndex_) * 36.0f : 0.0f;
    }

    void CloseMenu() {
        if (activeMenu_.empty() && menuOpenProgress_ <= 0.0f) return;
        menuClosing_ = true;
    }

    void SelectMenuItem(const std::wstring& kind, const std::wstring& value) {
        if (kind == L"dpi") {
            SetScale(value);
        } else if (kind == L"theme") {
            LoadTheme(value, true);
            SaveConfig();
            AddToast(Tr(L"label.theme", L"Theme"), themeName_, L"info");
        } else if (kind == L"language") {
            LoadLanguage(value);
            SaveConfig();
            AddToast(Tr(L"label.language", L"Language"), languageName_, L"info");
        } else if (kind == L"category") {
            selectedCategory_ = value;
            marketScroll_ = 0;
            ApplySearch();
            AddToast(Tr(L"label.category", L"Category"), CategoryLabel(selectedCategory_), L"info");
        }
        CloseMenu();
    }

    int CurrentMenuIndex() const {
        if (menuItems_.empty()) return -1;
        for (int i = 0; i < static_cast<int>(menuItems_.size()); ++i) {
            bool current = false;
            if (activeMenu_ == L"dpi") {
                current = static_cast<int>(chosenScale_ * 100 + 0.5f) == std::stoi(menuItems_[i].id);
            } else if (activeMenu_ == L"theme") {
                current = menuItems_[i].id == themeName_;
            } else if (activeMenu_ == L"language") {
                current = menuItems_[i].id == languageName_;
            } else if (activeMenu_ == L"category") {
                current = IEquals(menuItems_[i].id, selectedCategory_);
            }
            if (current) return i;
        }
        return menuItems_.empty() ? -1 : 0;
    }

    void MoveMenuFocus(int delta) {
        if (menuItems_.empty()) return;
        if (menuFocusIndex_ < 0 || menuFocusIndex_ >= static_cast<int>(menuItems_.size())) {
            menuFocusIndex_ = CurrentMenuIndex();
        }
        if (menuFocusIndex_ < 0) menuFocusIndex_ = 0;
        int n = static_cast<int>(menuItems_.size());
        menuFocusIndex_ = (menuFocusIndex_ + delta + n) % n;
        hoverMenuItem_ = menuItems_[menuFocusIndex_].id;
        menuHoverTargetY_ = static_cast<float>(menuFocusIndex_) * 36.0f;
    }

    std::wstring ReplaceToken(std::wstring text, const std::wstring& key, const std::wstring& value) const {
        std::wstring token = L"{" + key + L"}";
        size_t pos = 0;
        while ((pos = text.find(token, pos)) != std::wstring::npos) {
            text.replace(pos, token.size(), value);
            pos += value.size();
        }
        return text;
    }

    std::wstring DefaultCategoryId() const {
        return L"uncategorized";
    }

    std::wstring NormalizeCategory(std::wstring category) const {
        category = Trim(category);
        if (category.empty()) return DefaultCategoryId();
        return category;
    }

    std::wstring BestCategoryForVersions(const std::vector<PackageVersion>& versions) const {
        if (versions.empty()) return DefaultCategoryId();
        std::wstring latestCategory = Trim(versions.front().category);
        if (!latestCategory.empty()) return NormalizeCategory(latestCategory);
        for (const auto& version : versions) {
            std::wstring category = Trim(version.category);
            if (!category.empty()) return NormalizeCategory(category);
        }
        return DefaultCategoryId();
    }

    std::wstring CategoryFromJsonItem(const JsonValue& item) const {
        const wchar_t* keys[] = { L"category", L"category_id", L"categoryId", L"group", L"type" };
        for (const auto* key : keys) {
            if (auto v = item.Get(key)) {
                std::wstring category = Trim(v->String());
                if (!category.empty()) return category;
            }
        }
        const wchar_t* arrayKeys[] = { L"categories", L"tags" };
        for (const auto* key : arrayKeys) {
            if (auto v = item.Get(key)) {
                if (v->type == JsonValue::Type::Array) {
                    for (const auto& part : v->arrayValue) {
                        std::wstring category = Trim(part.String());
                        if (!category.empty()) return category;
                    }
                }
            }
        }
        return L"";
    }

    std::wstring CategoryLabel(const std::wstring& category) const {
        if (category.empty() || category == L"all") return Tr(L"category.all", L"All");
        if (category == DefaultCategoryId()) return Tr(L"category.uncategorized", L"Uncategorized");
        return category;
    }

    std::wstring ProgressStageLabel(const std::wstring& stage) const {
        std::wstring key = Lower(Trim(stage));
        if (key == L"download") return Tr(L"progress.stage.download", L"Downloading");
        if (key == L"install") return Tr(L"progress.stage.install", L"Installing");
        if (key == L"update") return Tr(L"progress.stage.update", L"Updating index");
        if (key == L"verify") return Tr(L"progress.stage.verify", L"Verifying");
        if (key == L"done") return Tr(L"progress.stage.done", L"Completed");
        if (key == L"error") return Tr(L"progress.stage.error", L"Failed");
        if (key == L"cancel") return Tr(L"progress.stage.cancel", L"Cancelled");
        if (!stage.empty()) return stage;
        return Tr(L"progress.stage.idle", L"Ready");
    }

    std::wstring ProgressSummaryText() const {
        if (!progress_.active && progress_.percent <= 0) {
            return Tr(L"progress.idle", L"No active operation");
        }
        std::wstring subject = progress_.packageName.empty() ? progress_.id : progress_.packageName;
        std::wstring stage = ProgressStageLabel(progress_.stage);
        std::wstring status = Trim(progress_.status);
        if (status.empty()) status = stage;
        std::wstring text = stage;
        if (!subject.empty()) text += L" · " + subject;
        text += L" · " + status;
        if (progress_.total > 0) {
            text += L" · " + FormatSize(progress_.downloaded) + L" / " + FormatSize(progress_.total);
        } else if (progress_.downloaded > 0) {
            text += L" · " + FormatSize(progress_.downloaded);
        }
        if (progress_.speed > 0 && progress_.active) {
            text += L" · " + ReplaceToken(Tr(L"progress.speed", L"{speed}/s"), L"speed", FormatSize(progress_.speed));
        }
        if (progress_.threads > 1) {
            text += L" · " + std::to_wstring(progress_.threads) + L" threads";
        }
        return text;
    }

    std::wstring ProgressTitleText() const {
        if (!progress_.active && progress_.percent <= 0) {
            return Tr(L"progress.idle", L"No active operation");
        }
        std::wstring stage = ProgressStageLabel(progress_.stage);
        std::wstring subject = progress_.packageName.empty() ? progress_.id : progress_.packageName;
        if (subject.empty()) return stage;
        return stage + L" · " + subject;
    }

    std::wstring ProgressMetaText() const {
        if (!progress_.active && progress_.percent <= 0) {
            return Tr(L"progress.stage.idle", L"Ready");
        }
        std::vector<std::wstring> parts;
        std::wstring status = Trim(progress_.status);
        if (!status.empty() && !IEquals(status, progress_.stage)) parts.push_back(status);
        if (progress_.total > 0) {
            parts.push_back(FormatTransferSize(progress_.downloaded) + L" / " + FormatTransferSize(progress_.total));
        } else if (progress_.downloaded > 0) {
            parts.push_back(FormatTransferSize(progress_.downloaded));
        }
        if (progress_.speed > 0 && progress_.active) {
            parts.push_back(ReplaceToken(Tr(L"progress.speed", L"{speed}/s"), L"speed", FormatTransferSize(progress_.speed)));
        }
        if (progress_.threads > 1) {
            parts.push_back(ReplaceToken(Tr(L"progress.threads", L"{count} threads"), L"count", std::to_wstring(progress_.threads)));
        }
        if (parts.empty()) return ProgressStageLabel(progress_.stage);
        std::wstring text = parts[0];
        for (size_t i = 1; i < parts.size(); ++i) text += L" · " + parts[i];
        return text;
    }

    void RebuildGroups() {
        std::map<std::wstring, std::vector<PackageVersion>> grouped;
        for (const auto& p : indexData_) grouped[p.name].push_back(p);

        std::map<std::wstring, bool> oldSel;
        for (const auto& g : groups_) oldSel[g.name] = g.selected;

        groups_.clear();
        for (auto& kv : grouped) {
            auto& versions = kv.second;
            std::sort(versions.begin(), versions.end(), [](const PackageVersion& a, const PackageVersion& b) {
                return CompareVersions(a.version, b.version) > 0;
            });
            if (versions.empty()) continue;
            PackageGroup g;
            g.name = kv.first;
            g.latestVersion = versions.front().version;
            g.author = versions.front().author;
            g.category = BestCategoryForVersions(versions);
            g.description = versions.front().description;
            g.size = versions.front().size;
            g.versionCount = static_cast<int>(versions.size());
            g.versions = versions;
            g.selected = oldSel[g.name];
            for (const auto& ip : installed_) {
                if (IEquals(ip.name, g.name)) {
                    g.installedVersion = ip.version;
                    break;
                }
            }
            groups_.push_back(g);
        }
        std::sort(groups_.begin(), groups_.end(), [](const PackageGroup& a, const PackageGroup& b) {
            return Lower(a.name) < Lower(b.name);
        });
        RebuildCategories();
        ApplySearch();
    }

    void ApplySearch() {
        filtered_.clear();
        ClampSearchCaret();
        std::wstring q = Lower(Trim(search_));
        std::wstring selectedCategory = NormalizeCategory(selectedCategory_);
        for (int i = 0; i < static_cast<int>(groups_.size()); ++i) {
            const auto& g = groups_[i];
            bool categoryMatch = selectedCategory_ == L"all" || IEquals(g.category, selectedCategory);
            bool searchMatch = q.empty() || Lower(g.name).find(q) != std::wstring::npos ||
                Lower(g.author).find(q) != std::wstring::npos ||
                Lower(g.category).find(q) != std::wstring::npos ||
                Lower(g.description).find(q) != std::wstring::npos;
            if (categoryMatch && searchMatch) {
                filtered_.push_back(i);
            }
        }
        ClampScroll();
    }

    void RebuildCategories() {
        std::vector<std::wstring> next{ L"all" };
        for (const auto& g : groups_) {
            std::wstring category = NormalizeCategory(g.category);
            bool exists = false;
            for (const auto& item : next) {
                if (IEquals(item, category)) {
                    exists = true;
                    break;
                }
            }
            if (!exists) next.push_back(category);
        }
        std::sort(next.begin() + 1, next.end(), [](const std::wstring& a, const std::wstring& b) {
            return Lower(a) < Lower(b);
        });
        categories_ = next;
        bool selectedExists = selectedCategory_ == L"all";
        for (const auto& item : categories_) {
            if (IEquals(item, selectedCategory_)) {
                selectedExists = true;
                selectedCategory_ = item;
                break;
            }
        }
        if (!selectedExists) selectedCategory_ = L"all";
    }

    float MeasureTextWidth(const std::wstring& text, float size, bool bold) const {
        if (text.empty()) return 0.0f;
        IDWriteFactory* dw = ExD2DFactory::GetDWriteFactory();
        if (!dw) return static_cast<float>(text.size()) * size * 0.56f;
        IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(size * userScale_ / ExDPI::GetScale(),
            bold, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        if (!fmt) return static_cast<float>(text.size()) * size * 0.56f;
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        IDWriteTextLayout* layout = nullptr;
        HRESULT hr = dw->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), fmt,
            ScaleF(1200.0f), ScaleF(64.0f), &layout);
        float width = static_cast<float>(text.size()) * size * 0.56f;
        if (SUCCEEDED(hr) && layout) {
            DWRITE_TEXT_METRICS metrics = {};
            if (SUCCEEDED(layout->GetMetrics(&metrics))) width = metrics.width / userScale_;
        }
        if (layout) layout->Release();
        fmt->Release();
        return width;
    }

    void ClampSearchCaret() {
        if (searchCaretIndex_ > search_.size()) searchCaretIndex_ = search_.size();
    }

    float MeasureSearchPrefix(size_t count) const {
        if (count == 0) return 0.0f;
        return MeasureTextWidth(search_.substr(0, (std::min)(count, search_.size())), 13.0f, false);
    }

    void EnsureSearchCaretVisible(float visibleWidth) {
        ClampSearchCaret();
        if (visibleWidth <= 24.0f) {
            searchScrollX_ = 0.0f;
            return;
        }
        float margin = 12.0f;
        float caretX = MeasureSearchPrefix(searchCaretIndex_);
        float totalW = MeasureTextWidth(search_, 13.0f, false);
        float maxScroll = (std::max)(0.0f, totalW - visibleWidth + margin);
        if (caretX - searchScrollX_ > visibleWidth - margin) {
            searchScrollX_ = caretX - (visibleWidth - margin);
        }
        if (caretX - searchScrollX_ < margin) {
            searchScrollX_ = (std::max)(0.0f, caretX - margin);
        }
        if (searchScrollX_ < 0.0f) searchScrollX_ = 0.0f;
        if (searchScrollX_ > maxScroll) searchScrollX_ = maxScroll;
    }

    void InsertSearchText(const std::wstring& text) {
        if (text.empty()) return;
        ClampSearchCaret();
        search_.insert(searchCaretIndex_, text);
        searchCaretIndex_ += text.size();
        ApplySearch();
        EnsureSearchCaretVisible(288.0f);
    }

    void UpdateStats() {
        available_ = static_cast<int>(groups_.size());
        installedCount_ = static_cast<int>(installed_.size());
        updates_ = 0;
        for (const auto& g : groups_) {
            if (!g.installedVersion.empty() && CompareVersions(g.latestVersion, g.installedVersion) > 0) ++updates_;
        }
    }

    std::wstring StatusText(const PackageGroup& g) const {
        if (g.installedVersion.empty()) return Tr(L"status.new", L"New");
        int cmp = CompareVersions(g.latestVersion, g.installedVersion);
        if (cmp > 0) return Tr(L"status.upgrade", L"Upgrade");
        if (cmp < 0) return Tr(L"status.downgrade", L"Downgrade");
        return Tr(L"status.installed", L"Installed");
    }

    COLORREF AccentForStatus(const std::wstring& status) const {
        if (status == Tr(L"status.installed", L"Installed")) return Palette::Success;
        if (status == Tr(L"status.upgrade", L"Upgrade")) return Palette::Info;
        if (status == Tr(L"status.downgrade", L"Downgrade")) return Palette::Warning;
        return RGB(98, 126, 234);
    }

    void ClampScroll() {
        int marketRows = static_cast<int>(filtered_.size());
        int visibleRowsH = static_cast<int>((std::max)(120.0f, ContentBottom() - TableHeaderBottom()));
        int maxMarket = (std::max)(0, marketRows * 44 - visibleRowsH);
        if (marketScroll_ < 0) marketScroll_ = 0;
        if (marketScroll_ > maxMarket) marketScroll_ = maxMarket;
        int maxInstalled = (std::max)(0, static_cast<int>(installed_.size()) * 44 - visibleRowsH);
        if (installedScroll_ < 0) installedScroll_ = 0;
        if (installedScroll_ > maxInstalled) installedScroll_ = maxInstalled;
    }

    float MinColumnWidth(size_t index, bool installed) const {
        if (index == 0) return 42.0f;
        if (installed) {
            static const float mins[] = { 42, 150, 82, 100, 128, 104 };
            if (index < _countof(mins)) return mins[index];
            return 86.0f;
        }
        static const float mins[] = { 42, 150, 82, 100, 92, 92, 78, 104 };
        if (index < _countof(mins)) return mins[index];
        return 86.0f;
    }

    void ResizeColumnTo(float lx) {
        auto& widths = resizingInstalled_ ? installedColumnWidths_ : marketColumnWidths_;
        int i = resizingColumnIndex_;
        if (i < 0 || i + 1 >= static_cast<int>(widths.size())) return;
        float delta = lx - resizeStartX_;
        float minLeft = MinColumnWidth(static_cast<size_t>(i), resizingInstalled_);
        float minRight = MinColumnWidth(static_cast<size_t>(i + 1), resizingInstalled_);
        float left = (std::max)(minLeft, resizeStartLeftW_ + delta);
        float right = resizeStartLeftW_ + resizeStartRightW_ - left;
        if (right < minRight) {
            right = minRight;
            left = resizeStartLeftW_ + resizeStartRightW_ - right;
        }
        widths[i] = left;
        widths[i + 1] = right;
    }

    void ToggleRowSelection(const RowHit& h) {
        if (h.installed) {
            if (h.index >= 0 && h.index < static_cast<int>(installed_.size())) {
                installed_[h.index].selected = !installed_[h.index].selected;
            }
        } else {
            if (h.index >= 0 && h.index < static_cast<int>(groups_.size())) {
                groups_[h.index].selected = !groups_[h.index].selected;
            }
        }
    }

    std::vector<std::wstring> SelectedMarket() const {
        std::vector<std::wstring> names;
        for (const auto& g : groups_) if (g.selected) names.push_back(g.name);
        return names;
    }

    std::vector<std::wstring> SelectedInstalled() const {
        std::vector<std::wstring> names;
        for (const auto& p : installed_) if (p.selected) names.push_back(p.name);
        return names;
    }

    std::wstring PackagesParam(const std::vector<std::wstring>& names) {
        std::wstring json = L"{\"packages\":[";
        for (size_t i = 0; i < names.size(); ++i) {
            if (i) json += L",";
            json += L"\"" + EscapeJson(names[i]) + L"\"";
        }
        json += L"]}";
        return json;
    }

    int FindGroupIndexByName(const std::wstring& name) const {
        for (int i = 0; i < static_cast<int>(groups_.size()); ++i) {
            if (IEquals(groups_[i].name, name)) return i;
        }
        return -1;
    }

    int FocusedVersionIndex() const {
        int idx = FindGroupIndexByName(versionFocusName_);
        if (idx >= 0) return idx;
        return groups_.empty() ? -1 : 0;
    }

    const PackageGroup* FocusedVersionGroup() const {
        int idx = FocusedVersionIndex();
        return idx >= 0 ? &groups_[idx] : nullptr;
    }

    bool IsVersionGroupCollapsed(const std::wstring& name) const {
        for (const auto& item : collapsedVersionGroups_) {
            if (IEquals(item, name)) return true;
        }
        return false;
    }

    void SetVersionGroupCollapsed(const std::wstring& name, bool collapsed) {
        if (name.empty()) return;
        for (auto it = collapsedVersionGroups_.begin(); it != collapsedVersionGroups_.end(); ++it) {
            if (IEquals(*it, name)) {
                if (!collapsed) collapsedVersionGroups_.erase(it);
                return;
            }
        }
        if (collapsed) collapsedVersionGroups_.push_back(name);
    }

    void ToggleVersionGroup(int groupIndex) {
        if (groupIndex < 0 || groupIndex >= static_cast<int>(groups_.size())) return;
        const auto& name = groups_[groupIndex].name;
        versionFocusName_ = name;
        SetVersionGroupCollapsed(name, !IsVersionGroupCollapsed(name));
    }

    int VersionTreeOffsetForGroup(int groupIndex) const {
        const int groupH = 52;
        const int versionH = 48;
        const int groupGap = 8;
        int y = 0;
        for (int i = 0; i < groupIndex && i < static_cast<int>(groups_.size()); ++i) {
            int versions = IsVersionGroupCollapsed(groups_[i].name) ? 0 : static_cast<int>(groups_[i].versions.size());
            y += groupH + versions * versionH + groupGap;
        }
        return (std::max)(0, y - 64);
    }

    bool IsMarketRowDoubleClick(int index) {
        ULONGLONG now = GetTickCount64();
        bool dbl = lastRowClickIndex_ == index && now - lastRowClickAt_ <= 420;
        lastRowClickIndex_ = index;
        lastRowClickAt_ = now;
        return dbl;
    }

    void OpenVersionHistory(int groupIndex) {
        if (groupIndex < 0 || groupIndex >= static_cast<int>(groups_.size())) return;
        versionFocusName_ = groups_[groupIndex].name;
        SetVersionGroupCollapsed(versionFocusName_, false);
        versionScroll_ = VersionTreeOffsetForGroup(groupIndex);
        queueScroll_ = 0;
        SwitchPage(Page::Versions);
    }

    std::wstring VersionSelectionKey(const std::wstring& name, const std::wstring& version) const {
        return Lower(name) + L"\x1f" + Lower(version);
    }

    bool IsVersionKeySelected(const std::wstring& key) const {
        for (const auto& item : selectedVersionKeys_) {
            if (item == key) return true;
        }
        return false;
    }

    bool IsVersionSelected(const PackageVersion& version) const {
        return IsVersionKeySelected(VersionSelectionKey(version.name, version.version));
    }

    void SetVersionSelected(const PackageVersion& version, bool selected) {
        std::wstring key = VersionSelectionKey(version.name, version.version);
        for (auto it = selectedVersionKeys_.begin(); it != selectedVersionKeys_.end(); ++it) {
            if (*it == key) {
                if (!selected) selectedVersionKeys_.erase(it);
                return;
            }
        }
        if (selected) selectedVersionKeys_.push_back(key);
    }

    void ToggleVersionSelected(int groupIndex, size_t versionIndex) {
        if (groupIndex < 0 || groupIndex >= static_cast<int>(groups_.size())) return;
        if (versionIndex >= groups_[groupIndex].versions.size()) return;
        const auto& version = groups_[groupIndex].versions[versionIndex];
        SetVersionSelected(version, !IsVersionSelected(version));
    }

    size_t SelectedVersionCount() const {
        size_t count = 0;
        for (const auto& g : groups_) {
            for (const auto& v : g.versions) {
                if (IsVersionSelected(v)) ++count;
            }
        }
        return count;
    }

    void ClearVersionSelection() {
        selectedVersionKeys_.clear();
    }

    void AddSelectedVersionsToQueue() {
        size_t count = 0;
        for (const auto& g : groups_) {
            for (const auto& v : g.versions) {
                if (IsVersionSelected(v)) {
                    AddVersionToQueue(v, false);
                    ++count;
                }
            }
        }
        if (count == 0) {
            AddToast(Tr(L"toast.selection", L"Selection"), Tr(L"toast.selectVersions", L"Select versions first"), L"warning");
            return;
        }
        AddToast(Tr(L"label.pendingInstall", L"Pending install"),
            ReplaceToken(Tr(L"toast.versionsQueued", L"{count} versions added to queue"), L"count", std::to_wstring(count)), L"info");
    }

    void AddVersionToQueue(const PackageVersion& version, bool toast = true) {
        for (auto& item : installQueue_) {
            if (IEquals(item.name, version.name)) {
                item.version = version.version;
                item.author = version.author;
                item.category = NormalizeCategory(version.category);
                item.size = version.size;
                if (toast) AddToast(Tr(L"label.pendingInstall", L"Pending install"), version.name + L" " + version.version, L"info");
                return;
            }
        }
        installQueue_.push_back({ version.name, version.version, version.author, NormalizeCategory(version.category), version.size, false });
        if (toast) AddToast(Tr(L"label.pendingInstall", L"Pending install"), version.name + L" " + version.version, L"info");
    }

    size_t SelectedQueueCount() const {
        size_t count = 0;
        for (const auto& item : installQueue_) {
            if (item.selected) ++count;
        }
        return count;
    }

    void ToggleQueueItem(size_t index) {
        if (index >= installQueue_.size()) return;
        installQueue_[index].selected = !installQueue_[index].selected;
    }

    void StartQueueExit(size_t index) {
        if (index >= installQueue_.size()) return;
        queueExitAnims_.push_back({ installQueue_[index], static_cast<float>(index) * 50.0f, GetTickCount64() });
    }

    void RemoveQueueItem(size_t index) {
        if (index >= installQueue_.size()) return;
        StartQueueExit(index);
        installQueue_.erase(installQueue_.begin() + static_cast<ptrdiff_t>(index));
        if (queueScroll_ < 0) queueScroll_ = 0;
    }

    void RemoveSelectedQueueItems() {
        size_t count = SelectedQueueCount();
        if (count == 0) {
            AddToast(Tr(L"toast.selection", L"Selection"), Tr(L"toast.selectQueue", L"Select queue items first"), L"warning");
            return;
        }
        for (int i = static_cast<int>(installQueue_.size()) - 1; i >= 0; --i) {
            if (installQueue_[i].selected) RemoveQueueItem(static_cast<size_t>(i));
        }
        AddToast(Tr(L"label.pendingInstall", L"Pending install"),
            ReplaceToken(Tr(L"toast.queueRemoved", L"{count} queue items removed"), L"count", std::to_wstring(count)), L"info");
    }

    void RemoveAllQueueItems() {
        for (int i = static_cast<int>(installQueue_.size()) - 1; i >= 0; --i) {
            RemoveQueueItem(static_cast<size_t>(i));
        }
        queueInstalling_ = false;
        queueScroll_ = 0;
    }

    void CompleteQueueItem(const std::wstring& packageName) {
        if (!queueInstalling_ || installQueue_.empty()) return;
        if (packageName.empty()) {
            RemoveAllQueueItems();
            return;
        }
        for (size_t i = 0; i < installQueue_.size(); ++i) {
            if (IEquals(installQueue_[i].name, packageName)) {
                RemoveQueueItem(i);
                if (installQueue_.empty()) queueInstalling_ = false;
                return;
            }
        }
        RemoveQueueItem(0);
        if (installQueue_.empty()) queueInstalling_ = false;
    }

    std::wstring InstallQueueParam() const {
        std::wstring json = L"{\"items\":[";
        for (size_t i = 0; i < installQueue_.size(); ++i) {
            if (i) json += L",";
            json += L"{\"name\":\"" + EscapeJson(installQueue_[i].name) + L"\",\"version\":\"" +
                EscapeJson(installQueue_[i].version) + L"\"}";
        }
        json += L"]}";
        return json;
    }

    void RunRowAction(const RowHit& h) {
        if (h.installed) {
            if (h.index >= 0 && h.index < static_cast<int>(installed_.size())) {
                client_.SendCommand("uninstall", PackagesParam({ installed_[h.index].name }));
                AddToast(L"Uninstall", installed_[h.index].name, L"info");
            }
        } else {
            if (h.index >= 0 && h.index < static_cast<int>(groups_.size())) {
                client_.SendCommand("install", PackagesParam({ groups_[h.index].name }));
                AddToast(L"Install", groups_[h.index].name, L"info");
            }
        }
    }

    void HandleButton(const std::wstring& id) {
        auto ensureBackend = [&]() -> bool {
            if (client_.IsConnected()) return true;
            AddToast(L"GPM", Tr(L"toast.backendStart", L"Start gpm.exe --gui on localhost:8080"), L"warning");
            if (!backendLaunchRequested_) {
                backendLaunchRequested_ = StartBackend();
                lastBackendLaunchAt_ = GetTickCount64();
            }
            return false;
        };
        if (id == L"refresh") {
            if (!ensureBackend()) return;
            client_.SendCommand("get_index");
            client_.SendCommand("get_installed");
            AddToast(Tr(L"action.refresh", L"Refresh"), Tr(L"toast.refreshRequested", L"Requested package data"), L"info");
        } else if (id == L"update_index") {
            if (!ensureBackend()) return;
            client_.SendCommand("update_index");
            AddToast(Tr(L"toast.index", L"Index"), Tr(L"toast.indexUpdating", L"Updating package index"), L"info");
        } else if (id == L"batch_install") {
            if (!ensureBackend()) return;
            auto names = SelectedMarket();
            if (names.empty()) AddToast(Tr(L"toast.selection", L"Selection"), Tr(L"toast.selectPackages", L"Select packages in the first column first"), L"warning");
            else {
                client_.SendCommand("install", PackagesParam(names));
                AddToast(Tr(L"action.batchInstall", L"Batch install"), ReplaceToken(Tr(L"toast.packagesQueued", L"{count} packages queued"), L"count", std::to_wstring(names.size())), L"info");
                for (auto& g : groups_) g.selected = false;
            }
        } else if (id == L"batch_uninstall") {
            if (!ensureBackend()) return;
            auto names = SelectedInstalled();
            if (names.empty()) AddToast(Tr(L"toast.selection", L"Selection"), Tr(L"toast.selectInstalled", L"Select installed packages first"), L"warning");
            else {
                client_.SendCommand("uninstall", PackagesParam(names));
                AddToast(Tr(L"action.batchUninstall", L"Batch uninstall"), ReplaceToken(Tr(L"toast.packagesQueued", L"{count} packages queued"), L"count", std::to_wstring(names.size())), L"info");
                for (auto& p : installed_) p.selected = false;
            }
        } else if (id == L"queue_install") {
            if (!ensureBackend()) return;
            if (installQueue_.empty()) {
                AddToast(Tr(L"label.pendingInstall", L"Pending install"), Tr(L"empty.queue", L"No versions selected"), L"warning");
                return;
            }
            size_t count = installQueue_.size();
            client_.SendCommand("install", InstallQueueParam());
            queueInstalling_ = true;
            for (auto& item : installQueue_) item.selected = false;
            queueScroll_ = 0;
            AddToast(Tr(L"action.installQueue", L"Install Queue"), ReplaceToken(Tr(L"toast.queueQueued", L"{count} versions queued"), L"count", std::to_wstring(count)), L"info");
        } else if (id == L"queue_clear") {
            RemoveAllQueueItems();
            queueScroll_ = 0;
            AddToast(Tr(L"label.pendingInstall", L"Pending install"), Tr(L"toast.queueCleared", L"Queue cleared"), L"info");
        } else if (id == L"queue_remove_selected") {
            RemoveSelectedQueueItems();
        } else if (id.rfind(L"queue_select:", 0) == 0) {
            try {
                ToggleQueueItem(static_cast<size_t>(std::stoul(id.substr(13))));
            } catch (...) {}
        } else if (id.rfind(L"queue_remove:", 0) == 0) {
            try {
                RemoveQueueItem(static_cast<size_t>(std::stoul(id.substr(13))));
            } catch (...) {}
        } else if (id.rfind(L"version_toggle:", 0) == 0) {
            try {
                ToggleVersionGroup(std::stoi(id.substr(15)));
            } catch (...) {}
        } else if (id == L"version_add_selected") {
            AddSelectedVersionsToQueue();
        } else if (id == L"version_clear_selection") {
            ClearVersionSelection();
            AddToast(Tr(L"toast.selection", L"Selection"), Tr(L"toast.selectionCleared", L"Selection cleared"), L"info");
        } else if (id.rfind(L"version_select:", 0) == 0) {
            try {
                std::wstring rest = id.substr(15);
                size_t sep = rest.find(L':');
                if (sep != std::wstring::npos) {
                    int groupIdx = std::stoi(rest.substr(0, sep));
                    size_t versionIdx = static_cast<size_t>(std::stoul(rest.substr(sep + 1)));
                    ToggleVersionSelected(groupIdx, versionIdx);
                }
            } catch (...) {}
        } else if (id.rfind(L"version_add:", 0) == 0) {
            try {
                std::wstring rest = id.substr(12);
                size_t sep = rest.find(L':');
                if (sep != std::wstring::npos) {
                    size_t groupIdx = static_cast<size_t>(std::stoul(rest.substr(0, sep)));
                    size_t versionIdx = static_cast<size_t>(std::stoul(rest.substr(sep + 1)));
                    if (groupIdx < groups_.size() && versionIdx < groups_[groupIdx].versions.size()) {
                        AddVersionToQueue(groups_[groupIdx].versions[versionIdx]);
                    }
                } else {
                    const PackageGroup* g = FocusedVersionGroup();
                    if (!g) return;
                    size_t idx = static_cast<size_t>(std::stoul(rest));
                    if (idx < g->versions.size()) AddVersionToQueue(g->versions[idx]);
                }
            } catch (...) {}
        } else if (id == L"cancel_task") {
            if (!ensureBackend()) return;
            if (!progress_.active) {
                AddToast(L"GPM", Tr(L"progress.idle", L"No active operation"), L"info");
                return;
            }
            client_.SendCommand("cancel_task", L"{\"id\":\"active\"}");
            progress_.status = L"Cancelling...";
            AddToast(L"GPM", Tr(L"toast.cancelRequested", L"Cancellation requested"), L"warning");
        } else if (id == L"search") {
            if (!ensureBackend()) return;
            searchFocused_ = true;
            searchCaretIndex_ = search_.size();
            EnsureSearchCaretVisible(288.0f);
            if (m_hWnd) ::SetFocus(m_hWnd);
            ApplySearch();
            client_.SendCommand("search", L"{\"query\":\"" + EscapeJson(search_) + L"\"}");
            AddToast(Tr(L"action.search", L"Search"), search_.empty() ? Tr(L"toast.showingAll", L"Showing all packages") : search_, L"info");
        } else if (id == L"status_toggle") {
            statusPanelOpen_ = !statusPanelOpen_;
            statusPanelToggledAt_ = GetTickCount64();
        } else if (id == L"logs_export") {
            ExportLogsToFile();
        } else if (id == L"search_box") {
            searchFocused_ = true;
            searchCaretIndex_ = search_.size();
            EnsureSearchCaretVisible(288.0f);
            if (m_hWnd) ::SetFocus(m_hWnd);
        } else if (id == L"clear_search") {
            search_.clear();
            searchCaretIndex_ = 0;
            searchScrollX_ = 0.0f;
            ApplySearch();
            searchFocused_ = true;
            if (m_hWnd) ::SetFocus(m_hWnd);
        } else if (id.rfind(L"category:", 0) == 0) {
            selectedCategory_ = id.substr(9);
            marketScroll_ = 0;
            ApplySearch();
            AddToast(Tr(L"label.category", L"Category"), CategoryLabel(selectedCategory_), L"info");
        } else if (id == L"dpi" || id == L"theme" || id == L"language" || id == L"category") {
            if (activeMenu_ == id) CloseMenu();
            else OpenMenu(id);
        }
    }

    void DrawBackground(ID2D1RenderTarget* rt, D2D1_RECT_F) {
        float motionFade = 1.0f;
        if (windowAnimMode_ != 0) {
            motionFade = 1.0f - 0.34f * WindowAnimProgress();
        }
        FillRound(rt, -7, 4, BaseW() + 14, BaseH() + 10, kBodyRadius + 5, RGB(14, 25, 42), 0.032f * motionFade);
        FillRound(rt, -4, 3, BaseW() + 8, BaseH() + 7, kBodyRadius + 3, RGB(14, 25, 42), 0.046f * motionFade);
        FillRound(rt, -2, 2, BaseW() + 4, BaseH() + 4, kBodyRadius + 1, RGB(14, 25, 42), 0.040f * motionFade);
        FillRound(rt, 0, 0, BaseW(), BaseH(), kBodyRadius, Palette::Window, 1.0f);
        float reveal = SplashSidebarReveal();
        float sw = SidebarW() * reveal;
        if (sw > 1.0f) {
            FillBottomLeftRect(rt, { 0, kTitleH, sw, static_cast<float>(BaseH() - kTitleH) }, kBodyRadius, Palette::Sidebar, 1.0f);
            // Sidebar right edge runs from titlebar bottom to footer
            // top (顶天立地). The footer's own divider meets it at the
            // bottom; the titlebar's chrome is painted first so the
            // top end of this line is naturally covered.
            FillRect(rt, sw, kTitleH, 1, BaseH() - kTitleH - kFooterH, Palette::Border, 0.62f);
        }
        StrokeRound(rt, 0.5f, 0.5f, BaseW() - 1, BaseH() - 1, kBodyRadius, RGB(120, 133, 152), 0.20f);
    }

    void DrawTopBar(ID2D1RenderTarget* rt) {
        FillRect(rt, 12, 0, BaseW() - 24, kTitleH, Palette::Window);
        FillRect(rt, 0, 10, BaseW(), kTitleH - 10, Palette::Window);
        RectF minRc{ BaseW() - 92.0f, 0.0f, 46.0f, static_cast<float>(kTitleH) };
        RectF closeRc{ BaseW() - 46.0f, 0.0f, 46.0f, static_cast<float>(kTitleH) };
        titleButtonHits_.push_back({ L"window_min", minRc });
        titleButtonHits_.push_back({ L"window_close", closeRc });
        bool hoverMin = hoverButton_ == L"window_min";
        bool hoverClose = hoverButton_ == L"window_close";
        // Min/close are full-height, edge-to-edge, no rounded corners \u2014
        // same shape as the close button (which already uses
        // FillTopRightRect to handle the window corner radius).
        if (hoverMin) FillRect(rt, minRc.x, minRc.y, minRc.w, minRc.h, Palette::SurfaceAlt, 1.0f);
        if (hoverClose) FillTopRightRect(rt, closeRc, kBodyRadius, Palette::Error, 1.0f);
        // App brand in the middle of the title bar.
        DrawText(rt, Tr(L"app.title", L"GPM"),
            24.0f, 0.0f, 420.0f, static_cast<float>(kTitleH),
            Palette::Text, 12.0f, true, DWRITE_TEXT_ALIGNMENT_LEADING, 0.95f);
        COLORREF closeColor = hoverButton_ == L"window_close" ? RGB(255, 255, 255) : Palette::TextSoft;
        DrawControlTextIcon(rt, L"\uE921", minRc.x, minRc.y + 1.0f, minRc.w, minRc.h, Palette::TextSoft, 10.5f, DWRITE_TEXT_ALIGNMENT_CENTER, hoverMin ? 1.0f : 0.92f);
        DrawControlTextIcon(rt, L"\uE8BB", closeRc.x, closeRc.y + 1.0f, closeRc.w, closeRc.h, closeColor, 10.5f, DWRITE_TEXT_ALIGNMENT_CENTER, hoverClose ? 1.0f : 0.94f);
    }

    void DrawSidebar(ID2D1RenderTarget* rt) {
        float sw = SidebarW();
        float reveal = SplashSidebarReveal();
        if (reveal <= 0.001f) return;
        sw *= reveal;
        rt->PushAxisAlignedClip(R(0, kTitleH, sw, BaseH() - kTitleH), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        RectF hamburger = HamburgerRect();
        if (hoverButton_ == L"sidebar_toggle") FillRound(rt, hamburger.x, hamburger.y, hamburger.w, hamburger.h, 4, Palette::SidebarHover, 1.0f);
        for (int i = 0; i < 3; ++i) {
            float yy = hamburger.y + 8.0f + i * 5.5f;
            StrokeLine(rt, hamburger.x + 9, yy, hamburger.x + 24, yy, Palette::TextSoft, 1.2f, 0.95f);
        }
        std::wstring labels[] = {
            Tr(L"nav.market", L"Market"),
            Tr(L"nav.installed", L"Installed"),
            Tr(L"nav.versions", L"Versions"),
            Tr(L"nav.settings", L"Settings"),
            Tr(L"nav.about", L"About")
        };
        const wchar_t* icons[] = { L"\uE719", L"\uE74D", L"\uE8A5", L"\uE713", L"\uE946" };
        float navTextAlpha = Clamp01((sw - 76.0f) / 96.0f);
        bool lightTheme = ColorLuma(Palette::Window) > 150.0f;
        COLORREF selectedBg = lightTheme
            ? Mix(Palette::SurfaceAlt, RGB(210, 214, 219), 0.52f)
            : Mix(Palette::SurfaceAlt, RGB(69, 72, 78), 0.48f);
        COLORREF hoverBg = lightTheme
            ? Mix(Palette::SidebarHover, RGB(220, 224, 229), 0.44f)
            : Mix(Palette::SidebarHover, RGB(63, 66, 72), 0.36f);
        RectF activeR = NavRect(0);
        activeR.y = 136.0f + navAnim_ * 48.0f;
        activeR.w = (std::max)(40.0f, sw - 24.0f);
        FillRound(rt, activeR.x, activeR.y, activeR.w, activeR.h, 9, selectedBg, 1.0f);
        if (navHoverAlpha_ > 0.01f) {
            RectF hoverR = NavRect(0);
            hoverR.y = 136.0f + navHoverAnim_ * 48.0f;
            hoverR.w = (std::max)(40.0f, sw - 24.0f);
            FillRound(rt, hoverR.x, hoverR.y, hoverR.w, hoverR.h, 9, hoverBg, navHoverAlpha_);
        }
        for (int i = 0; i < kNavCount; ++i) {
            RectF r = NavRect(i);
            bool active = static_cast<int>(currentPage_) == i;
            COLORREF iconColor = active ? Palette::Text : Palette::TextSoft;
            COLORREF textColor = active ? Palette::Text : Palette::TextSoft;
            float collapsedNudge = (1.0f - navTextAlpha) * 2.2f;
            float iconCenter = r.x + 17.0f + collapsedNudge + (navTextAlpha * 6.0f);
            float iconW = 34.0f;
            float iconAlpha = active ? 1.0f : 0.94f;
            DrawControlTextIcon(rt, icons[i], iconCenter - 17.0f, r.y, iconW, r.h, iconColor, 16.0f, DWRITE_TEXT_ALIGNMENT_CENTER, iconAlpha);
            float textAlpha = navTextAlpha;
            if (textAlpha > 0.01f) {
                float textX = r.x + 46.0f + (1.0f - textAlpha) * 8.0f;
                float textW = (std::max)(10.0f, r.w - 54.0f);
                DrawControlText(rt, labels[i], textX, r.y, textW, r.h,
                    textColor, 14, active, DWRITE_TEXT_ALIGNMENT_LEADING, textAlpha);
            }
        }
        float from = navIndicatorFrom_;
        float to = navIndicatorTo_;
        float raw = navIndicatorStartAt_ == 0
            ? 1.0f
            : Clamp01(static_cast<float>(GetTickCount64() - navIndicatorStartAt_) / 430.0f);
        float fast = EaseOutCubic(Clamp01(raw * 1.34f));
        float slow = EaseOutCubic(Clamp01((raw - 0.12f) / 0.88f));
        bool movingDown = to >= from;
        float itemTopA = 136.0f + from * 48.0f + 7.0f;
        float itemTopB = 136.0f + to * 48.0f + 7.0f;
        float itemBottomA = itemTopA + 24.0f;
        float itemBottomB = itemTopB + 24.0f;
        float markerY = movingDown
            ? itemTopA + (itemTopB - itemTopA) * slow
            : itemTopA + (itemTopB - itemTopA) * fast;
        float markerBottom = movingDown
            ? itemBottomA + (itemBottomB - itemBottomA) * fast
            : itemBottomA + (itemBottomB - itemBottomA) * slow;
        float markerH = (std::max)(18.0f, markerBottom - markerY);
        float markerX = activeR.x + 3.0f;
        FillRound(rt, markerX, markerY, 3.5f, markerH, 2.0f, Palette::Primary, 1.0f);
        rt->PopAxisAlignedClip();
    }

    void DrawPage(ID2D1RenderTarget* rt) {
        float t = Clamp01(static_cast<float>(GetTickCount64() - pageSwitchAt_) / 220.0f);
        pageEase_ = EaseOutCubic(t);
        DrawPageById(rt, currentPage_);
        if (pageEase_ < 0.999f) {
            FillRect(rt, SidebarW(), kTitleH, BaseW() - SidebarW(), BaseH() - kTitleH, Palette::Window, (1.0f - pageEase_) * 0.18f);
        }
        if (!activeMenu_.empty()) {
            RectF anchor = ActiveMenuAnchorRect(ContentX(), ContentW());
            DrawDropdownMenu(rt, anchor);
        }
    }

    void DrawPageById(ID2D1RenderTarget* rt, Page page) {
        switch (page) {
        case Page::Market: DrawMarket(rt); break;
        case Page::Installed: DrawInstalled(rt); break;
        case Page::Versions: DrawVersions(rt); break;
        case Page::Settings: DrawSettings(rt); break;
        case Page::About: DrawAbout(rt); break;
        }
    }

    void DrawPageTitle(ID2D1RenderTarget* rt, const std::wstring& title, const std::wstring& subtitle) {
        DrawText(rt, title, ContentX(), 48, 420, 34, Palette::Text, 23, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawText(rt, subtitle, ContentX(), 82, 680, 24, Palette::TextSoft, 13, false, DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    RectF SettingActionRect(float x, float y, float w) const {
        return { x + w - 192, y + 18, 168, 38 };
    }

    RectF ActiveMenuAnchorRect(float x, float w) const {
        if (activeMenu_ == L"category") return MarketCategoryRect(x, w);
        if (activeMenu_ == L"theme") return SettingActionRect(x, 232.0f, w);
        if (activeMenu_ == L"language") return SettingActionRect(x, 332.0f, w);
        return SettingActionRect(x, 132.0f, w);
    }

    RectF MarketCategoryRect(float x, float w) const {
        float batchW = 156.0f;
        float catW = 168.0f;
        float gap = 10.0f;
        float right = x + w;
        return { right - batchW - gap - catW, 124.0f, catW, 38.0f };
    }

    RectF MarketBatchRect(float x, float w) const {
        float batchW = 156.0f;
        return { x + w - batchW, 124.0f, batchW, 38.0f };
    }

    void DrawDropdownAction(ID2D1RenderTarget* rt, RectF action, const std::wstring& value, const std::wstring& buttonId) {
        bool active = activeMenu_ == buttonId;
        bool hover = hoverButton_ == buttonId;
        COLORREF bg = active ? Mix(Palette::SurfaceAlt, Palette::PrimarySoft, 0.20f)
            : hover ? HoverFill()
            : Mix(Palette::Surface, Palette::Window, 0.15f);
        COLORREF border = active ? Mix(Palette::Primary, Palette::BorderStrong, 0.22f)
            : hover ? HoverBorder() : Palette::Border;
        FillRound(rt, action.x, action.y, action.w, action.h, 7.0f, bg, 1.0f);
        StrokeRound(rt, action.x, action.y, action.w, action.h, 7.0f, border, active ? 0.95f : 0.58f);
        RectF indicator{ action.x + action.w - 32.0f, action.y + 2.0f, 26.0f, action.h - 4.0f };
        DrawControlText(rt, value, action.x + 14.0f, action.y, action.w - 50.0f, action.h,
            Palette::Text, 12.5f, false, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawControlTextIcon(rt, active ? L"\uE70E" : L"\uE70D", indicator.x, indicator.y + 0.5f,
            indicator.w, indicator.h, active ? Palette::Primary : Palette::TextSoft, 9.5f, DWRITE_TEXT_ALIGNMENT_CENTER);
        buttonHits_.push_back({ buttonId, action });
    }

    void DrawSettingCard(ID2D1RenderTarget* rt, float x, float y, float w, float h,
                         const std::wstring& title, const std::wstring& desc,
                         const std::wstring& value, const std::wstring& buttonId) {
        FillRound(rt, x, y, w, h, 6, Palette::Surface, 1.0f);
        StrokeRound(rt, x, y, w, h, 6, Palette::Border);
        DrawText(rt, title, x + 20, y + 12, 220, 24, Palette::Text, 15, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawText(rt, desc, x + 20, y + 40, w - 282, 42, Palette::TextSoft, 12, false, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawDropdownAction(rt, SettingActionRect(x, y, w), value, buttonId);
    }

    void DrawMarket(ID2D1RenderTarget* rt) {
        DrawPageTitle(rt, Tr(L"page.market", L"Market"),
            ReplaceToken(Tr(L"page.market.subtitle", L"{count} packages available"), L"count", std::to_wstring(static_cast<int>(filtered_.size()))));
        float x = ContentX();
        float y = 124;
        DrawSearchBox(rt, x, y, 386, 38);
        DrawButton(rt, Tr(L"action.refresh", L"Refresh"), L"refresh", x + 400, y, 90, 38, false);
        DrawButton(rt, Tr(L"action.updateIndex", L"Update Index"), L"update_index", x + 500, y, 120, 38, false);
        DrawDropdownAction(rt, MarketCategoryRect(x, ContentW()), CategoryLabel(selectedCategory_), L"category");
        RectF batch = MarketBatchRect(x, ContentW());
        DrawButton(rt, Tr(L"action.batchInstall", L"Batch Install"), L"batch_install", batch.x, y, batch.w, 38, true);

        DrawTableFrame(rt, x, TableTop(), ContentW(), ContentBottom() - TableTop(),
            { L"", Tr(L"table.name", L"Name"), Tr(L"table.version", L"Version"), Tr(L"table.author", L"Author"), Tr(L"table.category", L"Category"), Tr(L"table.status", L"Status"), Tr(L"table.size", L"Size"), Tr(L"table.action", L"Action") },
            marketColumnWidths_, false);

        float rowTop = TableHeaderBottom() - static_cast<float>(marketScroll_);
        for (int fi = 0; fi < static_cast<int>(filtered_.size()); ++fi) {
            int gi = filtered_[fi];
            if (gi < 0 || gi >= static_cast<int>(groups_.size())) continue;
            const auto& g = groups_[gi];
            if (rowTop > ContentBottom() - 6) break;
            if (rowTop + 44 >= TableHeaderBottom()) DrawMarketRow(rt, g, gi, x, rowTop);
            rowTop += 44;
        }
        if (filtered_.empty()) {
            DrawText(rt, Tr(L"empty.packages", L"No packages to display"), x + 24, 280, 260, 30, Palette::TextMuted, 14, false, DWRITE_TEXT_ALIGNMENT_LEADING);
        }
    }

    void DrawCategoryChips(ID2D1RenderTarget* rt, float x, float y, float w) {
        float cx = x;
        float maxX = x + w;
        float gap = 8.0f;
        std::wstring allLabel = CategoryLabel(L"all");
        std::vector<std::wstring> labels = categories_;
        if (labels.empty()) labels = { L"all" };
        for (const auto& cat : labels) {
            std::wstring id = cat;
            std::wstring label = CategoryLabel(cat);
            float chipW = (std::max)(76.0f, MeasureTextWidth(label, 12.0f, true) + 34.0f);
            if (cx + chipW > maxX && cx > x) {
                cx = x;
                y += 34.0f;
            }
            bool active = IEquals(selectedCategory_, id) || (selectedCategory_ == L"all" && IEquals(id, L"all"));
            DrawCategoryChip(rt, { cx, y, chipW, 28.0f }, label, active, L"category:" + id);
            cx += chipW + gap;
        }
    }

    void DrawCategoryChip(ID2D1RenderTarget* rt, RectF rc, const std::wstring& label, bool active, const std::wstring& id) {
        bool hover = hoverButton_ == id;
        COLORREF bg = active ? Palette::PrimarySoft : (hover ? HoverFill() : Palette::Surface);
        COLORREF border = active ? Palette::Primary : Palette::Border;
        COLORREF fg = active ? Palette::Primary : Palette::TextSoft;
        if (hover && !active) border = HoverBorder();
        FillRound(rt, rc.x, rc.y, rc.w, rc.h, 14, bg, 1.0f);
        StrokeRound(rt, rc.x, rc.y, rc.w, rc.h, 14, border);
        DrawControlText(rt, label, rc.x, rc.y, rc.w, rc.h, fg, 11.5f, active, DWRITE_TEXT_ALIGNMENT_CENTER);
        buttonHits_.push_back({ id, rc });
    }

    void DrawInstalled(ID2D1RenderTarget* rt) {
        DrawPageTitle(rt, Tr(L"page.installed", L"Installed"),
            ReplaceToken(Tr(L"page.installed.subtitle", L"{count} packages installed"), L"count", std::to_wstring(installedCount_)));
        float x = ContentX();
        float y = 124;
        DrawButton(rt, Tr(L"action.refresh", L"Refresh"), L"refresh", x, y, 96, 38, false);
        DrawButton(rt, Tr(L"action.batchUninstall", L"Batch Uninstall"), L"batch_uninstall", x + ContentW() - 176, y, 176, 38, true);

        DrawTableFrame(rt, x, TableTop(), ContentW(), ContentBottom() - TableTop(),
            { L"", Tr(L"table.name", L"Name"), Tr(L"table.version", L"Version"), Tr(L"table.author", L"Author"), Tr(L"table.installDate", L"Install Date"), Tr(L"table.action", L"Action") },
            installedColumnWidths_, true);

        float rowTop = TableHeaderBottom() - static_cast<float>(installedScroll_);
        for (int i = 0; i < static_cast<int>(installed_.size()); ++i) {
            const auto& item = installed_[i];
            if (rowTop > ContentBottom() - 6) break;
            if (rowTop + 44 >= TableHeaderBottom()) DrawInstalledRow(rt, item, i, x, rowTop);
            rowTop += 44;
        }
        if (installed_.empty()) {
            DrawText(rt, Tr(L"empty.installed", L"No installed packages"), x + 24, 260, 260, 30, Palette::TextMuted, 14, false, DWRITE_TEXT_ALIGNMENT_LEADING);
        }
    }

    void DrawVersions(ID2D1RenderTarget* rt) {
        int focusedIndex = FocusedVersionIndex();
        size_t totalVersions = 0;
        for (const auto& g : groups_) totalVersions += g.versions.size();
        DrawPageTitle(rt, Tr(L"page.versions", L"Version Management"),
            Tr(L"page.versions.subtitle", L"Click a package header to expand or collapse its versions"));

        float x = ContentX();
        float top = 124.0f;
        float bottom = ContentBottom();
        float gap = 16.0f;
        float leftW = ContentW() * 0.62f - gap;
        float rightW = ContentW() - leftW - gap;
        float leftX = x;
        float rightX = x + leftW + gap;
        float panelH = bottom - top;

        FillRound(rt, leftX, top, leftW, panelH, 7, Palette::Surface, 1.0f);
        StrokeRound(rt, leftX, top, leftW, panelH, 7, Palette::Border);
        FillRound(rt, rightX, top, rightW, panelH, 7, Palette::Surface, 1.0f);
        StrokeRound(rt, rightX, top, rightW, panelH, 7, Palette::Border);

        float listTop = top + 78.0f;
        float listBottom = bottom - 16.0f;

        DrawText(rt, Tr(L"label.history", L"Version history"), leftX + 22, top + 16, leftW - 300, 28,
            Palette::Text, 17, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        std::wstring treeMeta = ReplaceToken(Tr(L"label.packageCount", L"{count} packages"), L"count", std::to_wstring(groups_.size()));
        treeMeta += L" · ";
        treeMeta += ReplaceToken(Tr(L"label.historyCount", L"{count} versions"), L"count", std::to_wstring(totalVersions));
        treeMeta += L" · ";
        treeMeta += ReplaceToken(Tr(L"label.selectedCount", L"{count} selected"), L"count", std::to_wstring(SelectedVersionCount()));
        DrawText(rt, treeMeta, leftX + 22, top + 44, leftW - 44, 22, Palette::TextMuted, 11.5f, false, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawButton(rt, Tr(L"action.addSelected", L"Add selected"), L"version_add_selected", leftX + leftW - 270, top + 18, 124, 30, true);
        DrawButton(rt, Tr(L"action.clearSelection", L"Clear selection"), L"version_clear_selection", leftX + leftW - 138, top + 18, 116, 30, false);

        const float groupH = 52.0f;
        const float versionH = 48.0f;
        const float groupGap = 8.0f;
        float contentH = 0.0f;
        for (const auto& g : groups_) {
            float versionsH = IsVersionGroupCollapsed(g.name) ? 0.0f : static_cast<float>(g.versions.size()) * versionH;
            contentH += groupH + versionsH + groupGap;
        }
        int maxScroll = (std::max)(0, static_cast<int>(contentH - (listBottom - listTop)));
        if (versionScroll_ > maxScroll) versionScroll_ = maxScroll;
        if (groups_.empty()) {
            DrawText(rt, Tr(L"empty.packages", L"No packages to display"), leftX + 24, listTop + 12, leftW - 48, 28,
                Palette::TextMuted, 13, false, DWRITE_TEXT_ALIGNMENT_LEADING);
        } else {
            rt->PushAxisAlignedClip(R(leftX + 8, listTop, leftW - 16, listBottom - listTop), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            float y = listTop - static_cast<float>(versionScroll_);
            for (int gi = 0; gi < static_cast<int>(groups_.size()); ++gi) {
                const auto& g = groups_[gi];
                bool collapsed = IsVersionGroupCollapsed(g.name);
                float versionsH = collapsed ? 0.0f : static_cast<float>(g.versions.size()) * versionH;
                float groupBlockH = groupH + versionsH + groupGap;
                bool activeGroup = gi == focusedIndex && !versionFocusName_.empty();
                if (y + groupBlockH >= listTop && y <= listBottom) {
                    std::wstring toggleId = L"version_toggle:" + std::to_wstring(gi);
                    bool headerHover = hoverButton_ == toggleId;
                    RectF headerRc{ leftX + 16, y + 4, leftW - 32, groupH - 8 };
                    COLORREF headerBg = activeGroup ? Mix(Palette::Primary, Palette::Surface, 0.88f)
                        : (headerHover ? HoverFill() : Mix(Palette::Surface, Palette::SurfaceAlt, 0.36f));
                    FillRound(rt, headerRc.x, headerRc.y, headerRc.w, headerRc.h, 7, headerBg, 1.0f);
                    StrokeRound(rt, headerRc.x, headerRc.y, headerRc.w, headerRc.h, 7,
                        activeGroup ? Mix(Palette::Primary, Palette::Border, 0.22f) : (headerHover ? HoverBorder() : Palette::Border),
                        (activeGroup || headerHover) ? 0.9f : 0.48f);
                    DrawText(rt, collapsed ? L"\u25B8" : L"\u25BE", headerRc.x + 12, headerRc.y + 12, 22, 20,
                        activeGroup ? Palette::Primary : Palette::TextSoft, 12, true, DWRITE_TEXT_ALIGNMENT_CENTER);
                    DrawText(rt, g.name, headerRc.x + 42, headerRc.y + 7, headerRc.w - 220, 21,
                        Palette::Text, 13.5f, true, DWRITE_TEXT_ALIGNMENT_LEADING);
                    std::wstring meta = ReplaceToken(Tr(L"label.historyCount", L"{count} versions"), L"count", std::to_wstring(g.versionCount));
                    if (!g.latestVersion.empty()) meta += L" · " + g.latestVersion;
                    DrawText(rt, meta, headerRc.x + 42, headerRc.y + 28, headerRc.w - 220, 16,
                        Palette::TextMuted, 10.2f, false, DWRITE_TEXT_ALIGNMENT_LEADING);
                    DrawText(rt, CategoryLabel(g.category), headerRc.x + headerRc.w - 166, headerRc.y + 14, 146, 20,
                        Palette::TextSoft, 11.0f, false, DWRITE_TEXT_ALIGNMENT_TRAILING);
                    buttonHits_.push_back({ toggleId, headerRc });
                }

                float childY = y + groupH;
                float lineX = leftX + 43.0f;
                float childX = leftX + 54.0f;
                float childW = leftW - 70.0f;
                if (!collapsed && !g.versions.empty() && childY + static_cast<float>(g.versions.size()) * versionH >= listTop && childY <= listBottom) {
                    float lastMid = childY + static_cast<float>(g.versions.size() - 1) * versionH + versionH * 0.5f;
                    StrokeLine(rt, lineX, childY + 2.0f, lineX, lastMid, Palette::BorderStrong, 1.1f, 0.42f);
                }
                if (!collapsed) {
                    for (size_t vi = 0; vi < g.versions.size(); ++vi) {
                        const auto& v = g.versions[vi];
                        if (childY + versionH >= listTop && childY <= listBottom) {
                            std::wstring addId = L"version_add:" + std::to_wstring(gi) + L":" + std::to_wstring(vi);
                            std::wstring selectId = L"version_select:" + std::to_wstring(gi) + L":" + std::to_wstring(vi);
                            bool hover = hoverButton_ == addId;
                            bool selected = IsVersionSelected(v);
                            float midY = childY + versionH * 0.5f;
                            StrokeLine(rt, lineX, midY, childX + 4.0f, midY, Palette::BorderStrong, 1.1f, 0.42f);
                            FillRound(rt, lineX - 3.0f, midY - 3.0f, 6.0f, 6.0f, 3.0f, Palette::BorderStrong, 0.65f);

                            RectF rowRc{ childX, childY + 4, childW, versionH - 8 };
                            FillRound(rt, rowRc.x, rowRc.y, rowRc.w, rowRc.h, 6,
                                selected ? Mix(Palette::Primary, Palette::Surface, 0.88f) :
                                (hover ? HoverFill() : Mix(Palette::Surface, Palette::SurfaceAlt, 0.18f)), 1.0f);
                            StrokeRound(rt, rowRc.x, rowRc.y, rowRc.w, rowRc.h, 6,
                                selected ? Mix(Palette::Primary, Palette::Border, 0.25f) :
                                (hover ? HoverBorder() : Palette::Border), (hover || selected) ? 0.76f : 0.36f);

                            float addW = 74.0f;
                            float addX = rowRc.x + rowRc.w - addW - 12.0f;
                            RectF check{ rowRc.x + 13.0f, rowRc.y + 11.0f, 16.0f, 16.0f };
                            RectF checkHit{ check.x - 5.0f, check.y - 5.0f, 26.0f, 26.0f };
                            DrawCheck(rt, check, selected);
                            buttonHits_.push_back({ selectId, checkHit });

                            float textX = rowRc.x + 40.0f;
                            DrawText(rt, v.version, textX, rowRc.y + 6, 96, 20, Palette::Text, 12.5f, true, DWRITE_TEXT_ALIGNMENT_LEADING);
                            DrawText(rt, FormatSize(v.size), addX - 82, rowRc.y + 6, 72, 20, Palette::TextMuted, 10.8f, false, DWRITE_TEXT_ALIGNMENT_TRAILING);
                            DrawText(rt, v.description.empty() ? v.author : v.description, textX + 104, rowRc.y + 7,
                                (std::max)(40.0f, addX - textX - 198.0f), 18, Palette::TextMuted, 10.6f, false, DWRITE_TEXT_ALIGNMENT_LEADING);
                            DrawText(rt, CategoryLabel(v.category), textX, rowRc.y + 25, addX - textX - 12, 15,
                                Palette::TextSoft, 9.8f, false, DWRITE_TEXT_ALIGNMENT_LEADING);
                            RectF addRc{ addX, rowRc.y + 8, addW, 26 };
                            DrawButtonRaw(rt, Tr(L"action.addToQueue", L"Add"), addRc, false, hover);
                            buttonHits_.push_back({ addId, addRc });
                        }
                        childY += versionH;
                    }
                }
                y += groupBlockH;
            }
            rt->PopAxisAlignedClip();
        }

        DrawText(rt, Tr(L"label.pendingInstall", L"Pending install"), rightX + 20, top + 16, rightW - 40, 28,
            Palette::Text, 16, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        std::wstring queueMeta = ReplaceToken(Tr(L"label.queueCount", L"{count} selected"), L"count", std::to_wstring(installQueue_.size()));
        queueMeta += L" · ";
        queueMeta += ReplaceToken(Tr(L"label.selectedCount", L"{count} selected"), L"count", std::to_wstring(SelectedQueueCount()));
        DrawText(rt, queueMeta,
            rightX + 20, top + 44, rightW - 40, 22, Palette::TextMuted, 11.5f, false, DWRITE_TEXT_ALIGNMENT_LEADING);

        float actionY = bottom - 52.0f;
        float clearW = 68.0f;
        float removeW = 94.0f;
        float installW = (std::max)(92.0f, rightW - 48.0f - clearW - removeW - 16.0f);
        DrawButton(rt, Tr(L"action.installQueue", L"Install Queue"), L"queue_install", rightX + 20, actionY, installW, 34, true);
        DrawButton(rt, Tr(L"action.removeSelected", L"Remove selected"), L"queue_remove_selected", rightX + 26 + installW, actionY, removeW, 34, false);
        DrawButton(rt, Tr(L"action.clearQueue", L"Clear"), L"queue_clear", rightX + rightW - clearW - 20, actionY, clearW, 34, false);

        float queueTop = top + 78.0f;
        float queueBottom = actionY - 12.0f;
        float queueRowH = 50.0f;
        int queueMaxScroll = (std::max)(0, static_cast<int>(installQueue_.size() * queueRowH - (queueBottom - queueTop)));
        if (queueScroll_ > queueMaxScroll) queueScroll_ = queueMaxScroll;
        if (installQueue_.empty() && queueExitAnims_.empty()) {
            DrawText(rt, Tr(L"empty.queue", L"No versions selected"), rightX + 22, queueTop + 14, rightW - 44, 24,
                Palette::TextMuted, 12.5f, false, DWRITE_TEXT_ALIGNMENT_LEADING);
            return;
        }

        rt->PushAxisAlignedClip(R(rightX + 8, queueTop, rightW - 16, queueBottom - queueTop), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        float qy = queueTop - static_cast<float>(queueScroll_);
        for (size_t i = 0; i < installQueue_.size(); ++i) {
            const auto& item = installQueue_[i];
            if (qy + queueRowH >= queueTop && qy <= queueBottom) {
                std::wstring selectId = L"queue_select:" + std::to_wstring(i);
                bool hover = hoverButton_ == L"queue_remove:" + std::to_wstring(i);
                FillRound(rt, rightX + 14, qy + 3, rightW - 28, queueRowH - 7, 6,
                    item.selected ? Mix(Palette::Primary, Palette::Surface, 0.88f) : Mix(Palette::Surface, Palette::SurfaceAlt, 0.36f), 1.0f);
                StrokeRound(rt, rightX + 14, qy + 3, rightW - 28, queueRowH - 7, 6,
                    item.selected ? Mix(Palette::Primary, Palette::Border, 0.25f) : Palette::Border, item.selected ? 0.74f : 0.46f);
                RectF qCheck{ rightX + 26, qy + 16, 16, 16 };
                DrawCheck(rt, qCheck, item.selected);
                buttonHits_.push_back({ selectId, { qCheck.x - 5, qCheck.y - 5, 26, 26 } });
                DrawText(rt, item.name, rightX + 52, qy + 7, rightW - 126, 20, Palette::Text, 12.5f, true, DWRITE_TEXT_ALIGNMENT_LEADING);
                DrawText(rt, item.version + L" · " + FormatSize(item.size), rightX + 52, qy + 27, rightW - 126, 18,
                    Palette::TextMuted, 10.5f, false, DWRITE_TEXT_ALIGNMENT_LEADING);
                RectF rmRc{ rightX + rightW - 58, qy + 10, 30, 30 };
                DrawText(rt, L"×", rmRc.x, rmRc.y + 1.0f, rmRc.w, rmRc.h - 2.0f,
                    hover ? RGB(220, 70, 70) : Palette::TextMuted, 17.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
                buttonHits_.push_back({ L"queue_remove:" + std::to_wstring(i), rmRc });
            }
            qy += queueRowH;
        }
        ULONGLONG now = GetTickCount64();
        for (const auto& anim : queueExitAnims_) {
            float t = (std::min)(1.0f, static_cast<float>(now - anim.started) / 260.0f);
            float eased = EaseOutCubic(t);
            float alpha = 1.0f - eased;
            float rowY = queueTop + anim.y - static_cast<float>(queueScroll_) - 8.0f * eased;
            if (rowY + queueRowH < queueTop || rowY > queueBottom) continue;
            FillRound(rt, rightX + 14 + 18.0f * t, rowY + 3, rightW - 28, queueRowH - 7, 6,
                Mix(Palette::Surface, Palette::SurfaceAlt, 0.36f), alpha);
            StrokeRound(rt, rightX + 14 + 18.0f * t, rowY + 3, rightW - 28, queueRowH - 7, 6, Palette::Border, 0.38f * alpha);
            DrawText(rt, anim.item.name, rightX + 52 + 18.0f * t, rowY + 7, rightW - 126, 20,
                Palette::Text, 12.5f, true, DWRITE_TEXT_ALIGNMENT_LEADING, alpha);
            DrawText(rt, anim.item.version + L" · " + FormatSize(anim.item.size), rightX + 52 + 18.0f * t, rowY + 27, rightW - 126, 18,
                Palette::TextMuted, 10.5f, false, DWRITE_TEXT_ALIGNMENT_LEADING, alpha);
        }
        rt->PopAxisAlignedClip();
    }

    void DrawSettings(ID2D1RenderTarget* rt) {
        DrawPageTitle(rt, Tr(L"page.settings", L"Settings"), Tr(L"page.settings.subtitle", L"DPI scale, language, theme and backend connection"));
        float x = ContentX();
        wchar_t buf[32];
        swprintf_s(buf, L"%d%%", static_cast<int>(chosenScale_ * 100 + 0.5f));
        DrawSettingCard(rt, x, 132, ContentW(), 86,
            Tr(L"label.dpi", L"DPI Scale"),
            Tr(L"hint.dpi", L"The window is physically resized when DPI changes. Range: 100% to 300%."),
            buf, L"dpi");

        DrawSettingCard(rt, x, 232, ContentW(), 86,
            Tr(L"label.theme", L"Theme"),
            Tr(L"hint.theme", L"Theme files are loaded from the themes folder."),
            themeName_, L"theme");

        DrawSettingCard(rt, x, 332, ContentW(), 86,
            Tr(L"label.language", L"Language"),
            Tr(L"hint.language", L"Language files are loaded from the lang folder."),
            languageName_, L"language");

    }

    void DrawAbout(ID2D1RenderTarget* rt) {
        DrawPageTitle(rt, Tr(L"page.about", L"About"), Tr(L"page.about.subtitle", L"Windows package management frontend"));
        float x = ContentX();
        FillRound(rt, x, 134, ContentW(), 150, 5, Palette::Surface, 1.0f);
        StrokeRound(rt, x, 134, ContentW(), 150, 5, Palette::Border);
        DrawText(rt, Tr(L"about.title", L"GPM"), x + 24, 158, 300, 34, Palette::Text, 21, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawText(rt, Tr(L"about.developer", L"Developer: ArthurX"), x + 24, 193, 300, 24, Palette::TextSoft, 13, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawText(rt, Tr(L"about.body", L"Windows package management frontend."),
            x + 24, 226, ContentW() - 48, 40, Palette::TextSoft, 13, false, DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    void DrawFooter(ID2D1RenderTarget* rt) {
        float y = static_cast<float>(BaseH() - kFooterH);
        if (statusPanelOpen_) DrawStatusPanel(rt);
        float sw = SidebarW();
        // No footer background. The window's Window colour shows through
        // so the footer doesn't read as a separate "block" at the
        // bottom. Sidebar's own right edge is the only 1px vertical
        // divider on the left; this line is the top boundary of the
        // footer band and stops at the sidebar's right edge (not
        // running under it).
        FillRect(rt, sw, y, BaseW() - sw, 1, Palette::Border);
        float x = ContentX();
        int warnCount = 0;
        int errorCount = 0;
        for (const auto& t : toasts_) {
            if (t.severity == L"error") ++errorCount;
            else if (t.severity == L"warning") ++warnCount;
        }
        std::wstring counts = ReplaceToken(ReplaceToken(Tr(L"footer.counts", L"{warn} warnings  {error} errors"),
            L"warn", std::to_wstring(warnCount)), L"error", std::to_wstring(errorCount));
        float countsW = 166.0f;
        DrawText(rt, counts, x, y + 17.0f, countsW - 10.0f, 20.0f, Palette::TextMuted, 11.0f, false, DWRITE_TEXT_ALIGNMENT_LEADING);

        float barX = x + countsW;
        bool progressVisible = progress_.active || progress_.percent > 0;
        float cancelW = progress_.active ? 72.0f : 0.0f;
        float barW = ContentW() - countsW - 54.0f - cancelW;
        float pct = progressVisualInitialized_ ? progressVisual_ : static_cast<float>((std::max)(0, (std::min)(100, progress_.percent))) / 100.0f;
        pct = Clamp01(pct);
        int pctInt = static_cast<int>(pct * 100.0f + 0.5f);
        float dotPulse = progress_.active ? (0.72f + 0.28f * (0.5f + 0.5f * std::sin(progressPhase_ * 6.2831853f))) : 0.52f;
        COLORREF dotColor = progress_.active ? Palette::Primary
            : (pctInt >= 100 ? Palette::Success : Palette::TextMuted);
        FillRound(rt, barX, y + 19.5f, 8.0f, 8.0f, 4.0f, dotColor, dotPulse);

        float textX = barX + 20.0f;
        float percentW = 54.0f;
        float metaW = (std::max)(104.0f, (std::min)(238.0f, barW * 0.28f));
        std::wstring progressTitle = ProgressTitleText();
        std::wstring progressMeta = ProgressMetaText();
        std::wstring percentText = std::to_wstring(pctInt) + L"%";
        COLORREF progressTextColor = progressVisible ? Palette::Text : Palette::TextMuted;
        DrawText(rt, progressTitle, textX, y + 7.0f, barW - 28.0f - metaW - percentW, 19.0f,
            progressTextColor, 11.2f, progressVisible, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawText(rt, progressMeta, barX + barW - metaW - percentW - 8.0f, y + 7.0f, metaW, 19.0f,
            Palette::TextMuted, 10.0f, false, DWRITE_TEXT_ALIGNMENT_TRAILING);
        DrawText(rt, percentText, barX + barW - percentW, y + 7.0f, percentW, 19.0f,
            progressTextColor, 11.2f, true, DWRITE_TEXT_ALIGNMENT_TRAILING);

        float trackX = textX;
        float trackY = y + 30.0f;
        float trackW = barW - 14.0f;
        float trackH = 7.0f;
        FillRound(rt, trackX, trackY, trackW, trackH, 3.5f, Palette::ProgressTrack, 0.90f);
        FillRound(rt, trackX, trackY, trackW, 1.0f, 0.5f, RGB(255, 255, 255), 0.18f);
        if (progressVisible) {
            float fillW = (std::max)(trackH, trackW * pct);
            FillRound(rt, trackX, trackY, fillW, trackH, 3.5f, Mix(Palette::Primary, Palette::PrimaryHover, progress_.active ? 0.10f : 0.0f), 1.0f);
            FillRound(rt, trackX, trackY, fillW, 1.5f, 0.75f, RGB(255, 255, 255), 0.15f);
            if (progress_.active && fillW > 36.0f) {
                float shineX = trackX - 46.0f + (trackW + 92.0f) * progressPhase_;
                rt->PushAxisAlignedClip(R(trackX, trackY, trackW, trackH), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                FillRound(rt, shineX, trackY + 1.0f, 40.0f, trackH - 2.0f, 2.5f, RGB(255, 255, 255), 0.20f);
                rt->PopAxisAlignedClip();
            }
        }
        if (progress_.active) {
            RectF cancelRc{ x + ContentW() - 40.0f - cancelW, y + 13.0f, cancelW - 10.0f, 28.0f };
            bool cancelHover = hoverButton_ == L"cancel_task";
            FillRound(rt, cancelRc.x, cancelRc.y, cancelRc.w, cancelRc.h, 7.0f,
                cancelHover ? HoverFill() : Mix(Palette::Surface, Palette::SurfaceAlt, 0.35f), 1.0f);
            StrokeRound(rt, cancelRc.x, cancelRc.y, cancelRc.w, cancelRc.h, 7.0f,
                cancelHover ? HoverBorder() : Palette::Border, cancelHover ? 0.85f : 0.58f);
            DrawText(rt, Tr(L"action.cancelTask", L"Cancel"), cancelRc.x, cancelRc.y + 5.0f, cancelRc.w, cancelRc.h - 8.0f,
                cancelHover ? Palette::Text : Palette::TextSoft, 11.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER);
            buttonHits_.push_back({ L"cancel_task", cancelRc });
        }
        RectF toggle{ x + ContentW() - 40, y + 12, 28, 28 };
        bool toggleHover = hoverButton_ == L"status_toggle";
        // No frame on hover. The chevron itself paints in primary
        // blue when hovered, and the existing dir flip below rotates
        // it 180° as the panel opens/closes (statusPanelToggledAt_
        // drives the animation; toggling open == arrow flips up).
        float progress = statusPanelOpen_ ? 1.0f : 0.0f;
        if (statusPanelToggledAt_ != 0) {
            float animT = Clamp01(static_cast<float>(GetTickCount64() - statusPanelToggledAt_) / 200.0f);
            float eased = EaseOutCubic(animT);
            progress = statusPanelOpen_ ? eased : (1.0f - eased);
        }
        float centerX = toggle.x + toggle.w * 0.5f;
        float centerY = toggle.y + toggle.h * 0.5f + 0.5f;
        float halfW = 4.0f;
        float halfH = 2.8f;
        float dir = 1.0f - progress * 2.0f;
        if (toggleHover) FillRound(rt, toggle.x, toggle.y, toggle.w, toggle.h, 7.0f, HoverFill(), 1.0f);
        COLORREF chevronColor = toggleHover ? Palette::Text : Palette::TextSoft;
        float chevronAlpha = toggleHover ? 1.0f : 0.92f;
        float chevronStroke = toggleHover ? 1.6f : 1.35f;
        StrokeLine(rt, centerX - halfW, centerY - dir * halfH, centerX, centerY + dir * halfH,
            chevronColor, chevronStroke, chevronAlpha);
        StrokeLine(rt, centerX, centerY + dir * halfH, centerX + halfW, centerY - dir * halfH,
            chevronColor, chevronStroke, chevronAlpha);
        buttonHits_.push_back({ L"status_toggle", toggle });
    }

    void DrawStatusPanel(ID2D1RenderTarget* rt) {
        float progress = statusPanelOpen_ ? 1.0f : 0.0f;
        if (statusPanelToggledAt_ != 0) {
            float animT = Clamp01(static_cast<float>(GetTickCount64() - statusPanelToggledAt_) / 200.0f);
            float eased = EaseOutCubic(animT);
            progress = statusPanelOpen_ ? eased : (1.0f - eased);
        }
        if (progress <= 0.001f) return;

        float sw = SidebarW();

        // Mica-style backdrop on the body area: cool-blue tint and
        // a darker dim layer, both fading with the panel so the
        // rest of the window "steps back" while the drawer opens.
        FillRect(rt, sw, kTitleH, BaseW() - sw, BaseH() - kTitleH - kFooterH,
            Mix(Palette::Window, Palette::Primary, 0.05f), 0.55f * progress);
        FillRect(rt, sw, kTitleH, BaseW() - sw, BaseH() - kTitleH - kFooterH,
            RGB(14, 22, 36), 0.10f * progress);

        // Panel geometry: full width from sidebar to window edge;
        // bottom hugs the footer top (no gap), all corners square.
        float panelX = sw;
        float panelW = BaseW() - sw;
        float panelBottom = static_cast<float>(BaseH() - kFooterH);
        float panelH = 268.0f;
        float lift = (1.0f - progress) * 26.0f;
        float panelY = panelBottom - panelH + lift;

        FillRect(rt, panelX, panelY, panelW, panelH, Palette::Surface, 0.98f * progress);
        StrokeRect(rt, panelX, panelY, panelW, panelH, Palette::Border, 0.88f * progress);

        // 24 px gutters for content. listTop is anchored to the
        // panel's own top + header height so the rows can't escape
        // the panel's rounded corners when the panel is short on
        // warnings (i.e. when the bottom of the last row sits
        // above the footer's hairline).
        float padL = 24.0f;
        float padR = 24.0f;
        float headerH = 70.0f;
        float listTop = panelY + headerH;
        float listBottom = panelBottom - 16.0f;
        float rowH = 42.0f;
        int totalWarn = 0;
        for (const auto& t : toasts_) {
            if (t.severity == L"warning" || t.severity == L"error") ++totalWarn;
        }
        float totalHeight = totalWarn * rowH;
        float visibleHeight = (std::max)(0.0f, listBottom - listTop);
        float maxScroll = (std::max)(0.0f, totalHeight - visibleHeight);
        if (statusPanelScroll_ < 0) statusPanelScroll_ = 0;
        if (statusPanelScroll_ > static_cast<int>(maxScroll)) statusPanelScroll_ = static_cast<int>(maxScroll);

        // Header: title + subtitle + "Export logs" button on the right.
        DrawText(rt, Tr(L"panel.warnings", L"Warnings and errors"),
            panelX + padL, panelY + 14, 320, 28,
            Palette::Text, 17, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawText(rt, Tr(L"panel.warnings.subtitle",
            L"Recent backend warnings and failed operations stay here instead of popping over the UI."),
            panelX + padL, panelY + 42, panelW - padL - padR - 130, 24,
            Palette::TextMuted, 11, false, DWRITE_TEXT_ALIGNMENT_LEADING);
        RectF exportRc{ panelX + panelW - padR - 110.0f, panelY + 18.0f, 110.0f, 30.0f };
        bool exportHover = hoverButton_ == L"logs_export";
        FillRound(rt, exportRc.x, exportRc.y, exportRc.w, exportRc.h, 6,
            exportHover ? HoverFill() : Palette::SurfaceAlt,
            1.0f);
        COLORREF exportText = exportHover ? Palette::Text : Palette::Text;
        DrawText(rt, Tr(L"action.exportLogs", L"Export logs"),
            exportRc.x, exportRc.y + 6, exportRc.w, exportRc.h - 8,
            exportText, 12, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        buttonHits_.push_back({ L"logs_export", exportRc });

        // Warning/error rows.
        float rowsLeft = panelX + padL;
        float rowsW = panelW - padL - padR;
        float rowY = listTop - static_cast<float>(statusPanelScroll_);
        int shown = 0;
        for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it) {
            if (it->severity != L"warning" && it->severity != L"error") continue;
            if (rowY + 34 < listTop) {
                rowY += rowH;
                ++shown;
                continue;
            }
            if (rowY > listBottom) break;
            COLORREF accent = it->severity == L"error" ? Palette::Error : Palette::Warning;
            FillRect(rt, rowsLeft, rowY, rowsW, 34, Mix(Palette::Surface, accent, 0.08f), 1.0f);
            FillRect(rt, rowsLeft, rowY, 4, 34, accent, 1.0f);
            DrawText(rt, it->time, rowsLeft + 16, rowY, 58, 34,
                Palette::TextMuted, 10, false, DWRITE_TEXT_ALIGNMENT_LEADING);
            DrawText(rt, it->title, rowsLeft + 78, rowY, 150, 34,
                accent, 12, true, DWRITE_TEXT_ALIGNMENT_LEADING);
            DrawText(rt, it->message, rowsLeft + 234, rowY, rowsW - 250, 34,
                Palette::TextSoft, 12, false, DWRITE_TEXT_ALIGNMENT_LEADING);
            rowY += rowH;
            ++shown;
        }
        if (shown == 0) {
            DrawText(rt, Tr(L"empty.warnings", L"No warnings or errors"),
                rowsLeft, panelY + 96, 260, 30,
                Palette::TextMuted, 13, false, DWRITE_TEXT_ALIGNMENT_LEADING);
        } else if (maxScroll > 1.0f) {
            float trackX = panelX + panelW - 10.0f;
            float trackY = listTop;
            float trackH = visibleHeight;
            float thumbH = (std::max)(34.0f, trackH * visibleHeight / totalHeight);
            float thumbY = trackY + (trackH - thumbH) * (statusPanelScroll_ / maxScroll);
            FillRound(rt, trackX, trackY, 4, trackH, 2, Palette::SurfaceAlt, 0.90f);
            FillRound(rt, trackX, thumbY, 4, thumbH, 2, Palette::BorderStrong, 0.95f);
        }
    }

    void DrawSparks(ID2D1RenderTarget* rt) {
        ULONGLONG now = GetTickCount64();
        for (const auto& s : sparks_) {
            float age = static_cast<float>(now - s.created);
            float t = Clamp01(age / 460.0f);
            float e = EaseOutCubic(t);
            float alpha = (1.0f - t) * 0.55f;
            float x = s.x + s.vx * e * 22.0f;
            float y = s.y + s.vy * e * 22.0f;
            FillCircle(rt, x, y, 2.2f * (1.0f - t) + 0.8f, s.color, alpha);
        }
    }

    void DrawDialog(ID2D1RenderTarget* rt) {
        float sw = SidebarW();
        FillRect(rt, sw, kTitleH, BaseW() - sw, BaseH() - kTitleH, RGB(20, 24, 32), 0.30f);
        float w = 440;
        float h = 178;
        float x = sw + (BaseW() - sw - w) * 0.5f;
        float y = 210;
        FillRound(rt, x + 3, y + 5, w, h, 5, RGB(20, 26, 36), 0.12f);
        FillRound(rt, x, y, w, h, 5, Palette::Surface, 1.0f);
        StrokeRound(rt, x, y, w, h, 5, Palette::Border);
        DrawText(rt, dialog_.title.empty() ? L"GPM" : dialog_.title, x + 22, y + 18, w - 44, 30,
            Palette::Text, 18, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawText(rt, dialog_.message, x + 22, y + 58, w - 44, 54,
            Palette::TextSoft, 13, false, DWRITE_TEXT_ALIGNMENT_LEADING);
        float bw = 116;
        float gap = 12;
        int n = dialog_.options.empty() ? 1 : static_cast<int>(dialog_.options.size());
        float bx = x + w - 22 - n * bw - (n - 1) * gap;
        float by = y + h - 54;
        if (dialog_.options.empty()) dialog_.options.push_back(L"OK");
        for (int i = 0; i < n; ++i) {
            RectF br{ bx + i * (bw + gap), by, bw, 34 };
            bool primary = i == 0;
            DrawButtonRaw(rt, dialog_.options[i], br, primary, hoverDialogButton_ == i);
            dialogButtonHits_.push_back({ dialog_.options[i], br });
        }
    }

    void DrawWindowTransition(ID2D1RenderTarget* rt) {
        float progress = WindowAnimProgress();
        if (progress <= 0.001f) return;
        FillRound(rt, 0, 0, BaseW(), BaseH(), kBodyRadius, Palette::Window, 0.05f * progress);
        StrokeRound(rt, 0.5f, 0.5f, BaseW() - 1, BaseH() - 1, kBodyRadius,
            Mix(Palette::BorderStrong, Palette::Window, 0.35f), 0.20f * (1.0f - progress));
    }

    bool EnsureWatermarkBitmap(ID2D1RenderTarget* rt) {
        if (!rt || watermarkPath_.empty()) return false;
        if (watermarkRenderTarget_ != rt) {
            watermarkBitmap_.Reset();
            watermarkRenderTarget_ = rt;
            watermarkLoadFailed_ = false;
        }
        if (!watermarkBitmap_ && !watermarkLoadFailed_) {
            watermarkBitmap_.Attach(LoadBitmapFile(rt, watermarkPath_));
            watermarkLoadFailed_ = !watermarkBitmap_;
        }
        return watermarkBitmap_.Get() != nullptr;
    }

    void DrawWatermark(ID2D1RenderTarget* rt) {
        // Smoothly fade the watermark with the status drawer so the
        // brand mark and the warning rows don't compete for visual
        // attention. Hard-skipping at >0.05 looked like a pop; an
        // alpha multiplier keeps the close transition graceful.
        float panelProgress = 0.0f;
        if (statusPanelOpen_) panelProgress = 1.0f;
        if (statusPanelToggledAt_ != 0) {
            float animT = Clamp01(static_cast<float>(GetTickCount64() - statusPanelToggledAt_) / 200.0f);
            panelProgress = statusPanelOpen_ ? EaseOutCubic(animT) : (1.0f - EaseOutCubic(animT));
        }
        float fade = 1.0f - panelProgress;
        if (fade <= 0.001f) return;
        if (!EnsureWatermarkBitmap(rt)) return;
        D2D1_SIZE_F size = watermarkBitmap_->GetSize();
        if (size.width <= 1.0f || size.height <= 1.0f) return;
        float targetH = BaseH() * 0.52f;
        float scale = targetH / size.height;
        float w = size.width * scale;
        float h = size.height * scale;
        float x = BaseW() - w - 26.0f;
        float y = BaseH() - h - 34.0f;
        rt->DrawBitmap(watermarkBitmap_.Get(), R(x, y, w, h),
            0.09f * fade, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }

    void DrawSplash(ID2D1RenderTarget* rt) {
        BeginSplashPaint();
        float alpha = SplashOverlayAlpha();
        if (alpha <= 0.001f) return;
        FillRound(rt, 0, 0, BaseW(), BaseH(), kBodyRadius, Palette::Window, alpha);
        FillRect(rt, 0, 0, BaseW(), BaseH(), Mix(Palette::Window, Palette::PrimarySoft, 0.10f), 0.55f * alpha);
        float reveal = SplashSidebarReveal();
        if (reveal > 0.001f) {
            float wipeW = SidebarW() * reveal;
            FillBottomLeftRect(rt, { 0, kTitleH, wipeW, static_cast<float>(BaseH() - kTitleH) }, kBodyRadius, Palette::Sidebar, alpha);
        }
        float cx = BaseW() * 0.5f;
        float intro = splashDismissed_ ? 1.0f : EaseOutCubic(Clamp01(static_cast<float>(GetTickCount64() - SplashClockStart()) / kSplashIntroMs));
        float rise = (1.0f - intro) * 12.0f;
        float contentAlpha = alpha * (0.58f + 0.42f * intro);
        float iconH = 132.0f + 10.0f * intro;
        float blockTop = BaseH() * 0.5f - (iconH + 106.0f) * 0.5f - rise;
        if (EnsureWatermarkBitmap(rt)) {
            D2D1_SIZE_F size = watermarkBitmap_->GetSize();
            if (size.width > 1.0f && size.height > 1.0f) {
                float scale = iconH / size.height;
                float w = size.width * scale;
                float h = size.height * scale;
                float x = cx - w * 0.5f;
                float y = blockTop;
                rt->DrawBitmap(watermarkBitmap_.Get(), D2D1::RectF(ScaleF(x), ScaleF(y), ScaleF(x + w), ScaleF(y + h)),
                    contentAlpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
        }
        DrawText(rt, Tr(L"splash.title", L"GPM"), cx - 180.0f, blockTop + iconH + 24.0f, 360.0f, 42.0f, Palette::Text, 31.0f, true, DWRITE_TEXT_ALIGNMENT_CENTER, contentAlpha);
        DrawText(rt, Tr(L"splash.body", L"Windows package management, redrawn for a cleaner desktop flow."), cx - 450.0f, blockTop + iconH + 66.0f, 900.0f, 28.0f,
            Palette::TextSoft, 13.0f, false, DWRITE_TEXT_ALIGNMENT_CENTER, contentAlpha * 0.92f);
    }

    struct IconBitmapCacheEntry {
        std::wstring key;
        float width = 0.0f;
        float height = 0.0f;
        Detail::ComPtr<ID2D1Bitmap> bitmap;
    };

    IconBitmapCacheEntry* FindOrCreateIconBitmap(ID2D1RenderTarget* rt, const std::wstring& text,
        COLORREF color, float size, float alpha) {
        if (!rt || text.empty()) return nullptr;
        if (iconBitmapRenderTarget_ != rt) {
            iconBitmapCache_.clear();
            iconBitmapRenderTarget_ = rt;
        }

        int scaledSize = (std::max)(1, static_cast<int>(ScaleF(size) + 0.5f));
        int alphaByte = (std::max)(0, (std::min)(255, static_cast<int>(alpha * 255.0f + 0.5f)));
        std::wstring key = text + L"|" + std::to_wstring(color) + L"|" +
            std::to_wstring(scaledSize) + L"|" + std::to_wstring(alphaByte);

        for (auto& entry : iconBitmapCache_) {
            if (entry.key == key) return &entry;
        }

        HDC dc = CreateCompatibleDC(nullptr);
        if (!dc) return nullptr;

        HFONT font = CreateFontW(-scaledSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, kIconFontFamily);
        if (!font) {
            DeleteDC(dc);
            return nullptr;
        }

        HGDIOBJ oldFont = SelectObject(dc, font);
        WORD glyphIndex = 0xFFFF;
        if (GetGlyphIndicesW(dc, text.c_str(), static_cast<int>(text.size()), &glyphIndex, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR ||
            glyphIndex == 0xFFFF) {
            SelectObject(dc, oldFont);
            DeleteObject(font);
            DeleteDC(dc);
            return nullptr;
        }

        MAT2 mat{};
        mat.eM11.value = 1;
        mat.eM22.value = 1;

        GLYPHMETRICS gm{};
        DWORD bufSize = GetGlyphOutlineW(dc, glyphIndex, GGO_GLYPH_INDEX | GGO_GRAY8_BITMAP, &gm, 0, nullptr, &mat);
        if (bufSize == GDI_ERROR || gm.gmBlackBoxX == 0 || gm.gmBlackBoxY == 0) {
            SelectObject(dc, oldFont);
            DeleteObject(font);
            DeleteDC(dc);
            return nullptr;
        }

        std::vector<BYTE> glyph(bufSize);
        if (GetGlyphOutlineW(dc, glyphIndex, GGO_GLYPH_INDEX | GGO_GRAY8_BITMAP, &gm, bufSize, glyph.data(), &mat) == GDI_ERROR) {
            SelectObject(dc, oldFont);
            DeleteObject(font);
            DeleteDC(dc);
            return nullptr;
        }

        const UINT32 width = gm.gmBlackBoxX;
        const UINT32 height = gm.gmBlackBoxY;
        const UINT32 srcStride = ((width + 3u) / 4u) * 4u;
        const UINT32 dstStride = width * 4u;
        std::vector<BYTE> pixels(dstStride * height, 0);
        BYTE baseR = GetRValue(color);
        BYTE baseG = GetGValue(color);
        BYTE baseB = GetBValue(color);

        for (UINT32 yy = 0; yy < height; ++yy) {
            for (UINT32 xx = 0; xx < width; ++xx) {
                BYTE coverage = glyph[yy * srcStride + xx];
                if (coverage > 64) coverage = 64;
                BYTE a = static_cast<BYTE>((coverage * alphaByte + 32) / 64);
                size_t off = static_cast<size_t>(yy) * dstStride + xx * 4u;
                pixels[off + 0] = static_cast<BYTE>((baseB * a + 127) / 255);
                pixels[off + 1] = static_cast<BYTE>((baseG * a + 127) / 255);
                pixels[off + 2] = static_cast<BYTE>((baseR * a + 127) / 255);
                pixels[off + 3] = a;
            }
        }

        ID2D1Bitmap* bitmap = nullptr;
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        HRESULT hr = rt->CreateBitmap(D2D1::SizeU(width, height), pixels.data(), dstStride, &props, &bitmap);

        SelectObject(dc, oldFont);
        DeleteObject(font);
        DeleteDC(dc);
        if (FAILED(hr) || !bitmap) return nullptr;

        iconBitmapCache_.push_back({});
        auto& entry = iconBitmapCache_.back();
        entry.key = key;
        entry.width = static_cast<float>(width) / userScale_;
        entry.height = static_cast<float>(height) / userScale_;
        entry.bitmap.Attach(bitmap);
        return &entry;
    }

    void DrawControlTextIcon(ID2D1RenderTarget* rt, const std::wstring& text, float x, float y, float w, float h,
        COLORREF color, float size, DWRITE_TEXT_ALIGNMENT align, float alpha = 1.0f) {
        if (text.empty()) return;
        auto* entry = FindOrCreateIconBitmap(rt, text, color, size, alpha);
        if (!entry || !entry->bitmap) return;

        float drawX = x;
        if (align == DWRITE_TEXT_ALIGNMENT_CENTER) {
            drawX = x + (w - entry->width) * 0.5f;
        } else if (align == DWRITE_TEXT_ALIGNMENT_TRAILING) {
            drawX = x + w - entry->width;
        }
        float drawY = y + (h - entry->height) * 0.5f;
        rt->DrawBitmap(entry->bitmap.Get(),
            D2D1::RectF(ScaleF(drawX + kShadowPad), ScaleF(drawY + kShadowPad),
                ScaleF(drawX + kShadowPad + entry->width), ScaleF(drawY + kShadowPad + entry->height)),
            1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }

    void DrawFluentIcon(ID2D1RenderTarget* rt, const std::wstring& glyph, RectF rc,
        COLORREF color, float size, bool hover = false, float alpha = 1.0f) {
        if (hover) {
            FillRound(rt, rc.x, rc.y, rc.w, rc.h, 8.0f, HoverFill(), 1.0f);
        }
        DrawControlTextIcon(rt, glyph, rc.x, rc.y, rc.w, rc.h, color, size, DWRITE_TEXT_ALIGNMENT_CENTER, alpha);
    }

    void DrawStat(ID2D1RenderTarget* rt, float x, float y, const std::wstring& label, int value, COLORREF accent, float w = 168.0f) {
        FillRound(rt, x, y, w, 72, 5, Palette::Surface, 1.0f);
        StrokeRound(rt, x, y, w, 72, 5, Palette::Border);
        FillRound(rt, x + 14, y + 18, 6, 36, 3, accent, 1.0f);
        DrawText(rt, std::to_wstring(value), x + 32, y + 12, w - 48, 32, Palette::Text, 24, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawText(rt, label, x + 32, y + 42, w - 48, 22, Palette::TextSoft, 12, false, DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    void DrawSearchBox(ID2D1RenderTarget* rt, float x, float y, float w, float h) {
        RectF rc{ x, y, w, h };
        bool hover = hoverButton_ == L"search_box" || hoverButton_ == L"clear_search";
        COLORREF bg = searchFocused_ ? Mix(Palette::SurfaceAlt, Palette::PrimarySoft, 0.10f)
            : hover ? Mix(Palette::Surface, Palette::SurfaceAlt, 0.52f)
            : Mix(Palette::Surface, Palette::Window, 0.12f);
        COLORREF border = searchFocused_ ? Mix(Palette::Primary, Palette::BorderStrong, 0.30f)
            : hover ? HoverBorder() : Palette::Border;
        FillRound(rt, x, y, w, h, 7.0f, bg, 1.0f);
        StrokeRound(rt, x, y, w, h, 7.0f, border);

        RectF iconRc{ x + 12.0f, y + 6.0f, 24.0f, 24.0f };
        DrawControlTextIcon(rt, L"\uE721", iconRc.x, iconRc.y, iconRc.w, iconRc.h,
            searchFocused_ ? Palette::Primary : Palette::TextSoft, 12.5f, DWRITE_TEXT_ALIGNMENT_CENTER,
            searchFocused_ ? 0.96f : 0.88f);

        RectF clearAction{};
        bool hasClear = !search_.empty();
        float clearReserve = hasClear ? 34.0f : 12.0f;
        if (hasClear) {
            clearAction = { x + w - 34.0f, y + 5.0f, 26.0f, 26.0f };
            DrawFluentIcon(rt, L"\uE711", clearAction, Palette::TextSoft, 12.0f, hoverButton_ == L"clear_search");
            buttonHits_.push_back({ L"clear_search", clearAction });
        }

        float textX = x + 42.0f;
        float textW = (std::max)(72.0f, w - 42.0f - clearReserve);
        EnsureSearchCaretVisible(textW - 10.0f);
        buttonHits_.push_back({ L"search_box", rc });

        if (search_.empty()) {
            DrawControlText(rt, Tr(L"hint.search", L"Search packages..."), textX, y, textW, h,
                Palette::TextMuted, 13.0f, false, DWRITE_TEXT_ALIGNMENT_LEADING);
            return;
        }

        rt->PushAxisAlignedClip(R(textX, y + 1.0f, textW, h - 2.0f), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        float drawX = textX - searchScrollX_;
        float drawW = (std::max)(textW + searchScrollX_ + 28.0f, MeasureTextWidth(search_, 13.0f, false) + 20.0f);
        DrawControlText(rt, search_, drawX, y, drawW, h, Palette::Text, 13.0f, false, DWRITE_TEXT_ALIGNMENT_LEADING);
        if (searchFocused_ && caretOn_) {
            float caretX = textX + MeasureSearchPrefix(searchCaretIndex_) - searchScrollX_;
            StrokeLine(rt, caretX, y + 9.0f, caretX, y + h - 9.0f, Palette::Primary, 1.2f);
        }
        rt->PopAxisAlignedClip();
    }

    void DrawDropdownMenu(ID2D1RenderTarget* rt, RectF anchor) {
        if (activeMenu_.empty() && menuOpenProgress_ <= 0.001f) return;
        float open = EaseOutCubic(Clamp01(menuOpenProgress_));
        float x = anchor.x;
        float y = anchor.y + anchor.h + 6.0f;
        float w = anchor.w;
        float itemH = 36.0f;
        int count = static_cast<int>(menuItems_.size());
        float popupH = itemH * static_cast<float>(count);
        float revealH = (std::max)(1.0f, popupH * open);
        float slide = (1.0f - open) * 8.0f;
        float alpha = 0.16f + 0.84f * open;
        FillRound(rt, x + 1.0f, y + 4.0f, w, popupH, 8.0f, RGB(10, 16, 28), 0.12f * alpha);
        FillRound(rt, x + 0.5f, y + 2.0f, w, popupH, 8.0f, RGB(15, 23, 35), 0.06f * alpha);
        FillRound(rt, x, y, w, popupH, 8.0f, Palette::Surface, alpha);
        StrokeRound(rt, x, y, w, popupH, 8.0f, Mix(Palette::Border, Palette::BorderStrong, 0.30f), 0.95f * alpha);
        rt->PushAxisAlignedClip(R(x, y, w, revealH), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (menuHoverAlpha_ > 0.01f && !hoverMenuItem_.empty()) {
            float hoverY = y + menuHoverY_;
            FillRound(rt, x + 4.0f, hoverY + 3.0f - slide, w - 8.0f, itemH - 6.0f, 6.0f,
                HoverFill(), menuHoverAlpha_ * alpha * open);
        }
        for (int i = 0; i < count; ++i) {
            float itemAlpha = Clamp01((open - i * 0.08f) / 0.72f);
            if (itemAlpha <= 0.001f) continue;
            float iy = y + i * itemH - slide;
            bool current = false;
            if (activeMenu_ == L"dpi") current = static_cast<int>(chosenScale_ * 100 + 0.5f) == std::stoi(menuItems_[i].id);
            else if (activeMenu_ == L"theme") current = menuItems_[i].id == themeName_;
            else if (activeMenu_ == L"language") current = menuItems_[i].id == languageName_;
            else if (activeMenu_ == L"category") current = IEquals(menuItems_[i].id, selectedCategory_);
            if (current) {
                FillRound(rt, x + 4.0f, iy + 8.0f, 3.0f, itemH - 16.0f, 1.5f, Palette::Primary, itemAlpha);
            }
            DrawControlText(rt, menuItems_[i].label, x + 16.0f, iy, w - 28.0f, itemH,
                current ? Palette::Text : Palette::TextSoft, 12.5f, current, DWRITE_TEXT_ALIGNMENT_LEADING, itemAlpha);
            menuHits_.push_back({ menuItems_[i].id, { x, iy, w, itemH } });
        }
        rt->PopAxisAlignedClip();
    }

    void DrawButton(ID2D1RenderTarget* rt, const std::wstring& text, const std::wstring& id,
                    float x, float y, float w, float h, bool primary) {
        RectF rc{ x, y, w, h };
        DrawButtonRaw(rt, text, rc, primary, hoverButton_ == id);
        buttonHits_.push_back({ id, rc });
    }

    void DrawButtonRaw(ID2D1RenderTarget* rt, const std::wstring& text, RectF rc, bool primary, bool hover) {
        float radius = primary ? 8.0f : 7.0f;
        COLORREF bg = primary ? Palette::Primary : Palette::Surface;
        COLORREF fg = primary ? RGB(255, 255, 255) : Palette::Text;
        COLORREF bd = primary ? Mix(Palette::Primary, RGB(255, 255, 255), 0.10f) : Palette::Border;
        if (hover) {
            bg = primary ? Mix(Palette::Primary, HoverFill(), 0.16f) : HoverFill();
            bd = primary ? Mix(Palette::Primary, HoverBorder(), 0.18f) : HoverBorder();
        }
        FillRound(rt, rc.x, rc.y, rc.w, rc.h, radius, bg, 1.0f);
        StrokeRound(rt, rc.x, rc.y, rc.w, rc.h, radius, bd);
        if (!text.empty()) DrawControlText(rt, text, rc.x, rc.y, rc.w, rc.h, fg, 12, primary, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    void DrawTableFrame(ID2D1RenderTarget* rt, float x, float y, float w, float h,
                        const std::vector<std::wstring>& headers, const std::vector<float>& widths, bool installed) {
        FillRound(rt, x, y, w, h, 5, Palette::Surface, 1.0f);
        StrokeRound(rt, x, y, w, h, 5, Palette::Border);
        FillRound(rt, x, y, w, 42, 5, Palette::SurfaceAlt, 1.0f);
        FillRect(rt, x, y + 36, w, 6, Palette::SurfaceAlt);
        float cx = x;
        for (size_t i = 0; i < headers.size() && i < widths.size(); ++i) {
            DrawControlText(rt, headers[i], cx + 12, y, widths[i] - 18, 42, Palette::TextSoft, 12, true,
                i == 0 ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
            cx += widths[i];
            if (i + 1 < headers.size() && i + 1 < widths.size()) {
                RectF hit{ cx - 4.0f, y + 6.0f, 8.0f, 30.0f };
                columnResizeHits_.push_back({ installed, static_cast<int>(i), hit });
                bool active = resizingColumn_ && resizingInstalled_ == installed && resizingColumnIndex_ == static_cast<int>(i);
                bool hover = hoverColumnResize_ && hoverColumnResizeInstalled_ == installed && hoverColumnResizeIndex_ == static_cast<int>(i);
                if (active || hover) {
                    StrokeLine(rt, cx, y + 9, cx, y + 33, active ? Palette::Primary : Palette::BorderStrong,
                        active ? 1.4f : 1.0f, active ? 0.95f : 0.55f);
                }
            }
        }
        int rows = currentPage_ == Page::Market ? static_cast<int>(filtered_.size()) : static_cast<int>(installed_.size());
        int scroll = currentPage_ == Page::Market ? marketScroll_ : installedScroll_;
        float visible = (std::max)(1.0f, ContentBottom() - TableHeaderBottom());
        float total = (std::max)(visible, rows * 44.0f);
        if (total > visible + 1.0f) {
            float trackX = x + w - 8;
            float trackY = TableHeaderBottom() + 6;
            float trackH = visible - 12;
            float thumbH = (std::max)(34.0f, trackH * visible / total);
            float maxScroll = total - visible;
            float thumbY = trackY + (trackH - thumbH) * (scroll / maxScroll);
            FillRound(rt, trackX, trackY, 4, trackH, 2, RGB(223, 229, 237), 0.70f);
            FillRound(rt, trackX, thumbY, 4, thumbH, 2, Palette::BorderStrong, 0.95f);
        }
    }

    void DrawMarketRow(ID2D1RenderTarget* rt, const PackageGroup& g, int index, float x, float y) {
        const auto& widths = marketColumnWidths_;
        RectF rowRc{ x + 1, y, ContentW() - 2, 43 };
        COLORREF rowBg = (hoverRow_ == index) ? Palette::RowHover : Palette::Surface;
        FillRect(rt, rowRc.x, rowRc.y, rowRc.w, rowRc.h, rowBg);
        FillRect(rt, x + 12, y + 43, ContentW() - 24, 1, Palette::RowBorder);
        float cx = x;
        RectF check{ cx + 14, y + 13, 18, 18 };
        RectF checkHit{ check.x - 4, check.y - 4, 26, 26 };
        DrawCheck(rt, check, g.selected);
        cx += widths[0];
        DrawText(rt, g.name, cx + 12, y, widths[1] - 20, 43, Palette::Text, 13, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        cx += widths[1];
        DrawText(rt, g.latestVersion, cx + 12, y, widths[2] - 20, 43, Palette::TextSoft, 12, false, DWRITE_TEXT_ALIGNMENT_LEADING);
        cx += widths[2];
        DrawText(rt, g.author, cx + 12, y, widths[3] - 20, 43, Palette::TextSoft, 12, false, DWRITE_TEXT_ALIGNMENT_LEADING);
        cx += widths[3];
        DrawText(rt, CategoryLabel(g.category), cx + 12, y, widths[4] - 20, 43, Palette::TextSoft, 12, false, DWRITE_TEXT_ALIGNMENT_LEADING);
        cx += widths[4];
        std::wstring status = StatusText(g);
        DrawBadge(rt, status, cx + 12, y + 10, AccentForStatus(status));
        cx += widths[5];
        DrawText(rt, FormatSize(g.size), cx + 12, y, widths[6] - 20, 43, Palette::TextSoft, 12, false, DWRITE_TEXT_ALIGNMENT_LEADING);
        cx += widths[6];
        RectF action{ cx + 12, y + 8, 88, 28 };
        std::wstring actionText = status == Tr(L"status.installed", L"Installed") ? Tr(L"action.reinstall", L"Reinstall") :
            (status == Tr(L"status.upgrade", L"Upgrade") ? Tr(L"action.upgrade", L"Upgrade") : Tr(L"action.install", L"Install"));
        DrawButtonRaw(rt, actionText, action, status != Tr(L"status.installed", L"Installed"), hoverRow_ == index);
        rowHits_.push_back({ false, index, rowRc, checkHit, action });
    }

    void DrawInstalledRow(ID2D1RenderTarget* rt, const InstalledItem& item, int index, float x, float y) {
        const auto& widths = installedColumnWidths_;
        RectF rowRc{ x + 1, y, ContentW() - 2, 43 };
        COLORREF rowBg = (hoverRow_ == index) ? Palette::RowHover : Palette::Surface;
        FillRect(rt, rowRc.x, rowRc.y, rowRc.w, rowRc.h, rowBg);
        FillRect(rt, x + 12, y + 43, ContentW() - 24, 1, Palette::RowBorder);
        float cx = x;
        RectF check{ cx + 14, y + 13, 18, 18 };
        RectF checkHit{ check.x - 4, check.y - 4, 26, 26 };
        DrawCheck(rt, check, item.selected);
        cx += widths[0];
        DrawText(rt, item.name, cx + 12, y, widths[1] - 20, 43, Palette::Text, 13, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        cx += widths[1];
        DrawText(rt, item.version, cx + 12, y, widths[2] - 20, 43, Palette::TextSoft, 12, false, DWRITE_TEXT_ALIGNMENT_LEADING);
        cx += widths[2];
        DrawText(rt, item.author, cx + 12, y, widths[3] - 20, 43, Palette::TextSoft, 12, false, DWRITE_TEXT_ALIGNMENT_LEADING);
        cx += widths[3];
        DrawText(rt, item.installDate, cx + 12, y, widths[4] - 20, 43, Palette::TextSoft, 12, false, DWRITE_TEXT_ALIGNMENT_LEADING);
        cx += widths[4];
        RectF action{ cx + 12, y + 8, 94, 28 };
        DrawButtonRaw(rt, Tr(L"action.uninstall", L"Uninstall"), action, false, hoverRow_ == index);
        rowHits_.push_back({ true, index, rowRc, checkHit, action });
    }

    void DrawCheck(ID2D1RenderTarget* rt, RectF rc, bool checked) {
        FillRound(rt, rc.x, rc.y, rc.w, rc.h, 3, checked ? Palette::Primary : Palette::Surface, 1.0f);
        StrokeRound(rt, rc.x, rc.y, rc.w, rc.h, 3, checked ? Palette::Primary : Palette::BorderStrong);
        if (checked) {
            ID2D1SolidColorBrush* br = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(RGB(255,255,255)), &br);
            if (br) {
                float pulse = 0.85f + 0.15f * std::sin(static_cast<float>(GetTickCount64() % 1000) / 1000.0f * 6.2831853f);
                rt->DrawLine(P(rc.x + 4.2f, rc.y + 9.4f), P(rc.x + 7.6f, rc.y + 13.0f), br, ScaleF(1.8f * pulse));
                rt->DrawLine(P(rc.x + 7.6f, rc.y + 13.0f), P(rc.x + 14.0f, rc.y + 5.4f), br, ScaleF(1.8f * pulse));
                br->Release();
            }
        }
    }

    void DrawBadge(ID2D1RenderTarget* rt, const std::wstring& text, float x, float y, COLORREF accent) {
        COLORREF bg = Mix(Palette::Surface, accent, 0.16f);
        FillRound(rt, x, y, 82, 23, 5, bg, 1.0f);
        DrawControlText(rt, text, x + 8, y, 66, 23, accent, 11, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    D2D1_POINT_2F P(float x, float y) const {
        return D2D1::Point2F(ScaleF(x + kShadowPad), ScaleF(y + kShadowPad));
    }

    D2D1_RECT_F R(float x, float y, float w, float h) const {
        return D2D1::RectF(ScaleF(x + kShadowPad), ScaleF(y + kShadowPad), ScaleF(x + kShadowPad + w), ScaleF(y + kShadowPad + h));
    }

    void FillRect(ID2D1RenderTarget* rt, float x, float y, float w, float h, COLORREF color, float alpha = 1.0f) {
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(color, alpha), &br);
        if (br) {
            rt->FillRectangle(R(x, y, w, h), br);
            br->Release();
        }
    }

    void StrokeRect(ID2D1RenderTarget* rt, float x, float y, float w, float h, COLORREF color, float alpha = 1.0f) {
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(color, alpha), &br);
        if (br) {
            rt->DrawRectangle(R(x, y, w, h), br, ScaleF(1.0f));
            br->Release();
        }
    }

    void FillRound(ID2D1RenderTarget* rt, float x, float y, float w, float h, float r, COLORREF color, float alpha) {
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(color, alpha), &br);
        if (br) {
            auto rr = MakeRoundRect(ScaleF(x + kShadowPad), ScaleF(y + kShadowPad), ScaleF(w), ScaleF(h), ScaleF(r));
            rt->FillRoundedRectangle(&rr, br);
            br->Release();
        }
    }

    void StrokeRound(ID2D1RenderTarget* rt, float x, float y, float w, float h, float r, COLORREF color, float alpha = 1.0f) {
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(color, alpha), &br);
        if (br) {
            auto rr = MakeRoundRect(ScaleF(x + kShadowPad) + 0.5f, ScaleF(y + kShadowPad) + 0.5f, ScaleF(w) - 1.0f, ScaleF(h) - 1.0f, ScaleF(r));
            rt->DrawRoundedRectangle(&rr, br, ScaleF(1.0f));
            br->Release();
        }
    }

    void FillTopRightRect(ID2D1RenderTarget* rt, RectF rc, float radius, COLORREF color, float alpha) {
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(color, alpha), &br);
        if (!br) return;
        ID2D1PathGeometry* geo = nullptr;
        ExD2DFactory::GetFactory()->CreatePathGeometry(&geo);
        if (!geo) {
            br->Release();
            return;
        }
        ID2D1GeometrySink* sink = nullptr;
        geo->Open(&sink);
        if (sink) {
            float x0 = ScaleF(rc.x + kShadowPad);
            float y0 = ScaleF(rc.y + kShadowPad);
            float x1 = ScaleF(rc.x + rc.w + kShadowPad);
            float y1 = ScaleF(rc.y + rc.h + kShadowPad);
            float r = (std::min)(ScaleF(radius), (std::min)(x1 - x0, y1 - y0) * 0.5f);
            sink->BeginFigure(D2D1::Point2F(x0, y0), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(x1 - r, y0));
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(x1, y0 + r), D2D1::SizeF(r, r), 0,
                D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->AddLine(D2D1::Point2F(x1, y1));
            sink->AddLine(D2D1::Point2F(x0, y1));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            rt->FillGeometry(geo, br);
        }
        geo->Release();
        br->Release();
    }

    void FillBottomLeftRect(ID2D1RenderTarget* rt, RectF rc, float radius, COLORREF color, float alpha) {
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(color, alpha), &br);
        if (!br) return;
        ID2D1PathGeometry* geo = nullptr;
        ExD2DFactory::GetFactory()->CreatePathGeometry(&geo);
        if (!geo) {
            br->Release();
            return;
        }
        ID2D1GeometrySink* sink = nullptr;
        geo->Open(&sink);
        if (sink) {
            float x0 = ScaleF(rc.x + kShadowPad);
            float y0 = ScaleF(rc.y + kShadowPad);
            float x1 = ScaleF(rc.x + rc.w + kShadowPad);
            float y1 = ScaleF(rc.y + rc.h + kShadowPad);
            float r = (std::min)(ScaleF(radius), (std::min)(x1 - x0, y1 - y0) * 0.5f);
            sink->BeginFigure(D2D1::Point2F(x0, y0), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(x1, y0));
            sink->AddLine(D2D1::Point2F(x1, y1));
            sink->AddLine(D2D1::Point2F(x0 + r, y1));
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(x0, y1 - r), D2D1::SizeF(r, r), 0,
                D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->AddLine(D2D1::Point2F(x0, y0));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            rt->FillGeometry(geo, br);
        }
        geo->Release();
        br->Release();
    }

    void FillBottomRightRect(ID2D1RenderTarget* rt, RectF rc, float radius, COLORREF color, float alpha) {
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(color, alpha), &br);
        if (!br) return;
        ID2D1PathGeometry* geo = nullptr;
        ExD2DFactory::GetFactory()->CreatePathGeometry(&geo);
        if (!geo) {
            br->Release();
            return;
        }
        ID2D1GeometrySink* sink = nullptr;
        geo->Open(&sink);
        if (sink) {
            float x0 = ScaleF(rc.x + kShadowPad);
            float y0 = ScaleF(rc.y + kShadowPad);
            float x1 = ScaleF(rc.x + rc.w + kShadowPad);
            float y1 = ScaleF(rc.y + rc.h + kShadowPad);
            float r = (std::min)(ScaleF(radius), (std::min)(x1 - x0, y1 - y0) * 0.5f);
            sink->BeginFigure(D2D1::Point2F(x0, y0), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(x1, y0));
            sink->AddLine(D2D1::Point2F(x1, y1 - r));
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(x1 - r, y1), D2D1::SizeF(r, r), 0,
                D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->AddLine(D2D1::Point2F(x0, y1));
            sink->AddLine(D2D1::Point2F(x0, y0));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            rt->FillGeometry(geo, br);
        }
        geo->Release();
        br->Release();
    }

    // Fill a rect that has its top corners rounded (top-left + top-right
    // arcs), bottom corners square. Used by the status drawer so it
    // sits flush against the footer.
    void FillTopRoundRect(ID2D1RenderTarget* rt, RectF rc, float radius, COLORREF color, float alpha) {
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(color, alpha), &br);
        if (!br) return;
        ID2D1PathGeometry* geo = nullptr;
        ExD2DFactory::GetFactory()->CreatePathGeometry(&geo);
        if (!geo) {
            br->Release();
            return;
        }
        ID2D1GeometrySink* sink = nullptr;
        geo->Open(&sink);
        if (sink) {
            float x0 = ScaleF(rc.x + kShadowPad);
            float y0 = ScaleF(rc.y + kShadowPad);
            float x1 = ScaleF(rc.x + rc.w + kShadowPad);
            float y1 = ScaleF(rc.y + rc.h + kShadowPad);
            float r = (std::min)(ScaleF(radius), (std::min)(x1 - x0, y1 - y0) * 0.5f);
            sink->BeginFigure(D2D1::Point2F(x0, y0 + r), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(x0 + r, y0), D2D1::SizeF(r, r), 0,
                D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->AddLine(D2D1::Point2F(x1 - r, y0));
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(x1, y0 + r), D2D1::SizeF(r, r), 0,
                D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->AddLine(D2D1::Point2F(x1, y1));
            sink->AddLine(D2D1::Point2F(x0, y1));
            sink->AddLine(D2D1::Point2F(x0, y0 + r));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            rt->FillGeometry(geo, br);
        }
        geo->Release();
        br->Release();
    }

    void FillCircle(ID2D1RenderTarget* rt, float cx, float cy, float r, COLORREF color, float alpha = 1.0f) {
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(color, alpha), &br);
        if (br) {
            rt->FillEllipse(D2D1::Ellipse(P(cx, cy), ScaleF(r), ScaleF(r)), br);
            br->Release();
        }
    }

    void StrokeCircle(ID2D1RenderTarget* rt, float cx, float cy, float r, COLORREF color, float width, float alpha = 1.0f) {
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(color, alpha), &br);
        if (br) {
            rt->DrawEllipse(D2D1::Ellipse(P(cx, cy), ScaleF(r), ScaleF(r)), br, ScaleF(width));
            br->Release();
        }
    }

    void StrokeLine(ID2D1RenderTarget* rt, float x1, float y1, float x2, float y2, COLORREF color, float width, float alpha = 1.0f) {
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(color, alpha), &br);
        if (br) {
            rt->DrawLine(P(x1, y1), P(x2, y2), br, ScaleF(width));
            br->Release();
        }
    }

    void DrawText(ID2D1RenderTarget* rt, const std::wstring& text, float x, float y, float w, float h,
                  COLORREF color, float size, bool bold,
                  DWRITE_TEXT_ALIGNMENT align,
                  float alpha = 1.0f,
                  DWRITE_PARAGRAPH_ALIGNMENT valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER) {
        if (text.empty()) return;
        IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(size * userScale_ / ExDPI::GetScale(), bold, align, valign);
        if (!fmt) return;
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(color, alpha), &br);
        if (br) {
            rt->DrawText(text.c_str(), static_cast<UINT32>(text.size()), fmt, R(x, y, w, h), br,
                D2D1_DRAW_TEXT_OPTIONS_CLIP);
            br->Release();
        }
        fmt->Release();
    }

    void DrawControlText(ID2D1RenderTarget* rt, const std::wstring& text, float x, float y, float w, float h,
                         COLORREF color, float size, bool bold,
                         DWRITE_TEXT_ALIGNMENT align,
                         float alpha = 1.0f) {
        if (text.empty()) return;
        IDWriteFactory* dw = ExD2DFactory::GetDWriteFactory();
        if (!dw) {
            DrawText(rt, text, x, y + 0.75f, w, h, color, size, bold, align, alpha, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            return;
        }
        IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(size * userScale_ / ExDPI::GetScale(),
            bold, align, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        if (!fmt) return;
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        IDWriteTextLayout* layout = nullptr;
        HRESULT hr = dw->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), fmt,
            ScaleF(w), ScaleF(h), &layout);
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(color, alpha), &br);
        if (SUCCEEDED(hr) && layout && br) {
            DWRITE_TEXT_METRICS metrics = {};
            layout->GetMetrics(&metrics);
            float textH = metrics.height > 0.0f ? metrics.height : ScaleF(size);
            float px = ScaleF(x + kShadowPad);
            float py = ScaleF(y + kShadowPad) + (std::max)(0.0f, ScaleF(h) - textH) * 0.5f - metrics.top + ScaleF(0.35f);
            rt->DrawTextLayout(D2D1::Point2F(px, py), layout, br, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        } else {
            DrawText(rt, text, x, y + 0.75f, w, h, color, size, bold, align, alpha, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (br) br->Release();
        if (layout) layout->Release();
        fmt->Release();
    }

    mutable std::recursive_mutex mu_;
    GpmClient client_;
    Page currentPage_ = Page::Market;
    ULONGLONG pageSwitchAt_ = 0;
    float pageEase_ = 1.0f;
    float dpi_ = 1.0f;
    float userScale_ = 1.0f;
    float chosenScale_ = 1.0f;
    bool searchFocused_ = false;
    std::wstring search_;
    size_t searchCaretIndex_ = 0;
    float searchScrollX_ = 0.0f;
    std::wstring exeDir_;
    std::wstring themeName_ = L"default";
    std::wstring languageName_ = L"en-US";
    std::map<std::wstring, std::wstring> lang_;
    std::vector<std::wstring> themeFiles_;
    std::vector<std::wstring> langFiles_;
    int marketScroll_ = 0;
    int installedScroll_ = 0;
    int logScroll_ = 0;
    bool tableDragging_ = false;
    bool tableDragged_ = false;
    float dragStartY_ = 0.0f;
    int dragStartScroll_ = 0;
    int available_ = 0;
    int installedCount_ = 0;
    int updates_ = 0;
    bool seenIndexData_ = false;
    bool statusPanelOpen_ = false;
    ULONGLONG statusPanelToggledAt_ = 0;
    std::wstring statusLine_;
    int hoverNav_ = -1;
    int hoverRow_ = -1;
    int hoverDialogButton_ = -1;
    int menuFocusIndex_ = -1;
    std::wstring hoverButton_;
    std::wstring hoverMenuItem_;
    std::vector<PackageVersion> indexData_;
    std::vector<PackageGroup> groups_;
    std::vector<int> filtered_;
    std::vector<std::wstring> categories_{ L"all" };
    std::wstring selectedCategory_ = L"all";
    std::vector<InstalledItem> installed_;
    std::vector<InstallQueueItem> installQueue_;
    std::wstring versionFocusName_;
    std::vector<std::wstring> collapsedVersionGroups_;
    std::vector<std::wstring> selectedVersionKeys_;
    std::vector<QueueExitAnim> queueExitAnims_;
    bool queueInstalling_ = false;
    int versionScroll_ = 0;
    int queueScroll_ = 0;
    ULONGLONG lastRowClickAt_ = 0;
    int lastRowClickIndex_ = -1;
    std::vector<LogEntry> logs_;
    std::vector<Toast> toasts_;
    std::vector<Spark> sparks_;
    ProgressInfo progress_;
    DialogInfo dialog_;
    std::vector<ButtonHit> buttonHits_;
    std::vector<RowHit> rowHits_;
    std::vector<ColumnResizeHit> columnResizeHits_;
    std::vector<ButtonHit> menuHits_;
    std::vector<ButtonHit> dialogButtonHits_;
    std::vector<ButtonHit> titleButtonHits_;
    std::vector<float> marketColumnWidths_{ 42, 214, 98, 112, 132, 98, 76, 118 };
    std::vector<float> installedColumnWidths_{ 42, 250, 110, 135, 235, 134 };
    float navAnim_ = 0.0f;
    float navIndicatorFrom_ = 0.0f;
    float navIndicatorTo_ = 0.0f;
    float navHoverAnim_ = 0.0f;
    float navHoverTarget_ = 0.0f;
    float navHoverAlpha_ = 0.0f;
    ULONGLONG navIndicatorStartAt_ = 0;
    float navDirection_ = 1.0f;
    float sidebarAnim_ = 1.0f;
    float sidebarAnimStart_ = 1.0f;
    float sidebarAnimTarget_ = 1.0f;
    ULONGLONG sidebarAnimStartedAt_ = 0;
    bool sidebarCollapsed_ = false;
    ULONGLONG splashStartedAt_ = 0;
    ULONGLONG splashFirstPaintAt_ = 0;
    ULONGLONG splashRevealAt_ = 0;
    bool splashDismissed_ = false;
    float progressPhase_ = 0.0f;
    float progressVisual_ = 0.0f;
    float progressTarget_ = 0.0f;
    bool progressVisualInitialized_ = false;
    PaletteSnapshot themeFrom_{};
    PaletteSnapshot themeTo_{};
    ULONGLONG themeTransitionStartAt_ = 0;
    bool themeTransitionActive_ = false;
    ULONGLONG lastUiTickAt_ = 0;
    bool caretOn_ = true;
    bool hoverColumnResize_ = false;
    bool hoverColumnResizeInstalled_ = false;
    int hoverColumnResizeIndex_ = -1;
    bool resizingColumn_ = false;
    bool resizingInstalled_ = false;
    int resizingColumnIndex_ = -1;
    float resizeStartX_ = 0.0f;
    float resizeStartLeftW_ = 0.0f;
    float resizeStartRightW_ = 0.0f;
    Page previousPage_ = Page::Market;
    int statusPanelScroll_ = 0;
    bool backendLaunchRequested_ = false;
    ULONGLONG lastBackendLaunchAt_ = 0;
    std::wstring watermarkPath_;
    Detail::ComPtr<ID2D1Bitmap> watermarkBitmap_;
    ID2D1RenderTarget* watermarkRenderTarget_ = nullptr;
    bool watermarkLoadFailed_ = false;
    std::vector<IconBitmapCacheEntry> iconBitmapCache_;
    ID2D1RenderTarget* iconBitmapRenderTarget_ = nullptr;
    ULONGLONG windowAnimStartedAt_ = 0;
    int windowAnimMode_ = 0;
    struct MenuItem { std::wstring id; std::wstring label; };
    std::wstring activeMenu_;
    std::vector<MenuItem> menuItems_;
    float menuOpenProgress_ = 0.0f;
    float menuHoverY_ = 0.0f;
    float menuHoverAlpha_ = 0.0f;
    float menuHoverTargetY_ = 0.0f;
    bool menuHoverInitialized_ = false;
    bool menuClosing_ = false;
};

LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                              UINT_PTR, DWORD_PTR refData) {
    auto* panel = reinterpret_cast<GpmPanel*>(refData);
    switch (msg) {
    case WM_APP_WS_MESSAGE: {
        auto* s = reinterpret_cast<std::wstring*>(wParam);
        if (s) {
            panel->OnBackendMessage(*s);
            delete s;
        }
        return 0;
    }
    case WM_APP_WS_DISCONNECTED:
        panel->OnDisconnected();
        return 0;
    case WM_TIMER:
        if (wParam == kUiTimerId) {
            panel->OnTimerTick();
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (panel) {
            panel->OnKeyDown(static_cast<UINT>(wParam));
            return 0;
        }
        break;
    case WM_CHAR:
        if (panel) {
            panel->OnChar(static_cast<wchar_t>(wParam));
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
        if (panel && panel->ShouldStartWindowDrag(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) {
            ReleaseCapture();
            SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }
        break;
    case WM_DPICHANGED:
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        if (panel) {
            mmi->ptMinTrackSize.x = panel->GetWidth();
            mmi->ptMinTrackSize.y = panel->GetHeight();
            mmi->ptMaxTrackSize.x = panel->GetWidth();
            mmi->ptMaxTrackSize.y = panel->GetHeight();
        }
        return 0;
    }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    EnableDpiAwareness();

    std::wstring exeDir = GetExeDir();
    RegisterPrivateFonts();

    GpmApp app;
    if (!app.Init(L"Microsoft YaHei UI", 13.0f)) {
        MessageBoxW(nullptr, L"GPM init failed", L"GPM", MB_OK | MB_ICONERROR);
        return -1;
    }

    float initialScale = FitScaleToWorkArea(DetectScale());
    initialScale = FitScaleToWorkArea(LoadConfiguredScale(exeDir, initialScale));
    float exScale = ExDPI::GetScale();
    if (exScale <= 0.0f) exScale = 1.0f;
    int createW = static_cast<int>(((kBaseW + kShadowPad * 2) * initialScale) / exScale + 0.5f);
    int createH = static_cast<int>(((kBaseH + kShadowPad * 2) * initialScale) / exScale + 0.5f);
    int ret = 0;
    {
        GpmWindow wnd;
        if (!wnd.Create(nullptr, 0, 0,
            createW,
            createH,
            L"GPM", kFixedStyle)) {
            MessageBoxW(nullptr, L"window create failed", L"GPM", MB_OK | MB_ICONERROR);
            return -1;
        }
        wnd.SetBackgroundColor(RGB(0, 0, 0));
        wnd.SetWindowCornerRadius(0);

        wnd.SetTitleBarHeight(0);

        GpmPanel panel(initialScale);
        g_panel = &panel;
        panel.SetRect(0, 0,
            static_cast<int>((kBaseW + kShadowPad * 2) * initialScale + 0.5f),
            static_cast<int>((kBaseH + kShadowPad * 2) * initialScale + 0.5f), false);
        wnd.AddControl(&panel);

        panel.AttachWindow(&wnd);
        SetWindowSubclass(wnd.GetWindowHandle(), SubclassProc, 1, reinterpret_cast<DWORD_PTR>(&panel));
        ClampWindowToWorkArea(wnd.GetWindowHandle(),
            static_cast<int>((kBaseW + kShadowPad * 2) * initialScale + 0.5f),
            static_cast<int>((kBaseH + kShadowPad * 2) * initialScale + 0.5f),
            true);

        wnd.Show();
        ret = wnd.Run();
        HWND hwnd = wnd.GetWindowHandle();
        if (hwnd && IsWindow(hwnd)) {
            RemoveWindowSubclass(hwnd, SubclassProc, 1);
            KillTimer(hwnd, kUiTimerId);
            timeEndPeriod(1);
        }
        g_panel = nullptr;
    }
    app.UnInit();
    return ret;
}
