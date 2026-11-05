// input: Depends on OnboardingDialog.h, AuroraChrome, MPasteSettings, OcrService and ThemeManager.
// output: The first-run guide: welcome, GNOME extension, hotkey, practice, finding, last check.
// pos: Widget-layer dialog MPasteWidget shows on first run and from Settings → About.
// update: If I change, update this header block and my folder README.md.
#include "OnboardingDialog.h"

#include "AuroraChrome.h"
#include "BoardInternalHelpers.h"
#include "utils/MPasteSettings.h"
#include "utils/OcrService.h"
#include "utils/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QThread>
#include <QTimer>
#include <functional>
#include <QVBoxLayout>
#include <QWindow>

#ifdef Q_OS_LINUX
#include "utils/GnomeBridge.h"
#endif

namespace {
constexpr int kShadow = 10;
constexpr int kWidth = 740;
constexpr int kHeight = 560;
constexpr int kPad = 40;

struct Tone {
    QColor ink, muted, faint, hair, pane, keyTop, keyBottom, keyEdge, accent, accentSoft, ok, warn, card;
};

Tone toneFor(bool dark) {
    Tone t;
    t.ink = dark ? QColor(236, 238, 242) : QColor(28, 30, 37);
    t.muted = dark ? QColor(255, 255, 255, 150) : QColor(28, 30, 37, 160);
    t.faint = dark ? QColor(255, 255, 255, 95) : QColor(28, 30, 37, 110);
    t.hair = dark ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 20);
    t.pane = dark ? QColor(255, 255, 255, 10) : QColor(255, 255, 255, 170);
    t.keyTop = dark ? QColor(42, 44, 51) : QColor(255, 255, 255);
    t.keyBottom = dark ? QColor(32, 34, 39) : QColor(236, 238, 242);
    t.keyEdge = dark ? QColor(255, 255, 255, 26) : QColor(0, 0, 0, 34);
    t.accent = QColor(107, 140, 255);
    t.accentSoft = QColor(107, 140, 255, dark ? 42 : 34);
    t.ok = dark ? QColor(111, 207, 151) : QColor(30, 150, 80);
    t.warn = dark ? QColor(242, 180, 80) : QColor(200, 130, 20);
    t.card = dark ? QColor(31, 33, 39) : QColor(255, 255, 255);
    return t;
}

QString rgba(const QColor &c) {
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

bool currentDark() {
    return ThemeManager::instance()->isDark();
}

// The global hotkey's keys: "Alt+Q" as "Alt", "Q".
QStringList hotkeyKeys() {
    const QString text = QKeySequence(MPasteSettings::getInst()->getShortcutStr()).toString(QKeySequence::NativeText);
    QStringList keys = text.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    return keys.isEmpty() ? QStringList{QStringLiteral("Alt"), QStringLiteral("Q")} : keys;
}
}  // namespace

// A key, drawn as a key: big on the hotkey step, small in the text.
class GuideKeycap : public QWidget {
public:
    GuideKeycap(const QString &text, bool big, QWidget *parent = nullptr)
        : QWidget(parent), text_(text), big_(big) {
        QFont f = font();
        f.setFamily(QStringLiteral("monospace"));
        f.setBold(true);
        f.setPixelSize(big ? 22 : 13);
        setFont(f);
        const int w = QFontMetrics(f).horizontalAdvance(text) + (big ? 40 : 18);
        setFixedSize(qMax(big ? 72 : 30, w), big ? 64 : 30);
    }
    void setDown(bool down) { if (down_ != down) { down_ = down; update(); } }
    bool isDown() const { return down_; }
    const QString &text() const { return text_; }

protected:
    void paintEvent(QPaintEvent *) override {
        const Tone t = toneFor(currentDark());
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal radius = big_ ? 14 : 8;
        const qreal press = down_ ? (big_ ? 3 : 1) : 0;
        const QRectF body = QRectF(rect()).adjusted(1, 1 + press, -1, -1);
        // The key's side, then its face.
        p.setPen(Qt::NoPen);
        p.setBrush(t.keyEdge);
        p.drawRoundedRect(body, radius, radius);
        QRectF face = body.adjusted(1, 1, -1, -(big_ ? 4 : 2) + press);
        QLinearGradient g(face.topLeft(), face.bottomLeft());
        g.setColorAt(0, down_ ? t.accentSoft.lighter(130) : t.keyTop);
        g.setColorAt(1, down_ ? t.accentSoft : t.keyBottom);
        p.setBrush(g);
        if (down_) p.setPen(QPen(t.accent, 1.5));
        p.drawRoundedRect(face, radius - 1, radius - 1);
        p.setPen(t.ink);
        p.drawText(face, Qt::AlignCenter, text_);
    }

private:
    QString text_;
    bool big_ = false;
    bool down_ = false;
};

// Step dots: the current one long, in the accent.
class GuideDots : public QWidget {
public:
    using QWidget::QWidget;
    void set(int count, int current) { count_ = count; current_ = current; update(); }
    QSize sizeHint() const override { return QSize(count_ * 14 + 16, 10); }

protected:
    void paintEvent(QPaintEvent *) override {
        const Tone t = toneFor(currentDark());
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        qreal x = 0;
        for (int i = 0; i < count_; ++i) {
            const qreal w = i == current_ ? 22 : 7;
            p.setBrush(i == current_ ? t.accent : t.hair.lighter(200));
            p.drawRoundedRect(QRectF(x, (height() - 7) / 2.0, w, 7), 3.5, 3.5);
            x += w + 6;
        }
    }

private:
    int count_ = 0;
    int current_ = 0;
};

