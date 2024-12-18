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

#ifdef Q_OS_WIN
    // 多次尝试打开剪贴板
    int retries = 3;
    while (retries > 0) {
        if (OpenClipboard(NULL)) {
            break;
        }
        QThread::msleep(10);
        retries--;
    }

    if (retries == 0) {
        return;
    }

    // Get text
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        if (HANDLE hData = GetClipboardData(CF_UNICODETEXT)) {
            if (LPWSTR pData = static_cast<LPWSTR>(GlobalLock(hData))) {
                text = QString::fromWCharArray(pData);
                GlobalUnlock(hData);
            }
        }
    }

    // Get HTML
    UINT htmlFormat = RegisterClipboardFormat(L"HTML Format");
    if (IsClipboardFormatAvailable(htmlFormat)) {
        if (HANDLE hData = GetClipboardData(htmlFormat)) {
            if (LPSTR pData = static_cast<LPSTR>(GlobalLock(hData))) {
                html = QString::fromUtf8(pData);

                // 查找头部信息中的 StartHTML 和 EndHTML 位置
                QRegularExpression startRE("StartHTML:(\\d+)");
                QRegularExpression endRE("EndHTML:(\\d+)");

                auto startMatch = startRE.match(html);
                auto endMatch = endRE.match(html);

                if (startMatch.hasMatch() && endMatch.hasMatch()) {
                    int startPos = startMatch.captured(1).toInt();
                    int endPos = endMatch.captured(1).toInt();

                    html = html.mid(startPos, endPos - startPos);
                }

                GlobalUnlock(hData);
            }
        }
    }

    // Get image
    if (IsClipboardFormatAvailable(CF_BITMAP)) {
        if (HANDLE hBitmap = GetClipboardData(CF_BITMAP)) {
            QImage qimage = QImage::fromHBITMAP(static_cast<HBITMAP>(hBitmap));
            image = QPixmap::fromImage(qimage);
        }
    }

    // Get URLs
    if (IsClipboardFormatAvailable(CF_HDROP)) {
        if (HANDLE hData = GetClipboardData(CF_HDROP)) {
            if (HDROP hdrop = static_cast<HDROP>(GlobalLock(hData))) {
                UINT fileCount = DragQueryFile(hdrop, 0xFFFFFFFF, NULL, 0);
                for (UINT i = 0; i < fileCount; i++) {
                    WCHAR filePath[MAX_PATH];
                    if (DragQueryFile(hdrop, i, filePath, MAX_PATH)) {
                        urls.append(QUrl::fromLocalFile(QString::fromWCharArray(filePath)));
                    }
                }
                GlobalUnlock(hData);
            }
        }
    }

    CloseClipboard();

#else
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
#endif
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
