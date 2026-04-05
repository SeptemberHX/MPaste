// input: Depends on MPasteWidget.h, Qt runtime services, resource assets, and platform clipboard/window helpers.
// output: Implements the main window, item interaction flow, reliable quick-paste shortcuts, and plain-text paste behavior.
// pos: Widget-layer main window implementation coordinating boards, shortcuts, and system integration.
// update: If I change, update this header block and my folder README.md.
// note: Added theme application, dark mode propagation, tray menu theming, robust paste rehydration, and alias sync.
#include <QScrollBar>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QButtonGroup>
#include <QLocale>
#include <QIcon>
#include <QAction>
#include <QApplication>
#include <QStringList>
#include <QTimer>
#include <QScreen>
#include <QGuiApplication>
#include <QTextDocument>
#include <QWheelEvent>
#include <QWindow>
#include <QPainter>
#include <QPainterPath>
#include <QClipboard>
#include <QFrame>
#include <QMessageBox>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QToolButton>
#include <QAbstractItemView>
#include <QComboBox>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleFactory>
#include "utils/MPasteSettings.h"
#include "utils/ThemeManager.h"
#include "WindowBlurHelper.h"
#include "utils/IconResolver.h"
#include "MPasteWidget.h"
#include "ui_MPasteWidget.h"
#include "BoardInternalHelpers.h"
#include "ClipboardAppController.h"
#include "ClipboardCardDelegate.h"
#include "utils/PlatformRelated.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

// Frosted-glass dialog used for OCR loading indicator and result display.
class OcrResultDialog : public QDialog {
public:
    bool dark = false;
    QPoint dragOffset_;
    using QDialog::QDialog;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal r = 8.0;
        QRectF rect = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(QPen(dark ? QColor(255,255,255,40) : QColor(0,0,0,25), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(rect, r, r);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 1));
        p.drawRoundedRect(rect, r, r);
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            dragOffset_ = e->globalPosition().toPoint() - frameGeometry().topLeft();
            e->accept();
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (e->buttons() & Qt::LeftButton) {
            move(e->globalPosition().toPoint() - dragOffset_);
            e->accept();
        }
    }
    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) { close(); return; }
        QDialog::keyPressEvent(e);
    }
};

namespace {
// Helper functions moved to ClipboardAppController.cpp:
// rehydrateClipboardItem, hasUsableMimeData, elideClipboardLogText, widgetItemSummary
}

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>

// Windows 11 backdrop types (DWMWA_SYSTEMBACKDROP_TYPE = 38)
enum DWM_SYSTEMBACKDROP_TYPE {
    DWMSBT_AUTO            = 0,
    DWMSBT_NONE            = 1,
    DWMSBT_MAINWINDOW      = 2, // Mica
    DWMSBT_TRANSIENTWINDOW = 3, // Acrylic
    DWMSBT_TABBEDWINDOW    = 4  // Mica Alt
};

// Undocumented but stable blur-behind API
enum ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4
};

struct ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor; // AABBGGRR
    DWORD AnimationId;
};

enum WINDOWCOMPOSITIONATTRIB {
    WCA_ACCENT_POLICY = 19
};

struct WINDOWCOMPOSITIONATTRIBDATA {
    WINDOWCOMPOSITIONATTRIB Attrib;
    PVOID pvData;
    SIZE_T cbData;
};

typedef BOOL (WINAPI *pfnSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

static DWORD accentColorFromArgb(const QColor &color) {
    return (static_cast<DWORD>(color.alpha()) << 24)
        | (static_cast<DWORD>(color.blue()) << 16)
        | (static_cast<DWORD>(color.green()) << 8)
        | static_cast<DWORD>(color.red());
}

static void enableBlurBehind(HWND hwnd, const QColor &tintColor) {
    MARGINS margins = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    DWORD preference = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33, &preference, sizeof(preference));

    // Acrylic blur via SetWindowCompositionAttribute (instant, no flicker)
    auto user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    auto setWCA = reinterpret_cast<pfnSetWindowCompositionAttribute>(
        GetProcAddress(user32, "SetWindowCompositionAttribute"));
    if (!setWCA) return;

    DWORD tint = accentColorFromArgb(tintColor);

    ACCENT_POLICY accent{};
    accent.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
    accent.AccentFlags = 2;
    accent.GradientColor = tint;

    WINDOWCOMPOSITIONATTRIBDATA data{};
    data.Attrib = WCA_ACCENT_POLICY;
    data.pvData = &accent;
    data.cbData = sizeof(accent);
    setWCA(hwnd, &data);
}
#endif