// A row of made-up cards. In practice mode it takes the keys: arrows pick,
// Enter "pastes", Alt shows numbers, Alt+digit picks and "pastes" that one.
class GuidePractice : public QWidget {
public:
    struct Card { QColor app; QString source; QString body; bool picture; };

    GuidePractice(bool interactive, std::function<QString(const char *, const QString &)> text, QWidget *parent = nullptr)
        : QWidget(parent), interactive_(interactive), text_(std::move(text)) {
        cards_ = {
            {QColor(79, 140, 255), text_("Notes", QStringLiteral("示例笔记")), text_("Friday meeting: 2 pm, room 3", QStringLiteral("周五例会：下午两点三楼会议室")), false},
            {QColor(34, 176, 125), text_("Browser", QStringLiteral("示例浏览器")), QStringLiteral("https://example.org/docs/getting-started"), false},
            {QColor(255, 138, 76), text_("Screenshot", QStringLiteral("截图工具")), QString(), true},
            {QColor(164, 107, 255), text_("Terminal", QStringLiteral("终端")), QStringLiteral("git commit -m \"Fix preview zoom\""), false},
        };
        setFixedHeight(128);
        setFocusPolicy(interactive ? Qt::StrongFocus : Qt::NoFocus);
    }

    std::function<void(int goal, const QString &message)> onGoal;

    void showNumbers(bool on) { if (numbers_ != on) { numbers_ = on; update(); } }

protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (!interactive_) return QWidget::keyPressEvent(event);
        const int key = event->key();
        if (key == Qt::Key_Alt) { showNumbers(true); return; }
        if (key == Qt::Key_Left || key == Qt::Key_Right) {
            selected_ = (selected_ + (key == Qt::Key_Right ? 1 : -1) + int(cards_.size())) % int(cards_.size());
            update();
            if (onGoal) onGoal(0, QString());
            return;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            const bool plain = event->modifiers() & Qt::ControlModifier;
            if (onGoal) onGoal(1, plain ? text_("Pasted item %1 as plain text (practice: nothing is pasted)", QStringLiteral("已粘贴第 %1 条为纯文本（练习，不会真的粘贴）")).arg(selected_ + 1)
                                        : text_("Pasted item %1 (practice: nothing is pasted)", QStringLiteral("已粘贴第 %1 条（练习，不会真的粘贴）")).arg(selected_ + 1));
            return;
        }
        if ((event->modifiers() & Qt::AltModifier) && key >= Qt::Key_1 && key <= Qt::Key_4) {
            selected_ = key - Qt::Key_1;
            numbers_ = true;
            update();
            if (onGoal) onGoal(key == Qt::Key_2 ? 2 : -1, text_("Alt + %1: pasted item %1 at once", QStringLiteral("Alt + %1：直接粘贴第 %1 条")).arg(selected_ + 1));
            return;
        }
        QWidget::keyPressEvent(event);
    }
    void keyReleaseEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Alt) showNumbers(false);
        QWidget::keyReleaseEvent(event);
    }
    void focusOutEvent(QFocusEvent *event) override {
        showNumbers(false);
        QWidget::focusOutEvent(event);
    }
    void mousePressEvent(QMouseEvent *event) override {
        setFocus();
        QWidget::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent *) override {
        const Tone t = toneFor(currentDark());
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // Four cards across whatever room there is.
        const int gap = 12, top = 10, left = 10, h = 108;
        const int w = qMin(150, (width() - 2 * left - 3 * gap) / 4);
        QFont small = font();
        small.setPixelSize(11);
        QFont body = font();
        body.setPixelSize(12);
        for (int i = 0; i < int(cards_.size()); ++i) {
            const Card &c = cards_[i];
            const QRectF r(left + i * (w + gap), top, w, h);
            if (r.right() > width()) break;
            const bool chosen = interactive_ && i == selected_;
            p.setPen(chosen ? QPen(t.accent, 2) : QPen(t.hair, 1));
            p.setBrush(t.card);
            p.drawRoundedRect(r, 12, 12);
            p.setPen(Qt::NoPen);
            p.setBrush(c.app);
            p.drawRoundedRect(QRectF(r.left() + 12, r.top() + 11, 14, 14), 4, 4);
            p.setFont(small);
            p.setPen(t.muted);
            p.drawText(QRectF(r.left() + 32, r.top() + 9, w - 40, 18), Qt::AlignVCenter | Qt::AlignLeft, c.source);
            const QRectF content(r.left() + 12, r.top() + 34, w - 24, h - 44);
            if (c.picture) {
                QLinearGradient g(content.topLeft(), content.bottomRight());
                g.setColorAt(0, QColor(122, 167, 232));
                g.setColorAt(1, QColor(182, 215, 194));
                p.setBrush(g);
                p.drawRoundedRect(content, 7, 7);
            } else {
                p.setFont(body);
                p.setPen(t.ink);
                p.drawText(content, Qt::TextWordWrap | Qt::AlignTop | Qt::AlignLeft, c.body);
            }
            if (numbers_ && interactive_) {
                const QRectF badge(r.left() - 8, r.top() - 8, 22, 22);
                p.setPen(Qt::NoPen);
                p.setBrush(t.accent);
                p.drawRoundedRect(badge, 7, 7);
                QFont f = font();
                f.setBold(true);
                f.setPixelSize(12);
                p.setFont(f);
                p.setPen(Qt::white);
                p.drawText(badge, Qt::AlignCenter, QString::number(i + 1));
            }
        }
        if (interactive_ && hasFocus()) {
            return;
        }
        if (interactive_) {
            p.setPen(t.faint);
            p.setFont(small);
            p.drawText(QRectF(0, height() - 16, width(), 16), Qt::AlignRight | Qt::AlignVCenter,
                       text_("Click here, then try the keys", QStringLiteral("点一下这里，再试试按键")));
        }
    }

