#ifndef WIN_SWITCHER_QTWIN_H
#define WIN_SWITCHER_QTWIN_H

#include <QWidget>

namespace QtWin {
    void taskbarDeleteTab(QWidget* window);
    QPixmap fromHICON(HICON icon);

    // Windows 11 Mica Alt backdrop used by long-lived app surfaces such as settings.
    bool applyMicaAlt(QWidget* window, bool darkMode);

    // Windows 11 system-drawn transient backdrop. Desktop Acrylic maps to
    // DWMSBT_TRANSIENTWINDOW and matches the shell's short-lived switcher surfaces.
    bool applySwitcherBackdrop(QWidget* window, bool darkMode);
} // namespace QtWin

#endif // WIN_SWITCHER_QTWIN_H
