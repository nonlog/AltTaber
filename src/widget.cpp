#include "../header/widget.h"
#include "ui_Widget.h"
#include "utils/Util.h"
#include <QDebug>
#include <QWindow>
#include <QScreen>
#include <QPainter>
#include <QPen>
#include <QDateTime>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QFontMetrics>
#include <QHideEvent>
#include <QMouseEvent>
#include <QStyleHints>
#include <QSettings>
#include <QFrame>
#include <QtMath>
#include <limits>
#include <algorithm>
#include "utils/QtWin.h"
#include <QWheelEvent>
#include <QTimer>
#include <QMetaEnum>
#include "utils/SystemTray.h"
#include "utils/ConfigManager.h"

namespace {
    constexpr int PreviewAvailableRole = Qt::UserRole + 1;
    constexpr int CloseButtonPadding = 7;
    constexpr int DesiredCardWidth = 240;
    constexpr int DesiredCardHeight = 160;
    constexpr int MinimumCardWidth = 150;
    constexpr int MinimumCardHeight = 100;
    constexpr int CardInset = 4;
    constexpr int PreviewInset = 8;

    bool useDarkPalette() {
#ifdef Q_OS_WIN
        // Qt can report a stale/incorrect color scheme for this translucent native window.
        // Windows' AppsUseLightTheme is the source of truth used by the shell UI.
        QSettings personalize(
            R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)",
            QSettings::NativeFormat);
        return personalize.value("AppsUseLightTheme", 1).toInt() == 0;
#else
        return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#endif
    }

    int titleHeightForCard(const QRect& card) {
        return qBound(30, card.height() / 5, 38);
    }

    QRect cardRectForOption(const QStyleOptionViewItem& option) {
        return option.rect.adjusted(CardInset, CardInset, -CardInset, -CardInset);
    }

    QRect previewRectForOption(const QStyleOptionViewItem& option) {
        const auto card = cardRectForOption(option);
        const int titleHeight = titleHeightForCard(card);
        return card.adjusted(PreviewInset, titleHeight + 3, -PreviewInset, -PreviewInset);
    }

    QRect closeButtonRectForOption(const QStyleOptionViewItem& option) {
        const auto card = cardRectForOption(option);
        const int titleHeight = titleHeightForCard(card);
        const int side = qBound(22, titleHeight - 7, 28);
        return QRect(card.right() - side - CloseButtonPadding + 1,
                     card.top() + (titleHeight - side) / 2,
                     side, side);
    }

    class WindowThumbnailDelegate final : public QStyledItemDelegate {
    public:
        using QStyledItemDelegate::QStyledItemDelegate;

        void paint(QPainter* painter, const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);

            const bool dark = useDarkPalette();
            const bool selected = option.state & QStyle::State_Selected;
            const auto card = cardRectForOption(option);
            const auto preview = previewRectForOption(option);
            const auto closeButton = closeButtonRectForOption(option);
            const int titleHeight = titleHeightForCard(card);
            const bool hovered = option.state & QStyle::State_MouseOver;

            const QColor cardFill = dark ? QColor(48, 48, 48, 232) : QColor(255, 255, 255, 226);
            const QColor selectedFill = dark ? QColor(58, 58, 58, 244) : QColor(255, 255, 255, 246);
            const QColor border = dark ? QColor(255, 255, 255, 34) : QColor(0, 0, 0, 28);
            const QColor selectedBorder = dark ? QColor(232, 232, 232, 220) : QColor(74, 74, 74, 210);
            const QColor previewFill = dark ? QColor(20, 20, 20, 210) : QColor(235, 235, 235, 235);
            const QColor textColor = dark ? QColor(247, 247, 247) : QColor(32, 32, 32);

            QPen cardPen(selected ? selectedBorder : border);
            cardPen.setWidthF(selected ? 2.0 : 1.0);
            painter->setPen(cardPen);
            painter->setBrush(selected ? selectedFill : cardFill);
            painter->drawRoundedRect(card, 10, 10);

            const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
            const int iconSize = qBound(18, titleHeight - 14, 22);
            QRect iconRect(card.left() + 11, card.top() + (titleHeight - iconSize) / 2, iconSize, iconSize);
            if (!icon.isNull())
                icon.paint(painter, iconRect, Qt::AlignCenter, QIcon::Normal);

            const int textRight = (selected || hovered) ? closeButton.left() - 7 : card.right() - 10;
            QRect textRect(iconRect.right() + 8, card.top(),
                           qMax(10, textRight - iconRect.right() - 8),
                           titleHeight);
            auto font = option.font;
            if (font.pointSizeF() < 9.0)
                font.setPointSizeF(9.0);
            painter->setFont(font);
            painter->setPen(textColor);
            const QFontMetrics fm(font);
            const auto title = fm.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight,
                                             textRect.width());
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, title);

            // Windows 11 exposes a close button on the active/hovered Alt+Tab card.
            if (selected || hovered) {
                const QColor closeFill = dark ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 12);
                const QColor closeStroke = dark ? QColor(245, 245, 245) : QColor(45, 45, 45);
                painter->setPen(Qt::NoPen);
                painter->setBrush(closeFill);
                painter->drawRoundedRect(closeButton, 5, 5);

                painter->setPen(QPen(closeStroke, 1.35, Qt::SolidLine, Qt::RoundCap));
                const QPoint c = closeButton.center();
                const int d = qBound(4, closeButton.width() / 5, 5);
                painter->drawLine(c + QPoint(-d, -d), c + QPoint(d, d));
                painter->drawLine(c + QPoint(d, -d), c + QPoint(-d, d));
            }

            painter->setPen(QPen(dark ? QColor(255, 255, 255, 24) : QColor(0, 0, 0, 20), 1));
            painter->setBrush(previewFill);
            painter->drawRoundedRect(preview, 6, 6);

            if (!index.data(PreviewAvailableRole).toBool() && !icon.isNull()) {
                const int side = qBound(34, qMin(preview.width(), preview.height()) / 2, 56);
                QRect fallback(QPoint(), QSize(side, side));
                fallback.moveCenter(preview.center());
                icon.paint(painter, fallback, Qt::AlignCenter, QIcon::Normal);
            }

            painter->restore();
        }

        QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const override {
            const QSize hint = index.data(Qt::SizeHintRole).toSize();
            return hint.isValid() ? hint : QSize(DesiredCardWidth, DesiredCardHeight);
        }
    };
}
Widget::Widget(QWidget* parent) : QWidget(parent), ui(new Ui::Widget) {
    ui->setupUi(this);
    lw = ui->listWidget;
    setWindowFlag(Qt::WindowStaysOnTopHint);
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground); //设置窗口背景透明 !但是会造成show()时的闪烁 和 绘制延迟(?)
    QtWin::taskbarDeleteTab(this); //删除任务栏图标
    setWindowTitle("AltTaber");

    Util::setWindowRoundCorner(this->hWnd()); // let DWM clip the native window corners