private:
    bool interactive_ = false;
    bool numbers_ = false;
    int selected_ = 0;
    std::vector<Card> cards_;
    std::function<QString(const char *, const QString &)> text_;
};

OnboardingDialog::OnboardingDialog(bool needsExtension, QWidget *parent)
    : QDialog(parent), needsExtension_(needsExtension) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setWindowTitle(QStringLiteral("MPaste"));
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(kWidth + 2 * kShadow, kHeight + 2 * kShadow);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(kShadow + 20, kShadow + 18, kShadow + 20, kShadow + 18);
    root->setSpacing(0);

    // Top: the name, and a way out.
    auto *top = new QHBoxLayout;
    auto *logo = new QLabel(this);
    logo->setPixmap(QIcon(QStringLiteral(":/resources/resources/mpaste.svg")).pixmap(QSize(26, 26), devicePixelRatioF()));
    top->addWidget(logo);
    auto *name = new QLabel(QStringLiteral("MPaste"), this);
    name->setObjectName(QStringLiteral("guideName"));
    top->addWidget(name, 1);
    auto *skip = new QPushButton(uiText("Skip the guide", QStringLiteral("跳过引导")), this);
    skip->setObjectName(QStringLiteral("guideSkip"));
    skip->setCursor(Qt::PointingHandCursor);
    skip->setFocusPolicy(Qt::NoFocus);
    connect(skip, &QPushButton::clicked, this, &QDialog::accept);
    top->addWidget(skip);
    root->addLayout(top);

    pages_ = new QStackedWidget(this);
    pages_->addWidget(welcomePage());
    if (needsExtension_) {
        pages_->addWidget(extensionPage());
    }
    hotkeyPageIndex_ = pages_->addWidget(hotkeyPage());
    practicePageIndex_ = pages_->addWidget(practicePage());
    pages_->addWidget(findPage());
    pages_->addWidget(readyPage());
    root->addWidget(pages_, 1);

    // Bottom: where we are, and the way on.
    auto *bottom = new QHBoxLayout;
    bottom->setSpacing(12);
    dots_ = new GuideDots(this);
    dots_->setFixedHeight(12);
    bottom->addWidget(dots_, 1, Qt::AlignVCenter);
    keyHint_ = new QLabel(this);
    keyHint_->setObjectName(QStringLiteral("guideFaint"));
    bottom->addWidget(keyHint_);
    back_ = new QPushButton(uiText("Back", QStringLiteral("上一步")), this);
    back_->setObjectName(QStringLiteral("auroraSecondary"));
    back_->setCursor(Qt::PointingHandCursor);
    back_->setFocusPolicy(Qt::NoFocus);
    connect(back_, &QPushButton::clicked, this, [this]() { goTo(pages_->currentIndex() - 1); });
    bottom->addWidget(back_);
    next_ = new QPushButton(this);
    next_->setObjectName(QStringLiteral("auroraPrimary"));
    next_->setCursor(Qt::PointingHandCursor);
    next_->setFocusPolicy(Qt::NoFocus);
    connect(next_, &QPushButton::clicked, this, [this]() {
        if (pages_->currentIndex() == pages_->count() - 1) accept();
        else goTo(pages_->currentIndex() + 1);
    });
    bottom->addWidget(next_);
    root->addLayout(bottom);

    applyTheme(currentDark());
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &OnboardingDialog::applyTheme);
    goTo(0);
}

QString OnboardingDialog::uiText(const char *source, const QString &zh) const {
    const QString translated = tr(source);
    // Compared as UTF-8: a source with an arrow or an ellipsis is not Latin-1.
    if (translated == QString::fromUtf8(source) || BoardHelpers::looksBrokenTranslation(translated)) {
        return BoardHelpers::prefersChineseUi() ? zh : QString::fromUtf8(source);
    }
    return translated;
}

namespace {
QWidget *pageShell(QWidget *parent, const QString &eyebrow, const QString &title, const QString &lead, QVBoxLayout **body) {
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(kPad - 20, 22, kPad - 20, 12);
    layout->setSpacing(16);
    auto *heads = new QVBoxLayout;
    heads->setSpacing(6);
    auto *e = new QLabel(eyebrow, page);
    e->setObjectName(QStringLiteral("guideEyebrow"));
    heads->addWidget(e);
    auto *t = new QLabel(title, page);
    t->setObjectName(QStringLiteral("guideTitle"));
    t->setWordWrap(true);
    heads->addWidget(t);
    layout->addLayout(heads);
    if (!lead.isEmpty()) {
        auto *l = new QLabel(lead, page);
        l->setObjectName(QStringLiteral("guideLead"));
        l->setWordWrap(true);
        l->setTextFormat(Qt::RichText);
        layout->addWidget(l);
    }
    *body = layout;
    return page;
}

QLabel *panel(const QString &html, QWidget *parent) {
    auto *label = new QLabel(html, parent);
    label->setObjectName(QStringLiteral("guidePanel"));
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    return label;
}

// Small keys inside running text.
QString kbd(const QString &key) {
    return QStringLiteral("<span style=\"font-family:monospace; font-weight:700; background:rgba(128,128,128,0.22);\">&nbsp;%1&nbsp;</span>")
        .arg(key.toHtmlEscaped());
}
}  // namespace

