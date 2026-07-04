/*
 * GpmLabel.cpp - 标签控件 (D2D渲染)
 * ImGui风格：简洁文字渲染
 */
#include "../core/gpm_ui.h"

#ifdef GPMUI_ENABLE_LABEL

namespace gpm_ui {

GpmLabel::GpmLabel() { ApplyTheme(); }
GpmLabel::~GpmLabel() {}

void GpmLabel::ApplyTheme() {
    m_style.ApplyTheme_Label();
    m_cornerRadius = 0;
}

void GpmLabel::Create(GpmWindow* parent, int x, int y, int w, int h,
                     const std::wstring& text, int id) {
    m_parentWnd = parent;
    m_hWnd = parent ? parent->GetWindowHandle() : NULL;
    m_x = ExDPI::Scale(x); m_y = ExDPI::Scale(y);
    m_width = ExDPI::Scale(w); m_height = ExDPI::Scale(h);
    m_text = text; m_id = id;
    if (parent) parent->AddControl(this);
}

void GpmLabel::OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) {
    if (!m_visible || !rt) return;

    COLORREF txC = m_style.textColors.Get(m_state);
    DWRITE_TEXT_ALIGNMENT hAlign = DWRITE_TEXT_ALIGNMENT_LEADING;
    if (m_align == LABEL_CENTER) hAlign = DWRITE_TEXT_ALIGNMENT_CENTER;
    else if (m_align == LABEL_RIGHT) hAlign = DWRITE_TEXT_ALIGNMENT_TRAILING;

    IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, m_style.bold,
        hAlign, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (fmt) {
        ID2D1SolidColorBrush* tb = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(txC, m_style.opacity), &tb);
        if (tb) {
            rt->DrawText(m_text.c_str(), (UINT32)m_text.length(), fmt, rc, tb);
            tb->Release();
        }
        fmt->Release();
    }
}

} // namespace gpm_ui

#endif // GPMUI_ENABLE_LABEL