#ifdef Q_OS_WIN
    // The legacy BlurBehind path paints the whole rectangular HWND, so it leaks through
    // the transparent Qt corners. Disable the native 1px border too; Qt paints the shell.
    const COLORREF noBorder = 0xFFFFFFFE; // DWMWA_COLOR_NONE
    DwmSetWindowAttribute(hWnd(), static_cast<DWMWINDOWATTRIBUTE>(34),
                          &noBorder, sizeof(noBorder)); // DWMWA_BORDER_COLOR
#endif

    setupLabelFont();
    ui->label->hide();
    lw->setViewMode(QListView::IconMode);
    lw->setMovement(QListView::Static);
    lw->setFlow(QListView::LeftToRight);
    lw->setWrapping(true);
    lw->setResizeMode(QListView::Adjust);
    lw->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lw->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // QListView's default frame makes viewport() a few pixels narrower than the widget.
    // With an exact N*gridWidth size that silently wraps the last column and clips rows.
    lw->setFrameShape(QFrame::NoFrame);
    lw->setContentsMargins(0, 0, 0, 0);
    lw->setIconSize({22, 22});
    lw->setGridSize({DesiredCardWidth, DesiredCardHeight});
    lw->setUniformItemSizes(true);
    lw->setSpacing(0);
    lw->setStyleSheet(R"(
        QListWidget {
            background-color: transparent;
            border: none;
            outline: none;
        }
        QListWidget::item:selected {
            background: transparent;
        }
    )");
    lw->setItemDelegate(new WindowThumbnailDelegate(lw));
    lw->installEventFilter(this);
    lw->viewport()->installEventFilter(this);
    lw->setMouseTracking(true);
    lw->viewport()->setMouseTracking(true);

    connect(lw, &QListWidget::currentItemChanged, this, [this](QListWidgetItem*, QListWidgetItem*) {
        pendingTargetWindow = nullptr;
    });

    connect(qApp, &QApplication::focusWindowChanged, this, [this](QWindow* focusWindow) {
        if (focusWindow == nullptr) {
            if (!this->underMouse()) // hide when lost focus & mouse outside (means user choose to)
                hide();
            else { // Windows Terminal will do
                qWarning() << "Someone tried to steal focus!";
            }
        }
    });
}

Widget::~Widget() {
    clearThumbnails();
    delete ui;
}

