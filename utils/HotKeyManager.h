// input: 依赖 Qt 平台抽象、系统 API 与调用方声明。
// output: 对外提供 HotKeyManager 的工具接口。
// pos: utils 层中的 HotKeyManager 接口定义。
// update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
// HotKeyManager.h
#ifndef MPASTE_HOTKEYMANAGER_H
#define MPASTE_HOTKEYMANAGER_H

#include <QObject>
#include <QKeySequence>

class HotkeyManager : public QObject {
    Q_OBJECT

public:
    explicit HotkeyManager(QObject *parent = nullptr);
    ~HotkeyManager() override;

    bool registerHotkey(const QKeySequence &keySequence);
    void unregisterHotkey();

    signals:
        void hotkeyPressed();

private:
    class Private;
    friend class Private;
    Private *d;
};

#endif //MPASTE_HOTKEYMANAGER_H