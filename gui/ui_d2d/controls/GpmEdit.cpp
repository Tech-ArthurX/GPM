/*
 * GpmEdit.cpp - 编辑框控件 (D2D渲染 + IME输入法 + 插入符)
 * 支持单行/多行模式、水平/垂直滚动条、文本选中
 * ImGui风格：深色背景，焦点高亮边框，选中高亮
 */
#include "../core/gpm_ui.h"

#ifdef GPMUI_ENABLE_EDIT

namespace gpm_ui {

GpmEdit::GpmEdit() 
    : m_caretPos(0), m_hasFocus(false), m_caretVisible(false), m_showCaret(true),
      m_draggingSel(false), m_readOnly(false), m_passwordMode(false),
      m_passwordChar(L'*'), m_selStart(-1), m_selEnd(-1), 
      m_scrollOffsetX(0), m_scrollOffsetY(0),
      m_cornerRadius(0), m_multiline(false), m_lineHeight(0),
      m_totalTextWidth(0), m_totalTextHeight(0),
      m_vScrollDragging(false), m_hScrollDragging(false),
      m_scrollDragStartX(0), m_scrollDragStartY(0),
      m_scrollDragStartOffX(0), m_scrollDragStartOffY(0),
      m_hIMC(nullptr), m_textCb(nullptr)
{ 
    m_style.ApplyTheme_Edit(); 
    m_cornerRadius = m_style.cornerRadius;
    m_lineHeight = (int)ExDPI::ScaleF(18.0f);
}

GpmEdit::~GpmEdit() {
    DestroyCaret();
}

void GpmEdit::Create(GpmWindow* parent, int x, int y, int w, int h,
                    const std::wstring& text, int id) {
    m_parentWnd = parent;
    m_hWnd = parent ? parent->GetWindowHandle() : NULL;
    m_x = ExDPI::Scale(x); m_y = ExDPI::Scale(y);
    m_width = ExDPI::Scale(w); m_height = ExDPI::Scale(h);
    m_text = text; m_id = id;
    m_caretPos = (int)m_text.length();
    if (parent) parent->AddControl(this);
    RecalcTextExtent();
}

void GpmEdit::SetText(const std::wstring& text, bool redraw) {
    // 统一换行为 \r\n
    m_text = text;
    size_t pos = 0;
    while ((pos = m_text.find(L'\n', pos)) != std::wstring::npos) {
        if (pos == 0 || m_text[pos-1] != L'\r') {
            m_text.insert(pos, 1, L'\r');
            pos += 2;
        } else {
            pos++;
        }
    }
    m_caretPos = (int)m_text.length();
    RecalcTextExtent();
    EnsureCaretVisible();
    if (m_textCb) m_textCb(this, m_id, m_text);
    if (redraw) Invalidate();
}

void GpmEdit::AppendText(const std::wstring& text) {
    m_text += text;
    m_caretPos = (int)m_text.length();
    RecalcTextExtent();
    EnsureCaretVisible();
    if (m_textCb) m_textCb(this, m_id, m_text);
    Invalidate();
}

void GpmEdit::Clear() {
    m_text.clear();
    m_caretPos = 0;
    ClearSelection();
    m_scrollOffsetX = 0;
    m_scrollOffsetY = 0;
    m_totalTextWidth = 0;
    m_totalTextHeight = 0;
    if (m_textCb) m_textCb(this, m_id, m_text);
    Invalidate();
}

void GpmEdit::SetCaretPos(int pos, bool redraw) {
    m_caretPos = (std::max)(0, (std::min)((int)m_text.length(), pos));
    EnsureCaretVisible();
    if (redraw) Invalidate();
}

void GpmEdit::SetSelection(int start, int end) {
    m_selStart = (std::max)(0, (std::min)((int)m_text.length(), start));
    m_selEnd = (std::max)(0, (std::min)((int)m_text.length(), end));
    if (m_selStart == m_selEnd) { m_selStart = -1; m_selEnd = -1; }
    Invalidate();
}

void GpmEdit::ClearSelection() {
    m_selStart = -1; m_selEnd = -1;
    Invalidate();
}

// ============================================================
// 行拆分 & 文本尺寸计算
// ============================================================
std::vector<std::wstring> GpmEdit::SplitLines(const std::wstring& text) const {
    std::vector<std::wstring> lines;
    size_t start = 0;
    for (size_t i = 0; i < text.length(); i++) {
        if (text[i] == L'\n') {
            if (i > 0 && text[i-1] == L'\r')
                lines.push_back(text.substr(start, i - 1 - start));
            else
                lines.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    if (start <= text.length())
        lines.push_back(text.substr(start));
    if (lines.empty()) lines.push_back(L"");
    return lines;
}

void GpmEdit::RecalcTextExtent() {
    if (m_text.empty()) {
        m_lines.clear();
        m_lineWidths.clear();
        m_totalTextWidth = 0;
        m_totalTextHeight = m_multiline ? m_lineHeight : m_height;
        return;
    }
    m_lines = SplitLines(m_text);
    m_lineWidths.clear();
    m_totalTextWidth = 0;
    
    IDWriteFactory* dw = ExD2DFactory::GetDWriteFactory();
    float scaledSize = ExDPI::ScaleF(m_style.fontSize);
    ComPtr<IDWriteTextFormat> fmt;
    if (dw) {
        dw->CreateTextFormat(ExFont::GetGlobalFont().c_str(), nullptr,
            m_style.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_REGULAR,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            scaledSize, L"", &fmt);
    }
    
    for (auto& line : m_lines) {
        int lineW = 0;
        if (fmt && !line.empty()) {
            ComPtr<IDWriteTextLayout> layout;
            dw->CreateTextLayout(line.c_str(), (UINT32)line.length(),
                fmt.Get(), 10000.0f, ExDPI::ScaleF(20.0f), &layout);
            if (layout) {
                DWRITE_TEXT_METRICS metrics;
                layout->GetMetrics(&metrics);
                lineW = (int)(metrics.width + 0.5f);
            }
        }
        m_lineWidths.push_back(lineW);
        if (lineW > m_totalTextWidth) m_totalTextWidth = lineW;
    }
    m_totalTextHeight = (int)m_lines.size() * m_lineHeight;
}

// ============================================================
// 坐标与字符位置转换
// ============================================================
int GpmEdit::LineFromChar(int pos) const {
    if (m_lines.empty()) return 0;
    if (m_multiline) {
        int idx = 0;
        for (size_t i = 0; i < m_lines.size(); i++) {
            int lineLen = (int)m_lines[i].length();
            int checkPos = idx + lineLen;
            if (i + 1 < m_lines.size()) {
                int newlineLen = 0;
                if (checkPos < (int)m_text.length() && m_text[checkPos] == L'\r')
                    newlineLen = 2;
                else if (checkPos < (int)m_text.length() && m_text[checkPos] == L'\n')
                    newlineLen = 1;
                
                if (pos <= idx + lineLen + newlineLen) {
                    if (pos > idx + lineLen) return (int)(i + 1);
                    return (int)i;
                }
                idx += lineLen + newlineLen;
            } else {
                if (pos <= idx + lineLen) return (int)i;
            }
        }
        return (int)m_lines.size() - 1;
    } else {
        return 0;
    }
}

int GpmEdit::CharPosFromLineCol(int line, int col) const {
    if (line < 0 || m_lines.empty()) return 0;
    if (line >= (int)m_lines.size()) line = (int)m_lines.size() - 1;
    int pos = 0;
    for (int i = 0; i < line; i++) {
        pos += (int)m_lines[i].length();
        if (pos < (int)m_text.length() && m_text[pos] == L'\r')
            pos += 2;
        else if (pos < (int)m_text.length() && m_text[pos] == L'\n')
            pos += 1;
    }
    return pos + col;
}

int GpmEdit::LineLength(int line) const {
    if (line >= 0 && line < (int)m_lines.size())
        return (int)m_lines[line].length();
    return 0;
}

// 计算前缀[0..pos)的宽度，正确处理所有字符（包括尾随空格）
// DWrite的GetMetrics会忽略尾随空格，所以我们在每次测量时都在文本末尾
// 追加一个非空格字符'x'作为"防修剪守卫"(guard)，然后减去'x'的宽度。
// 这样即使sub以空格结尾，guard字符也能阻止DWrite将空格trim掉。
int GpmEdit::XFromCharPos(int pos) const {
    std::wstring display = GetDisplayText();
    if (display.empty() || pos <= 0) return 0;

    std::wstring sub = display.substr(0, pos);
    if (sub.empty()) return 0;

    IDWriteFactory* dw = ExD2DFactory::GetDWriteFactory();
    if (!dw) return 0;

    float scaledSize = ExDPI::ScaleF(m_style.fontSize);
    ComPtr<IDWriteTextFormat> fmt;
    dw->CreateTextFormat(
        ExFont::GetGlobalFont().c_str(), nullptr,
        m_style.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        scaledSize, L"", &fmt);
    if (!fmt) return 0;

    // 先测量 guard 字符 'x' 的宽度（每次重新测量，避免不同字体大小的影响）
    ComPtr<IDWriteTextLayout> guardLayout;
    dw->CreateTextLayout(L"x", 1, fmt.Get(), 10000.0f, ExDPI::ScaleF(20.0f), &guardLayout);
    float guardW = ExDPI::ScaleF(6.0f);
    if (guardLayout) {
        DWRITE_TEXT_METRICS gm; guardLayout->GetMetrics(&gm); guardW = gm.width;
    }
    
    // 测量(sub + "x")，减去'x'宽度，得到sub精确宽度（包括尾随空格）
    std::wstring mText = sub + L"x";
    ComPtr<IDWriteTextLayout> textLayout;
    dw->CreateTextLayout(mText.c_str(), (UINT32)mText.length(),
        fmt.Get(), 10000.0f, ExDPI::ScaleF(20.0f), &textLayout);
    if (!textLayout) return 0;

    DWRITE_TEXT_METRICS metrics;
    textLayout->GetMetrics(&metrics);
    float totalW = metrics.width - guardW;
    if (totalW < 0) totalW = 0;

    return (int)(totalW + 0.5f);
}

// 计算某一行中给定列号（0-based）的X位置
// 使用"guard字符'x'"技巧防止DWrite忽略尾随空格
int GpmEdit::XFromCol(int line, int col) const {
    if (line < 0 || line >= (int)m_lines.size()) return 0;
    auto& text = m_lines[line];
    if (text.empty()) return 0;
    if (col <= 0) return 0;

    std::wstring sub = text.substr(0, col);
    if (sub.empty()) return 0;

    IDWriteFactory* dw = ExD2DFactory::GetDWriteFactory();
    if (!dw) return 0;

    float scaledSize = ExDPI::ScaleF(m_style.fontSize);
    ComPtr<IDWriteTextFormat> fmt;
    dw->CreateTextFormat(
        ExFont::GetGlobalFont().c_str(), nullptr,
        m_style.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        scaledSize, L"", &fmt);
    if (!fmt) return 0;

    // 先测量 guard 字符 'x' 的宽度
    ComPtr<IDWriteTextLayout> guardLayout;
    dw->CreateTextLayout(L"x", 1, fmt.Get(), 10000.0f, ExDPI::ScaleF(20.0f), &guardLayout);
    float guardW = ExDPI::ScaleF(6.0f);
    if (guardLayout) {
        DWRITE_TEXT_METRICS gm; guardLayout->GetMetrics(&gm); guardW = gm.width;
    }
    
    // 测量(sub + "x")，减去'x'宽度，得到sub精确宽度（包括尾随空格）
    std::wstring mText = sub + L"x";
    ComPtr<IDWriteTextLayout> textLayout;
    dw->CreateTextLayout(mText.c_str(), (UINT32)mText.length(),
        fmt.Get(), 10000.0f, ExDPI::ScaleF(20.0f), &textLayout);
    if (!textLayout) return 0;

    DWRITE_TEXT_METRICS metrics;
    textLayout->GetMetrics(&metrics);
    float totalW = metrics.width - guardW;
    if (totalW < 0) totalW = 0;

    return (int)(totalW + 0.5f);
}

// 从整段文本的X位置计算字符位置（单行模式用）
// 使用二分查找 + HitTestTextPosition，能正确处理空格
int GpmEdit::CharPosFromX(int x) const {
    float pad = ExDPI::ScaleF(6.0f);
    float localX = (float)(x - m_x) + m_scrollOffsetX - pad;
    if (localX <= 0) return 0;

    std::wstring display = GetDisplayText();
    if (display.empty()) return 0;

    int len = (int)display.length();
    int lo = 0, hi = len;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        int mx = XFromCharPos(mid);
        if (mx <= localX) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return lo;
}

// 从某一行中的X位置计算列号
// 使用二分查找 + XFromCol（基于HitTestTextPosition），能正确处理空格
int GpmEdit::ColFromX(int line, int x) const {
    if (line < 0 || line >= (int)m_lines.size()) return 0;
    auto& text = m_lines[line];
    if (text.empty()) return 0;
    
    float pad = ExDPI::ScaleF(6.0f);
    float localX = (float)(x - m_x) + m_scrollOffsetX - pad;
    if (localX <= 0) return 0;
    
    int len = (int)text.length();
    int lo = 0, hi = len;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        int mx = XFromCol(line, mid);
        if (mx <= localX) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return lo;
}

int GpmEdit::YFromLine(int line) const {
    return line * m_lineHeight;
}

int GpmEdit::LineFromY(int y) const {
    int ly = y - m_y + m_scrollOffsetY;
    if (ly <= 0) return 0;
    int line = ly / m_lineHeight;
    if (!m_lines.empty() && line >= (int)m_lines.size()) line = (int)m_lines.size() - 1;
    return line;
}

// ============================================================
// 复制/粘贴/剪切
// ============================================================
void GpmEdit::CopyToClipboard() {
    if (m_selStart < 0 || m_selEnd < 0) return;
    int start = (std::min)(m_selStart, m_selEnd);
    int end = (std::max)(m_selStart, m_selEnd);
    std::wstring sel = m_text.substr(start, end - start);
    
    if (::OpenClipboard(m_hWnd)) {
        ::EmptyClipboard();
        size_t size = (sel.length() + 1) * sizeof(wchar_t);
        HGLOBAL hGlobal = ::GlobalAlloc(GMEM_MOVEABLE, size);
        if (hGlobal) {
            wchar_t* pDest = (wchar_t*)::GlobalLock(hGlobal);
            wcscpy_s(pDest, sel.length() + 1, sel.c_str());
            ::GlobalUnlock(hGlobal);
            ::SetClipboardData(CF_UNICODETEXT, hGlobal);
        }
        ::CloseClipboard();
    }
}

void GpmEdit::PasteFromClipboard() {
    if (m_readOnly) return;
    if (::OpenClipboard(m_hWnd)) {
        HANDLE hData = ::GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            wchar_t* pText = (wchar_t*)::GlobalLock(hData);
            if (pText) {
                if (m_selStart >= 0) {
                    int start = (std::min)(m_selStart, m_selEnd);
                    int end = (std::max)(m_selStart, m_selEnd);
                    m_text.erase(start, end - start);
                    m_caretPos = start;
                    ClearSelection();
                }
                std::wstring paste(pText);
                // 单行模式：移除所有换行符
                if (!m_multiline) {
                    size_t pos = 0;
                    while ((pos = paste.find_first_of(L"\r\n", pos)) != std::wstring::npos) {
                        paste.erase(pos, 1);
                    }
                }
                m_text.insert(m_caretPos, paste);
                m_caretPos += (int)paste.length();
                RecalcTextExtent();
                EnsureCaretVisible();
                if (m_textCb) m_textCb(this, m_id, m_text);
                Invalidate();
                ::GlobalUnlock(hData);
            }
        }
        ::CloseClipboard();
    }
}

void GpmEdit::CutToClipboard() {
    CopyToClipboard();
    if (!m_readOnly && m_selStart >= 0) {
        int start = (std::min)(m_selStart, m_selEnd);
        int end = (std::max)(m_selStart, m_selEnd);
        m_text.erase(start, end - start);
        m_caretPos = start;
        ClearSelection();
        RecalcTextExtent();
        EnsureCaretVisible();
        if (m_textCb) m_textCb(this, m_id, m_text);
        Invalidate();
    }
}

void GpmEdit::ApplyTheme() {
    m_style.ApplyTheme_Edit();
    m_cornerRadius = m_style.cornerRadius;
}

void GpmEdit::OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) {
    if (!m_visible || !rt) return;

    auto& t = Theme();
    float x = rc.left, y = rc.top;
    float w = rc.right - rc.left, h = rc.bottom - rc.top;

    COLORREF bgC = m_style.bgColors.Get(m_state);
    ID2D1SolidColorBrush* bgBr = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(bgC, m_style.opacity), &bgBr);
    if (bgBr) {
        if (m_cornerRadius > 0) {
            D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, w, h, (float)m_cornerRadius);
            rt->FillRoundedRectangle(&rr, bgBr);
        } else {
            rt->FillRectangle(D2D1::RectF(x, y, x + w, y + h), bgBr);
        }
        bgBr->Release();
    }

    COLORREF borderC = m_style.borderColor;
    if (m_hasFocus && m_style.borderFocusColor) borderC = m_style.borderFocusColor;
    else if (m_state == STATE_HOVER && m_style.borderHoverColor) borderC = m_style.borderHoverColor;

    if (borderC) {
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(borderC, m_style.opacity), &br);
        if (br) {
            if (m_cornerRadius > 0) {
                D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, w, h, (float)m_cornerRadius);
                rt->DrawRoundedRectangle(&rr, br, 1.0f);
            } else {
                rt->DrawRectangle(D2D1::RectF(x, y, x + w, y + h), br, 1.0f);
            }
            br->Release();
        }
    }

    float clipPad = ExDPI::ScaleF(2.0f);
    float sbW = ExDPI::ScaleF(10.0f);
    bool needHScroll = m_multiline && m_totalTextWidth > (w - clipPad * 2);
    bool needVScroll = m_multiline && m_totalTextHeight > (h - clipPad * 2);
    float textAreaW = w - clipPad * 2 - (needVScroll ? sbW : 0);
    float textAreaH = h - clipPad * 2 - (needHScroll ? sbW : 0);

    // 裁剪
    D2D1_RECT_F clipRect = D2D1::RectF(x + clipPad, y + clipPad, x + w - (needVScroll ? sbW : clipPad), y + h - (needHScroll ? sbW : clipPad));
    ComPtr<ID2D1PathGeometry> clipGeo;
    ExD2DFactory::GetFactory()->CreatePathGeometry(&clipGeo);
    if (clipGeo) {
        ComPtr<ID2D1GeometrySink> sink;
        clipGeo->Open(&sink);
        if (sink) {
            sink->BeginFigure(D2D1::Point2F(clipRect.left, clipRect.top), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(clipRect.right, clipRect.top));
            sink->AddLine(D2D1::Point2F(clipRect.right, clipRect.bottom));
            sink->AddLine(D2D1::Point2F(clipRect.left, clipRect.bottom));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            rt->PushLayer(D2D1::LayerParameters(clipRect, nullptr), nullptr);
        }
    }

    float pad = ExDPI::ScaleF(6.0f);
    std::wstring displayText = GetDisplayText();
    bool showPlaceholder = displayText.empty() && !m_hasFocus;
    std::wstring renderText = showPlaceholder ? m_placeholderText : displayText;

    if (!m_multiline) {
        // ===== 单行模式 =====
        float textX = x + pad - m_scrollOffsetX;
        
        if (m_hasFocus && m_selStart >= 0 && !showPlaceholder) {
            int selStart = (std::min)(m_selStart, m_selEnd);
            int selEnd = (std::max)(m_selStart, m_selEnd);
            float sx1 = textX + XFromCharPos(selStart);
            float sx2 = textX + XFromCharPos(selEnd);
            ID2D1SolidColorBrush* selBr = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(t.editSelection, m_style.opacity), &selBr);
            if (selBr) {
                rt->FillRectangle(D2D1::RectF(sx1, y + 1, sx2, y + h - 1), selBr);
                selBr->Release();
            }
        }

        if (!renderText.empty()) {
            COLORREF txC = showPlaceholder ? t.fgDisabled : m_style.textColors.Get(m_state);
            IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, m_style.bold,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            if (fmt) {
                ID2D1SolidColorBrush* tb = nullptr;
                rt->CreateSolidColorBrush(ColorRefToD2D(txC, m_style.opacity), &tb);
                if (tb) {
                    D2D1_RECT_F textRc = D2D1::RectF(textX, y, textX + (float)m_totalTextWidth, y + h);
                    rt->DrawText(renderText.c_str(), (UINT32)renderText.length(), fmt, textRc, tb);
                    tb->Release();
                }
                fmt->Release();
            }
        }

        if (m_hasFocus && m_showCaret && !showPlaceholder) {
            float caretX = textX + XFromCharPos(m_caretPos);
            ID2D1SolidColorBrush* caretBr = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(t.editCaret, m_style.opacity), &caretBr);
            if (caretBr) {
                float caretH = ExDPI::ScaleF(14.0f);
                float caretY = y + (h - caretH) / 2;
                rt->DrawLine(D2D1::Point2F(caretX, caretY), D2D1::Point2F(caretX, caretY + caretH), caretBr, 1.5f);
                caretBr->Release();
            }
        }
    } else {
        // ===== 多行模式 - 逐行绘制，每行独立计算X位置 =====
        int lineH = m_lineHeight;
        float baseX = x + pad - m_scrollOffsetX;
        float baseY = y + clipPad - m_scrollOffsetY;
        
        for (int i = 0; i < (int)m_lines.size(); i++) {
            float ly = baseY + i * lineH;
            if (ly + lineH < y + clipPad || ly > y + h - clipPad) continue;
            
            float lineX = baseX;
            float lineWidth = textAreaW;
            if ((int)m_lineWidths.size() > i) lineWidth = (float)m_lineWidths[i];
            
            // 选中背景（跨行正确）
            if (m_hasFocus && m_selStart >= 0 && !showPlaceholder) {
                int lineStart = CharPosFromLineCol(i, 0);
                int lineEnd = CharPosFromLineCol(i, (int)m_lines[i].length());
                int selS = (std::min)(m_selStart, m_selEnd);
                int selE = (std::max)(m_selStart, m_selEnd);
                int clipS = (std::max)(selS, lineStart);
                int clipE = (std::min)(selE, lineEnd);
                
                if (clipS < clipE) {
                    int colS = clipS - lineStart;
                    int colE = clipE - lineStart;
                    float sx1 = baseX + XFromCol(i, colS);
                    float sx2 = baseX + XFromCol(i, colE);
                    
                    ID2D1SolidColorBrush* selBr = nullptr;
                    rt->CreateSolidColorBrush(ColorRefToD2D(t.editSelection, m_style.opacity), &selBr);
                    if (selBr) {
                        rt->FillRectangle(D2D1::RectF(sx1, ly, sx2, ly + lineH), selBr);
                        selBr->Release();
                    }
                }
            }
            
            // 文本
            if (!m_lines[i].empty()) {
                COLORREF txC = m_style.textColors.Get(m_state);
                IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(m_style.fontSize, m_style.bold,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                if (fmt) {
                    ID2D1SolidColorBrush* tb = nullptr;
                    rt->CreateSolidColorBrush(ColorRefToD2D(txC, m_style.opacity), &tb);
                    if (tb) {
                        D2D1_RECT_F textRc = D2D1::RectF(lineX, ly, lineX + lineWidth, ly + lineH);
                        rt->DrawText(m_lines[i].c_str(), (UINT32)m_lines[i].length(), fmt, textRc, tb);
                        tb->Release();
                    }
                    fmt->Release();
                }
            }
        }

        // 插入符（基于行+列）
        if (m_hasFocus && m_showCaret && !showPlaceholder) {
            int line = LineFromChar(m_caretPos);
            int lineStart = CharPosFromLineCol(line, 0);
            int col = m_caretPos - lineStart;
            float caretX = baseX + XFromCol(line, col);
            float caretY = baseY + line * lineH;
            
            ID2D1SolidColorBrush* caretBr = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(t.editCaret, m_style.opacity), &caretBr);
            if (caretBr) {
                rt->DrawLine(D2D1::Point2F(caretX, caretY), D2D1::Point2F(caretX, caretY + lineH * 0.8f), caretBr, 1.5f);
                caretBr->Release();
            }
        }
    }

    if (clipGeo) {
        rt->PopLayer();
    }

    // ===== 滚动条（仅绘制，交互在OnLButtonDown/OnMouseMove） =====
    if (m_multiline) {
        if (needVScroll) {
            float vx = x + w - sbW;
            float vy = y + clipPad;
            float vh = textAreaH;
            ID2D1SolidColorBrush* sbBg = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(RGB(30, 30, 30), 0.5f), &sbBg);
            if (sbBg) {
                rt->FillRectangle(D2D1::RectF(vx, vy, vx + sbW, vy + vh), sbBg);
                sbBg->Release();
            }
            if (m_totalTextHeight > 0) {
                float thumbH = (std::max)(sbW * 2, vh * vh / m_totalTextHeight);
                float maxOff = m_totalTextHeight - (int)vh;
                float thumbY = vy + (maxOff > 0 ? (float)m_scrollOffsetY / maxOff * (vh - thumbH) : 0);
                ID2D1SolidColorBrush* thumbBr = nullptr;
                rt->CreateSolidColorBrush(ColorRefToD2D(RGB(120, 120, 120), 0.8f), &thumbBr);
                if (thumbBr) {
                    D2D1_ROUNDED_RECT rr = MakeRoundRect(vx + 1, thumbY, sbW - 2, thumbH, sbW/2);
                    rt->FillRoundedRectangle(&rr, thumbBr);
                    thumbBr->Release();
                }
            }
        }
        if (needHScroll) {
            float hx = x + clipPad;
            float hy = y + h - sbW;
            float hw = textAreaW;
            ID2D1SolidColorBrush* sbBg = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(RGB(30, 30, 30), 0.5f), &sbBg);
            if (sbBg) {
                rt->FillRectangle(D2D1::RectF(hx, hy, hx + hw, hy + sbW), sbBg);
                sbBg->Release();
            }
            if (m_totalTextWidth > 0) {
                float thumbW = (std::max)(sbW * 2, hw * hw / m_totalTextWidth);
                float maxOff = m_totalTextWidth - (int)hw;
                float thumbX = hx + (maxOff > 0 ? (float)m_scrollOffsetX / maxOff * (hw - thumbW) : 0);
                ID2D1SolidColorBrush* thumbBr = nullptr;
                rt->CreateSolidColorBrush(ColorRefToD2D(RGB(120, 120, 120), 0.8f), &thumbBr);
                if (thumbBr) {
                    D2D1_ROUNDED_RECT rr = MakeRoundRect(thumbX, hy + 1, thumbW, sbW - 2, sbW/2);
                    rt->FillRoundedRectangle(&rr, thumbBr);
                    thumbBr->Release();
                }
            }
        }
    }

    m_dirty = false;
}

