/*
 * GpmDataGrid.cpp - 数据表格控件 (多列表头 + 嵌入控件)
 * 每个单元格可嵌入: 按钮/标签/进度�?滑块/选择�?组合�?
 */
#include "../core/gpm_ui.h"
#include <cstdio>

#ifdef GPMUI_ENABLE_DATAGRID

namespace gpm_ui {

GpmDataGrid::GpmDataGrid() 
    : m_selRow(-1), m_hoverRow(-1), m_hoverCol(-1),
      m_scrollOffsetX(0), m_scrollOffsetY(0),
      m_defaultRowHeight(0), m_headerHeight(0), m_cornerRadius(0),
      m_vScrollDragging(false), m_hScrollDragging(false),
      m_scrollDragStartY(0), m_scrollDragStartOffY(0),
      m_scrollDragStartX(0), m_scrollDragStartOffX(0),
      m_sliderDragging(false), m_sliderDragCtrl(nullptr),
      m_sliderDragRow(0), m_sliderDragCol(0),
      m_selectCb(nullptr),
      m_vScrollBarW(8), m_hScrollBarH(8)
{ ApplyTheme(); }
GpmDataGrid::~GpmDataGrid() {}

void GpmDataGrid::ApplyTheme() {
    auto& t = Theme();
    m_gridBg = t.listBg;
    m_headerBg = RGB(48, 48, 48);
    m_headerFg = t.fgPrimary;
    m_itemBg = t.listItemBg;
    m_itemHover = t.listItemHover;
    m_itemSelected = t.listItemSelected;
    m_itemText = t.listItemText;
    m_gridBorder = t.listBorder;
    m_scrollbarBg = t.listScrollbar;
    m_scrollThumb = t.listScrollThumb;
    m_style.cornerRadius = ExDPI::Scale(4);
    m_cornerRadius = m_style.cornerRadius;
}

void GpmDataGrid::Create(GpmWindow* parent, int x, int y, int w, int h, int id) {
    m_parentWnd = parent;
    m_hWnd = parent ? parent->GetWindowHandle() : NULL;
    m_x = ExDPI::Scale(x); m_y = ExDPI::Scale(y);
    m_width = ExDPI::Scale(w); m_height = ExDPI::Scale(h);
    m_id = id;
    m_defaultRowHeight = ExDPI::Scale(32);
    m_headerHeight = ExDPI::Scale(28);
    if (parent) parent->AddControl(this);
}

// ---- 列管�?----
int GpmDataGrid::AddColumn(const std::wstring& header, int width) {
    GpmDataGridColumn col;
    col.header = header;
    col.width = ExDPI::Scale(width);
    m_columns.push_back(col);
    Invalidate();
    return (int)m_columns.size() - 1;
}

void GpmDataGrid::ClearColumns() { m_columns.clear(); ClearRows(); Invalidate(); }

GpmDataGridColumn* GpmDataGrid::GetColumn(int index) {
    if (index >= 0 && index < (int)m_columns.size()) return &m_columns[index];
    return nullptr;
}

void GpmDataGrid::SetColumnWidth(int index, int width) {
    if (index >= 0 && index < (int)m_columns.size()) {
        m_columns[index].width = ExDPI::Scale(width);
        Invalidate();
    }
}

// ---- 行管�?----
int GpmDataGrid::AddRow(const std::vector<std::wstring>& cellTexts, int height) {
    GpmDataGridRow row;
    row.cellTexts = cellTexts;
    while ((int)row.cellTexts.size() < (int)m_columns.size())
        row.cellTexts.push_back(L"");
    row.cellCtrls.resize(m_columns.size());
    row.height = height > 0 ? ExDPI::Scale(height) : m_defaultRowHeight;
    row.textColor = m_itemText;
    m_rows.push_back(row);
    Invalidate();
    return (int)m_rows.size() - 1;
}

void GpmDataGrid::RemoveRow(int index) {
    if (index >= 0 && index < (int)m_rows.size()) {
        m_rows.erase(m_rows.begin() + index);
        if (m_selRow >= (int)m_rows.size()) m_selRow = -1;
        Invalidate();
    }
}

void GpmDataGrid::ClearRows() {
    m_rows.clear(); m_selRow = -1; m_hoverRow = -1; m_scrollOffsetX = 0; m_scrollOffsetY = 0;
    Invalidate();
}

GpmDataGridRow* GpmDataGrid::GetRow(int index) {
    if (index >= 0 && index < (int)m_rows.size()) return &m_rows[index];
    return nullptr;
}

void GpmDataGrid::SetCellText(int row, int col, const std::wstring& text) {
    if (row >= 0 && row < (int)m_rows.size() && col >= 0 && col < (int)m_columns.size()) {
        m_rows[row].cellTexts[col] = text;
        Invalidate();
    }
}

void GpmDataGrid::SetRowTextColor(int row, COLORREF color) {
    if (row >= 0 && row < (int)m_rows.size()) { m_rows[row].textColor = color; Invalidate(); }
}

void GpmDataGrid::SetRowBgColor(int row, COLORREF color) {
    if (row >= 0 && row < (int)m_rows.size()) { m_rows[row].bgColor = color; Invalidate(); }
}

void GpmDataGrid::SetRowHeight(int row, int height) {
    if (row >= 0 && row < (int)m_rows.size()) { m_rows[row].height = ExDPI::Scale(height); Invalidate(); }
}

// ---- 单元格嵌入控�?----
void GpmDataGrid::AddCellButton(int row, int col, int localX, int localY, int w, int h,
                                const std::wstring& text, ClickCallback cb) {
    if (row < 0 || row >= (int)m_rows.size() || col < 0 || col >= (int)m_columns.size()) return;
    auto& t = Theme();
    GpmDataGridCellCtrl ctrl;
    ctrl.type = SLICT_BUTTON;
    ctrl.localX = ExDPI::Scale(localX); ctrl.localY = ExDPI::Scale(localY);
    ctrl.width = ExDPI::Scale(w); ctrl.height = ExDPI::Scale(h);
    ctrl.text = text; ctrl.clickCb = cb;
    ctrl.bkNormal = t.btnBg; ctrl.bkHover = t.btnBgHover; ctrl.bkDown = t.btnBgDown;
    ctrl.fgNormal = t.btnFg; ctrl.fgHover = t.btnFg; ctrl.fgDown = t.btnFg;
    ctrl.cornerRadius = ExDPI::Scale(4);
    m_rows[row].cellCtrls[col].push_back(ctrl);
    Invalidate();
}

void GpmDataGrid::AddCellLabel(int row, int col, int localX, int localY, int w, int h,
                               const std::wstring& text, COLORREF color) {
    if (row < 0 || row >= (int)m_rows.size() || col < 0 || col >= (int)m_columns.size()) return;
    GpmDataGridCellCtrl ctrl;
    ctrl.type = SLICT_LABEL;
    ctrl.localX = ExDPI::Scale(localX); ctrl.localY = ExDPI::Scale(localY);
    ctrl.width = ExDPI::Scale(w); ctrl.height = ExDPI::Scale(h);
    ctrl.text = text; ctrl.fgNormal = color ? color : m_itemText;
    ctrl.fgHover = ctrl.fgNormal; ctrl.fgDown = ctrl.fgNormal;
    m_rows[row].cellCtrls[col].push_back(ctrl);
    Invalidate();
}

void GpmDataGrid::AddCellProgressBar(int row, int col, int localX, int localY, int w, int h,
                                     int value, int minVal, int maxVal) {
    if (row < 0 || row >= (int)m_rows.size() || col < 0 || col >= (int)m_columns.size()) return;
    auto& t = Theme();
    GpmDataGridCellCtrl ctrl;
    ctrl.type = SLICT_PROGRESSBAR;
    ctrl.localX = ExDPI::Scale(localX); ctrl.localY = ExDPI::Scale(localY);
    ctrl.width = ExDPI::Scale(w); ctrl.height = ExDPI::Scale(h);
    ctrl.value = value; ctrl.minVal = minVal; ctrl.maxVal = maxVal;
    ctrl.bkNormal = t.progressBg; ctrl.fgNormal = t.progressFill;
    ctrl.cornerRadius = ExDPI::Scale(3);
    m_rows[row].cellCtrls[col].push_back(ctrl);
    Invalidate();
}

void GpmDataGrid::AddCellSlider(int row, int col, int localX, int localY, int w, int h,
                                int value, int minVal, int maxVal, ValueCallback cb) {
    if (row < 0 || row >= (int)m_rows.size() || col < 0 || col >= (int)m_columns.size()) return;
    auto& t = Theme();
    GpmDataGridCellCtrl ctrl;
    ctrl.type = SLICT_SLIDER;
    ctrl.localX = ExDPI::Scale(localX); ctrl.localY = ExDPI::Scale(localY);
    ctrl.width = ExDPI::Scale(w); ctrl.height = ExDPI::Scale(h);
    ctrl.value = value; ctrl.minVal = minVal; ctrl.maxVal = maxVal;
    ctrl.bkNormal = t.sliderTrack; ctrl.fgNormal = t.sliderFill;
    ctrl.valueCb = cb;
    m_rows[row].cellCtrls[col].push_back(ctrl);
    Invalidate();
}

void GpmDataGrid::AddCellCheckBox(int row, int col, int localX, int localY, int w, int h,
                                  const std::wstring& text, bool checked, ClickCallback cb) {
    if (row < 0 || row >= (int)m_rows.size() || col < 0 || col >= (int)m_columns.size()) return;
    auto& t = Theme();
    GpmDataGridCellCtrl ctrl;
    ctrl.type = SLICT_CHECKBOX;
    ctrl.localX = ExDPI::Scale(localX); ctrl.localY = ExDPI::Scale(localY);
    ctrl.width = ExDPI::Scale(w); ctrl.height = ExDPI::Scale(h);
    ctrl.text = text; ctrl.value = checked ? 1 : 0; ctrl.clickCb = cb;
    ctrl.bkNormal = t.bgInput; ctrl.bkHover = t.bgHover; ctrl.bkDown = t.bgActive;
    ctrl.fgNormal = t.fgPrimary; ctrl.fgHover = t.fgPrimary; ctrl.fgDown = t.fgPrimary;
    m_rows[row].cellCtrls[col].push_back(ctrl);
    Invalidate();
}

void GpmDataGrid::AddCellComboBox(int row, int col, int localX, int localY, int w, int h,
                                  const std::vector<std::wstring>& items, int selIdx,
                                  SelectCallback cb) {
    if (row < 0 || row >= (int)m_rows.size() || col < 0 || col >= (int)m_columns.size()) return;
    auto& t = Theme();
    GpmDataGridCellCtrl ctrl;
    ctrl.type = SLICT_COMBOBOX;
    ctrl.localX = ExDPI::Scale(localX); ctrl.localY = ExDPI::Scale(localY);
    ctrl.width = ExDPI::Scale(w); ctrl.height = ExDPI::Scale(h);
    ctrl.comboItems = items; ctrl.value = selIdx; ctrl.selectCb = cb;
    ctrl.bkNormal = t.bgInput; ctrl.bkHover = t.bgHover; ctrl.bkDown = t.bgActive;
    ctrl.fgNormal = t.fgPrimary; ctrl.fgHover = t.fgPrimary; ctrl.fgDown = t.fgPrimary;
    ctrl.cornerRadius = ExDPI::Scale(4);
    m_rows[row].cellCtrls[col].push_back(ctrl);
    Invalidate();
}

// ---- 选中/滚动 ----
void GpmDataGrid::SetSelectedRow(int row, bool redraw) {
    m_selRow = row;
    if (redraw) Invalidate();
}

void GpmDataGrid::SetScrollOffset(int x, int y, bool redraw) {
    m_scrollOffsetX = (std::max)(0, (std::min)(GetMaxScrollX(), x));
    m_scrollOffsetY = (std::max)(0, (std::min)(GetMaxScrollY(), y));
    if (redraw) Invalidate();
}

void GpmDataGrid::SetGridColors(COLORREF bg, COLORREF headerBg, COLORREF headerFg,
                                COLORREF itemBg, COLORREF itemHover, COLORREF itemSelected,
                                COLORREF itemText, COLORREF border) {
    m_gridBg = bg; m_headerBg = headerBg; m_headerFg = headerFg; m_itemBg = itemBg;
    m_itemHover = itemHover; m_itemSelected = itemSelected; m_itemText = itemText; m_gridBorder = border;
}

void GpmDataGrid::SetScrollbarColors(COLORREF track, COLORREF thumb) {
    m_scrollbarBg = track; m_scrollThumb = thumb;
}

// ---- 内部计算 ----
int GpmDataGrid::GetTotalWidth() const {
    int total = 0;
    for (auto& c : m_columns) total += c.width;
    return total;
}

int GpmDataGrid::GetTotalHeight() const {
    int total = 0;
    for (auto& r : m_rows) total += r.height;
    return total;
}

int GpmDataGrid::GetMaxScrollX() const {
    int diff = GetTotalWidth() - GetVisibleWidth();
    return diff > 0 ? diff : 0;
}

int GpmDataGrid::GetMaxScrollY() const {
    int diff = GetTotalHeight() - GetVisibleHeight();
    return diff > 0 ? diff : 0;
}

int GpmDataGrid::HitTestRow(int y) const {
    int iy = m_y + m_headerHeight - m_scrollOffsetY;
    for (int i = 0; i < (int)m_rows.size(); i++) {
        int top = iy, bot = iy + m_rows[i].height;
        if (y >= top && y < bot) return i;
        iy = bot;
    }
    return -1;
}

int GpmDataGrid::HitTestCol(int x) const {
    int ix = m_x - m_scrollOffsetX;
    for (int i = 0; i < (int)m_columns.size(); i++) {
        int left = ix, right = ix + m_columns[i].width;
        if (x >= left && x < right) return i;
        ix = right;
    }
    return -1;
}

bool GpmDataGrid::HitTestVScrollbar(int x, int y) const {
    int sbX = m_x + m_width - m_vScrollBarW;
    int sbTop = m_y + m_headerHeight;
    int sbBot = m_y + m_height - m_hScrollBarH;
    return x >= sbX && x < m_x + m_width && y >= sbTop && y < sbBot;
}

bool GpmDataGrid::HitTestHScrollbar(int x, int y) const {
    int sbY = m_y + m_height - m_hScrollBarH;
    return x >= m_x && x < m_x + m_width - m_vScrollBarW && y >= sbY && y < m_y + m_height;
}

void GpmDataGrid::GetVScrollThumbRect(D2D1_RECT_F& outRect) const {
    int totalH = GetTotalHeight();
    int visH = GetVisibleHeight();
    if (totalH <= visH) return;
    float trackH = (float)visH;
    float thumbH = (std::max)(ExDPI::ScaleF(20.0f), trackH * visH / totalH);
    float thumbY = (float)(m_y + m_headerHeight) + (trackH - thumbH) * m_scrollOffsetY / (float)GetMaxScrollY();
    float sx = (float)(m_x + m_width - m_vScrollBarW);
    outRect = D2D1::RectF(sx, thumbY, sx + m_vScrollBarW, thumbY + thumbH);
}

void GpmDataGrid::GetHScrollThumbRect(D2D1_RECT_F& outRect) const {
    int totalW = GetTotalWidth();
    int visW = GetVisibleWidth();
    if (totalW <= visW) return;
    float trackW = (float)visW;
    float thumbW = (std::max)(ExDPI::ScaleF(20.0f), trackW * visW / totalW);
    float thumbX = (float)m_x + (trackW - thumbW) * m_scrollOffsetX / (float)GetMaxScrollX();
    float sy = (float)(m_y + m_height - m_hScrollBarH);
    outRect = D2D1::RectF(thumbX, sy, thumbX + thumbW, sy + m_hScrollBarH);
}

// 辅助函数: 获取单元格在客户区中的真�?X 坐标
int GpmDataGrid::GetCellClientX(int col) const {
    int cx = m_x - m_scrollOffsetX;
    for (int c = 0; c < col; c++) cx += m_columns[c].width;
    return cx;
}

// 辅助函数: 获取单元格在客户区中的真�?Y 坐标
int GpmDataGrid::GetCellClientY(int row) const {
    int cy = m_y + m_headerHeight - m_scrollOffsetY;
    for (int r = 0; r < row; r++) cy += m_rows[r].height;
    return cy;
}

GpmDataGridCellCtrl* GpmDataGrid::HitTestCellCtrl(int row, int col, int mx, int my) {
    if (row < 0 || row >= (int)m_rows.size() || col < 0 || col >= (int)m_columns.size()) return nullptr;
    auto& ctrls = m_rows[row].cellCtrls[col];
    int cellX = GetCellClientX(col);
    int cellY = GetCellClientY(row);
    for (auto& ctrl : ctrls) {
        int cx = cellX + ctrl.localX, cy = cellY + ctrl.localY;
        if (mx >= cx && mx < cx + ctrl.width && my >= cy && my < cy + ctrl.height)
            return &ctrl;
    }
    return nullptr;
}

// ---- 绘制嵌入控件 ----
void GpmDataGrid::DrawCellCtrl(ID2D1RenderTarget* rt, const GpmDataGridCellCtrl& ctrl,
                               float cellX, float cellY, float opacity) {
    float cx = cellX + ctrl.localX, cy = cellY + ctrl.localY;
    float cw = (float)ctrl.width, ch = (float)ctrl.height;
    COLORREF bkC = ctrl.bkNormal, fgC = ctrl.fgNormal;
    if (ctrl.state == STATE_HOVER) { bkC = ctrl.bkHover; fgC = ctrl.fgHover; }
    else if (ctrl.state == STATE_DOWN) { bkC = ctrl.bkDown; fgC = ctrl.fgDown; }

    switch (ctrl.type) {
    case SLICT_BUTTON: {
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(bkC, opacity), &br);
        if (br) { D2D1_ROUNDED_RECT rr = MakeRoundRect(cx, cy, cw, ch, (float)ctrl.cornerRadius); rt->FillRoundedRectangle(&rr, br); br->Release(); }
        COLORREF darkEdge = RGB((std::max)(0,(int)GetRValue(bkC)-20),(std::max)(0,(int)GetGValue(bkC)-20),(std::max)(0,(int)GetBValue(bkC)-20));
        ID2D1SolidColorBrush* dBr = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(darkEdge, opacity), &dBr);
        if (dBr) { D2D1_ROUNDED_RECT rr = MakeRoundRect(cx, cy, cw, ch, (float)ctrl.cornerRadius); rt->DrawRoundedRectangle(&rr, dBr, 1.0f); dBr->Release(); }
        IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, false, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (fmt) { ID2D1SolidColorBrush* tb = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(fgC, opacity), &tb); if (tb) { rt->DrawText(ctrl.text.c_str(), (UINT32)ctrl.text.length(), fmt, D2D1::RectF(cx, cy, cx+cw, cy+ch), tb); tb->Release(); } fmt->Release(); }
        break;
    }
    case SLICT_LABEL: {
        IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, false, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (fmt) { ID2D1SolidColorBrush* tb = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(fgC, opacity), &tb); if (tb) { rt->DrawText(ctrl.text.c_str(), (UINT32)ctrl.text.length(), fmt, D2D1::RectF(cx, cy, cx+cw, cy+ch), tb); tb->Release(); } fmt->Release(); }
        break;
    }
    case SLICT_PROGRESSBAR: {
        float range = (float)(ctrl.maxVal - ctrl.minVal);
        float ratio = range > 0 ? (float)(ctrl.value - ctrl.minVal) / range : 0;
        ID2D1SolidColorBrush* tr = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(ctrl.bkNormal, opacity), &tr);
        if (tr) { D2D1_ROUNDED_RECT rr = MakeRoundRect(cx, cy, cw, ch, (float)ctrl.cornerRadius); rt->FillRoundedRectangle(&rr, tr); tr->Release(); }
        if (ratio > 0) {
            ID2D1SolidColorBrush* fl = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(ctrl.fgNormal, opacity), &fl);
            if (fl) {
                float fillW = cw * ratio;
                if (fillW < cw) {
                    D2D1_ROUNDED_RECT rrClip = MakeRoundRect(cx, cy, cw, ch, (float)ctrl.cornerRadius);
                    ID2D1RoundedRectangleGeometry* geo = nullptr;
                    ExD2DFactory::GetFactory()->CreateRoundedRectangleGeometry(rrClip, &geo);
                    if (geo) { rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), geo), nullptr); rt->FillRectangle(D2D1::RectF(cx, cy, cx+fillW, cy+ch), fl); rt->PopLayer(); geo->Release(); }
                } else { D2D1_ROUNDED_RECT rr = MakeRoundRect(cx, cy, fillW, ch, (float)ctrl.cornerRadius); rt->FillRoundedRectangle(&rr, fl); }
                fl->Release();
            }
        }
        break;
    }
    case SLICT_SLIDER: {
        float range = (float)(ctrl.maxVal - ctrl.minVal);
        float ratio = range > 0 ? (float)(ctrl.value - ctrl.minVal) / range : 0;
        float trackH = 4.0f, trackY = cy + (ch - trackH) / 2;
        float thumbR = ch * 0.3f, trackL = cx + thumbR, trackR = cx + cw - thumbR;
        ID2D1SolidColorBrush* tr = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(ctrl.bkNormal, opacity), &tr);
        if (tr) { D2D1_ROUNDED_RECT rr = MakeRoundRect(trackL, trackY, trackR-trackL, trackH, 2.0f); rt->FillRoundedRectangle(&rr, tr); tr->Release(); }
        float thumbX = trackL + ratio * (trackR - trackL);
        ID2D1SolidColorBrush* fl = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(ctrl.fgNormal, opacity), &fl);
        if (fl) { if (thumbX - trackL > 0) { D2D1_ROUNDED_RECT rr = MakeRoundRect(trackL, trackY, thumbX-trackL, trackH, 2.0f); rt->FillRoundedRectangle(&rr, fl); } fl->Release(); }
        ID2D1SolidColorBrush* tb = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(Theme().sliderThumb, opacity), &tb);
        if (tb) { rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, cy+ch/2), thumbR, thumbR), tb); tb->Release(); }
        break;
    }
    case SLICT_COMBOBOX: {
        ID2D1SolidColorBrush* br = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(bkC, opacity), &br);
        if (br) { D2D1_ROUNDED_RECT rr = MakeRoundRect(cx, cy, cw, ch, (float)ctrl.cornerRadius); rt->FillRoundedRectangle(&rr, br); br->Release(); }
        COLORREF darkEdge = RGB((std::max)(0,(int)GetRValue(bkC)-15),(std::max)(0,(int)GetGValue(bkC)-15),(std::max)(0,(int)GetBValue(bkC)-15));
        ID2D1SolidColorBrush* dBr = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(darkEdge, opacity), &dBr);
        if (dBr) { D2D1_ROUNDED_RECT rr = MakeRoundRect(cx, cy, cw, ch, (float)ctrl.cornerRadius); rt->DrawRoundedRectangle(&rr, dBr, 1.0f); dBr->Release(); }
        std::wstring disp = (ctrl.value>=0 && ctrl.value<(int)ctrl.comboItems.size()) ? ctrl.comboItems[ctrl.value] : L"--";
        IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, false, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (fmt) { ID2D1SolidColorBrush* tb = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(fgC, opacity), &tb); if (tb) { D2D1_RECT_F trc = D2D1::RectF(cx+6, cy, cx+cw-ch, cy+ch); rt->DrawText(disp.c_str(), (UINT32)disp.length(), fmt, trc, tb); tb->Release(); } fmt->Release(); }
        ID2D1SolidColorBrush* ar = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(fgC, opacity), &ar);
        if (ar) { float acx=cx+cw-ch/2, acy=cy+ch/2, as=3.0f; ID2D1PathGeometry* geo=nullptr; ExD2DFactory::GetFactory()->CreatePathGeometry(&geo);
            if(geo){ID2D1GeometrySink* s=nullptr; geo->Open(&s); if(s){s->BeginFigure(D2D1::Point2F(acx-as,acy-as/2),D2D1_FIGURE_BEGIN_FILLED); s->AddLine(D2D1::Point2F(acx+as,acy-as/2)); s->AddLine(D2D1::Point2F(acx,acy+as/2)); s->EndFigure(D2D1_FIGURE_END_CLOSED); s->Close(); s->Release(); rt->FillGeometry(geo,ar);} geo->Release();} ar->Release(); }
        break;
    }
    case SLICT_CHECKBOX: {
        float boxSz = 14.0f, bx = cx+2, by = cy+(ch-boxSz)/2;
        bool checked = ctrl.value != 0;
        ID2D1SolidColorBrush* br = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(checked ? Theme().checkMark : bkC, opacity), &br);
        if (br) { D2D1_ROUNDED_RECT rr = MakeRoundRect(bx, by, boxSz, boxSz, 2.0f); rt->FillRoundedRectangle(&rr, br); br->Release(); }
        if (checked) { ID2D1SolidColorBrush* ck = nullptr; rt->CreateSolidColorBrush(D2D1::ColorF(1,1,1,opacity), &ck);
            if(ck){float ccx=bx+boxSz/2,ccy=by+boxSz/2,s=boxSz*0.25f; rt->DrawLine(D2D1::Point2F(ccx-s,ccy),D2D1::Point2F(ccx-s*0.2f,ccy+s*0.7f),ck,1.5f);rt->DrawLine(D2D1::Point2F(ccx-s*0.2f,ccy+s*0.7f),D2D1::Point2F(ccx+s,ccy-s*0.6f),ck,1.5f);ck->Release();}}
        IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, false, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (fmt) { ID2D1SolidColorBrush* tb = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(fgC, opacity), &tb); if (tb) { rt->DrawText(ctrl.text.c_str(), (UINT32)ctrl.text.length(), fmt, D2D1::RectF(bx+boxSz+4, cy, cx+cw, cy+ch), tb); tb->Release(); } fmt->Release(); }
        break;
    }
    default: break;
    }
}

