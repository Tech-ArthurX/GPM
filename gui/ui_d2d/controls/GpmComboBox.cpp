/*
 * GpmComboBox.cpp - 组合框控�?(D2D渲染 + D2D自绘圆角下拉窗口)
 * ImGui风格：下拉箭头，悬停高亮�?D边框，自绘圆角下拉列�?
 * 使用ComPtr管理D2D资源
 */
#include "../core/gpm_ui.h"

#ifdef GPMUI_ENABLE_COMBOBOX

namespace gpm_ui {

bool GpmComboBox::s_dropClassRegistered = false;

GpmComboBox::GpmComboBox() 
    : m_selIndex(-1), m_hoverIndex(-1), m_itemHeight(0), 
      m_cornerRadius(0), m_dropVisible(false), m_dropWnd(NULL),
      m_selectCb(nullptr)
{ ApplyTheme(); }
GpmComboBox::~GpmComboBox() {
    DestroyDropRT();
    if (m_dropWnd && ::IsWindow(m_dropWnd)) ::DestroyWindow(m_dropWnd);
}

void GpmComboBox::ApplyTheme() {
    m_style.ApplyTheme_ComboBox();
    auto& t = Theme();
    m_dropBg = t.bgInput;
    m_dropHover = t.bgHover;
    m_dropText = t.fgPrimary;
}

void GpmComboBox::Create(GpmWindow* parent, int x, int y, int w, int h, int id) {
    m_parentWnd = parent;
    m_hWnd = parent ? parent->GetWindowHandle() : NULL;
    m_x = ExDPI::Scale(x); m_y = ExDPI::Scale(y);
    m_width = ExDPI::Scale(w); m_height = ExDPI::Scale(h);
    m_id = id;
    m_itemHeight = ExDPI::Scale(28);
    m_cornerRadius = m_style.cornerRadius;
    if (parent) parent->AddControl(this);
}

void GpmComboBox::AddItem(const std::wstring& text) { m_items.push_back(text); }
void GpmComboBox::ClearItems() { m_items.clear(); m_selIndex = -1; Invalidate(); }

std::wstring GpmComboBox::GetSelectedText() const {
    if (m_selIndex >= 0 && m_selIndex < (int)m_items.size()) return m_items[m_selIndex];
    return L"";
}

void GpmComboBox::SetSelectedIndex(int idx, bool redraw) {
    m_selIndex = idx;
    if (redraw) Invalidate();
}

void GpmComboBox::OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) {
    if (!m_visible || !rt) return;

    COLORREF bkC = m_style.bgColors.Get(m_state);
    float x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
    float cr = (float)m_style.cornerRadius;

    // 背景
    ID2D1SolidColorBrush* brush = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(bkC, m_style.opacity), &brush);
    if (brush) {
        if (cr > 0) {
            D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, w, h, cr);
            rt->FillRoundedRectangle(&rr, brush);
        } else {
            rt->FillRectangle(D2D1::RectF(x, y, x + w, y + h), brush);
        }
        brush->Release();
    }

    // 3D边框
    ID2D1SolidColorBrush* lightBr = nullptr;
    ID2D1SolidColorBrush* darkBr = nullptr;
    COLORREF lightBorder = RGB(
        (std::min)(255, (int)GetRValue(m_style.borderColor) + 15),
        (std::min)(255, (int)GetGValue(m_style.borderColor) + 15),
        (std::min)(255, (int)GetBValue(m_style.borderColor) + 15));
    COLORREF darkBorder = RGB(
        (std::max)(0, (int)GetRValue(m_style.borderColor) - 15),
        (std::max)(0, (int)GetGValue(m_style.borderColor) - 15),
        (std::max)(0, (int)GetBValue(m_style.borderColor) - 15));
    rt->CreateSolidColorBrush(ColorRefToD2D(lightBorder, m_style.opacity), &lightBr);
    rt->CreateSolidColorBrush(ColorRefToD2D(darkBorder, m_style.opacity), &darkBr);
    if (lightBr && darkBr) {
        if (cr > 0) {
            D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, w, h, cr);
            rt->DrawRoundedRectangle(&rr, darkBr, 1.0f);
        } else {
            rt->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(x + w, y), lightBr, 1.0f);
            rt->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(x, y + h), lightBr, 1.0f);
            rt->DrawLine(D2D1::Point2F(x, y + h), D2D1::Point2F(x + w, y + h), darkBr, 1.0f);
            rt->DrawLine(D2D1::Point2F(x + w, y), D2D1::Point2F(x + w, y + h), darkBr, 1.0f);
        }
        lightBr->Release();
        darkBr->Release();
    } else {
        if (lightBr) lightBr->Release();
        if (darkBr) darkBr->Release();
    }

    // 文字
    std::wstring dispText = GetSelectedText();
    if (dispText.empty() && !m_items.empty()) dispText = L"请选择...";

    IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, false,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (fmt) {
        float pad = ExDPI::ScaleF(8.0f);
        D2D1_RECT_F textRc = D2D1::RectF(x + pad, y, x + w - h, y + h);
        ID2D1SolidColorBrush* tb = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(m_style.textColors.Get(m_state), m_style.opacity), &tb);
        if (tb) {
            rt->DrawText(dispText.c_str(), (UINT32)dispText.length(), fmt, textRc, tb);
            tb->Release();
        }
        fmt->Release();
    }

    // 下拉箭头
    ID2D1SolidColorBrush* arrowBr = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(m_style.textColors.Get(m_state), m_style.opacity), &arrowBr);
    if (arrowBr) {
        float arrowCx = x + w - h / 2.0f;
        float arrowCy = y + h / 2.0f;
        float as = ExDPI::ScaleF(4.0f);
        ID2D1PathGeometry* pGeometry = nullptr;
        ExD2DFactory::GetFactory()->CreatePathGeometry(&pGeometry);
        if (pGeometry) {
            ID2D1GeometrySink* pSink = nullptr;
            pGeometry->Open(&pSink);
            if (pSink) {
                pSink->BeginFigure(D2D1::Point2F(arrowCx - as, arrowCy - as / 2), D2D1_FIGURE_BEGIN_FILLED);
                pSink->AddLine(D2D1::Point2F(arrowCx + as, arrowCy - as / 2));
                pSink->AddLine(D2D1::Point2F(arrowCx, arrowCy + as / 2));
                pSink->EndFigure(D2D1_FIGURE_END_CLOSED);
                pSink->Close();
                pSink->Release();
                rt->FillGeometry(pGeometry, arrowBr);
            }
            pGeometry->Release();
        }
        arrowBr->Release();
    }
}

