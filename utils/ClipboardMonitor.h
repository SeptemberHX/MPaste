// input: 依赖 Qt 平台抽象、系统 API 与调用方声明。
// output: 对外提供 ClipboardMonitor 的工具接口。
// pos: utils 层中的 ClipboardMonitor 接口定义。
// update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
//
// Created by ragdoll on 2021/5/22.
//

#ifndef MPASTE_CLIPBOARDMONITOR_H
#define MPASTE_CLIPBOARDMONITOR_H

#include <QObject>
#include <QTimer>
#include "data/ClipboardItem.h"

class ClipboardMonitor : public QObject {
    Q_OBJECT

public:
    ClipboardMonitor();

    void disconnectMonitor();
    void connectMonitor();

signals:
    void clipboardUpdated(ClipboardItem item, int wId);

public slots:
    void clipboardChanged();

private:
    void checkAndCapture();
    void captureClipboard();

    QTimer stabilizeTimer_;
    int pendingWId_ = 0;
    quint32 lastSeqNumber_ = 0;
    int retryCount_ = 0;

    static const int STABILIZE_INTERVAL = 200;  // 每次检查间隔 ms
    static const int MAX_RETRIES = 10;           // 最多等待 2 秒
};


#endif //MPASTE_CLIPBOARDMONITOR_H
