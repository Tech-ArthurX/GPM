/*
 * GpmUIBase.cpp - UIElement 基类实现 (控件树 / 布局引擎 / 脏矩形)
 * ImGui风格绘制样式
 */
#include "gpm_ui.h"

namespace gpm_ui {

// ============================================================
// 基本属性
// ============================================================
void UIElement::SetText(const std::wstring& text, bool redraw) {
    m_text = text;
    if (redraw) Invalidate();
}

void UIElement::SetVisible(bool v, bool redraw) {
    m_visible = v;
    if (redraw) Invalidate();
}

void UIElement::SetEnabled(bool e, bool redraw) {
    m_enabled = e;
    if (!e) m_state = STATE_DISABLE;
    else if (m_state == STATE_DISABLE) m_state = STATE_NORMAL;
    if (redraw) Invalidate();
}

void UIElement::GetRect(RECT& rc) const {
    rc.left = m_x; rc.top = m_y;
    rc.right = m_x + m_width; rc.bottom = m_y + m_height;
}

void UIElement::SetRect(int x, int y, int w, int h, bool redraw) {
    m_x = x; m_y = y; m_width = w; m_height = h;
    if (redraw) Invalidate();
}

void UIElement::SetBkColor(COLORREF normal, COLORREF hover, COLORREF down) {
    m_style.bgColors = StateColors(normal, hover, down);
}

void UIElement::SetTextColor(COLORREF normal, COLORREF hover, COLORREF down) {
    m_style.textColors = StateColors(normal, hover, down);
}

void UIElement::SetOpacity(float opacity, bool redraw) {
    m_style.opacity = (std::max)(0.0f, (std::min)(1.0f, opacity));
    if (redraw) Invalidate();
}

void UIElement::ApplyTheme() {
    auto& t = Theme();
    m_style.bgColors = StateColors(t.bgEditor, t.bgHover, t.bgActive);
    m_style.textColors = StateColors(t.fgPrimary, t.fgPrimary, t.fgPrimary);
    m_style.borderColor = t.border;
}

// ============================================================
// 控件树
// ============================================================
void UIElement::AddChild(std::unique_ptr<UIElement> child) {
    if (child) {
        child->m_parent = this;
        child->m_hWnd = m_hWnd;
        child->m_parentWnd = m_parentWnd;
        m_children.push_back(std::move(child));
    }
}

void UIElement::RemoveChild(UIElement* child) {
    auto it = std::find_if(m_children.begin(), m_children.end(),
        [child](const std::unique_ptr<UIElement>& ptr) { return ptr.get() == child; });
    if (it != m_children.end()) {
        m_children.erase(it);
    }
}

UIElement* UIElement::GetChild(int index) const {
    if (index >= 0 && index < (int)m_children.size())
        return m_children[index].get();
    return nullptr;
}

UIElement* UIElement::FindChildByID(int id) {
    for (auto& child : m_children) {
        if (child->GetID() == id) return child.get();
        auto* found = child->FindChildByID(id);
        if (found) return found;
    }
    return nullptr;
}

UIElement* UIElement::HitTest(int x, int y) {
    if (!m_visible || !m_enabled) return nullptr;
    if (x >= m_x && x < m_x + m_width && y >= m_y && y < m_y + m_height) {
        // Check children in reverse order (top-most first)
        for (int i = (int)m_children.size() - 1; i >= 0; --i) {
            auto* hit = m_children[i]->HitTest(x, y);
            if (hit) return hit;
        }
        if (!m_hitThrough) return this;
    }
    return nullptr;
}

// ============================================================
// 布局引擎
// ============================================================
void UIElement::UpdateLayout(const RECT& parentRect) {
    int parentW = parentRect.right - parentRect.left;
    int parentH = parentRect.bottom - parentRect.top;

    if (m_layout.anchor & ANCHOR_LEFT) {
        // 百分比定位
        if (m_layout.leftPercent > 0) {
            m_x = (int)(parentW * m_layout.leftPercent) + m_layout.leftMargin;
        } else {
            m_x = parentRect.left + m_layout.leftMargin;
        }
    }

    if (m_layout.anchor & ANCHOR_TOP) {
        if (m_layout.topPercent > 0) {
            m_y = (int)(parentH * m_layout.topPercent) + m_layout.topMargin;
        } else {
            m_y = parentRect.top + m_layout.topMargin;
        }
    }

    if (m_layout.anchor & ANCHOR_RIGHT) {
        if (m_layout.widthPercent > 0) {
            m_width = (int)(parentW * m_layout.widthPercent) - m_layout.rightMargin - m_layout.leftMargin;
        } else {
            m_width = parentW - m_x - m_layout.rightMargin;
        }
    }

    if (m_layout.anchor & ANCHOR_BOTTOM) {
        if (m_layout.heightPercent > 0) {
            m_height = (int)(parentH * m_layout.heightPercent) - m_layout.bottomMargin - m_layout.topMargin;
        } else {
            m_height = parentH - m_y - m_layout.bottomMargin;
        }
    }

    if (m_layout.anchor & ANCHOR_CENTER_X) {
        m_x = (parentW - m_width) / 2 + m_layout.leftMargin;
    }

    if (m_layout.anchor & ANCHOR_CENTER_Y) {
        m_y = (parentH - m_height) / 2 + m_layout.topMargin;
    }

    // 更新子控件
    RECT childRect = { 0, 0, m_width, m_height };
    for (auto& child : m_children) {
        child->UpdateLayout(childRect);
    }
}

// ============================================================
// 脏矩形
// ============================================================
void UIElement::Invalidate() {
    m_dirty = true;
    // 向上传播到父窗口
    if (m_parentWnd && m_hWnd) {
        RECT rc = { m_x, m_y, m_x + m_width, m_y + m_height };
        ::InvalidateRect(m_hWnd, &rc, FALSE);
    }
}

void UIElement::InvalidateRect(const D2D1_RECT_F& rect) {
    m_dirty = true;
    m_dirtyRect = rect;
    if (m_parentWnd && m_hWnd) {
        RECT rc = { (int)rect.left, (int)rect.top, (int)rect.right, (int)rect.bottom };
        ::InvalidateRect(m_hWnd, &rc, FALSE);
    }
}

// ============================================================
// 消息路由统一接口
// ============================================================
bool UIElement::OnMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
    // 默认实现：将消息路由到对应的虚拟函数
    switch (msg) {
    case WM_MOUSEMOVE:
        OnMouseMove((short)LOWORD(lParam), (short)HIWORD(lParam));
        return true;
    case WM_LBUTTONDOWN:
        OnLButtonDown((short)LOWORD(lParam), (short)HIWORD(lParam));
        return true;
    case WM_LBUTTONUP:
        OnLButtonUp((short)LOWORD(lParam), (short)HIWORD(lParam));
        return true;
    case WM_LBUTTONDBLCLK:
        OnLButtonDblClk((short)LOWORD(lParam), (short)HIWORD(lParam));
        return true;
    case WM_MOUSEWHEEL:
        OnMouseWheel((short)LOWORD(lParam), (short)HIWORD(lParam), (short)HIWORD(wParam));
        return true;
    case WM_KEYDOWN:
        OnKeyDown((UINT)wParam);
        return true;
    case WM_KEYUP:
        OnKeyUp((UINT)wParam);
        return true;
    case WM_CHAR:
        OnChar((wchar_t)wParam);
        return true;
    case WM_SETFOCUS:
        OnSetFocus();
        return true;
    case WM_KILLFOCUS:
        OnKillFocus();
        return true;
    }
    return false;
}