// ---- 绘制 ----
void GpmDataGrid::OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) {
    if (!m_visible || !rt) return;
    float x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
    float op = m_style.opacity;

    // 背景
    ID2D1SolidColorBrush* bgBr = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(m_gridBg, op), &bgBr);
    if (bgBr) { rt->FillRectangle(rc, bgBr); bgBr->Release(); }

    int sbW = m_vScrollBarW, sbH = m_hScrollBarH;
    bool needV = GetTotalHeight() > GetVisibleHeight();
    bool needH = GetTotalWidth() > GetVisibleWidth();
    float bodyW = w - (needV ? sbW : 0);
    float bodyH = h - m_headerHeight - (needH ? sbH : 0);
    float headerH = (float)m_headerHeight;

    // 裁剪区域
    D2D1_ROUNDED_RECT clipRR = MakeRoundRect(x+1, y+1, w-2, h-2, 2.0f);
    ID2D1RoundedRectangleGeometry* clipGeo = nullptr;
    ExD2DFactory::GetFactory()->CreateRoundedRectangleGeometry(clipRR, &clipGeo);
    if (!clipGeo) return;
    rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clipGeo), nullptr);

    // ---- 绘制表头 ----
    ID2D1SolidColorBrush* hdrBr = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(m_headerBg, op), &hdrBr);
    if (hdrBr) { rt->FillRectangle(D2D1::RectF(x, y, x+bodyW, y+headerH), hdrBr); hdrBr->Release(); }
    int hx = (int)x - m_scrollOffsetX;
    for (int i = 0; i < (int)m_columns.size(); i++) {
        int colW = m_columns[i].width;
        D2D1_RECT_F colRc = D2D1::RectF((float)hx, y, (float)(hx + colW), y + headerH);
        if (hx + colW > x && hx < x + bodyW) {
            // 分隔�?
            ID2D1SolidColorBrush* sepBr = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(RGB((std::max)(0,(int)GetRValue(m_headerBg)-20),(std::max)(0,(int)GetGValue(m_headerBg)-20),(std::max)(0,(int)GetBValue(m_headerBg)-20)),op),&sepBr);
            if (sepBr) { rt->DrawLine(D2D1::Point2F((float)(hx+colW-1), y), D2D1::Point2F((float)(hx+colW-1), y+headerH), sepBr, 1.0f); sepBr->Release(); }
            IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_columns[i].fontSize, m_columns[i].bold, m_columns[i].align, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            if (fmt) { ID2D1SolidColorBrush* tb = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(m_headerFg, op), &tb);
                if (tb) { D2D1_RECT_F tr = D2D1::RectF((float)(hx+4), y, (float)(hx+colW-4), y+headerH); rt->DrawText(m_columns[i].header.c_str(), (UINT32)m_columns[i].header.length(), fmt, tr, tb); tb->Release(); } fmt->Release(); }
        }
        hx += colW;
    }

    // ---- 绘制数据�?----
    float iy = y + headerH - m_scrollOffsetY;
    for (int r = 0; r < (int)m_rows.size(); r++) {
        auto& row = m_rows[r];
        float rowTop = iy, rowBot = iy + row.height;
        iy = rowBot;
        if (rowBot < y + headerH || rowTop > y + headerH + bodyH) continue;

        bool isSel = (r == m_selRow), isHov = (r == m_hoverRow);
        COLORREF rBg = row.bgColor ? row.bgColor : m_itemBg;
        if (isSel) rBg = m_itemSelected; else if (isHov) rBg = m_itemHover;
        ID2D1SolidColorBrush* rb = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(rBg, op), &rb);
        if (rb) { rt->FillRectangle(D2D1::RectF(x, rowTop, x+bodyW, rowBot), rb); rb->Release(); }

        // 选中标记
        if (isSel) { ID2D1SolidColorBrush* sm = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(Theme().fgAccent, op), &sm);
            if (sm) { rt->FillRectangle(D2D1::RectF(x+1, rowTop+2, x+3, rowBot-2), sm); sm->Release(); }}

        // 绘制各列单元�?
        int cx = (int)x - m_scrollOffsetX;
        for (int c = 0; c < (int)m_columns.size(); c++) {
            int colW = m_columns[c].width;
            if (cx + colW > x && cx < x + bodyW) {
                // 单元格文�?
                COLORREF txC = row.textColor ? row.textColor : m_itemText;
                if (c < (int)row.cellTexts.size() && !row.cellTexts[c].empty()) {
                    IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, false, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    if (fmt) { ID2D1SolidColorBrush* tb = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(txC, op), &tb);
                        if (tb) { float pad = ExDPI::ScaleF(6.0f) + (isSel?4.0f:0); rt->DrawText(row.cellTexts[c].c_str(), (UINT32)row.cellTexts[c].length(), fmt, D2D1::RectF((float)(cx+pad), rowTop, (float)(cx+colW-pad), rowBot), tb); tb->Release(); } fmt->Release(); }
                }
                // 嵌入控件
                if (c < (int)row.cellCtrls.size()) {
                    for (auto& ctrl : row.cellCtrls[c]) DrawCellCtrl(rt, ctrl, (float)cx, rowTop, op);
                }
                // 列网格线
                ID2D1SolidColorBrush* gBr = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(m_gridBorder, op*0.3f), &gBr);
                if (gBr) { rt->DrawLine(D2D1::Point2F((float)(cx+colW-1), rowTop), D2D1::Point2F((float)(cx+colW-1), rowBot), gBr, 0.5f); gBr->Release(); }
            }
            cx += colW;
        }
        // 行网格线
        ID2D1SolidColorBrush* gBr = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(m_gridBorder, op*0.3f), &gBr);
        if (gBr) { rt->DrawLine(D2D1::Point2F(x, rowBot-1), D2D1::Point2F(x+bodyW, rowBot-1), gBr, 0.5f); gBr->Release(); }
    }

    rt->PopLayer();
    clipGeo->Release();

    // ---- 垂直滚动�?----
    if (needV) {
        D2D1_RECT_F vTrack = D2D1::RectF(x+bodyW, y+headerH, x+w, y+headerH+bodyH);
        ID2D1SolidColorBrush* sbBr = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(m_scrollbarBg, op*0.5f), &sbBr);
        if (sbBr) { rt->FillRectangle(vTrack, sbBr); sbBr->Release(); }
        D2D1_RECT_F vThumb; GetVScrollThumbRect(vThumb);
        ID2D1SolidColorBrush* thBr = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(m_scrollThumb, op), &thBr);
        if (thBr) { D2D1_ROUNDED_RECT rr; rr.rect = vThumb; rr.radiusX = sbW/2.0f; rr.radiusY = sbW/2.0f; rt->FillRoundedRectangle(&rr, thBr); thBr->Release(); }
    }
    // ---- 水平滚动�?----
    if (needH) {
        D2D1_RECT_F hTrack = D2D1::RectF(x, y+headerH+bodyH, x+bodyW, y+h);
        ID2D1SolidColorBrush* sbBr = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(m_scrollbarBg, op*0.5f), &sbBr);
        if (sbBr) { rt->FillRectangle(hTrack, sbBr); sbBr->Release(); }
        D2D1_RECT_F hThumb; GetHScrollThumbRect(hThumb);
        ID2D1SolidColorBrush* thBr = nullptr; rt->CreateSolidColorBrush(ColorRefToD2D(m_scrollThumb, op), &thBr);
        if (thBr) { D2D1_ROUNDED_RECT rr; rr.rect = hThumb; rr.radiusX = sbH/2.0f; rr.radiusY = sbH/2.0f; rt->FillRoundedRectangle(&rr, thBr); thBr->Release(); }
    }
}

