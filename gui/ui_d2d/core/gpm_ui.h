/*
 * gpm_ui.h - 主头文件
 * D2D渲染 + 分层透明窗口 (用D2D绘制圆角，不用SetWindowRgn)
 */
#pragma once

#include "gpm_ui_pch.h"
#include <d2d1helper.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <map>
#include <cstdio>

#include <dwrite.h>

// Windows SDK未定义时引入
#ifndef _INC_COMBASEAPI
#  include <combaseapi.h>
#endif

namespace gpm_ui {

// ============================================================
// 控件启用宏 - 注释掉某个宏可禁用对应控件，使其不参与编译
// ============================================================
#define GPMUI_ENABLE_BUTTON
#define GPMUI_ENABLE_LABEL
#define GPMUI_ENABLE_CHECKBOX
#define GPMUI_ENABLE_RADIOBUTTON
#define GPMUI_ENABLE_SLIDER
#define GPMUI_ENABLE_COMBOBOX
#define GPMUI_ENABLE_EDIT
#define GPMUI_ENABLE_PROGRESSBAR
#define GPMUI_ENABLE_TABCONTROL
#define GPMUI_ENABLE_LISTBOX
#define GPMUI_ENABLE_IMAGEBUTTON
#define GPMUI_ENABLE_GRAPHBUTTON
#define GPMUI_ENABLE_SUPERLISTBOX
#define GPMUI_ENABLE_DATAGRID

// ============================================================
// 窗口样式标志
// ============================================================
#define GPMWND_STYLE_CLOSE       0x01
#define GPMWND_STYLE_MINIMIZE    0x04
#define GPMWND_STYLE_MAXIMIZE    0x02
#define GPMWND_STYLE_MOVEABLE    0x800
#define GPMWND_STYLE_CENTER      0x20000
#define GPMWND_STYLE_TITLE       0x100
#define GPMWND_STYLE_SIZEABLE    0x400
#define GPMWND_STYLE_MAINWINDOW  0x10000
#define GPMWND_STYLE_RESIZEABLE  0x400
#define EFF_BLUR                0x01

// ============================================================
// D2D 工具函数
// ============================================================
inline D2D1_COLOR_F ColorRefToD2D(COLORREF cr, float alpha = 1.0f) {
    return D2D1::ColorF(
        (float)GetRValue(cr) / 255.0f,
        (float)GetGValue(cr) / 255.0f,
        (float)GetBValue(cr) / 255.0f,
        alpha);
}

inline D2D1_ROUNDED_RECT MakeRoundRect(float x, float y, float w, float h, float r) {
    if (r > w / 2.0f) r = w / 2.0f;
    if (r > h / 2.0f) r = h / 2.0f;
    return D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), r, r);
}

// ============================================================
// 自定义 ComPtr (兼容 MinGW)
// ============================================================
namespace Detail {
template<typename T>
class ComPtr {
public:
    ComPtr() : m_ptr(nullptr) {}
    ComPtr(std::nullptr_t) : m_ptr(nullptr) {}
    explicit ComPtr(T* p) : m_ptr(p) { if (m_ptr) m_ptr->AddRef(); }
    ComPtr(const ComPtr& other) : m_ptr(other.m_ptr) { if (m_ptr) m_ptr->AddRef(); }
    ComPtr(ComPtr&& other) noexcept : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }
    ~ComPtr() { if (m_ptr) m_ptr->Release(); }

    ComPtr& operator=(T* p) {
        if (m_ptr != p) { if (m_ptr) m_ptr->Release(); m_ptr = p; if (m_ptr) m_ptr->AddRef(); }
        return *this;
    }
    ComPtr& operator=(const ComPtr& other) { return *this = other.m_ptr; }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) { if (m_ptr) m_ptr->Release(); m_ptr = other.m_ptr; other.m_ptr = nullptr; }
        return *this;
    }

    T* Get() const { return m_ptr; }
    T** GetAddressOf() { return &m_ptr; }
    void Reset() { if (m_ptr) { m_ptr->Release(); m_ptr = nullptr; } }
    T* Detach() { T* p = m_ptr; m_ptr = nullptr; return p; }
    void Attach(T* p) { if (m_ptr) m_ptr->Release(); m_ptr = p; }
    T* operator->() const { return m_ptr; }
    explicit operator bool() const { return m_ptr != nullptr; }
    bool operator!() const { return m_ptr == nullptr; }
    // 允许 &comPtr 作为 T** 传递给COM函数（类似 WRL::ComPtr 的行为）
    T** operator&() { return &m_ptr; }
private:
    T* m_ptr;
};

// ComPtr赋值运算符特化: ID2D1Bitmap赋值
struct ComPtrAssignTag {};
} // namespace Detail

// 使 ComPtr 在 gpm_ui 命名空间中可直接使用
using Detail::ComPtr;

// ============================================================
// 状态枚举
// ============================================================
enum ElementState {
    STATE_NORMAL  = 0,
    STATE_HOVER   = 1,
    STATE_DOWN    = 2,
    STATE_FOCUS   = 3,
    STATE_DISABLE = 4
};
using ControlState = ElementState;

// ============================================================
// ExDPI
// ============================================================
class ExDPI {
public:
    static void Init();
    static float GetScale();
    static int Scale(int value);
    static float ScaleF(float value);
private:
    static float s_scale;
    static bool  s_inited;
};

// ============================================================
// GpmThemeColors
// ============================================================
struct GpmThemeColors {
    COLORREF bgWindow, bgTitleBar, bgEditor, bgInput, bgHover, bgActive;
    COLORREF bgSelection, bgDisabled;
    COLORREF fgPrimary, fgSecondary, fgDisabled, fgAccent;
    COLORREF border, borderFocus, borderHover;
    COLORREF btnBg, btnBgHover, btnBgDown, btnFg;
    COLORREF sliderTrack, sliderFill, sliderThumb;
    COLORREF checkMark;
    COLORREF closeHover;
    COLORREF progressBg, progressFill, progressText;
    COLORREF listBg, listItemBg, listItemHover, listItemSelected;
    COLORREF listItemText, listBorder, listScrollbar, listScrollThumb;
    COLORREF editBg, editFg, editCaret, editSelection;
    COLORREF tabBg, tabActive, tabInactive, tabHover, tabBorder;
};
GpmThemeColors GetOneDarkProTheme();
GpmThemeColors& Theme();
void SetTheme(const GpmThemeColors& theme);