// ============================================================
// 鼠标事件 - 包括滚动条拖拽 + 多行选中
// ============================================================
void GpmEdit::OnMouseMove(int x, int y) {
    if (!m_enabled) return;
    
    // 滚动条拖拽
    if (m_vScrollDragging) {
        float sbW = ExDPI::ScaleF(10.0f);
        float clipPad = ExDPI::ScaleF(2.0f);
        float vh = m_height - clipPad * 2;
        float thumbH = (std::max)(sbW * 2, vh * vh / m_totalTextHeight);
        float range = vh - thumbH;
        if (range > 0) {
            float maxOff = (float)(m_totalTextHeight - (int)(m_height - clipPad * 2));
            m_scrollOffsetY = (int)((float)(y - m_y - clipPad) / range * maxOff);
            if (m_scrollOffsetY < 0) m_scrollOffsetY = 0;
            if (m_scrollOffsetY > m_totalTextHeight - (int)vh) m_scrollOffsetY = m_totalTextHeight - (int)vh;
        }
        Invalidate();
        return;
    }
    if (m_hScrollDragging) {
        float sbW = ExDPI::ScaleF(10.0f);
        float clipPad = ExDPI::ScaleF(2.0f);
        float needV = (m_totalTextHeight > m_height - clipPad * 2) ? sbW : 0;
        float hw = m_width - clipPad * 2 - needV;
        float thumbW = (std::max)(sbW * 2, hw * hw / m_totalTextWidth);
        float range = hw - thumbW;
        if (range > 0) {
            float maxOff = (float)(m_totalTextWidth - (int)(m_width - clipPad * 2 - needV));
            m_scrollOffsetX = (int)((float)(x - m_x - clipPad) / range * maxOff);
            if (m_scrollOffsetX < 0) m_scrollOffsetX = 0;
            if (m_scrollOffsetX > m_totalTextWidth - (int)hw) m_scrollOffsetX = m_totalTextWidth - (int)hw;
        }
        Invalidate();
        return;
    }
    
    // 拖拽选择文字（不需要 m_hasFocus，因为鼠标按下时已设置焦点）
    if (m_draggingSel) {
        if (m_multiline) {
            if (y < m_y) {
                // 鼠标在控件上方：根据移出距离自动向上滚动
                int distOut = m_y - y;
                int scrollSpeed = (std::min)(distOut / 2 + 1, 10); // 速度 1~10 行/帧
                m_scrollOffsetY -= m_lineHeight * scrollSpeed;
                m_scrollOffsetY = (std::max)(0, m_scrollOffsetY);
                // 取控件顶部第一行可见行作为选中行
                int topVisibleLine = m_scrollOffsetY / m_lineHeight;
                m_caretPos = CharPosFromLineCol(topVisibleLine, 0);
            } else if (y >= m_y + m_height) {
                // 鼠标在控件下方：根据移出距离自动向下滚动
                int distOut = y - (m_y + m_height);
                int scrollSpeed = (std::min)(distOut / 2 + 1, 10); // 速度 1~10 行/帧
                m_scrollOffsetY += m_lineHeight * scrollSpeed;
                int maxOff = (std::max)(0, m_totalTextHeight - (int)(m_height));
                m_scrollOffsetY = (std::min)(maxOff, m_scrollOffsetY);
                // 取控件底部最后一行可见行作为选中行
                int bottomVisibleLine = (m_scrollOffsetY + m_height) / m_lineHeight;
                if (bottomVisibleLine >= (int)m_lines.size()) bottomVisibleLine = (int)m_lines.size() - 1;
                int col = ColFromX(bottomVisibleLine, x);
                int maxCol = LineLength(bottomVisibleLine);
                if (col > maxCol) col = maxCol;
                m_caretPos = CharPosFromLineCol(bottomVisibleLine, col);
            } else {
                int line = LineFromY(y);
                if (line >= (int)m_lines.size()) line = (int)m_lines.size() - 1;
                int col = ColFromX(line, x);
                int maxCol = LineLength(line);
                if (col > maxCol) col = maxCol;
                m_caretPos = CharPosFromLineCol(line, col);
            }
            m_selEnd = m_caretPos;
            UpdateCaretPos();
        } else {
            int pos = CharPosFromX(x);
            m_caretPos = pos;
            m_selEnd = pos;
            EnsureCaretVisible();
        }
        Invalidate();
    } else if (m_state != STATE_HOVER) {
        m_state = STATE_HOVER;
        Invalidate();
    }
}

