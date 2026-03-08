// input: 依赖对应头文件、Qt 服务与平台系统能力。
// output: 对外提供 ClipboardMonitor 的工具实现。
// pos: utils 层中的 ClipboardMonitor 实现文件。
// update: 一旦我被更新，务必更新我的开头注释，以及所属的文件夹的 README.md。
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

#ifdef Q_OS_WIN
#include <windows.h>
#endif

static quint32 getClipboardSeqNumber() {
#ifdef Q_OS_WIN
    return GetClipboardSequenceNumber();
#else
    // Linux/macOS 没有等效 API，直接返回 0
    return 0;
#endif
}

void ClipboardMonitor::clipboardChanged() {
    pendingWId_ = PlatformRelated::currActiveWindow();
    lastSeqNumber_ = getClipboardSeqNumber();
    retryCount_ = 0;
    stabilizeTimer_.start();
}

void ClipboardMonitor::checkAndCapture() {
#ifdef Q_OS_WIN
    quint32 currentSeq = getClipboardSeqNumber();
    if (currentSeq != lastSeqNumber_) {
        lastSeqNumber_ = currentSeq;
        retryCount_++;
        if (retryCount_ < MAX_RETRIES) {
            stabilizeTimer_.start();
            return;
        }
    }
#endif
    captureClipboard();
}

void ClipboardMonitor::captureClipboard() {
    const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData();

    if (!mimeData) {
        return;
    }

    // Skip clipboard data with no meaningful content (e.g. delayed render failed)
    bool hasContent = mimeData->hasText() && !mimeData->text().isEmpty();
    hasContent = hasContent || mimeData->hasHtml();
    hasContent = hasContent || mimeData->hasImage();
    hasContent = hasContent || mimeData->hasUrls();
    hasContent = hasContent || mimeData->hasColor();
    if (!hasContent) {
        return;
    }

    Q_EMIT clipboardUpdated(ClipboardItem(PlatformRelated::getWindowIcon(pendingWId_), mimeData), pendingWId_);
}

ClipboardMonitor::ClipboardMonitor() {
    stabilizeTimer_.setSingleShot(true);
    stabilizeTimer_.setInterval(STABILIZE_INTERVAL);
    connect(&stabilizeTimer_, &QTimer::timeout, this, &ClipboardMonitor::checkAndCapture);

    this->connectMonitor();
}

void ClipboardMonitor::disconnectMonitor() {
    stabilizeTimer_.stop();
    disconnect(QGuiApplication::clipboard(), &QClipboard::dataChanged,
              this, &ClipboardMonitor::clipboardChanged);
}

void ClipboardMonitor::connectMonitor() {
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged,
        this, &ClipboardMonitor::clipboardChanged);
}
