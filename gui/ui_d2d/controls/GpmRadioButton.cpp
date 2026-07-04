/*
 * GpmRadioButton.cpp - 单选框控件 (D2D渲染)
 * ImGui风格：圆形选择框+选中圆点，悬停高亮，按下凹陷
 * 同组内互斥选择
 */
#include "../core/gpm_ui.h"

#ifdef GPMUI_ENABLE_RADIOBUTTON

namespace gpm_ui {

GpmRadioButton::GpmRadioButton() 
    : m_selected(false), m_groupId(0), m_radioSize(0), m_cornerRadius(0), m_clickCb(nullptr)
{ ApplyTheme(); }
GpmRadioButton::~GpmRadioButton() {}

void GpmRadioButton::ApplyTheme() {
    m_style.ApplyTheme_CheckBox();
    m_cornerRadius = 0;
}

void GpmRadioButton::Create(GpmWindow* parent, int x, int y, int w, int h,
                           const std::wstring& text, int groupId, int id) {
    m_parentWnd = parent;
    m_hWnd = parent ? parent->GetWindowHandle() : NULL;
    m_x = ExDPI::Scale(x); m_y = ExDPI::Scale(y);
    m_width = ExDPI::Scale(w); m_height = ExDPI::Scale(h);
    m_text = text; m_id = id; m_groupId = groupId;
    if (parent) parent->AddControl(this);
}

void GpmRadioButton::SetSelected(bool sel, bool redraw) {
    m_selected = sel;
    if (redraw) Invalidate();
}

void GpmRadioButton::AddToGroup(GpmRadioButton* other) {
    if (!other || other == this) return;
    
    // 直接互相添加到对方的兄弟列表
    // 这一步确保 A↔B 双向连接
    auto addPair = [](GpmRadioButton* a, GpmRadioButton* b) {
        if (std::find(a->m_groupSiblings.begin(), a->m_groupSiblings.end(), b) == a->m_groupSiblings.end())
            a->m_groupSiblings.push_back(b);
        if (std::find(b->m_groupSiblings.begin(), b->m_groupSiblings.end(), a) == b->m_groupSiblings.end())
            b->m_groupSiblings.push_back(a);
    };
    
    // 连接 A ↔ B
    addPair(this, other);
    
    // 再让 A 的所有兄弟也认识 B 的所有兄弟
    std::vector<GpmRadioButton*> aAll = m_groupSiblings;  // 当前 A 的全部兄弟（不含A自身）
    std::vector<GpmRadioButton*> bAll = other->m_groupSiblings;  // 当前 B 的全部兄弟（不含B自身）
    
    for (auto* sibA : aAll) {
        if (sibA) addPair(sibA, other);      // A的兄弟 ↔ B
        for (auto* sibB : bAll) {
            if (sibB && sibA) addPair(sibA, sibB);  // A的兄弟 ↔ B的兄弟
        }
    }
    for (auto* sibB : bAll) {
        if (sibB) addPair(sibB, this);       // B的兄弟 ↔ A
    }
}

void GpmRadioButton::OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) {
    if (!m_visible || !rt) return;
    auto& t = Theme();
    float circleSize = ExDPI::ScaleF(16.0f);
    float pad = ExDPI::ScaleF(4.0f);
    float cx = rc.left + pad + circleSize / 2.0f;
    float cy = (rc.top + rc.bottom) / 2.0f;
    float radius = circleSize / 2.0f;

    // 圆形背景
    COLORREF circleBg = m_selected ? t.checkMark : t.bgInput;
    if (m_state == STATE_HOVER && !m_selected) circleBg = t.bgHover;
    if (m_state == STATE_DOWN) circleBg = m_selected ? RGB(
        (std::max)(0, (int)GetRValue(t.checkMark) - 20),
        (std::max)(0, (int)GetGValue(t.checkMark) - 20),
        (std::max)(0, (int)GetBValue(t.checkMark) - 20)) : t.bgActive;

    ID2D1SolidColorBrush* brush = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(circleBg, m_style.opacity), &brush);
    if (brush) {
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), brush);
        brush->Release();
    }

    // 边框
    COLORREF borderC = m_selected ? t.checkMark : m_style.borderColor;
    if (m_state == STATE_HOVER && !m_selected) borderC = t.borderHover;

    ID2D1SolidColorBrush* borderBr = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(borderC, m_style.opacity), &borderBr);
    if (borderBr) {
        rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), borderBr, 1.0f);
        borderBr->Release();
    }

    // 选中圆点
    if (m_selected) {
        ID2D1SolidColorBrush* dotBr = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, m_style.opacity), &dotBr);
        if (dotBr) {
            float dotR = radius * 0.4f;
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), dotR, dotR), dotBr);
            dotBr->Release();
        }
    }

    // 文字
    COLORREF txC = m_enabled ? m_style.textColors.Get(m_state) : t.fgDisabled;
    IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, m_style.bold,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (fmt) {
        ID2D1SolidColorBrush* tb = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(txC, m_style.opacity), &tb);
        if (tb) {
            float textX = rc.left + pad + circleSize + pad * 2;
            D2D1_RECT_F textRc = D2D1::RectF(textX, rc.top, rc.right, rc.bottom);
            rt->DrawText(m_text.c_str(), (UINT32)m_text.length(), fmt, textRc, tb);
            tb->Release();
        }
        fmt->Release();
    }
}

void GpmRadioButton::OnMouseMove(int x, int y) {
    if (!m_enabled) return;
    if (m_state != STATE_HOVER && m_state != STATE_DOWN) { m_state = STATE_HOVER; Invalidate(); }
}

void GpmRadioButton::OnLButtonDown(int x, int y) {
    if (m_enabled) { m_state = STATE_DOWN; Invalidate(); }
}

void GpmRadioButton::OnLButtonUp(int x, int y) {
    if (!m_enabled) return;
    if (m_state == STATE_DOWN) {
        if (!m_selected) {
            // 取消所有同组已选的兄弟（直接设false，不调用SetSelected避免递归）
            for (auto* s : m_groupSiblings) {
                if (s && s != this && s->m_selected) {
                    s->m_selected = false;
                }
            }
            // 选中自己
            m_selected = true;
            // 强制整个窗口重绘
            if (m_parentWnd) m_parentWnd->Redraw();
            if (m_clickCb) m_clickCb(this, m_id);
        }
        m_state = STATE_HOVER;
        Invalidate();
    }
}

void GpmRadioButton::OnMouseLeave() {
    if (m_state != STATE_NORMAL) { m_state = STATE_NORMAL; Invalidate(); }
}

} // namespace gpm_ui

#endif // GPMUI_ENABLE_RADIOBUTTON
