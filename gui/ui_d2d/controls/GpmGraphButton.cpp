/*
 * GpmGraphButton.cpp - ĺžĺ˝˘ćéŽć§äťś (D2Dć¸˛ć)
 * ImGuiéŁć źďźćŻćĺžć ?ćĺ­çťĺĺ¸ĺąďźĺč§ďźä¸ćé˘č?
 * ĺŻčŽžç˝Žĺžć ĺ¨ćĺ­çä¸/ĺˇ?ĺłćš
 */
#include "../core/gpm_ui.h"
#include <cmath>

#ifdef GPMUI_ENABLE_GRAPHBUTTON

namespace gpm_ui {

GpmGraphButton::GpmGraphButton() { ApplyTheme(); }
GpmGraphButton::~GpmGraphButton() {}

void GpmGraphButton::ApplyTheme() {
    m_style.ApplyTheme_Button();
    m_cornerRadius = m_style.cornerRadius;
}

void GpmGraphButton::Create(GpmWindow* parent, int x, int y, int w, int h,
                           const std::wstring& text, int id) {
    m_parentWnd = parent;
    m_hWnd = parent ? parent->GetWindowHandle() : NULL;
    m_x = ExDPI::Scale(x); m_y = ExDPI::Scale(y);
    m_width = ExDPI::Scale(w); m_height = ExDPI::Scale(h);
    m_text = text; m_id = id; m_state = STATE_NORMAL;
    if (parent) parent->AddControl(this);
}

void GpmGraphButton::SetIcon(GraphIconType type, COLORREF color) {
    m_iconType = type;
    m_iconColor = color;
    Invalidate();
}

void GpmGraphButton::OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) {
    if (!m_visible || !rt) return;

    COLORREF bkC = m_state == STATE_DISABLE ? Theme().bgDisabled : m_style.bgColors.Get(m_state);
    COLORREF txC = m_state == STATE_DISABLE ? Theme().fgDisabled : m_style.textColors.Get(m_state);

    float x = rc.left, y = rc.top;
    float w = rc.right - rc.left, h = rc.bottom - rc.top;
    float cr = (float)m_cornerRadius;

    // čćŻ
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

    // 3DčžšćĄ
    COLORREF lightEdge = RGB(
        (std::min)(255, (int)GetRValue(bkC) + 20),
        (std::min)(255, (int)GetGValue(bkC) + 20),
        (std::min)(255, (int)GetBValue(bkC) + 20));
    COLORREF darkEdge = RGB(
        (std::max)(0, (int)GetRValue(bkC) - 25),
        (std::max)(0, (int)GetGValue(bkC) - 25),
        (std::max)(0, (int)GetBValue(bkC) - 25));

    ID2D1SolidColorBrush* borderBr = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(m_state == STATE_DOWN ? darkEdge : lightEdge, m_style.opacity), &borderBr);
    if (borderBr) {
        if (cr > 0) {
            D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, w, h, cr);
            rt->DrawRoundedRectangle(&rr, borderBr, 1.0f);
        }
        borderBr->Release();
    }

    // čŽĄçŽĺžć ĺćĺ­ĺ¸ĺą
    float iconSize = ExDPI::ScaleF(m_iconSize);
    float pad = ExDPI::ScaleF(6.0f);
    float gap = ExDPI::ScaleF(4.0f);
    COLORREF iconC = m_iconColor ? m_iconColor : txC;

    if (m_iconType != ICON_NONE) {
        float iconX, iconY, textX, textY, textW, textH;

        if (m_iconLayout == ICON_TOP) {
            // ĺžć ĺ¨ä¸ďźćĺ­ĺ¨ä¸?
            float totalH = iconSize + gap + ExDPI::ScaleF(12.0f);
            iconX = x + (w - iconSize) / 2;
            iconY = y + (h - totalH) / 2;
            textX = x; textY = iconY + iconSize + gap;
            textW = w; textH = ExDPI::ScaleF(14.0f);
        } else {
            // ĺžć ĺ¨ĺˇŚďźćĺ­ĺ¨ĺ?
            iconX = x + pad;
            iconY = y + (h - iconSize) / 2;
            textX = iconX + iconSize + gap;
            textY = y; textW = w - iconSize - pad * 2 - gap;
            textH = h;
        }

        // çťĺśĺžć 
        DrawIcon(rt, iconX, iconY, iconSize, iconC);

        // çťĺśćĺ­
        if (!m_text.empty()) {
            DWRITE_TEXT_ALIGNMENT hAlign = (m_iconLayout == ICON_TOP) ?
                DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING;
            IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, false,
                hAlign, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            if (fmt) {
                ID2D1SolidColorBrush* tb = nullptr;
                rt->CreateSolidColorBrush(ColorRefToD2D(txC, m_style.opacity), &tb);
                if (tb) {
                    D2D1_RECT_F textRc = D2D1::RectF(textX, textY, textX + textW, textY + textH);
                    rt->DrawText(m_text.c_str(), (UINT32)m_text.length(), fmt, textRc, tb);
                    tb->Release();
                }
                fmt->Release();
            }
        }
    } else {
        // ć ĺžć ďźĺąä¸­ćĺ­
        IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, false,
            DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
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
}

