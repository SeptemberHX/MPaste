// output: Builds the panel UI with native Wayland flags and size-aware rounded tab backgrounds.
// MPasteWidgetUI.cpp — UI initialisation, theming, and page-selector logic.
// Split from MPasteWidget.cpp; shares the MPasteWidget class.
#include <QButtonGroup>
#include <QComboBox>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QAction>
#include <QApplication>
#include <QStyle>
#include <QStyleFactory>
#include <QStyledItemDelegate>
#include <QScreen>
#include <QGuiApplication>
#include <QWindow>
#include <QPropertyAnimation>
#include <QLocale>
#include <QIcon>
#include <QPushButton>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMenu>
#include <QFontDatabase>

#include "MPasteWidget.h"
#include "ui_MPasteWidget.h"
#include "utils/MPasteSettings.h"
#include "utils/ThemeManager.h"
#include "utils/IconResolver.h"
#include "BoardInternalHelpers.h"
#include "WindowBlurHelper.h"
#include "ScrollItemsWidget.h"

// ── helpers (anonymous namespace) ──────────────────────────────────────

namespace {

bool familyHasMediumWeight(const QString &family) {
    const QStringList styles = QFontDatabase::styles(family);
    for (const QString &style : styles) {
        const int weight = QFontDatabase::weight(family, style);
        if (weight >= QFont::Medium && weight <= QFont::DemiBold) {
            return true;
        }
    }
    return false;
}

// The top bar relies on 500/600 weights. Fonts such as Microsoft YaHei only
// ship Light/Regular/Bold, so those weights snap back to Regular and the bar
// reads thin; borrow a CJK sans that has a real Medium in that case.
QString topBarFontFamily() {
    const QString uiFamily = QApplication::font().family();
    if (familyHasMediumWeight(uiFamily)) {
        return uiFamily;
    }
    const QStringList available = QFontDatabase::families();
    for (const QString &family : {QStringLiteral("Noto Sans CJK SC"), QStringLiteral("Source Han Sans SC"),
                                  QStringLiteral("Source Han Sans CN"), QStringLiteral("Noto Sans SC")}) {
        if (available.contains(family, Qt::CaseInsensitive) && familyHasMediumWeight(family)) {
            return family;
        }
    }
    return uiFamily;
}

QString menuText(const char *source, const QString &zhFallback) {
    const QString translated = QObject::tr(source);
    const QLocale locale = QLocale::system();
    if (translated == QLatin1String(source) || BoardHelpers::looksBrokenTranslation(translated)) {
        if (locale.language() == QLocale::Chinese || locale.name().startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
            return zhFallback;
        }
        return QString::fromUtf8(source);
    }
    return translated;
}

class PageSelectorPill final : public QWidget {
public:
    explicit PageSelectorPill(QWidget *parent = nullptr)
        : QWidget(parent) {}

    void setColors(const QColor &background, const QColor &border, const QColor &divider) {
        background_ = background;
        border_ = border;
        divider_ = divider;
        update();
    }

    void setCornerRadius(int radius) {
        cornerRadius_ = radius;
        update();
    }

    void setDividerPosition(int x) {
        dividerX_ = x;
        update();
    }

    void setDividerInset(int inset) {
        dividerInset_ = inset;
        update();
    }

    void setPaintFrame(bool paintFrame) {
        paintFrame_ = paintFrame;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        if (!paintFrame_) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF rectF = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        if (border_.alpha() > 0) {
            painter.setPen(QPen(border_, 2));
        } else {
            painter.setPen(Qt::NoPen);
        }
        painter.setBrush(background_);
        painter.drawRoundedRect(rectF, cornerRadius_, cornerRadius_);

        if (dividerX_ > 0) {
            painter.setPen(QPen(divider_, 1));
            const int top = qBound(2, dividerInset_, height() / 2);
            const int bottom = qMax(top, height() - dividerInset_ - 1);
            painter.drawLine(QPointF(dividerX_, top), QPointF(dividerX_, bottom));
        }
    }

private:
    QColor background_ = QColor(Qt::white);
    QColor border_ = QColor(Qt::black);
    QColor divider_ = QColor(Qt::black);
    int cornerRadius_ = 14;
    int dividerX_ = 0;
    int dividerInset_ = 3;
    bool paintFrame_ = true;
};

} // anonymous namespace

// ── MPasteWidget UI methods ────────────────────────────────────────────

