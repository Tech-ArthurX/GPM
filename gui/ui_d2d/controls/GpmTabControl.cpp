/*
 * GpmTabControl.cpp - 选择夹/Tab容器控件 (D2D渲染)
 * ImGui风格：水平Tab头，选中Tab高亮，3D边框
 */
#include "../core/gpm_ui.h"

#ifdef GPMUI_ENABLE_TABCONTROL

namespace gpm_ui {

GpmTabControl::GpmTabControl() 
    : m_currentIndex(0), m_hoverTab(-1), m_tabHeight(0), m_selectCb(nullptr)
{
    m_style.ApplyTheme_Tab();
    m_tabHeight = ExDPI::Scale(32);
}

GpmTabControl::~GpmTabControl() {}

void GpmTabControl::Create(GpmWindow* parent, int x, int y, int w, int h, int id) {
    m_parentWnd = parent;
    m_hWnd = parent ? parent->GetWindowHandle() : NULL;
    m_x = ExDPI::Scale(x); m_y = ExDPI::Scale(y);
    m_width = ExDPI::Scale(w); m_height = ExDPI::Scale(h);
    m_tabHeight = ExDPI::Scale(32);
    m_id = id;
    if (parent) parent->AddControl(this);
}

int GpmTabControl::AddPage(const std::wstring& title) {
    ExTabPage page;
    page.title = title;
    m_pages.push_back(std::move(page));
    Invalidate();
    return (int)m_pages.size() - 1;
}

void GpmTabControl::RemovePage(int index) {
    if (index >= 0 && index < (int)m_pages.size()) {
        m_pages.erase(m_pages.begin() + index);
        if (m_currentIndex >= (int)m_pages.size())
            m_currentIndex = (int)m_pages.size() - 1;
        if (m_currentIndex < 0) m_currentIndex = 0;
        Invalidate();
    }
}

void GpmTabControl::SetCurrentIndex(int index, bool redraw) {
    if (index >= 0 && index < (int)m_pages.size() && index != m_currentIndex) {
        m_currentIndex = index;
        Invalidate();
    }
}

void GpmTabControl::AddControlToPage(int pageIndex, std::unique_ptr<UIElement> ctrl) {
    if (pageIndex >= 0 && pageIndex < (int)m_pages.size()) {
    if (ctrl) {
        ctrl->SetParent(this);
        ctrl->SetWindowHandle(m_hWnd);
        ctrl->SetParentWindow(m_parentWnd);
        m_pages[pageIndex].controls.push_back(std::move(ctrl));
            Invalidate();
        }
    }
}

void GpmTabControl::ApplyTheme() {
    m_style.ApplyTheme_Tab();
}

int GpmTabControl::HitTestTab(int x, int y) const {
    // 转换到控件局部坐标
    int lx = x - m_x;
    int ly = y - m_y;
    if (ly < 0 || ly > m_tabHeight) return -1;
    if (m_pages.empty()) return -1;
    
    float tabW = (float)m_width / (float)m_pages.size();
    int idx = (int)((float)lx / tabW);
    if (idx >= 0 && idx < (int)m_pages.size()) return idx;
    return -1;
}

void GpmTabControl::OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) {
    if (!m_visible || !rt) return;
    
    float x = rc.left, y = rc.top;
    float w = rc.right - rc.left, h = rc.bottom - rc.top;
    
    // 背景
    auto& t = Theme();
    ID2D1SolidColorBrush* bgBr = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(t.tabBg, m_style.opacity), &bgBr);
    if (bgBr) {
        rt->FillRectangle(D2D1::RectF(x, y, x + w, y + h), bgBr);
        bgBr->Release();
    }

    // 绘制Tab头
    DrawTabHeader(rt, rc);
    
    // 绘制当前Tab内容区域
    DrawTabContent(rt, rc);
    
    // 底部边框 (在Tab头下方)
    float tabBottom = y + m_tabHeight;
    ID2D1SolidColorBrush* borderBr = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(t.tabBorder, m_style.opacity), &borderBr);
    if (borderBr) {
        // 只有非选中的Tab下方才有横线
        // 选中的Tab下方是空白
        rt->DrawLine(D2D1::Point2F(x, tabBottom), D2D1::Point2F(x + w, tabBottom), borderBr, 1.0f);
        borderBr->Release();
    }
}