void Widget::keyPressEvent(QKeyEvent* event) {
    auto key = event->key();
    auto modifiers = event->modifiers();
    static const QHash<int, int> VimArrows = {
        {Qt::Key_K, Qt::Key_Up},    // ↑
        {Qt::Key_J, Qt::Key_Down},  // ↓
        {Qt::Key_H, Qt::Key_Left},  // ←
        {Qt::Key_L, Qt::Key_Right}, // →
    };
    if (key == Qt::Key_Tab) { // switch to next or prev
        auto i = lw->currentRow();
        bool isShiftPressed = (modifiers & Qt::ShiftModifier);
        // weird formula, but works (hhh)
        auto index = (i - (2 * isShiftPressed - 1) + lw->count()) % lw->count();
        lw->setCurrentRow(index);
    } else if (key == Qt::Key_Up || key == Qt::Key_Down) {
        if (auto item = lw->currentItem()) {
            auto center = lw->visualItemRect(item).center();
            // 转发映射到WheelEvent
            auto wheelEvent = new QWheelEvent(center, lw->mapToGlobal(center), {},
                                              {key == Qt::Key_Up ? 120 : -120, 0},
                                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::postEvent(lw, wheelEvent);
        }
    } else if (key == Qt::Key_Left || key == Qt::Key_Right) { // 默认情况下 左右键可以切换item 只需要处理边界循环即可
        const int N = lw->count();
        const int i = lw->currentRow();
        if (key == Qt::Key_Left && i == 0)
            lw->setCurrentRow(N - 1);
        else if (key == Qt::Key_Right && i == N - 1)
            lw->setCurrentRow(0);
    } else if (VimArrows.contains(key)) { // map [K J H L] to [↑ ↓ ← →]
        QApplication::postEvent(lw, new QKeyEvent(QEvent::KeyPress, VimArrows.value(key), modifiers));
    }
    QWidget::keyPressEvent(event);
}

bool Widget::forceShow() {
    setWindowOpacity(0.005); // reduce the translucent-window show flash
    showMinimized();
    showNormal();
    setWindowOpacity(1);
    lw->doItemsLayout();
    refreshThumbnails();
    return isForeground();
}
void Widget::setupLabelFont() {
    static auto reloadLabelFontCfg = [this] {
        const QStringList Fonts = {"Microsoft YaHei UI", "Microsoft YaHei", "Consolas"}; // fallback
        auto labelFont = ui->label->font();
        labelFont.setPointSize(cfg.get("label/font_size", 10).toInt());
        auto defaultFF = QStringList{cfg.get("label/font_family", Fonts[0]).toString()};
        labelFont.setFamilies(defaultFF << Fonts.mid(1));
        ui->label->setFont(labelFont);
        qDebug() << labelFont.families();
        qDebug() << "Label Actual Font:" << QFontInfo(labelFont).family();
    };
    reloadLabelFontCfg();

    // auto reload
    connect(&cfg, &ConfigManager::configEdited, this, [] {
        reloadLabelFontCfg();
    });
}

void Widget::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Alt) {
        groupWindowOrder.clear(); // reset temporary grouped-window ordering
        if (this->isVisible()) {
            HWND target = pendingTargetWindow;
            if (!target) {
                if (auto item = lw->currentItem()) {
                    const auto info = item->data(Qt::UserRole).value<WindowInfo>();
                    target = info.hwnd;
                }
            }

            if (target && IsWindow(target)) {
                Util::switchToWindow(target);
                qInfo() << "Switch to" << Util::getWindowTitle(target) << target;
            }

            pendingTargetWindow = nullptr;
            hide(); //! must hide after activating target window
        }
    }
    QWidget::keyReleaseEvent(event);
}
void Widget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const bool dark = useDarkPalette();
    const QColor fill = dark ? QColor(32, 32, 32, 205) : QColor(248, 248, 248, 205);
    const QColor border = dark ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 24);

    painter.setPen(QPen(border, 1));
    painter.setBrush(fill);
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 13, 13);
}

void Widget::hideEvent(QHideEvent* event) {
    clearThumbnails();
    pendingTargetWindow = nullptr;
    QWidget::hideEvent(event);
}

QRect Widget::previewRectForItem(QListWidgetItem* item) const {
    if (!item) return {};
    const auto itemRect = lw->visualItemRect(item);
    if (!itemRect.isValid()) return {};

    QStyleOptionViewItem option;
    option.rect = itemRect;
    const auto previewInViewport = previewRectForOption(option);
    const auto topLeft = lw->viewport()->mapTo(const_cast<Widget*>(this), previewInViewport.topLeft());
    return {topLeft, previewInViewport.size()};
}

void Widget::clearThumbnails() {
    for (auto& thumbnail: thumbnails) {
        if (thumbnail.handle)
            DwmUnregisterThumbnail(thumbnail.handle);
    }
    thumbnails.clear();

    if (!lw) return;
    for (int i = 0; i < lw->count(); ++i) {
        if (auto item = lw->item(i))
            item->setData(PreviewAvailableRole, false);
    }
}