void GpmEdit::OnLButtonDown(int x, int y) {
    if (!m_enabled) return;
    
    float clipPad = ExDPI::ScaleF(2.0f);
    float sbW = ExDPI::ScaleF(10.0f);
    bool needV = m_multiline && m_totalTextHeight > (m_height - clipPad * 2);
    bool needH = m_multiline && m_totalTextWidth > (m_width - clipPad * 2);
    
    if (needV && x >= m_x + m_width - sbW && x < m_x + m_width) {
        m_vScrollDragging = true;
        m_scrollDragStartY = y;
        m_scrollDragStartOffY = m_scrollOffsetY;
        ::SetCapture(m_hWnd);
        Invalidate();
        return;
    }
    if (needH && y >= m_y + m_height - sbW && y < m_y + m_height) {
        m_hScrollDragging = true;
        m_scrollDragStartX = x;
        m_scrollDragStartOffX = m_scrollOffsetX;
        ::SetCapture(m_hWnd);
        Invalidate();
        return;
    }
    
    m_state = STATE_DOWN;
    m_draggingSel = true;
    
    if (m_multiline) {
        int line = LineFromY(y);
        if (line >= (int)m_lines.size()) line = (int)m_lines.size() - 1;
        int col = ColFromX(line, x);
        int maxCol = LineLength(line);
        if (col > maxCol) col = maxCol;
        m_caretPos = CharPosFromLineCol(line, col);
    } else {
        m_caretPos = CharPosFromX(x);
    }
    m_selStart = m_caretPos;
    m_selEnd = m_caretPos;
    ::SetCapture(m_hWnd);
    Invalidate();
}

