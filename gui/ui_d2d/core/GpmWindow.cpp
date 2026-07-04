/*
 * GpmWindow.cpp - 窗口类 (D2D DCRenderTarget + 内存DIB + UpdateLayeredWindow)
 * 
 * 真正支持分层透明窗口的正确方式：
 * 1. CreateDIBSection 创建32位带alpha的DIB
 * 2. ID2D1DCRenderTarget BindDC 到内存DC
 * 3. 用D2D绘制圆角+所有内容
 * 4. UpdateLayeredWindow 提交到分层窗口
 * 
 * 使用 m_painting 重入保护防止 UpdateLayeredWindow 递归
 */
#include "gpm_ui.h"

namespace gpm_ui {

#define HIT_NONE    0
#define HIT_CLOSE   1
#define HIT_MIN     2

void GpmWindow::UpdateWindowRegion() {
    return;
}

bool GpmWindow::s_classRegistered = false;
const wchar_t* GpmWindow::GetWindowClassName() { return L"GPMUI_Window_D2D"; }

bool GpmWindow::RegisterWindowClass() {
    if (s_classRegistered) return true;
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = GpmWindow::WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = GetWindowClassName();
    wc.hbrBackground = NULL;
    if (RegisterClassExW(&wc)) { s_classRegistered = true; return true; }
    return false;
}

GpmWindow::GpmWindow() 
    : m_hWnd(NULL)
    , m_exStyle(0), m_text()
    , m_isMainWindow(false)
    , m_x(0), m_y(0), m_width(0), m_height(0)
    , m_titleBarHeight(0), m_windowCornerRadius(0)
    , m_bkColor(0), m_resizeBorder(0)
    , m_hoverBtn(HIT_NONE), m_downBtn(HIT_NONE)
    , m_dragging(false), m_dragStart{0,0}
    , m_resizing(false), m_resizeDir(RSZ_NONE)
    , m_resizeStartRect{0,0,0,0}, m_resizeStartPoint{0,0}
    , m_focusCtrl(nullptr), m_captureCtrl(nullptr), m_lastHoverCtrl(nullptr)
    , m_hMemDC(NULL), m_hBmp(NULL), m_pBits(NULL)
    , m_rtWidth(0), m_rtHeight(0), m_painting(false) {}
    
GpmWindow::~GpmWindow() {
    DestroyRenderTarget();
    if (m_hBmp) { ::DeleteObject(m_hBmp); m_hBmp = NULL; }
    if (m_hMemDC) { ::DeleteDC(m_hMemDC); m_hMemDC = NULL; }
    if (m_hWnd && ::IsWindow(m_hWnd)) ::DestroyWindow(m_hWnd);
}

bool GpmWindow::Create(HWND hParent, int x, int y, int width, int height,
                      const std::wstring& title, DWORD style) {
    if (!RegisterWindowClass()) return false;
    m_exStyle = style;
    m_text = title;
    m_isMainWindow = (style & GPMWND_STYLE_MAINWINDOW) != 0;
    m_bkColor = Theme().bgWindow;
    m_titleBarHeight = ExDPI::Scale(32);
    m_windowCornerRadius = 8;
    m_resizeBorder = 12;

    int sw = ExDPI::Scale(width);
    int sh = ExDPI::Scale(height);
    m_width = sw; m_height = sh;

    DWORD dwWinStyle = WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    DWORD dwExStyle = WS_EX_APPWINDOW | WS_EX_LAYERED;

    int posX = x, posY = y;
    if (style & GPMWND_STYLE_CENTER) {
        posX = (GetSystemMetrics(SM_CXSCREEN) - sw) / 2;
        posY = (GetSystemMetrics(SM_CYSCREEN) - sh) / 2;
    }
    m_x = posX; m_y = posY;

    m_hWnd = ::CreateWindowExW(dwExStyle, GetWindowClassName(), title.c_str(), dwWinStyle,
                               posX, posY, sw, sh, hParent, NULL, GetModuleHandle(NULL), this);
    return m_hWnd != NULL;
}

void GpmWindow::Show(int nCmdShow) {
    if (m_hWnd) {
        ::ShowWindow(m_hWnd, nCmdShow);
        DoPaint();
        ::SetForegroundWindow(m_hWnd);
    }
}
void GpmWindow::Hide() { if (m_hWnd) ::ShowWindow(m_hWnd, SW_HIDE); }
void GpmWindow::SetBackgroundColor(COLORREF color) { m_bkColor = color; }
void GpmWindow::AddControl(UIElement* ctrl) { if (ctrl) m_controls.push_back(ctrl); }

UIElement* GpmWindow::GetControlByID(int id) {
    for (auto* c : m_controls) if (c && c->GetID() == id) return c;
    for (auto* c : m_controls) {
        if (c) {
            auto* found = c->FindChildByID(id);
            if (found) return found;
        }
    }
    return nullptr;
}

void GpmWindow::Redraw() {
    if (m_hWnd && ::IsWindow(m_hWnd)) DoPaint();
}

int GpmWindow::Run() {
    MSG msg;
    while (::GetMessage(&msg, NULL, 0, 0)) { ::TranslateMessage(&msg); ::DispatchMessage(&msg); }
    return (int)msg.wParam;
}

void GpmWindow::SetFocusControl(int id) {
    UIElement* ctrl = GetControlByID(id);
    if (ctrl) {
        if (m_focusCtrl && m_focusCtrl != ctrl) m_focusCtrl->OnKillFocus();
        m_focusCtrl = ctrl;
        m_focusCtrl->OnSetFocus();
        if (m_hWnd) ::SetFocus(m_hWnd);
        // 设置IME组合窗口位置到焦点控件
        HIMC hIMC = ::ImmGetContext(m_hWnd);
        if (hIMC) {
            CANDIDATEFORM cf = { CFS_CANDIDATEPOS };
            POINT pt = { m_focusCtrl->GetX(), m_focusCtrl->GetY() + m_focusCtrl->GetHeight() };
            cf.ptCurrentPos = pt;
            ::ImmSetCandidateWindow(hIMC, &cf);
            COMPOSITIONFORM ccf = { CFS_POINT };
            ccf.ptCurrentPos = pt;
            ::ImmSetCompositionWindow(hIMC, &ccf);
            ::ImmReleaseContext(m_hWnd, hIMC);
        }
    }
}

void GpmWindow::CreateDIB(int w, int h) {
    if (m_hBmp) { ::DeleteObject(m_hBmp); m_hBmp = NULL; }
    if (m_hMemDC) { ::DeleteDC(m_hMemDC); m_hMemDC = NULL; }
    m_pBits = NULL;

    HDC hScreenDC = ::GetDC(NULL);
    m_hMemDC = ::CreateCompatibleDC(hScreenDC);

    BITMAPV5HEADER bi = { sizeof(BITMAPV5HEADER) };
    bi.bV5Width = w;
    bi.bV5Height = -h;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_RGB;
    bi.bV5AlphaMask = 0xFF000000;
    m_hBmp = ::CreateDIBSection(hScreenDC, (BITMAPINFO*)&bi, DIB_RGB_COLORS, (void**)&m_pBits, NULL, 0);
    ::ReleaseDC(NULL, hScreenDC);

    if (m_hBmp) ::SelectObject(m_hMemDC, m_hBmp);
    m_rtWidth = w; m_rtHeight = h;
}

void GpmWindow::CreateRenderTarget() {
    if (m_renderTarget) return;
    auto* factory = ExD2DFactory::GetFactory();
    if (!factory || !m_hWnd) return;
    if (m_width <= 0 || m_height <= 0) return;

    CreateDIB(m_width, m_height);
    if (!m_hMemDC || !m_hBmp) return;

    D2D1_PIXEL_FORMAT pf = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, pf);
    ID2D1DCRenderTarget* rt = nullptr;
    HRESULT hr = factory->CreateDCRenderTarget(&props, &rt);
    if (FAILED(hr)) {
        props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, pf);
        factory->CreateDCRenderTarget(&props, &rt);
    }
    if (rt) {
        RECT bindRc = { 0, 0, m_width, m_height };
        hr = rt->BindDC(m_hMemDC, &bindRc);
        if (SUCCEEDED(hr)) m_renderTarget.Attach(rt);
        else rt->Release();
    }
}

