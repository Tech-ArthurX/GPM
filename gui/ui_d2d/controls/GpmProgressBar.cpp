/*
 * GpmProgressBar.cpp - čżĺşŚćĄć§äť?(D2Dć¸˛ć)
 * ImGuiéŁć źďźĺč§č˝¨é?ĺĄŤĺďźĺ¸Ś3DčžšćĄćć
 */
#include "../core/gpm_ui.h"
#include <cstdio>

#ifdef GPMUI_ENABLE_PROGRESSBAR

namespace gpm_ui {

GpmProgressBar::GpmProgressBar() 
    : m_minVal(0), m_maxVal(100), m_value(0), m_showText(false)
{ ApplyTheme(); }
GpmProgressBar::~GpmProgressBar() {}

void GpmProgressBar::ApplyTheme() {
    auto& t = Theme();
    m_progressBg = t.progressBg;
    m_progressFill = t.progressFill;
    m_progressText = t.progressText;
    m_style.borderColor = 0;
    m_style.cornerRadius = ExDPI::Scale(4);
}

void GpmProgressBar::Create(GpmWindow* parent, int x, int y, int w, int h, int id) {
    m_parentWnd = parent;
    m_hWnd = parent ? parent->GetWindowHandle() : NULL;
    m_x = ExDPI::Scale(x); m_y = ExDPI::Scale(y);
    m_width = ExDPI::Scale(w); m_height = ExDPI::Scale(h);
    m_id = id;
    if (parent) parent->AddControl(this);
}

void GpmProgressBar::SetValue(int v, bool redraw) {
    m_value = (std::max)(m_minVal, (std::min)(m_maxVal, v));
    if (redraw) Invalidate();
}

void GpmProgressBar::SetRange(int minV, int maxV) {
    m_minVal = minV; m_maxVal = maxV;
    m_value = (std::max)(m_minVal, (std::min)(m_maxVal, m_value));
    Invalidate();
}

void GpmProgressBar::SetProgressColors(COLORREF bg, COLORREF fill, COLORREF text) {
    m_progressBg = bg; m_progressFill = fill; m_progressText = text;
}

void GpmProgressBar::OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) {
    if (!m_visible || !rt) return;

    float x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
    float r = (float)m_style.cornerRadius;
    float range = (float)(m_maxVal - m_minVal);
    float ratio = range > 0 ? (float)(m_value - m_minVal) / range : 0;

    // ImGuiéŁć źčćŻďźĺ¸Śĺšéˇććďź?
    ID2D1SolidColorBrush* bgBr = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(m_progressBg, m_style.opacity), &bgBr);
    if (bgBr) {
        D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, w, h, r);
        rt->FillRoundedRectangle(&rr, bgBr);
        bgBr->Release();
    }

    // čćŻĺšéˇčžšćĄ
    ID2D1SolidColorBrush* bgBorder = nullptr;
    COLORREF bgDark = RGB(
        (std::max)(0, (int)GetRValue(m_progressBg) - 15),
        (std::max)(0, (int)GetGValue(m_progressBg) - 15),
        (std::max)(0, (int)GetBValue(m_progressBg) - 15));
    rt->CreateSolidColorBrush(ColorRefToD2D(bgDark, m_style.opacity), &bgBorder);
    if (bgBorder) {
        D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, w, h, r);
        rt->DrawRoundedRectangle(&rr, bgBorder, 0.5f);
        bgBorder->Release();
    }

    // ImGuiéŁć źĺĄŤĺďźĺ¸Ść¸ĺćć - ä˝żç¨ä¸¤ĺąĺĄŤĺć¨Ąćďź?
    float fillW = w * ratio;
    if (fillW > 0) {
        // ä¸ťĺĄŤĺč˛
        ID2D1SolidColorBrush* fillBr = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(m_progressFill, m_style.opacity), &fillBr);
        if (fillBr) {
            if (fillW < w) {
                D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, w, h, r);
                ID2D1RoundedRectangleGeometry* geo = nullptr;
                ExD2DFactory::GetFactory()->CreateRoundedRectangleGeometry(rr, &geo);
                if (geo) {
                    rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), geo), nullptr);
                    rt->FillRectangle(D2D1::RectF(x, y, x + fillW, y + h), fillBr);
                    rt->PopLayer();
                    geo->Release();
                }
            } else {
                D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, fillW, h, r);
                rt->FillRoundedRectangle(&rr, fillBr);
            }
            fillBr->Release();
        }

        // ĺĄŤĺéĄśé¨éŤĺçşżďźImGuiéŁć źĺćł˝ććďź?
        ID2D1SolidColorBrush* highlightBr = nullptr;
        COLORREF highlight = RGB(
            (std::min)(255, (int)GetRValue(m_progressFill) + 40),
            (std::min)(255, (int)GetGValue(m_progressFill) + 40),
            (std::min)(255, (int)GetBValue(m_progressFill) + 40));
        rt->CreateSolidColorBrush(ColorRefToD2D(highlight, m_style.opacity * 0.4f), &highlightBr);
        if (highlightBr && fillW > 4) {
            D2D1_RECT_F highlightRc = D2D1::RectF(x + 1, y + 1, x + fillW - 1, y + h * 0.45f);
            if (fillW < w) {
                D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, w, h, r);
                ID2D1RoundedRectangleGeometry* geo = nullptr;
                ExD2DFactory::GetFactory()->CreateRoundedRectangleGeometry(rr, &geo);
                if (geo) {
                    rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), geo), nullptr);
                    rt->FillRectangle(highlightRc, highlightBr);
                    rt->PopLayer();
                    geo->Release();
                }
            } else {
                rt->FillRectangle(highlightRc, highlightBr);
            }
            highlightBr->Release();
        }
    }

    // é…ç½®ä½å­å¤§å°
    if (m_showText) {
        int pct = (int)(ratio * 100 + 0.5f);
        wchar_t buf[16];
        swprintf_s(buf, L"%d%%", pct);
        IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, false,
            DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (fmt) {
            ID2D1SolidColorBrush* tb = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(m_progressText, m_style.opacity), &tb);
            if (tb) { rt->DrawText(buf, (UINT32)wcslen(buf), fmt, rc, tb); tb->Release(); }
            fmt->Release();
        }
    }
}

} // namespace gpm_ui

#endif // GPMUI_ENABLE_PROGRESSBAR