void GpmEdit::OnLButtonUp(int x, int y) {
    m_vScrollDragging = false;
    m_hScrollDragging = false;
    m_draggingSel = false;
    ::ReleaseCapture();
    m_state = STATE_HOVER;
    
    if (m_selStart == m_selEnd) {
        ClearSelection();
    }
    
    if (m_parentWnd) {
        m_parentWnd->SetFocusControl(m_id);
    }
}

void GpmEdit::OnLButtonDblClk(int x, int y) {
    m_selStart = 0;
    m_selEnd = (int)m_text.length();
    m_caretPos = m_selEnd;
    Invalidate();
}

void GpmEdit::OnMouseWheel(int x, int y, int delta) {
    if (!m_enabled || !m_multiline) return;
    if (m_totalTextHeight <= m_height) return;
    
    int maxOff = m_totalTextHeight - m_height;
    int scrollAmount = m_lineHeight * 3;
    m_scrollOffsetY -= (delta > 0) ? scrollAmount : -scrollAmount;
    m_scrollOffsetY = (std::max)(0, (std::min)(maxOff, m_scrollOffsetY));
    Invalidate();
}

void GpmEdit::OnSetFocus() {
    m_hasFocus = true;
    m_caretVisible = true;
    CreateCaret();
    Invalidate();
}

