#include <shobjidl.h>
#include "utils/QtWin.h"
#include <windows.h>
#include <dwmapi.h>
#include <QtDebug>

namespace QtWin {
    /// internal
    ITaskbarList3* qt_createITaskbarList3() { // 每个线程统一进行COM初始化，不在特定函数中进行
        ITaskbarList3* pTbList = nullptr;
        HRESULT result = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskbarList3,
                                          reinterpret_cast<void**>(&pTbList));
        if (SUCCEEDED(result)) {
            if (FAILED(pTbList->HrInit())) {
                pTbList->Release();
                pTbList = nullptr;
            }
        }
        return pTbList;
    }

    void taskbarDeleteTab(QWidget* window) {
        ITaskbarList* pTbList = qt_createITaskbarList3();
        if (pTbList) {
            pTbList->DeleteTab(reinterpret_cast<HWND>(window->winId()));
            pTbList->Release();
        }
    }

    /// new implementation for Qt6
    QPixmap fromHICON(HICON icon) {
        return QPixmap::fromImage(QImage::fromHICON(icon));
    }

    static bool applySystemBackdrop(QWidget* window, bool darkMode,
                                    DWM_SYSTEMBACKDROP_TYPE backdrop) {
        if (!window)
            return false;
        const HWND hwnd = reinterpret_cast<HWND>(window->winId());
        if (!hwnd)
            return false;

        const BOOL dark = darkMode ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

        const HRESULT backdropHr = DwmSetWindowAttribute(
            hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

        const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

        // Extend DWM rendering through the full client area. The top-level Qt window stays
        // non-layered/non-translucent; DWM itself paints the selected system material.
        const MARGINS margins{-1, -1, -1, -1};
        DwmExtendFrameIntoClientArea(hwnd, &margins);
        return SUCCEEDED(backdropHr);
    }

    bool applyMicaAlt(QWidget* window, bool darkMode) {
        return applySystemBackdrop(window, darkMode, DWMSBT_TABBEDWINDOW);
    }

    bool applySwitcherBackdrop(QWidget* window, bool darkMode) {
        // Alt+Tab is a transient shell surface. Windows maps TRANSIENTWINDOW to
        // Desktop Acrylic, whereas TABBEDWINDOW is Mica Alt for long-lived tabbed UI.
        return applySystemBackdrop(window, darkMode, DWMSBT_TRANSIENTWINDOW);
    }
} // QtWin