QWidget *OnboardingDialog::welcomePage() {
    QVBoxLayout *body = nullptr;
    QWidget *page = pageShell(pages_, uiText("Welcome", QStringLiteral("欢迎")),
                              uiText("Everything you copy, MPaste keeps for you", QStringLiteral("你复制过的，MPaste 都替你记着")),
                              uiText("Text, pictures, links, files, colours: each copy becomes a card that says which app it came from and when. "
                                     "Find it again when you need it and paste it anywhere.",
                                     QStringLiteral("文字、图片、链接、文件、颜色……每次复制都会变成一张卡片，标着来自哪个应用、什么时候。需要时再把它找回来，粘贴到任何地方。")),
                              &body);
    body->addWidget(new GuidePractice(false, [this](const char *s, const QString &z) { return uiText(s, z); }, page));
    auto *facts = new QHBoxLayout;
    facts->setSpacing(10);
    facts->addWidget(panel(uiText("<b>Nothing to learn</b><br>Copy as always; MPaste records it in the background.",
                                  QStringLiteral("<b>不用改习惯</b><br>照常复制，MPaste 在后台记录。")), page));
    facts->addWidget(panel(uiText("<b>One key away</b><br>One shortcut opens it; type to search.",
                                  QStringLiteral("<b>一键找回</b><br>一个快捷键打开，打字就能搜。")), page));
    facts->addWidget(panel(uiText("<b>Private</b><br>What a password manager copies is not recorded.",
                                  QStringLiteral("<b>放心</b><br>密码管理器复制的内容不会被记录。")), page));
    body->addLayout(facts);
    body->addStretch(1);
    return page;
}

QWidget *OnboardingDialog::extensionPage() {
    QVBoxLayout *body = nullptr;
    QWidget *page = pageShell(pages_, uiText("Before we start", QStringLiteral("开始之前")),
                              uiText("Turn on MPaste's GNOME extension", QStringLiteral("先启用 MPaste 的 GNOME 扩展")),
                              uiText("On GNOME Wayland, apps cannot see the clipboard or a global shortcut by themselves. "
                                     "MPaste's small extension does that part; it is installed but not on yet.",
                                     QStringLiteral("在 GNOME Wayland 上，应用自己读不到剪贴板，也注册不了全局快捷键。"
                                                    "MPaste 的小扩展负责这部分：它已经装好了，只是还没启用。")),
                              &body);
    auto *row = new QHBoxLayout;
    auto *state = panel(uiText("Not on yet.", QStringLiteral("尚未启用。")), page);
    row->addWidget(state, 1);
    auto *enable = new QPushButton(uiText("Turn it on", QStringLiteral("启用扩展")), page);
    enable->setObjectName(QStringLiteral("auroraPrimary"));
    enable->setCursor(Qt::PointingHandCursor);
    row->addWidget(enable, 0, Qt::AlignTop);
    body->addLayout(row);
    connect(enable, &QPushButton::clicked, this, [this, state, enable]() {
        QProcess proc;
        proc.start(QStringLiteral("gnome-extensions"), {QStringLiteral("enable"), QStringLiteral("mpaste@mpaste.local")});
        const bool ran = proc.waitForFinished(5000) && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
        bool live = false;
#ifdef Q_OS_LINUX
        if (ran) {
            QThread::msleep(400);
            live = GnomeBridge::instance()->call(QStringLiteral("CurrentWindow")).type() != QDBusMessage::ErrorMessage;
        }
#endif
        if (live) {
            state->setText(uiText("<b>On.</b> The clipboard and the shortcut work now.", QStringLiteral("<b>✓ 已启用。</b>剪贴板记录和快捷键现在就能用了。")));
            enable->hide();
        } else if (ran) {
            state->setText(uiText("<b>Turned on.</b> GNOME loads a newly installed extension at the next login: log out and back in once.",
                                  QStringLiteral("<b>已启用。</b>新装的扩展要在下次登录时才加载：注销并重新登录一次即可。")));
            enable->hide();
        } else {
            state->setText(uiText("Could not turn it on here. In a terminal: <b>gnome-extensions enable mpaste@mpaste.local</b>, "
                                  "or with the Extensions app; then log out and back in.",
                                  QStringLiteral("没能在这里启用。请在终端运行 <b>gnome-extensions enable mpaste@mpaste.local</b>，"
                                                 "或在“扩展”应用里打开它，然后注销并重新登录。")));
        }
    });
    body->addStretch(1);
    return page;
}

