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
#include "XUtils.h"

void ClipboardMonitor::clipboardChanged() {
    int wId = KWindowSystem::activeWindow();
    const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData();

    QString text, html;
    QPixmap image;
    QList<QUrl> urls;
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

    Q_EMIT clipboardUpdated(ClipboardItem(XUtils::getWindowIconName(wId), text, image, html, urls), wId);
}

ClipboardMonitor::ClipboardMonitor() {
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &ClipboardMonitor::clipboardChanged);
}
