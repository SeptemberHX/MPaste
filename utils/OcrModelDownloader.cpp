// input: Depends on OcrModelDownloader.h and Qt Network.
// output: Implements the checked, cancellable model download.
// pos: Utils-layer helper behind Settings → Text recognition → Model on Linux.
// update: If I change, update this header block and my folder README.md.
#include "OcrModelDownloader.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

OcrModelDownloader::OcrModelDownloader(QObject *parent)
    : QObject(parent), network_(new QNetworkAccessManager(this)) {
}

OcrModelDownloader::~OcrModelDownloader() {
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
    }
    if (part_.isOpen()) {
        part_.close();
        part_.remove();
    }
}

void OcrModelDownloader::start(const QString &model) {
    if (isRunning()) {
        return;
    }
    files_ = OcrService::modelFiles(model);
    index_ = 0;
    doneBytes_ = 0;
    totalBytes_ = 0;
    for (const auto &file : files_) {
        totalBytes_ += file.size;
    }
    if (files_.isEmpty() || !QDir().mkpath(OcrService::userModelsDir())) {
        emit finished(false, QStringLiteral("cannot create %1").arg(OcrService::userModelsDir()));
        return;
    }
    startNext();
}

void OcrModelDownloader::cancel() {
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
        reply_ = nullptr;
    }
    if (part_.isOpen()) {
        part_.close();
        part_.remove();
    }
    emit finished(false, QStringLiteral("cancelled"));
}

void OcrModelDownloader::fail(const QString &error) {
    if (part_.isOpen()) {
        part_.close();
        part_.remove();
    }
    reply_ = nullptr;
    emit finished(false, error);
}

void OcrModelDownloader::startNext() {
    // Skip a file already there at its size (an earlier, interrupted run).
    while (index_ < files_.size()
           && QFileInfo(OcrService::userModelsDir() + QLatin1Char('/') + files_[index_].name).size() == files_[index_].size) {
        doneBytes_ += files_[index_].size;
        ++index_;
    }
    if (index_ >= files_.size()) {
        reply_ = nullptr;
        emit progress(totalBytes_, totalBytes_);
        emit finished(true, QString());
        return;
    }
    const OcrService::ModelFile file = files_[index_];
    part_.setFileName(OcrService::userModelsDir() + QLatin1Char('/') + file.name + QStringLiteral(".part"));
    if (!part_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(part_.errorString());
        return;
    }
    hash_.reset();
    QNetworkRequest request{QUrl(file.url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    reply_ = network_->get(request);
    connect(reply_, &QNetworkReply::readyRead, this, [this]() {
        const QByteArray chunk = reply_->readAll();
        hash_.addData(chunk);
        if (part_.write(chunk) != chunk.size()) {
            reply_->disconnect(this);
            reply_->abort();
            fail(part_.errorString());
        }
    });
    connect(reply_, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64) {
        emit progress(doneBytes_ + received, totalBytes_);
    });
    connect(reply_, &QNetworkReply::finished, this, [this, file]() {
        QNetworkReply *reply = reply_;
        reply_ = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            fail(reply->errorString());
            return;
        }
        const QByteArray rest = reply->readAll();
        hash_.addData(rest);
        part_.write(rest);
        part_.close();
        if (hash_.result().toHex() != file.sha256) {
            part_.remove();
            fail(QStringLiteral("%1: checksum does not match").arg(file.name));
            return;
        }
        const QString target = OcrService::userModelsDir() + QLatin1Char('/') + file.name;
        QFile::remove(target);
        if (!part_.rename(target)) {
            fail(part_.errorString());
            return;
        }
        doneBytes_ += file.size;
        ++index_;
        startNext();
    });
}