QWidget *OnboardingDialog::hotkeyPage() {
    const QStringList keys = hotkeyKeys();
    const QString combo = keys.join(QStringLiteral(" + "));
    QVBoxLayout *body = nullptr;
    QWidget *page = pageShell(pages_, uiText("The one key to remember", QStringLiteral("第 1 个快捷键")),
                              uiText("Press %1 to open it, anywhere", QStringLiteral("按 %1，随时打开")).arg(combo),
                              uiText("It works in any app. Remember this one and you have what you need.",
                                     QStringLiteral("在任何应用里都能用。这是最重要的一个键，记住它就够用了。")),
                              &body);
    auto *tryRow = new QFrame(page);
    tryRow->setObjectName(QStringLiteral("guideBox"));
    auto *tryLayout = new QHBoxLayout(tryRow);
    tryLayout->setContentsMargins(20, 16, 20, 16);
    tryLayout->setSpacing(10);
    for (int i = 0; i < keys.size(); ++i) {
        if (i > 0) {
            auto *plus = new QLabel(QStringLiteral("+"), tryRow);
            plus->setObjectName(QStringLiteral("guideFaint"));
            tryLayout->addWidget(plus);
        }
        auto *cap = new GuideKeycap(keys[i], true, tryRow);
        hotkeyCaps_ << cap;
        tryLayout->addWidget(cap);
    }
    tryLayout->addSpacing(18);
    hotkeyStatus_ = new QLabel(uiText("<b>Try it now:</b> hold %1, then press %2.<br>The keys light up as you press them.",
                                      QStringLiteral("<b>现在试一下：</b>按住 %1，再按 %2。<br>按下时这几个键会亮起来。"))
                                   .arg(keys.mid(0, keys.size() - 1).join(QStringLiteral(" + ")).toHtmlEscaped(), keys.last().toHtmlEscaped()),
                               tryRow);
    hotkeyStatus_->setObjectName(QStringLiteral("guideLead"));
    hotkeyStatus_->setWordWrap(true);
    tryLayout->addWidget(hotkeyStatus_, 1);
    body->addWidget(tryRow);

    auto *opens = new QLabel(uiText("It opens as:", QStringLiteral("打开后显示为：")), page);
    opens->setObjectName(QStringLiteral("guideFaint"));
    body->addWidget(opens);
    auto *choice = new QHBoxLayout;
    choice->setSpacing(10);
    const bool stack = MPasteSettings::getInst()->getActivationLayout() == MPasteSettings::LayoutStack;
    auto addOption = [&](const QString &title, const QString &note, MPasteSettings::ActivationLayout layout) {
        auto *option = new QPushButton(page);
        option->setObjectName(QStringLiteral("guideOption"));
        option->setCheckable(true);
        option->setChecked((layout == MPasteSettings::LayoutStack) == stack);
        option->setCursor(Qt::PointingHandCursor);
        option->setFocusPolicy(Qt::NoFocus);
        auto *l = new QVBoxLayout(option);
        l->setContentsMargins(14, 12, 14, 12);
        auto *t = new QLabel(QStringLiteral("<b>%1</b><br><span style=\"font-size:12px\">%2</span>").arg(title.toHtmlEscaped(), note.toHtmlEscaped()), option);
        t->setObjectName(QStringLiteral("guideOptionText"));
        t->setWordWrap(true);   // the note may be longer than the half width, in some languages
        t->setAttribute(Qt::WA_TransparentForMouseEvents);
        l->addWidget(t);
        option->setMinimumHeight(64);
        connect(option, &QPushButton::clicked, this, [this, option, layout]() {
            for (QPushButton *o : layoutOptions_) o->setChecked(o == option);
            MPasteSettings::getInst()->setActivationLayout(layout);
            MPasteSettings::getInst()->saveSettings();
        });
        layoutOptions_ << option;
        choice->addWidget(option);
    };
    addOption(uiText("Bottom panel", QStringLiteral("底部面板")),
              uiText("Cards along the foot of the screen; shows the most", QStringLiteral("屏幕底部一排卡片，看得最全")),
              MPasteSettings::LayoutPanel);
    addOption(uiText("Stack by the caret", QStringLiteral("光标旁堆栈")),
              uiText("The nine most recent, right where you type", QStringLiteral("在输入的位置弹出最近 9 条")),
              MPasteSettings::LayoutStack);
    body->addLayout(choice);

    auto *change = new QPushButton(uiText("Clashes with another app? Change the shortcut", QStringLiteral("和别的软件冲突？换一个快捷键")), page);
    change->setObjectName(QStringLiteral("guideLink"));
    change->setCursor(Qt::PointingHandCursor);
    change->setFocusPolicy(Qt::NoFocus);
    connect(change, &QPushButton::clicked, this, &OnboardingDialog::changeShortcutRequested);
    body->addWidget(change, 0, Qt::AlignLeft);
    body->addStretch(1);
    return page;
}

QWidget *OnboardingDialog::practicePage() {
    QVBoxLayout *body = nullptr;
    QWidget *page = pageShell(pages_, uiText("Pick one, paste it", QStringLiteral("挑一条，粘贴")),
                              uiText("Arrows to pick, Enter to paste; numbers are quicker", QStringLiteral("方向键选，回车贴；数字键更快")),
                              uiText("In the panel: %1 %2 pick, %3 pastes into the window you were in. Hold %4 to see numbers; %4 + %5 pastes the second at once. Practise below:",
                                     QStringLiteral("面板里：%1 %2 选择，%3 粘贴到刚才的窗口。按住 %4 会显示编号，%4 + %5 直接粘贴第 2 条。下面是练习区，随便按："))
                                  .arg(kbd(QStringLiteral("←")), kbd(QStringLiteral("→")), kbd(QStringLiteral("Enter")), kbd(QStringLiteral("Alt")), kbd(QStringLiteral("2"))),
                              &body);
    auto *box = new QFrame(page);
    box->setObjectName(QStringLiteral("guideBox"));
    auto *boxLayout = new QVBoxLayout(box);
    boxLayout->setContentsMargins(14, 10, 14, 12);
    boxLayout->setSpacing(10);
    practice_ = new GuidePractice(true, [this](const char *s, const QString &z) { return uiText(s, z); }, box);
    boxLayout->addWidget(practice_);
    auto *goals = new QHBoxLayout;
    goals->setSpacing(8);
    QList<QLabel *> goalLabels;
    const QStringList goalTexts = {uiText("Pick another with an arrow", QStringLiteral("用方向键换一张")),
                                   uiText("Press Enter to paste", QStringLiteral("按 Enter 粘贴")),
                                   uiText("Hold Alt, press 2", QStringLiteral("按住 Alt 再按 2"))};
    for (const QString &text : goalTexts) {
        auto *goal = new QLabel(QStringLiteral("○  ") + text, box);
        goal->setObjectName(QStringLiteral("guideGoal"));
        goals->addWidget(goal, 1);
        goalLabels << goal;
    }
    boxLayout->addLayout(goals);
    auto *toast = new QLabel(box);
    toast->setObjectName(QStringLiteral("guideOk"));
    toast->setMinimumHeight(18);
    boxLayout->addWidget(toast);
    body->addWidget(box);
    practice_->onGoal = [goalLabels, goalTexts, toast](int goal, const QString &message) {
        if (goal >= 0 && goal < goalLabels.size()) {
            goalLabels[goal]->setText(QStringLiteral("✓  ") + goalTexts[goal]);
            goalLabels[goal]->setProperty("done", true);
            goalLabels[goal]->style()->unpolish(goalLabels[goal]);
            goalLabels[goal]->style()->polish(goalLabels[goal]);
        }
        if (!message.isEmpty()) toast->setText(QStringLiteral("✓ ") + message);
    };
    auto *plain = new QLabel(uiText("%1 + %2 pastes as plain text, without formatting.", QStringLiteral("%1 + %2 粘贴为纯文本，去掉格式。"))
                                 .arg(kbd(QStringLiteral("Ctrl")), kbd(QStringLiteral("Enter"))), page);
    plain->setObjectName(QStringLiteral("guideFaint"));
    plain->setTextFormat(Qt::RichText);
    body->addWidget(plain);
    body->addStretch(1);
    return page;
}