void MPasteWidget::initStyle() {
    Qt::WindowFlags flags = Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint | Qt::Tool | Qt::FramelessWindowHint;

#ifdef Q_OS_LINUX
    if (qEnvironmentVariable("XDG_SESSION_TYPE") != QStringLiteral("wayland"))
        flags |= Qt::X11BypassWindowManagerHint;
    flags |= Qt::Window;
#endif
    setWindowFlags(flags);

    setFocusPolicy(Qt::StrongFocus);

#ifdef Q_OS_WIN
    setAttribute(Qt::WA_InputMethodEnabled, false);
    setAttribute(Qt::WA_KeyCompression, false);
    setAttribute(Qt::WA_NoSystemBackground);
#else
    setAttribute(Qt::WA_TranslucentBackground);
#endif

    setAttribute(Qt::WA_AlwaysStackOnTop);
    ui_.ui->itemsWidget->setAttribute(Qt::WA_TranslucentBackground);
    ui_.ui->itemsWidget->setAttribute(Qt::WA_NoSystemBackground, false);

    setObjectName("pasteWidget");
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &MPasteWidget::applyTheme);
}

void MPasteWidget::initUI() {
    initSearchAnimations();

    ui_.clipboardWidget = new ScrollItemsWidget(
        MPasteSettings::CLIPBOARD_CATEGORY_NAME, MPasteSettings::CLIPBOARD_CATEGORY_COLOR, this);
    ui_.ui->clipboardButton->setProperty("category", ui_.clipboardWidget->getCategory());
    ui_.clipboardWidget->installEventFilter(this);
    ui_.boardWidgetMap.insert(ui_.clipboardWidget->getCategory(), ui_.clipboardWidget);

    ui_.staredWidget = new ScrollItemsWidget(
        MPasteSettings::STAR_CATEGORY_NAME, MPasteSettings::STAR_CATEGORY_COLOR, this);
    ui_.ui->staredButton->setProperty("category", ui_.staredWidget->getCategory());
    ui_.staredWidget->installEventFilter(this);
    ui_.boardWidgetMap.insert(ui_.staredWidget->getCategory(), ui_.staredWidget);

    ui_.layout = new QHBoxLayout(ui_.ui->itemsWidget);
    ui_.layout->setContentsMargins(0, 4, 0, 0);
    ui_.layout->setSpacing(0);
    ui_.layout->addWidget(ui_.clipboardWidget);
    ui_.layout->addWidget(ui_.staredWidget);

    ui_.staredWidget->hide();

    ui_.ui->clipboardButton->setText(menuText("Clipboard", QStringLiteral("\u526A\u8D34\u677F")));
    ui_.ui->staredButton->setText(menuText("Stared", QStringLiteral("\u6536\u85CF\u5939")));
    ui_.ui->allTypeBtn->setText(menuText("All", QStringLiteral("\u5168\u90E8")));
    ui_.ui->textTypeBtn->setText(menuText("Text", QStringLiteral("\u6587\u672C")));
    ui_.ui->linkTypeBtn->setText(menuText("Link", QStringLiteral("\u94FE\u63A5")));
    ui_.ui->imageTypeBtn->setText(menuText("Image", QStringLiteral("\u56FE\u7247")));
    ui_.ui->officeTypeBtn->setText(menuText("Office", QStringLiteral("Office")));
    ui_.ui->richTextTypeBtn->setText(menuText("Rich Text", QStringLiteral("\u5BCC\u6587\u672C")));
    ui_.ui->fileTypeBtn->setText(menuText("File", QStringLiteral("\u6587\u4EF6")));

    ui_.ui->searchButton->setText(QString());
    ui_.ui->searchButton->setIcon(IconResolver::themedIcon(QStringLiteral("search"), ThemeManager::instance()->isDark()));
    ui_.ui->searchButton->setIconSize(QSize(16, 16));
    ui_.ui->searchButton->setToolButtonStyle(Qt::ToolButtonIconOnly);

    ui_.ui->menuButton->setText(QString());
    ui_.ui->menuButton->setIcon(IconResolver::themedIcon(QStringLiteral("settings"), ThemeManager::instance()->isDark()));
    ui_.ui->menuButton->setIconSize(QSize(20, 20));
    ui_.ui->menuButton->setFixedSize(QSize(32, 28));
    ui_.ui->menuButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    ui_.ui->menuButton->setToolTip(menuText("More", QStringLiteral("\u66F4\u591A")));

    ui_.pageSelectorWidget = new PageSelectorPill(this);
    auto *pageLayout = new QHBoxLayout(ui_.pageSelectorWidget);
    pageLayout->setContentsMargins(8, 0, 8, 0);
    pageLayout->setSpacing(3);

    ui_.pagePrefixLabel = new QLabel(QStringLiteral("\u7B2C"), ui_.pageSelectorWidget);
    ui_.pagePrefixLabel->setAlignment(Qt::AlignCenter);
    ui_.pagePrefixLabel->setFixedWidth(14);

    // Hidden QComboBox kept for compatibility with updatePageSelector() which
    // reads its count/index.  The visible control is pageNumberLabel.
    ui_.pageComboBox = new QComboBox(ui_.pageSelectorWidget);
    ui_.pageComboBox->hide();
    ui_.pageComboBox->addItem(QStringLiteral("1"), 1);
    ui_.pageComboBox->setCurrentIndex(0);

    ui_.pageNumberLabel = new QLabel(QStringLiteral("1"), ui_.pageSelectorWidget);
    // Fixed-width halves meeting at the slash: digits grow outward, nothing shifts.
    ui_.pageNumberLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ui_.pageNumberLabel->setCursor(Qt::PointingHandCursor);
    ui_.pageNumberLabel->setFixedWidth(28);
    ui_.pageNumberLabel->installEventFilter(this);

    ui_.pageTotalLabel = new QLabel(QStringLiteral("/ 1"), ui_.pageSelectorWidget);
    ui_.pageTotalLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui_.pageTotalLabel->setFixedWidth(36);

    ui_.pageSuffixLabel = new QLabel(QStringLiteral("\u9875"), ui_.pageSelectorWidget);
    ui_.pageSuffixLabel->setAlignment(Qt::AlignCenter);
    ui_.pageSuffixLabel->setFixedWidth(14);

    pageLayout->addWidget(ui_.pagePrefixLabel);
    pageLayout->addWidget(ui_.pageNumberLabel);
    pageLayout->addWidget(ui_.pageTotalLabel);
    pageLayout->addWidget(ui_.pageSuffixLabel);
    // "1 / 3": the 第…页 frame was Chinese in every locale.
    ui_.pagePrefixLabel->hide();
    ui_.pageSuffixLabel->hide();
    ui_.pageSelectorWidget->setFixedWidth(110);

    if (ui_.ui->horizontalLayout) {
        const int countIndex = ui_.ui->horizontalLayout->indexOf(ui_.ui->countArea);
        ui_.ui->horizontalLayout->insertWidget(countIndex >= 0 ? countIndex : ui_.ui->horizontalLayout->count(),
                                               ui_.pageSelectorWidget);
    }
    ui_.ui->countArea->setFixedWidth(72);

    const QString chromeFamily = topBarFontFamily();
    QList<QWidget *> chromeWidgets{ui_.ui->countArea, ui_.pageNumberLabel, ui_.pageTotalLabel};
    for (QToolButton *button : ui_.ui->clipboardBtnWidget->findChildren<QToolButton *>()) {
        chromeWidgets << button;
    }
    for (QToolButton *button : ui_.ui->typeBtnWidget->findChildren<QToolButton *>()) {
        chromeWidgets << button;
    }
    for (QWidget *widget : chromeWidgets) {
        QFont font = widget->font();
        font.setFamily(chromeFamily);
        widget->setFont(font);
    }

    applyScale(MPasteSettings::getInst()->getItemScale());

    ui_.buttonGroup = new QButtonGroup(this);
    ui_.buttonGroup->setExclusive(true);
    ui_.buttonGroup->addButton(ui_.ui->clipboardButton);
    ui_.buttonGroup->addButton(ui_.ui->staredButton);

    ui_.typeButtonGroup = new QButtonGroup(this);
    ui_.typeButtonGroup->setExclusive(true);

    ui_.ui->allTypeBtn->setProperty("contentType", static_cast<int>(All));
    ui_.ui->textTypeBtn->setProperty("contentType", static_cast<int>(Text));
    ui_.ui->linkTypeBtn->setProperty("contentType", static_cast<int>(Link));
    ui_.ui->imageTypeBtn->setProperty("contentType", static_cast<int>(Image));
    ui_.ui->officeTypeBtn->setProperty("contentType", static_cast<int>(Office));
    ui_.ui->richTextTypeBtn->setProperty("contentType", static_cast<int>(RichText));
    ui_.ui->fileTypeBtn->setProperty("contentType", static_cast<int>(File));

    ui_.typeButtonGroup->addButton(ui_.ui->allTypeBtn);
    ui_.typeButtonGroup->addButton(ui_.ui->textTypeBtn);
    ui_.typeButtonGroup->addButton(ui_.ui->linkTypeBtn);
    ui_.typeButtonGroup->addButton(ui_.ui->imageTypeBtn);
    ui_.typeButtonGroup->addButton(ui_.ui->officeTypeBtn);
    ui_.typeButtonGroup->addButton(ui_.ui->richTextTypeBtn);
    ui_.typeButtonGroup->addButton(ui_.ui->fileTypeBtn);

    ui_.ui->allTypeBtn->setChecked(true);

    ui_.ui->clipboardButton->setChecked(true);

    initMenu();

    ui_.ui->searchEdit->installEventFilter(this);

    // Board and kind tabs share one capsule (the Aurora design): moved into a
    // container that paints the pill, with room for the hairline between them.
    if (ui_.ui->horizontalLayout) {
        const int at = ui_.ui->horizontalLayout->indexOf(ui_.ui->clipboardBtnWidget);
        ui_.tabCapsule = new QWidget(ui_.ui->widget);
        ui_.tabCapsule->setObjectName(QStringLiteral("tabCapsule"));
        ui_.tabCapsule->setAttribute(Qt::WA_TranslucentBackground);
        auto *capsuleLayout = new QHBoxLayout(ui_.tabCapsule);
        capsuleLayout->setContentsMargins(2, 2, 2, 2);
        capsuleLayout->setSpacing(0);
        ui_.ui->horizontalLayout->removeWidget(ui_.ui->clipboardBtnWidget);
        ui_.ui->horizontalLayout->removeWidget(ui_.ui->typeBtnWidget);
        capsuleLayout->addWidget(ui_.ui->clipboardBtnWidget);
        capsuleLayout->addSpacing(13);
        capsuleLayout->addWidget(ui_.ui->typeBtnWidget);
        ui_.ui->horizontalLayout->insertWidget(at >= 0 ? at : 0, ui_.tabCapsule, 0, Qt::AlignVCenter);
        ui_.tabCapsule->installEventFilter(this);
    }

    applyTheme(ThemeManager::instance()->isDark());
    updatePageSelector();
    updatePageSelectorStyle();
}

