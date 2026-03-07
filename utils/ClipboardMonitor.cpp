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