QWidget *OnboardingDialog::findPage() {
    QVBoxLayout *body = nullptr;
    QWidget *page = pageShell(pages_, uiText("Find it", QStringLiteral("找到它")),
                              uiText("Just type to search; Space to look closer", QStringLiteral("直接打字搜索，空格看清楚")),
                              QString(), &body);
    auto *grid = new QGridLayout;
    grid->setSpacing(10);
    const QList<QPair<QString, QString>> tips = {
        {kbd(QStringLiteral("A")) + kbd(QStringLiteral("B")) + kbd(QStringLiteral("C")),
         uiText("<b>Type as soon as it opens</b><br>No search box to click: typing filters at once; text read from pictures is found too.",
                QStringLiteral("<b>打开后直接打字</b><br>不用先点搜索框，输入就开始筛选；图片里识别出的字也能搜到。"))},
        {kbd(QStringLiteral("Space")),
         uiText("<b>Space: a large preview</b><br>Read all of it, zoom into a picture; Space again closes it.",
                QStringLiteral("<b>空格：大图预览</b><br>看全文、放大图片；再按一次空格关闭。"))},
        {kbd(QStringLiteral("Tab")),
         uiText("<b>To your favourites</b><br>Keep what you use often in a board of its own, where it is not pushed out.",
                QStringLiteral("<b>切到收藏</b><br>常用的内容收藏起来，单独一栏，不会被挤掉。"))},
        {uiText("Right click", QStringLiteral("右键")),
         uiText("<b>More on a card</b><br>Pin, rename, read the text in a picture, open in the browser, details…",
                QStringLiteral("<b>更多操作</b><br>置顶、改名、识别图片里的文字、在浏览器中打开、详情……"))},
    };
    for (int i = 0; i < tips.size(); ++i) {
        grid->addWidget(panel(tips[i].first + QStringLiteral("<br><br>") + tips[i].second, page), i / 2, i % 2);
    }
    body->addLayout(grid);
    body->addStretch(1);
    return page;
}

