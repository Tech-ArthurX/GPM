/*
 * GpmSuperListBox.cpp - 超级列表框控�?(D2D渲染)
 * 每个项目文字可独立设置颜�?背景�?
 * 可插�? 按钮/标签/进度�?滑块/组合�?等嵌入控�?
 * ImGui风格：选中高亮，悬停变色，3D滚动�?
 */
#include "../core/gpm_ui.h"
#include <cstdio>
#include <cmath>

#ifdef GPMUI_ENABLE_SUPERLISTBOX

namespace gpm_ui {

GpmSuperListBox::GpmSuperListBox() 
    : m_selIndex(-1), m_hoverIndex(-1), m_scrollOffset(0), 
      m_defaultItemHeight(0), m_cornerRadius(0),
      m_scrollDragging(false), m_scrollDragStartY(0), m_scrollDragStartOffset(0),
      m_sliderDragging(false), m_sliderDragCtrl(nullptr),
      m_selectCb(nullptr)
{ ApplyTheme(); }
GpmSuperListBox::~GpmSuperListBox() {}

void GpmSuperListBox::ApplyTheme() {
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

void GpmSuperListBox::Create(GpmWindow* parent, int x, int y, int w, int h, int id) {
    m_parentWnd = parent;
    m_hWnd = parent ? parent->GetWindowHandle() : NULL;
    m_x = ExDPI::Scale(x); m_y = ExDPI::Scale(y);
    m_width = ExDPI::Scale(w); m_height = ExDPI::Scale(h);
    m_id = id;
    m_defaultItemHeight = ExDPI::Scale(36);
    if (parent) parent->AddControl(this);
}

int GpmSuperListBox::AddItem(const std::wstring& text, int height) {
    SuperListItem item;
    item.text = text;
    item.height = height > 0 ? ExDPI::Scale(height) : m_defaultItemHeight;
    item.textColor = m_listItemText;
    item.bgColor = 0;
    m_items.push_back(item);
    Invalidate();
    return (int)m_items.size() - 1;
}

void GpmSuperListBox::RemoveItem(int index) {
    if (index >= 0 && index < (int)m_items.size()) {
        m_items.erase(m_items.begin() + index);
        if (m_selIndex >= (int)m_items.size()) m_selIndex = -1;
        Invalidate();
    }
}

void GpmSuperListBox::ClearItems() {
    m_items.clear();
    m_selIndex = -1; m_hoverIndex = -1; m_scrollOffset = 0;
    Invalidate();
}

SuperListItem* GpmSuperListBox::GetItem(int index) {
    if (index >= 0 && index < (int)m_items.size()) return &m_items[index];
    return nullptr;
}

void GpmSuperListBox::SetItemTextColor(int index, COLORREF color) {
    if (index >= 0 && index < (int)m_items.size()) {
        m_items[index].textColor = color;
        Invalidate();
    }
}

void GpmSuperListBox::SetItemBgColor(int index, COLORREF color) {
    if (index >= 0 && index < (int)m_items.size()) {
        m_items[index].bgColor = color;
        Invalidate();
    }
}

void GpmSuperListBox::SetItemHeight(int index, int height) {
    if (index >= 0 && index < (int)m_items.size()) {
        m_items[index].height = ExDPI::Scale(height);
        Invalidate();
    }
}

void GpmSuperListBox::SetItemFont(int index, float fontSize, bool bold) {
    if (index >= 0 && index < (int)m_items.size()) {
        m_items[index].fontSize = fontSize;
        m_items[index].bold = bold;
        Invalidate();
    }
}

void GpmSuperListBox::AddItemButton(int itemIdx, int localX, int localY, int w, int h,
                                    const std::wstring& text, ClickCallback cb) {
    if (itemIdx < 0 || itemIdx >= (int)m_items.size()) return;
    auto& t = Theme();
    SuperListItemCtrl ctrl;
    ctrl.type = SLICT_BUTTON;
    ctrl.localX = ExDPI::Scale(localX); ctrl.localY = ExDPI::Scale(localY);
    ctrl.width = ExDPI::Scale(w); ctrl.height = ExDPI::Scale(h);
    ctrl.text = text;
    ctrl.bkNormal = t.btnBg; ctrl.bkHover = t.btnBgHover; ctrl.bkDown = t.btnBgDown;
    ctrl.fgNormal = t.btnFg; ctrl.fgHover = t.btnFg; ctrl.fgDown = t.btnFg;
    ctrl.cornerRadius = ExDPI::Scale(4);
    ctrl.clickCb = cb;
    m_items[itemIdx].ctrls.push_back(ctrl);
    Invalidate();
}

void GpmSuperListBox::AddItemLabel(int itemIdx, int localX, int localY, int w, int h,
                                   const std::wstring& text, COLORREF color) {
    if (itemIdx < 0 || itemIdx >= (int)m_items.size()) return;
    SuperListItemCtrl ctrl;
    ctrl.type = SLICT_LABEL;
    ctrl.localX = ExDPI::Scale(localX); ctrl.localY = ExDPI::Scale(localY);
    ctrl.width = ExDPI::Scale(w); ctrl.height = ExDPI::Scale(h);
    ctrl.text = text;
    ctrl.fgNormal = color ? color : m_listItemText;
    ctrl.fgHover = ctrl.fgNormal; ctrl.fgDown = ctrl.fgNormal;
    m_items[itemIdx].ctrls.push_back(ctrl);
    Invalidate();
}

void GpmSuperListBox::AddItemProgressBar(int itemIdx, int localX, int localY, int w, int h,
                                         int value, int minVal, int maxVal) {
    if (itemIdx < 0 || itemIdx >= (int)m_items.size()) return;
    auto& t = Theme();
    SuperListItemCtrl ctrl;
    ctrl.type = SLICT_PROGRESSBAR;
    ctrl.localX = ExDPI::Scale(localX); ctrl.localY = ExDPI::Scale(localY);
    ctrl.width = ExDPI::Scale(w); ctrl.height = ExDPI::Scale(h);
    ctrl.value = value; ctrl.minVal = minVal; ctrl.maxVal = maxVal;
    ctrl.bkNormal = t.progressBg;
    ctrl.fgNormal = t.progressFill;
    ctrl.cornerRadius = ExDPI::Scale(3);
    m_items[itemIdx].ctrls.push_back(ctrl);
    Invalidate();
}

void GpmSuperListBox::AddItemSlider(int itemIdx, int localX, int localY, int w, int h,
                                    int value, int minVal, int maxVal, ValueCallback cb) {
    if (itemIdx < 0 || itemIdx >= (int)m_items.size()) return;
    auto& t = Theme();
    SuperListItemCtrl ctrl;
    ctrl.type = SLICT_SLIDER;
    ctrl.localX = ExDPI::Scale(localX); ctrl.localY = ExDPI::Scale(localY);
    ctrl.width = ExDPI::Scale(w); ctrl.height = ExDPI::Scale(h);
    ctrl.value = value; ctrl.minVal = minVal; ctrl.maxVal = maxVal;
    ctrl.bkNormal = t.sliderTrack;
    ctrl.fgNormal = t.sliderFill;
    ctrl.valueCb = cb;
    m_items[itemIdx].ctrls.push_back(ctrl);
    Invalidate();
}

void GpmSuperListBox::AddItemComboBox(int itemIdx, int localX, int localY, int w, int h,
                                      const std::vector<std::wstring>& items, int selIdx,
                                      SelectCallback cb) {
    if (itemIdx < 0 || itemIdx >= (int)m_items.size()) return;
    auto& t = Theme();
    SuperListItemCtrl ctrl;
    ctrl.type = SLICT_COMBOBOX;
    ctrl.localX = ExDPI::Scale(localX); ctrl.localY = ExDPI::Scale(localY);
    ctrl.width = ExDPI::Scale(w); ctrl.height = ExDPI::Scale(h);
    ctrl.comboItems = items;
    ctrl.value = selIdx;
    ctrl.bkNormal = t.bgInput; ctrl.bkHover = t.bgHover; ctrl.bkDown = t.bgActive;
    ctrl.fgNormal = t.fgPrimary; ctrl.fgHover = t.fgPrimary; ctrl.fgDown = t.fgPrimary;
    ctrl.cornerRadius = ExDPI::Scale(4);
    ctrl.selectCb = cb;
    m_items[itemIdx].ctrls.push_back(ctrl);
    Invalidate();
}

void GpmSuperListBox::AddItemCheckBox(int itemIdx, int localX, int localY, int w, int h,
                                      const std::wstring& text, bool checked, ClickCallback cb) {
    if (itemIdx < 0 || itemIdx >= (int)m_items.size()) return;
    auto& t = Theme();
    SuperListItemCtrl ctrl;
    ctrl.type = SLICT_CHECKBOX;
    ctrl.localX = ExDPI::Scale(localX); ctrl.localY = ExDPI::Scale(localY);
    ctrl.width = ExDPI::Scale(w); ctrl.height = ExDPI::Scale(h);
    ctrl.text = text;
    ctrl.value = checked ? 1 : 0;
    ctrl.bkNormal = t.bgInput; ctrl.bkHover = t.bgHover; ctrl.bkDown = t.bgActive;
    ctrl.fgNormal = t.fgPrimary; ctrl.fgHover = t.fgPrimary; ctrl.fgDown = t.fgPrimary;
    ctrl.clickCb = cb;
    m_items[itemIdx].ctrls.push_back(ctrl);
    Invalidate();
}

void GpmSuperListBox::SetSelectedIndex(int idx, bool redraw) {
    m_selIndex = idx;
    if (redraw) Invalidate();
}

void GpmSuperListBox::SetScrollOffset(int offset, bool redraw) {
    int mx = GetMaxScroll();
    m_scrollOffset = (std::max)(0, (std::min)(mx, offset));
    if (redraw) Invalidate();
}

void GpmSuperListBox::SetListColors(COLORREF bg, COLORREF itemBg, COLORREF itemHover,
                                    COLORREF itemSelected, COLORREF itemText, COLORREF border) {
    m_listBg = bg; m_listItemBg = itemBg; m_listItemHover = itemHover;
    m_listItemSelected = itemSelected; m_listItemText = itemText; m_listBorder = border;
}

void GpmSuperListBox::SetScrollbarColors(COLORREF track, COLORREF thumb) {
    m_scrollbarColor = track; m_scrollThumbColor = thumb;
}

// ============================================================
// Internal helpers
// ============================================================
int GpmSuperListBox::GetTotalHeight() const {
    int total = 0;
    for (auto& it : m_items) total += it.height;
    return total;
}

int GpmSuperListBox::GetMaxScroll() const {
    int diff = GetTotalHeight() - GetVisibleHeight();
    return diff > 0 ? diff : 0;
}

int GpmSuperListBox::HitTestItem(int y) const {
    int iy = m_y - m_scrollOffset;
    for (int i = 0; i < (int)m_items.size(); i++) {
        int itemTop = iy;
        int itemBot = iy + m_items[i].height;
        if (y >= itemTop && y < itemBot) return i;
        iy = itemBot;
    }
    return -1;
}

bool GpmSuperListBox::HitTestScrollbar(int x, int y) const {
    int sbW = ExDPI::Scale(8);
    return x >= m_x + m_width - sbW && x < m_x + m_width;
}

int GpmSuperListBox::GetScrollbarThumbRect(D2D1_RECT_F& outRect) const {
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

SuperListItemCtrl* GpmSuperListBox::HitTestItemCtrl(int itemIdx, int mx, int my) {
    if (itemIdx < 0 || itemIdx >= (int)m_items.size()) return nullptr;
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

// ============================================================
// Draw embedded controls
// ============================================================
void GpmSuperListBox::DrawItemCtrl(ID2D1RenderTarget* rt, const SuperListItemCtrl& ctrl,
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
    case SLICT_BUTTON: {
        // 按钮背景
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(bkC, opacity), &br);
        if (br) {
            float r = (float)ctrl.cornerRadius;
            D2D1_ROUNDED_RECT rr = MakeRoundRect(cx, cy, cw, ch, r);
            rt->FillRoundedRectangle(&rr, br); br->Release();
        }
        // 3D边框
        COLORREF darkEdge = RGB(
            (std::max)(0, (int)GetRValue(bkC) - 20),
            (std::max)(0, (int)GetGValue(bkC) - 20),
            (std::max)(0, (int)GetBValue(bkC) - 20));
        ID2D1SolidColorBrush* dBr = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(darkEdge, opacity), &dBr);
        if (dBr) {
            D2D1_ROUNDED_RECT rr = MakeRoundRect(cx, cy, cw, ch, (float)ctrl.cornerRadius);
            rt->DrawRoundedRectangle(&rr, dBr, 1.0f); dBr->Release();
        }
        // 文字
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
    case SLICT_LABEL: {
        IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, false,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
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
    case SLICT_PROGRESSBAR: {
        float range = (float)(ctrl.maxVal - ctrl.minVal);
        float ratio = range > 0 ? (float)(ctrl.value - ctrl.minVal) / range : 0;
        float r = (float)ctrl.cornerRadius;
        // Track
        ID2D1SolidColorBrush* tr = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(ctrl.bkNormal, opacity), &tr);
        if (tr) {
            D2D1_ROUNDED_RECT rr = MakeRoundRect(cx, cy, cw, ch, r);
            rt->FillRoundedRectangle(&rr, tr); tr->Release();
        }
        // Fill
        if (ratio > 0) {
            ID2D1SolidColorBrush* fl = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(ctrl.fgNormal, opacity), &fl);
            if (fl) {
                float fillW = cw * ratio;
                if (fillW < cw) {
                    D2D1_ROUNDED_RECT rrClip = MakeRoundRect(cx, cy, cw, ch, r);
                    ID2D1RoundedRectangleGeometry* geo = nullptr;
                    ExD2DFactory::GetFactory()->CreateRoundedRectangleGeometry(rrClip, &geo);
                    if (geo) {
                        rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), geo), nullptr);
                        rt->FillRectangle(D2D1::RectF(cx, cy, cx + fillW, cy + ch), fl);
                        rt->PopLayer();
                        geo->Release();
                    }
                } else {
                    D2D1_ROUNDED_RECT rr = MakeRoundRect(cx, cy, fillW, ch, r);
                    rt->FillRoundedRectangle(&rr, fl);
                }
                fl->Release();
            }
        }
        // Percentage text
        int pct = (int)(ratio * 100 + 0.5f);
        wchar_t buf[16]; swprintf_s(buf, L"%d%%", pct);
        IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, false,
            DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (fmt) {
            ID2D1SolidColorBrush* tb = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(RGB(255,255,255), opacity), &tb);
            if (tb) {
                D2D1_RECT_F textRc = D2D1::RectF(cx, cy, cx + cw, cy + ch);
                rt->DrawText(buf, (UINT32)wcslen(buf), fmt, textRc, tb); tb->Release();
            }
            fmt->Release();
        }
        break;
    }
    case SLICT_SLIDER: {
        float range = (float)(ctrl.maxVal - ctrl.minVal);
        float ratio = range > 0 ? (float)(ctrl.value - ctrl.minVal) / range : 0;
        float trackH = 4.0f;
        float trackY = cy + (ch - trackH) / 2;
        float thumbR = ch * 0.3f;
        float trackL = cx + thumbR;
        float trackR = cx + cw - thumbR;
        // Track
        ID2D1SolidColorBrush* tr = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(ctrl.bkNormal, opacity), &tr);
        if (tr) {
            D2D1_ROUNDED_RECT rr = MakeRoundRect(trackL, trackY, trackR - trackL, trackH, 2.0f);
            rt->FillRoundedRectangle(&rr, tr); tr->Release();
        }
        // Fill
        float thumbX = trackL + ratio * (trackR - trackL);
        ID2D1SolidColorBrush* fl = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(ctrl.fgNormal, opacity), &fl);
        if (fl) {
            float fillW = thumbX - trackL;
            if (fillW > 0) {
                D2D1_ROUNDED_RECT rr = MakeRoundRect(trackL, trackY, fillW, trackH, 2.0f);
                rt->FillRoundedRectangle(&rr, fl);
            }
            fl->Release();
        }
        // Thumb
        ID2D1SolidColorBrush* tb = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(Theme().sliderThumb, opacity), &tb);
        if (tb) {
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, cy + ch / 2), thumbR, thumbR), tb);
            tb->Release();
        }
        break;
    }
    case SLICT_COMBOBOX: {
        // ComboBox background
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(bkC, opacity), &br);
        if (br) {
            float r = (float)ctrl.cornerRadius;
            D2D1_ROUNDED_RECT rr = MakeRoundRect(cx, cy, cw, ch, r);
            rt->FillRoundedRectangle(&rr, br); br->Release();
        }
        // Border
        COLORREF darkEdge = RGB(
            (std::max)(0, (int)GetRValue(bkC) - 15),
            (std::max)(0, (int)GetGValue(bkC) - 15),
            (std::max)(0, (int)GetBValue(bkC) - 15));
        ID2D1SolidColorBrush* dBr = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(darkEdge, opacity), &dBr);
        if (dBr) {
            D2D1_ROUNDED_RECT rr = MakeRoundRect(cx, cy, cw, ch, (float)ctrl.cornerRadius);
            rt->DrawRoundedRectangle(&rr, dBr, 1.0f); dBr->Release();
        }
        // Selected text
        std::wstring dispText = L"--";
        if (ctrl.value >= 0 && ctrl.value < (int)ctrl.comboItems.size())
            dispText = ctrl.comboItems[ctrl.value];
        float pad = ExDPI::ScaleF(6.0f);
        IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, false,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (fmt) {
            ID2D1SolidColorBrush* tb = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(fgC, opacity), &tb);
            if (tb) {
                D2D1_RECT_F textRc = D2D1::RectF(cx + pad, cy, cx + cw - ch, cy + ch);
                rt->DrawText(dispText.c_str(), (UINT32)dispText.length(), fmt, textRc, tb);
                tb->Release();
            }
            fmt->Release();
        }
        // Arrow
        ID2D1SolidColorBrush* arBr = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(fgC, opacity), &arBr);
        if (arBr) {
            float arCx = cx + cw - ch / 2.0f;
            float arCy = cy + ch / 2.0f;
            float as = ExDPI::ScaleF(3.0f);
            ID2D1PathGeometry* geo = nullptr;
            ExD2DFactory::GetFactory()->CreatePathGeometry(&geo);
            if (geo) {
                ID2D1GeometrySink* sink = nullptr;
                geo->Open(&sink);
                if (sink) {
                    sink->BeginFigure(D2D1::Point2F(arCx - as, arCy - as/2), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(arCx + as, arCy - as/2));
                    sink->AddLine(D2D1::Point2F(arCx, arCy + as/2));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close(); sink->Release();
                    rt->FillGeometry(geo, arBr);
                }
                geo->Release();
            }
            arBr->Release();
        }
        break;
    }
    case SLICT_CHECKBOX: {
        float boxSz = ExDPI::ScaleF(14.0f);
        float bx = cx + 2, by = cy + (ch - boxSz) / 2;
        bool checked = ctrl.value != 0;
        auto& t = Theme();
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(checked ? t.checkMark : bkC, opacity), &br);
        if (br) {
            D2D1_ROUNDED_RECT rr = MakeRoundRect(bx, by, boxSz, boxSz, 2.0f);
            rt->FillRoundedRectangle(&rr, br); br->Release();
        }
        if (checked) {
            ID2D1SolidColorBrush* ck = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, opacity), &ck);
            if (ck) {
                float ccx = bx + boxSz/2, ccy = by + boxSz/2;
                float s = boxSz * 0.25f;
                rt->DrawLine(D2D1::Point2F(ccx-s, ccy), D2D1::Point2F(ccx-s*0.2f, ccy+s*0.7f), ck, 1.5f);
                rt->DrawLine(D2D1::Point2F(ccx-s*0.2f, ccy+s*0.7f), D2D1::Point2F(ccx+s, ccy-s*0.6f), ck, 1.5f);
                ck->Release();
            }
        }
        // Border
        ID2D1SolidColorBrush* bdr = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(checked ? t.checkMark : t.border, opacity), &bdr);
        if (bdr) {
            D2D1_ROUNDED_RECT rr = MakeRoundRect(bx, by, boxSz, boxSz, 2.0f);
            rt->DrawRoundedRectangle(&rr, bdr, 1.0f); bdr->Release();
        }
        // Text
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
    default: break;
    }
}

