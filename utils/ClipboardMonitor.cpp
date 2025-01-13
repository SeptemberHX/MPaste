//
// Created by ragdoll on 2021/5/22.
//

#include "ClipboardMonitor.h"

#include "PlatformRelated.h"
#include <iostream>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QUrl>

void ClipboardMonitor::clipboardChanged() {
    int wId = PlatformRelated::currActiveWindow();
    const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData();

    if (!mimeData) {
        return;
    }

    // 直接使用新的构造方式创建 ClipboardItem
    Q_EMIT clipboardUpdated(ClipboardItem(PlatformRelated::getWindowIcon(wId), mimeData), wId);
}

ClipboardMonitor::ClipboardMonitor() {
    this->connectMonitor();
}

void ClipboardMonitor::disconnectMonitor() {
    // 设置数据前先断开剪贴板的 dataChanged 信号
    disconnect(QGuiApplication::clipboard(), &QClipboard::dataChanged,
              this, &ClipboardMonitor::clipboardChanged);
}

void ClipboardMonitor::connectMonitor() {
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged,
        this, &ClipboardMonitor::clipboardChanged);
}