QWidget *OnboardingDialog::readyPage() {
    QVBoxLayout *body = nullptr;
    QWidget *page = pageShell(pages_, uiText("Ready", QStringLiteral("准备好了")),
                              uiText("Three last things", QStringLiteral("最后确认三件事")), QString(), &body);
    const Tone t = toneFor(dark_);
    auto addRow = [&](bool ok, const QString &title, const QString &note, QPushButton *button) -> QLabel * {
        auto *row = new QFrame(page);
        row->setObjectName(QStringLiteral("guideBox"));
        auto *l = new QHBoxLayout(row);
        l->setContentsMargins(14, 12, 14, 12);
        l->setSpacing(12);
        auto *dot = new QLabel(QStringLiteral("●"), row);
        dot->setStyleSheet(QStringLiteral("color: %1; background: transparent; font-size: 11px;").arg(rgba(ok ? t.ok : t.warn)));
        l->addWidget(dot, 0, Qt::AlignTop);
        auto *text = new QLabel(QStringLiteral("<b>%1</b><br><span style=\"font-size:12px\">%2</span>").arg(title.toHtmlEscaped(), note), row);
        text->setObjectName(QStringLiteral("guideLead"));
        text->setWordWrap(true);
        l->addWidget(text, 1);
        if (button) {
            button->setParent(row);
            button->setObjectName(QStringLiteral("auroraSecondary"));
            button->setCursor(Qt::PointingHandCursor);
            button->setFocusPolicy(Qt::NoFocus);
            l->addWidget(button, 0, Qt::AlignVCenter);
        }
        body->addWidget(row);
        text->setProperty("dot", QVariant::fromValue(static_cast<void *>(dot)));
        return text;
    };

    // Start with the system, so the shortcut always works.
#ifdef Q_OS_WIN
    auto *autostart = new QPushButton(uiText("In Settings", QStringLiteral("去设置")));
    connect(autostart, &QPushButton::clicked, this, &OnboardingDialog::autostartRequested);
    addRow(true, uiText("Start with Windows", QStringLiteral("开机自动启动")),
           uiText("So the shortcut always works: Settings → General → Launch at startup.",
                  QStringLiteral("这样快捷键一直可用：设置 → 通用 → 开机自启动。")).toHtmlEscaped(), autostart);
#else
    bool autostartOn = QFileInfo::exists(QStringLiteral("/etc/xdg/autostart/MPaste.desktop"));
    const QDir userAutostart(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/autostart"));
    for (const QString &entry : userAutostart.entryList({QStringLiteral("*.desktop")}, QDir::Files)) {
        autostartOn = autostartOn || entry.contains(QStringLiteral("mpaste"), Qt::CaseInsensitive);
    }
    addRow(autostartOn, uiText("Start with the system", QStringLiteral("开机自动启动")),
           (autostartOn ? uiText("On: the shortcut works from login.", QStringLiteral("已设置：登录后快捷键就能用。"))
                        : uiText("Not set: add MPaste in your desktop's startup applications so the shortcut always works.",
                                 QStringLiteral("未设置：在桌面的“启动应用程序”里添加 MPaste，快捷键才会一直可用。"))).toHtmlEscaped(),
           nullptr);
#endif

    auto *privacy = new QPushButton(uiText("Leave apps out…", QStringLiteral("排除应用…")));
    connect(privacy, &QPushButton::clicked, this, &OnboardingDialog::privacyRequested);
    addRow(true, uiText("Privacy is on", QStringLiteral("隐私保护已开启")),
           uiText("What a password manager marks as secret is never recorded; you can also leave out whole apps.",
                  QStringLiteral("密码管理器标记为敏感的内容不会被记录；也可以排除指定的应用。")).toHtmlEscaped(), privacy);

    // Reading text in pictures.
#ifdef Q_OS_WIN
    addRow(true, uiText("Text in pictures", QStringLiteral("识别图片里的文字")),
           uiText("Built into Windows: right click a picture → Read text.",
                  QStringLiteral("Windows 自带：在图片上右键 → 识别文字。")).toHtmlEscaped(), nullptr);
#else
    ocrCopy_ = new QPushButton(uiText("Copy the command", QStringLiteral("复制安装命令")));
    ocrCopy_->hide();
    connect(ocrCopy_, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(OcrService::runtimeInstallCommand());
        ocrCopy_->setText(uiText("Copied", QStringLiteral("已复制")));
    });
    ocrState_ = addRow(true, uiText("Text in pictures (optional)", QStringLiteral("识别图片里的文字（可选）")),
                       uiText("Checking…", QStringLiteral("正在检查…")), ocrCopy_);
    QPointer<OnboardingDialog> guard(this);
    QThread *probe = QThread::create([guard]() {
        const OcrService::LocalState state = OcrService::localState();
        QMetaObject::invokeMethod(qApp, [guard, state]() {
            if (!guard || !guard->ocrState_) return;
            const Tone tone = toneFor(guard->dark_);
            auto *dot = static_cast<QLabel *>(guard->ocrState_->property("dot").value<void *>());
            QString note;
            bool ok = true;
            if (state == OcrService::LocalReady) {
                note = guard->uiText("Ready: right click a picture → Read text.", QStringLiteral("已就绪：在图片上右键 → 识别文字。"));
            } else if (state == OcrService::LocalNoRuntime) {
                ok = false;
                note = guard->uiText("Needs ONNX Runtime (about 80 MB). Install it later too, in Settings: %1",
                                     QStringLiteral("需要安装 ONNX Runtime（约 80 MB），以后在设置里也能装：%1"))
                           .arg(OcrService::runtimeInstallCommand());
                guard->ocrCopy_->show();
            } else {
                note = guard->uiText("Uses tesseract if it is installed.", QStringLiteral("会使用已安装的 tesseract。"));
            }
            if (dot) dot->setStyleSheet(QStringLiteral("color: %1; background: transparent; font-size: 11px;").arg(rgba(ok ? tone.ok : tone.warn)));
            guard->ocrState_->setText(QStringLiteral("<b>%1</b><br><span style=\"font-size:12px\">%2</span>")
                                          .arg(guard->uiText("Text in pictures (optional)", QStringLiteral("识别图片里的文字（可选）")).toHtmlEscaped(),
                                               note.toHtmlEscaped()));
        });
    });
    connect(probe, &QThread::finished, probe, &QObject::deleteLater);
    probe->start();
#endif

    auto *last = new QLabel(uiText("Forgot a key? In the panel, hold %1 to see the numbers; Settings → About shows this guide again.",
                                   QStringLiteral("记不住也没关系：面板里按住 %1 随时显示编号；设置 → 关于 里可以重看这个引导。"))
                                .arg(kbd(QStringLiteral("Alt"))), page);
    last->setObjectName(QStringLiteral("guideFaint"));
    last->setTextFormat(Qt::RichText);
    last->setWordWrap(true);
    body->addWidget(last);
    body->addStretch(1);
    return page;
}

void OnboardingDialog::goTo(int index) {
    index = qBound(0, index, pages_->count() - 1);
    pages_->setCurrentIndex(index);
    dots_->set(pages_->count(), index);
    const bool last = index == pages_->count() - 1;
    back_->setVisible(index > 0);
    next_->setText(index == 0 ? uiText("Start", QStringLiteral("开始"))
                   : last    ? uiText("Start using MPaste", QStringLiteral("开始使用"))
                             : uiText("Next", QStringLiteral("下一步")));
    keyHint_->setText(index == practicePageIndex_ ? QString()
                      : last ? uiText("Enter to finish", QStringLiteral("Enter 完成"))
                             : uiText("→ next", QStringLiteral("→ 下一步")));
    if (index == practicePageIndex_ && practice_) {
        practice_->setFocus();
    } else {
        setFocus();
    }
}