void MPasteWidget::initMenu() {
    ui_.menu = new QMenu(this);
    ui_.trayMenu = new QMenu(this);

    ui_.aboutAction = new QAction(
        IconResolver::themedIcon(QStringLiteral("info"), ThemeManager::instance()->isDark()),
        menuText("About", QString::fromUtf16(u"\u5173\u4E8E")),
        this);
    connect(ui_.aboutAction, &QAction::triggered, this, [this]() {
        AboutWidget *aboutWidget = ensureAboutWidget();
        QScreen *screen = QGuiApplication::primaryScreen();
        if (const QWindow *window = windowHandle())
            screen = window->screen();
        if (!screen)
            return;

        QRect screenGeometry = screen->geometry();
        int x = (screenGeometry.width() - aboutWidget->width()) / 2;
        int y = (screenGeometry.height() - aboutWidget->height()) / 2;
        aboutWidget->move(screenGeometry.x() + x, screenGeometry.y() + y);
        aboutWidget->show();
        aboutWidget->applyTheme(darkTheme_);
        aboutWidget->raise();
        aboutWidget->activateWindow();
    });

    ui_.settingsAction = new QAction(
        IconResolver::themedIcon(QStringLiteral("settings"), ThemeManager::instance()->isDark()),
        menuText("Settings", QString::fromUtf16(u"\u8BBE\u7F6E")),
        this);
    connect(ui_.settingsAction, &QAction::triggered, this, [this]() {
        MPasteSettingsWidget *settingsWidget = ensureSettingsWidget();
        QScreen *screen = QGuiApplication::primaryScreen();
        if (const QWindow *window = windowHandle())
            screen = window->screen();
        if (!screen)
            return;

        QRect screenGeometry = screen->geometry();
        int x = (screenGeometry.width() - settingsWidget->width()) / 2;
        int y = (screenGeometry.height() - settingsWidget->height()) / 2;
        settingsWidget->move(screenGeometry.x() + x, screenGeometry.y() + y);
        settingsWidget->show();
    });

    ui_.quitAction = new QAction(
        QIcon(QStringLiteral(":/resources/resources/quit.svg")),
        menuText("Quit", QString::fromUtf16(u"\u9000\u51FA")),
        this);
    connect(ui_.quitAction, &QAction::triggered, this, []() { qApp->exit(0); });

    const auto addActions = [this](QMenu *menu) {
        if (!menu) {
            return;
        }
        menu->addAction(ui_.settingsAction);
        menu->addSeparator();
        menu->addAction(ui_.quitAction);
    };
    addActions(ui_.menu);
    addActions(ui_.trayMenu);

    BoardHelpers::applyMenuTheme(ui_.menu);
    BoardHelpers::applyMenuTheme(ui_.trayMenu);
}