void GpmTabControl::DrawTabHeader(ID2D1RenderTarget* rt, D2D1_RECT_F rc) {
    if (m_pages.empty()) return;
    auto& t = Theme();
    
    float x = rc.left, y = rc.top;
    float w = rc.right - rc.left;
    float tabW = w / (float)m_pages.size();
    float tabH = (float)m_tabHeight;

    for (int i = 0; i < (int)m_pages.size(); i++) {
        float tx = x + i * tabW;
        float ty = y;
        bool isActive = (i == m_currentIndex);
        bool isHover = (i == m_hoverTab);
        
        // Tab背景
        COLORREF tabBg = isActive ? t.tabActive : (isHover ? t.tabHover : t.tabInactive);
        ID2D1SolidColorBrush* tabBr = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(tabBg, m_style.opacity), &tabBr);
        if (tabBr) {
            if (isActive) {
                // 选中的Tab：绘制到稍高的位置，使得底部边框被覆盖
                D2D1_RECT_F tabRect = D2D1::RectF(tx, ty, tx + tabW, ty + tabH + 1);
                rt->FillRectangle(tabRect, tabBr);
            } else {
                D2D1_RECT_F tabRect = D2D1::RectF(tx, ty, tx + tabW, ty + tabH);
                rt->FillRectangle(tabRect, tabBr);
            }
            tabBr->Release();
        }

        // 左侧边框 (非第一个Tab)
        if (i > 0) {
            ID2D1SolidColorBrush* sepBr = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(t.tabBorder, m_style.opacity * 0.6f), &sepBr);
            if (sepBr) {
                float sepY = ty + ExDPI::ScaleF(6.0f);
                float sepH = tabH - ExDPI::ScaleF(12.0f);
                rt->DrawLine(D2D1::Point2F(tx, ty + sepY), D2D1::Point2F(tx, ty + sepY + sepH), sepBr, 1.0f);
                sepBr->Release();
            }
        }

        // 选中Tab的顶部高亮线 (ImGui风格)
        if (isActive) {
            ID2D1SolidColorBrush* accentBr = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(t.fgAccent, m_style.opacity), &accentBr);
            if (accentBr) {
                float accentH = ExDPI::ScaleF(2.0f);
                rt->FillRectangle(D2D1::RectF(tx + 2, ty, tx + tabW - 2, ty + accentH), accentBr);
                accentBr->Release();
            }
        }

        // 文字
        COLORREF txtC = isActive ? t.fgPrimary : t.fgSecondary;
        IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, false,
            DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (fmt) {
            ID2D1SolidColorBrush* textBr = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(txtC, m_style.opacity), &textBr);
            if (textBr) {
                D2D1_RECT_F textRc = D2D1::RectF(tx, ty, tx + tabW, ty + tabH);
                rt->DrawText(m_pages[i].title.c_str(), (UINT32)m_pages[i].title.length(), fmt, textRc, textBr);
                textBr->Release();
            }
            fmt->Release();
        }
    }
}

void GpmTabControl::DrawTabContent(ID2D1RenderTarget* rt, D2D1_RECT_F rc) {
    if (m_pages.empty() || m_currentIndex < 0) return;
    
    auto& page = m_pages[m_currentIndex];
    float x = rc.left, y = rc.top + m_tabHeight;
    float w = rc.right - rc.left, h = rc.bottom - rc.top - m_tabHeight;
    
    // 内容区域背景
    auto& t = Theme();
    ID2D1SolidColorBrush* contentBg = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(t.tabActive, m_style.opacity), &contentBg);
    if (contentBg) {
        rt->FillRectangle(D2D1::RectF(x, y, x + w, y + h), contentBg);
        contentBg->Release();
    }

    // 绘制该Tab页下的控件
    // TODO: 当TabControl作为Container时，子控件自行绘制
    // 目前子控件的OnPaintD2D由GpmWindow统一调用，这里不做重复绘制
}

void GpmTabControl::OnMouseMove(int x, int y) {
    if (!m_enabled) return;
    int idx = HitTestTab(x, y);
    if (idx != m_hoverTab) {
        m_hoverTab = idx;
        Invalidate();
    }
}

void GpmTabControl::OnLButtonDown(int x, int y) {
    if (!m_enabled) return;
}

void GpmTabControl::OnLButtonUp(int x, int y) {
    if (!m_enabled) return;
    int idx = HitTestTab(x, y);
    if (idx >= 0 && idx != m_currentIndex) {
        m_currentIndex = idx;
        Invalidate();
        if (m_selectCb) m_selectCb(this, m_id, idx, m_pages[idx].title);
    }
}

void GpmTabControl::OnMouseLeave() {
    if (m_hoverTab >= 0) {
        m_hoverTab = -1;
        Invalidate();
    }
}

} // namespace gpm_ui

#endif // GPMUI_ENABLE_TABCONTROL