void OnboardingDialog::hotkeyPressed() {
    for (GuideKeycap *cap : hotkeyCaps_) cap->setDown(true);
    if (pages_->currentIndex() == hotkeyPageIndex_ && hotkeyStatus_) {
        hotkeyStatus_->setText(uiText("<b>✓ That's it!</b><br>From now on, %1 opens MPaste in any app.",
                                      QStringLiteral("<b>✓ 就是这样！</b><br>以后在任何应用里按 %1 都能打开 MPaste。"))
                                   .arg(hotkeyKeys().join(QStringLiteral(" + ")).toHtmlEscaped()));
    }
    QPointer<OnboardingDialog> guard(this);
    QTimer::singleShot(600, this, [guard]() {
        if (!guard) return;
        for (GuideKeycap *cap : guard->hotkeyCaps_) cap->setDown(false);
    });
}

void OnboardingDialog::keyPressEvent(QKeyEvent *event) {
    const int index = pages_->currentIndex();
    // The hotkey's own keys light up as they go down (the shortcut itself
    // may never reach a window: the system takes it).
    if (index == hotkeyPageIndex_) {
        for (GuideKeycap *cap : hotkeyCaps_) {
            const QString name = cap->text();
            const bool modifier = (name == QLatin1String("Alt") && event->key() == Qt::Key_Alt)
                || (name.startsWith(QLatin1String("Ctrl")) && event->key() == Qt::Key_Control)
                || (name == QLatin1String("Shift") && event->key() == Qt::Key_Shift)
                || ((name == QLatin1String("Meta") || name == QLatin1String("Win")) && event->key() == Qt::Key_Meta);
            const bool plain = QKeySequence(event->key()).toString(QKeySequence::NativeText) == name;
            if (modifier || plain) cap->setDown(true);
        }
    }
    if (index != practicePageIndex_) {
        if (event->key() == Qt::Key_Right || ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && event->modifiers() == Qt::NoModifier)) {
            if (index == pages_->count() - 1) accept();
            else goTo(index + 1);
            return;
        }
        if (event->key() == Qt::Key_Left) {
            goTo(index - 1);
            return;
        }
    }
    QDialog::keyPressEvent(event);   // Esc closes, as skipping
}

void OnboardingDialog::keyReleaseEvent(QKeyEvent *event) {
    if (pages_->currentIndex() == hotkeyPageIndex_) {
        for (GuideKeycap *cap : hotkeyCaps_) cap->setDown(false);
    }
    QDialog::keyReleaseEvent(event);
}

void OnboardingDialog::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        dragOffset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
    QDialog::mousePressEvent(event);
}

void OnboardingDialog::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) {
        move(event->globalPosition().toPoint() - dragOffset_);
    }
    QDialog::mouseMoveEvent(event);
}

void OnboardingDialog::done(int result) {
    // Finished or skipped, it is seen.
    MPasteSettings::getInst()->setOnboardingVersion(1);
    MPasteSettings::getInst()->saveSettings();
    QDialog::done(result);
}

void OnboardingDialog::present() {
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (screen) {
        move(screen->availableGeometry().center() - QPoint(width() / 2, height() / 2));
    }
    goTo(0);
    show();
    AuroraChrome::attachBackdrop(this, dark_, kShadow);
    raise();
    activateWindow();
}

void OnboardingDialog::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    AuroraChrome::paintWindow(painter, QRectF(rect()).adjusted(kShadow, kShadow, -kShadow, -kShadow), dark_, toneFor(dark_).accent);
}

void OnboardingDialog::applyTheme(bool dark) {
    dark_ = dark;
    const Tone t = toneFor(dark);
    AuroraChrome::attachBackdrop(this, dark, kShadow);
    setStyleSheet(AuroraChrome::styleSheet(dark) + QStringLiteral(
        "QLabel#guideName { color: %2; font-size: 13px; font-weight: 700; background: transparent; }"
        "QPushButton#guideSkip { border: none; background: transparent; color: %3; font-size: 12.5px; padding: 6px 8px; border-radius: 8px; }"
        "QPushButton#guideSkip:hover { color: %1; background: %4; }"
        "QLabel#guideEyebrow { color: %5; font-size: 12px; font-weight: 700; background: transparent; }"
        "QLabel#guideTitle { color: %1; font-size: 24px; font-weight: 900; background: transparent; }"
        "QLabel#guideLead { color: %2; font-size: 14px; background: transparent; }"
        "QLabel#guideFaint { color: %3; font-size: 12.5px; background: transparent; }"
        "QLabel#guidePanel { color: %2; font-size: 12.5px; background: %6; border: 1px solid %4; border-radius: 12px; padding: 12px 14px; }"
        "QFrame#guideBox { background: %6; border: 1px solid %4; border-radius: 14px; }"
        "QLabel#guideGoal { color: %2; font-size: 12.5px; background: %6; border-radius: 10px; padding: 8px 10px; }"
        "QLabel#guideGoal[done=\"true\"] { color: %7; font-weight: 700; }"
        "QLabel#guideOk { color: %7; font-size: 12.5px; background: transparent; }"
        "QPushButton#guideOption { text-align: left; background: %6; border: 1px solid %4; border-radius: 12px; }"
        "QPushButton#guideOption:checked { background: %8; border: 2px solid %5; }"
        "QLabel#guideOptionText { color: %1; font-size: 13px; background: transparent; }"
        "QPushButton#guideLink { border: none; background: transparent; color: %5; font-size: 12.5px; padding: 0; text-align: left; }"
        "QPushButton#guideLink:hover { text-decoration: underline; }")
        .arg(rgba(t.ink), rgba(t.muted), rgba(t.faint), rgba(t.hair), rgba(t.accent), rgba(t.pane), rgba(t.ok), rgba(t.accentSoft)));
    update();
}