// ============================================================
// StateColors
// ============================================================
struct StateColors {
    COLORREF normal, hover, down;
    StateColors() : normal(0), hover(0), down(0) {}
    StateColors(COLORREF n, COLORREF h, COLORREF d) : normal(n), hover(h), down(d) {}
    COLORREF Get(ElementState state) const {
        switch (state) {
        case STATE_HOVER:   return hover;
        case STATE_DOWN:    return down;
        case STATE_DISABLE: return RGB(76, 82, 94);
        default:            return normal;
        }
    }
};

// ============================================================
// ExStyle
// ============================================================
struct ExStyle {
    StateColors bgColors, textColors;
    COLORREF borderColor, borderHoverColor, borderFocusColor;
    int cornerRadius;
    float fontSize, borderWidth;
    bool bold;
    float opacity;

    ExStyle() : borderColor(0), borderHoverColor(0), borderFocusColor(0),
        cornerRadius(0), fontSize(10.0f), borderWidth(1.0f), bold(false), opacity(1.0f) {}

    void ApplyTheme_Button();
    void ApplyTheme_Label();
    void ApplyTheme_Edit();
    void ApplyTheme_CheckBox();
    void ApplyTheme_Slider();
    void ApplyTheme_ComboBox();
    void ApplyTheme_ListBox();
    void ApplyTheme_Tab();
};

// ============================================================
// 布局锚点
// ============================================================
enum AnchorFlags {
    ANCHOR_NONE    = 0,
    ANCHOR_LEFT    = 1 << 0,
    ANCHOR_TOP     = 1 << 1,
    ANCHOR_RIGHT   = 1 << 2,
    ANCHOR_BOTTOM  = 1 << 3,
    ANCHOR_CENTER_X = 1 << 4,
    ANCHOR_CENTER_Y = 1 << 5
};

struct LayoutInfo {
    int anchor;
    int leftMargin, topMargin, rightMargin, bottomMargin;
    float leftPercent, topPercent, widthPercent, heightPercent;
    LayoutInfo() : anchor(ANCHOR_NONE), leftMargin(0), topMargin(0),
        rightMargin(0), bottomMargin(0), leftPercent(0), topPercent(0),
        widthPercent(0), heightPercent(0) {}
};

// ============================================================
// ExD2DFactory (使用自定义ComPtr, 不带IDWriteFactory5)
// ============================================================
class ExD2DFactory {
public:
    static bool Init();
    static void UnInit();
    static ID2D1Factory* GetFactory();
    static IDWriteFactory* GetDWriteFactory();
    static IWICImagingFactory* GetWICFactory();
    static IDWriteTextFormat* CreateTextFormat(float fontSize, bool bold,
        DWRITE_TEXT_ALIGNMENT hAlign = DWRITE_TEXT_ALIGNMENT_LEADING,
        DWRITE_PARAGRAPH_ALIGNMENT vAlign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    static IDWriteTextLayout* CreateTextLayout(const std::wstring& text,
        float fontSize, float maxWidth, float maxHeight, bool bold = false);
private:
    static Detail::ComPtr<ID2D1Factory>      s_factory;
    static Detail::ComPtr<IDWriteFactory>    s_dwFactory;
    static Detail::ComPtr<IWICImagingFactory> s_wicFactory;
};

// ============================================================
// ExFont
// ============================================================
class ExFont {
public:
    static void SetGlobalFont(const std::wstring& fontName);
    static const std::wstring& GetGlobalFont();
    static void SetDefaultFontSize(float size);
    static float GetDefaultFontSize();
    static bool RegisterFontFile(const std::wstring& path);
    static bool RegisterFontMemory(const void* data, DWORD size);
private:
    static std::wstring s_fontName;
    static float s_defaultFontSize;
};

// ============================================================
// 前置声明
// ============================================================
class UIElement;
class GpmWindow;

// ============================================================
// ResizeDir
// ============================================================
enum ResizeDir {
    RSZ_NONE      = 0,
    RSZ_LEFT      = 1,
    RSZ_RIGHT     = 2,
    RSZ_TOP       = 4,
    RSZ_BOTTOM    = 8,
    RSZ_TOPLEFT   = 5,
    RSZ_TOPRIGHT  = 6,
    RSZ_BOTTOMLEFT = 9,
    RSZ_BOTTOMRIGHT = 10
};

// ============================================================
// UIElement 基类
// ============================================================
class UIElement {
public:
    UIElement() : m_x(0), m_y(0), m_width(0), m_height(0), m_id(0),
        m_visible(true), m_enabled(true), m_hitThrough(false),
        m_state(STATE_NORMAL), m_hWnd(nullptr), m_parent(nullptr),
        m_parentWnd(nullptr), m_dirty(false) { m_style.opacity = 1.0f; }
    virtual ~UIElement() {}

    void SetText(const std::wstring& text, bool redraw = true);
    void SetVisible(bool v, bool redraw = true);
    void SetEnabled(bool e, bool redraw = true);
    void GetRect(RECT& rc) const;
    void SetRect(int x, int y, int w, int h, bool redraw = true);
    void SetBkColor(COLORREF normal, COLORREF hover = 0, COLORREF down = 0);
    void SetTextColor(COLORREF normal, COLORREF hover = 0, COLORREF down = 0);
    void SetOpacity(float opacity, bool redraw = true);
    virtual void ApplyTheme();

    int GetX() const { return m_x; }
    int GetY() const { return m_y; }
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    int GetID() const { return m_id; }
    bool IsVisible() const { return m_visible; }
    bool IsEnabled() const { return m_enabled; }
    bool IsHitThrough() const { return m_hitThrough; }
    void SetHitThrough(bool h) { m_hitThrough = h; }
    ElementState GetState() const { return m_state; }
    void SetState(ElementState s) { m_state = s; }
    float GetOpacity() const { return m_style.opacity; }
    ExStyle& GetStyle() { return m_style; }
    const ExStyle& GetStyle() const { return m_style; }

    // 控件树
    void AddChild(std::unique_ptr<UIElement> child);
    void RemoveChild(UIElement* child);
    UIElement* GetChild(int index) const;
    UIElement* FindChildByID(int id);
    UIElement* HitTest(int x, int y);
    int GetChildCount() const { return (int)m_children.size(); }
    UIElement* GetParent() const { return m_parent; }
    void SetParent(UIElement* p) { m_parent = p; }
    void SetWindowHandle(HWND h) { m_hWnd = h; }
    void SetParentWindow(GpmWindow* w) { m_parentWnd = w; }

    // 布局
    void UpdateLayout(const RECT& parentRect);
    LayoutInfo& GetLayout() { return m_layout; }

    // 脏矩形
    void Invalidate();
    void InvalidateRect(const D2D1_RECT_F& rect);