void GpmEdit::OnKillFocus() {
    m_hasFocus = false;
    m_draggingSel = false;
    m_vScrollDragging = false;
    m_hScrollDragging = false;
    m_caretVisible = false;
    ClearSelection();
    DestroyCaret();
    Invalidate();
}

void GpmEdit::OnKeyDown(UINT vk) {
    if (!m_hasFocus || m_readOnly) return;

    switch (vk) {
    case VK_LEFT:
        if (m_caretPos > 0) {
            m_caretPos--;
            if (m_caretPos > 0 && m_text[m_caretPos] == L'\n' && m_text[m_caretPos - 1] == L'\r') {
                m_caretPos--;
            }
        }
        if (!(GetKeyState(VK_SHIFT) & 0x8000)) ClearSelection();
        EnsureCaretVisible();
        Invalidate();
        break;

    case VK_RIGHT:
        if (m_caretPos < (int)m_text.length()) {
            if (m_caretPos + 1 < (int)m_text.length() && m_text[m_caretPos] == L'\r' && m_text[m_caretPos + 1] == L'\n') {
                m_caretPos += 2;
            } else {
                m_caretPos++;
            }
        }
        if (!(GetKeyState(VK_SHIFT) & 0x8000)) ClearSelection();
        EnsureCaretVisible();
        Invalidate();
        break;

    case VK_UP:
        if (m_multiline) {
            int line = LineFromChar(m_caretPos);
            if (line > 0) {
                int col = m_caretPos - CharPosFromLineCol(line, 0);
                m_caretPos = CharPosFromLineCol(line - 1, (std::min)(col, LineLength(line - 1)));
                if (!(GetKeyState(VK_SHIFT) & 0x8000)) ClearSelection();
            }
            EnsureCaretVisible();
            Invalidate();
        }
        break;

    case VK_DOWN:
        if (m_multiline) {
            int line = LineFromChar(m_caretPos);
            if (line + 1 < (int)m_lines.size()) {
                int col = m_caretPos - CharPosFromLineCol(line, 0);
                m_caretPos = CharPosFromLineCol(line + 1, (std::min)(col, LineLength(line + 1)));
                if (!(GetKeyState(VK_SHIFT) & 0x8000)) ClearSelection();
            }
            EnsureCaretVisible();
            Invalidate();
        }
        break;

    case VK_HOME:
        if (m_multiline && (GetKeyState(VK_CONTROL) & 0x8000)) {
            m_caretPos = 0;
        } else if (m_multiline) {
            int line = LineFromChar(m_caretPos);
            m_caretPos = CharPosFromLineCol(line, 0);
        } else {
            m_caretPos = 0;
        }
        ClearSelection();
        EnsureCaretVisible();
        Invalidate();
        break;

    case VK_END:
        if (m_multiline && (GetKeyState(VK_CONTROL) & 0x8000)) {
            m_caretPos = (int)m_text.length();
        } else if (m_multiline) {
            int line = LineFromChar(m_caretPos);
            m_caretPos = CharPosFromLineCol(line, LineLength(line));
        } else {
            m_caretPos = (int)m_text.length();
        }
        ClearSelection();
        EnsureCaretVisible();
        Invalidate();
        break;

    case VK_DELETE:
        if (m_selStart >= 0) {
            int start = (std::min)(m_selStart, m_selEnd);
            int end = (std::max)(m_selStart, m_selEnd);
            m_text.erase(start, end - start);
            m_caretPos = start;
            ClearSelection();
        } else if (m_caretPos < (int)m_text.length()) {
            if (m_caretPos + 1 < (int)m_text.length() && m_text[m_caretPos] == L'\r' && m_text[m_caretPos + 1] == L'\n') {
                m_text.erase(m_caretPos, 2);
            } else {
                m_text.erase(m_caretPos, 1);
            }
        }
        if (m_textCb) m_textCb(this, m_id, m_text);
        RecalcTextExtent();
        Invalidate();
        break;

    case VK_BACK:
        if (m_selStart >= 0) {
            int start = (std::min)(m_selStart, m_selEnd);
            int end = (std::max)(m_selStart, m_selEnd);
            m_text.erase(start, end - start);
            m_caretPos = start;
            ClearSelection();
        } else if (m_caretPos > 0) {
            if (m_caretPos >= 2 && m_text[m_caretPos - 2] == L'\r' && m_text[m_caretPos - 1] == L'\n') {
                m_text.erase(m_caretPos - 2, 2);
                m_caretPos -= 2;
            } else {
                m_text.erase(m_caretPos - 1, 1);
                m_caretPos--;
            }
        }
        if (m_textCb) m_textCb(this, m_id, m_text);
        RecalcTextExtent();
        Invalidate();
        break;

    case VK_RETURN:
        if (m_multiline) {
            if (m_caretPos > (int)m_text.length()) m_caretPos = (int)m_text.length();
            if (m_caretPos < 0) m_caretPos = 0;
            while (m_caretPos < (int)m_text.length() && (m_text[m_caretPos] == L'\r' || m_text[m_caretPos] == L'\n'))
                m_caretPos++;
            m_text.insert(m_caretPos, L"\r\n");
            m_caretPos += 2;
            if (m_textCb) m_textCb(this, m_id, m_text);
            RecalcTextExtent();
            EnsureCaretVisible();
            Invalidate();
        }
        break;

    case VK_PRIOR:
        if (m_multiline) {
            int line = LineFromChar(m_caretPos);
            int linesPerPage = m_height / m_lineHeight;
            line = (std::max)(0, line - linesPerPage);
            m_caretPos = CharPosFromLineCol(line, 0);
            EnsureCaretVisible();
            Invalidate();
        }
        break;

    case VK_NEXT:
        if (m_multiline) {
            int line = LineFromChar(m_caretPos);
            int linesPerPage = m_height / m_lineHeight;
            line = (std::min)((int)m_lines.size() - 1, line + linesPerPage);
            m_caretPos = CharPosFromLineCol(line, 0);
            EnsureCaretVisible();
            Invalidate();
        }
        break;

    // 空格键在 OnChar 中处理（与普通字符保持一致）
    case 'A':
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            m_selStart = 0;
            m_selEnd = (int)m_text.length();
            m_caretPos = m_selEnd;
            Invalidate();
        }
        break;

    case 'C':
        if (GetKeyState(VK_CONTROL) & 0x8000) { CopyToClipboard(); }
        break;
    case 'V':
        if (GetKeyState(VK_CONTROL) & 0x8000) { PasteFromClipboard(); }
        break;
    case 'X':
        if (GetKeyState(VK_CONTROL) & 0x8000) { CutToClipboard(); }
        break;
    }
}

