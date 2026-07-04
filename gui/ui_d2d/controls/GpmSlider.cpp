/*
 * GpmSlider.cpp - 滑块控件 (D2D渲染)
 * ImGui风格：轨道+填充+圆形滑块，悬停高亮
 */
#include "../core/gpm_ui.h"

#ifdef GPMUI_ENABLE_SLIDER

namespace gpm_ui {

GpmSlider::GpmSlider() { ApplyTheme(); }
GpmSlider::~GpmSlider() {}

void GpmSlider::ApplyTheme() {
    auto& t = Theme();
    m_trackColor = t.sliderTrack;
    m_fillColor = t.sliderFill;
    m_thumbColor = t.sliderThumb;
    m_style.borderColor = 0;
    m_style.cornerRadius = 0;
}

void GpmSlider::Create(GpmWindow* parent, int x, int y, int w, int h, int minVal, int maxVal, int id) {
    m_parentWnd = parent;
    m_hWnd = parent ? parent->GetWindowHandle() : NULL;
    m_x = ExDPI::Scale(x); m_y = ExDPI::Scale(y);
    m_width = ExDPI::Scale(w); m_height = ExDPI::Scale(h);
    m_minVal = minVal; m_maxVal = maxVal;
    m_value = (minVal + maxVal) / 2;
    m_id = id;
    m_thumbSize = ExDPI::Scale(14);
    m_trackHeight = ExDPI::Scale(4);
    if (parent) parent->AddControl(this);
}

void GpmSlider::SetValue(int v, bool redraw) {
    m_value = (std::max)(m_minVal, (std::min)(m_maxVal, v));
    if (redraw) Invalidate();
}

void GpmSlider::SetRange(int minV, int maxV) {
    m_minVal = minV; m_maxVal = maxV;
    m_value = (std::max)(m_minVal, (std::min)(m_maxVal, m_value));
    Invalidate();
}

int GpmSlider::ValueFromX(int mx, D2D1_RECT_F rc) {
    float trackLeft = rc.left + m_thumbSize / 2.0f;
    float trackRight = rc.right - m_thumbSize / 2.0f;
    float ratio = ((float)mx - trackLeft) / (trackRight - trackLeft);
    ratio = (std::max)(0.0f, (std::min)(1.0f, ratio));
    return m_minVal + (int)(ratio * (m_maxVal - m_minVal) + 0.5f);
}

void GpmSlider::OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) {
    if (!m_visible || !rt) return;

    float cx = rc.left, cy = rc.top, cw = rc.right - rc.left, ch = rc.bottom - rc.top;
    float trackY = cy + (ch - m_trackHeight) / 2.0f;
    float trackLeft = cx + m_thumbSize / 2.0f;
    float trackRight = cx + cw - m_thumbSize / 2.0f;
    float range = (float)(m_maxVal - m_minVal);
    float ratio = range > 0 ? (float)(m_value - m_minVal) / range : 0;
    float thumbX = trackLeft + ratio * (trackRight - trackLeft);

    // 轨道
    ID2D1SolidColorBrush* trackBr = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(m_trackColor, m_style.opacity), &trackBr);
    if (trackBr) {
        D2D1_ROUNDED_RECT rr = MakeRoundRect(trackLeft, trackY, trackRight - trackLeft, (float)m_trackHeight, m_trackHeight / 2.0f);
        rt->FillRoundedRectangle(&rr, trackBr);
        trackBr->Release();
    }

    ID2D1SolidColorBrush* trackBorder = nullptr;
    COLORREF trackDark = RGB(
        (std::max)(0, (int)GetRValue(m_trackColor) - 15),
        (std::max)(0, (int)GetGValue(m_trackColor) - 15),
        (std::max)(0, (int)GetBValue(m_trackColor) - 15));
    rt->CreateSolidColorBrush(ColorRefToD2D(trackDark, m_style.opacity), &trackBorder);
    if (trackBorder) {
        D2D1_ROUNDED_RECT rr = MakeRoundRect(trackLeft, trackY, trackRight - trackLeft, (float)m_trackHeight, m_trackHeight / 2.0f);
        rt->DrawRoundedRectangle(&rr, trackBorder, 0.5f);
        trackBorder->Release();
    }

    // 填充
    ID2D1SolidColorBrush* fillBr = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(m_fillColor, m_style.opacity), &fillBr);
    if (fillBr) {
        float fillW = thumbX - trackLeft;
        if (fillW > 0) {
            D2D1_ROUNDED_RECT rr = MakeRoundRect(trackLeft, trackY, fillW, (float)m_trackHeight, m_trackHeight / 2.0f);
            rt->FillRoundedRectangle(&rr, fillBr);
        }
        fillBr->Release();
    }

    // 滑块高光
    if (m_state == STATE_HOVER || m_state == STATE_DOWN) {
        ID2D1SolidColorBrush* glowBr = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(m_fillColor, m_style.opacity * 0.3f), &glowBr);
        if (glowBr) {
            float thumbR = m_thumbSize / 2.0f;
            float thumbCY = cy + ch / 2.0f;
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, thumbCY), thumbR + 3.0f, thumbR + 3.0f), glowBr);
            glowBr->Release();
        }
    }

    // 滑块
    float thumbR = m_thumbSize / 2.0f;
    float thumbCY = cy + ch / 2.0f;
    ID2D1SolidColorBrush* thumbBr = nullptr;
    COLORREF thumbC = m_thumbColor;
    if (m_state == STATE_HOVER) {
        thumbC = RGB(
            (std::min)(255, (int)GetRValue(thumbC) + 20),
            (std::min)(255, (int)GetGValue(thumbC) + 20),
            (std::min)(255, (int)GetBValue(thumbC) + 20));
    } else if (m_state == STATE_DOWN) {
        thumbC = m_fillColor;
    }
    rt->CreateSolidColorBrush(ColorRefToD2D(thumbC, m_style.opacity), &thumbBr);
    if (thumbBr) {
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, thumbCY), thumbR, thumbR), thumbBr);
        thumbBr->Release();
    }

    ID2D1SolidColorBrush* thumbBorder = nullptr;
    COLORREF thumbBorderC = RGB(
        (std::max)(0, (int)GetRValue(thumbC) - 30),
        (std::max)(0, (int)GetGValue(thumbC) - 30),
        (std::max)(0, (int)GetBValue(thumbC) - 30));
    rt->CreateSolidColorBrush(ColorRefToD2D(thumbBorderC, m_style.opacity), &thumbBorder);
    if (thumbBorder) {
        rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, thumbCY), thumbR, thumbR), thumbBorder, 1.0f);
        thumbBorder->Release();
    }
}