// ============================================================
// OnPaintD2D
// ============================================================
void GpmSuperListBox::OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) {
    if (!m_visible || !rt) return;

    float x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
    float r = (float)m_style.cornerRadius;
    float opacity = m_style.opacity;

    // Background
    ID2D1SolidColorBrush* bgBr = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(m_listBg, opacity), &bgBr);
    if (bgBr) {
        D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, w, h, r);
        rt->FillRoundedRectangle(&rr, bgBr); bgBr->Release();
    }

    // 3D border
    if (m_listBorder) {
        COLORREF darkBorder = RGB(
            (std::max)(0, (int)GetRValue(m_listBorder) - 15),
            (std::max)(0, (int)GetGValue(m_listBorder) - 15),
            (std::max)(0, (int)GetBValue(m_listBorder) - 15));
        ID2D1SolidColorBrush* bBr = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(darkBorder, opacity), &bBr);
        if (bBr) {
            D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, w, h, r);
            rt->DrawRoundedRectangle(&rr, bBr, 1.0f); bBr->Release();
        }
    }

    // Clip
    D2D1_ROUNDED_RECT clipRR = MakeRoundRect(x, y, w, h, r);
    ID2D1RoundedRectangleGeometry* clipGeo = nullptr;
    ExD2DFactory::GetFactory()->CreateRoundedRectangleGeometry(clipRR, &clipGeo);
    if (!clipGeo) return;
    rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clipGeo), nullptr);

    float pad = ExDPI::ScaleF(6.0f);
    float iy = y - m_scrollOffset;
    int sbW = ExDPI::Scale(8);
    float contentW = w - (GetTotalHeight() > GetVisibleHeight() ? sbW : 0);

    for (int i = 0; i < (int)m_items.size(); i++) {
        auto& item = m_items[i];
        float itemTop = iy;
        float itemBot = iy + item.height;
        iy = itemBot;

        if (itemBot < y || itemTop > y + h) continue;

        bool isSel = (i == m_selIndex);
        bool isHov = (i == m_hoverIndex);

        // Item background
        COLORREF ibg = m_listItemBg;
        if (item.bgColor) ibg = item.bgColor;
        if (isSel) ibg = m_listItemSelected;
        else if (isHov) ibg = m_listItemHover;

        ID2D1SolidColorBrush* ib = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(ibg, opacity), &ib);
        if (ib) {
            rt->FillRectangle(D2D1::RectF(x + 1, itemTop, x + contentW - 1, itemBot), ib);
            ib->Release();
        }

        // Selection indicator
        if (isSel) {
            ID2D1SolidColorBrush* selMark = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(Theme().fgAccent, opacity), &selMark);
            if (selMark) {
                rt->FillRectangle(D2D1::RectF(x + 1, itemTop + 2, x + 3, itemBot - 2), selMark);
                selMark->Release();
            }
        }

        // Item text (with individual color)
        COLORREF txC = item.textColor ? item.textColor : m_listItemText;
        float fontSize = item.fontSize > 0 ? item.fontSize : 9.5f;
        IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(fontSize, item.bold,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (fmt && !item.text.empty()) {
            ID2D1SolidColorBrush* tb = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(txC, opacity), &tb);
            if (tb) {
                float textLeft = x + pad + (isSel ? 4.0f : 0);
                D2D1_RECT_F textRc = D2D1::RectF(textLeft, itemTop, x + contentW - pad, itemBot);
                rt->DrawText(item.text.c_str(), (UINT32)item.text.length(), fmt, textRc, tb);
                tb->Release();
            }
            fmt->Release();
        }

        // Embedded controls
        for (auto& ctrl : item.ctrls) {
            DrawItemCtrl(rt, ctrl, x, itemTop, opacity);
        }
    }

    // Scrollbar
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

