// input: 依赖相关 Qt/标准库类型与同层或跨层前置声明。
// output: 对外提供 MPasteSettings 的声明接口。
// pos: utils 层中的 MPasteSettings 接口定义。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
//
// Created by ragdoll on 2021/5/24.
//

#ifndef MPASTE_MPASTESETTINGS_H
#define MPASTE_MPASTESETTINGS_H

#include <QDateTime>
#include <QString>
#include <QNetworkProxy>

class MPasteSettings : public QObject {
    Q_OBJECT

public:
    enum PasteShortcutMode { AutoPasteShortcut = 0, CtrlVShortcut, ShiftInsertShortcut, CtrlShiftVShortcut, AltInsertShortcut };
    enum HistoryRetentionUnit { RetentionDays = 0, RetentionWeeks, RetentionMonths };
    enum ThemeMode { ThemeSystem = 0, ThemeLight, ThemeDark };
    enum HistoryViewMode { ViewModePaged = 0, ViewModeContinuous };
    enum OcrBackend { OcrWindowsBuiltin = 0, OcrBaiduApi = 1 };

    static MPasteSettings* getInst();

    static const QString CLIPBOARD_CATEGORY_NAME;
    static const QString CLIPBOARD_CATEGORY_COLOR;
    static const QString STAR_CATEGORY_NAME;
    static const QString STAR_CATEGORY_COLOR;

    const QString &getSaveDir() const;
    void setSaveDir(const QString &dir);

    int getMaxSize() const;
    int getHistoryRetentionValue() const;
    HistoryRetentionUnit getHistoryRetentionUnit() const;
    QDateTime historyRetentionCutoff(const QDateTime &reference = QDateTime::currentDateTime()) const;

    QNetworkProxy::ProxyType getProxyType() const;

    const QString &getProxyHost() const;

    int getPort() const;

    bool isAutoPaste() const;

    void setAutoPaste(bool autoPaste);

    PasteShortcutMode getPasteShortcutMode() const;
    void setPasteShortcutMode(PasteShortcutMode mode);
    static QString pasteShortcutModeLabel(PasteShortcutMode mode);

    const QString &getShortcutStr() const;

    void setShortcutStr(const QString &shortcutStr);

    void setMaxSize(int maxSize);
    void setHistoryRetentionValue(int value);
    void setHistoryRetentionUnit(HistoryRetentionUnit unit);

    int getItemScale() const;
    void setItemScale(int itemScale);
    bool isPlaySound() const;
    void setPlaySound(bool playSound);

    ThemeMode getThemeMode() const;
    void setThemeMode(ThemeMode mode);

    HistoryViewMode getHistoryViewMode() const;
    void setHistoryViewMode(HistoryViewMode mode);
    bool isDarkTheme() const;

    OcrBackend getOcrBackend() const;
    void setOcrBackend(OcrBackend backend);
    const QString &getBaiduOcrApiKey() const;
    void setBaiduOcrApiKey(const QString &key);
    const QString &getBaiduOcrSecretKey() const;
    void setBaiduOcrSecretKey(const QString &key);
    bool isAutoOcr() const;
    void setAutoOcr(bool enabled);

    bool isTerminalTitle(const QString &title);

    int getCurrFocusWinId() const;

    void setCurrFocusWinId(int currFocusWinId);

    void saveSettings();

signals:
    void themeModeChanged(MPasteSettings::ThemeMode mode);

private:
    MPasteSettings();
    void loadSettings();

    QStringList terminalNames;

    QString saveDir;
    int maxSize;
    int historyRetentionValue;
    HistoryRetentionUnit historyRetentionUnit;

    QNetworkProxy::ProxyType proxyType;
    QString proxyHost;
    int port;

    bool autoPaste;
    PasteShortcutMode pasteShortcutMode;
    QString shortcutStr;
    int itemScale;
    bool playSound;
    ThemeMode themeMode;
    HistoryViewMode historyViewMode;
    OcrBackend ocrBackend;
    QString baiduOcrApiKey;
    QString baiduOcrSecretKey;
    bool autoOcr = false;

    int currFocusWinId;

    static MPasteSettings *inst;
};


#endif //MPASTE_MPASTESETTINGS_H