// ---- 鼠标事件 ----
void GpmDataGrid::OnMouseMove(int x, int y) {
    if (!m_enabled) return;
    POINT pt; ::GetCursorPos(&pt); if (m_hWnd) ::ScreenToClient(m_hWnd, &pt);
    int realY = pt.y;

    if (m_vScrollDragging) {
        int totalH = GetTotalHeight(), visH = GetVisibleHeight();
        if (totalH > visH) {
            int deltaY = realY - m_scrollDragStartY;
            float trackH = (float)visH, thumbH = (std::max)(ExDPI::ScaleF(20.0f), trackH*visH/totalH);
            float range = trackH - thumbH;
            if (range > 0) SetScrollOffset(m_scrollOffsetX, m_scrollDragStartOffY + (int)(deltaY*GetMaxScrollY()/range));
        } return;
    }
    if (m_hScrollDragging) {
        int totalW = GetTotalWidth(), visW = GetVisibleWidth();
        if (totalW > visW) {
            int deltaX = pt.x - m_scrollDragStartX;
            float trackW = (float)visW, thumbW = (std::max)(ExDPI::ScaleF(20.0f), trackW*visW/totalW);
            float range = trackW - thumbW;
            if (range > 0) SetScrollOffset(m_scrollDragStartOffX + (int)(deltaX*GetMaxScrollX()/range), m_scrollOffsetY);
        } return;
    }
    if (m_sliderDragging && m_sliderDragCtrl) {
        float cellX = (float)GetCellClientX(m_sliderDragCol);
        float trackL = cellX + m_sliderDragCtrl->localX + m_sliderDragCtrl->height*0.3f;
        float trackR = cellX + m_sliderDragCtrl->localX + m_sliderDragCtrl->width - m_sliderDragCtrl->height*0.3f;
        float ratio = ((float)pt.x - trackL) / (trackR - trackL);
        ratio = (std::max)(0.0f, (std::min)(1.0f, ratio));
        int newVal = m_sliderDragCtrl->minVal + (int)(ratio*(m_sliderDragCtrl->maxVal-m_sliderDragCtrl->minVal)+0.5f);
        if (newVal != m_sliderDragCtrl->value) { m_sliderDragCtrl->value = newVal; if (m_sliderDragCtrl->valueCb) m_sliderDragCtrl->valueCb(this, m_id, newVal); Invalidate(); }
        return;
    }

    int row = HitTestRow(y), col = HitTestCol(x);
    if (row != m_hoverRow || col != m_hoverCol) {
        if (m_hoverRow >= 0 && m_hoverRow < (int)m_rows.size() && m_hoverCol >= 0) {
            auto& c = m_rows[m_hoverRow].cellCtrls[m_hoverCol]; for (auto& cc : c) cc.state = STATE_NORMAL;
        }
        m_hoverRow = row; m_hoverCol = col; Invalidate();
    }
    if (row >= 0 && row < (int)m_rows.size() && col >= 0 && col < (int)m_columns.size()) {
        GpmDataGridCellCtrl* ctrl = HitTestCellCtrl(row, col, x, y);
        for (auto& c : m_rows[row].cellCtrls[col]) c.state = (&c == ctrl) ? STATE_HOVER : STATE_NORMAL;
        Invalidate();
    }
}