void GpmComboBox::OnMouseMove(int x, int y) {
    if (!m_enabled) return;
    if (m_state != STATE_HOVER && !m_dropVisible) { m_state = STATE_HOVER; Invalidate(); }
}

void GpmComboBox::OnLButtonDown(int x, int y) {
    if (!m_enabled) return;
    m_state = STATE_DOWN; Invalidate();
}

void GpmComboBox::OnLButtonUp(int x, int y) {
    if (!m_enabled) return;
    m_state = STATE_HOVER; Invalidate();
    if (m_dropVisible) HideDropdown(); else ShowDropdown();
}

void GpmComboBox::OnMouseLeave() {
    if (!m_dropVisible && m_state != STATE_NORMAL) { m_state = STATE_NORMAL; Invalidate(); }
}

// ---- D2D自绘下拉窗口 (ComPtr版本 + 圆角) ----
void GpmComboBox::CreateDropRT() {
    if (m_dropRT) return;
    auto* factory = ExD2DFactory::GetFactory();
    if (!factory || !m_dropWnd) return;

    RECT rc; ::GetClientRect(m_dropWnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);
    if (size.width == 0 || size.height == 0) return;

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(m_dropWnd, size);
    ID2D1HwndRenderTarget* rt = nullptr;
    HRESULT hr = factory->CreateHwndRenderTarget(&rtProps, &hwndProps, &rt);

    if (FAILED(hr)) {
        rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(),
            0, 0,
            D2D1_RENDER_TARGET_USAGE_NONE,
            D2D1_FEATURE_LEVEL_DEFAULT);
        factory->CreateHwndRenderTarget(&rtProps, &hwndProps, &rt);
    }
    if (SUCCEEDED(hr) && rt) {
        m_dropRT.Attach(rt);
    }
}

void GpmComboBox::DestroyDropRT() {
    m_dropRT.Reset();
}

void GpmComboBox::ShowDropdown() {
    if (m_items.empty() || !m_hWnd) return;

    if (!s_dropClassRegistered) {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = GpmComboBox::DropWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"GPMUI_Drop_D2D";
        wc.hbrBackground = NULL;
        if (RegisterClassExW(&wc)) s_dropClassRegistered = true;
    }

    POINT pt = { m_x, m_y + m_height };
    ::ClientToScreen(m_hWnd, &pt);
    int dropH = (int)m_items.size() * m_itemHeight + 2;
    int maxH = ExDPI::Scale(200);
    if (dropH > maxH) dropH = maxH;

    m_dropWnd = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"GPMUI_Drop_D2D", L"", WS_POPUP,
        pt.x, pt.y, m_width, dropH, m_hWnd, NULL, GetModuleHandle(NULL), this);

    if (m_dropWnd) {
        // 设置圆角窗口区域
        int r = ExDPI::Scale(12);
        HRGN hrgn = ::CreateRoundRectRgn(0, 0, m_width + 1, dropH + 1, r, r);
        ::SetWindowRgn(m_dropWnd, hrgn, TRUE);
        ::DeleteObject(hrgn);
        
        m_dropVisible = true;
        ::ShowWindow(m_dropWnd, SW_SHOWNOACTIVATE);
    }
}

void GpmComboBox::HideDropdown() {
    DestroyDropRT();
    if (m_dropWnd && ::IsWindow(m_dropWnd)) ::DestroyWindow(m_dropWnd);
    m_dropWnd = NULL;
    m_dropVisible = false;
    m_hoverIndex = -1;
}

