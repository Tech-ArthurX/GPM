/*
 * GpmApp.cpp - 应用程序初始化 (D2D + DPI + 全局字体)
 */
#include "gpm_ui.h"

namespace gpm_ui {

GpmApp::GpmApp() : m_inited(false) {}
GpmApp::~GpmApp() { UnInit(); }

bool GpmApp::Init(const std::wstring& fontName, float fontSize) {
    if (m_inited) return true;
    ExDPI::Init();
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ExFont::SetGlobalFont(fontName);
    ExFont::SetDefaultFontSize(fontSize);
    if (!ExD2DFactory::Init()) return false;
    m_inited = true;
    return true;
}

void GpmApp::UnInit() {
    if (m_inited) {
        ExD2DFactory::UnInit();
        CoUninitialize();
        m_inited = false;
    }
}

} // namespace gpm_ui
