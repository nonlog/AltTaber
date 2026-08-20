#ifndef WIN_SWITCHER_QTWIN_H
#define WIN_SWITCHER_QTWIN_H

#include <QWidget>

namespace QtWin {
    void taskbarDeleteTab(QWidget* window);
    QPixmap fromHICON(HICON icon);

    // Windows 11 system-drawn opaque backdrop. Mica Alt maps to DWMSBT_TABBEDWINDOW.
    bool applyMicaAlt(QWidget* window, bool darkMode);
} // namespace QtWin

#endif // WIN_SWITCHER_QTWIN_H