void GpmSlider::OnMouseMove(int x, int y) {
    if (!m_enabled) return;
    if (m_draggingThumb) {
        // 使用 GetCursorPos + ScreenToClient 获取真实鼠标位置（支持窗口外跟踪）
        POINT pt;
        ::GetCursorPos(&pt);
        if (m_hWnd) ::ScreenToClient(m_hWnd, &pt);
        RECT rc; GetRect(rc);
        D2D1_RECT_F d2dRc = D2D1::RectF((float)rc.left, (float)rc.top, (float)rc.right, (float)rc.bottom);
        int newVal = ValueFromX(pt.x, d2dRc);
        if (newVal != m_value) {
            m_value = newVal; Invalidate();
            if (m_valueCb) m_valueCb(this, m_id, m_value);
        }
    } else {
        if (m_state != STATE_HOVER) { m_state = STATE_HOVER; Invalidate(); }
    }
}

void GpmSlider::OnLButtonDown(int x, int y) {
    if (!m_enabled) return;
    m_draggingThumb = true; m_state = STATE_DOWN;
    // 捕获鼠标，确保移出窗口后仍能收到 LButtonUp，并跟踪窗口外鼠标位置
    if (m_hWnd) ::SetCapture(m_hWnd);
    RECT rc; GetRect(rc);
    D2D1_RECT_F d2dRc = D2D1::RectF((float)rc.left, (float)rc.top, (float)rc.right, (float)rc.bottom);
    // 用光标真实位置初始化
    POINT pt;
    ::GetCursorPos(&pt);
    if (m_hWnd) ::ScreenToClient(m_hWnd, &pt);
    int newVal = ValueFromX(pt.x, d2dRc);
    if (newVal != m_value) {
        m_value = newVal;
        if (m_valueCb) m_valueCb(this, m_id, m_value);
    }
    Invalidate();
}

void GpmSlider::OnLButtonUp(int x, int y) {
    // 释放鼠标捕获
    if (m_hWnd) ::ReleaseCapture();
    m_draggingThumb = false; m_state = STATE_NORMAL; Invalidate();
}

void GpmSlider::OnMouseLeave() {
    // 不重置拖拽状态！SetCapture + GetCursorPos 保证可跟踪到窗口外
    if (!m_draggingThumb && m_state != STATE_NORMAL) { m_state = STATE_NORMAL; Invalidate(); }
}

} // namespace gpm_ui

#endif // GPMUI_ENABLE_SLIDER
