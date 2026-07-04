/*
 * GpmImageButton.cpp - 图片按钮控件 (D2D渲染 + ComPtr)
 * ImGui风格：三态图片切换，悬停高亮
 */
#include "../core/gpm_ui.h"

#ifdef GPMUI_ENABLE_IMAGEBUTTON

namespace gpm_ui {

GpmImageButton::GpmImageButton() { ApplyTheme(); }
GpmImageButton::~GpmImageButton() { ReleaseBitmaps(); }

void GpmImageButton::ApplyTheme() {
    m_style.ApplyTheme_Button();
}

void GpmImageButton::Create(GpmWindow* parent, int x, int y, int w, int h, int id) {
    m_parentWnd = parent;
    m_hWnd = parent ? parent->GetWindowHandle() : NULL;
    m_x = ExDPI::Scale(x); m_y = ExDPI::Scale(y);
    m_width = ExDPI::Scale(w); m_height = ExDPI::Scale(h);
    m_id = id;
    m_state = STATE_NORMAL;
    if (parent) parent->AddControl(this);
}

bool GpmImageButton::SetImageFromFile(ControlState state, const std::wstring& filePath) {
    m_imagePaths[state] = filePath;
    m_bitmaps[state].Reset();
    m_memoryData[state] = {};
    Invalidate();
    return true;
}

bool GpmImageButton::SetImageFromMemory(ControlState state, const void* data, size_t size) {
    m_memoryData[state] = {data, size};
    m_imagePaths[state].clear();
    // 释放已有位图（标记为需要重新加载）
    m_bitmaps[state].Reset();
    Invalidate();
    return true;
}

ID2D1Bitmap* GpmImageButton::LoadBitmapFromFile(ID2D1RenderTarget* rt, const std::wstring& path) {
    if (path.empty() || !rt) return nullptr;
    
    IWICImagingFactory* wicFactory = ExD2DFactory::GetWICFactory();
    if (!wicFactory) return nullptr;

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = wicFactory->CreateDecoderFromFilename(
        path.c_str(), NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr) || !decoder) return nullptr;

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) { decoder->Release(); return nullptr; }

    IWICFormatConverter* converter = nullptr;
    hr = wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) { frame->Release(); decoder->Release(); return nullptr; }

    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr)) { converter->Release(); frame->Release(); decoder->Release(); return nullptr; }

    ID2D1Bitmap* bitmap = nullptr;
    hr = rt->CreateBitmapFromWicBitmap(converter, NULL, &bitmap);
    
    converter->Release();
    frame->Release();
    decoder->Release();
    return SUCCEEDED(hr) ? bitmap : nullptr;
}

ID2D1Bitmap* GpmImageButton::LoadBitmapFromMemory(ID2D1RenderTarget* rt, const void* data, size_t size) {
    if (!data || !size || !rt) return nullptr;

    IWICImagingFactory* wicFactory = ExD2DFactory::GetWICFactory();
    if (!wicFactory) return nullptr;

    IWICStream* stream = nullptr;
    HRESULT hr = wicFactory->CreateStream(&stream);
    if (FAILED(hr) || !stream) return nullptr;

    hr = stream->InitializeFromMemory((BYTE*)data, (DWORD)size);
    if (FAILED(hr)) { stream->Release(); return nullptr; }

    IWICBitmapDecoder* decoder = nullptr;
    hr = wicFactory->CreateDecoderFromStream(stream, NULL, WICDecodeMetadataCacheOnLoad, &decoder);
    stream->Release();
    if (FAILED(hr) || !decoder) return nullptr;

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) { decoder->Release(); return nullptr; }

    IWICFormatConverter* converter = nullptr;
    hr = wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) { frame->Release(); decoder->Release(); return nullptr; }

    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr)) { converter->Release(); frame->Release(); decoder->Release(); return nullptr; }

    ID2D1Bitmap* bitmap = nullptr;
    hr = rt->CreateBitmapFromWicBitmap(converter, NULL, &bitmap);
    
    converter->Release();
    frame->Release();
    decoder->Release();
    return SUCCEEDED(hr) ? bitmap : nullptr;
}