void GpmComboBox::DrawDropList(ID2D1RenderTarget* rt) {
    if (!rt) return;
    RECT rc; ::GetClientRect(m_dropWnd, &rc);
    float w = (float)rc.right, h = (float)rc.bottom;
    if (w <= 0 || h <= 0) return;

    auto& t = Theme();
    float borderRadius = ExDPI::ScaleF(6.0f);
    rt->Clear(ColorRefToD2D(m_dropBg));

    // 圆角边框
    ID2D1SolidColorBrush* borderBr = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(t.border), &borderBr);
    if (borderBr) {
        D2D1_ROUNDED_RECT rr = MakeRoundRect(0.5f, 0.5f, w - 1.0f, h - 1.0f, borderRadius);
        rt->DrawRoundedRectangle(&rr, borderBr, 1.0f);
        borderBr->Release();
    }

    float itemH = (float)m_itemHeight;
    for (int i = 0; i < (int)m_items.size(); i++) {
        float iy = 1.0f + i * itemH;
        COLORREF itemBg = m_dropBg;
        if (i == m_hoverIndex) itemBg = m_dropHover;

        ID2D1SolidColorBrush* itemBgBr = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(itemBg), &itemBgBr);
        if (itemBgBr) {
            rt->FillRectangle(D2D1::RectF(1, iy, w - 1, iy + itemH), itemBgBr);
            itemBgBr->Release();
        }

        if (i == m_selIndex) {
            ID2D1SolidColorBrush* selMark = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(t.fgAccent), &selMark);
            if (selMark) {
                float markW = ExDPI::ScaleF(3.0f);
                rt->FillRectangle(D2D1::RectF(1, iy + 2, 1 + markW, iy + itemH - 2), selMark);
                selMark->Release();
            }
        }

        IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, false,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (fmt) {
            ID2D1SolidColorBrush* textBr = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(m_dropText), &textBr);
            if (textBr) {
                float pad = ExDPI::ScaleF(12.0f);
                D2D1_RECT_F textRc = D2D1::RectF(1 + pad, iy, w - 1, iy + itemH);
                rt->DrawText(m_items[i].c_str(), (UINT32)m_items[i].length(), fmt, textRc, textBr);
                textBr->Release();
            }
            fmt->Release();
        }
    }
}

LRESULT CALLBACK GpmComboBox::DropWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    GpmComboBox* pThis = nullptr;
    if (msg == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        pThis = (GpmComboBox*)cs->lpCreateParams;
        ::SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
    } else {
        pThis = (GpmComboBox*)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
    }
    if (!pThis) return ::DefWindowProc(hWnd, msg, wParam, lParam);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        ::BeginPaint(hWnd, &ps);
        if (!pThis->m_dropRT) pThis->CreateDropRT();

        if (pThis->m_dropRT) {
            RECT rc; ::GetClientRect(hWnd, &rc);
            D2D1_SIZE_U curSize = pThis->m_dropRT->GetPixelSize();
            if ((int)curSize.width != rc.right || (int)curSize.height != rc.bottom) {
                pThis->m_dropRT->Resize(D2D1::SizeU(rc.right, rc.bottom));
            }
            pThis->m_dropRT->BeginDraw();
            pThis->m_dropRT->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            pThis->DrawDropList(pThis->m_dropRT.Get());
            HRESULT hr = pThis->m_dropRT->EndDraw();
            if (hr == D2DERR_RECREATE_TARGET) pThis->DestroyDropRT();
        } else {
            RECT rc; ::GetClientRect(hWnd, &rc);
            HDC hdc = ps.hdc;
            HBRUSH bkBrush = ::CreateSolidBrush(pThis->m_dropBg);
            ::FillRect(hdc, &rc, bkBrush);
            ::DeleteObject(bkBrush);
        }
        ::EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int my = (short)HIWORD(lParam);
        int idx = (my - 1) / pThis->m_itemHeight;
        if (idx != pThis->m_hoverIndex) {
            pThis->m_hoverIndex = idx;
            ::InvalidateRect(hWnd, NULL, FALSE);
        }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
        ::TrackMouseEvent(&tme);
        return 0;
    }
    case WM_LBUTTONUP: {
        int my = (short)HIWORD(lParam);
        int idx = (my - 1) / pThis->m_itemHeight;
        if (idx >= 0 && idx < (int)pThis->m_items.size()) {
            pThis->m_selIndex = idx;
            pThis->Invalidate();
            if (pThis->m_selectCb) pThis->m_selectCb(pThis, pThis->m_id, idx, pThis->m_items[idx]);
        }
        pThis->HideDropdown();
        return 0;
    }
    case WM_MOUSELEAVE:
        pThis->m_hoverIndex = -1;
        ::InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            HWND hActive = (HWND)lParam;
            if (hActive != pThis->m_dropWnd) pThis->HideDropdown();
        }
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}

} // namespace gpm_ui

#endif // GPMUI_ENABLE_COMBOBOX