// ============================================================
// 绘制辅助
// ============================================================
void UIElement::DrawBackgroundD2D(ID2D1RenderTarget* rt, float x, float y, float w, float h,
                                  COLORREF bkColor, float opacity) {
    if (!rt) return;
    float alpha = (opacity < 0) ? m_style.opacity : opacity;

    ID2D1SolidColorBrush* brush = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(bkColor, alpha), &brush);
    if (!brush) return;

    if (m_style.cornerRadius > 0) {
        D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, w, h, (float)m_style.cornerRadius);
        rt->FillRoundedRectangle(&rr, brush);
    } else {
        rt->FillRectangle(D2D1::RectF(x, y, x + w, y + h), brush);
    }
    brush->Release();

    // ImGui风格3D边框
    if (m_style.borderColor != 0) {
        COLORREF darkBorder = RGB(
            (std::max)(0, (int)GetRValue(m_style.borderColor) - 20),
            (std::max)(0, (int)GetGValue(m_style.borderColor) - 20),
            (std::max)(0, (int)GetBValue(m_style.borderColor) - 20));
        COLORREF lightBorder = RGB(
            (std::min)(255, (int)GetRValue(m_style.borderColor) + 15),
            (std::min)(255, (int)GetGValue(m_style.borderColor) + 15),
            (std::min)(255, (int)GetBValue(m_style.borderColor) + 15));

        ID2D1SolidColorBrush* borderBrush = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(m_style.borderColor, alpha), &borderBrush);
        if (borderBrush) {
            if (m_style.cornerRadius > 0) {
                D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, w, h, (float)m_style.cornerRadius);
                rt->DrawRoundedRectangle(&rr, borderBrush, 1.0f);
            } else {
                ID2D1SolidColorBrush* lightBrush = nullptr;
                rt->CreateSolidColorBrush(ColorRefToD2D(lightBorder, alpha), &lightBrush);
                ID2D1SolidColorBrush* darkBrush = nullptr;
                rt->CreateSolidColorBrush(ColorRefToD2D(darkBorder, alpha), &darkBrush);
                if (lightBrush && darkBrush) {
                    rt->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(x + w, y), lightBrush, 1.0f);
                    rt->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(x, y + h), lightBrush, 1.0f);
                    rt->DrawLine(D2D1::Point2F(x, y + h), D2D1::Point2F(x + w, y + h), darkBrush, 1.0f);
                    rt->DrawLine(D2D1::Point2F(x + w, y), D2D1::Point2F(x + w, y + h), darkBrush, 1.0f);
                    lightBrush->Release();
                    darkBrush->Release();
                } else {
                    if (lightBrush) lightBrush->Release();
                    if (darkBrush) darkBrush->Release();
                }
            }
            borderBrush->Release();
        }
    }
}

void UIElement::DrawTextD2D(ID2D1RenderTarget* rt, const std::wstring& text, float x, float y,
                            float w, float h, COLORREF color, float fontSize,
                            bool bold,
                            DWRITE_TEXT_ALIGNMENT hAlign,
                            DWRITE_PARAGRAPH_ALIGNMENT vAlign) {
    if (!rt || text.empty()) return;

    IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(fontSize, bold, hAlign, vAlign);
    if (fmt) {
        ID2D1SolidColorBrush* tb = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(color, m_style.opacity), &tb);
        if (tb) {
            D2D1_RECT_F textRc = D2D1::RectF(x, y, x + w, y + h);
            rt->DrawText(text.c_str(), (UINT32)text.length(), fmt, textRc, tb);
            tb->Release();
        }
        fmt->Release();
    }
}

} // namespace gpm_ui