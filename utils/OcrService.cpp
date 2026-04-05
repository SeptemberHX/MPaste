#include "OcrService.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QUrlQuery>

#include "MPasteSettings.h"
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>

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

    qInfo() << "[ocr] requestOcr item=" << itemName
            << "imageSize=" << image.size();

    // Read backend config on the main thread before dispatching.
    const auto *settings = MPasteSettings::getInst();
    const auto backend = settings->getOcrBackend();
    const QString baiduApiKey = settings->getBaiduOcrApiKey();
    const QString baiduSecretKey = settings->getBaiduOcrSecretKey();

    QPointer<OcrService> guard(this);
    QThread *thread = QThread::create([guard, itemName, image, backend, baiduApiKey, baiduSecretKey]() {
        Result result;
        if (backend == MPasteSettings::OcrBaiduApi
            && !baiduApiKey.isEmpty() && !baiduSecretKey.isEmpty()) {
            result = runOcrBaidu(image, baiduApiKey, baiduSecretKey);
        } else {
#ifdef Q_OS_WIN
            result = runOcrWindows(image);
#else
            result = runOcrLinux(image);
#endif
        }
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
// Post-processing helpers
// ---------------------------------------------------------------------------

namespace {

// Remove spaces inserted between CJK characters by Windows OCR.
// Keeps spaces between Latin/digit words intact.
QString removeCjkSpaces(const QString &text) {
    if (text.isEmpty()) return text;
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char(' ') && i > 0 && i + 1 < text.size()) {
            const QChar prev = text.at(i - 1);
            const QChar next = text.at(i + 1);
            // Skip space if either neighbor is CJK.
            const bool prevCjk = prev.unicode() >= 0x2E80;
            const bool nextCjk = next.unicode() >= 0x2E80;
            if (prevCjk || nextCjk) {
                continue;
            }
        }
        out.append(ch);
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Windows backend — PowerShell + Windows.Media.Ocr
// ---------------------------------------------------------------------------

#ifdef Q_OS_WIN

OcrService::Result OcrService::runOcrWindows(const QImage &image) {
    Result result;

    // Save image to a regular temp file (not QTemporaryFile, which on
    // Windows sets FILE_FLAG_DELETE_ON_CLOSE and removes the file
    // as soon as it is closed — before PowerShell can read it).
    const QString tempFilePath = QDir::tempPath() + QStringLiteral("/mpaste_ocr_")
        + QString::number(QCoreApplication::applicationPid()) + QStringLiteral(".png");
    if (!image.save(tempFilePath, "PNG")) {
        result.status = Failed;
        result.errorMessage = QStringLiteral("cannot save image to temp file");
        return result;
    }

    // Build PowerShell script and pass via -EncodedCommand (base64
    // UTF-16LE) to avoid temp-file and escaping issues.
    // Uses Windows.Media.Ocr (built-in since Windows 10 1809).
    const QString imagePath = QDir::toNativeSeparators(tempFilePath);
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
        "# Try all available OCR languages, keep the result with the most text.\n"
        "$bestText = ''\n"
        "$langs = [Windows.Media.Ocr.OcrEngine]::AvailableRecognizerLanguages\n"
        "foreach ($lang in $langs) {\n"
        "    $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage($lang)\n"
        "    if ($engine -ne $null) {\n"
        "        $r = Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])\n"
        "        if ($r.Text.Length -gt $bestText.Length) { $bestText = $r.Text }\n"
        "    }\n"
        "}\n"
        "$stream.Dispose()\n"
        "[Console]::OutputEncoding = [System.Text.Encoding]::UTF8\n"
        "[Console]::Write($bestText)\n"
    ).arg(imagePath);

    // -EncodedCommand expects base64 of UTF-16LE bytes.
    const QByteArray utf16(reinterpret_cast<const char *>(script.utf16()),
                           script.size() * 2);
    const QString encoded = QString::fromLatin1(utf16.toBase64());

    QProcess proc;
    proc.setProgram(QStringLiteral("powershell.exe"));
    proc.setArguments({
        QStringLiteral("-NoProfile"),
        QStringLiteral("-NonInteractive"),
        QStringLiteral("-EncodedCommand"), encoded
    });
    proc.start();
    const bool finished = proc.waitForFinished(30000);
    QFile::remove(tempFilePath);
    const QString stderrOut = QString::fromUtf8(proc.readAllStandardError()).trimmed();
    if (!stderrOut.isEmpty()) {
        qInfo().noquote() << "[ocr] powershell:" << stderrOut;
    }

    if (!finished) {
        result.status = Failed;
        result.errorMessage = QStringLiteral("OCR process timed out");
        return result;
    }

    if (proc.exitCode() != 0) {
        result.status = Failed;
        result.errorMessage = stderrOut.isEmpty()
            ? QStringLiteral("OCR process exited with code %1").arg(proc.exitCode())
            : stderrOut;
        return result;
    }

    result.status = Ready;
    result.text = removeCjkSpaces(QString::fromUtf8(proc.readAllStandardOutput()).trimmed());
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

// ---------------------------------------------------------------------------
// Baidu OCR API backend (cross-platform)
// ---------------------------------------------------------------------------

namespace {

// Synchronous HTTP helper for use on worker threads.
QByteArray syncHttpPost(const QUrl &url, const QByteArray &body,
                        const QByteArray &contentType, int timeoutMs = 15000) {
    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    QNetworkReply *reply = nam.post(req, body);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    QByteArray result;
    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        result = reply->readAll();
    }
    reply->deleteLater();
    return result;
}

} // namespace

OcrService::Result OcrService::runOcrBaidu(const QImage &image,
                                            const QString &apiKey,
                                            const QString &secretKey) {
    Result result;

    // Step 1: Get access token (credentials in POST body).
    QUrl tokenUrl(QStringLiteral("https://aip.baidubce.com/oauth/2.0/token"));
    QUrlQuery tokenBody;
    tokenBody.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("client_credentials"));
    tokenBody.addQueryItem(QStringLiteral("client_id"), apiKey);
    tokenBody.addQueryItem(QStringLiteral("client_secret"), secretKey);
    const QByteArray tokenResp = syncHttpPost(tokenUrl,
                                               tokenBody.query(QUrl::FullyEncoded).toUtf8(),
                                               "application/x-www-form-urlencoded");
    if (tokenResp.isEmpty()) {
        result.status = Failed;
        result.errorMessage = QStringLiteral("Failed to get Baidu access token (network error)");
        return result;
    }
    const QJsonDocument tokenDoc = QJsonDocument::fromJson(tokenResp);
    const QString accessToken = tokenDoc.object().value(QStringLiteral("access_token")).toString();
    if (accessToken.isEmpty()) {
        result.status = Failed;
        result.errorMessage = QStringLiteral("Baidu token error: ")
            + tokenDoc.object().value(QStringLiteral("error_description")).toString();
        return result;
    }

    // Step 2: Encode image to base64.
    QByteArray imageBytes;
    {
        QBuffer buf(&imageBytes);
        buf.open(QIODevice::WriteOnly);
        // Scale down very large images to stay within the 4 MB API limit.
        QImage scaled = image;
        if (scaled.width() > 4096 || scaled.height() > 4096) {
            scaled = scaled.scaled(4096, 4096, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        scaled.save(&buf, "PNG");
    }
    const QByteArray base64Image = imageBytes.toBase64();

    // Step 3: Call OCR API.
    // Use general_basic (标准版, 500 free calls/day) instead of
    // accurate_basic (高精度版) which has stricter QPS limits.
    QUrl ocrUrl(QStringLiteral("https://aip.baidubce.com/rest/2.0/ocr/v1/general_basic"));
    QUrlQuery ocrQuery;
    ocrQuery.addQueryItem(QStringLiteral("access_token"), accessToken);
    ocrUrl.setQuery(ocrQuery);

    // Build the POST body manually — QUrlQuery double-encodes the
    // base64 payload which Baidu rejects.
    QByteArray ocrBodyData = "image=" + QUrl::toPercentEncoding(base64Image)
        + "&language_type=CHN_ENG&detect_direction=true";
    const QByteArray ocrResp = syncHttpPost(ocrUrl, ocrBodyData,
                                             "application/x-www-form-urlencoded",
                                             30000);
    if (ocrResp.isEmpty()) {
        result.status = Failed;
        result.errorMessage = QStringLiteral("Baidu OCR request failed (network error)");
        return result;
    }

    // Step 4: Parse response.
    const QJsonDocument ocrDoc = QJsonDocument::fromJson(ocrResp);
    const QJsonObject ocrObj = ocrDoc.object();
    if (ocrObj.contains(QStringLiteral("error_code"))) {
        result.status = Failed;
        result.errorMessage = QStringLiteral("Baidu OCR error %1: %2")
            .arg(ocrObj.value(QStringLiteral("error_code")).toInt())
            .arg(ocrObj.value(QStringLiteral("error_msg")).toString());
        return result;
    }

    const QJsonArray wordsResult = ocrObj.value(QStringLiteral("words_result")).toArray();
    QStringList lines;
    for (const QJsonValue &v : wordsResult) {
        lines << v.toObject().value(QStringLiteral("words")).toString();
    }

    result.status = Ready;
    result.text = lines.join(QStringLiteral("\n"));
    return result;
}