void GpmEdit::OnChar(wchar_t ch) {
    if (!m_hasFocus || m_readOnly) return;
    if (ch == VK_RETURN) return;
    if (ch < 0x20) return;

    if (m_selStart >= 0) {
        int start = (std::min)(m_selStart, m_selEnd);
        int end = (std::max)(m_selStart, m_selEnd);
        m_text.erase(start, end - start);
        m_caretPos = start;
        ClearSelection();
    }

    if (m_text.empty()) {
        m_scrollOffsetX = 0;
        m_scrollOffsetY = 0;
    }
    
    m_text.insert(m_caretPos, 1, ch);
    m_caretPos++;
    RecalcTextExtent();
    if (m_textCb) m_textCb(this, m_id, m_text);
    EnsureCaretVisible();
    UpdateCaretPos();
    Invalidate();
}

// ============================================================
// 插入符 (Caret) 管理
// ============================================================
void GpmEdit::CreateCaret() {
    if (m_hWnd) {
        ::CreateCaret(m_hWnd, NULL, 1, (int)ExDPI::ScaleF(14.0f));
        UpdateCaretPos();
        ::ShowCaret(m_hWnd);
    }
}

void GpmEdit::DestroyCaret() {
    if (m_hWnd) {
        ::HideCaret(m_hWnd);
        ::DestroyCaret();
    }
}

