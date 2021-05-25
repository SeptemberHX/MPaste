//
// Created by ragdoll on 2021/5/22.
//

#include "ClipboardMonitor.h"

#include <KWindowSystem>
#include <iostream>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QUrl>

void ClipboardMonitor::clipboardChanged() {
    int wId = KWindowSystem::activeWindow();
    const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData();

    QString text, html;
    QPixmap image;
    QList<QUrl> urls;
    QColor color(-1, -1, -1);
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
        std::cout << color.name().toStdString() << std::endl;
    }

    Q_EMIT clipboardUpdated(ClipboardItem(KWindowSystem::self()->icon(wId), text, image, html, urls, color), wId);
}

ClipboardMonitor::ClipboardMonitor() {
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &ClipboardMonitor::clipboardChanged);
}