void GpmDataGrid::OnLButtonDown(int x, int y) {
    if (!m_enabled) return;

    if (HitTestVScrollbar(x, y)) {
        D2D1_RECT_F thumbRc; GetVScrollThumbRect(thumbRc);
        if (y >= thumbRc.top && y <= thumbRc.bottom) {
            m_vScrollDragging = true; m_scrollDragStartY = y; m_scrollDragStartOffY = m_scrollOffsetY;
            if (m_hWnd) ::SetCapture(m_hWnd); return;
        }
    }
    if (HitTestHScrollbar(x, y)) {
        D2D1_RECT_F thumbRc; GetHScrollThumbRect(thumbRc);
        if (x >= thumbRc.left && x <= thumbRc.right) {
            m_hScrollDragging = true; m_scrollDragStartX = x; m_scrollDragStartOffX = m_scrollOffsetX;
            if (m_hWnd) ::SetCapture(m_hWnd); return;
        }
    }

    int row = HitTestRow(y), col = HitTestCol(x);
    if (row >= 0 && row < (int)m_rows.size() && col >= 0 && col < (int)m_columns.size()) {
        // 清除旧的嵌入控件悬停状�?
        if (m_hoverRow >= 0 && m_hoverRow < (int)m_rows.size() && m_hoverCol >= 0 &&
            (m_hoverRow != row || m_hoverCol != col)) {
            auto& c = m_rows[m_hoverRow].cellCtrls[m_hoverCol];
            for (auto& cc : c) cc.state = STATE_NORMAL;
        }
        
        // 清除新的所在列的嵌入控件状态（先全部重置）
        for (auto& cc : m_rows[row].cellCtrls[col]) cc.state = STATE_NORMAL;
        
        // 设置选中�?
        m_selRow = row;
        m_hoverRow = row;
        m_hoverCol = col;
        if (m_selectCb) m_selectCb(this, m_id, row, L"");
        
        GpmDataGridCellCtrl* ctrl = HitTestCellCtrl(row, col, x, y);
        if (ctrl) {
            ctrl->state = STATE_DOWN;
            if (ctrl->type == SLICT_SLIDER) {
                m_sliderDragging = true; m_sliderDragCtrl = ctrl; m_sliderDragRow = row; m_sliderDragCol = col;
                if (m_hWnd) ::SetCapture(m_hWnd);
                float cellX = (float)GetCellClientX(col);
                float trackL = cellX + ctrl->localX + ctrl->height*0.3f;
                float trackR = cellX + ctrl->localX + ctrl->width - ctrl->height*0.3f;
                float ratio = ((float)x - trackL) / (trackR - trackL); ratio = (std::max)(0.0f, (std::min)(1.0f, ratio));
                ctrl->value = ctrl->minVal + (int)(ratio*(ctrl->maxVal-ctrl->minVal)+0.5f);
                if (ctrl->valueCb) ctrl->valueCb(this, m_id, ctrl->value);
            }
        }
        Invalidate();
    }
}

