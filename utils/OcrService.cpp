#include "OcrService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThread>

// ---------------------------------------------------------------------------
// Async dispatch
// ---------------------------------------------------------------------------

OcrService::OcrService(QObject *parent)
    : QObject(parent)
{
}

void OcrService::requestOcr(const QString &itemName, const QImage &image) {
    if (itemName.isEmpty() || image.isNull()) {
        Result r;
        r.status = Failed;
        r.errorMessage = QStringLiteral("empty input");
        emit ocrFinished(itemName, r);
        return;
    }

    QPointer<OcrService> guard(this);
    QThread *thread = QThread::create([guard, itemName, image]() {
#ifdef Q_OS_WIN
        Result result = runOcrWindows(image);
#else
        Result result = runOcrLinux(image);
#endif
        if (guard) {
            QMetaObject::invokeMethod(guard.data(), [guard, itemName, result]() {
                if (guard) {
                    emit guard->ocrFinished(itemName, result);
                }
            }, Qt::QueuedConnection);
        }
    });
    thread->setObjectName(QStringLiteral("ocr-") + itemName);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

// ---------------------------------------------------------------------------
// Sidecar helpers
// ---------------------------------------------------------------------------

QString OcrService::sidecarPath(const QString &mpastePath) {
    if (mpastePath.isEmpty()) {
        return {};
    }
    // 1234567890123.mpaste → 1234567890123.ocr.json
    QString path = mpastePath;
    if (path.endsWith(QStringLiteral(".mpaste"), Qt::CaseInsensitive)) {
        path.chop(7); // remove ".mpaste"
    }
    return path + QStringLiteral(".ocr.json");
}

OcrService::Result OcrService::readSidecar(const QString &mpastePath) {
    Result r;
    const QString path = sidecarPath(mpastePath);
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return r; // status == None
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return r;
    }
    const QJsonObject obj = doc.object();
    r.status = static_cast<Status>(obj.value(QStringLiteral("status")).toInt(0));
    r.text = obj.value(QStringLiteral("text")).toString();
    r.errorMessage = obj.value(QStringLiteral("error")).toString();
    return r;
}

bool OcrService::writeSidecar(const QString &mpastePath, const Result &result) {
    const QString path = sidecarPath(mpastePath);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    QJsonObject obj;
    obj[QStringLiteral("version")] = 1;
    obj[QStringLiteral("status")] = static_cast<int>(result.status);
    obj[QStringLiteral("text")] = result.text;
    if (!result.errorMessage.isEmpty()) {
        obj[QStringLiteral("error")] = result.errorMessage;
    }
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    return true;
}

bool OcrService::removeSidecar(const QString &mpastePath) {
    const QString path = sidecarPath(mpastePath);
    if (path.isEmpty() || !QFile::exists(path)) {
        return false;
    }
    return QFile::remove(path);
}

// ---------------------------------------------------------------------------
// Windows backend — PowerShell + Windows.Media.Ocr
// ---------------------------------------------------------------------------

#ifdef Q_OS_WIN