// ============================================================
// Mouse handling
// ============================================================
void GpmSuperListBox::OnMouseMove(int x, int y) {
    if (!m_enabled) return;

    // 拖拽时用 GetCursorPos 获取真实鼠标位置（支持窗口外�?
    POINT pt;
    ::GetCursorPos(&pt);
    if (m_hWnd) ::ScreenToClient(m_hWnd, &pt);
    int realX = pt.x;
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
                int newOff = m_scrollDragStartOffset + (int)(deltaY * GetMaxScroll() / scrollRange);
                SetScrollOffset(newOff);
            }
        }
        return;
    }

    // Slider drag inside item
    if (m_sliderDragging && m_sliderDragCtrl) {
        float trackL = (float)(m_x + m_sliderDragCtrl->localX) + m_sliderDragCtrl->height * 0.3f;
        float trackR = (float)(m_x + m_sliderDragCtrl->localX + m_sliderDragCtrl->width) - m_sliderDragCtrl->height * 0.3f;
        float ratio = ((float)realX - trackL) / (trackR - trackL);
        ratio = (std::max)(0.0f, (std::min)(1.0f, ratio));
        int newVal = m_sliderDragCtrl->minVal + (int)(ratio * (m_sliderDragCtrl->maxVal - m_sliderDragCtrl->minVal) + 0.5f);
        if (newVal != m_sliderDragCtrl->value) {
            m_sliderDragCtrl->value = newVal;
            if (m_sliderDragCtrl->valueCb) m_sliderDragCtrl->valueCb(this, m_id, newVal);
            Invalidate();
        }
        return;
    }

    int idx = HitTestItem(y);
    if (idx != m_hoverIndex) {
        if (m_hoverIndex >= 0 && m_hoverIndex < (int)m_items.size()) {
            for (auto& c : m_items[m_hoverIndex].ctrls) c.state = STATE_NORMAL;
        }
        m_hoverIndex = idx;
        Invalidate();
    }

    if (idx >= 0 && idx < (int)m_items.size()) {
        SuperListItemCtrl* ctrl = HitTestItemCtrl(idx, x, y);
        for (auto& c : m_items[idx].ctrls) {
            c.state = (&c == ctrl) ? STATE_HOVER : STATE_NORMAL;
        }
        Invalidate();
    }
}