void GpmWindow::DestroyRenderTarget() {
    m_renderTarget.Reset();
    if (m_hBmp) { ::DeleteObject(m_hBmp); m_hBmp = NULL; m_pBits = NULL; }
    if (m_hMemDC) { ::DeleteDC(m_hMemDC); m_hMemDC = NULL; }
    m_rtWidth = 0; m_rtHeight = 0;
}

void GpmWindow::DoPaint() {
    if (!m_hWnd || m_painting) return;
    int w = m_width, h = m_height;
    if (w <= 0 || h <= 0) return;

    if (w != m_rtWidth || h != m_rtHeight) DestroyRenderTarget();
    if (!m_renderTarget) CreateRenderTarget();
    if (!m_renderTarget) return;

    RECT bindRc = { 0, 0, w, h };
    m_renderTarget->BindDC(m_hMemDC, &bindRc);

    m_painting = true;

    m_renderTarget->BeginDraw();
    m_renderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_renderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    float radius = (float)ExDPI::Scale(m_windowCornerRadius);
    auto& t = Theme();
    float borderWidth = 1.0f;

    if (m_bkColor == RGB(0, 0, 0) && m_windowCornerRadius == 0) {
        // Transparent host surface; app content draws its own rounded body and shadow.
    } else if (radius > 0) {
        ID2D1SolidColorBrush* bgBrush = nullptr;
        m_renderTarget->CreateSolidColorBrush(ColorRefToD2D(m_bkColor), &bgBrush);
        if (bgBrush) {
            D2D1_ROUNDED_RECT bgRR = MakeRoundRect(0, 0, (float)w, (float)h, radius);
            m_renderTarget->FillRoundedRectangle(&bgRR, bgBrush);
            bgBrush->Release();
        }
    } else {
        m_renderTarget->Clear(ColorRefToD2D(m_bkColor));
    }

    DrawTitleBar(m_renderTarget.Get(), (float)w, (float)h);

    ID2D1SolidColorBrush* borderBrush = nullptr;
    m_renderTarget->CreateSolidColorBrush(ColorRefToD2D(t.border), &borderBrush);
    if (borderBrush && !(m_bkColor == RGB(0, 0, 0) && m_windowCornerRadius == 0)) {
        const float half = borderWidth * 0.5f;
        D2D1_ROUNDED_RECT rr = MakeRoundRect(half, half, (float)w - borderWidth, (float)h - borderWidth, radius);
        m_renderTarget->DrawRoundedRectangle(&rr, borderBrush, borderWidth);
        borderBrush->Release();
    }

    for (auto* ctrl : m_controls) {
        if (ctrl && ctrl->IsVisible() && ctrl->GetOpacity() > 0.001f) {
            RECT rc; ctrl->GetRect(rc);
            D2D1_RECT_F d2dRc = D2D1::RectF((float)rc.left, (float)rc.top, (float)rc.right, (float)rc.bottom);
            ctrl->OnPaintD2D(m_renderTarget.Get(), d2dRc);
        }
    }

    m_renderTarget->EndDraw();

    if (m_hWnd && m_hMemDC) {
        RECT wndRc;
        ::GetWindowRect(m_hWnd, &wndRc);
        m_x = wndRc.left;
        m_y = wndRc.top;
        POINT pt = { m_x, m_y };
        SIZE size = { w, h };
        POINT srcPt = { 0, 0 };
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        ::UpdateLayeredWindow(m_hWnd, NULL, &pt, &size, m_hMemDC, &srcPt, 0, &blend, ULW_ALPHA);
    }

    m_painting = false;
}