    // 消息
    virtual bool OnMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* pResult);
    virtual void OnMouseMove(int x, int y) {}
    virtual void OnMouseLeave() {}
    virtual void OnLButtonDown(int x, int y) {}
    virtual void OnLButtonUp(int x, int y) {}
    virtual void OnLButtonDblClk(int x, int y) {}
    virtual void OnMouseWheel(int x, int y, int delta) {}
    virtual void OnKeyDown(UINT vk) {}
    virtual void OnKeyUp(UINT vk) {}
    virtual void OnChar(wchar_t ch) {}
    virtual void OnSetFocus() {}
    virtual void OnKillFocus() {}
    virtual void OnWindowMoved() {}  // 父窗口移动时回调
    virtual void OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) = 0;

    // 绘制辅助
    void DrawBackgroundD2D(ID2D1RenderTarget* rt, float x, float y, float w, float h,
                           COLORREF bkColor, float opacity = -1.0f);
    void DrawTextD2D(ID2D1RenderTarget* rt, const std::wstring& text, float x, float y,
                     float w, float h, COLORREF color, float fontSize = 10.0f,
                     bool bold = false,
                     DWRITE_TEXT_ALIGNMENT hAlign = DWRITE_TEXT_ALIGNMENT_CENTER,
                     DWRITE_PARAGRAPH_ALIGNMENT vAlign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
protected:
    int m_x, m_y, m_width, m_height;
    int m_id;
    bool m_visible, m_enabled, m_hitThrough;
    ElementState m_state;
    std::wstring m_text;
    ExStyle m_style;
    LayoutInfo m_layout;
    HWND m_hWnd;
    UIElement* m_parent;
    GpmWindow* m_parentWnd;
    std::vector<std::unique_ptr<UIElement>> m_children;
    bool m_dirty;
    D2D1_RECT_F m_dirtyRect;
};

// ============================================================
// GpmWindow - 使用 D2D 分层透明 + 圆角裁剪
// ============================================================
class GpmWindow {
public:
    GpmWindow();
    ~GpmWindow();

    bool Create(HWND hParent, int x, int y, int width, int height,
                const std::wstring& title,
                DWORD style = GPMWND_STYLE_CLOSE | GPMWND_STYLE_TITLE | GPMWND_STYLE_MOVEABLE);
    void Show(int nCmdShow = SW_SHOW);
    void Hide();
    void SetBackgroundColor(COLORREF color);
    void AddControl(UIElement* ctrl);
    UIElement* GetControlByID(int id);
    void Redraw();
    void SetFocusControl(int id);
    int Run();
    HWND GetWindowHandle() const { return m_hWnd; }

    void CreateDIB(int w, int h);
    void CreateRenderTarget();
    void DestroyRenderTarget();
    void DoPaint();

    void DrawTitleBar(ID2D1RenderTarget* rt, float w, float h);
    void DrawCloseButton(ID2D1RenderTarget* rt, D2D1_RECT_F rc, bool hover, bool down);
    void DrawMinButton(ID2D1RenderTarget* rt, D2D1_RECT_F rc, bool hover);
    int HitTestTitleButton(int x, int y);

    // 窗口圆角 (D2D绘制, 不使用SetWindowRgn)
    void UpdateWindowRegion();

    // 鼠标调整尺寸
    ResizeDir HitTestResizeBorder(int x, int y) const;
    void UpdateResizeCursor(ResizeDir dir);
    void PerformResize(int screenX, int screenY);

    void RouteMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void SetWindowCornerRadius(int r) { m_windowCornerRadius = r; }
    int GetWindowCornerRadius() const { return m_windowCornerRadius; }
    void SetTitleBarHeight(int h) { m_titleBarHeight = h; }
    DWORD GetExStyle() const { return m_exStyle; }
    bool IsMainWindow() const { return m_isMainWindow; }
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

    int GetControlCount() const { return (int)m_controls.size(); }
    UIElement* GetControl(int index) const { if (index >= 0 && index < (int)m_controls.size()) return m_controls[index]; return nullptr; }

    static bool RegisterWindowClass();
    static const wchar_t* GetWindowClassName();
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hWnd;
    DWORD m_exStyle;
    std::wstring m_text;
    bool m_isMainWindow;
    int m_x, m_y, m_width, m_height;
    int m_titleBarHeight;
    int m_windowCornerRadius;
    COLORREF m_bkColor;
    int m_resizeBorder;

    int m_hoverBtn, m_downBtn;
    bool m_dragging;
    POINT m_dragStart;
    bool m_resizing;
    ResizeDir m_resizeDir;
    RECT m_resizeStartRect;
    POINT m_resizeStartPoint;

    std::vector<UIElement*> m_controls;
    UIElement* m_focusCtrl;
    UIElement* m_captureCtrl;
    UIElement* m_lastHoverCtrl;

    Detail::ComPtr<ID2D1DCRenderTarget> m_renderTarget;
    HDC m_hMemDC;
    HBITMAP m_hBmp;
    void* m_pBits;
    int m_rtWidth, m_rtHeight;
    bool m_painting; // 重入保护

    static bool s_classRegistered;
    static constexpr int HIT_NONE  = 0;
    static constexpr int HIT_CLOSE = 1;
    static constexpr int HIT_MIN   = 2;
};

// ============================================================
// GpmApp
// ============================================================
class GpmApp {
public:
    GpmApp();
    ~GpmApp();
    bool Init(const std::wstring& fontName = L"Microsoft YaHei UI", float fontSize = 10.0f);
    void UnInit();
    bool IsInited() const { return m_inited; }
private:
    bool m_inited;
};

// ============================================================
// 回调类型别名 (多个控件共享，无条件定义)
// ============================================================
using ClickCallback = void(*)(UIElement*, int);
using RadioCallback = void(*)(UIElement*, int);
using SliderCallback = void(*)(UIElement*, int, int);
using ComboSelectCallback = void(*)(UIElement*, int, int, const std::wstring&);
using TextChangeCallback = void(*)(UIElement*, int, const std::wstring&);
using TabSelectCallback = void(*)(UIElement*, int, int, const std::wstring&);
using ListBoxSelectCallback = void(*)(UIElement*, int, int, const std::wstring&);
using ValueCallback = void(*)(UIElement*, int, int);
using SelectCallback = void(*)(UIElement*, int, int, const std::wstring&);

// ============================================================
// 控件类声明 (受宏控制)
// ============================================================

#ifdef GPMUI_ENABLE_BUTTON
// ---- GpmButton ----
class GpmButton : public UIElement {
public:
    GpmButton();
    ~GpmButton() override;
    void ApplyTheme() override;
    void Create(GpmWindow* parent, int x, int y, int w, int h,
                const std::wstring& text, int id = 0);
    void OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) override;
    void OnMouseMove(int x, int y) override;
    void OnMouseLeave() override;
    void OnLButtonDown(int x, int y) override;
    void OnLButtonUp(int x, int y) override;
    void SetClickCallback(ClickCallback cb) { m_clickCb = cb; }