void GpmDataGrid::OnLButtonUp(int x, int y) {
    if (m_vScrollDragging) { m_vScrollDragging = false; if(m_hWnd)::ReleaseCapture(); return; }
    if (m_hScrollDragging) { m_hScrollDragging = false; if(m_hWnd)::ReleaseCapture(); return; }
    if (m_sliderDragging) { m_sliderDragging = false; m_sliderDragCtrl = nullptr; if(m_hWnd)::ReleaseCapture(); return; }
    if (!m_enabled) return;

    int row = HitTestRow(y), col = HitTestCol(x);
    if (row >= 0 && row < (int)m_rows.size() && col >= 0 && col < (int)m_columns.size()) {
        GpmDataGridCellCtrl* ctrl = HitTestCellCtrl(row, col, x, y);
        if (ctrl && ctrl->state == STATE_DOWN) {
            switch (ctrl->type) {
            case SLICT_BUTTON: if (ctrl->clickCb) ctrl->clickCb(this, m_id); break;
            case SLICT_CHECKBOX: ctrl->value = ctrl->value ? 0 : 1; if (ctrl->clickCb) ctrl->clickCb(this, m_id); break;
            case SLICT_COMBOBOX: if (!ctrl->comboItems.empty()) { ctrl->value = (ctrl->value + 1) % (int)ctrl->comboItems.size(); if (ctrl->selectCb) ctrl->selectCb(this, m_id, ctrl->value, ctrl->comboItems[ctrl->value]); } break;
            default: break;
            }
            ctrl->state = STATE_HOVER; Invalidate();
        }
    }
}

void GpmDataGrid::OnMouseLeave() {
    if (m_hoverRow >= 0 && m_hoverRow < (int)m_rows.size() && m_hoverCol >= 0) {
        auto& c = m_rows[m_hoverRow].cellCtrls[m_hoverCol]; for (auto& cc : c) cc.state = STATE_NORMAL;
    }
    m_hoverRow = -1; m_hoverCol = -1; Invalidate();
}

void GpmDataGrid::OnMouseWheel(int x, int y, int delta) {
    int step = ExDPI::Scale(40);
    SetScrollOffset(m_scrollOffsetX, m_scrollOffsetY - (delta > 0 ? step : -step));
}

} // namespace gpm_ui

#endif // GPMUI_ENABLE_DATAGRID