void Widget::refreshThumbnails() {
    clearThumbnails();
    if (!isVisible() || !lw || lw->count() == 0) return;

    RECT nativeClient{};
    GetClientRect(hWnd(), &nativeClient);
    const qreal scaleX = width() > 0 ? qreal(nativeClient.right - nativeClient.left) / qreal(width()) : 1.0;
    const qreal scaleY = height() > 0 ? qreal(nativeClient.bottom - nativeClient.top) / qreal(height()) : 1.0;

    for (int i = 0; i < lw->count(); ++i) {
        auto* item = lw->item(i);
        if (!item) continue;

        const auto info = item->data(Qt::UserRole).value<WindowInfo>();
        if (!info.hwnd || !IsWindow(info.hwnd) || info.hwnd == hWnd()) continue;

        HTHUMBNAIL thumbnail = nullptr;
        HRESULT hr = DwmRegisterThumbnail(hWnd(), info.hwnd, &thumbnail);
        if (FAILED(hr) || !thumbnail) {
            qWarning() << "DwmRegisterThumbnail failed" << hr << info.hwnd << info.title;
            continue;
        }

        auto preview = previewRectForItem(item);
        if (!preview.isValid() || preview.isEmpty()) {
            DwmUnregisterThumbnail(thumbnail);
            continue;
        }

        SIZE sourceSize{};
        if (SUCCEEDED(DwmQueryThumbnailSourceSize(thumbnail, &sourceSize)) &&
            sourceSize.cx > 0 && sourceSize.cy > 0) {
            const qreal scale = qMin(qreal(preview.width()) / qreal(sourceSize.cx),
                                     qreal(preview.height()) / qreal(sourceSize.cy));
            QSize fitted(qMax(1, qRound(sourceSize.cx * scale)),
                         qMax(1, qRound(sourceSize.cy * scale)));
            QRect fittedRect(QPoint(), fitted);
            fittedRect.moveCenter(preview.center());
            preview = fittedRect;
        }

        DWM_THUMBNAIL_PROPERTIES props{};
        props.dwFlags = DWM_TNP_VISIBLE | DWM_TNP_OPACITY |
                        DWM_TNP_RECTDESTINATION | DWM_TNP_SOURCECLIENTAREAONLY;
        props.fVisible = TRUE;
        props.opacity = 255;
        props.fSourceClientAreaOnly = FALSE;
        props.rcDestination = {
            qRound(preview.left() * scaleX),
            qRound(preview.top() * scaleY),
            qRound((preview.right() + 1) * scaleX),
            qRound((preview.bottom() + 1) * scaleY)
        };

        hr = DwmUpdateThumbnailProperties(thumbnail, &props);
        if (FAILED(hr)) {
            qWarning() << "DwmUpdateThumbnailProperties failed" << hr << info.hwnd << info.title;
            DwmUnregisterThumbnail(thumbnail);
            continue;
        }

        thumbnails.append({thumbnail, info.hwnd, item});
        item->setData(PreviewAvailableRole, true);
    }

    lw->viewport()->update();
}
/// 通知前台窗口变化
/// @param hwnd 前台窗口句柄
/// @param source 通知来源, for debug, @b Optional
void Widget::notifyForegroundChanged(HWND hwnd, ForegroundChangeSource source) { // TODO isVisible or AltDown时，关闭前台更新通知
    if (hwnd == this->hWnd()) return;
    // （其实监听前台变化只是为了排序，不需要太精确，可以放松限制）
    // 通过`EVENT_SYSTEM_FOREGROUND`触发时忽略`IsWindowVisible`，因为窗口在创建瞬间可能不可见
    if (!Util::isWindowAcceptable(hwnd, source == WinEvent)) return;
    auto path = Util::getWindowProcessPath(hwnd); // TODO 比较耗时，最好仅在单次show期间缓存，同时避免hwnd复用造成缓存错误
    // TODO 不能让winActiveOrder无限增长，需要定时清理
    winActiveOrder[path].insert(hwnd, QDateTime::currentDateTime());

    auto sourceStr = QMetaEnum::fromType<ForegroundChangeSource>().valueToKey(source);
    qDebug() << qUtf8Printable(QString("*ForeWin changed (%1):").arg(sourceStr)) // qUtf8Printable removes quotes ""
            << Util::getWindowTitle(hwnd) << Util::getClassName(hwnd) << path << Util::getFileDescription(path);
} // TODO 控制面板 和 资源管理器 exe是同一个，如何区分图标

/// collect, filter, and sort individual windows for the Alt+Tab view
QList<WindowInfo> Widget::prepareWindowList() {
    QList<WindowInfo> windows;
    const auto list = Util::listValidWindows();
    const auto foreground = GetForegroundWindow();

    for (auto hwnd: list) {
        if (!hwnd || hwnd == this->hWnd()) continue;

        auto path = Util::getWindowProcessPath(hwnd);
        if (path.isEmpty()) continue;

        WindowInfo info{Util::getWindowTitle(hwnd), Util::getClassName(hwnd), hwnd};
        info.exePath = path;
        info.icon = Util::getCachedIcon(path, hwnd);
        windows.append(info);
    }

    std::stable_sort(windows.begin(), windows.end(), [this, foreground](const WindowInfo& a, const WindowInfo& b) {
        if (a.hwnd == foreground && b.hwnd != foreground) return true;
        if (b.hwnd == foreground && a.hwnd != foreground) return false;

        const auto timeA = winActiveOrder.value(a.exePath).value(a.hwnd);
        const auto timeB = winActiveOrder.value(b.exePath).value(b.hwnd);
        if (timeA.isValid() && timeB.isValid())
            return timeA > timeB;
        if (timeA.isValid() != timeB.isValid())
            return timeA.isValid();
        return false;
    });

    return windows;
}

