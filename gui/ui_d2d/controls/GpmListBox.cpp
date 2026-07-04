/*
 * GpmListBox.cpp - 列表框控�?(D2D渲染, 虚列�?Virtual Mode, 内嵌子控�?
 * ImGui风格：选中项高亮，悬停变色�?D滚动�?
 */
#include "../core/gpm_ui.h"

#ifdef GPMUI_ENABLE_LISTBOX

namespace gpm_ui {

GpmListBox::GpmListBox() { ApplyTheme(); }
GpmListBox::~GpmListBox() {}

void GpmListBox::ApplyTheme() {
    m_style.ApplyTheme_ListBox();
    auto& t = Theme();
    m_listBg = t.listBg;
    m_listItemBg = t.listItemBg;
    m_listItemHover = t.listItemHover;
    m_listItemSelected = t.listItemSelected;
    m_listItemText = t.listItemText;
    m_listBorder = t.listBorder;
    m_scrollbarColor = t.listScrollbar;
    m_scrollThumbColor = t.listScrollThumb;
    m_style.borderColor = t.listBorder;
    m_style.cornerRadius = ExDPI::Scale(4);
    m_cornerRadius = m_style.cornerRadius;
}

void GpmListBox::Create(GpmWindow* parent, int x, int y, int w, int h, int id) {
    m_parentWnd = parent;
    m_hWnd = parent ? parent->GetWindowHandle() : NULL;
    m_x = ExDPI::Scale(x); m_y = ExDPI::Scale(y);
    m_width = ExDPI::Scale(w); m_height = ExDPI::Scale(h);
    m_id = id;
    m_defaultItemHeight = ExDPI::Scale(32);
    if (parent) parent->AddControl(this);
}

int GpmListBox::AddItem(const std::wstring& text, int height) {
    ExListItem item;
    item.text = text;
    item.height = height > 0 ? ExDPI::Scale(height) : m_defaultItemHeight;
    m_items.push_back(item);
    Invalidate();
    return (int)m_items.size() - 1;
}

void GpmListBox::RemoveItem(int index) {
    if (index >= 0 && index < (int)m_items.size()) {
        m_items.erase(m_items.begin() + index);
        if (m_selIndex >= (int)m_items.size()) m_selIndex = -1;
        Invalidate();
    }
}

void GpmListBox::ClearItems() {
    m_items.clear(); m_selIndex = -1; m_hoverIndex = -1; m_scrollOffset = 0;
    Invalidate();
}

ExListItem* GpmListBox::GetItem(int index) {
    if (index >= 0 && index < (int)m_items.size()) return &m_items[index];
    return nullptr;
}

void GpmListBox::AddItemControl(int itemIndex, const ListItemCtrl& ctrl) {
    if (itemIndex >= 0 && itemIndex < (int)m_items.size()) {
        m_items[itemIndex].ctrls.push_back(ctrl);
        Invalidate();
    }
}

void GpmListBox::SetSelectedIndex(int idx, bool redraw) {
    m_selIndex = idx;
    if (redraw) Invalidate();
}

void GpmListBox::SetScrollOffset(int offset, bool redraw) {
    int mx = GetMaxScroll();
    m_scrollOffset = (std::max)(0, (std::min)(mx, offset));
    if (redraw) Invalidate();
}

void GpmListBox::SetListColors(COLORREF bg, COLORREF itemBg, COLORREF itemHover,
                               COLORREF itemSelected, COLORREF itemText, COLORREF border) {
    m_listBg = bg; m_listItemBg = itemBg; m_listItemHover = itemHover;
    m_listItemSelected = itemSelected; m_listItemText = itemText; m_listBorder = border;
}

void GpmListBox::SetScrollbarColors(COLORREF track, COLORREF thumb) {
    m_scrollbarColor = track; m_scrollThumbColor = thumb;
}

// ---- 虚列�?Virtual Mode ----
void GpmListBox::SetVirtualMode(VirtualGetCount countFn, VirtualGetItem itemFn) {
    m_virtualMode = true;
    m_virtualGetCount = countFn;
    m_virtualGetItem = itemFn;
    m_virtualCount = countFn ? countFn() : 0;
    InvalidateVirtualCache();
    Invalidate();
}

void GpmListBox::UpdateVirtualData() {
    if (m_virtualMode) {
        m_virtualCount = m_virtualGetCount ? m_virtualGetCount() : 0;
        InvalidateVirtualCache();
        Invalidate();
    }
}

bool GpmListBox::GetVirtualItem(int index, std::wstring& text, int& height) {
    if (!m_virtualMode || !m_virtualGetItem) return false;
    
    auto it = m_virtualCache.find(index);
    if (it != m_virtualCache.end() && it->second.valid) {
        text = it->second.text;
        height = it->second.height;
        return true;
    }
    
    bool result = m_virtualGetItem(index, text, height);
    if (result) {
        VirtualCache cache;
        cache.text = text;
        cache.height = height;
        cache.valid = true;
        m_virtualCache[index] = cache;
    }
    return result;
}

int GpmListBox::GetTotalHeight() const {
    if (m_virtualMode) {
        // 虚列表模式下，需要快速估算高�?
        // 可以缓存最近请求的数据
        return m_virtualCount * m_defaultItemHeight;
    }
    int total = 0;
    for (auto& it : m_items) total += it.height;
    return total;
}

int GpmListBox::GetVisibleHeight() const { return m_height; }

int GpmListBox::GetMaxScroll() const {
    int diff = GetTotalHeight() - GetVisibleHeight();
    return diff > 0 ? diff : 0;
}

int GpmListBox::HitTestItem(int y) const {
    int iy = m_y - m_scrollOffset;
    int count = m_virtualMode ? m_virtualCount : (int)m_items.size();
    
    for (int i = 0; i < count; i++) {
        int itemH = m_defaultItemHeight;
        if (!m_virtualMode && i < (int)m_items.size()) {
            itemH = m_items[i].height;
        }
        int itemTop = iy;
        int itemBot = iy + itemH;
        if (y >= itemTop && y < itemBot) return i;
        iy = itemBot;
    }
    return -1;
}

bool GpmListBox::HitTestScrollbar(int x, int y) const {
    int sbW = ExDPI::Scale(8);
    return x >= m_x + m_width - sbW && x < m_x + m_width;
}

int GpmListBox::GetScrollbarThumbRect(D2D1_RECT_F& outRect) const {
    int totalH = GetTotalHeight();
    int visH = GetVisibleHeight();
    if (totalH <= visH) return 0;

    int sbW = ExDPI::Scale(8);
    float trackH = (float)visH;
    float thumbH = (std::max)(ExDPI::ScaleF(20.0f), trackH * visH / totalH);
    float thumbY = (float)m_y + (trackH - thumbH) * m_scrollOffset / (float)GetMaxScroll();

    outRect = D2D1::RectF((float)(m_x + m_width - sbW), thumbY,
                           (float)(m_x + m_width), thumbY + thumbH);
    return 1;
}

ListItemCtrl* GpmListBox::HitTestItemCtrl(int itemIdx, int mx, int my) {
    if (itemIdx < 0 || m_virtualMode || itemIdx >= (int)m_items.size()) return nullptr;
    auto& item = m_items[itemIdx];

    int iy = m_y - m_scrollOffset;
    for (int i = 0; i < itemIdx; i++) iy += m_items[i].height;

    for (auto& ctrl : item.ctrls) {
        int cx = m_x + ctrl.localX;
        int cy = iy + ctrl.localY;
        if (mx >= cx && mx < cx + ctrl.width && my >= cy && my < cy + ctrl.height)
            return &ctrl;
    }
    return nullptr;
}

void GpmListBox::DrawItemCtrl(ID2D1RenderTarget* rt, const ListItemCtrl& ctrl,
                              float itemX, float itemY, float opacity) {
    float cx = itemX + ctrl.localX;
    float cy = itemY + ctrl.localY;
    float cw = (float)ctrl.width;
    float ch = (float)ctrl.height;

    COLORREF bkC = ctrl.bkNormal;
    COLORREF fgC = ctrl.fgNormal;
    if (ctrl.state == STATE_HOVER) { bkC = ctrl.bkHover; fgC = ctrl.fgHover; }
    else if (ctrl.state == STATE_DOWN) { bkC = ctrl.bkDown; fgC = ctrl.fgDown; }

    switch (ctrl.type) {
    case LICT_BUTTON: {
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(bkC, opacity), &br);
        if (br) {
            float r = (float)ctrl.cornerRadius;
            D2D1_ROUNDED_RECT rr = MakeRoundRect(cx, cy, cw, ch, r);
            rt->FillRoundedRectangle(&rr, br);
            br->Release();
        }
        COLORREF lightEdge = RGB(
            (std::min)(255, (int)GetRValue(bkC) + 20),
            (std::min)(255, (int)GetGValue(bkC) + 20),
            (std::min)(255, (int)GetBValue(bkC) + 20));
        COLORREF darkEdge = RGB(
            (std::max)(0, (int)GetRValue(bkC) - 20),
            (std::max)(0, (int)GetGValue(bkC) - 20),
            (std::max)(0, (int)GetBValue(bkC) - 20));
        ID2D1SolidColorBrush* lightBr = nullptr;
        ID2D1SolidColorBrush* darkBr = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(lightEdge, opacity), &lightBr);
        rt->CreateSolidColorBrush(ColorRefToD2D(darkEdge, opacity), &darkBr);
        if (lightBr && darkBr) {
            float r = (float)ctrl.cornerRadius;
            D2D1_ROUNDED_RECT rr = MakeRoundRect(cx, cy, cw, ch, r);
            rt->DrawRoundedRectangle(&rr, darkBr, 1.0f);
            lightBr->Release(); darkBr->Release();
        } else {
            if (lightBr) lightBr->Release();
            if (darkBr) darkBr->Release();
        }
        IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, false,
            DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (fmt) {
            ID2D1SolidColorBrush* tb = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(fgC, opacity), &tb);
            if (tb) {
                D2D1_RECT_F textRc = D2D1::RectF(cx, cy, cx + cw, cy + ch);
                rt->DrawText(ctrl.text.c_str(), (UINT32)ctrl.text.length(), fmt, textRc, tb);
                tb->Release();
            }
            fmt->Release();
        }
        break;
    }
    case LICT_CHECKBOX: {
        float boxSz = ExDPI::ScaleF(14.0f);
        float bx = cx + 2, by = cy + (ch - boxSz) / 2;
        bool checked = ctrl.value != 0;
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(checked ? Theme().checkMark : bkC, opacity), &br);
        if (br) {
            D2D1_ROUNDED_RECT rr = MakeRoundRect(bx, by, boxSz, boxSz, 2.0f);
            rt->FillRoundedRectangle(&rr, br); br->Release();
        }
        if (checked) {
            ID2D1SolidColorBrush* ck = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, opacity), &ck);
            if (ck) {
                float ccx = bx + boxSz / 2, ccy = by + boxSz / 2;
                float s = boxSz * 0.25f;
                rt->DrawLine(D2D1::Point2F(ccx - s, ccy), D2D1::Point2F(ccx - s * 0.2f, ccy + s * 0.7f), ck, 1.5f);
                rt->DrawLine(D2D1::Point2F(ccx - s * 0.2f, ccy + s * 0.7f), D2D1::Point2F(ccx + s, ccy - s * 0.6f), ck, 1.5f);
                ck->Release();
            }
        }
        IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, false,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (fmt) {
            ID2D1SolidColorBrush* tb = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(fgC, opacity), &tb);
            if (tb) {
                D2D1_RECT_F textRc = D2D1::RectF(bx + boxSz + 4, cy, cx + cw, cy + ch);
                rt->DrawText(ctrl.text.c_str(), (UINT32)ctrl.text.length(), fmt, textRc, tb);
                tb->Release();
            }
            fmt->Release();
        }
        break;
    }
    case LICT_PROGRESSBAR: {
        float range = (float)(ctrl.maxVal - ctrl.minVal);
        float ratio = range > 0 ? (float)(ctrl.value - ctrl.minVal) / range : 0;
        ID2D1SolidColorBrush* tr = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(bkC, opacity), &tr);
        if (tr) {
            D2D1_ROUNDED_RECT rr = MakeRoundRect(cx, cy + ch * 0.3f, cw, ch * 0.4f, ch * 0.2f);
            rt->FillRoundedRectangle(&rr, tr); tr->Release();
        }
        if (ratio > 0) {
            ID2D1SolidColorBrush* fl = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(fgC, opacity), &fl);
            if (fl) {
                float fillW = cw * ratio;
                D2D1_ROUNDED_RECT rr = MakeRoundRect(cx, cy + ch * 0.3f, fillW, ch * 0.4f, ch * 0.2f);
                rt->FillRoundedRectangle(&rr, fl); fl->Release();
            }
        }
        break;
    }
    case LICT_SLIDER: {
        float range = (float)(ctrl.maxVal - ctrl.minVal);
        float ratio = range > 0 ? (float)(ctrl.value - ctrl.minVal) / range : 0;
        float trackY = cy + ch / 2 - 2;
        ID2D1SolidColorBrush* tr = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(bkC, opacity), &tr);
        if (tr) {
            D2D1_ROUNDED_RECT rr = MakeRoundRect(cx, trackY, cw, 4.0f, 2.0f);
            rt->FillRoundedRectangle(&rr, tr); tr->Release();
        }
        float thumbX = cx + ratio * cw;
        ID2D1SolidColorBrush* tb = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(fgC, opacity), &tb);
        if (tb) {
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, cy + ch / 2), 6, 6), tb);
            tb->Release();
        }
        break;
    }
    default: break;
    }
}

