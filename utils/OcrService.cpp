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
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QStandardPaths>
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
    const QString localModel = settings->getOcrLocalModel();

    QPointer<OcrService> guard(this);
    QThread *thread = QThread::create([guard, itemName, image, backend, baiduApiKey, baiduSecretKey, localModel]() {
        Result result;
        if (backend == MPasteSettings::OcrBaiduApi
            && !baiduApiKey.isEmpty() && !baiduSecretKey.isEmpty()) {
            result = runOcrBaidu(image, baiduApiKey, baiduSecretKey);
        } else {
#ifdef Q_OS_WIN
            result = runOcrWindows(image);
#else
            result = runOcrLinux(image, localModel);
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

namespace {
bool chineseUi() {
    const QLocale locale = QLocale::system();
    return locale.language() == QLocale::Chinese || locale.name().startsWith(QStringLiteral("zh"));
}

// mpaste-ocr and its models: next to MPaste in a build or a local install,
// under share/mpaste/ocr when installed from the package.
QString findOcrHelper() {
    const QString beside = QCoreApplication::applicationDirPath() + QStringLiteral("/mpaste-ocr");
    if (QFileInfo(beside).isExecutable()) {
        return beside;
    }
    return QStandardPaths::findExecutable(QStringLiteral("mpaste-ocr"));
}

QString findOcrModels(const QString &helper) {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QFileInfo(helper).absolutePath() + QStringLiteral("/ocr-models"),
        appDir + QStringLiteral("/ocr-models"),
        appDir + QStringLiteral("/../share/mpaste/ocr"),
        QStringLiteral("/usr/share/mpaste/ocr"),
    };
    for (const QString &dir : candidates) {
        if (QFileInfo::exists(dir + QStringLiteral("/PP-OCRv6_rec_tiny.onnx"))) {
            return QDir::cleanPath(dir);
        }
    }
    return QString();
}

// The last line the helper or tesseract wrote that says something: the
// ONNX Runtime of some distributions writes pages of schema notices first.
QString lastMeaningfulLine(const QByteArray &output) {
    const QStringList lines = QString::fromUtf8(output).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const QString line = it->trimmed();
        if (!line.isEmpty() && !line.startsWith(QStringLiteral("Schema error"))) {
            return line;
        }
    }
    return QString();
}

QString runtimeMissingMessage() {
    return chineseUi()
        ? QStringLiteral("本机识别需要 ONNX Runtime（连同依赖约 80 MB），安装后即可使用：%1").arg(OcrService::runtimeInstallCommand())
        : QStringLiteral("Recognition on this computer needs ONNX Runtime (about 80 MB with what it needs): %1").arg(OcrService::runtimeInstallCommand());
}

// tesseract's languages, Simplified Chinese first when it has them.
QString tesseractLanguages() {
    QProcess proc;
    proc.start(QStringLiteral("tesseract"), {QStringLiteral("--list-langs")});
    if (!proc.waitForFinished(5000)) {
        return QStringLiteral("eng");
    }
    const QString list = QString::fromUtf8(proc.readAllStandardOutput() + proc.readAllStandardError());
    QStringList langs;
    for (const QString &lang : {QStringLiteral("chi_sim"), QStringLiteral("chi_tra"), QStringLiteral("eng")}) {
        if (list.contains(QRegularExpression(QStringLiteral("(^|\\s)%1(\\s|$)").arg(lang)))) {
            langs << lang;
        }
    }
    return langs.isEmpty() ? QStringLiteral("eng") : langs.join(QLatin1Char('+'));
}
}  // namespace

QString OcrService::runtimeInstallCommand() {
#ifdef MPASTE_ONNXRUNTIME_PACKAGE
    return QStringLiteral("sudo apt install " MPASTE_ONNXRUNTIME_PACKAGE);
#else
    return QStringLiteral("sudo apt install libonnxruntime1.23");
#endif
}

QList<OcrService::ModelFile> OcrService::modelFiles(const QString &model) {
    const QString base = QStringLiteral("https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.10.0/onnx/PP-OCRv6/");
    if (model == QLatin1String("small")) {
        return {
            {QStringLiteral("PP-OCRv6_det_small.onnx"), base + QStringLiteral("det/PP-OCRv6_det_small.onnx"),
             "090f04abcd9d9a7498bc4ebf677e4cb9bdce1fe4197ddb7e529f1ef44e1ff94f", 9929594},
            {QStringLiteral("PP-OCRv6_rec_small.onnx"), base + QStringLiteral("rec/PP-OCRv6_rec_small.onnx"),
             "6f327246b50388f3c176ae304bd95767ea6dc0c9ae92153ef8cbe210b3c14884", 21234383},
        };
    }
    return {};
}

QString OcrService::userModelsDir() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/MPaste/ocr-models");
}