/// collect, filter, sort Windows for grouped app features
QList<WindowGroup> Widget::prepareWindowGroupList() {
    QMap<QString, WindowGroup> winGroupMap;
    const auto list = Util::listValidWindows();
    for (auto hwnd: list) {
        if (hwnd == this->hWnd()) continue; // skip self
        auto path = Util::getWindowProcessPath(hwnd);
        if (path.isEmpty()) continue; // TODO 可能需要管理员权限
        auto& winGroup = winGroupMap[path];
        if (winGroup.exePath.isEmpty()) { // QIcon::isNull 判断可能不太准（例如空图标）
            winGroup.exePath = path;
            auto icon = Util::getCachedIcon(path, hwnd); // TODO background thread
            if (path.endsWith("QQ\\bin\\QQ.exe", Qt::CaseInsensitive)) { // draw chat partner for classical QQ
                QPixmap overlay = Util::getWindowIcon(hwnd);
                const auto iSize = lw->iconSize();
                QPixmap bgPixmap = icon.pixmap(iSize);
                icon = Util::overlayIcon(bgPixmap, overlay, {{iSize.width() / 2, iSize.height() / 2}, iSize / 2});
            }
            winGroup.icon = icon;
        }
        winGroup.addWindow({Util::getWindowTitle(hwnd), Util::getClassName(hwnd), hwnd});
    }
    auto winGroupList = winGroupMap.values();
    // 按照活跃度排序
    std::sort(winGroupList.begin(), winGroupList.end(), [this](const WindowGroup& a, const WindowGroup& b) {
        auto timeA = getLastValidActiveGroupWindow(a).second;
        auto timeB = getLastValidActiveGroupWindow(b).second;
        if (timeA.isNull() && timeB.isNull()) return false;
        if (timeA.isValid() && timeB.isValid()) return timeA > timeB;
        return timeA.isValid();
    });
    return winGroupList;
}

bool Widget::prepareListWidget() {
    clearThumbnails();
    pendingTargetWindow = nullptr;

    auto windowList = prepareWindowList();
    lw->clear();

    if (windowList.isEmpty())
        return false;

    bool displayOnPrimary = (cfg.getDisplayMonitor() == PrimaryMonitor);
    auto screen = displayOnPrimary ?
                  QGuiApplication::primaryScreen() :
                  QGuiApplication::screenAt(QCursor::pos());
    if (!screen && !displayOnPrimary) {
        qWarning() << "Cursor Screen nullptr! Fallback to primary";
        screen = QApplication::primaryScreen();
    }
    if (!screen) {
        qWarning() << "Screen nullptr!";
        sysTray.showMessage("Error", "Screen nullptr!");
        return false;
    }

    const auto available = screen->availableGeometry();
    const int contentMaxWidth = qMax(MinimumCardWidth, qRound(available.width() * 0.90) - ListWidgetMargin.left() - ListWidgetMargin.right());
    const int contentMaxHeight = qMax(MinimumCardHeight, qRound(available.height() * 0.78) - ListWidgetMargin.top() - ListWidgetMargin.bottom());
    const int count = windowList.size();
    constexpr qreal CardAspect = qreal(DesiredCardWidth) / qreal(DesiredCardHeight);

    int bestRows = 1;
    int bestColumns = count;
    QSize bestCardSize(DesiredCardWidth, DesiredCardHeight);
    qreal bestScore = std::numeric_limits<qreal>::max();

    for (int rows = 1; rows <= count; ++rows) {
        const int columns = (count + rows - 1) / rows;
        const int maxWidth = contentMaxWidth / columns;
        const int maxHeight = contentMaxHeight / rows;
        if (maxWidth <= 0 || maxHeight <= 0)
            continue;

        int cardWidth = qMin(DesiredCardWidth, maxWidth);
        int cardHeight = qRound(cardWidth / CardAspect);
        if (cardHeight > qMin(DesiredCardHeight, maxHeight)) {
            cardHeight = qMin(DesiredCardHeight, maxHeight);
            cardWidth = qRound(cardHeight * CardAspect);
        }

        const int undersize = qMax(0, MinimumCardWidth - cardWidth) +
                              qMax(0, MinimumCardHeight - cardHeight);
        const int emptyCells = rows * columns - count;
        const qreal sizeLoss = (DesiredCardWidth - qMin(cardWidth, DesiredCardWidth)) * 0.75 +
                               (DesiredCardHeight - qMin(cardHeight, DesiredCardHeight)) * 0.45;
        const qreal score = undersize * 20.0 + emptyCells * 180.0 + sizeLoss + rows * 12.0;

        if (score < bestScore) {
            bestScore = score;
            bestRows = rows;
            bestColumns = columns;
            bestCardSize = QSize(qMax(1, cardWidth), qMax(1, cardHeight));
        }
    }

    lw->setGridSize(bestCardSize);
    lw->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lw->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    for (auto& info: windowList) {
        auto title = info.title;
        if (title.isEmpty())
            title = Util::getFileDescription(info.exePath);
        if (title.isEmpty())
            title = "Window";

        auto* item = new QListWidgetItem(info.icon, title);
        item->setData(Qt::UserRole, QVariant::fromValue(info));
        item->setData(PreviewAvailableRole, false);
        item->setSizeHint(bestCardSize);
        lw->addItem(item);
    }

    // +1 prevents a style/DPI rounding edge from making the viewport one pixel too small
    // and moving a whole column to the next row. The frame itself is disabled above.
    lw->setFixedSize(bestColumns * bestCardSize.width() + 1,
                     bestRows * bestCardSize.height() + 1);
    lw->doItemsLayout();

    // Defensive check: every item must actually be inside the viewport. If Qt's style
    // still rounds a grid boundary differently at a fractional DPI, expand only by
    // the exact missing pixels rather than leaving a blank card-sized region.
    QRect laidOutBounds;
    for (int i = 0; i < lw->count(); ++i)
        laidOutBounds = laidOutBounds.united(lw->visualItemRect(lw->item(i)));
    if (laidOutBounds.isValid()) {
        const int missingWidth = qMax(0, laidOutBounds.right() + 1 - lw->viewport()->width());
        const int missingHeight = qMax(0, laidOutBounds.bottom() + 1 - lw->viewport()->height());
        if (missingWidth || missingHeight) {
            lw->setFixedSize(lw->width() + missingWidth, lw->height() + missingHeight);
            lw->doItemsLayout();
        }
    }

    QRect lwRect(QPoint(0, 0), lw->size());
    auto thisRect = lwRect.marginsAdded(ListWidgetMargin);
    thisRect.moveCenter(available.center());
    this->setGeometry(thisRect);

    lwRect.moveCenter(this->rect().center());
    lw->move(lwRect.topLeft());
    lw->doItemsLayout();

    if (lw->count() >= 2) {
        const auto foreground = GetForegroundWindow();
        const bool firstIsForeground = windowList.at(0).hwnd == foreground;
        lw->setCurrentRow(firstIsForeground ? 1 : 0);
    } else {
        lw->setCurrentRow(0);
    }

    qDebug() << "Prepared" << lw->count() << "window thumbnails on" << screen->name()
             << "grid" << bestColumns << "x" << bestRows << "card" << bestCardSize;
    return true;
}