void GpmListBox::OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) {
    if (!m_visible || !rt) return;

    float x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
    float r = (float)m_style.cornerRadius;
    float opacity = m_style.opacity;

    // 背景
    ID2D1SolidColorBrush* bgBr = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(m_listBg, opacity), &bgBr);
    if (bgBr) {
        D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, w, h, r);
        rt->FillRoundedRectangle(&rr, bgBr); bgBr->Release();
    }

    // 3D边框
    if (m_listBorder) {
        COLORREF lightBorder = RGB(
            (std::min)(255, (int)GetRValue(m_listBorder) + 15),
            (std::min)(255, (int)GetGValue(m_listBorder) + 15),
            (std::min)(255, (int)GetBValue(m_listBorder) + 15));
        COLORREF darkBorder = RGB(
            (std::max)(0, (int)GetRValue(m_listBorder) - 15),
            (std::max)(0, (int)GetGValue(m_listBorder) - 15),
            (std::max)(0, (int)GetBValue(m_listBorder) - 15));
        ID2D1SolidColorBrush* lightBr = nullptr;
        ID2D1SolidColorBrush* darkBr = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(lightBorder, opacity), &lightBr);
        rt->CreateSolidColorBrush(ColorRefToD2D(darkBorder, opacity), &darkBr);
        if (lightBr && darkBr) {
            D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, w, h, r);
            rt->DrawRoundedRectangle(&rr, darkBr, 1.0f);
            lightBr->Release(); darkBr->Release();
        } else {
            if (lightBr) lightBr->Release();
            if (darkBr) darkBr->Release();
            ID2D1SolidColorBrush* bBr = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(m_listBorder, opacity), &bBr);
            if (bBr) {
                D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, w, h, r);
                rt->DrawRoundedRectangle(&rr, bBr, 1.0f); bBr->Release();
            }
        }
    }

    // 裁剪
    D2D1_ROUNDED_RECT clipRR = MakeRoundRect(x, y, w, h, r);
    ID2D1RoundedRectangleGeometry* clipGeo = nullptr;
    ExD2DFactory::GetFactory()->CreateRoundedRectangleGeometry(clipRR, &clipGeo);
    if (!clipGeo) return;
    rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clipGeo), nullptr);

    float pad = ExDPI::ScaleF(6.0f);
    float iy = y - m_scrollOffset;
    int sbW = ExDPI::Scale(8);
    float contentW = w - (GetTotalHeight() > GetVisibleHeight() ? sbW : 0);

    int itemCount = m_virtualMode ? m_virtualCount : (int)m_items.size();
    for (int i = 0; i < itemCount; i++) {
        int itemH = m_defaultItemHeight;
        std::wstring itemText;
        bool hasCustomH = false;
        
        if (m_virtualMode) {
            int vH = 0;
            std::wstring vT;
            if (GetVirtualItem(i, vT, vH)) {
                itemText = vT;
                if (vH > 0) { itemH = vH; hasCustomH = true; }
            } else {
                wchar_t buf[32];
                swprintf_s(buf, L"项目 %d", i);
                itemText = buf;
            }
        } else if (i < (int)m_items.size()) {
            itemText = m_items[i].text;
            itemH = m_items[i].height;
            hasCustomH = true;
        }

        float itemTop = iy;
        float itemBot = iy + itemH;
        iy = itemBot;

        if (itemBot < y || itemTop > y + h) continue;

        bool isSel = (i == m_selIndex);
        bool isHov = (i == m_hoverIndex);
        COLORREF ibg = m_listItemBg;
        if (!m_virtualMode && i < (int)m_items.size() && m_items[i].bgColor)
            ibg = m_items[i].bgColor;
        if (isSel) ibg = m_listItemSelected;
        else if (isHov) ibg = m_listItemHover;

        ID2D1SolidColorBrush* ib = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(ibg, opacity), &ib);
        if (ib) {
            rt->FillRectangle(D2D1::RectF(x + 1, itemTop, x + contentW - 1, itemBot), ib);
            ib->Release();
        }

        if (isSel) {
            ID2D1SolidColorBrush* selMark = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(Theme().fgAccent, opacity), &selMark);
            if (selMark) {
                rt->FillRectangle(D2D1::RectF(x + 1, itemTop + 2, x + 3, itemBot - 2), selMark);
                selMark->Release();
            }
        }

        COLORREF txC = m_listItemText;
        if (!m_virtualMode && i < (int)m_items.size() && m_items[i].textColor)
            txC = m_items[i].textColor;

        IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, false,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (fmt && !itemText.empty()) {
            ID2D1SolidColorBrush* tb = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(txC, opacity), &tb);
            if (tb) {
                D2D1_RECT_F textRc = D2D1::RectF(x + pad + (isSel ? 4.0f : 0), itemTop, x + contentW - pad, itemBot);
                rt->DrawText(itemText.c_str(), (UINT32)itemText.length(), fmt, textRc, tb);
                tb->Release();
            }
            fmt->Release();
        }

        // 嵌入控件 (仅非虚列表模�?
        if (!m_virtualMode && i < (int)m_items.size()) {
            for (auto& ctrl : m_items[i].ctrls) {
                DrawItemCtrl(rt, ctrl, x, itemTop, opacity);
            }
        }
    }

    // 滚动�?
    D2D1_RECT_F thumbRc;
    if (GetScrollbarThumbRect(thumbRc)) {
        ID2D1SolidColorBrush* sbBr = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(m_scrollbarColor, opacity * 0.5f), &sbBr);
        if (sbBr) {
            D2D1_RECT_F trackRc = D2D1::RectF(x + w - sbW, y, x + w, y + h);
            rt->FillRectangle(trackRc, sbBr); sbBr->Release();
        }
        ID2D1SolidColorBrush* thBr = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(m_scrollThumbColor, opacity), &thBr);
        if (thBr) {
            D2D1_ROUNDED_RECT rr;
            rr.rect = thumbRc; rr.radiusX = sbW / 2.0f; rr.radiusY = sbW / 2.0f;
            rt->FillRoundedRectangle(&rr, thBr); thBr->Release();
        }
    }

    rt->PopLayer();
    clipGeo->Release();
}

