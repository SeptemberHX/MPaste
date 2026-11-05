// input: Depends on Qt Network and OcrService's model table.
// output: Downloads a recognition model's files into OcrService::userModelsDir(), checked against their SHA-256.
// pos: Utils-layer helper behind Settings → Text recognition → Model on Linux.
// update: If I change, update this header block and my folder README.md.
#ifndef MPASTE_OCRMODELDOWNLOADER_H
#define MPASTE_OCRMODELDOWNLOADER_H

#include <QCryptographicHash>
#include <QFile>
#include <QList>
#include <QNetworkReply>   // complete: Qt 6.8's QPointer compares through a static_cast
#include <QObject>
#include <QPointer>

#include "OcrService.h"

class QNetworkAccessManager;

// One file after the other, each into a .part file hashed as it arrives and
// renamed when whole and right; nothing half-written is left as a model.
class OcrModelDownloader : public QObject {
    Q_OBJECT

public:
    explicit OcrModelDownloader(QObject *parent = nullptr);
    ~OcrModelDownloader() override;

    void start(const QString &model);
    void cancel();
    bool isRunning() const { return reply_ != nullptr; }

signals:
    void progress(qint64 received, qint64 total);
    // ok, or why not ("cancelled" when cancelled).
    void finished(bool ok, const QString &error);

private:
    void startNext();
    void fail(const QString &error);

    QNetworkAccessManager *network_ = nullptr;
    QPointer<QNetworkReply> reply_;
    QList<OcrService::ModelFile> files_;
    int index_ = 0;
    qint64 doneBytes_ = 0;
    qint64 totalBytes_ = 0;
    QFile part_;
    QCryptographicHash hash_{QCryptographicHash::Sha256};
};

#endif // MPASTE_OCRMODELDOWNLOADER_H
