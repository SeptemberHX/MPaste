// input: Depends on Qt dialog widgets and ThemeManager palette hooks.
// output: Provides a small themed dialog for renaming clipboard items.
// pos: Widget-layer rename dialog used by item context actions.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_CLIPBOARD_ITEM_RENAME_DIALOG_H
#define MPASTE_CLIPBOARD_ITEM_RENAME_DIALOG_H

#include <QColor>
#include <QDialog>
#include <QPixmap>

class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;

class ClipboardItemRenameDialog : public QDialog {
    Q_OBJECT

public:
    explicit ClipboardItemRenameDialog(const QString &currentAlias, QWidget *parent = nullptr);
    QString alias() const;
    // The item's source app: its icon heads the dialog and its colour lights it.
    void setSource(const QPixmap &icon, const QString &appName);

protected:
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void applyTheme(bool dark);

    QFrame *card_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QLabel *hintLabel_ = nullptr;
    QLineEdit *input_ = nullptr;
    QPushButton *okButton_ = nullptr;
    QPushButton *cancelButton_ = nullptr;
    QLabel *iconLabel_ = nullptr;
    QColor accent_;
    bool dark_ = true;
};

#endif // MPASTE_CLIPBOARD_ITEM_RENAME_DIALOG_H
