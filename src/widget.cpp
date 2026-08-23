#include "../header/widget.h"
#include "ui_Widget.h"
#include "utils/Util.h"
#include <QDebug>
#include <QWindow>
#include <QScreen>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QDateTime>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QFontMetrics>
#include <QHideEvent>
#include <QMouseEvent>
#include <QStyleHints>
#include <QSettings>
#include <QSet>
#include <QFrame>
#include <QVector>
#include <QtMath>
#include <algorithm>
#include "utils/QtWin.h"
#include <QWheelEvent>
#include <QTimer>
#include <QMetaEnum>
#include "utils/SystemTray.h"
#include "utils/ConfigManager.h"

namespace {
    constexpr int PreviewAvailableRole = Qt::UserRole + 1;
    constexpr int SpacerRole = Qt::UserRole + 2;
    constexpr int CloseButtonPadding = 7;
    constexpr int DefaultCardWidth = 320;
    constexpr int DefaultCardHeight = 200;
    constexpr int MinimumCardWidth = 140;
    constexpr int MinimumCardHeight = 108;
    constexpr int CardInset = 6;
    constexpr int PreviewInset = 7;

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

    QColor systemAccentColor() {
#ifdef Q_OS_WIN
        // AccentColorMenu is the user's actual Windows accent color. AccentPalette
        // contains a family of derived shades; picking a fixed palette slot can turn
        // a brown/blue/green system accent into an unrelated purple shade.
        QSettings explorerAccent(
            R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\Accent)",
            QSettings::NativeFormat);
        bool accentOk = false;
        const quint32 accent = explorerAccent.value("AccentColorMenu").toUInt(&accentOk);
        if (accentOk) {
            // Explorer stores this DWORD as AABBGGRR.
            return QColor(accent & 0xFF,
                          (accent >> 8) & 0xFF,
                          (accent >> 16) & 0xFF);
        }

        DWORD colorization = 0;
        BOOL opaqueBlend = FALSE;
        if (SUCCEEDED(DwmGetColorizationColor(&colorization, &opaqueBlend))) {
            Q_UNUSED(opaqueBlend);
            return QColor((colorization >> 16) & 0xFF,
                          (colorization >> 8) & 0xFF,
                          colorization & 0xFF);
        }

        QSettings dwm(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\DWM)",
                      QSettings::NativeFormat);
        bool ok = false;
        const quint32 raw = dwm.value("ColorizationColor").toUInt(&ok);
        if (ok)
            return QColor((raw >> 16) & 0xFF, (raw >> 8) & 0xFF, raw & 0xFF);
#endif
        return QApplication::palette().color(QPalette::Highlight);
    }

    int titleHeightForCard(const QRect& card) {
        return qBound(36, card.height() / 5, 44);
    }

    QSize sourceWindowSize(HWND hwnd) {
#ifdef Q_OS_WIN
        if (!hwnd || !IsWindow(hwnd))
            return {};

        // A minimized Win32 window can report its tiny iconic rectangle (for example
        // 183x31) instead of the restore rectangle. Using that aspect ratio makes the
        // Alt+Tab card and DWM preview look inexplicably ultra-wide. Windows' own
        // switcher uses the normal/restored window shape instead.
        if (IsIconic(hwnd)) {
            WINDOWPLACEMENT placement{};
            placement.length = sizeof(placement);
            if (GetWindowPlacement(hwnd, &placement)) {
                const auto& normal = placement.rcNormalPosition;
                const int width = normal.right - normal.left;
                const int height = normal.bottom - normal.top;
                if (width > 0 && height > 0)
                    return {width, height};
            }
        }

        RECT bounds{};
        if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                                            &bounds, sizeof(bounds)))) {
            const int width = bounds.right - bounds.left;
            const int height = bounds.bottom - bounds.top;
            if (width > 0 && height > 0)
                return {width, height};
        }

        if (GetWindowRect(hwnd, &bounds)) {
            const int width = bounds.right - bounds.left;
            const int height = bounds.bottom - bounds.top;
            if (width > 0 && height > 0)
                return {width, height};
        }