private:
    ClickCallback m_clickCb;
    int m_cornerRadius;
};
#endif // GPMUI_ENABLE_BUTTON

#ifdef GPMUI_ENABLE_LABEL
// ---- GpmLabel ----
enum LabelAlign { LABEL_LEFT = 0, LABEL_CENTER = 1, LABEL_RIGHT = 2 };
class GpmLabel : public UIElement {
public:
    GpmLabel();
    ~GpmLabel() override;
    void ApplyTheme() override;
    void Create(GpmWindow* parent, int x, int y, int w, int h,
                const std::wstring& text, int id = 0);
    void OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) override;
    void SetAlign(LabelAlign align) { m_align = align; }
private:
    int m_cornerRadius;
    LabelAlign m_align;
};
#endif // GPMUI_ENABLE_LABEL

#ifdef GPMUI_ENABLE_CHECKBOX
// ---- GpmCheckBox ----
class GpmCheckBox : public UIElement {
public:
    GpmCheckBox();
    ~GpmCheckBox() override;
    void ApplyTheme() override;
    void Create(GpmWindow* parent, int x, int y, int w, int h,
                const std::wstring& text, int id = 0);
    void OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) override;
    void OnMouseMove(int x, int y) override;
    void OnMouseLeave() override;
    void OnLButtonDown(int x, int y) override;
    void OnLButtonUp(int x, int y) override;
    bool IsChecked() const { return m_checked; }
    void SetChecked(bool c, bool redraw = true);
    void SetCallback(ClickCallback cb) { m_clickCb = cb; }
private:
    bool m_checked;
    int m_checkSize;
    int m_cornerRadius;
    ClickCallback m_clickCb;
};
#endif // GPMUI_ENABLE_CHECKBOX

#ifdef GPMUI_ENABLE_RADIOBUTTON
// ---- GpmRadioButton ----
class GpmRadioButton : public UIElement {
public:
    GpmRadioButton();
    ~GpmRadioButton() override;
    void ApplyTheme() override;
    void Create(GpmWindow* parent, int x, int y, int w, int h,
                const std::wstring& text, int groupId = 0, int id = 0);
    void OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) override;
    void OnMouseMove(int x, int y) override;
    void OnMouseLeave() override;
    void OnLButtonDown(int x, int y) override;
    void OnLButtonUp(int x, int y) override;
    bool IsSelected() const { return m_selected; }
    void SetSelected(bool sel, bool redraw = true);
    int GetGroupID() const { return m_groupId; }
    void AddToGroup(GpmRadioButton* other);
    void SetCallback(RadioCallback cb) { m_clickCb = cb; }
private:
    bool m_selected;
    int m_groupId, m_radioSize, m_cornerRadius;
    std::vector<GpmRadioButton*> m_groupSiblings;
    RadioCallback m_clickCb;
};
#endif // GPMUI_ENABLE_RADIOBUTTON

#ifdef GPMUI_ENABLE_SLIDER
// ---- GpmSlider ----
class GpmSlider : public UIElement {
public:
    GpmSlider();
    ~GpmSlider() override;
    void ApplyTheme() override;
    void Create(GpmWindow* parent, int x, int y, int w, int h,
                int minVal = 0, int maxVal = 100, int id = 0);
    void OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) override;
    void OnMouseMove(int x, int y) override;
    void OnMouseLeave() override;
    void OnLButtonDown(int x, int y) override;
    void OnLButtonUp(int x, int y) override;
    int GetValue() const { return m_value; }
    void SetValue(int v, bool redraw = true);
    void SetRange(int minV, int maxV);
    void SetCallback(SliderCallback cb) { m_valueCb = cb; }
private:
    int ValueFromX(int mx, D2D1_RECT_F rc);
    int m_minVal, m_maxVal, m_value;
    bool m_draggingThumb;
    int m_thumbSize, m_trackHeight;
    COLORREF m_trackColor, m_fillColor, m_thumbColor;
    SliderCallback m_valueCb;
};
#endif // GPMUI_ENABLE_SLIDER

#ifdef GPMUI_ENABLE_COMBOBOX
// ---- GpmComboBox ----
class GpmComboBox : public UIElement {
public:
    GpmComboBox();
    ~GpmComboBox() override;
    void ApplyTheme() override;
    void Create(GpmWindow* parent, int x, int y, int w, int h, int id = 0);
    void OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) override;
    void OnMouseMove(int x, int y) override;
    void OnMouseLeave() override;
    void OnLButtonDown(int x, int y) override;
    void OnLButtonUp(int x, int y) override;
    void OnWindowMoved() override { if (m_dropVisible) HideDropdown(); }
    void AddItem(const std::wstring& text);
    void ClearItems();
    int GetSelectedIndex() const { return m_selIndex; }
    void SetSelectedIndex(int idx, bool redraw = true);
    std::wstring GetSelectedText() const;
    void SetSelectCallback(ComboSelectCallback cb) { m_selectCb = cb; }
private:
    void ShowDropdown();
    void HideDropdown();
    void CreateDropRT();
    void DestroyDropRT();
    void DrawDropList(ID2D1RenderTarget* rt);
    static LRESULT CALLBACK DropWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    std::vector<std::wstring> m_items;
    int m_selIndex, m_hoverIndex, m_itemHeight, m_cornerRadius;
    bool m_dropVisible;
    HWND m_dropWnd;
    Detail::ComPtr<ID2D1HwndRenderTarget> m_dropRT;
    COLORREF m_dropBg, m_dropHover, m_dropText;
    ComboSelectCallback m_selectCb;
    static bool s_dropClassRegistered;
};
#endif // GPMUI_ENABLE_COMBOBOX

#define EEM_MULTILINE 0x0001