void GpmListBox::OnMouseMove(int x, int y) {
    if (!m_enabled) return;

    // 拖拽时用 GetCursorPos 获取真实鼠标位置（支持窗口外�?
    POINT pt;
    ::GetCursorPos(&pt);
    if (m_hWnd) ::ScreenToClient(m_hWnd, &pt);
    int realY = pt.y;

    if (m_scrollDragging) {
        int totalH = GetTotalHeight();
        int visH = GetVisibleHeight();
        if (totalH > visH) {
            int deltaY = realY - m_scrollDragStartY;
            float trackH = (float)visH;
            float thumbH = (std::max)(ExDPI::ScaleF(20.0f), trackH * visH / totalH);
            float scrollRange = trackH - thumbH;
            if (scrollRange > 0) {
                int newOffset = m_scrollDragStartOffset + (int)(deltaY * GetMaxScroll() / scrollRange);
                SetScrollOffset(newOffset);
            }
        }
        return;
    }

    int idx = HitTestItem(y);
    if (idx != m_hoverIndex) {
        if (m_hoverIndex >= 0 && !m_virtualMode && m_hoverIndex < (int)m_items.size()) {
            for (auto& c : m_items[m_hoverIndex].ctrls) c.state = STATE_NORMAL;
        }
        m_hoverIndex = idx;
        Invalidate();
    }

    if (idx >= 0 && !m_virtualMode) {
        ListItemCtrl* ctrl = HitTestItemCtrl(idx, x, y);
        for (auto& c : m_items[idx].ctrls) {
            c.state = (&c == ctrl) ? STATE_HOVER : STATE_NORMAL;
        }
        Invalidate();
    }
}