OcrService::Result OcrService::runOcrWindows(const QImage &image) {
    Result result;

    // Save image to a temp file.
    QTemporaryFile tempFile(QDir::tempPath() + QStringLiteral("/mpaste_ocr_XXXXXX.png"));
    tempFile.setAutoRemove(true);
    if (!tempFile.open()) {
        result.status = Failed;
        result.errorMessage = QStringLiteral("cannot create temp file");
        return result;
    }
    if (!image.save(&tempFile, "PNG")) {
        result.status = Failed;
        result.errorMessage = QStringLiteral("cannot save image to temp file");
        return result;
    }
    tempFile.close();

    // Write a PowerShell script to a temp file and execute it.
    // Uses Windows.Media.Ocr (built-in since Windows 10 1809).
    QTemporaryFile psFile(QDir::tempPath() + QStringLiteral("/mpaste_ocr_XXXXXX.ps1"));
    psFile.setAutoRemove(false);
    if (!psFile.open()) {
        result.status = Failed;
        result.errorMessage = QStringLiteral("cannot create ps1 temp file");
        return result;
    }
    const QString imagePath = QDir::toNativeSeparators(tempFile.fileName());
    const QString script = QStringLiteral(
        "Add-Type -AssemblyName System.Runtime.WindowsRuntime\n"
        "$null = [Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType=WindowsRuntime]\n"
        "$null = [Windows.Graphics.Imaging.BitmapDecoder, Windows.Foundation, ContentType=WindowsRuntime]\n"
        "function Await {\n"
        "    param($AsyncOp, [Type]$ResultType)\n"
        "    $m = ([System.WindowsRuntimeSystemExtensions].GetMethods() |\n"
        "        Where-Object { $_.Name -eq 'AsTask' -and\n"
        "                       $_.GetParameters().Count -eq 1 -and\n"
        "                       $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]\n"
        "    $t = $m.MakeGenericMethod($ResultType).Invoke($null, @($AsyncOp))\n"
        "    $t.Wait()\n"
        "    return $t.Result\n"
        "}\n"
        "$stream = [System.IO.File]::OpenRead('%1')\n"
        "$ras = [System.IO.WindowsRuntimeStreamExtensions]::AsRandomAccessStream($stream)\n"
        "$decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($ras)) ([Windows.Graphics.Imaging.BitmapDecoder])\n"
        "$bitmap = Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])\n"
        "$engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()\n"
        "if ($engine -eq $null) { Write-Error 'OCR engine not available'; exit 1 }\n"
        "$result = Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])\n"
        "$stream.Dispose()\n"
        "Write-Output $result.Text\n"
    ).arg(QString(imagePath).replace(QLatin1Char('\''), QStringLiteral("''")));
    psFile.write(script.toUtf8());
    psFile.close();

    QProcess proc;
    proc.setProgram(QStringLiteral("powershell.exe"));
    proc.setArguments({
        QStringLiteral("-NoProfile"),
        QStringLiteral("-NonInteractive"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), QDir::toNativeSeparators(psFile.fileName())
    });
    proc.start();
    const bool finished = proc.waitForFinished(30000);
    QFile::remove(psFile.fileName()); // clean up ps1 temp file

    if (!finished) {
        result.status = Failed;
        result.errorMessage = QStringLiteral("OCR process timed out");
        return result;
    }

    if (proc.exitCode() != 0) {
        result.status = Failed;
        result.errorMessage = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        if (result.errorMessage.isEmpty()) {
            result.errorMessage = QStringLiteral("OCR process exited with code %1").arg(proc.exitCode());
        }
        return result;
    }

    result.status = Ready;
    result.text = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    return result;
}

#endif // Q_OS_WIN

// ---------------------------------------------------------------------------
// Linux backend — tesseract CLI
// ---------------------------------------------------------------------------

#ifndef Q_OS_WIN

OcrService::Result OcrService::runOcrLinux(const QImage &image) {
    Result result;

    QTemporaryFile tempFile(QDir::tempPath() + QStringLiteral("/mpaste_ocr_XXXXXX.png"));
    tempFile.setAutoRemove(true);
    if (!tempFile.open() || !image.save(&tempFile, "PNG")) {
        result.status = Failed;
        result.errorMessage = QStringLiteral("cannot save temp image");
        return result;
    }
    tempFile.close();

    // tesseract <input> stdout
    QProcess proc;
    proc.setProgram(QStringLiteral("tesseract"));
    proc.setArguments({tempFile.fileName(), QStringLiteral("stdout")});
    proc.start();
    if (!proc.waitForFinished(30000)) {
        result.status = Failed;
        result.errorMessage = QStringLiteral("tesseract timed out or not found");
        return result;
    }

    if (proc.exitCode() != 0) {
        result.status = Failed;
        result.errorMessage = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        if (result.errorMessage.isEmpty()) {
            result.errorMessage = QStringLiteral("tesseract exited with code %1").arg(proc.exitCode());
        }
        return result;
    }

    result.status = Ready;
    result.text = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    return result;
}

#endif // !Q_OS_WIN