void GpmSuperListBox::OnLButtonDown(int x, int y) {
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
    if (idx >= 0 && idx < (int)m_items.size()) {
        SuperListItemCtrl* ctrl = HitTestItemCtrl(idx, x, y);
        if (ctrl) {
            ctrl->state = STATE_DOWN;
            // Handle slider dragging
            if (ctrl->type == SLICT_SLIDER) {
                m_sliderDragging = true;
                m_sliderDragCtrl = ctrl;
                if (m_hWnd) ::SetCapture(m_hWnd);
                // Set initial value
                float trackL = (float)(m_x + ctrl->localX) + ctrl->height * 0.3f;
                float trackR = (float)(m_x + ctrl->localX + ctrl->width) - ctrl->height * 0.3f;
                float ratio = ((float)x - trackL) / (trackR - trackL);
                ratio = (std::max)(0.0f, (std::min)(1.0f, ratio));
                ctrl->value = ctrl->minVal + (int)(ratio * (ctrl->maxVal - ctrl->minVal) + 0.5f);
                if (ctrl->valueCb) ctrl->valueCb(this, m_id, ctrl->value);
            }
            Invalidate();
            return;
        }

        if (idx != m_selIndex) {
            m_selIndex = idx;
            Invalidate();
            if (m_selectCb) m_selectCb(this, m_id, idx, m_items[idx].text);
        }
    }
}