void MPasteWidget::initSystemTray() {
    ui_.trayIcon = new QSystemTrayIcon(this);
    ui_.trayIcon->setIcon(QIcon(":/resources/resources/mpaste.svg"));
    ui_.trayIcon->setContextMenu(ui_.trayMenu ? ui_.trayMenu : ui_.menu);
    ui_.trayIcon->show();
}

void MPasteWidget::initSearchAnimations() {
    ui_.searchHideAnim = new QPropertyAnimation(ui_.ui->searchEdit, "maximumWidth");
    ui_.searchHideAnim->setEndValue(0);
    ui_.searchHideAnim->setDuration(150);

    ui_.searchShowAnim = new QPropertyAnimation(ui_.ui->searchEdit, "maximumWidth");
    ui_.searchShowAnim->setEndValue(200);
    ui_.searchShowAnim->setDuration(150);
}

void MPasteWidget::applyTheme(bool dark) {
    darkTheme_ = dark;

    WindowBlurHelper::enableBlurBehind(this, darkTheme_);
    nativeBackdropBlur_ = WindowBlurHelper::enableNativeBlur(this);

    if (ui_.ui) {
        ui_.ui->menuButton->setIcon(IconResolver::themedIcon(QStringLiteral("settings"), darkTheme_));
        ui_.ui->searchButton->setIcon(QIcon(darkTheme_
            ? QStringLiteral(":/resources/resources/search_light.svg")
            : QStringLiteral(":/resources/resources/search.svg")));
        ui_.ui->searchButton->setIconSize(QSize(16, 16));
        ui_.ui->searchButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
        ui_.ui->searchButton->setText(QString());
        ui_.ui->firstButton->setIcon(IconResolver::themedIcon(QStringLiteral("first"), darkTheme_));
        ui_.ui->lastButton->setIcon(IconResolver::themedIcon(QStringLiteral("last"), darkTheme_));
    }

    if (ui_.aboutAction) {
        ui_.aboutAction->setIcon(IconResolver::themedIcon(QStringLiteral("info"), darkTheme_));
    }
    if (ui_.settingsAction) {
        ui_.settingsAction->setIcon(IconResolver::themedIcon(QStringLiteral("settings"), darkTheme_));
    }
    if (ui_.quitAction) {
        ui_.quitAction->setIcon(QIcon(QStringLiteral(":/resources/resources/quit.svg")));
    }
    // Pill tabs: the radius lives in the same rule as the background, or Qt
    // paints the chosen tab square.
    // Kept under half the tab's real height, which can come in below its
    // maximum: QSS drops a radius that is too large and paints square.
    const int tabPillRadius = ui_.ui ? qMax(6, ui_.ui->clipboardButton->maximumHeight() / 2 - 2) : 12;
    // Force glass-style tab borders in dark mode (QSS tokens don't override reliably)
    if (darkTheme_ && ui_.ui) {
        const QString glassTabStyle = QStringLiteral(
            "QToolButton { background-color: transparent; border: none; border-radius: %1px; }"
            "QToolButton:hover { background-color: rgba(255,255,255,20); }"
            "QToolButton:checked, QToolButton:pressed { background-color: rgba(255,255,255,38); }").arg(tabPillRadius);
        ui_.ui->clipboardBtnWidget->setStyleSheet(glassTabStyle);
        ui_.ui->typeBtnWidget->setStyleSheet(glassTabStyle);
    } else if (ui_.ui) {
        // Light mode: glass style with dark tint
        const QString lightGlassTabStyle = QStringLiteral(
            "QToolButton { background-color: transparent; border: none; border-radius: %1px; }"
            "QToolButton:hover { background-color: rgba(0,0,0,10); }"
            "QToolButton:checked, QToolButton:pressed { background-color: rgba(255,255,255,235); }").arg(tabPillRadius);
        ui_.ui->clipboardBtnWidget->setStyleSheet(lightGlassTabStyle);
        ui_.ui->typeBtnWidget->setStyleSheet(lightGlassTabStyle);
    }
    // The search box takes the tab groups' glass, set on the widget for the same
    // reason: the desktop sheet's focused and hovered field rules win otherwise.
    // It holds the focus whenever it is open, so focus only lifts the edge.
    if (ui_.ui) {
        const int searchRadius = qMax(6, ui_.ui->searchEdit->maximumHeight() / 2 - 1);
        ui_.ui->searchEdit->setStyleSheet((darkTheme_
            ? QStringLiteral(
                "QLineEdit, QLineEdit:hover { background-color: rgba(255,255,255,15); border: 1px solid rgba(255,255,255,20);"
                " border-radius: %1px; padding: 1px 12px; color: #F2F6FB; selection-background-color: rgba(91,141,239,150); }"
                "QLineEdit:focus { background-color: rgba(255,255,255,22); border: 1px solid rgba(255,255,255,46); }")
            : QStringLiteral(
                "QLineEdit, QLineEdit:hover { background-color: rgba(255,255,255,150); border: 1px solid rgba(0,0,0,20);"
                " border-radius: %1px; padding: 1px 12px; color: #2C3E50; selection-background-color: rgba(47,104,216,90); }"
                "QLineEdit:focus { background-color: rgba(255,255,255,190); border: 1px solid rgba(0,0,0,36); }")).arg(searchRadius));
    }

    updatePageSelectorStyle();
    BoardHelpers::applyMenuTheme(ui_.menu);
    BoardHelpers::applyMenuTheme(ui_.trayMenu);
    update();
}