bool Widget::requestShow() { // TODO 当前台是开始菜单（Win）时，会导致显示 但无法操控
    return prepareListWidget() && forceShow();
}

/// Warning: the `HWND` not guarantee to be valid (may be closed)
auto Widget::getLastActiveGroupWindow(const QString& exePath) -> QPair<HWND, QDateTime> {
    auto hwndOrder = winActiveOrder.value(exePath);
    if (hwndOrder.isEmpty()) return {nullptr, QDateTime()};
    // QHash & QMap deref to value(QDateTime) rather than QPair
    auto iter = std::max_element(hwndOrder.begin(), hwndOrder.end());
    return {iter.key(), iter.value()};
}

/// return null if no window recorded in group
auto Widget::getLastValidActiveGroupWindow(const WindowGroup& group) -> QPair<HWND, QDateTime> {
    auto hwndOrder = winActiveOrder.value(group.exePath);
    if (hwndOrder.isEmpty()) return {nullptr, QDateTime()};

    QList<HWND> windows;
    for (auto& info: group.windows)
        windows << info.hwnd;
    sortGroupWindows(windows, group.exePath);

    if (auto time = hwndOrder.value(windows.first()); !time.isNull())
        return {windows.first(), time};
    else // check if the first HWND is recorded
        return {nullptr, QDateTime()};
}

/// sort Windows of [Group specified by exePath], by active order (latest first)
void Widget::sortGroupWindows(QList<HWND>& windows, const QString& exePath) {
    auto activeOrdMap = winActiveOrder.value(exePath);
    if (activeOrdMap.isEmpty()) return;
    // sort by active order
    std::sort(windows.begin(), windows.end(), [&activeOrdMap](HWND a, HWND b) {
        return activeOrdMap.value(a) > activeOrdMap.value(b); // default value if not found
    }); // TODO update winActiveOrder! (remove invalid HWND)
}

/// group by exePath, sort by active order (last active first)
QList<HWND> Widget::buildGroupWindowOrder(const QString& exePath) {
    auto windows = Util::listValidWindows(exePath); // filter by path
    sortGroupWindows(windows, exePath);
    return windows;
}