#else
        Q_UNUSED(hwnd);
#endif
        return {};
    }

    int cardWidthForWindow(HWND hwnd, int itemHeight) {
        const auto source = sourceWindowSize(hwnd);
        qreal aspect = source.height() > 0
                           ? qreal(source.width()) / qreal(source.height())
                           : qreal(DefaultCardWidth) / qreal(DefaultCardHeight);
        aspect = qBound<qreal>(0.42, aspect, 3.00);

        const int paintedCardHeight = qMax(1, itemHeight - 2 * CardInset);
        const int titleHeight = titleHeightForCard(QRect(0, 0, 1, paintedCardHeight));
        const int previewHeight = qMax(1, paintedCardHeight - titleHeight - 3 - PreviewInset);
        const int naturalWidth = qRound(aspect * previewHeight)
                                 + 2 * (CardInset + PreviewInset) + 8;
        const int minimumWidth = qMax(MinimumCardWidth, qRound(itemHeight * 0.64));
        const int maximumWidth = qMax(minimumWidth, qRound(itemHeight * 2.25));
        return qBound(minimumWidth, naturalWidth, maximumWidth);
    }

    QList<int> widthBalancedRowCounts(const QList<int>& widths, int rows) {
        const int itemCount = widths.size();
        if (itemCount <= 0)
            return {};

        rows = qBound(1, rows, itemCount);
        QVector<qint64> prefix(itemCount + 1, 0);
        for (int i = 0; i < itemCount; ++i)
            prefix[i + 1] = prefix[i] + widths.at(i);

        constexpr qint64 Inf = (qint64(1) << 60);
        QVector<QVector<qint64>> bestMax(rows + 1,
                                         QVector<qint64>(itemCount + 1, Inf));
        QVector<QVector<qint64>> balancePenalty(rows + 1,
                                                QVector<qint64>(itemCount + 1, Inf));
        QVector<QVector<int>> cut(rows + 1,
                                  QVector<int>(itemCount + 1, -1));
        bestMax[0][0] = 0;
        balancePenalty[0][0] = 0;
        const qint64 totalWidth = prefix[itemCount];

        // Linear partition: preserve Alt+Tab order, but choose row boundaries that
        // minimize the widest row. This mirrors the native shell's width-sensitive
        // wrapping much better than fixed 3+4 / 4+5 rules for odd item counts.
        for (int row = 1; row <= rows; ++row) {
            for (int end = row; end <= itemCount; ++end) {
                for (int start = row - 1; start < end; ++start) {
                    if (bestMax[row - 1][start] == Inf)
                        continue;
                    const qint64 rowWidth = prefix[end] - prefix[start];
                    const qint64 candidateMax = qMax(bestMax[row - 1][start], rowWidth);
                    const qint64 candidatePenalty = balancePenalty[row - 1][start]
                                                    + qAbs(rowWidth * rows - totalWidth);
                    if (candidateMax < bestMax[row][end]
                        || (candidateMax == bestMax[row][end]
                            && candidatePenalty < balancePenalty[row][end])) {
                        bestMax[row][end] = candidateMax;
                        balancePenalty[row][end] = candidatePenalty;
                        cut[row][end] = start;
                    }
                }
            }
        }

        QList<int> counts;
        int end = itemCount;
        for (int row = rows; row >= 1; --row) {
            const int start = cut[row][end];
            if (start < 0)
                return {};
            counts.prepend(end - start);
            end = start;
        }
        return counts;
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
            if (index.data(SpacerRole).toBool())
                return;
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            // Never let the antialiased selection stroke bleed into a neighboring
            // item's paint area. This also prevents stale edges from visually
            // joining two adjacent Alt+Tab cards while the current item changes.
            painter->setClipRect(option.rect);

            const bool dark = useDarkPalette();
            const auto* view = qobject_cast<const QListView*>(option.widget);
            // Qt's selection state can be affected by style/view bookkeeping. The
            // Windows switcher has exactly one emphasized card: the current item.
            const bool selected = view && view->currentIndex() == index;
            const auto card = cardRectForOption(option);
            const auto preview = previewRectForOption(option);
            const auto closeButton = closeButtonRectForOption(option);
            const int titleHeight = titleHeightForCard(card);
            const bool hovered = option.state & QStyle::State_MouseOver;

            const QColor cardFill = dark ? QColor(31, 31, 31) : QColor(248, 248, 248);
            const QColor selectedFill = dark ? QColor(31, 31, 31) : QColor(250, 250, 250);
            const QColor selectedBorder = systemAccentColor();
            const QColor previewFill = dark ? QColor(20, 20, 20) : QColor(238, 238, 238);
            const QColor textColor = dark ? QColor(247, 247, 247) : QColor(32, 32, 32);

            // Draw the accent entirely *inside* the card instead of using a QPen
            // centered on the outer edge. A centered antialiased pen leaves half of
            // its pixels outside the normal card fill; when the current item moves,
            // those pixels can survive the repaint and accumulate into joined or
            // deformed outlines across previously visited cards.
            painter->setPen(Qt::NoPen);
            if (selected) {
                // Keep every accent pixel strictly inside the normal card geometry.
                // A later opaque non-selected repaint can then erase the previous state.
                const QRect accentCard = card.adjusted(1, 1, -1, -1);
                painter->setBrush(selectedBorder);
                painter->drawRoundedRect(accentCard, 7, 7);

                // Native Windows 11 uses a visibly heavier current-target outline.
                // Three logical pixels becomes ~4 physical pixels at 125% DPI.
                const QRect innerCard = accentCard.adjusted(3, 3, -3, -3);
                painter->setBrush(selectedFill);
                painter->drawRoundedRect(innerCard, 4, 4);
            } else {
                painter->setBrush(cardFill);
                painter->drawRoundedRect(card, 8, 8);
            }

            const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
            const int iconSize = qBound(20, titleHeight - 14, 24);
            QRect iconRect(card.left() + 11, card.top() + (titleHeight - iconSize) / 2, iconSize, iconSize);
            if (!icon.isNull())
                icon.paint(painter, iconRect, Qt::AlignCenter, QIcon::Normal);

            const int textRight = hovered ? closeButton.left() - 7 : card.right() - 10;
            QRect textRect(iconRect.right() + 8, card.top(),
                           qMax(10, textRight - iconRect.right() - 8),
                           titleHeight);
            auto font = option.font;
            painter->setFont(font);
            painter->setPen(textColor);
            const QFontMetrics fm(font);
            const auto title = fm.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight,
                                             textRect.width());
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, title);

            // Windows 11 exposes the close affordance on pointer hover; keyboard
            // selection alone keeps the title row clean.
            if (hovered) {
                const QColor closeFill = dark ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 12);
                const QColor closeStroke = dark ? QColor(245, 245, 245) : QColor(45, 45, 45);
                painter->setPen(Qt::NoPen);
                painter->setBrush(closeFill);
                painter->drawRoundedRect(closeButton, 4, 4);

                painter->setPen(QPen(closeStroke, 1.35, Qt::SolidLine, Qt::RoundCap));
                const QPoint c = closeButton.center();
                const int d = qBound(4, closeButton.width() / 5, 5);
                painter->drawLine(c + QPoint(-d, -d), c + QPoint(d, d));
                painter->drawLine(c + QPoint(d, -d), c + QPoint(-d, d));
            }

            painter->setPen(QPen(dark ? QColor(44, 44, 44) : QColor(222, 222, 222), 1));
            painter->setBrush(previewFill);
            painter->drawRoundedRect(preview, 4, 4);

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
            return hint.isValid() ? hint : QSize(DefaultCardWidth, DefaultCardHeight);
        }
    };
}
Widget::Widget(QWidget* parent) : QWidget(parent), ui(new Ui::Widget) {
    ui->setupUi(this);
    lw = ui->listWidget;
    setWindowFlag(Qt::WindowStaysOnTopHint);
    setWindowFlag(Qt::FramelessWindowHint);
    // Let DWM own the switcher's transient Acrylic backdrop. Do not use a
    // layered/per-pixel translucent Qt window on top of it.
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
    QtWin::taskbarDeleteTab(this); //删除任务栏图标
    setWindowTitle("AltTaber");

    Util::setWindowRoundCorner(this->hWnd());
#ifdef Q_OS_WIN
    const COLORREF noBorder = 0xFFFFFFFE; // DWMWA_COLOR_NONE
    DwmSetWindowAttribute(hWnd(), DWMWA_BORDER_COLOR, &noBorder, sizeof(noBorder));
    QtWin::applySwitcherBackdrop(this, useDarkPalette());
#endif

    setupLabelFont();
    ui->label->hide();
    lw->setViewMode(QListView::IconMode);
    lw->setMovement(QListView::Static);
    lw->setFlow(QListView::LeftToRight);
    lw->setWrapping(true);
    lw->setResizeMode(QListView::Adjust);
    // The shell has one current Alt+Tab target, not a Qt multi-selection. Keep Qt
    // selection painting out of the equation and render emphasis from currentIndex().
    lw->setSelectionMode(QAbstractItemView::NoSelection);
    lw->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lw->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // QListView's default frame makes viewport() a few pixels narrower than the widget.
    // With an exact N*gridWidth size that silently wraps the last column and clips rows.
    lw->setFrameShape(QFrame::NoFrame);
    lw->setContentsMargins(0, 0, 0, 0);
    lw->setIconSize({22, 22});
    // Keep the grid unset so Qt respects each item's size hint. Native Windows 11
    // Alt+Tab gives portrait, landscape, and ordinary windows different card widths.
    lw->setGridSize(QSize());
    lw->setUniformItemSizes(false);
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

    connect(lw, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem* previous) {
        pendingTargetWindow = nullptr;

        // Selection is rendered from currentIndex() rather than Qt's selection state.
        // With NoSelection, QListView does not reliably invalidate the old current
        // item's custom delegate painting. Explicitly repaint both rectangles so a
        // previously visited card cannot retain its accent outline.
        if (previous)
            lw->viewport()->update(lw->visualItemRect(previous));
        if (current)
            lw->viewport()->update(lw->visualItemRect(current));
        lw->viewport()->update();
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
    int key = event->key();
    const auto modifiers = event->modifiers();
    if (key == Qt::Key_H) key = Qt::Key_Left;
    else if (key == Qt::Key_L) key = Qt::Key_Right;
    else if (key == Qt::Key_K) key = Qt::Key_Up;
    else if (key == Qt::Key_J) key = Qt::Key_Down;

    auto selectableItems = [this] {
        QList<QListWidgetItem*> items;
        for (int i = 0; i < lw->count(); ++i) {
            auto* item = lw->item(i);
            if (!item || item->data(SpacerRole).toBool())
                continue;
            const auto info = item->data(Qt::UserRole).value<WindowInfo>();
            if (info.hwnd)
                items.append(item);
        }
        return items;
    };

    if (key == Qt::Key_Tab || key == Qt::Key_Left || key == Qt::Key_Right) {
        const auto items = selectableItems();
        if (!items.isEmpty()) {
            int current = items.indexOf(lw->currentItem());
            if (current < 0) current = 0;
            const bool backward = key == Qt::Key_Left
                                  || (key == Qt::Key_Tab && (modifiers & Qt::ShiftModifier));
            const int step = backward ? -1 : 1;
            lw->setCurrentItem(items.at((current + step + items.size()) % items.size()));
        }
        event->accept();
        return;
    }

    if (key == Qt::Key_Up || key == Qt::Key_Down) {
        if (auto item = lw->currentItem()) {
            auto center = lw->visualItemRect(item).center();
            // 转发映射到WheelEvent
            auto wheelEvent = new QWheelEvent(center, lw->mapToGlobal(center), {},
                                              {key == Qt::Key_Up ? 120 : -120, 0},
                                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::postEvent(lw, wheelEvent);
        }
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool Widget::forceShow() {
#ifdef Q_OS_WIN
    // Reapply the system backdrop immediately before every show. This picks up
    // light/dark theme changes made while AltTaber stays resident in the tray.
    QtWin::applySwitcherBackdrop(this, useDarkPalette());
#endif
    setWindowOpacity(0.005); // reduce the translucent-window show flash
    showMinimized();
    showNormal();
    lw->doItemsLayout();
    // DWM thumbnail surfaces are composed outside Qt's painter. Registering them in
    // the same stack frame as showNormal() can use geometry from the previous Alt+Tab
    // session and, after repeated show/hide cycles, leave narrow stale strips between
    // cards. Refresh on the next event-loop turn after Qt has committed this layout.
    const quint64 generation = ++thumbnailGeneration;
    lw->viewport()->update();
    QTimer::singleShot(0, this, [this, generation] {
        if (generation != thumbnailGeneration || !isVisible())
            return;
        lw->doItemsLayout();
        lw->viewport()->repaint();
        refreshThumbnails();
        // Keep the switcher effectively invisible until the DWM thumbnails have
        // been registered and the delegate has repainted with PreviewAvailableRole.
        // Otherwise the fallback application icons are visible for one frame before
        // the live previews arrive on the next event-loop turn.
        lw->viewport()->repaint();
#ifdef Q_OS_WIN
        DwmFlush();
#endif
        setWindowOpacity(1);
    });
    return isForeground();
}
void Widget::setupLabelFont() {
    static auto reloadLabelFontCfg = [this] {
        const QStringList Fonts = {"Segoe UI Variable Text", "Segoe UI", "Microsoft YaHei UI",
                                   "Microsoft YaHei", "Consolas"}; // Windows 11 shell + CJK fallbacks
        auto labelFont = ui->label->font();
        labelFont.setPointSize(cfg.get("label/font_size", 10).toInt());
        auto defaultFF = QStringList{cfg.get("label/font_family", Fonts[0]).toString()};
        labelFont.setFamilies(defaultFF << Fonts.mid(1));
        ui->label->setFont(labelFont);
        lw->setFont(labelFont);
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
    // Intentionally do not paint an outer translucent shell. DWM owns the full-window
    // Mica Alt backdrop; the item delegate paints only the cards/content above it.
}

void Widget::hideEvent(QHideEvent* event) {
    ++thumbnailGeneration; // cancel any refresh queued by the previous show
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
    bool hadRegisteredThumbnail = false;
    for (auto& thumbnail: thumbnails) {
        if (thumbnail.handle) {
            // Explicitly remove the DWM surface from composition before destroying
            // the handle. This prevents a previous session's destination pixels from
            // surviving for a frame when the next switcher is already visible.
            DWM_THUMBNAIL_PROPERTIES props{};
            props.dwFlags = DWM_TNP_VISIBLE;
            props.fVisible = FALSE;
            DwmUpdateThumbnailProperties(thumbnail.handle, &props);
            DwmUnregisterThumbnail(thumbnail.handle);
            hadRegisteredThumbnail = true;
        }
    }
    thumbnails.clear();

#ifdef Q_OS_WIN
    if (hadRegisteredThumbnail)
        DwmFlush();
#endif

    if (!lw) return;
    for (int i = 0; i < lw->count(); ++i) {
        if (auto item = lw->item(i))
            item->setData(PreviewAvailableRole, false);
    }
    lw->viewport()->update();
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

        // DwmQueryThumbnailSourceSize can report the tiny iconic rectangle for a
        // minimized window. Prefer the normal/restored geometry in that case so a
        // minimized Explorer/Notepad preview is not flattened into a wide strip.
        QSize source = sourceWindowSize(info.hwnd);
        if (!IsIconic(info.hwnd)) {
            SIZE queried{};
            if (SUCCEEDED(DwmQueryThumbnailSourceSize(thumbnail, &queried)) &&
                queried.cx > 0 && queried.cy > 0) {
                source = QSize(queried.cx, queried.cy);
            }
        }

        if (source.isValid() && !source.isEmpty()) {
            const qreal scale = qMin(qreal(preview.width()) / qreal(source.width()),
                                     qreal(preview.height()) / qreal(source.height()));
            QSize fitted(qMax(1, qRound(source.width() * scale)),
                         qMax(1, qRound(source.height() * scale)));
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
    auto list = Util::listValidWindows();
    const auto foreground = GetForegroundWindow();

    // Some Flutter/desktop apps briefly clear WS_VISIBLE during focus transitions. If such a
    // window was a foreground window moments ago, keep it in this Alt+Tab snapshot. The relaxed
    // acceptance check still applies all other task-window rules, and the short time window avoids
    // surfacing ordinary tray/background windows that have been hidden for a long time.
    QSet<HWND> seen;
    for (auto hwnd: list)
        seen.insert(hwnd);
    const auto recentCutoff = QDateTime::currentDateTime().addSecs(-5);
    for (auto appIt = winActiveOrder.cbegin(); appIt != winActiveOrder.cend(); ++appIt) {
        for (auto winIt = appIt.value().cbegin(); winIt != appIt.value().cend(); ++winIt) {
            const HWND hwnd = winIt.key();
            if (seen.contains(hwnd) || winIt.value() < recentCutoff || !IsWindow(hwnd))
                continue;
            if (!Util::isWindowAcceptable(hwnd, true))
                continue;
            qDebug() << "#include recent hidden window:" << hwnd << Util::getWindowTitle(hwnd);
            list.append(hwnd);
            seen.insert(hwnd);
        }
    }

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
    const int count = windowList.size();
    // Measurements from the native Windows 11 25H2 switcher show a flow layout rather
    // than an equal-cell grid: all cards in one snapshot share a height, while each
    // card's width follows its source window aspect ratio. Rows are balanced and each
    // row is centered independently. The outer switcher remains compact instead of
    // stretching to almost the full monitor width.
    const int contentMaxWidth = qMax(MinimumCardWidth,
                                     qRound(available.width() * 0.70));
    const int contentMaxHeight = qMax(MinimumCardHeight,
                                      qRound(available.height() * 0.84)
                                          - ListWidgetMargin.top() - ListWidgetMargin.bottom());
    const int comfortableCardHeight = qBound(145, qRound(available.height() * 0.19), 165);
    const int preferredCardHeight = qBound(210, qRound(available.height() * 0.29), 240);

    auto widthsForHeight = [&windowList](int height) {
        QList<int> widths;
        widths.reserve(windowList.size());
        for (const auto& info: windowList)
            widths.append(cardWidthForWindow(info.hwnd, height));
        return widths;
    };

    auto rowsFit = [contentMaxWidth, &widthsForHeight](int rows, int height,
                                                       QList<int>* fittedCounts = nullptr) {
        const auto widths = widthsForHeight(height);
        const auto rowCounts = widthBalancedRowCounts(widths, rows);
        if (rowCounts.size() != rows)
            return false;
        int item = 0;
        for (const int rowCount: rowCounts) {
            int rowWidth = 0;
            for (int i = 0; i < rowCount && item < widths.size(); ++i, ++item)
                rowWidth += widths.at(item);
            if (rowWidth > contentMaxWidth)
                return false;
        }
        if (item != widths.size())
            return false;
        if (fittedCounts)
            *fittedCounts = rowCounts;
        return true;
    };

    int bestRows = 1;
    QList<int> bestRowCounts;
    bool layoutFound = false;
    for (int rows = 1; rows <= count; ++rows) {
        const int candidateHeight = qMin(comfortableCardHeight, contentMaxHeight / rows);
        if (candidateHeight < MinimumCardHeight)
            break;
        QList<int> rowCounts;
        if (rowsFit(rows, candidateHeight, &rowCounts)) {
            bestRows = rows;
            bestRowCounts = rowCounts;
            layoutFound = true;
            break;
        }
    }

    if (!layoutFound) {
        // Extreme window counts can exhaust the comfortable height. Keep increasing
        // rows and allow a smaller card rather than overflowing the monitor.
        constexpr int AbsoluteMinimumHeight = 72;
        for (int rows = 1; rows <= count; ++rows) {
            const int candidateHeight = qMin(comfortableCardHeight, contentMaxHeight / rows);
            if (candidateHeight < AbsoluteMinimumHeight)
                break;
            QList<int> rowCounts;
            if (rowsFit(rows, candidateHeight, &rowCounts)) {
                bestRows = rows;
                bestRowCounts = rowCounts;
                layoutFound = true;
                break;
            }
        }
    }

    if (!layoutFound) {
        bestRows = qMin(count, qMax(1, contentMaxHeight / 72));
        bestRowCounts = widthBalancedRowCounts(widthsForHeight(72), bestRows);
    }

    int bestCardHeight = qMin(preferredCardHeight, contentMaxHeight / bestRows);
    while (bestCardHeight > 72) {
        QList<int> rowCounts;
        if (rowsFit(bestRows, bestCardHeight, &rowCounts)) {
            bestRowCounts = rowCounts;
            break;
        }
        --bestCardHeight;
    }
    bestCardHeight = qMax(72, bestCardHeight);
    if (bestRowCounts.isEmpty())
        bestRowCounts = widthBalancedRowCounts(widthsForHeight(bestCardHeight), bestRows);

    const auto cardWidths = widthsForHeight(bestCardHeight);
    QList<int> rowWidths;
    rowWidths.reserve(bestRows);
    int itemIndex = 0;
    int layoutWidth = 1;
    for (const int rowCount: bestRowCounts) {
        int rowWidth = 0;
        for (int i = 0; i < rowCount; ++i)
            rowWidth += cardWidths.at(itemIndex++);
        rowWidths.append(rowWidth);
        layoutWidth = qMax(layoutWidth, rowWidth);
    }

    lw->setGridSize(QSize());
    lw->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lw->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QList<QListWidgetItem*> windowItems;
    windowItems.reserve(count);
    auto addSpacer = [this, bestCardHeight](int width) {
        if (width <= 0)
            return;
        auto* spacer = new QListWidgetItem;
        spacer->setData(SpacerRole, true);
        spacer->setFlags(Qt::NoItemFlags);
        spacer->setSizeHint(QSize(width, bestCardHeight));
        lw->addItem(spacer);
    };

    itemIndex = 0;
    for (int row = 0; row < bestRows; ++row) {
        const int freeWidth = qMax(0, layoutWidth - rowWidths.at(row));
        const int leftSpacer = freeWidth / 2;
        const int rightSpacer = freeWidth - leftSpacer;
        addSpacer(leftSpacer);

        for (int i = 0; i < bestRowCounts.at(row); ++i, ++itemIndex) {
            const auto& info = windowList.at(itemIndex);
            auto title = info.title;
            if (title.isEmpty())
                title = Util::getFileDescription(info.exePath);
            if (title.isEmpty())
                title = "Window";

            auto* item = new QListWidgetItem(info.icon, title);
            item->setData(Qt::UserRole, QVariant::fromValue(info));
            item->setData(PreviewAvailableRole, false);
            item->setSizeHint(QSize(cardWidths.at(itemIndex), bestCardHeight));
            lw->addItem(item);
            windowItems.append(item);
        }

        addSpacer(rightSpacer);
    }

    // +1 prevents a fractional-DPI boundary from wrapping a completed row.
    lw->setFixedSize(layoutWidth + 1, bestRows * bestCardHeight + 1);
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

    if (windowItems.size() >= 2) {
        const auto foreground = GetForegroundWindow();
        const bool firstIsForeground = windowList.at(0).hwnd == foreground;
        lw->setCurrentItem(windowItems.at(firstIsForeground ? 1 : 0));
    } else if (!windowItems.isEmpty())
        lw->setCurrentItem(windowItems.first());

    qDebug() << "Prepared" << windowItems.size() << "window thumbnails on" << screen->name()
             << "flow rows" << bestRowCounts << "height" << bestCardHeight
             << "row widths" << rowWidths << "card widths" << cardWidths;
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
            if (item->data(SpacerRole).toBool())
                return true;
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
            if (item->data(SpacerRole).toBool())
                return true;
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
