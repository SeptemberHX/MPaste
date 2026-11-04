// input: Depends on Qt Widgets, AuroraChrome, MPasteSettings and OcrService.
// output: Exposes the first-run guide: a few steps that teach MPaste by having the user press its keys.
// pos: Widget-layer dialog MPasteWidget shows on first run and from Settings → About.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_ONBOARDINGDIALOG_H
#define MPASTE_ONBOARDINGDIALOG_H

#include <QDialog>
#include <QList>
#include <QPoint>

class QLabel;
class QPushButton;
class QStackedWidget;
class GuideDots;
class GuideKeycap;
class GuidePractice;

// Welcome; the GNOME extension (only on GNOME Wayland without it); the
// hotkey, pressed for real, and what it opens; picking and pasting, in a
// practice row of made-up cards; finding (search, preview, favorites, the
// menu); a last check (start with the system, privacy, text recognition).
// It can be skipped at any step; closing it marks it seen.
class OnboardingDialog : public QDialog {
    Q_OBJECT

public:
    explicit OnboardingDialog(bool needsExtension, QWidget *parent = nullptr);

    // Shown centred on the screen under the pointer, at its first step.
    void present();
    // The global hotkey went off while the guide was open.
    void hotkeyPressed();

signals:
    void changeShortcutRequested();
    void privacyRequested();
    void autostartRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void done(int result) override;

private:
    QWidget *welcomePage();
    QWidget *extensionPage();
    QWidget *hotkeyPage();
    QWidget *practicePage();
    QWidget *findPage();
    QWidget *readyPage();
    void goTo(int index);
    void applyTheme(bool dark);
    QString uiText(const char *source, const QString &zh) const;

    bool needsExtension_ = false;
    bool dark_ = true;
    QStackedWidget *pages_ = nullptr;
    GuideDots *dots_ = nullptr;
    QPushButton *back_ = nullptr;
    QPushButton *next_ = nullptr;
    QLabel *keyHint_ = nullptr;
    int hotkeyPageIndex_ = -1;
    int practicePageIndex_ = -1;
    QList<GuideKeycap *> hotkeyCaps_;
    QLabel *hotkeyStatus_ = nullptr;
    QList<QPushButton *> layoutOptions_;
    GuidePractice *practice_ = nullptr;
    QLabel *ocrState_ = nullptr;
    QPushButton *ocrCopy_ = nullptr;
    QPoint dragOffset_;
};

#endif // MPASTE_ONBOARDINGDIALOG_H
