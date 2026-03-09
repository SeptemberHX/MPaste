// input: 依赖相关 Qt/标准库类型与同层或跨层前置声明。
// output: 对外提供 HotKeyManager 的声明接口。
// pos: utils 层中的 HotKeyManager 接口定义。
// update: 修改本文件时，同步更新文件头注释与所属目录 README.md。
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
