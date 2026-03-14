// input: Depends on Qt application state, settings, and QSS resources.
// output: Provides a centralized theme manager for palette, style sheet, and icon resolution.
// pos: utils layer theme service.
// update: If I change, update this header block and utils/README.md.
#ifndef MPASTE_THEME_MANAGER_H
#define MPASTE_THEME_MANAGER_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QPalette>

class ThemeManager : public QObject {
    Q_OBJECT

public:
    static ThemeManager *instance();

    void initialize();
    void applyTheme();
    bool isDark() const;

    QPalette palette(bool dark) const;
    QString mergedStyleSheet(bool dark) const;

signals:
    void themeChanged(bool dark);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    ThemeManager();

    QString loadStyleFile(const QString &path) const;
    QString applyTokens(const QString &style, const QHash<QString, QString> &tokens) const;
    QHash<QString, QString> themeTokens(bool dark) const;

    bool dark_ = false;
    bool initialized_ = false;
};

#endif // MPASTE_THEME_MANAGER_H
