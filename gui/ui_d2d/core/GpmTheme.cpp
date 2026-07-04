/*
 * GpmTheme.cpp - One Dark Pro 主题 + DPI缩放 + D2D工厂 + 全局字体
 *               + ExStyle 主题应用方法
 * ImGui风格绘制样式
 */
#include "gpm_ui.h"

namespace gpm_ui {

// ---- One Dark Pro 主题色 (ImGui风格配色) ----
GpmThemeColors GetOneDarkProTheme() {
    GpmThemeColors t = {};
    t.bgWindow    = RGB(40, 44, 52);
    t.bgTitleBar  = RGB(33, 37, 43);
    t.bgEditor    = RGB(40, 44, 52);
    t.bgInput     = RGB(30, 33, 39);
    t.bgHover     = RGB(55, 60, 70);
    t.bgActive    = RGB(44, 49, 58);
    t.bgSelection = RGB(62, 68, 81);
    t.bgDisabled  = RGB(50, 54, 62);
    t.fgPrimary   = RGB(171, 178, 191);
    t.fgSecondary = RGB(120, 128, 143);
    t.fgDisabled  = RGB(76, 82, 94);
    t.fgAccent    = RGB(97, 175, 239);
    t.border      = RGB(60, 65, 75);
    t.borderFocus = RGB(97, 175, 239);
    t.borderHover = RGB(80, 86, 98);
    t.btnBg       = RGB(55, 60, 72);
    t.btnBgHover  = RGB(70, 76, 90);
    t.btnBgDown   = RGB(45, 50, 60);
    t.btnFg       = RGB(171, 178, 191);
    t.sliderTrack = RGB(60, 65, 75);
    t.sliderFill  = RGB(97, 175, 239);
    t.sliderThumb = RGB(171, 178, 191);
    t.checkMark   = RGB(97, 175, 239);
    t.closeHover  = RGB(232, 17, 35);
    t.progressBg  = RGB(60, 65, 75);
    t.progressFill= RGB(97, 175, 239);
    t.progressText= RGB(255, 255, 255);
    t.listBg          = RGB(33, 37, 43);
    t.listItemBg      = RGB(40, 44, 52);
    t.listItemHover   = RGB(55, 60, 70);
    t.listItemSelected= RGB(62, 68, 81);
    t.listItemText    = RGB(171, 178, 191);
    t.listBorder      = RGB(60, 65, 75);
    t.listScrollbar   = RGB(50, 54, 62);
    t.listScrollThumb = RGB(80, 86, 98);
    
    // 新控件颜色
    t.editBg       = RGB(30, 33, 39);
    t.editFg       = RGB(171, 178, 191);
    t.editCaret    = RGB(171, 178, 191);
    t.editSelection= RGB(62, 68, 81);
    t.tabBg        = RGB(33, 37, 43);
    t.tabActive    = RGB(40, 44, 52);
    t.tabInactive  = RGB(30, 33, 39);
    t.tabHover     = RGB(50, 55, 65);
    t.tabBorder    = RGB(60, 65, 75);
    return t;
}

static GpmThemeColors s_theme = GetOneDarkProTheme();
GpmThemeColors& Theme() { return s_theme; }
void SetTheme(const GpmThemeColors& theme) { s_theme = theme; }

// ---- DPI ----
float ExDPI::s_scale = 1.0f;
bool  ExDPI::s_inited = false;

void ExDPI::Init() {
    if (s_inited) return;
    s_inited = true;
    HMODULE hShcore = LoadLibraryW(L"Shcore.dll");
    if (hShcore) {
        typedef HRESULT(WINAPI* PFN)(int);
        auto fn = (PFN)GetProcAddress(hShcore, "SetProcessDpiAwareness");
        if (fn) fn(2);
        FreeLibrary(hShcore);
    }
    HDC hdc = ::GetDC(NULL);
    int dpiX = ::GetDeviceCaps(hdc, LOGPIXELSX);
    ::ReleaseDC(NULL, hdc);
    s_scale = (float)dpiX / 96.0f;
    if (s_scale < 1.0f) s_scale = 1.0f;
}

float ExDPI::GetScale() { if (!s_inited) Init(); return s_scale; }
int ExDPI::Scale(int value) { return (int)(value * GetScale() + 0.5f); }
float ExDPI::ScaleF(float value) { return value * GetScale(); }

// ---- D2D工厂 (ComPtr版本) ----
Detail::ComPtr<ID2D1Factory>      ExD2DFactory::s_factory;
Detail::ComPtr<IDWriteFactory>    ExD2DFactory::s_dwFactory;
Detail::ComPtr<IWICImagingFactory> ExD2DFactory::s_wicFactory;

bool ExD2DFactory::Init() {
    if (s_factory) return true;
    ID2D1Factory* pFactory = nullptr;
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&pFactory));
    if (SUCCEEDED(hr)) s_factory.Attach(pFactory);
    if (FAILED(hr)) return false;
    
    IDWriteFactory* pDwFactory = nullptr;
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), (IUnknown**)&pDwFactory);
    if (SUCCEEDED(hr)) s_dwFactory.Attach(pDwFactory);
    if (FAILED(hr)) return false;

    IWICImagingFactory* pWicFactory = nullptr;
    hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pWicFactory));
    if (SUCCEEDED(hr)) s_wicFactory.Attach(pWicFactory);
    // WIC failure is non-fatal
    return true;
}