void MPasteWidget::applyScale(int scale) {
    if (ui_.clipboardWidget) {
        ui_.clipboardWidget->applyScale(scale);
    }
    if (ui_.staredWidget) {
        ui_.staredWidget->applyScale(scale);
    }

    auto scaleFont = [scale](QWidget *widget) {
        if (!widget) {
            return;
        }
        QFont font = widget->font();
        QVariant baseVar = widget->property("baseFontPt");
        qreal base = baseVar.isValid() ? baseVar.toReal() : font.pointSizeF();
        if (!baseVar.isValid()) {
            widget->setProperty("baseFontPt", base);
        }
        if (base > 0) {
            font.setPointSizeF(qMax<qreal>(8.0, base * scale / 100.0));
            widget->setFont(font);
        }
    };

    auto scaleHeight = [scale](QWidget *widget, int minBase = 24) {
        if (!widget) {
            return;
        }
        QVariant baseVar = widget->property("baseHeight");
        int base = baseVar.isValid() ? baseVar.toInt() : widget->sizeHint().height();
        if (!baseVar.isValid()) {
            widget->setProperty("baseHeight", base);
        }
        const int height = qMax(minBase, base * scale / 100);
        widget->setMinimumHeight(height);
        widget->setMaximumHeight(height);
    };
    auto applyRadius = [this](QWidget *widget) {
        if (!widget) {
            return;
        }
        const int height = qMax(1, widget->maximumHeight());
        const int radius = qMax(0, height / 2 - 1);
        widget->setProperty("roundedTab", true);
        widget->installEventFilter(this);
        widget->setStyleSheet(QStringLiteral("border-radius: %1px;").arg(radius));
    };

    // The bar scales with the cards; set outright, since the form's own minimum
    // heights (36 and 28) would otherwise win over anything smaller.
    auto fixHeight = [](QWidget *widget, int height) {
        if (widget) {
            widget->setMinimumHeight(height);
            widget->setMaximumHeight(height);
        }
    };
    const int topBarHeight = qMax(26, 30 * scale / 100);
    if (ui_.ui->topLine) {
        ui_.ui->topLine->setFixedHeight(qMax(1, scale / 20));
    }
    fixHeight(ui_.ui->widget, topBarHeight);

    const int tabButtonHeight = qMax(20, 24 * scale / 100);
    fixHeight(ui_.ui->clipboardButton, tabButtonHeight);
    fixHeight(ui_.ui->staredButton, tabButtonHeight);
    fixHeight(ui_.ui->allTypeBtn, tabButtonHeight);
    fixHeight(ui_.ui->textTypeBtn, tabButtonHeight);
    fixHeight(ui_.ui->linkTypeBtn, tabButtonHeight);
    fixHeight(ui_.ui->imageTypeBtn, tabButtonHeight);
    fixHeight(ui_.ui->officeTypeBtn, tabButtonHeight);
    fixHeight(ui_.ui->richTextTypeBtn, tabButtonHeight);
    fixHeight(ui_.ui->fileTypeBtn, tabButtonHeight);
    scaleFont(ui_.ui->clipboardButton);
    scaleFont(ui_.ui->staredButton);
    scaleFont(ui_.ui->allTypeBtn);
    scaleFont(ui_.ui->textTypeBtn);
    scaleFont(ui_.ui->linkTypeBtn);
    scaleFont(ui_.ui->imageTypeBtn);
    scaleFont(ui_.ui->officeTypeBtn);
    scaleFont(ui_.ui->richTextTypeBtn);
    scaleFont(ui_.ui->fileTypeBtn);

    applyRadius(ui_.ui->clipboardButton);
    applyRadius(ui_.ui->staredButton);
    applyRadius(ui_.ui->allTypeBtn);
    applyRadius(ui_.ui->textTypeBtn);
    applyRadius(ui_.ui->linkTypeBtn);
    applyRadius(ui_.ui->imageTypeBtn);
    applyRadius(ui_.ui->officeTypeBtn);
    applyRadius(ui_.ui->richTextTypeBtn);
    applyRadius(ui_.ui->fileTypeBtn);

    if (ui_.ui->searchEdit) {
        fixHeight(ui_.ui->searchEdit, tabButtonHeight);
    }
    if (ui_.ui->firstButton) {
        fixHeight(ui_.ui->firstButton, tabButtonHeight);
    }
    if (ui_.ui->lastButton) {
        fixHeight(ui_.ui->lastButton, tabButtonHeight);
    }
    if (ui_.pageComboBox) {
        fixHeight(ui_.pageComboBox, tabButtonHeight);
        scaleFont(ui_.pageComboBox);
    }
    if (ui_.pagePrefixLabel) {
        scaleFont(ui_.pagePrefixLabel);
        ui_.pagePrefixLabel->setFixedWidth(qMax(12, 14 * scale / 100));
    }
    if (ui_.pageTotalLabel) {
        scaleFont(ui_.pageTotalLabel);
        ui_.pageTotalLabel->setFixedWidth(qMax(32, 36 * scale / 100));
    }
    if (ui_.pageSuffixLabel) {
        scaleFont(ui_.pageSuffixLabel);
        ui_.pageSuffixLabel->setFixedWidth(qMax(12, 14 * scale / 100));
    }
    if (ui_.pageSelectorWidget) {
        ui_.pageSelectorWidget->setFixedHeight(tabButtonHeight);
        ui_.pageSelectorWidget->setFixedWidth(qMax(96, 110 * scale / 100));
    }
    if (ui_.ui->countArea) {
        ui_.ui->countArea->setFixedWidth(qMax(64, 72 * scale / 100));
    }
    updatePageSelector();
    updatePageSelectorStyle();

    if (ui_.layout) {
        ui_.layout->setContentsMargins(0, qMax(3, 4 * scale / 100), 0, 0);
    }

    const int newHeight = ui_.ui->widget->height()
        + ui_.ui->itemsWidget->sizeHint().height()
        + qMax(3, 4 * scale / 100)
        + qMax(1, scale / 20);
    setFixedHeight(newHeight);

    QScreen *screen = this->screen();
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen) {
        const QRect avail = screen->availableGeometry();
        QRect geom = geometry();
        const int newWidth = geom.width();
        int newY = avail.bottom() - newHeight + 1;
        if (newY < avail.top()) {
            newY = avail.top();
        }
        int newX = geom.x();
        if (newX + newWidth > avail.right()) {
            newX = avail.right() - newWidth + 1;
        }
        if (newX < avail.left()) {
            newX = avail.left();
        }
        setGeometry(newX, newY, newWidth, newHeight);
    }
}

