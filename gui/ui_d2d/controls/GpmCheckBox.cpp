/*
 * GpmCheckBox.cpp - 选择框控件 (D2D渲染)
 * ImGui风格：方框+勾选标记，悬停高亮，按下凹陷
 */
#include "../core/gpm_ui.h"

#ifdef GPMUI_ENABLE_CHECKBOX

namespace gpm_ui {

GpmCheckBox::GpmCheckBox() { ApplyTheme(); }
GpmCheckBox::~GpmCheckBox() {}

void GpmCheckBox::ApplyTheme() {
    m_style.ApplyTheme_CheckBox();
    m_cornerRadius = m_style.cornerRadius;
}

void GpmCheckBox::Create(GpmWindow* parent, int x, int y, int w, int h,
                        const std::wstring& text, int id) {
    m_parentWnd = parent;
    m_hWnd = parent ? parent->GetWindowHandle() : NULL;
    m_x = ExDPI::Scale(x); m_y = ExDPI::Scale(y);
    m_width = ExDPI::Scale(w); m_height = ExDPI::Scale(h);
    m_text = text; m_id = id;
    if (parent) parent->AddControl(this);
}

void GpmCheckBox::SetChecked(bool c, bool redraw) {
    m_checked = c;
    if (redraw) Invalidate();
}

void GpmCheckBox::OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) {
    if (!m_visible || !rt) return;
    auto& t = Theme();
    float boxSize = ExDPI::ScaleF(16.0f);
    float pad = ExDPI::ScaleF(4.0f);
    float bx = rc.left + pad;
    float by = (rc.top + rc.bottom - boxSize) / 2.0f;

    COLORREF boxBg = m_checked ? t.checkMark : t.bgInput;
    if (m_state == STATE_HOVER && !m_checked) boxBg = t.bgHover;
    if (m_state == STATE_DOWN) boxBg = m_checked ? RGB(
        (std::max)(0, (int)GetRValue(t.checkMark) - 20),
        (std::max)(0, (int)GetGValue(t.checkMark) - 20),
        (std::max)(0, (int)GetBValue(t.checkMark) - 20)) : t.bgActive;

    ID2D1SolidColorBrush* brush = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(boxBg, m_style.opacity), &brush);
    if (brush) {
        float r = ExDPI::ScaleF(3.0f);
        D2D1_ROUNDED_RECT rr = MakeRoundRect(bx, by, boxSize, boxSize, r);
        rt->FillRoundedRectangle(&rr, brush);
        brush->Release();
    }

    COLORREF borderC = m_checked ? t.checkMark : m_style.borderColor;
    if (m_state == STATE_HOVER && !m_checked) borderC = t.borderHover;

    ID2D1SolidColorBrush* lightBr = nullptr;
    ID2D1SolidColorBrush* darkBr = nullptr;
    COLORREF lightBorder = RGB(
        (std::min)(255, (int)GetRValue(borderC) + 15),
        (std::min)(255, (int)GetGValue(borderC) + 15),
        (std::min)(255, (int)GetBValue(borderC) + 15));
    COLORREF darkBorder = RGB(
        (std::max)(0, (int)GetRValue(borderC) - 15),
        (std::max)(0, (int)GetGValue(borderC) - 15),
        (std::max)(0, (int)GetBValue(borderC) - 15));

    rt->CreateSolidColorBrush(ColorRefToD2D(lightBorder, m_style.opacity), &lightBr);
    rt->CreateSolidColorBrush(ColorRefToD2D(darkBorder, m_style.opacity), &darkBr);
    if (lightBr && darkBr) {
        float r = ExDPI::ScaleF(3.0f);
        D2D1_ROUNDED_RECT rr = MakeRoundRect(bx, by, boxSize, boxSize, r);
        rt->DrawRoundedRectangle(&rr, darkBr, 1.0f);
        lightBr->Release();
        darkBr->Release();
    } else {
        if (lightBr) lightBr->Release();
        if (darkBr) darkBr->Release();
    }

    if (m_checked) {
        ID2D1SolidColorBrush* ck = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, m_style.opacity), &ck);
        if (ck) {
            float cx = bx + boxSize / 2, cy = by + boxSize / 2;
            float s = boxSize * 0.25f;
            float lineW = ExDPI::ScaleF(2.0f);
            rt->DrawLine(D2D1::Point2F(cx - s, cy), D2D1::Point2F(cx - s * 0.2f, cy + s * 0.7f), ck, lineW);
            rt->DrawLine(D2D1::Point2F(cx - s * 0.2f, cy + s * 0.7f), D2D1::Point2F(cx + s, cy - s * 0.6f), ck, lineW);
            ck->Release();
        }
    }

    COLORREF txC = m_enabled ? m_style.textColors.Get(m_state) : t.fgDisabled;
    IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, m_style.bold,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (fmt) {
        ID2D1SolidColorBrush* tb = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(txC, m_style.opacity), &tb);
        if (tb) {
            D2D1_RECT_F textRc = D2D1::RectF(bx + boxSize + pad * 2, rc.top, rc.right, rc.bottom);
            rt->DrawText(m_text.c_str(), (UINT32)m_text.length(), fmt, textRc, tb);
            tb->Release();
        }
        fmt->Release();
    }
}

void GpmCheckBox::OnMouseMove(int x, int y) {
    if (!m_enabled) return;
    if (m_state != STATE_HOVER && m_state != STATE_DOWN) { m_state = STATE_HOVER; Invalidate(); }
}
void GpmCheckBox::OnLButtonDown(int x, int y) { if (m_enabled) { m_state = STATE_DOWN; Invalidate(); } }
void GpmCheckBox::OnLButtonUp(int x, int y) {
    if (!m_enabled) return;
    if (m_state == STATE_DOWN) {
        m_checked = !m_checked; m_state = STATE_HOVER; Invalidate();
        if (m_clickCb) m_clickCb(this, m_id);
    }
}
void GpmCheckBox::OnMouseLeave() {
    if (m_state != STATE_NORMAL) { m_state = STATE_NORMAL; Invalidate(); }
}

} // namespace gpm_ui

#endif // GPMUI_ENABLE_CHECKBOX