void ExD2DFactory::UnInit() {
    s_wicFactory.Reset();
    s_dwFactory.Reset();
    s_factory.Reset();
}

ID2D1Factory* ExD2DFactory::GetFactory() { return s_factory.Get(); }
IDWriteFactory* ExD2DFactory::GetDWriteFactory() { return s_dwFactory.Get(); }
IWICImagingFactory* ExD2DFactory::GetWICFactory() { return s_wicFactory.Get(); }

IDWriteTextFormat* ExD2DFactory::CreateTextFormat(float fontSize, bool bold,
    DWRITE_TEXT_ALIGNMENT hAlign, DWRITE_PARAGRAPH_ALIGNMENT vAlign) {
    if (!s_dwFactory) return nullptr;
    IDWriteTextFormat* fmt = nullptr;
    float scaledSize = ExDPI::ScaleF(fontSize);
    s_dwFactory->CreateTextFormat(
        ExFont::GetGlobalFont().c_str(), nullptr,
        bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        scaledSize, L"", &fmt);
    if (fmt) {
        fmt->SetTextAlignment(hAlign);
        fmt->SetParagraphAlignment(vAlign);
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    return fmt;
}

IDWriteTextLayout* ExD2DFactory::CreateTextLayout(const std::wstring& text,
    float fontSize, float maxWidth, float maxHeight, bool bold) {
    if (!s_dwFactory) return nullptr;
    IDWriteTextLayout* layout = nullptr;
    float scaledSize = ExDPI::ScaleF(fontSize);
    IDWriteTextFormat* fmt = nullptr;
    s_dwFactory->CreateTextFormat(
        ExFont::GetGlobalFont().c_str(), nullptr,
        bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        scaledSize, L"", &fmt);
    if (fmt) {
        s_dwFactory->CreateTextLayout(
            text.c_str(), (UINT32)text.length(),
            fmt, maxWidth, maxHeight, &layout);
        fmt->Release();
    }
    return layout;
}

// ---- 全局字体 ----
std::wstring ExFont::s_fontName = L"Microsoft YaHei UI";
float ExFont::s_defaultFontSize = 10.0f;
void ExFont::SetGlobalFont(const std::wstring& fontName) { s_fontName = fontName; }
const std::wstring& ExFont::GetGlobalFont() { return s_fontName; }
void ExFont::SetDefaultFontSize(float size) { s_defaultFontSize = size; }
float ExFont::GetDefaultFontSize() { return s_defaultFontSize; }
bool ExFont::RegisterFontFile(const std::wstring& path) {
    if (path.empty()) return false;
    return AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr) > 0;
}
bool ExFont::RegisterFontMemory(const void* data, DWORD size) {
    if (!data || size == 0) return false;
    DWORD fontsAdded = 0;
    HANDLE handle = AddFontMemResourceEx(const_cast<void*>(data), size, nullptr, &fontsAdded);
    return handle != nullptr && fontsAdded > 0;
}