#ifdef GPMUI_ENABLE_EDIT
// ---- GpmEdit ----
class GpmEdit : public UIElement {
public:
    GpmEdit();
    ~GpmEdit() override;
    void ApplyTheme() override;
    void Create(GpmWindow* parent, int x, int y, int w, int h,
                const std::wstring& text = L"", int id = 0);
    void OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) override;
    void OnMouseMove(int x, int y) override;
    void OnLButtonDown(int x, int y) override;
    void OnLButtonUp(int x, int y) override;
    void OnLButtonDblClk(int x, int y) override;
    void OnSetFocus() override;
    void OnKillFocus() override;
    void OnKeyDown(UINT vk) override;
    void OnChar(wchar_t ch) override;
    void OnMouseWheel(int x, int y, int delta) override;
    bool OnMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* pResult) override;
    void SetText(const std::wstring& text, bool redraw = true);
    void AppendText(const std::wstring& text);
    void Clear();
    std::wstring GetText() const { return m_text; }
    void SetReadOnly(bool ro) { m_readOnly = ro; }
    bool IsReadOnly() const { return m_readOnly; }
    void SetPasswordMode(bool pwd, wchar_t ch = L'*') { m_passwordMode = pwd; m_passwordChar = ch; }
    void SetPlaceholder(const std::wstring& text) { m_placeholderText = text; }
    void SetCaretPos(int pos, bool redraw = true);
    int GetCaretPos() const { return m_caretPos; }
    void SetSelection(int start, int end);
    void ClearSelection();
    void SelectAll() { SetSelection(0, (int)m_text.length()); }
    void SetTextCallback(TextChangeCallback cb) { m_textCb = cb; }
    void SetMultiline(bool ml) { m_multiline = ml; RecalcTextExtent(); Invalidate(); }
    bool IsMultiline() const { return m_multiline; }
    int GetLineCount() const { return (int)m_lines.size(); }
    std::wstring GetLine(int index) const { if (index >= 0 && index < (int)m_lines.size()) return m_lines[index]; return L""; }
private:
    std::vector<std::wstring> SplitLines(const std::wstring& text) const;
    void RecalcTextExtent();
    int LineFromChar(int pos) const;
    int CharPosFromLineCol(int line, int col) const;
    int LineLength(int line) const;
    int YFromLine(int line) const;
    int LineFromY(int y) const;
    void OnImeComposition(LPARAM lParam);
    HIMC m_hIMC;
    void CreateCaret();
    void DestroyCaret();
    void ShowCaret(bool show);
    void UpdateCaretPos();
    std::wstring GetDisplayText() const;
    int GetTextWidth() const;
    void EnsureCaretVisible();
    int CharPosFromX(int x) const;
    int XFromCharPos(int pos) const;
    int XFromCol(int line, int col) const;
    int ColFromX(int line, int x) const;
    void CopyToClipboard();
    void PasteFromClipboard();
    void CutToClipboard();
    int m_caretPos;
    bool m_hasFocus, m_caretVisible, m_showCaret, m_draggingSel, m_readOnly, m_passwordMode;
    wchar_t m_passwordChar;
    int m_selStart, m_selEnd, m_scrollOffsetX, m_scrollOffsetY, m_cornerRadius;
    bool m_multiline;
    int m_lineHeight;
    std::vector<std::wstring> m_lines;
    std::vector<int> m_lineWidths;
    int m_totalTextWidth, m_totalTextHeight;
    bool m_vScrollDragging, m_hScrollDragging;
    int m_scrollDragStartX, m_scrollDragStartY;
    int m_scrollDragStartOffX, m_scrollDragStartOffY;
    std::wstring m_placeholderText;
    TextChangeCallback m_textCb;
};
#endif // GPMUI_ENABLE_EDIT

#ifdef GPMUI_ENABLE_PROGRESSBAR
// ---- GpmProgressBar ----
class GpmProgressBar : public UIElement {
public:
    GpmProgressBar();
    ~GpmProgressBar() override;
    void ApplyTheme() override;
    void Create(GpmWindow* parent, int x, int y, int w, int h, int id = 0);
    void OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) override;
    int GetValue() const { return m_value; }
    int GetMinVal() const { return m_minVal; }
    int GetMaxVal() const { return m_maxVal; }
    void SetValue(int v, bool redraw = true);
    void SetRange(int minV, int maxV);
    void SetProgressColors(COLORREF bg, COLORREF fill, COLORREF text);
    void SetShowText(bool show) { m_showText = show; }
    bool IsShowText() const { return m_showText; }
private:
    COLORREF m_progressBg, m_progressFill, m_progressText;
    int m_minVal, m_maxVal, m_value;
    bool m_showText;
};
#endif // GPMUI_ENABLE_PROGRESSBAR

#ifdef GPMUI_ENABLE_TABCONTROL
// ---- GpmTabControl ----
struct ExTabPage {
    std::wstring title;
    std::vector<std::unique_ptr<UIElement>> controls;
};
class GpmTabControl : public UIElement {
public:
    GpmTabControl();
    ~GpmTabControl() override;
    void ApplyTheme() override;
    void Create(GpmWindow* parent, int x, int y, int w, int h, int id = 0);
    void OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) override;
    void OnMouseMove(int x, int y) override;
    void OnMouseLeave() override;
    void OnLButtonDown(int x, int y) override;
    void OnLButtonUp(int x, int y) override;
    int AddPage(const std::wstring& title);
    void RemovePage(int index);
    int GetPageCount() const { return (int)m_pages.size(); }
    int GetCurrentIndex() const { return m_currentIndex; }
    void SetCurrentIndex(int index, bool redraw = true);
    void AddControlToPage(int pageIndex, std::unique_ptr<UIElement> ctrl);
    void SetSelectCallback(TabSelectCallback cb) { m_selectCb = cb; }
private:
    int HitTestTab(int x, int y) const;
    void DrawTabHeader(ID2D1RenderTarget* rt, D2D1_RECT_F rc);
    void DrawTabContent(ID2D1RenderTarget* rt, D2D1_RECT_F rc);
    std::vector<ExTabPage> m_pages;
    int m_currentIndex, m_hoverTab, m_tabHeight;
    TabSelectCallback m_selectCb;
};
#endif // GPMUI_ENABLE_TABCONTROL

#ifdef GPMUI_ENABLE_LISTBOX
// ---- GpmListBox ----
struct ExListItem {
    std::wstring text;
    int height;
    COLORREF bgColor, textColor;
    std::vector<class ListItemCtrl> ctrls;
    ExListItem() : height(32), bgColor(0), textColor(0) {}
};
enum ListItemCtrlType { LICT_NONE = 0, LICT_BUTTON, LICT_CHECKBOX, LICT_PROGRESSBAR, LICT_SLIDER };
struct ListItemCtrl {
    ListItemCtrlType type;
    std::wstring text;
    int localX, localY, width, height, cornerRadius;
    int value, minVal, maxVal, state;
    COLORREF bkNormal, bkHover, bkDown, fgNormal, fgHover, fgDown;
    void (*clickCb)(UIElement*, int);
    ListItemCtrl() : type(LICT_NONE), localX(0), localY(0), width(0), height(0),
        cornerRadius(0), value(0), minVal(0), maxVal(100), state(0),
        bkNormal(0), bkHover(0), bkDown(0), fgNormal(0), fgHover(0), fgDown(0), clickCb(nullptr) {}
};
using VirtualGetCount = int(*)();
using VirtualGetItem = bool(*)(int index, std::wstring& text, int& height);
struct VirtualCache {
    std::wstring text; int height; bool valid;
    VirtualCache() : height(0), valid(false) {}
};