void GpmEdit::ShowCaret(bool show) {
    m_showCaret = show;
    if (m_hasFocus && m_hWnd) {
        if (show) ::ShowCaret(m_hWnd);
        else ::HideCaret(m_hWnd);
    }
}

void GpmEdit::UpdateCaretPos() {
    if (m_hasFocus && m_hWnd) {
        float pad = ExDPI::ScaleF(6.0f);
        if (m_multiline) {
            int line = LineFromChar(m_caretPos);
            int lineStart = CharPosFromLineCol(line, 0);
            int col = m_caretPos - lineStart;
            float cx = pad + XFromCol(line, col) - m_scrollOffsetX;
            float cy = (float)(line * m_lineHeight) + 2 - m_scrollOffsetY;
            ::SetCaretPos(m_x + (int)cx, m_y + (int)cy);
        } else {
            float cx = pad + XFromCharPos(m_caretPos) - m_scrollOffsetX;
            float cy = (m_height - ExDPI::ScaleF(14.0f)) / 2;
            ::SetCaretPos(m_x + (int)cx, m_y + (int)cy);
        }
    }
}

bool GpmEdit::OnMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
    return false;
}

// ============================================================
// 辅助方法
// ============================================================
std::wstring GpmEdit::GetDisplayText() const {
    if (m_passwordMode && !m_text.empty()) {
        return std::wstring(m_text.length(), m_passwordChar);
    }
    return m_text;
}