// ============================================================
// ExStyle 主题应用方法
// ============================================================
void ExStyle::ApplyTheme_Button() {
    auto& t = Theme();
    bgColors = StateColors(t.btnBg, t.btnBgHover, t.btnBgDown);
    textColors = StateColors(t.btnFg, t.btnFg, t.btnFg);
    borderColor = t.border;
    cornerRadius = ExDPI::Scale(4);
    fontSize = ExFont::GetDefaultFontSize();
    bold = false;
}

void ExStyle::ApplyTheme_Label() {
    auto& t = Theme();
    bgColors = StateColors(0, 0, 0);
    textColors = StateColors(t.fgPrimary, t.fgPrimary, t.fgPrimary);
    borderColor = 0;
    cornerRadius = 0;
    fontSize = ExFont::GetDefaultFontSize();
    bold = false;
}

void ExStyle::ApplyTheme_Edit() {
    auto& t = Theme();
    bgColors = StateColors(t.editBg, t.editBg, t.editBg);
    textColors = StateColors(t.editFg, t.editFg, t.editFg);
    borderColor = t.border;
    borderHoverColor = t.borderHover;
    borderFocusColor = t.borderFocus;
    cornerRadius = ExDPI::Scale(4);
    fontSize = ExFont::GetDefaultFontSize();
    bold = false;
}

void ExStyle::ApplyTheme_CheckBox() {
    auto& t = Theme();
    bgColors = StateColors(t.bgInput, t.bgHover, t.bgActive);
    textColors = StateColors(t.fgPrimary, t.fgPrimary, t.fgPrimary);
    borderColor = t.border;
    cornerRadius = ExDPI::Scale(3);
    fontSize = ExFont::GetDefaultFontSize();
}

void ExStyle::ApplyTheme_Slider() {
    auto& t = Theme();
    bgColors = StateColors(t.sliderTrack, t.sliderTrack, t.sliderTrack);
    textColors = StateColors(t.sliderThumb, t.sliderThumb, t.sliderThumb);
    borderColor = 0;
    cornerRadius = 0;
    fontSize = ExFont::GetDefaultFontSize();
}

void ExStyle::ApplyTheme_ComboBox() {
    auto& t = Theme();
    bgColors = StateColors(t.bgInput, t.bgHover, t.bgActive);
    textColors = StateColors(t.fgPrimary, t.fgPrimary, t.fgPrimary);
    borderColor = t.border;
    cornerRadius = ExDPI::Scale(4);
    fontSize = ExFont::GetDefaultFontSize();
}

void ExStyle::ApplyTheme_ListBox() {
    auto& t = Theme();
    bgColors = StateColors(t.listBg, t.listBg, t.listBg);
    textColors = StateColors(t.listItemText, t.listItemText, t.listItemText);
    borderColor = t.listBorder;
    cornerRadius = ExDPI::Scale(4);
    fontSize = ExFont::GetDefaultFontSize();
}

void ExStyle::ApplyTheme_Tab() {
    auto& t = Theme();
    bgColors = StateColors(t.tabBg, t.tabBg, t.tabBg);
    textColors = StateColors(t.fgPrimary, t.fgPrimary, t.fgPrimary);
    borderColor = t.tabBorder;
    cornerRadius = 0;
    fontSize = ExFont::GetDefaultFontSize();
}

} // namespace gpm_ui