void GpmSuperListBox::OnLButtonUp(int x, int y) {
    if (m_scrollDragging) {
        m_scrollDragging = false;
        if (m_hWnd) ::ReleaseCapture();
        return;
    }
    if (m_sliderDragging) {
        m_sliderDragging = false;
        m_sliderDragCtrl = nullptr;
        if (m_hWnd) ::ReleaseCapture();
        return;
    }
    if (!m_enabled) return;

    int idx = HitTestItem(y);
    if (idx >= 0 && idx < (int)m_items.size()) {
        SuperListItemCtrl* ctrl = HitTestItemCtrl(idx, x, y);
        if (ctrl && ctrl->state == STATE_DOWN) {
            switch (ctrl->type) {
            case SLICT_BUTTON:
                if (ctrl->clickCb) ctrl->clickCb(this, m_id);
                break;
            case SLICT_CHECKBOX:
                ctrl->value = ctrl->value ? 0 : 1;
                if (ctrl->clickCb) ctrl->clickCb(this, m_id);
                break;
            case SLICT_COMBOBOX:
                // Cycle through items
                if (!ctrl->comboItems.empty()) {
                    ctrl->value = (ctrl->value + 1) % (int)ctrl->comboItems.size();
                    if (ctrl->selectCb) ctrl->selectCb(this, m_id, ctrl->value,
                        ctrl->comboItems[ctrl->value]);
                }
                break;
            default: break;
            }
            ctrl->state = STATE_HOVER;
            Invalidate();
        }
    }
}

void GpmSuperListBox::OnMouseLeave() {
    // 不重置拖拽状态！SetCapture 保证在窗口外松开时能收到 LButtonUp
    // 仅重置悬停视觉效�?
    if (m_hoverIndex >= 0 && m_hoverIndex < (int)m_items.size()) {
        for (auto& c : m_items[m_hoverIndex].ctrls) c.state = STATE_NORMAL;
    }
    m_hoverIndex = -1;
    Invalidate();
}

void GpmSuperListBox::OnMouseWheel(int x, int y, int delta) {
    int step = ExDPI::Scale(40);
    SetScrollOffset(m_scrollOffset - (delta > 0 ? step : -step));
}

} // namespace gpm_ui

#endif // GPMUI_ENABLE_SUPERLISTBOX