bool OcrService::isModelDownloaded(const QString &model) {
    const QList<ModelFile> files = modelFiles(model);
    if (files.isEmpty()) {
        return false;
    }
    for (const ModelFile &file : files) {
        if (QFileInfo(userModelsDir() + QLatin1Char('/') + file.name).size() != file.size) {
            return false;
        }
    }
    return true;
}

OcrService::LocalState OcrService::localState() {
    const QString helper = findOcrHelper();
    if (helper.isEmpty() || findOcrModels(helper).isEmpty()) {
        return LocalNoHelper;
    }
    QProcess proc;
    proc.start(helper, {QStringLiteral("--check")});
    if (!proc.waitForFinished(10000)) {
        proc.kill();
        return LocalNoRuntime;
    }
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0 ? LocalReady : LocalNoRuntime;
}

OcrService::Result OcrService::runOcrLinux(const QImage &image, const QString &model) {
    Result result;

    QTemporaryFile tempFile(QDir::tempPath() + QStringLiteral("/mpaste_ocr_XXXXXX.png"));
    tempFile.setAutoRemove(true);
    if (!tempFile.open() || !image.save(&tempFile, "PNG")) {
        result.status = Failed;
        result.errorMessage = QStringLiteral("cannot save temp image");
        return result;
    }
    tempFile.close();

    // PaddleOCR through mpaste-ocr, its own process: its models are in
    // memory only while it reads.
    // The downloaded model when it is chosen and whole, else the one in the package.
    const QString helper = findOcrHelper();
    const bool useDownloaded = model != QLatin1String("tiny") && isModelDownloaded(model);
    const QString models = helper.isEmpty() ? QString() : (useDownloaded ? userModelsDir() : findOcrModels(helper));
    bool runtimeMissing = false;
    if (!helper.isEmpty() && !models.isEmpty()) {
        QProcess proc;
        proc.start(helper, {QStringLiteral("--models"), models,
                            QStringLiteral("--model"), useDownloaded ? model : QStringLiteral("tiny"),
                            tempFile.fileName()});
        // The precise model on a long screenshot takes a few seconds; two
        // minutes is a hang.
        if (!proc.waitForFinished(120000)) {
            proc.kill();
            proc.waitForFinished(2000);
            result.status = Failed;
            result.errorMessage = chineseUi() ? QStringLiteral("文字识别超时") : QStringLiteral("Text recognition timed out");
            return result;
        }
        if (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0) {
            result.status = Ready;
            result.text = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
            return result;
        }
        runtimeMissing = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 5;
        qWarning().noquote() << QStringLiteral("[ocr] mpaste-ocr failed code=%1 %2")
                                    .arg(proc.exitCode()).arg(lastMeaningfulLine(proc.readAllStandardError()));
        // Falls through to tesseract, if there is one.
    }

    if (QStandardPaths::findExecutable(QStringLiteral("tesseract")).isEmpty()) {
        result.status = Failed;
        if (runtimeMissing) {
            result.errorMessage = runtimeMissingMessage();
            return result;
        }
        result.errorMessage = chineseUi()
            ? QStringLiteral("没有可用的文字识别：请安装 MPaste 的完整包（含 mpaste-ocr），或运行 sudo apt install tesseract-ocr tesseract-ocr-chi-sim")
            : QStringLiteral("No text recognition available: install MPaste's full package (with mpaste-ocr), or run sudo apt install tesseract-ocr tesseract-ocr-chi-sim");
        return result;
    }

    // tesseract <input> stdout -l <languages>
    QProcess proc;
    proc.start(QStringLiteral("tesseract"), {tempFile.fileName(), QStringLiteral("stdout"),
                                             QStringLiteral("-l"), tesseractLanguages()});
    if (!proc.waitForFinished(30000)) {
        proc.kill();
        result.status = Failed;
        result.errorMessage = chineseUi() ? QStringLiteral("文字识别超时") : QStringLiteral("Text recognition timed out");
        return result;
    }

    if (proc.exitCode() != 0) {
        result.status = Failed;
        result.errorMessage = lastMeaningfulLine(proc.readAllStandardError());
        if (result.errorMessage.isEmpty()) {
            result.errorMessage = QStringLiteral("tesseract exited with code %1").arg(proc.exitCode());
        }
        return result;
    }

    result.status = Ready;
    result.text = removeCjkSpaces(QString::fromUtf8(proc.readAllStandardOutput()).trimmed());
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