bool Widget::eventFilter(QObject* watched, QEvent* event) {
    const bool isListSurface = watched == lw || watched == lw->viewport();

    if (isListSurface && event->type() == QEvent::MouseButtonRelease) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        QPoint pos = mouseEvent->position().toPoint();
        if (watched == lw)
            pos = lw->viewport()->mapFrom(lw, pos);

        if (auto* item = lw->itemAt(pos)) {
            const auto info = item->data(Qt::UserRole).value<WindowInfo>();
            if (!info.hwnd || !IsWindow(info.hwnd))
                return true;

            auto closeWindow = [this](HWND hwnd) {
                PostMessage(hwnd, WM_CLOSE, 0, 0);
                QTimer::singleShot(120, this, [this] {
                    if (!isVisible()) return;
                    if (!prepareListWidget()) {
                        hide();
                        return;
                    }
                    lw->doItemsLayout();
                    refreshThumbnails();
                });
            };

            if (mouseEvent->button() == Qt::LeftButton) {
                QStyleOptionViewItem option;
                option.rect = lw->visualItemRect(item);
                if (closeButtonRectForOption(option).contains(pos)) {
                    closeWindow(info.hwnd);
                    return true;
                }

                lw->setCurrentItem(item);
                pendingTargetWindow = nullptr;
                Util::switchToWindow(info.hwnd);
                hide();
                return true;
            }

            if (mouseEvent->button() == Qt::MiddleButton) {
                closeWindow(info.hwnd);
                return true;
            }
        }
    }

    if (isListSurface && event->type() == QEvent::Wheel) {
        auto* wheelEvent = static_cast<QWheelEvent*>(event);
        QPoint cursorPos = wheelEvent->position().toPoint();
        if (watched == lw)
            cursorPos = lw->viewport()->mapFrom(lw, cursorPos);

        if (auto item = lw->itemAt(cursorPos)) {
            if (lw->currentItem() != item)
                lw->setCurrentItem(item);

            const auto windowInfo = item->data(Qt::UserRole).value<WindowInfo>();
            if (!windowInfo.hwnd || windowInfo.exePath.isEmpty()) return false;

            static QListWidgetItem* lastItem = nullptr;
            static HWND hwnd = nullptr;
            if (lastItem != item) {
                lastItem = item;
                hwnd = windowInfo.hwnd;
                groupWindowOrder.clear();
            }

            const auto targetExe = windowInfo.exePath;
            static bool isLastRollUp = true;
            const QPoint delta = wheelEvent->angleDelta();
            const int wheelDelta = qAbs(delta.x()) >= qAbs(delta.y()) ? delta.x() : delta.y();
            const bool isRollUp = wheelDelta > 0;
            if (groupWindowOrder.isEmpty())
                groupWindowOrder = buildGroupWindowOrder(targetExe);
            if (groupWindowOrder.isEmpty()) return false;

            if (!hwnd || !groupWindowOrder.contains(hwnd))
                hwnd = groupWindowOrder.contains(windowInfo.hwnd) ? windowInfo.hwnd : groupWindowOrder.first();
            else if (isLastRollUp == isRollUp)
                hwnd = rotateWindowInGroup(groupWindowOrder, hwnd, isRollUp);
            isLastRollUp = isRollUp;

            HWND nextFocus = hwnd;
            if (isRollUp) {
                Util::bringWindowToTop(hwnd, this->hWnd());
            } else {
                if (auto normal = rotateNormalWindowInGroup(groupWindowOrder, hwnd, false)) {
                    ShowWindow(normal, SW_SHOWMINNOACTIVE);
                    hwnd = normal;
                    nextFocus = hwnd;
                }
                if (auto normal = rotateNormalWindowInGroup(groupWindowOrder, hwnd, false))
                    nextFocus = normal;
            }

            pendingTargetWindow = nextFocus;
            notifyForegroundChanged(nextFocus, Inner);
            item->setToolTip(Util::getWindowTitle(nextFocus));
            qDebug() << "Wheel" << isRollUp << Util::getWindowTitle(nextFocus) << hwnd;
            return true;
        }
    }
    return false;
}

