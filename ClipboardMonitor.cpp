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
    std::cout << wId << std::endl;

    const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData();

    QString text, html;
    QPixmap image;
    QList<QUrl> urls;
    if (mimeData->hasImage()) {
        std::cout << "Has image" << std::endl;
        image = qvariant_cast<QPixmap>(mimeData->imageData());
    }

    if (mimeData->hasHtml()) {
        std::cout << "Has html" << std::endl;
        html = mimeData->html();
    }

    if (mimeData->hasText()) {
        std::cout << "Has text" << std::endl;
        text = mimeData->text();
    }

    if (mimeData->hasUrls()) {
        std::cout << "Has urls" << std::endl;
        urls = mimeData->urls();
    }

    Q_EMIT clipboardUpdated(ClipboardItem(text, image, html, urls), wId);
}

ClipboardMonitor::ClipboardMonitor() {
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &ClipboardMonitor::clipboardChanged);
}
