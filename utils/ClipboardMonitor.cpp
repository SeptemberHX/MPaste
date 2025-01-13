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

    // Debug: 输出所有可用的格式
    qDebug() << "Available formats:" << mimeData->formats();
    // 1. 首先检查富文本格式（包括Office原始格式）
    if (mimeData->hasHtml()) {
        html = mimeData->html();
        // PowerPoint可能在text中包含额外的格式信息
        if (mimeData->hasText()) {
            text = mimeData->text();
        }
    }
    // 2. 检查URL（文件链接等）
    else if (mimeData->hasUrls()) {
        urls = mimeData->urls();
        if (mimeData->hasText()) {
            text = mimeData->text();
        }
    }
    // 3. 检查纯文本
    else if (mimeData->hasText()) {
        text = mimeData->text();
    }
    // 4. 最后才检查图片格式
    else if (mimeData->hasImage()) {
        image = qvariant_cast<QPixmap>(mimeData->imageData());
    }

    // 颜色数据的处理保持不变
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