void GpmWindow::RouteMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (m_focusCtrl) { LRESULT r = 0; m_focusCtrl->OnMessage(msg, wParam, lParam, &r); }
}

ResizeDir GpmWindow::HitTestResizeBorder(int x, int y) const {
    if (!(m_exStyle & GPMWND_STYLE_SIZEABLE)) return RSZ_NONE;
    RECT rc; ::GetClientRect(m_hWnd, &rc);
    int w = rc.right, h = rc.bottom, b = ExDPI::Scale(m_resizeBorder);
    bool left = (x < b), right = (x >= w - b), top = (y < b), bottom = (y >= h - b);
    if (top && left) return RSZ_TOPLEFT; if (top && right) return RSZ_TOPRIGHT;
    if (bottom && left) return RSZ_BOTTOMLEFT; if (bottom && right) return RSZ_BOTTOMRIGHT;
    if (left) return RSZ_LEFT; if (right) return RSZ_RIGHT; if (top) return RSZ_TOP; if (bottom) return RSZ_BOTTOM;
    return RSZ_NONE;
}

void GpmWindow::UpdateResizeCursor(ResizeDir dir) {
    LPCTSTR id = IDC_ARROW;
    switch (dir) {
        case RSZ_LEFT: case RSZ_RIGHT: id = IDC_SIZEWE; break;
        case RSZ_TOP: case RSZ_BOTTOM: id = IDC_SIZENS; break;
        case RSZ_TOPLEFT: case RSZ_BOTTOMRIGHT: id = IDC_SIZENWSE; break;
        case RSZ_TOPRIGHT: case RSZ_BOTTOMLEFT: id = IDC_SIZENESW; break;
        default: break;
    }
    ::SetCursor(::LoadCursor(NULL, id));
}