/// `forward`: true for restore, false for minimize
void Widget::rotateTaskbarWindowInGroup(const QString& exePath, bool forward, int windows) {
    qDebug() << "(Taskbar)Wheel on:" << exePath << forward << windows;
    if (exePath.isEmpty()) return;
    if (!windows) { // 程序没有打开的窗口，处于关闭状态; 若不拦截，可能造成错误窗口被触发：explorer.exe -> msedge.exe
        qDebug() << "No window for this app";
        return;
    }

    static QString lastPath;
    static HWND lastHwnd = nullptr;
    if (lastPath != exePath) {
        lastPath = exePath;
        groupWindowOrder.clear();
    }
    if (groupWindowOrder.isEmpty()) {
        groupWindowOrder = buildGroupWindowOrder(exePath);
        lastHwnd = nullptr;
    }

    if (groupWindowOrder.isEmpty()) {
        qCritical() << "No window in group!" << exePath;
        // 有些软件的窗口是由子进程创建的，如 steam.exe -> steamwebhelper.exe (持有窗口)
        // 但是在任务栏只能获取到父进程steam.exe
        // 这种情况下，需要查找其子进程的路径
        auto childPaths = Util::getChildProcessPaths(exePath);
        if (childPaths.isEmpty()) return;
        if (childPaths.size() == 1) {
            qDebug() << "Try to switch to child process:" << childPaths.first();
            groupWindowOrder = buildGroupWindowOrder(childPaths.first());
        } else {
            // 如果有多个子进程路径，就根据validWindows过滤
            qWarning() << "!Multiple child processes:" << childPaths;
            QSet<QString> validPaths;
            // If range-initializer returns a temporary, its lifetime is extended until the end of the loop
            for (auto hwnd: Util::listValidWindows()) {
                if (auto path = Util::getWindowProcessPath(hwnd); !path.isEmpty())
                    validPaths.insert(path.toLower());
            }
            for (auto& path: childPaths) {
                if (validPaths.contains(path.toLower())) {
                    qDebug() << "Try to switch to valid child process:" << path;
                    groupWindowOrder = buildGroupWindowOrder(path);
                    break;
                }
            }
        }
        // TODO 有可能a进程开启b进程之后，a就关闭了，他俩也没有真的父子关系
        //  例如：ksolaunch.exe -> wps.exe
        //  此时只能通过File Description来匹配，均为“WPS Office”
        if (groupWindowOrder.isEmpty()) { // 无力回天
            qCritical() << "もうおしまいだ！";
            return;
        }
    }

    static bool isLastForward = true;
    HWND hwnd = nullptr;
    if (!lastHwnd) {
        hwnd = groupWindowOrder.first();
        if (forward && hwnd == GetForegroundWindow()) // 如果first是前台窗口且forward，则轮换下一个
            hwnd = rotateWindowInGroup(groupWindowOrder, hwnd, true);
    } else {
        if (isLastForward == forward)
            hwnd = rotateWindowInGroup(groupWindowOrder, lastHwnd, forward);
        else
            hwnd = lastHwnd;
    }
    isLastForward = forward;

    if (forward) {
        static auto mouseEvent = [](DWORD flag) {
            mouse_event(flag, 0, 0, 0, 0);
        };
        if (windows == 1) { // 由于过滤的存在，groupWindowOrder.size() 不一定等于 windows(真实窗口数量)
            // 单窗口情况下，模拟点击呼出，是最保险的
            if ((hwnd != GetForegroundWindow() || IsIconic(hwnd))) { // 若采用SW_SHOWMINNOACTIVE, 则前台窗口不会变化，可能为刚刚最小化的窗口
                mouseEvent(MOUSEEVENTF_LEFTDOWN);
                mouseEvent(MOUSEEVENTF_LEFTUP);
                qApp->processEvents();
                qDebug() << "(Taskbar)Switch by click";
            }
        } else {
            // 在TaskListThumbnailWnd显示的情况下restore window会导致预览实时刷新，导致卡顿和闪烁
            // 隐藏TaskListThumbnailWnd也无效，会自动show
            // DwmSetWindowAttribute[DWMWA_FORCE_ICONIC_REPRESENTATION, DWMWA_DISALLOW_PEEK], 效果都不好，还是会刷新闪烁

            // 只能采用偷鸡hack，按住左键的情况下，预览窗口会消失
            if (HWND thumbnail = Util::getCurrentTaskListThumbnailWnd(); IsWindowVisible(thumbnail)) {
                qDebug() << "(Taskbar)#Press LButton";
                mouseEvent(MOUSEEVENTF_LEFTDOWN);
                // 由于本程序hook了mouse，所以必须处理全局鼠标事件（in事件循环）
                QTimer::singleShot(20, this, [hwnd]() {
                    Util::switchToWindow(hwnd, true); // TODO thumbnail隐藏之前 不要switch，并且block滚轮 防止闪烁卡顿
                });
            } else
                Util::switchToWindow(hwnd, true);

            static QTimer* timer = [this]() {
                auto* timer = new QTimer;
                timer->setSingleShot(true);
                timer->setInterval(200);
                // TODO cursor移动后立即释放 防止拖拽
                timer->callOnTimeout(this, [this]() {
                    mouseEvent(MOUSEEVENTF_LEFTUP);
                    qDebug() << "(Taskbar)#Release LButton";

                    // 鼠标点击thumbnail之后，其获取焦点，此时若焦点在其窗口组成员中，thumbnail就不会隐藏，这是Windows机制
                    // 只能通过将焦点转移到Taskbar使其隐藏
                    // 直接 HIDE thumbnail 不太行，会导致之后restore窗口时 thumbnail刷新 + 窗口闪烁，闪瞎了
                    QTimer::singleShot(100, this, []() {
                        // 等待thumbnail显示
                        if (HWND thumbnail = Util::getCurrentTaskListThumbnailWnd(); IsWindowVisible(thumbnail)) {
                            if (HWND taskbar = FindWindow(L"Shell_TrayWnd", nullptr))
                                Util::switchToWindow(taskbar, true);
                        }
                    });
                });
                return timer;
            }();
            timer->stop();
            timer->start();
        }
        qDebug() << "(Taskbar)Switch to" << hwnd << Util::getWindowTitle(hwnd) << Util::getClassName(hwnd);
    } else {
        if (auto normal = rotateNormalWindowInGroup(groupWindowOrder, hwnd, false)) { // skip minimized
            if (normal != hwnd)
                qDebug() << "(Taskbar)Skip minimized" << hwnd << "->" << normal;
            hwnd = normal;
            ShowWindow(hwnd, SW_MINIMIZE); // SW_MINIMIZE 会让焦点自动回落到下一个窗口
            // 当所有窗口隐藏后，getElementUnderMouse() 会变成"CEF-OSC-WIDGET"，但是焦点和前台窗口并不是他，离谱
            // 此时Automation对鼠标下任务栏Element的判定会出错，solution为手动变焦到任务栏（见TaskbarWheelHooker.cpp）
            // SW_SHOWMINNOACTIVE不会切换焦点，即便本窗口已经最小化，但仍然持有焦点；但这不是合理的行为，同时会让QQ Follower反复弹出
            qDebug() << "(Taskbar)Minimize" << hwnd << Util::getWindowTitle(hwnd) << Util::getClassName(hwnd);
        }
    }

    lastHwnd = hwnd;
}

/// select next(forward)(older) or prev window in group<br>
/// Do nothing, but select HWND
HWND Widget::rotateWindowInGroup(const QList<HWND>& windows, HWND current, bool forward) {
    const auto N = windows.size();
    if (N == 1) return windows.first();
    for (int i = 0; i < N; i++) {
        if (windows.at(i) == current) {
            auto next_i = forward ? (i + 1) : (i - 1);
            auto next = windows.at((next_i + N) % N);
            return next;
        }
    }
    return nullptr;
}

/// Select next (including `current`) normal (!minimized) window in group<br>
/// return nullptr if all minimized
HWND Widget::rotateNormalWindowInGroup(const QList<HWND>& windows, HWND current, bool forward) {
    for (int i = 0; IsIconic(current) && i < windows.size(); i++) // skip minimized
        current = rotateWindowInGroup(windows, current, forward);
    return IsIconic(current) ? nullptr : current;
}

void Widget::clearGroupWindowOrder() {
    groupWindowOrder.clear();
}