void MPasteWidget::updatePageSelector() {
    if (!ui_.pageSelectorWidget || !ui_.pagePrefixLabel || !ui_.pageComboBox || !ui_.pageTotalLabel || !ui_.pageSuffixLabel) {
        return;
    }

    const bool pagedMode = MPasteSettings::getInst()->getHistoryViewMode() == MPasteSettings::ViewModePaged;
    ui_.pageSelectorWidget->setVisible(pagedMode);
    if (!pagedMode) {
        return;
    }

    ScrollItemsWidget *board = currItemsWidget();
    const int totalPages = board ? board->totalPageCount() : 0;
    const int currentPage = board ? board->currentPageNumber() : 0;

    const int clampedTotalPages = qMax(1, totalPages);
    ui_.pageComboBox->blockSignals(true);
    if (ui_.pageComboBox->count() != clampedTotalPages) {
        ui_.pageComboBox->clear();
        for (int page = 1; page <= clampedTotalPages; ++page) {
            ui_.pageComboBox->addItem(QString::number(page), page);
        }
    }
    ui_.pageComboBox->setCurrentIndex(qBound(0, (currentPage > 0 ? currentPage : 1) - 1, clampedTotalPages - 1));
    ui_.pageComboBox->blockSignals(false);
    // Clamped like the current page: an unloaded board reads "1 / 1", never "/ 0".
    ui_.pageTotalLabel->setText(QStringLiteral("/ %1").arg(clampedTotalPages));

    const int displayPage = qBound(1, currentPage > 0 ? currentPage : 1, clampedTotalPages);
    ui_.pageNumberLabel->setText(QString::number(displayPage));

    // Room for three digits whatever the page count, so nothing beside it shifts as it changes.
    const QString digitsReserve = QString(qMax(3, int(QString::number(clampedTotalPages).size())), QLatin1Char('8'));
    const QFontMetrics labelMetrics(ui_.pageNumberLabel->font());
    const int labelWidth = qMax(20, labelMetrics.horizontalAdvance(digitsReserve) + 8);
    ui_.pageNumberLabel->setFixedWidth(labelWidth);

    const QFontMetrics totalMetrics(ui_.pageTotalLabel->font());
    const int totalWidth = qMax(26, totalMetrics.horizontalAdvance(QStringLiteral("/ ") + digitsReserve) + 4);
    ui_.pageTotalLabel->setFixedWidth(totalWidth);

    if (ui_.pageSelectorWidget && ui_.pageSelectorWidget->layout()) {
        const QMargins margins = ui_.pageSelectorWidget->layout()->contentsMargins();
        const int spacing = ui_.pageSelectorWidget->layout()->spacing();
        auto shown = [spacing](QWidget *label) { return label->isHidden() ? 0 : label->width() + spacing; };
        ui_.pageSelectorWidget->setFixedWidth(margins.left()
                                              + shown(ui_.pagePrefixLabel)
                                              + labelWidth
                                              + spacing
                                              + totalWidth
                                              + shown(ui_.pageSuffixLabel)
                                              + margins.right());
    }
}