void GpmWindow::PerformResize(int screenX, int screenY) {
    RECT rc = m_resizeStartRect;
    int dx = screenX - m_resizeStartPoint.x, dy = screenY - m_resizeStartPoint.y;
    int minW = ExDPI::Scale(200), minH = ExDPI::Scale(150);
    if (m_resizeDir & RSZ_LEFT) { rc.left += dx; if (rc.right - rc.left < minW) rc.left = rc.right - minW; }
    if (m_resizeDir & RSZ_RIGHT) { rc.right += dx; if (rc.right - rc.left < minW) rc.right = rc.left + minW; }
    if (m_resizeDir & RSZ_TOP) { rc.top += dy; if (rc.bottom - rc.top < minH) rc.top = rc.bottom - minH; }
    if (m_resizeDir & RSZ_BOTTOM) { rc.bottom += dy; if (rc.bottom - rc.top < minH) rc.bottom = rc.top + minH; }
    ::SetWindowPos(m_hWnd, NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
    m_width = rc.right - rc.left; m_height = rc.bottom - rc.top;
    m_x = rc.left; m_y = rc.top;
    if (!m_painting) DoPaint();
}

void GpmWindow::DrawTitleBar(ID2D1RenderTarget* rt, float w, float h) {
    if (!(m_exStyle & GPMWND_STYLE_TITLE)) return;
    auto& t = Theme();
    float tbH = (float)m_titleBarHeight;
    float radius = (float)ExDPI::Scale(m_windowCornerRadius);

    ID2D1SolidColorBrush* brush = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(t.bgTitleBar), &brush);
    if (brush) {
        if (radius > 0 && tbH > 0) {
            float r = (std::min)(radius, tbH);
            ID2D1PathGeometry* tbGeo = nullptr;
            ExD2DFactory::GetFactory()->CreatePathGeometry(&tbGeo);
            if (tbGeo) {
                ID2D1GeometrySink* sink = nullptr;
                tbGeo->Open(&sink);
                if (sink) {
                    sink->BeginFigure(D2D1::Point2F(r, 0), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(w - r, 0));
                    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(w, r), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                    sink->AddLine(D2D1::Point2F(w, tbH));
                    sink->AddLine(D2D1::Point2F(0, tbH));
                    sink->AddLine(D2D1::Point2F(0, r));
                    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(r, 0), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close(); sink->Release();
                }
                rt->FillGeometry(tbGeo, brush);
                tbGeo->Release();
            }
        } else {
            rt->FillRectangle(D2D1::RectF(0, 0, w, tbH), brush);
        }
        brush->Release();
    }
    ID2D1SolidColorBrush* sepBr = nullptr;
    COLORREF sepColor = RGB((std::max)(0, (int)GetRValue(t.bgTitleBar) - 15), (std::max)(0, (int)GetGValue(t.bgTitleBar) - 15), (std::max)(0, (int)GetBValue(t.bgTitleBar) - 15));
    rt->CreateSolidColorBrush(ColorRefToD2D(sepColor), &sepBr);
    if (sepBr) { rt->DrawLine(D2D1::Point2F(0, (float)m_titleBarHeight), D2D1::Point2F(w, (float)m_titleBarHeight), sepBr, 1.0f); sepBr->Release(); }
    IDWriteTextFormat* fmt = ExD2DFactory::CreateTextFormat(11.0f, false, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (fmt) {
        ID2D1SolidColorBrush* textBrush = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(t.fgPrimary), &textBrush);
        if (textBrush) {
            float pad = ExDPI::ScaleF(10.0f);
            D2D1_RECT_F textRect = D2D1::RectF(pad, 0, w - ExDPI::ScaleF(120.0f), (float)m_titleBarHeight);
            rt->DrawText(m_text.c_str(), (UINT32)m_text.length(), fmt, textRect, textBrush); textBrush->Release();
        } fmt->Release();
    }
    float btnW = (float)ExDPI::Scale(46);
    if (m_exStyle & GPMWND_STYLE_CLOSE) { D2D1_RECT_F rcClose = D2D1::RectF(w - btnW, 0, w, (float)m_titleBarHeight); DrawCloseButton(rt, rcClose, m_hoverBtn == HIT_CLOSE, m_downBtn == HIT_CLOSE); }
    if (m_exStyle & GPMWND_STYLE_MINIMIZE) { float offset = (m_exStyle & GPMWND_STYLE_CLOSE) ? btnW * 2 : btnW; D2D1_RECT_F rcMin = D2D1::RectF(w - offset, 0, w - offset + btnW, (float)m_titleBarHeight); DrawMinButton(rt, rcMin, m_hoverBtn == HIT_MIN); }
}