void GpmEdit::EnsureCaretVisible() {
    float pad = ExDPI::ScaleF(6.0f);
    float clipPad = ExDPI::ScaleF(2.0f);
    float sbW = ExDPI::ScaleF(10.0f);
    
    if (!m_multiline) {
        float visW = (float)m_width - pad - clipPad;
        float caretX = XFromCharPos(m_caretPos);
        if (caretX - m_scrollOffsetX < 0) m_scrollOffsetX = (int)caretX;
        else if (caretX - m_scrollOffsetX > visW) m_scrollOffsetX = (int)(caretX - visW);
        if (m_scrollOffsetX < 0) m_scrollOffsetX = 0;
    } else {
        int line = LineFromChar(m_caretPos);
        int col = m_caretPos - CharPosFromLineCol(line, 0);
        float caretX = XFromCol(line, col);
        float caretY = line * m_lineHeight;
        bool needVScr = m_totalTextHeight > (m_height - clipPad * 2);
        bool needHScr = m_multiline && m_totalTextWidth > (m_width - clipPad * 2);
        float visW = (float)m_width - pad - clipPad - (needVScr ? sbW : 0);
        float visH = (float)m_height - clipPad * 2 - (needHScr ? sbW : 0);
        
        if (caretX - m_scrollOffsetX < 0) m_scrollOffsetX = (int)caretX;
        else if (caretX - m_scrollOffsetX > visW) m_scrollOffsetX = (int)(caretX - visW) + 20;
        
        if (caretY - m_scrollOffsetY < 0) m_scrollOffsetY = (int)caretY;
        else if (caretY - m_scrollOffsetY > visH - m_lineHeight) m_scrollOffsetY = (int)(caretY - visH + m_lineHeight) + 5;
        
        int maxOffX = (std::max)(0, m_totalTextWidth - (int)visW);
        int maxOffY = (std::max)(0, m_totalTextHeight - (int)visH);
        m_scrollOffsetX = (std::max)(0, (std::min)(maxOffX, m_scrollOffsetX));
        m_scrollOffsetY = (std::max)(0, (std::min)(maxOffY, m_scrollOffsetY));
    }
    UpdateCaretPos();
}

} // namespace gpm_ui

#endif // GPMUI_ENABLE_EDIT