class GpmListBox : public UIElement {
public:
    GpmListBox();
    ~GpmListBox() override;
    void ApplyTheme() override;
    void Create(GpmWindow* parent, int x, int y, int w, int h, int id = 0);
    void OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) override;
    void OnMouseMove(int x, int y) override;
    void OnMouseLeave() override;
    void OnLButtonDown(int x, int y) override;
    void OnLButtonUp(int x, int y) override;
    void OnMouseWheel(int x, int y, int delta) override;
    int AddItem(const std::wstring& text, int height = 0);
    void RemoveItem(int index);
    void ClearItems();
    ExListItem* GetItem(int index);
    int GetItemCount() const { return m_virtualMode ? m_virtualCount : (int)m_items.size(); }
    void AddItemControl(int itemIndex, const ListItemCtrl& ctrl);
    int GetSelectedIndex() const { return m_selIndex; }
    void SetSelectedIndex(int idx, bool redraw = true);
    void SetSelectCallback(ListBoxSelectCallback cb) { m_selectCb = cb; }
    int GetScrollOffset() const { return m_scrollOffset; }
    void SetScrollOffset(int offset, bool redraw = true);
    void SetListColors(COLORREF bg, COLORREF itemBg, COLORREF itemHover,
                       COLORREF itemSelected, COLORREF itemText, COLORREF border);
    void SetScrollbarColors(COLORREF track, COLORREF thumb);
    void SetVirtualMode(VirtualGetCount countFn, VirtualGetItem itemFn);
    void UpdateVirtualData();
private:
    int GetTotalHeight() const;
    int GetVisibleHeight() const;
    int GetMaxScroll() const;
    int HitTestItem(int y) const;
    bool HitTestScrollbar(int x, int y) const;
    int GetScrollbarThumbRect(D2D1_RECT_F& outRect) const;
    ListItemCtrl* HitTestItemCtrl(int itemIdx, int mx, int my);
    void DrawItemCtrl(ID2D1RenderTarget* rt, const ListItemCtrl& ctrl, float itemX, float itemY, float opacity);
    bool GetVirtualItem(int index, std::wstring& text, int& height);
    void InvalidateVirtualCache() { m_virtualCache.clear(); }
    std::vector<ExListItem> m_items;
    int m_selIndex, m_hoverIndex, m_scrollOffset, m_defaultItemHeight;
    int m_cornerRadius;
    bool m_scrollDragging;
    int m_scrollDragStartY, m_scrollDragStartOffset;
    COLORREF m_listBg, m_listItemBg, m_listItemHover, m_listItemSelected;
    COLORREF m_listItemText, m_listBorder, m_scrollbarColor, m_scrollThumbColor;
    ListBoxSelectCallback m_selectCb;
    bool m_virtualMode;
    VirtualGetCount m_virtualGetCount;
    VirtualGetItem m_virtualGetItem;
    int m_virtualCount;
    std::map<int, VirtualCache> m_virtualCache;
};
#endif // GPMUI_ENABLE_LISTBOX

#ifdef GPMUI_ENABLE_IMAGEBUTTON
// ---- GpmImageButton ----
class GpmImageButton : public UIElement {
public:
    GpmImageButton();
    ~GpmImageButton() override;
    void ApplyTheme() override;
    void Create(GpmWindow* parent, int x, int y, int w, int h, int id = 0);
    void OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) override;
    void OnMouseMove(int x, int y) override;
    void OnMouseLeave() override;
    void OnLButtonDown(int x, int y) override;
    void OnLButtonUp(int x, int y) override;
    bool SetImageFromFile(ElementState state, const std::wstring& filePath);
    bool SetImageFromMemory(ElementState state, const void* data, size_t size);
    void SetClickCallback(ClickCallback cb) { m_clickCb = cb; }
private:
    ID2D1Bitmap* LoadBitmapFromFile(ID2D1RenderTarget* rt, const std::wstring& path);
    ID2D1Bitmap* LoadBitmapFromMemory(ID2D1RenderTarget* rt, const void* data, size_t size);
    ID2D1Bitmap* GetCurrentBitmap() const;
    void ReleaseBitmaps();
    std::wstring m_imagePaths[4];
    Detail::ComPtr<ID2D1Bitmap> m_bitmaps[4];
    std::pair<const void*, size_t> m_memoryData[4];
    ClickCallback m_clickCb;
};
#endif // GPMUI_ENABLE_IMAGEBUTTON

#ifdef GPMUI_ENABLE_GRAPHBUTTON
// ---- GpmGraphButton ----
enum GraphIconType {
    ICON_NONE = 0,
    ICON_PLAY, ICON_STOP, ICON_PLUS, ICON_MINUS,
    ICON_CHECK, ICON_CROSS, ICON_ARROW_UP, ICON_ARROW_DOWN,
    ICON_GEAR, ICON_SEARCH
};
enum GraphIconLayout { ICON_LEFT = 0, ICON_TOP = 1 };

class GpmGraphButton : public UIElement {
public:
    GpmGraphButton();
    ~GpmGraphButton() override;
    void ApplyTheme() override;
    void Create(GpmWindow* parent, int x, int y, int w, int h,
                const std::wstring& text = L"", int id = 0);
    void OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) override;
    void OnMouseMove(int x, int y) override;
    void OnMouseLeave() override;
    void OnLButtonDown(int x, int y) override;
    void OnLButtonUp(int x, int y) override;
    void SetClickCallback(ClickCallback cb) { m_clickCb = cb; }
    void SetIcon(GraphIconType type, COLORREF color = 0);
    void SetIconLayout(GraphIconLayout layout) { m_iconLayout = layout; }
    void SetIconSize(int size) { m_iconSize = size; }
private:
    void DrawIcon(ID2D1RenderTarget* rt, float x, float y, float size, COLORREF color);
    ClickCallback m_clickCb;
    int m_cornerRadius;
    GraphIconType m_iconType;
    GraphIconLayout m_iconLayout;
    int m_iconSize;
    COLORREF m_iconColor;
};
#endif // GPMUI_ENABLE_GRAPHBUTTON

// ============================================================
// 前向声明 for GpmSuperListBox
// ============================================================
#ifdef GPMUI_ENABLE_SUPERLISTBOX
enum SuperListItemCtrlType {
    SLICT_NONE = 0,
    SLICT_BUTTON,
    SLICT_LABEL,
    SLICT_PROGRESSBAR,
    SLICT_SLIDER,
    SLICT_COMBOBOX,
    SLICT_CHECKBOX
};