void GpmWindow::DrawCloseButton(ID2D1RenderTarget* rt, D2D1_RECT_F rc, bool hover, bool down) {
    auto& t = Theme();
    float radius = (float)ExDPI::Scale(m_windowCornerRadius);
    if (hover) {
        COLORREF c = down ? RGB(200, 40, 40) : t.closeHover;
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(c), &br);
        if (br) {
            if (radius <= 0) {
                D2D1_ROUNDED_RECT hoverRc = D2D1::RoundedRect(
                    D2D1::RectF(rc.left + ExDPI::ScaleF(4.0f), rc.top + ExDPI::ScaleF(4.0f),
                        rc.right - ExDPI::ScaleF(4.0f), rc.bottom - ExDPI::ScaleF(4.0f)),
                    ExDPI::ScaleF(5.0f), ExDPI::ScaleF(5.0f));
                rt->FillRoundedRectangle(&hoverRc, br);
            } else {
                // 使用圆角路径，右上角匹配窗口圆角
                ID2D1PathGeometry* btnGeo = nullptr;
                ExD2DFactory::GetFactory()->CreatePathGeometry(&btnGeo);
                if (btnGeo) {
                    ID2D1GeometrySink* sink = nullptr;
                    btnGeo->Open(&sink);
                    if (sink) {
                        float r = (std::min)(radius, rc.bottom - rc.top);
                        sink->BeginFigure(D2D1::Point2F(rc.left, rc.top), D2D1_FIGURE_BEGIN_FILLED);
                        sink->AddLine(D2D1::Point2F(rc.right - r, rc.top));
                        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(rc.right, rc.top + r), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                        sink->AddLine(D2D1::Point2F(rc.right, rc.bottom));
                        sink->AddLine(D2D1::Point2F(rc.left, rc.bottom));
                        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                        sink->Close(); sink->Release();
                    }
                    rt->FillGeometry(btnGeo, br);
                    btnGeo->Release();
                }
            }
            br->Release();
        }
    }
    float cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2, s = ExDPI::ScaleF(5.0f);
    ID2D1SolidColorBrush* pen = nullptr; rt->CreateSolidColorBrush(D2D1::ColorF(0.94f, 0.94f, 0.94f), &pen);
    if (pen) { rt->DrawLine(D2D1::Point2F(cx - s, cy - s), D2D1::Point2F(cx + s, cy + s), pen, ExDPI::ScaleF(1.5f)); rt->DrawLine(D2D1::Point2F(cx + s, cy - s), D2D1::Point2F(cx - s, cy + s), pen, ExDPI::ScaleF(1.5f)); pen->Release(); }
}