void GpmListBox::OnLButtonDown(int x, int y) {
    if (!m_enabled) return;

    if (HitTestScrollbar(x, y)) {
        D2D1_RECT_F thumbRc;
        if (GetScrollbarThumbRect(thumbRc)) {
            if (y >= thumbRc.top && y <= thumbRc.bottom) {
                m_scrollDragging = true;
                m_scrollDragStartY = y;
                m_scrollDragStartOffset = m_scrollOffset;
                if (m_hWnd) ::SetCapture(m_hWnd);
                return;
            }
        }
    }

    int idx = HitTestItem(y);
    if (idx >= 0) {
        if (!m_virtualMode) {
            ListItemCtrl* ctrl = HitTestItemCtrl(idx, x, y);
            if (ctrl) {
                ctrl->state = STATE_DOWN;
                Invalidate();
                return;
            }
        }

        if (idx != m_selIndex) {
            m_selIndex = idx; Invalidate();
            std::wstring text;
            if (m_virtualMode) {
                int h;
                GetVirtualItem(idx, text, h);
            } else if (idx < (int)m_items.size()) {
                text = m_items[idx].text;
            }
            if (m_selectCb) m_selectCb(this, m_id, idx, text);
        }
    }
}

void GpmListBox::OnLButtonUp(int x, int y) {
    if (m_scrollDragging) {
        m_scrollDragging = false;
        if (m_hWnd) ::ReleaseCapture();
        return;
    }
    if (!m_enabled) return;

    int idx = HitTestItem(y);
    if (idx >= 0 && !m_virtualMode) {
        ListItemCtrl* ctrl = HitTestItemCtrl(idx, x, y);
        if (ctrl && ctrl->state == STATE_DOWN) {
            if (ctrl->type == LICT_BUTTON) {
                if (ctrl->clickCb) ctrl->clickCb(this, m_id);
            } else if (ctrl->type == LICT_CHECKBOX) {
                ctrl->value = ctrl->value ? 0 : 1;
                if (ctrl->clickCb) ctrl->clickCb(this, m_id);
            }
            ctrl->state = STATE_HOVER;
            Invalidate();
        }
    }
}

void GpmListBox::OnMouseLeave() {
    // 不重置拖拽状态！SetCapture 保证在窗口外松开时能收到 LButtonUp
    if (m_hoverIndex >= 0) {
        if (!m_virtualMode && m_hoverIndex < (int)m_items.size()) {
            for (auto& c : m_items[m_hoverIndex].ctrls) c.state = STATE_NORMAL;
        }
        m_hoverIndex = -1;
        Invalidate();
    }
}

void GpmListBox::OnMouseWheel(int x, int y, int delta) {
    int step = ExDPI::Scale(40);
    SetScrollOffset(m_scrollOffset - (delta > 0 ? step : -step));
}

} // namespace gpm_ui

#endif // GPMUI_ENABLE_LISTBOX