struct SuperListItemCtrl {
    SuperListItemCtrlType type;
    std::wstring text;
    int localX, localY, width, height, cornerRadius;
    int value, minVal, maxVal, state;
    std::vector<std::wstring> comboItems;
    COLORREF bkNormal, bkHover, bkDown, fgNormal, fgHover, fgDown;
    ClickCallback clickCb;
    ValueCallback valueCb;
    SelectCallback selectCb;

    SuperListItemCtrl() : type(SLICT_NONE), localX(0), localY(0), width(0), height(0),
        cornerRadius(0), value(0), minVal(0), maxVal(100), state(STATE_NORMAL),
        bkNormal(0), bkHover(0), bkDown(0), fgNormal(0), fgHover(0), fgDown(0),
        clickCb(nullptr), valueCb(nullptr), selectCb(nullptr) {}
};

struct SuperListItem {
    std::wstring text;
    int height;
    COLORREF textColor, bgColor;
    float fontSize;
    bool bold;
    std::vector<SuperListItemCtrl> ctrls;
    SuperListItem() : height(36), textColor(0), bgColor(0), fontSize(0), bold(false) {}
};

// ---- GpmSuperListBox ----
class GpmSuperListBox : public UIElement {
public:
    GpmSuperListBox();
    ~GpmSuperListBox() override;
    void ApplyTheme() override;
    void Create(GpmWindow* parent, int x, int y, int w, int h, int id = 0);
    void OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) override;
    void OnMouseMove(int x, int y) override;
    void OnMouseLeave() override;
    void OnLButtonDown(int x, int y) override;
    void OnLButtonUp(int x, int y) override;
    void OnMouseWheel(int x, int y, int delta) override;
    int AddItem(const std::wstring& text, int height = 0);
    void RemoveItem(int index);
    void ClearItems();
    SuperListItem* GetItem(int index);
    int GetItemCount() const { return (int)m_items.size(); }
    int GetSelectedIndex() const { return m_selIndex; }
    void SetSelectedIndex(int idx, bool redraw = true);
    void SetSelectCallback(ListBoxSelectCallback cb) { m_selectCb = cb; }
    int GetScrollOffset() const { return m_scrollOffset; }
    void SetScrollOffset(int offset, bool redraw = true);
    void SetListColors(COLORREF bg, COLORREF itemBg, COLORREF itemHover,
                       COLORREF itemSelected, COLORREF itemText, COLORREF border);
    void SetScrollbarColors(COLORREF track, COLORREF thumb);
    // 嵌入控件
    void AddItemButton(int itemIdx, int localX, int localY, int w, int h,
                       const std::wstring& text, ClickCallback cb);
    void AddItemLabel(int itemIdx, int localX, int localY, int w, int h,
                      const std::wstring& text, COLORREF color = 0);
    void AddItemProgressBar(int itemIdx, int localX, int localY, int w, int h,
                            int value = 0, int minVal = 0, int maxVal = 100);
    void AddItemSlider(int itemIdx, int localX, int localY, int w, int h,
                       int value = 0, int minVal = 0, int maxVal = 100, ValueCallback cb = nullptr);
    void AddItemComboBox(int itemIdx, int localX, int localY, int w, int h,
                         const std::vector<std::wstring>& items, int selIdx = 0, SelectCallback cb = nullptr);
    void AddItemCheckBox(int itemIdx, int localX, int localY, int w, int h,
                         const std::wstring& text, bool checked = false, ClickCallback cb = nullptr);
    // 项目属性
    void SetItemTextColor(int index, COLORREF color);
    void SetItemBgColor(int index, COLORREF color);
    void SetItemHeight(int index, int height);
    void SetItemFont(int index, float fontSize, bool bold = false);
private:
    int GetTotalHeight() const;
    int GetVisibleHeight() const { return m_height; }
    int GetMaxScroll() const;
    int HitTestItem(int y) const;
    bool HitTestScrollbar(int x, int y) const;
    int GetScrollbarThumbRect(D2D1_RECT_F& outRect) const;
    SuperListItemCtrl* HitTestItemCtrl(int itemIdx, int mx, int my);
    void DrawItemCtrl(ID2D1RenderTarget* rt, const SuperListItemCtrl& ctrl,
                      float itemX, float itemY, float opacity);
    std::vector<SuperListItem> m_items;
    int m_selIndex, m_hoverIndex, m_scrollOffset, m_defaultItemHeight;
    int m_cornerRadius;
    bool m_scrollDragging;
    int m_scrollDragStartY, m_scrollDragStartOffset;
    bool m_sliderDragging;
    SuperListItemCtrl* m_sliderDragCtrl;
    COLORREF m_listBg, m_listItemBg, m_listItemHover, m_listItemSelected;
    COLORREF m_listItemText, m_listBorder, m_scrollbarColor, m_scrollThumbColor;
    ListBoxSelectCallback m_selectCb;
};
#endif // GPMUI_ENABLE_SUPERLISTBOX

// ============================================================
// GpmMessageBox 消息框控件宏
// ============================================================
#define GPMUI_ENABLE_MESSAGEBOX

#ifdef GPMUI_ENABLE_MESSAGEBOX
// ---- GpmMessageBox ----
class GpmMessageBox {
public:
    GpmMessageBox();
    ~GpmMessageBox();

    // 设置图标（可选）
    void SetIcon(ID2D1Bitmap* icon, int size = 0);

    // 显示消息框
    // hParent: 父窗口句柄（可为NULL，居中于屏幕）
    // title: 标题
    // message: 消息内容（支持自动换行）
    // buttons: 按钮文本，用 | 分隔，如 "确认|取消|重试"
    // autoCloseMs: 自动销毁时间（毫秒），0=不自动销毁
    // 返回值: 点击按钮的1-based索引，定时关闭或关闭窗口返回0
    int Show(HWND hParent, const std::wstring& title, const std::wstring& message,
             const std::wstring& buttons = L"确认",
             int autoCloseMs = 0);

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void DoPaint();
    void DestroyRT();
    void CreateDIB(int w, int h);
    void MeasureText(const std::wstring& text, float* outW, float* outH, float fontSize = 0);
    int HitTestButton(int x, int y) const;
    bool HitTestCloseBtn(int x, int y) const;
    void CloseWithResult(int result);

    HWND m_hWnd;
    int m_result;
    int m_autoCloseMs;
    ID2D1Bitmap* m_iconBitmap;
    int m_iconSize;