void GpmWindow::DrawMinButton(ID2D1RenderTarget* rt, D2D1_RECT_F rc, bool hover) {
    auto& t = Theme();
    if (hover) {
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(t.bgHover), &br);
        if (br) {
            D2D1_ROUNDED_RECT hoverRc = D2D1::RoundedRect(
                D2D1::RectF(rc.left + ExDPI::ScaleF(4.0f), rc.top + ExDPI::ScaleF(4.0f),
                    rc.right - ExDPI::ScaleF(4.0f), rc.bottom - ExDPI::ScaleF(4.0f)),
                ExDPI::ScaleF(5.0f), ExDPI::ScaleF(5.0f));
            rt->FillRoundedRectangle(&hoverRc, br);
            br->Release();
        }
    }
    float cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2, s = ExDPI::ScaleF(5.0f);
    ID2D1SolidColorBrush* pen = nullptr; rt->CreateSolidColorBrush(D2D1::ColorF(0.94f, 0.94f, 0.94f), &pen);
    if (pen) { rt->DrawLine(D2D1::Point2F(cx - s, cy + 1), D2D1::Point2F(cx + s, cy + 1), pen, ExDPI::ScaleF(1.5f)); pen->Release(); }
}

int GpmWindow::HitTestTitleButton(int x, int y) {
    if (y < 0 || y > m_titleBarHeight) return HIT_NONE;
    RECT rcClient; ::GetClientRect(m_hWnd, &rcClient); int w = rcClient.right, btnW = ExDPI::Scale(46);
    if ((m_exStyle & GPMWND_STYLE_CLOSE) && x >= w - btnW && x < w) return HIT_CLOSE;
    if (m_exStyle & GPMWND_STYLE_MINIMIZE) { int offset = (m_exStyle & GPMWND_STYLE_CLOSE) ? btnW * 2 : btnW; if (x >= w - offset && x < w - offset + btnW) return HIT_MIN; }
    return HIT_NONE;
}

LRESULT CALLBACK GpmWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    GpmWindow* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<GpmWindow*>(pCreate->lpCreateParams);
        ::SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hWnd = hWnd;
    } else { pThis = reinterpret_cast<GpmWindow*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA)); }
    if (pThis) return pThis->HandleMessage(msg, wParam, lParam);
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}