ID2D1Bitmap* GpmImageButton::GetCurrentBitmap() const {
    // m_bitmaps[STATE_DISABLE] takes priority if disabled
    if (m_state == STATE_DISABLE && m_bitmaps[STATE_DISABLE])
        return m_bitmaps[STATE_DISABLE].Get();
    if (m_state == STATE_DOWN && m_bitmaps[STATE_DOWN])
        return m_bitmaps[STATE_DOWN].Get();
    if (m_state == STATE_HOVER && m_bitmaps[STATE_HOVER])
        return m_bitmaps[STATE_HOVER].Get();
    if (m_bitmaps[STATE_NORMAL])
        return m_bitmaps[STATE_NORMAL].Get();
    return nullptr;
}

void GpmImageButton::ReleaseBitmaps() {
    for (int i = 0; i < 4; i++) {
        m_bitmaps[i].Reset();
    }
}

void GpmImageButton::OnPaintD2D(ID2D1RenderTarget* rt, D2D1_RECT_F rc) {
    if (!m_visible || !rt) return;

    float x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;

    // 背景
    COLORREF bkC = m_state == STATE_DISABLE ? Theme().bgDisabled : m_style.bgColors.Get(m_state);
    ID2D1SolidColorBrush* brush = nullptr;
    rt->CreateSolidColorBrush(ColorRefToD2D(bkC, m_style.opacity), &brush);
    if (brush) {
        if (m_style.cornerRadius > 0) {
            D2D1_ROUNDED_RECT rr = MakeRoundRect(x, y, w, h, (float)m_style.cornerRadius);
            rt->FillRoundedRectangle(&rr, brush);
        } else {
            rt->FillRectangle(D2D1::RectF(x, y, x + w, y + h), brush);
        }
        brush->Release();
    }

    // 加载位图 (延迟加载)
    for (int i = 0; i < 4; i++) {
        if (!m_bitmaps[i]) {
            if (!m_imagePaths[i].empty()) {
                ID2D1Bitmap* bmp = LoadBitmapFromFile(rt, m_imagePaths[i]);
                if (bmp) m_bitmaps[i].Attach(bmp);
            } else if (m_memoryData[i].first) {
                ID2D1Bitmap* bmp = LoadBitmapFromMemory(rt, m_memoryData[i].first, m_memoryData[i].second);
                if (bmp) m_bitmaps[i].Attach(bmp);
            }
        }
    }

    // 绘制图片
    ID2D1Bitmap* bmp = GetCurrentBitmap();
    if (bmp) {
        D2D1_SIZE_F bmpSize = bmp->GetSize();
        float scaleX = w / bmpSize.width;
        float scaleY = h / bmpSize.height;
        float scale = (std::min)(scaleX, scaleY);
        float drawW = bmpSize.width * scale;
        float drawH = bmpSize.height * scale;
        float drawX = x + (w - drawW) / 2;
        float drawY = y + (h - drawH) / 2;

        rt->DrawBitmap(bmp, D2D1::RectF(drawX, drawY, drawX + drawW, drawY + drawH),
                       m_style.opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
}

void GpmImageButton::OnMouseMove(int x, int y) {
    if (!m_enabled) return;
    if (m_state != STATE_DOWN && m_state != STATE_HOVER) { m_state = STATE_HOVER; Invalidate(); }
}

void GpmImageButton::OnLButtonDown(int x, int y) {
    if (m_enabled) { m_state = STATE_DOWN; Invalidate(); }
}

void GpmImageButton::OnLButtonUp(int x, int y) {
    if (!m_enabled) return;
    if (m_state == STATE_DOWN) {
        m_state = STATE_HOVER; Invalidate();
        if (m_clickCb) m_clickCb(this, m_id);
    }
}

void GpmImageButton::OnMouseLeave() {
    if (m_state != STATE_NORMAL) { m_state = STATE_NORMAL; Invalidate(); }
}

} // namespace gpm_ui

#endif // GPMUI_ENABLE_IMAGEBUTTON