    std::wstring m_title;
    std::wstring m_message;
    std::vector<std::wstring> m_buttons;
    std::vector<int> m_btnWidths;

    int m_winWidth, m_winHeight;
    int m_buttonHeight, m_buttonGap, m_padding;
    int m_btnCornerRadius, m_cornerRadius;
    float m_fontSize;
    int m_hoverBtn;
    bool m_hoverClose;
    int m_closeBtnX, m_closeBtnY, m_closeBtnW, m_closeBtnH;
    float m_closeBtnR;
    bool m_dragging;
    POINT m_dragStart;

    Detail::ComPtr<ID2D1DCRenderTarget> m_renderTarget;
    HDC m_memDC;
    HBITMAP m_hBmp;
    void* m_pBits;
    int m_rtWidth, m_rtHeight;
    bool m_painting;
};
#endif // GPMUI_ENABLE_MESSAGEBOX

// ============================================================
// GpmDataGrid 类型声明 (依赖 GpmSuperListBox 类型)
// ============================================================
#ifdef GPMUI_ENABLE_DATAGRID
using GpmDataGridCellCtrl = SuperListItemCtrl;

struct GpmDataGridColumn {
    std::wstring header;
    int width;
    float fontSize;
    bool bold;
    DWRITE_TEXT_ALIGNMENT align;
    GpmDataGridColumn() : width(100), fontSize(9.5f), bold(false), align(DWRITE_TEXT_ALIGNMENT_CENTER) {}
};

struct GpmDataGridRow {
    std::vector<std::wstring> cellTexts;
    std::vector<std::vector<GpmDataGridCellCtrl>> cellCtrls;
    int height;
    COLORREF textColor, bgColor;
    GpmDataGridRow() : height(32), textColor(0), bgColor(0) {}
};

// ---- GpmDataGrid ----
class GpmDataGrid : public UIElement {
public:
    GpmDataGrid();
    ~GpmDataGrid() override;
    void ApplyTheme() override;
    void Create(GpmWindow* parent, int x, int y, int w, int h, int id = 0);
    void OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) override;
    void OnMouseMove(int x, int y) override;
    void OnMouseLeave() override;
    void OnLButtonDown(int x, int y) override;
    void OnLButtonUp(int x, int y) override;
    void OnMouseWheel(int x, int y, int delta) override;
    // 列
    int AddColumn(const std::wstring& header, int width = 100);
    void ClearColumns();
    GpmDataGridColumn* GetColumn(int index);
    void SetColumnWidth(int index, int width);
    int GetColumnCount() const { return (int)m_columns.size(); }
    // 行
    int AddRow(const std::vector<std::wstring>& cellTexts, int height = 0);
    void RemoveRow(int index);
    void ClearRows();
    GpmDataGridRow* GetRow(int index);
    int GetRowCount() const { return (int)m_rows.size(); }
    void SetCellText(int row, int col, const std::wstring& text);
    void SetRowTextColor(int row, COLORREF color);
    void SetRowBgColor(int row, COLORREF color);
    void SetRowHeight(int row, int height);
    // 单元格嵌入控件
    void AddCellButton(int row, int col, int localX, int localY, int w, int h,
                       const std::wstring& text, ClickCallback cb);
    void AddCellLabel(int row, int col, int localX, int localY, int w, int h,
                      const std::wstring& text, COLORREF color = 0);
    void AddCellProgressBar(int row, int col, int localX, int localY, int w, int h,
                            int value = 0, int minVal = 0, int maxVal = 100);
    void AddCellSlider(int row, int col, int localX, int localY, int w, int h,
                       int value = 0, int minVal = 0, int maxVal = 100, ValueCallback cb = nullptr);
    void AddCellCheckBox(int row, int col, int localX, int localY, int w, int h,
                         const std::wstring& text, bool checked = false, ClickCallback cb = nullptr);
    void AddCellComboBox(int row, int col, int localX, int localY, int w, int h,
                         const std::vector<std::wstring>& items, int selIdx = 0, SelectCallback cb = nullptr);
    // 选择/滚动
    int GetSelectedRow() const { return m_selRow; }
    void SetSelectedRow(int row, bool redraw = true);
    void SetSelectCallback(ListBoxSelectCallback cb) { m_selectCb = cb; }
    void SetScrollOffset(int x, int y, bool redraw = true);
    void SetGridColors(COLORREF bg, COLORREF headerBg, COLORREF headerFg,
                       COLORREF itemBg, COLORREF itemHover, COLORREF itemSelected,
                       COLORREF itemText, COLORREF border);
    void SetScrollbarColors(COLORREF track, COLORREF thumb);
private:
    int GetTotalWidth() const;
    int GetTotalHeight() const;
    int GetVisibleWidth() const { return m_width; }
    int GetVisibleHeight() const { return m_height - m_headerHeight; }
    int GetMaxScrollX() const;
    int GetMaxScrollY() const;
    int HitTestRow(int y) const;
    int HitTestCol(int x) const;
    bool HitTestVScrollbar(int x, int y) const;
    bool HitTestHScrollbar(int x, int y) const;
    void GetVScrollThumbRect(D2D1_RECT_F& outRect) const;
    void GetHScrollThumbRect(D2D1_RECT_F& outRect) const;
    int GetCellClientX(int col) const;
    int GetCellClientY(int row) const;
    GpmDataGridCellCtrl* HitTestCellCtrl(int row, int col, int mx, int my);
    void DrawCellCtrl(ID2D1RenderTarget* rt, const GpmDataGridCellCtrl& ctrl,
                      float cellX, float cellY, float opacity);
    std::vector<GpmDataGridColumn> m_columns;
    std::vector<GpmDataGridRow> m_rows;
    int m_selRow, m_hoverRow, m_hoverCol;
    int m_scrollOffsetX, m_scrollOffsetY;
    int m_defaultRowHeight, m_headerHeight;
    int m_cornerRadius;
    bool m_vScrollDragging, m_hScrollDragging;
    int m_scrollDragStartY, m_scrollDragStartOffY;
    int m_scrollDragStartX, m_scrollDragStartOffX;
    bool m_sliderDragging;
    GpmDataGridCellCtrl* m_sliderDragCtrl;
    int m_sliderDragRow, m_sliderDragCol;
    COLORREF m_gridBg, m_headerBg, m_headerFg;
    COLORREF m_itemBg, m_itemHover, m_itemSelected, m_itemText, m_gridBorder;
    COLORREF m_scrollbarBg, m_scrollThumb;
    ListBoxSelectCallback m_selectCb;
    int m_vScrollBarW, m_hScrollBarH;
};
#endif // GPMUI_ENABLE_DATAGRID

} // namespace gpm_ui