void MPasteWidget::updatePageSelectorStyle() {
    if (!ui_.pageSelectorWidget || !ui_.pagePrefixLabel || !ui_.pageComboBox || !ui_.pageTotalLabel || !ui_.pageSuffixLabel) {
        return;
    }

    const int height = qMax(24, ui_.pageSelectorWidget->height());
    const QString text = darkTheme_ ? QStringLiteral("#F2F6FB") : QStringLiteral("#2C3E50");
    if (auto *pill = static_cast<PageSelectorPill *>(ui_.pageSelectorWidget)) {
        // The same quiet base as the tab groups beside it.
        ui_.pageSelectorWidget->layout()->activate();
        pill->setColors(darkTheme_ ? QColor(255, 255, 255, 15) : QColor(0, 0, 0, 10), Qt::transparent, Qt::transparent);
        pill->setCornerRadius(height / 2);
        pill->setDividerPosition(0);
        pill->setPaintFrame(true);
    }
    const QString hoverBg = darkTheme_ ? QStringLiteral("rgba(255, 255, 255, 22)") : QStringLiteral("rgba(0, 0, 0, 12)");
    const QString hoverColor = darkTheme_ ? QStringLiteral("#4A90E2") : QStringLiteral("#2A6FC9");
    ui_.pageNumberLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 13px; font-weight: 600; background: transparent;"
        " border-radius: 5px; padding: 1px 3px; }"
        "QLabel:hover { color: %2; background: %3; }").arg(text, hoverColor, hoverBg));
    const QString secondary = darkTheme_ ? QStringLiteral("rgba(242, 246, 251, 185)")
                                         : QStringLiteral("rgba(44, 62, 80, 185)");
    ui_.pagePrefixLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 13px; font-weight: 600; background: transparent; }").arg(secondary));
    ui_.pageTotalLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 13px; font-weight: 500; background: transparent; }").arg(secondary));
    ui_.pageSuffixLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 13px; font-weight: 600; background: transparent; }").arg(secondary));
}

void MPasteWidget::updateItemCount(int itemCount) {
    const int selectedCount = currItemsWidget() ? currItemsWidget()->selectedItemCount() : 0;
    // "12 条" rather than a bare number nobody can place.
    ui_.ui->countArea->setText(selectedCount > 1
        ? QStringLiteral("%1/%2").arg(selectedCount).arg(itemCount)
        : (BoardHelpers::prefersChineseUi() ? QStringLiteral("%1 条").arg(itemCount) : QString::number(itemCount)));
    ui_.ui->countArea->updateGeometry();
}