LRESULT GpmWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; ::BeginPaint(m_hWnd, &ps); ::EndPaint(m_hWnd, &ps);
        if (!m_painting) DoPaint();
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt; ::GetCursorPos(&pt); ::ScreenToClient(m_hWnd, &pt);
            ResizeDir dir = HitTestResizeBorder(pt.x, pt.y);
            if (dir != RSZ_NONE) { UpdateResizeCursor(dir); return TRUE; }
        } return ::DefWindowProc(m_hWnd, msg, wParam, lParam);
    }
    case WM_MOUSEMOVE: {
        int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 }; ::TrackMouseEvent(&tme);
        if (m_dragging && (m_exStyle & GPMWND_STYLE_MOVEABLE)) {
            POINT ptCur; ::GetCursorPos(&ptCur); RECT rcWnd; ::GetWindowRect(m_hWnd, &rcWnd);
            ::SetWindowPos(m_hWnd, NULL, rcWnd.left + ptCur.x - m_dragStart.x, rcWnd.top + ptCur.y - m_dragStart.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            ::GetWindowRect(m_hWnd, &rcWnd);
            m_x = rcWnd.left;
            m_y = rcWnd.top;
            m_dragStart = ptCur;
            // 通知控件父窗口已移动
            for (auto* ctrl : m_controls) {
                if (ctrl) ctrl->OnWindowMoved();
            }
            return 0;
        }
        if (m_resizing) { POINT ptScreen; ::GetCursorPos(&ptScreen); PerformResize(ptScreen.x, ptScreen.y); return 0; }
        int oldHover = m_hoverBtn; m_hoverBtn = HitTestTitleButton(mx, my);
        if (oldHover != m_hoverBtn && !m_painting) ::InvalidateRect(m_hWnd, nullptr, FALSE);
        if (m_captureCtrl) {
            m_captureCtrl->OnMouseMove(mx, my);
            if (!m_painting) ::InvalidateRect(m_hWnd, nullptr, FALSE);
        } else {
            // 未捕获时，通过HitTest查找鼠标下方的控件，分发悬停/离开事件
            UIElement* hoverCtrl = nullptr;
            for (auto* ctrl : m_controls) {
                if (ctrl && ctrl->IsVisible() && ctrl->IsEnabled()) {
                    RECT rc; ctrl->GetRect(rc);
                    if (mx >= rc.left && mx <= rc.right && my >= rc.top && my <= rc.bottom) {
                        hoverCtrl = ctrl;
                        break;
                    }
                }
            }
            if (hoverCtrl != m_lastHoverCtrl) {
                if (m_lastHoverCtrl) {
                    m_lastHoverCtrl->OnMouseLeave();
                }
                if (hoverCtrl) {
                    hoverCtrl->OnMouseMove(mx, my);
                }
                m_lastHoverCtrl = hoverCtrl;
                if (!m_painting) ::InvalidateRect(m_hWnd, nullptr, FALSE);
            } else if (hoverCtrl) {
                hoverCtrl->OnMouseMove(mx, my);
                if (!m_painting) ::InvalidateRect(m_hWnd, nullptr, FALSE);
            }
        }
        return 0;
    }
    case WM_MOUSELEAVE: {
        m_hoverBtn = HIT_NONE;
        // 拖拽选择时不清除 captureCtrl，确保鼠标移出窗口后仍能接收消息
        bool isDraggingSel = false;
        if (m_captureCtrl) {
            isDraggingSel = (GetKeyState(VK_LBUTTON) & 0x8000) != 0;
            if (!isDraggingSel) {
                m_captureCtrl->OnMouseLeave();
                m_captureCtrl = nullptr;
            }
        }
        if (!isDraggingSel) {
            // 使用 m_lastHoverCtrl 精确发送离开事件，避免重复发送
            if (m_lastHoverCtrl) {
                m_lastHoverCtrl->OnMouseLeave();
                m_lastHoverCtrl = nullptr;
            }
        }
        if (!m_painting) ::InvalidateRect(m_hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
        ::SetCapture(m_hWnd);
        // 尺寸调节优先级最高（仅6px边缘区域），防止与按钮/标题栏重叠
        ResizeDir dir = HitTestResizeBorder(mx, my);
        if (dir != RSZ_NONE && (m_exStyle & GPMWND_STYLE_SIZEABLE)) {
            m_resizing = true; m_resizeDir = dir;
            ::GetWindowRect(m_hWnd, &m_resizeStartRect);
            ::GetCursorPos(&m_resizeStartPoint);
            return 0;
        }
        int hit = HitTestTitleButton(mx, my);
        if (hit == HIT_CLOSE || hit == HIT_MIN) {
            m_downBtn = hit;
            if (!m_painting) ::InvalidateRect(m_hWnd, nullptr, FALSE);
            return 0;
        }
        if (my >= 0 && my <= m_titleBarHeight && (m_exStyle & GPMWND_STYLE_MOVEABLE)) {
            m_dragging = true;
            POINT pt; ::GetCursorPos(&pt);
            m_dragStart = pt;
            return 0;
        }
        UIElement* hitCtrl = nullptr;
        for (auto* ctrl : m_controls) {
            if (ctrl && ctrl->IsVisible() && ctrl->IsEnabled()) {
                RECT rc; ctrl->GetRect(rc);
                if (mx >= rc.left && mx <= rc.right && my >= rc.top && my <= rc.bottom) {
                    hitCtrl = ctrl;
                    break;
                }
            }
        }
        if (hitCtrl != m_focusCtrl) {
            if (m_focusCtrl) {
                m_focusCtrl->OnKillFocus();
                m_focusCtrl = nullptr;
            }
        }
        if (hitCtrl) {
            m_captureCtrl = hitCtrl;
            hitCtrl->OnLButtonDown(mx, my);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        ::ReleaseCapture();
        if (m_dragging) { m_dragging = false; return 0; }
        if (m_resizing) { m_resizing = false; return 0; }
        int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
        if (m_downBtn == HIT_CLOSE) { m_downBtn = HIT_NONE; ::PostQuitMessage(0); return 0; }
        if (m_downBtn == HIT_MIN) { m_downBtn = HIT_NONE; ::ShowWindow(m_hWnd, SW_MINIMIZE); return 0; }
        m_downBtn = HIT_NONE;
        if (m_captureCtrl) { m_captureCtrl->OnLButtonUp(mx, my); m_captureCtrl = nullptr; }
        if (!m_painting) ::InvalidateRect(m_hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
        for (auto* ctrl : m_controls) {
            if (ctrl && ctrl->IsVisible() && ctrl->IsEnabled()) {
                RECT rc; ctrl->GetRect(rc);
                if (mx >= rc.left && mx <= rc.right && my >= rc.top && my <= rc.bottom) {
                    ctrl->OnLButtonDblClk(mx, my);
                    break;
                }
            }
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
        int delta = (short)HIWORD(wParam);
        POINT pt = { mx, my };
        ::ScreenToClient(m_hWnd, &pt);
        mx = pt.x; my = pt.y;
        for (auto* ctrl : m_controls) {
            if (ctrl && ctrl->IsVisible() && ctrl->IsEnabled()) {
                RECT rc; ctrl->GetRect(rc);
                if (mx >= rc.left && mx <= rc.right && my >= rc.top && my <= rc.bottom) {
                    ctrl->OnMouseWheel(mx, my, delta);
                    break;
                }
            }
        }
        return 0;
    }
    case WM_KEYDOWN: {
        if (m_focusCtrl) { m_focusCtrl->OnKeyDown((UINT)wParam); return 0; }
        return 0;
    }
    case WM_KEYUP: {
        if (m_focusCtrl) { m_focusCtrl->OnKeyUp((UINT)wParam); return 0; }
        return 0;
    }
    case WM_CHAR: {
        if (m_focusCtrl) { m_focusCtrl->OnChar((wchar_t)wParam); return 0; }
        return 0;
    }
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    case WM_IME_SETCONTEXT:
    case WM_IME_NOTIFY:
        // 所有IME消息必须交给 DefWindowProc 处理
        return ::DefWindowProc(m_hWnd, msg, wParam, lParam);
    case WM_SIZE: {
        m_width = LOWORD(lParam); m_height = HIWORD(lParam);
        if (!m_painting) ::InvalidateRect(m_hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_SETFOCUS: {
        if (m_focusCtrl) m_focusCtrl->OnSetFocus();
        return 0;
    }
    case WM_KILLFOCUS: {
        if (m_focusCtrl) m_focusCtrl->OnKillFocus();
        return 0;
    }
    case WM_DESTROY: {
        ::PostQuitMessage(0);
        return 0;
    }
    case WM_CLOSE: {
        ::DestroyWindow(m_hWnd);
        return 0;
    }
    default:
        return ::DefWindowProc(m_hWnd, msg, wParam, lParam);
    }
    return 0;
}

} // namespace gpm_ui
