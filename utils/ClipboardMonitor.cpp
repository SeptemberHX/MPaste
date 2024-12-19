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
    QString text, html;
    QPixmap image;
    QList<QUrl> urls;
    QColor color(-1, -1, -1);

    const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData();
    if (!mimeData) {
        return;
    }

    if (mimeData->hasImage()) {
        image = qvariant_cast<QPixmap>(mimeData->imageData());
    }

    if (mimeData->hasHtml()) {
        html = mimeData->html();
    }

    if (mimeData->hasText()) {
        text = mimeData->text();
    }

    if (mimeData->hasUrls()) {
        urls = mimeData->urls();
    }

    if (mimeData->hasColor()) {
        color = qvariant_cast<QColor>(mimeData->colorData());
    }

    Q_EMIT clipboardUpdated(ClipboardItem(PlatformRelated::getWindowIcon(wId), text, image, html, urls, color), wId);
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