void GpmGraphButton::DrawIcon(ID2D1RenderTarget* rt, float x, float y, float size, COLORREF color) {
    ID2D1SolidColorBrush* br = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(color, m_style.opacity), &br);
    if (!br) return;

    float cx = x + size / 2, cy = y + size / 2;
    float s = size * 0.35f;
    float lineW = ExDPI::ScaleF(1.5f);

    switch (m_iconType) {
    case ICON_PLAY: {
        // ä¸č§ĺ˝˘ć­ćžĺžć ?
        ID2D1PathGeometry* geo = nullptr;
        ExD2DFactory::GetFactory()->CreatePathGeometry(&geo);
        if (geo) {
            ID2D1GeometrySink* sink = nullptr;
            geo->Open(&sink);
            if (sink) {
                sink->BeginFigure(D2D1::Point2F(cx - s * 0.7f, cy - s), D2D1_FIGURE_BEGIN_FILLED);
                sink->AddLine(D2D1::Point2F(cx + s, cy));
                sink->AddLine(D2D1::Point2F(cx - s * 0.7f, cy + s));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close(); sink->Release();
                rt->FillGeometry(geo, br);
            }
            geo->Release();
        }
        break;
    }
    case ICON_STOP: {
        // ćšĺ˝˘ĺć­˘ĺžć 
        rt->FillRectangle(D2D1::RectF(cx - s * 0.7f, cy - s * 0.7f, cx + s * 0.7f, cy + s * 0.7f), br);
        break;
    }
    case ICON_PLUS: {
        // ĺ ĺˇ
        rt->DrawLine(D2D1::Point2F(cx - s, cy), D2D1::Point2F(cx + s, cy), br, lineW);
        rt->DrawLine(D2D1::Point2F(cx, cy - s), D2D1::Point2F(cx, cy + s), br, lineW);
        break;
    }
    case ICON_MINUS: {
        // ĺĺˇ
        rt->DrawLine(D2D1::Point2F(cx - s, cy), D2D1::Point2F(cx + s, cy), br, lineW);
        break;
    }
    case ICON_CHECK: {
        // ĺžé?
        rt->DrawLine(D2D1::Point2F(cx - s, cy), D2D1::Point2F(cx - s * 0.2f, cy + s * 0.7f), br, lineW);
        rt->DrawLine(D2D1::Point2F(cx - s * 0.2f, cy + s * 0.7f), D2D1::Point2F(cx + s, cy - s * 0.6f), br, lineW);
        break;
    }
    case ICON_CROSS: {
        // Xĺ?
        rt->DrawLine(D2D1::Point2F(cx - s, cy - s), D2D1::Point2F(cx + s, cy + s), br, lineW);
        rt->DrawLine(D2D1::Point2F(cx + s, cy - s), D2D1::Point2F(cx - s, cy + s), br, lineW);
        break;
    }
    case ICON_ARROW_UP: {
        rt->DrawLine(D2D1::Point2F(cx, cy - s), D2D1::Point2F(cx - s, cy + s * 0.3f), br, lineW);
        rt->DrawLine(D2D1::Point2F(cx, cy - s), D2D1::Point2F(cx + s, cy + s * 0.3f), br, lineW);
        rt->DrawLine(D2D1::Point2F(cx, cy - s), D2D1::Point2F(cx, cy + s), br, lineW);
        break;
    }
    case ICON_ARROW_DOWN: {
        rt->DrawLine(D2D1::Point2F(cx, cy + s), D2D1::Point2F(cx - s, cy - s * 0.3f), br, lineW);
        rt->DrawLine(D2D1::Point2F(cx, cy + s), D2D1::Point2F(cx + s, cy - s * 0.3f), br, lineW);
        rt->DrawLine(D2D1::Point2F(cx, cy + s), D2D1::Point2F(cx, cy - s), br, lineW);
        break;
    }
    case ICON_GEAR: {
        // é˝żč˝Ž (çŽĺä¸şĺ?ç?
        rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), s * 0.6f, s * 0.6f), br, lineW);
        for (int i = 0; i < 6; i++) {
            float angle = i * 3.14159f / 3.0f;
            float px = cx + s * cosf(angle);
            float py = cy + s * sinf(angle);
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px, py), lineW, lineW), br);
        }
        break;
    }
    case ICON_SEARCH: {
        // ćžĺ¤§é?
        float r = s * 0.6f;
        rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx - s * 0.15f, cy - s * 0.15f), r, r), br, lineW);
        rt->DrawLine(D2D1::Point2F(cx + r * 0.5f, cy + r * 0.5f),
                     D2D1::Point2F(cx + s, cy + s), br, lineW * 1.2f);
        break;
    }
    default: break;
    }
    br->Release();
}

void GpmGraphButton::OnMouseMove(int x, int y) {
    if (!m_enabled) return;
    if (m_state != STATE_DOWN && m_state != STATE_HOVER) { m_state = STATE_HOVER; Invalidate(); }
}

void GpmGraphButton::OnLButtonDown(int x, int y) {
    if (m_enabled) { m_state = STATE_DOWN; Invalidate(); }
}

void GpmGraphButton::OnLButtonUp(int x, int y) {
    if (!m_enabled) return;
    if (m_state == STATE_DOWN) {
        m_state = STATE_HOVER; Invalidate();
        if (m_clickCb) m_clickCb(this, m_id);
    }
}

void GpmGraphButton::OnMouseLeave() {
    if (m_state != STATE_NORMAL) { m_state = STATE_NORMAL; Invalidate(); }
}

} // namespace gpm_ui

#endif // GPMUI_ENABLE_GRAPHBUTTON
