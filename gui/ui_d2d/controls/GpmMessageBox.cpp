/*
 * GpmMessageBox.cpp - 消息框类 (D2D分层透明窗口)
 * 
 * 功能：
 * - 通过按钮文本分隔符（如"确认|取消|重试"）动态生成按钮
 * - 支持定时自动销毁（毫秒，0=不自动销毁）
 * - 支持传入图标位图指针（可选）
 * - 返回被点击按钮的1-based索引
 * - 带有关闭按钮 (X)，点关闭返回 0
 */
#include "../core/gpm_ui.h"
#include <cstdio>
#include <numeric>
#include <commctrl.h>

namespace gpm_ui {

// === 静态成员 ===
static bool s_msgBoxClassRegistered = false;
static const wchar_t* GetMsgBoxClassName() { return L"GPMUI_MessageBox_D2D"; }

static bool RegisterMsgBoxClass() {
    if (s_msgBoxClassRegistered) return true;
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = GpmMessageBox::WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = GetMsgBoxClassName();
    wc.hbrBackground = NULL;
    if (RegisterClassExW(&wc)) { s_msgBoxClassRegistered = true; return true; }
    return false;
}

// ============================================================
// GpmMessageBox 实现
// ============================================================

GpmMessageBox::GpmMessageBox()
    : m_hWnd(NULL)
    , m_result(0)
    , m_autoCloseMs(0)
    , m_iconBitmap(nullptr)
    , m_iconSize(0)
    , m_buttonHeight(0)
    , m_buttonGap(0)
    , m_padding(0)
    , m_btnCornerRadius(0)
    , m_fontSize(0)
    , m_memDC(NULL)
    , m_hBmp(NULL)
    , m_pBits(NULL)
    , m_rtWidth(0)
    , m_rtHeight(0)
    , m_painting(false)
    , m_hoverBtn(-1)
    , m_hoverClose(false)
    , m_cornerRadius(0)
    , m_dragging(false)
    , m_dragStart{0,0}
{
}

GpmMessageBox::~GpmMessageBox() {
    DestroyRT();
    if (m_hBmp) { ::DeleteObject(m_hBmp); m_hBmp = NULL; }
    if (m_memDC) { ::DeleteDC(m_memDC); m_memDC = NULL; }
    if (m_hWnd && ::IsWindow(m_hWnd)) ::DestroyWindow(m_hWnd);
}

void GpmMessageBox::SetIcon(ID2D1Bitmap* icon, int size) {
    m_iconBitmap = icon;
    m_iconSize = size > 0 ? size : ExDPI::Scale(48);
}

int GpmMessageBox::Show(HWND hParent, const std::wstring& title, const std::wstring& message,
                        const std::wstring& buttons, int autoCloseMs) {
    if (!RegisterMsgBoxClass()) return 0;

    m_title = title;
    m_message = message;
    m_autoCloseMs = autoCloseMs;
    m_result = 0;
    m_hoverBtn = -1;
    m_hoverClose = false;

    // 解析按钮文本
    m_buttons.clear();
    size_t start = 0, end;
    while ((end = buttons.find(L'|', start)) != std::wstring::npos) {
        if (end > start) m_buttons.push_back(buttons.substr(start, end - start));
        start = end + 1;
    }
    if (start < buttons.length()) m_buttons.push_back(buttons.substr(start));

    // 测量尺寸
    m_fontSize = ExDPI::ScaleF(12.0f);
    m_padding = ExDPI::Scale(20);
    m_buttonHeight = ExDPI::Scale(34);
    m_buttonGap = ExDPI::Scale(10);
    m_btnCornerRadius = ExDPI::Scale(4);
    m_cornerRadius = ExDPI::Scale(8);

    // 计算消息文本尺寸
    float msgW = 0, msgH = 0;
    MeasureText(m_message, &msgW, &msgH);

    // 消息框最小/最大宽度
    int minW = ExDPI::Scale(280);
    int maxW = ExDPI::Scale(600);
    int iconPadding = (m_iconBitmap && m_iconSize > 0) ? (m_iconSize + m_padding) : 0;

    int contentW = (int)msgW + iconPadding;
    if (contentW < minW) contentW = minW;
    if (contentW > maxW) contentW = maxW;

    // 计算按钮区域宽度
    int btnAreaW = 0;
    int btnCount = (int)m_buttons.size();
    if (btnCount > 0) {
        for (auto& btn : m_buttons) {
            float btnTW = 0, btnTH = 0;
            MeasureText(btn, &btnTW, &btnTH, m_fontSize);
            int btnW = (int)btnTW + m_padding;
            if (btnW < ExDPI::Scale(80)) btnW = ExDPI::Scale(80);
            btnAreaW += btnW;
        }
        btnAreaW += (btnCount - 1) * m_buttonGap;
    }
    if (btnAreaW > contentW - m_padding * 2) contentW = btnAreaW + m_padding * 2;
    if (contentW > maxW) contentW = maxW;

    // 计算按钮实际宽度
    m_btnWidths.resize(btnCount);
    if (btnCount > 0) {
        int availW = contentW - m_padding * 2 - (btnCount - 1) * m_buttonGap;
        int eachW = availW / btnCount;
        for (int i = 0; i < btnCount; i++) {
            float btnTW = 0, btnTH = 0;
            MeasureText(m_buttons[i], &btnTW, &btnTH, m_fontSize);
            int btnW = (int)btnTW + m_padding;
            if (btnW < ExDPI::Scale(80)) btnW = ExDPI::Scale(80);
            if (btnW > eachW && btnCount > 1) btnW = eachW;
            m_btnWidths[i] = btnW;
        }
        // 重新分配剩余空间
        int usedW = (int)std::accumulate(m_btnWidths.begin(), m_btnWidths.end(), 0) + (btnCount - 1) * m_buttonGap;
        int extra = (contentW - m_padding * 2) - usedW;
        if (extra > 0) {
            int addPerBtn = extra / btnCount;
            for (int i = 0; i < btnCount; i++) m_btnWidths[i] += addPerBtn;
        }
    }

    // 计算总高度
    int titleH = ExDPI::Scale(24);
    int msgH_int = (int)msgH + ExDPI::Scale(8);
    if (msgH_int < ExDPI::Scale(24)) msgH_int = ExDPI::Scale(24);
    int iconH = (m_iconBitmap && m_iconSize > 0) ? (m_iconSize + ExDPI::Scale(8)) : 0;
    int contentH = (iconH > msgH_int ? iconH : msgH_int);
    int totalH = m_padding + titleH + ExDPI::Scale(12) + contentH + m_padding + m_buttonHeight + ExDPI::Scale(16);
    if (totalH < ExDPI::Scale(160)) totalH = ExDPI::Scale(160);

    m_winWidth = contentW + m_padding * 2;
    m_winHeight = totalH;

    // 创建窗口（居中于父窗口或屏幕）
    RECT parentRc = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    if (hParent && ::IsWindow(hParent)) ::GetWindowRect(hParent, &parentRc);
    int cx = parentRc.left + (parentRc.right - parentRc.left - m_winWidth) / 2;
    int cy = parentRc.top + (parentRc.bottom - parentRc.top - m_winHeight) / 3;

    DWORD dwExStyle = WS_EX_LAYERED | WS_EX_APPWINDOW;
    m_hWnd = ::CreateWindowExW(dwExStyle, GetMsgBoxClassName(), title.c_str(),
                                WS_POPUP, cx, cy, m_winWidth, m_winHeight,
                                hParent, NULL, GetModuleHandle(NULL), this);
    if (!m_hWnd) return 0;

    // 设置定时器
    if (m_autoCloseMs > 0) {
        ::SetTimer(m_hWnd, 1001, m_autoCloseMs, NULL);
    }

    // 关闭按钮区域
    m_closeBtnX = m_winWidth - ExDPI::Scale(36);
    m_closeBtnY = 0;
    m_closeBtnW = ExDPI::Scale(36);
    m_closeBtnH = ExDPI::Scale(28);
    m_closeBtnR = (float)m_cornerRadius;

    ::ShowWindow(m_hWnd, SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    DoPaint();

    // 模态消息循环 - 使用 PeekMessage 避免 WM_QUIT 问题
    MSG msg;
    while (::IsWindow(m_hWnd) && ::IsWindowVisible(m_hWnd)) {
        while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                // 不要转发 WM_QUIT，忽略它
                continue;
            }
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
        // 小休眠避免CPU占用
        ::Sleep(1);
    }

    int result = m_result;
    if (m_hWnd) { ::DestroyWindow(m_hWnd); m_hWnd = NULL; }
    DestroyRT();
    return result;
}

void GpmMessageBox::DestroyRT() {
    m_renderTarget.Reset();
    if (m_hBmp) { ::DeleteObject(m_hBmp); m_hBmp = NULL; m_pBits = NULL; }
    if (m_memDC) { ::DeleteDC(m_memDC); m_memDC = NULL; }
    m_rtWidth = 0; m_rtHeight = 0;
}

void GpmMessageBox::CreateDIB(int w, int h) {
    if (m_hBmp) { ::DeleteObject(m_hBmp); m_hBmp = NULL; }
    if (m_memDC) { ::DeleteDC(m_memDC); m_memDC = NULL; }
    m_pBits = NULL;

    HDC hScreenDC = ::GetDC(NULL);
    m_memDC = ::CreateCompatibleDC(hScreenDC);

    BITMAPV5HEADER bi = { sizeof(BITMAPV5HEADER) };
    bi.bV5Width = w;
    bi.bV5Height = -h;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_RGB;
    bi.bV5AlphaMask = 0xFF000000;
    m_hBmp = ::CreateDIBSection(hScreenDC, (BITMAPINFO*)&bi, DIB_RGB_COLORS, (void**)&m_pBits, NULL, 0);
    ::ReleaseDC(NULL, hScreenDC);

    if (m_hBmp) ::SelectObject(m_memDC, m_hBmp);
    m_rtWidth = w; m_rtHeight = h;
}

void GpmMessageBox::DoPaint() {
    if (!m_hWnd || m_painting) return;
    int w = m_winWidth, h = m_winHeight;
    if (w <= 0 || h <= 0) return;

    if (w != m_rtWidth || h != m_rtHeight) DestroyRT();
    if (!m_renderTarget) {
        CreateDIB(w, h);
        if (!m_memDC || !m_hBmp) return;

        auto* factory = ExD2DFactory::GetFactory();
        if (!factory) return;

        D2D1_PIXEL_FORMAT pf = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, pf);
        ID2D1DCRenderTarget* rt = nullptr;
        HRESULT hr = factory->CreateDCRenderTarget(&props, &rt);
        if (FAILED(hr)) {
            props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, pf);
            factory->CreateDCRenderTarget(&props, &rt);
        }
        if (rt) {
            RECT bindRc = { 0, 0, w, h };
            hr = rt->BindDC(m_memDC, &bindRc);
            if (SUCCEEDED(hr)) m_renderTarget.Attach(rt);
            else rt->Release();
        }
    }
    if (!m_renderTarget) return;

    RECT bindRc = { 0, 0, w, h };
    m_renderTarget->BindDC(m_memDC, &bindRc);

    m_painting = true;
    m_renderTarget->BeginDraw();
    m_renderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    // 背景
    auto& t = Theme();
    float radius = (float)m_cornerRadius;
    ID2D1SolidColorBrush* bgBr = nullptr;
    COLORREF bgColor = RGB(44, 47, 55);
    m_renderTarget->CreateSolidColorBrush(ColorRefToD2D(bgColor), &bgBr);
    if (bgBr) {
        D2D1_ROUNDED_RECT bgRR = MakeRoundRect(0, 0, (float)w, (float)h, radius);
        m_renderTarget->FillRoundedRectangle(&bgRR, bgBr);
        bgBr->Release();
    }

    // 边框
    ID2D1SolidColorBrush* bdBr = nullptr;
    m_renderTarget->CreateSolidColorBrush(ColorRefToD2D(t.border), &bdBr);
    if (bdBr) {
        const float half = 0.5f;
        D2D1_ROUNDED_RECT rr = MakeRoundRect(half, half, (float)w - 1.0f, (float)h - 1.0f, radius);
        m_renderTarget->DrawRoundedRectangle(&rr, bdBr, 1.0f);
        bdBr->Release();
    }

    // 顶部高亮线
    ID2D1SolidColorBrush* accentBr = nullptr;
    m_renderTarget->CreateSolidColorBrush(ColorRefToD2D(t.fgAccent, 0.6f), &accentBr);
    if (accentBr) {
        float lineR = radius;
        if (lineR > ExDPI::ScaleF(4)) lineR = ExDPI::ScaleF(4);
        m_renderTarget->FillRectangle(D2D1::RectF(lineR, 0, (float)w - lineR, ExDPI::ScaleF(2)), accentBr);
        accentBr->Release();
    }

    float x = (float)m_padding;
    float y = (float)m_padding;
    float contentW = (float)(w - m_padding * 2);

    // 绘制标题
    IDWriteTextFormat* titleFmt = ExD2DFactory::CreateTextFormat(13.0f, true, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (titleFmt) {
        ID2D1SolidColorBrush* titleBr = nullptr;
        m_renderTarget->CreateSolidColorBrush(ColorRefToD2D(t.fgPrimary), &titleBr);
        if (titleBr) {
            D2D1_RECT_F titleRc = D2D1::RectF(x, y, x + contentW - m_closeBtnW, y + ExDPI::ScaleF(24));
            m_renderTarget->DrawText(m_title.c_str(), (UINT32)m_title.length(), titleFmt, titleRc, titleBr);
            titleBr->Release();
        }
        titleFmt->Release();
    }

    // 关闭按钮
    float cbx = (float)m_closeBtnX, cby = (float)m_closeBtnY;
    float cbw = (float)m_closeBtnW, cbh = (float)m_closeBtnH;
    // 仅绘制X图标，背景透明
    {
        ID2D1SolidColorBrush* pen = nullptr;
        COLORREF closeColor = m_hoverClose ? t.closeHover : t.fgSecondary;
        m_renderTarget->CreateSolidColorBrush(ColorRefToD2D(closeColor), &pen);
        if (pen) {
            float cx = cbx + cbw / 2, cy = cby + cbh / 2, s = ExDPI::ScaleF(6.0f);
            m_renderTarget->DrawLine(D2D1::Point2F(cx - s, cy - s), D2D1::Point2F(cx + s, cy + s), pen, ExDPI::ScaleF(1.5f));
            m_renderTarget->DrawLine(D2D1::Point2F(cx + s, cy - s), D2D1::Point2F(cx - s, cy + s), pen, ExDPI::ScaleF(1.5f));
            pen->Release();
        }
    }

    // 分隔线
    float sepY = y + ExDPI::ScaleF(24) + ExDPI::ScaleF(6);
    ID2D1SolidColorBrush* sepBr = nullptr;
    COLORREF sepColor = RGB((std::max)(0, (int)GetRValue(bgColor) - 20),
                            (std::max)(0, (int)GetGValue(bgColor) - 20),
                            (std::max)(0, (int)GetBValue(bgColor) - 20));
    m_renderTarget->CreateSolidColorBrush(ColorRefToD2D(sepColor), &sepBr);
    if (sepBr) {
        m_renderTarget->DrawLine(D2D1::Point2F(x, sepY), D2D1::Point2F(x + contentW, sepY), sepBr, 1.0f);
        sepBr->Release();
    }

    // 绘制图标（如果有）
    float iconX = x;
    float iconY = sepY + ExDPI::ScaleF(10);
    float msgAreaX = x;
    float msgAreaW = contentW;
    float msgAreaY = iconY;

    if (m_iconBitmap && m_iconSize > 0) {
        float iconS = (float)m_iconSize;
        D2D1_RECT_F iconRc = D2D1::RectF(iconX, iconY, iconX + iconS, iconY + iconS);
        m_renderTarget->DrawBitmap(m_iconBitmap, iconRc, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        msgAreaX = iconX + iconS + ExDPI::ScaleF(12);
        msgAreaW = contentW - (msgAreaX - x);
    }

    // 绘制消息文本
    IDWriteTextFormat* msgFmt = ExD2DFactory::CreateTextFormat(m_fontSize, false, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (msgFmt) {
        // 自动换行
        msgFmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        ID2D1SolidColorBrush* msgBr = nullptr;
        m_renderTarget->CreateSolidColorBrush(ColorRefToD2D(t.fgSecondary), &msgBr);
        if (msgBr) {
            float maxMsgH = (float)(h - m_padding * 2 - ExDPI::Scale(24) - ExDPI::Scale(12) - m_buttonHeight - ExDPI::Scale(16));
            D2D1_RECT_F msgRc = D2D1::RectF(msgAreaX, msgAreaY, msgAreaX + msgAreaW, msgAreaY + maxMsgH);
            m_renderTarget->DrawText(m_message.c_str(), (UINT32)m_message.length(), msgFmt, msgRc, msgBr);
            msgBr->Release();
        }
        // 恢复不换行
        msgFmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        msgFmt->Release();
    }

    // 绘制按钮（底部居中）
    float btnAreaY = (float)(h - m_padding - m_buttonHeight);
    int btnCount = (int)m_buttons.size();
    if (btnCount > 0) {
        int totalBtnW = 0;
        for (int i = 0; i < btnCount; i++) totalBtnW += m_btnWidths[i];
        totalBtnW += (btnCount - 1) * m_buttonGap;
        float btnStartX = ((float)w - (float)totalBtnW) / 2.0f;

        for (int i = 0; i < btnCount; i++) {
            float bx = btnStartX;
            for (int j = 0; j < i; j++) bx += m_btnWidths[j] + m_buttonGap;
            float bw = (float)m_btnWidths[i];
            float bh = (float)m_buttonHeight;
            bool isHover = (i == m_hoverBtn);

            // 按钮背景
            COLORREF btnBg = isHover ? t.btnBgHover : t.btnBg;
            ID2D1SolidColorBrush* btnBr = nullptr;
            m_renderTarget->CreateSolidColorBrush(ColorRefToD2D(btnBg, 1.0f), &btnBr);
            if (btnBr) {
                D2D1_ROUNDED_RECT btnRR = MakeRoundRect(bx, btnAreaY, bw, bh, (float)m_btnCornerRadius);
                m_renderTarget->FillRoundedRectangle(&btnRR, btnBr);
                btnBr->Release();
            }

            // 按钮边框
            ID2D1SolidColorBrush* btnBd = nullptr;
            COLORREF bdC = isHover ? t.borderFocus : t.border;
            m_renderTarget->CreateSolidColorBrush(ColorRefToD2D(bdC), &btnBd);
            if (btnBd) {
                D2D1_ROUNDED_RECT btnRR = MakeRoundRect(bx, btnAreaY, bw, bh, (float)m_btnCornerRadius);
                m_renderTarget->DrawRoundedRectangle(&btnRR, btnBd, 1.0f);
                btnBd->Release();
            }

            // 按钮文字
            IDWriteTextFormat* btnFmt = ExD2DFactory::CreateTextFormat(m_fontSize, false, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            if (btnFmt) {
                ID2D1SolidColorBrush* btnTxt = nullptr;
                m_renderTarget->CreateSolidColorBrush(ColorRefToD2D(t.fgPrimary), &btnTxt);
                if (btnTxt) {
                    D2D1_RECT_F btnRc = D2D1::RectF(bx, btnAreaY, bx + bw, btnAreaY + bh);
                    m_renderTarget->DrawText(m_buttons[i].c_str(), (UINT32)m_buttons[i].length(), btnFmt, btnRc, btnTxt);
                    btnTxt->Release();
                }
                btnFmt->Release();
            }
        }
    }

    m_renderTarget->EndDraw();

    if (m_hWnd && m_memDC) {
        RECT wndRc;
        ::GetWindowRect(m_hWnd, &wndRc);
        POINT pt = { wndRc.left, wndRc.top };
        SIZE size = { w, h };
        POINT srcPt = { 0, 0 };
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        ::UpdateLayeredWindow(m_hWnd, NULL, &pt, &size, m_memDC, &srcPt, 0, &blend, ULW_ALPHA);
    }

    m_painting = false;
}

void GpmMessageBox::MeasureText(const std::wstring& text, float* outW, float* outH, float fontSize) {
    if (text.empty()) { *outW = 0; *outH = 0; return; }
    if (fontSize <= 0) fontSize = m_fontSize;

    IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(fontSize, false, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (!fmt) { *outW = 0; *outH = 0; return; }

    IDWriteTextLayout* layout = nullptr;
    ExD2DFactory::GetDWriteFactory()->CreateTextLayout(
        text.c_str(), (UINT32)text.length(), fmt,
        ExDPI::ScaleF(400.0f), ExDPI::ScaleF(200.0f), &layout);

    if (layout) {
        DWRITE_TEXT_METRICS metrics;
        layout->GetMetrics(&metrics);
        *outW = metrics.width;
        *outH = metrics.height;
        layout->Release();
    } else {
        *outW = (float)text.length() * ExDPI::ScaleF(7.0f);
        *outH = ExDPI::ScaleF(20.0f);
    }
    fmt->Release();
}

int GpmMessageBox::HitTestButton(int x, int y) const {
    int w = m_winWidth, h = m_winHeight;
    float btnAreaY = (float)(h - m_padding - m_buttonHeight);
    if (y < btnAreaY || y > btnAreaY + m_buttonHeight) return -1;

    int btnCount = (int)m_buttons.size();
    if (btnCount == 0) return -1;

    int totalBtnW = 0;
    for (int i = 0; i < btnCount; i++) totalBtnW += m_btnWidths[i];
    totalBtnW += (btnCount - 1) * m_buttonGap;
    float btnStartX = ((float)w - (float)totalBtnW) / 2.0f;

    float bx = btnStartX;
    for (int i = 0; i < btnCount; i++) {
        float bw = (float)m_btnWidths[i];
        if (x >= bx && x <= bx + bw) return i;
        bx += bw + m_buttonGap;
    }
    return -1;
}

bool GpmMessageBox::HitTestCloseBtn(int x, int y) const {
    return x >= m_closeBtnX && x < m_closeBtnX + m_closeBtnW &&
           y >= m_closeBtnY && y < m_closeBtnY + m_closeBtnH;
}

void GpmMessageBox::CloseWithResult(int result) {
    m_result = result;
    if (m_hWnd) {
        ::ShowWindow(m_hWnd, SW_HIDE);
        ::DestroyWindow(m_hWnd);
        m_hWnd = NULL;
    }
}

LRESULT CALLBACK GpmMessageBox::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    GpmMessageBox* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<GpmMessageBox*>(pCreate->lpCreateParams);
        ::SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hWnd = hWnd;
    } else {
        pThis = reinterpret_cast<GpmMessageBox*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }
    if (!pThis) return ::DefWindowProc(hWnd, msg, wParam, lParam);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        ::BeginPaint(hWnd, &ps);
        ::EndPaint(hWnd, &ps);
        if (!pThis->m_painting) pThis->DoPaint();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
        // 处理窗口拖拽
        if (pThis->m_dragging) {
            RECT rcWnd;
            ::GetWindowRect(hWnd, &rcWnd);
            POINT ptCur;
            ::GetCursorPos(&ptCur);
            int newX = rcWnd.left + ptCur.x - pThis->m_dragStart.x;
            int newY = rcWnd.top + ptCur.y - pThis->m_dragStart.y;
            ::SetWindowPos(hWnd, NULL, newX, newY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            pThis->m_dragStart = ptCur;
            return 0;
        }
        int hit = pThis->HitTestButton(mx, my);
        bool hitClose = pThis->HitTestCloseBtn(mx, my);
        bool needRedraw = false;
        if (hit != pThis->m_hoverBtn) {
            pThis->m_hoverBtn = hit;
            needRedraw = true;
        }
        if (hitClose != pThis->m_hoverClose) {
            pThis->m_hoverClose = hitClose;
            needRedraw = true;
        }
        if (needRedraw) pThis->DoPaint();
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
        ::TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE: {
        bool needRedraw = false;
        if (pThis->m_hoverBtn >= 0) {
            pThis->m_hoverBtn = -1;
            needRedraw = true;
        }
        if (pThis->m_hoverClose) {
            pThis->m_hoverClose = false;
            needRedraw = true;
        }
        if (needRedraw) pThis->DoPaint();
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
        // 检查关闭按钮
        if (pThis->HitTestCloseBtn(mx, my)) {
            pThis->CloseWithResult(0);
            return 0;
        }
        // 检查普通按钮
        int hit = pThis->HitTestButton(mx, my);
        if (hit >= 0) {
            pThis->CloseWithResult(hit + 1); // 1-based index
            return 0;
        }
        // 标题栏拖拽：点击在标题区域（分隔线以上，非关闭按钮区域）
        // 标题区域 = y >= 0 && y < 分隔线Y 且 不在关闭按钮上
        float sepY = (float)pThis->m_padding + ExDPI::ScaleF(24) + ExDPI::ScaleF(6);
        if (my >= 0 && my < sepY) {
            ::SetCapture(hWnd);
            pThis->m_dragging = true;
            ::GetCursorPos(&pThis->m_dragStart);
        }
        return 0;
    }
    case WM_TIMER: {
        if (wParam == 1001) {
            pThis->CloseWithResult(0);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (pThis->m_dragging) {
            pThis->m_dragging = false;
            ::ReleaseCapture();
            return 0;
        }
        return 0;
    }
    case WM_KEYDOWN: {
        if (wParam == VK_RETURN || wParam == VK_ESCAPE) {
            int btnCount = (int)pThis->m_buttons.size();
            if (wParam == VK_RETURN && btnCount > 0) {
                pThis->CloseWithResult(1);
            } else if (wParam == VK_ESCAPE) {
                // Esc = 关闭按钮效果，返回0
                pThis->CloseWithResult(0);
            }
        }
        return 0;
    }
    case WM_CLOSE: {
        pThis->CloseWithResult(0);
        return 0;
    }
    case WM_DESTROY: {
        if (pThis->m_autoCloseMs > 0) ::KillTimer(hWnd, 1001);
        return 0;
    }
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}

} // namespace gpm_ui