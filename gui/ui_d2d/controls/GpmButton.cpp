/*
 * GpmButton.cpp - 按钮控件 (D2D渲染)
 * ImGui风格：按下时颜色反转，悬停时高亮，3D边框效果
 */
#include "../core/gpm_ui.h"

#ifdef GPMUI_ENABLE_BUTTON

namespace gpm_ui {

GpmButton::GpmButton() { ApplyTheme(); }
GpmButton::~GpmButton() {}

void GpmButton::ApplyTheme() {
    m_style.ApplyTheme_Button();
    m_cornerRadius = m_style.cornerRadius;
}

void GpmButton::Create(GpmWindow* parent, int x, int y, int w, int h,
                      const std::wstring& text, int id) {
    m_parentWnd = parent;
    m_hWnd = parent ? parent->GetWindowHandle() : NULL;
    m_x = ExDPI::Scale(x); m_y = ExDPI::Scale(y);
    m_width = ExDPI::Scale(w); m_height = ExDPI::Scale(h);
    m_text = text; m_id = id; m_state = STATE_NORMAL;
    if (parent) parent->AddControl(this);
}

void GpmButton::OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) {
    if (!m_visible || !rt) return;

    COLORREF bkC = m_state == STATE_DISABLE ? Theme().bgDisabled : m_style.bgColors.Get(m_state);
    COLORREF txC = m_state == STATE_DISABLE ? Theme().fgDisabled : m_style.textColors.Get(m_state);

    float x = rc.left, y = rc.top;
    float w = rc.right - rc.left, h = rc.bottom - rc.top;

    // 背景
    ID2D1SolidColorBrush* brush = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(bkC, m_style.opacity), &brush);
    if (brush) {
        if (m_cornerRadius > 0) {
            D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, w, h, (float)m_cornerRadius);
            rt->FillRoundedRectangle(&rr, brush);
        } else {
            rt->FillRectangle(D2D1::RectF(x, y, x + w, y + h), brush);
        }
        brush->Release();
    }

    // ImGui风格3D边框
    if (m_state == STATE_DOWN) {
        COLORREF darkEdge = RGB(
            (std::max)(0, (int)GetRValue(bkC) - 25),
            (std::max)(0, (int)GetGValue(bkC) - 25),
            (std::max)(0, (int)GetBValue(bkC) - 25));
        COLORREF lightEdge = RGB(
            (std::min)(255, (int)GetRValue(bkC) + 20),
            (std::min)(255, (int)GetGValue(bkC) + 20),
            (std::min)(255, (int)GetBValue(bkC) + 20));

        ID2D1SolidColorBrush* darkBr = nullptr;
        ID2D1SolidColorBrush* lightBr = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(darkEdge, m_style.opacity), &darkBr);
        rt->CreateSolidColorBrush(ColorRefToD2D(lightEdge, m_style.opacity), &lightBr);

        if (darkBr && lightBr) {
            if (m_cornerRadius > 0) {
                D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, w, h, (float)m_cornerRadius);
                rt->DrawRoundedRectangle(&rr, darkBr, 1.0f);
            } else {
                rt->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(x + w, y), darkBr, 1.0f);
                rt->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(x, y + h), darkBr, 1.0f);
                rt->DrawLine(D2D1::Point2F(x, y + h), D2D1::Point2F(x + w, y + h), lightBr, 1.0f);
                rt->DrawLine(D2D1::Point2F(x + w, y), D2D1::Point2F(x + w, y + h), lightBr, 1.0f);
            }
        }
        if (darkBr) darkBr->Release();
        if (lightBr) lightBr->Release();
    } else {
        COLORREF lightEdge = RGB(
            (std::min)(255, (int)GetRValue(bkC) + 20),
            (std::min)(255, (int)GetGValue(bkC) + 20),
            (std::min)(255, (int)GetBValue(bkC) + 20));
        COLORREF darkEdge = RGB(
            (std::max)(0, (int)GetRValue(bkC) - 25),
            (std::max)(0, (int)GetGValue(bkC) - 25),
            (std::max)(0, (int)GetBValue(bkC) - 25));

        ID2D1SolidColorBrush* lightBr = nullptr;
        ID2D1SolidColorBrush* darkBr = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(lightEdge, m_style.opacity), &lightBr);
        rt->CreateSolidColorBrush(ColorRefToD2D(darkEdge, m_style.opacity), &darkBr);

        if (lightBr && darkBr) {
            if (m_cornerRadius > 0) {
                D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, w, h, (float)m_cornerRadius);
                rt->DrawRoundedRectangle(&rr, darkBr, 1.0f);
            } else {
                rt->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(x + w, y), lightBr, 1.0f);
                rt->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(x, y + h), lightBr, 1.0f);
                rt->DrawLine(D2D1::Point2F(x, y + h), D2D1::Point2F(x + w, y + h), darkBr, 1.0f);
                rt->DrawLine(D2D1::Point2F(x + w, y), D2D1::Point2F(x + w, y + h), darkBr, 1.0f);
            }
        }
        if (lightBr) lightBr->Release();
        if (darkBr) darkBr->Release();
    }

    // 文字
    IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, m_style.bold,
        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (fmt) {
        ID2D1SolidColorBrush* textBrush = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(txC, m_style.opacity), &textBrush);
        if (textBrush) {
            D2D1_RECT_F textRc = rc;
            if (m_state == STATE_DOWN) {
                textRc.top += 1.0f;
                textRc.bottom += 1.0f;
            }
            rt->DrawText(m_text.c_str(), (UINT32)m_text.length(), fmt, textRc, textBrush);
            textBrush->Release();
        }
        fmt->Release();
    }
}

// Fix: OnMouseMove now does self hit-test, so we can detect when mouse leaves
// the button's bounds even without WM_MOUSELEAVE firing on us.
void GpmButton::OnMouseMove(int x, int y) {
    if (!m_enabled) return;
    RECT rc; GetRect(rc);
    bool inside = (x >= rc.left && x <= rc.right && y >= rc.top && y <= rc.bottom);
    if (inside) {
        if (m_state != STATE_DOWN && m_state != STATE_HOVER) { m_state = STATE_HOVER; Invalidate(); }
    } else {
        if (m_state == STATE_HOVER || m_state == STATE_DOWN) { m_state = STATE_NORMAL; Invalidate(); }
    }
}

void GpmButton::OnLButtonDown(int x, int y) {
    if (!m_enabled) return;
    m_state = STATE_DOWN; Invalidate();
}

// Fix: OnLButtonUp returns to STATE_NORMAL instead of STATE_HOVER,
// so the button doesn't stay highlighted after clicking.
void GpmButton::OnLButtonUp(int x, int y) {
    if (!m_enabled) return;
    if (m_state == STATE_DOWN) {
        m_state = STATE_NORMAL; Invalidate();
        if (m_clickCb) m_clickCb(this, m_id);
    }
}

void GpmButton::OnMouseLeave() {
    if (!m_enabled) return;
    if (m_state != STATE_NORMAL) { m_state = STATE_NORMAL; Invalidate(); }
}

} // namespace gpm_ui

#endif // GPMUI_ENABLE_BUTTON