MPasteWidget::MPasteWidget(QWidget *parent) :
    QWidget(parent)
{
    ui_.ui = new Ui::MPasteWidget;
    ui_.ui->setupUi(this);
    initializeWidget();
}

MPasteWidget::~MPasteWidget() {
    delete ui_.ui;
}

void MPasteWidget::initializeWidget() {
    misc_.startupPerfTimer.start();
    qInfo() << "[startup] initializeWidget begin";

    initStyle();
    qInfo().noquote() << QStringLiteral("[startup] initStyle done elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());
    initUI();
    qInfo().noquote() << QStringLiteral("[startup] initUI done elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());
    controller_ = new ClipboardAppController(ui_.boardWidgetMap, ui_.clipboardWidget, ui_.staredWidget, this, this);
    connect(controller_, &ClipboardAppController::pasteRequested, this, &MPasteWidget::hideAndPaste);
    connect(controller_, &ClipboardAppController::boardItemCountChanged,
            this, [this](const QString &category, int count) {
        if (ui_.buttonGroup->checkedButton()->property("category").toString() == category) {
            updateItemCount(count);
        }
    });
    connect(controller_, &ClipboardAppController::boardSelectionStateChanged,
            this, [this](const QString &category) {
        if (ui_.buttonGroup->checkedButton()->property("category").toString() == category) {
            auto *board = ui_.boardWidgetMap.value(category);
            if (board) updateItemCount(board->getItemCount());
        }
    });
    connect(controller_, &ClipboardAppController::boardPageStateChanged,
            this, [this](const QString &category) {
        if (ui_.buttonGroup->checkedButton()->property("category").toString() == category) {
            updatePageSelector();
        }
    });
    connect(controller_, &ClipboardAppController::ocrStarted, this, [this]() {
        if (ocrLoadingDialog_) {
            ocrLoadingDialog_->close();
        }
        const bool dark = MPasteSettings::getInst()->isDarkTheme();
        auto *dlg = new OcrResultDialog(this);
        dlg->dark = dark;
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setAttribute(Qt::WA_TranslucentBackground);
        dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::Tool);
        dlg->setModal(false);
        dlg->resize(320, 120);
        auto *lay = new QVBoxLayout(dlg);
        lay->setAlignment(Qt::AlignCenter);
        auto *label = new QLabel(tr("Recognizing..."), dlg);
        label->setAlignment(Qt::AlignCenter);
        const QString color = dark ? QStringLiteral("#E6EDF5") : QStringLiteral("#1E2936");
        label->setStyleSheet(QStringLiteral("color: %1; font-size: 18px; background: transparent;").arg(color));
        lay->addWidget(label);
        QScreen *screen = nullptr;
        if (auto *w = window()->windowHandle()) screen = w->screen();
        if (!screen) screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (screen) {
            const QRect geo = screen->availableGeometry();
            dlg->move(geo.center() - QPoint(dlg->width() / 2, dlg->height() / 2));
        }
        dlg->show();
        WindowBlurHelper::enableBlurBehind(dlg, dark);
        dlg->raise();
        ocrLoadingDialog_ = dlg;
    });
    connect(controller_, &ClipboardAppController::ocrCompleted, this, [this](const QString &text) {
        if (ocrLoadingDialog_) {
            ocrLoadingDialog_->close();
            ocrLoadingDialog_ = nullptr;
        }
        showOcrResultDialog(text);
    });
    connect(controller_, &ClipboardAppController::ocrFailed, this, [this](const QString &msg) {
        if (ocrLoadingDialog_) {
            ocrLoadingDialog_->close();
            ocrLoadingDialog_ = nullptr;
        }
        QMessageBox::warning(this, tr("OCR"), msg);
    });
    qInfo().noquote() << QStringLiteral("[startup] controller created elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());
    initShortcuts();
    qInfo().noquote() << QStringLiteral("[startup] initShortcuts done elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());
    initSystemTray();
    qInfo().noquote() << QStringLiteral("[startup] initSystemTray done elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());
    setupConnections();
    qInfo().noquote() << QStringLiteral("[startup] setupConnections done elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());
    scheduleStartupWarmup();
    qInfo().noquote() << QStringLiteral("[startup] startup warmup scheduled elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());

    setFocusOnSearch(false);
    misc_.pendingNumKey = 0;
    qInfo().noquote() << QStringLiteral("[startup] initializeWidget end totalElapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());
    QTimer::singleShot(0, this, [this]() {
        qInfo().noquote() << QStringLiteral("[startup] event-loop checkpoint 0ms elapsedMs=%1 visible=%2 active=%3")
            .arg(misc_.startupPerfTimer.elapsed())
            .arg(isVisible())
            .arg(isActiveWindow());
    });
    QTimer::singleShot(100, this, [this]() {
        qInfo().noquote() << QStringLiteral("[startup] event-loop checkpoint 100ms elapsedMs=%1 visible=%2 active=%3")
            .arg(misc_.startupPerfTimer.elapsed())
            .arg(isVisible())
            .arg(isActiveWindow());
    });
    QTimer::singleShot(500, this, [this]() {
        qInfo().noquote() << QStringLiteral("[startup] event-loop checkpoint 500ms elapsedMs=%1 visible=%2 active=%3")
            .arg(misc_.startupPerfTimer.elapsed())
            .arg(isVisible())
            .arg(isActiveWindow());
    });

#ifdef _DEBUG
    QTimer* debugTimer = new QTimer(this);
    connect(debugTimer, &QTimer::timeout, this, &MPasteWidget::debugKeyState);
    debugTimer->start(1000);
#endif
}

void MPasteWidget::initShortcuts() {
    misc_.numKeyList.clear();
    misc_.numKeyList << Qt::Key_1 << Qt::Key_2 << Qt::Key_3 << Qt::Key_4 << Qt::Key_5
                     << Qt::Key_6 << Qt::Key_7 << Qt::Key_8 << Qt::Key_9 << Qt::Key_0;
}

AboutWidget *MPasteWidget::ensureAboutWidget() {
    if (!ui_.aboutWidget) {
        ui_.aboutWidget = new AboutWidget(this);
        ui_.aboutWidget->hide();
    }
    return ui_.aboutWidget;
}

ClipboardItemDetailsDialog *MPasteWidget::ensureDetailsDialog() {
    if (!ui_.detailsDialog) {
        ui_.detailsDialog = new ClipboardItemDetailsDialog(this);
        ui_.detailsDialog->setWindowFlag(Qt::Tool);
        ui_.detailsDialog->hide();
    }
    return ui_.detailsDialog;
}

ClipboardItemPreviewDialog *MPasteWidget::ensurePreviewDialog() {
    if (!ui_.previewDialog) {
        ui_.previewDialog = new ClipboardItemPreviewDialog(this);
        ui_.previewDialog->setWindowFlag(Qt::Tool);
        ui_.previewDialog->hide();
    }
    return ui_.previewDialog;
}

MPasteSettingsWidget *MPasteWidget::ensureSettingsWidget() {
    if (!ui_.settingsWidget) {
        ui_.settingsWidget = new MPasteSettingsWidget(this);
        connect(ui_.settingsWidget, &MPasteSettingsWidget::shortcutChanged,
                this, &MPasteWidget::shortcutChanged);
        connect(ui_.settingsWidget, &MPasteSettingsWidget::historyRetentionChanged,
                this, &MPasteWidget::reloadHistoryBoards);
        connect(ui_.settingsWidget, &MPasteSettingsWidget::historyViewModeChanged,
                this, &MPasteWidget::reloadHistoryBoards);
        connect(ui_.settingsWidget, &MPasteSettingsWidget::saveDirChanged,
                this, [this]() {
                    controller_->setupSyncWatcher();
                    reloadHistoryBoards();
                });
        connect(ui_.settingsWidget, &MPasteSettingsWidget::itemScaleChanged,
                this, [this](int scale) {
                    applyScale(scale);
                });
    }
    return ui_.settingsWidget;
}

void MPasteWidget::scheduleStartupWarmup() {
    if (loading_.startupWarmupScheduled) {
        return;
    }
    loading_.startupWarmupScheduled = true;

    QTimer::singleShot(0, this, [this]() {
        controller_->loadFromSaveDir();
        qInfo().noquote() << QStringLiteral("[startup] deferred loadFromSaveDir done elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());

        // Wait for the clipboard board to finish loading before priming
        // the clipboard, so duplicate detection has the full history.
        // Always wait — hasPendingItems() is unreliable during async scan.
        auto *boardService = ui_.clipboardWidget->boardServiceRef();
        if (boardService) {
            connect(boardService, &ClipboardBoardService::deferredLoadCompleted, this, [this]() {
                controller_->primeCurrentClipboard();
                qInfo().noquote() << QStringLiteral("[startup] deferred primeCurrentClipboard done elapsedMs=%1").arg(misc_.startupPerfTimer.elapsed());
                loading_.startupWarmupCompleted = true;
            }, Qt::SingleShotConnection);
        }
    });
}

void MPasteWidget::setupConnections() {
    // Board orchestration signals (paste, OCR, star, sync) are wired
    // in ClipboardAppController::connectBoardSignals().  Only UI-only
    // connections remain here.
    for (auto *boardWidget : ui_.boardWidgetMap.values()) {
        connect(boardWidget, &ScrollItemsWidget::detailsRequested,
        this, [this](const ClipboardItem &item, int sequence, int totalCount) {
            ensureDetailsDialog()->showItem(item, sequence, totalCount);
        });
        connect(boardWidget, &ScrollItemsWidget::previewRequested,
        this, [this](const ClipboardItem &item) {
            if (ClipboardItemPreviewDialog::supportsPreview(item)) {
                ensurePreviewDialog()->showItem(item);
            }
        });
    }

    connect(ui_.ui->menuButton, &QToolButton::clicked, this, [this]() {
        ui_.menu->popup(ui_.ui->menuButton->mapToGlobal(ui_.ui->menuButton->rect().bottomLeft()));
    });

    connect(ui_.ui->searchEdit, &QLineEdit::textChanged, this, [this](const QString &str) {
        this->currItemsWidget()->filterByKeyword(str);
    });
    connect(ui_.ui->searchButton, &QToolButton::clicked, this, [this](bool flag) {
        this->setFocusOnSearch(flag);
    });
    connect(ui_.pageComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0) {
            return;
        }
        if (ScrollItemsWidget *board = currItemsWidget()) {
            board->setCurrentPageNumber(index + 1);
        }
        if (ui_.pageNumberLabel) {
            ui_.pageNumberLabel->setText(QString::number(index + 1));
        }
    });

    connect(ui_.ui->firstButton, &QPushButton::clicked, this, [this]() {
        this->currItemsWidget()->scrollToFirst();
    });
    connect(ui_.ui->lastButton, &QPushButton::clicked, this, [this]() {
        this->currItemsWidget()->scrollToLast();
    });

    connect(ui_.trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            this->setVisibleWithAnnimation(true);
        }
    });

    connect(ui_.typeButtonGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
        [this](QAbstractButton *button) {
            auto type = static_cast<ContentType>(button->property("contentType").toInt());
            this->currItemsWidget()->filterByType(type);
        });

    connect(ui_.buttonGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
        [this](QAbstractButton *button) {
            auto typeBtn = ui_.typeButtonGroup->checkedButton();
            auto type = typeBtn ? static_cast<ContentType>(typeBtn->property("contentType").toInt())
                                : All;

            for (auto *toolButton : ui_.buttonGroup->buttons()) {
                auto *boardWidget = ui_.boardWidgetMap[toolButton->property("category").toString()];
                boardWidget->setVisible(toolButton == button);
                if (toolButton == button) {
                    boardWidget->filterByType(type);
                    boardWidget->filterByKeyword(ui_.ui->searchEdit->text());
                }
            }
            updateItemCount(currItemsWidget()->getItemCount());
            updatePageSelector();
        });
}

// clipboardActivityObserved and clipboardUpdated moved to ClipboardAppController.

// Clipboard write and URL handling are delegated to the controller.

ScrollItemsWidget *MPasteWidget::currItemsWidget() {
    if (!ui_.buttonGroup) {
        return ui_.clipboardWidget;
    }

    QAbstractButton* currentBtn = ui_.buttonGroup->checkedButton();
    if (currentBtn) {
        QString category = currentBtn->property("category").toString();
        return ui_.boardWidgetMap[category];
    }

    return ui_.clipboardWidget;
}

void MPasteWidget::hideAndPaste() {
    WId previousWId = PlatformRelated::previousActiveWindow();

    hide();

    controller_->pasteToTarget(previousWId);
}
void MPasteWidget::setVisibleWithAnnimation(bool visible) {
    if (visible == isVisible()) return;

    if (visible) {
        QElapsedTimer t; t.start();
        setWindowOpacity(0);
        show();
        qInfo().noquote() << QStringLiteral("[wake] setVisibleWithAnnimation: show() took %1 ms").arg(t.elapsed());
        if (controller_->copiedWhenHide()) {
            controller_->clearCopiedWhenHide();
            ui_.clipboardWidget->scrollToFirst();
        }

        QPropertyAnimation* animation = new QPropertyAnimation(this, "windowOpacity", this);
        animation->setDuration(200);
        animation->setStartValue(0.0);
        animation->setEndValue(1.0);
        animation->setEasingCurve(QEasingCurve::OutCubic);

        connect(animation, &QPropertyAnimation::finished, this, [this, animation]() {
            animation->deleteLater();
            if (!ui_.ui->searchEdit->text().isEmpty()) {
                ui_.ui->searchEdit->setFocus();
            }

            for (int i = 0; i < 10; ++i) {
                if (PlatformRelated::currActiveWindow() == winId()) {
                    break;
                }
                PlatformRelated::activateWindow(winId());
            }
        });

        animation->start(QAbstractAnimation::DeleteWhenStopped);
    } else {
        QPropertyAnimation* animation = new QPropertyAnimation(this, "windowOpacity", this);
        animation->setDuration(HIDE_ANIMATION_TIME);
        animation->setStartValue(1.0);
        animation->setEndValue(0.0);
        animation->setEasingCurve(QEasingCurve::InCubic);

        connect(animation, &QPropertyAnimation::finished, this, [this, animation]() {
            animation->deleteLater();
            hide();
            currItemsWidget()->cleanShortCutInfo();
        });

        animation->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void MPasteWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
}

void MPasteWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal radius = 8.0;
    QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    // Clear to transparent so DWM acrylic shows through
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.fillRect(rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    if (darkTheme_) {
        // Glass edge - thin luminous white border
        QPen glassPen(QColor(255, 255, 255, 40), 1.5);
        p.setPen(glassPen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, radius, radius);

        // Specular highlight arc at top
        QLinearGradient specular(r.topLeft(), QPointF(r.left(), r.top() + r.height() * 0.15));
        specular.setColorAt(0.0, QColor(255, 255, 255, 18));
        specular.setColorAt(1.0, QColor(255, 255, 255, 0));
        QPainterPath topClip;
        topClip.addRoundedRect(r.adjusted(1.5, 1.5, -1.5, 0), radius - 1, radius - 1);
        p.setClipPath(topClip);
        p.fillRect(QRectF(r.left(), r.top(), r.width(), r.height() * 0.15), specular);
        p.setClipping(false);

        // Inner edge glow
        QPen innerGlow(QColor(255, 255, 255, 20), 0.5);
        p.setPen(innerGlow);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(2, 2, -2, -2), radius - 1.5, radius - 1.5);
    } else {
        // Light: gradient border (original style)
        const qreal bw = 3.0;
        QRectF rb = QRectF(rect()).adjusted(bw / 2.0, bw / 2.0, -bw / 2.0, -bw / 2.0);
        QConicalGradient grad(rb.center(), 135);
        grad.setColorAt(0.00, QColor("#4A90E2"));
        grad.setColorAt(0.25, QColor("#1abc9c"));
        grad.setColorAt(0.50, QColor("#fc9867"));
        grad.setColorAt(0.75, QColor("#9B59B6"));
        grad.setColorAt(1.00, QColor("#4A90E2"));
        p.setPen(QPen(QBrush(grad), bw));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(rb, radius, radius);
    }
}

void MPasteWidget::showEvent(QShowEvent *event) {
    QElapsedTimer t; t.start();
    QWidget::showEvent(event);
    qInfo().noquote() << QStringLiteral("[wake] showEvent: QWidget::showEvent %1 ms").arg(t.elapsed());
    stopKeepAliveTimer();
    qInfo().noquote() << QStringLiteral("[wake] showEvent: stopKeepAliveTimer %1 ms").arg(t.elapsed());
    activateWindow();
    raise();
    setFocus();
    qInfo().noquote() << QStringLiteral("[wake] showEvent: activate/raise/focus %1 ms").arg(t.elapsed());
    controller_->onWidgetShown();
    // Show "Loading..." if data is still being loaded from disk,
    // so the panel is not blank on very early wake.
    if (auto *board = currItemsWidget()) {
        board->updateLoadingOverlay();
    }
    qInfo().noquote() << QStringLiteral("[wake] showEvent total: %1 ms").arg(t.elapsed());
}

void MPasteWidget::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    for (auto it = ui_.boardWidgetMap.cbegin(); it != ui_.boardWidgetMap.cend(); ++it) {
        if (auto *board = it.value()) {
            board->hideHoverTools();
        }
    }
    startKeepAliveTimer();
}

void MPasteWidget::reloadHistoryBoards() {
    const QString keyword = ui_.ui->searchEdit->text();
    const auto type = static_cast<ContentType>(
        ui_.typeButtonGroup->checkedButton()
            ? ui_.typeButtonGroup->checkedButton()->property("contentType").toInt()
            : static_cast<int>(All));

    controller_->loadFromSaveDir();
    ui_.clipboardWidget->filterByType(type);
    ui_.clipboardWidget->filterByKeyword(keyword);
    ui_.staredWidget->filterByType(type);
    ui_.staredWidget->filterByKeyword(keyword);
    updateItemCount(currItemsWidget()->getItemCount());
    updatePageSelector();
}

void MPasteWidget::startKeepAliveTimer() {
#ifdef Q_OS_WIN
    // Tell Windows to keep a reasonable minimum working set so that the
    // process memory is not aggressively paged out during long idle periods.
    // This is the primary mechanism to prevent the multi-second freeze when
    // the user invokes the hotkey after hours of inactivity.
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        // Use current working set as the minimum, capped to a sane range.
        const SIZE_T current = pmc.WorkingSetSize;
        const SIZE_T minWS = qBound<SIZE_T>(20ULL * 1024 * 1024, current, 200ULL * 1024 * 1024);
        const SIZE_T maxWS = qMax<SIZE_T>(minWS * 2, 400ULL * 1024 * 1024);
        SetProcessWorkingSetSize(GetCurrentProcess(), minWS, maxWS);
    }

    if (!keepAliveTimer_) {
        keepAliveTimer_ = new QTimer(this);
        keepAliveTimer_->setTimerType(Qt::VeryCoarseTimer);
        connect(keepAliveTimer_, &QTimer::timeout, this, &MPasteWidget::touchWorkingSet);
    }
    keepAliveTimer_->start(KEEPALIVE_INTERVAL_MS);
#endif
}

void MPasteWidget::stopKeepAliveTimer() {
#ifdef Q_OS_WIN
    // Restore default working set policy so the OS can reclaim memory
    // while the widget is visible and actively used.
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
#endif
    if (keepAliveTimer_) {
        keepAliveTimer_->stop();
    }
}

void MPasteWidget::touchWorkingSet() {
#ifdef Q_OS_WIN
    // Walk key data structures to fault their pages back in, preventing
    // Windows from trimming them even with the minimum working set set.
    volatile char sink = 0;

    auto touchWidget = [&sink](ScrollItemsWidget *w) {
        if (!w) return;
        auto *model = w->boardModel();
        if (!model) return;
        const int rows = model->rowCount();
        const int step = qMax(1, rows / 16);
        for (int i = 0; i < rows; i += step) {
            const ClipboardItem *item = model->itemPtrAt(i);
            if (item) {
                const QString &name = item->getName();
                if (!name.isEmpty()) {
                    sink += name.at(0).unicode();
                }
                // Touch thumbnail pixmap data if present.
                if (item->hasThumbnail()) {
                    const QPixmap &px = item->thumbnail();
                    sink += reinterpret_cast<const volatile char *>(&px)[0];
                }
            }
        }
        // Touch the widget's own object tree.
        sink += reinterpret_cast<const volatile char *>(w)[0];
    };

    touchWidget(ui_.clipboardWidget);
    touchWidget(ui_.staredWidget);

    // Touch this widget and its UI object.
    sink += reinterpret_cast<const volatile char *>(this)[0];
    if (ui_.ui) {
        sink += reinterpret_cast<const volatile char *>(ui_.ui)[0];
    }
    Q_UNUSED(sink);
#endif
}

void MPasteWidget::dumpMemoryStats() {
    qInfo().noquote() << QStringLiteral("===== MPaste Memory Stats =====");
    if (ui_.clipboardWidget) {
        qInfo().noquote() << ui_.clipboardWidget->memoryStats();
    }
    if (ui_.staredWidget) {
        qInfo().noquote() << ui_.staredWidget->memoryStats();
    }
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        qInfo().noquote() << QStringLiteral("[process] workingSet: %1 MB, peakWorkingSet: %2 MB, pagefile: %3 MB")
                                .arg(pmc.WorkingSetSize / (1024 * 1024))
                                .arg(pmc.PeakWorkingSetSize / (1024 * 1024))
                                .arg(pmc.PagefileUsage / (1024 * 1024));
    }
#endif
    qInfo().noquote() << QStringLiteral("===============================");
}

void MPasteWidget::debugKeyState() {
#ifdef Q_OS_WIN
    qDebug() << "Alt Key State:" << (GetAsyncKeyState(VK_MENU) & 0x8000)
             << "Window Focus:" << hasFocus()
             << "Window ID:" << winId()
             << "Is Visible:" << isVisible()
             << "Active Window:" << QApplication::activeWindow();
#endif
}

void MPasteWidget::showOcrResultDialog(const QString &text) {
    const bool dark = MPasteSettings::getInst()->isDarkTheme();

    auto *dialog = new OcrResultDialog(this);
    dialog->dark = dark;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setAttribute(Qt::WA_TranslucentBackground);
    dialog->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::Tool);
    dialog->setModal(false);
    dialog->resize(720, 520);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(12);

    auto *headerLayout = new QHBoxLayout;
    auto *titleLabel = new QLabel(tr("Recognized Text"), dialog);
    titleLabel->setObjectName(QStringLiteral("previewTitle"));
    headerLayout->addWidget(titleLabel, 1);

    auto *copyBtn = new QToolButton(dialog);
    copyBtn->setObjectName(QStringLiteral("closeButton"));
    copyBtn->setText(QStringLiteral("\u2398"));
    copyBtn->setToolTip(tr("Copy to Clipboard"));
    copyBtn->setCursor(Qt::PointingHandCursor);
    connect(copyBtn, &QToolButton::clicked, dialog, [text, copyBtn]() {
        QGuiApplication::clipboard()->setText(text);
        copyBtn->setText(QStringLiteral("\u2713"));
        QTimer::singleShot(1200, copyBtn, [copyBtn]() {
            if (copyBtn) copyBtn->setText(QStringLiteral("\u2398"));
        });
    });
    headerLayout->addWidget(copyBtn, 0, Qt::AlignTop);

    auto *closeBtn = new QToolButton(dialog);
    closeBtn->setObjectName(QStringLiteral("closeButton"));
    closeBtn->setText(QStringLiteral("\u00D7"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QToolButton::clicked, dialog, &QDialog::close);
    headerLayout->addWidget(closeBtn, 0, Qt::AlignTop);

    layout->addLayout(headerLayout);

    auto *textEdit = new QTextEdit(dialog);
    textEdit->setReadOnly(true);
    textEdit->setPlainText(text);
    textEdit->setLineWrapMode(QTextEdit::WidgetWidth);
    textEdit->setFrameShape(QFrame::NoFrame);
    textEdit->setObjectName(QStringLiteral("ocrTextEdit"));
    layout->addWidget(textEdit, 1);

    const QString textColor = dark ? QStringLiteral("#E6EDF5") : QStringLiteral("#1E2936");
    const QString subtextColor = dark ? QStringLiteral("#9AA7B5") : QStringLiteral("#5E7084");
    const QString borderColor = dark ? QStringLiteral("rgba(255,255,255,25)")
                                     : QStringLiteral("rgba(0,0,0,18)");
    const QString btnBg = dark ? QStringLiteral("rgba(255,255,255,14)")
                               : QStringLiteral("rgba(0,0,0,8)");
    const QString btnHoverBg = dark ? QStringLiteral("rgba(255,255,255,28)")
                                    : QStringLiteral("rgba(0,0,0,16)");
    const QString selBg = dark ? QStringLiteral("rgba(74,144,226,110)")
                               : QStringLiteral("rgba(74,144,226,76)");

    dialog->setStyleSheet(QStringLiteral(
        "QLabel#previewTitle {"
        "  color: %1; font-size: 22px; font-weight: 700; background: transparent;"
        "}"
        "QTextEdit#ocrTextEdit {"
        "  background-color: transparent; border: none;"
        "  color: %1; font-size: 15px;"
        "  selection-background-color: %2;"
        "  padding: 8px;"
        "}"
        "QToolButton#closeButton {"
        "  background-color: %3; border: 1px solid %4;"
        "  border-radius: 14px; color: %5;"
        "  font-size: 17px; font-weight: 700;"
        "  min-width: 28px; min-height: 28px;"
        "}"
        "QToolButton#closeButton:hover {"
        "  background-color: %6; border-color: %4;"
        "}"
    ).arg(textColor, selBg, btnBg, borderColor, subtextColor, btnHoverBg));

    QScreen *screen = nullptr;
    if (auto *w = window()->windowHandle()) {
        screen = w->screen();
    }
    if (!screen) screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect geo = screen->availableGeometry();
        dialog->move(geo.center() - QPoint(dialog->width() / 2, dialog->height() / 2));
    }

    dialog->show();
    WindowBlurHelper::enableBlurBehind(dialog, dark);
    dialog->raise();
    dialog->activateWindow();
}

#include "MPasteWidget.moc"
