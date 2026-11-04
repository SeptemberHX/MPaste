#ifndef MPASTE_OCR_SERVICE_H
#define MPASTE_OCR_SERVICE_H

#include <QObject>
#include <QString>
#include <QImage>
#include <QJsonObject>

/// Lightweight OCR service.
///
/// V1 scope:
///   - Manual trigger only (context menu "Extract Text").
///   - Results stored in sidecar `.ocr.json` next to the `.mpaste` file.
///   - OCR text is merged into the search index but never into the
///     original MIME payload.
///   - On Windows: calls Windows.Media.Ocr via PowerShell (zero deps).
///   - On Linux: starts `mpaste-ocr` (PaddleOCR through ONNX Runtime, which
///     is installed apart), else the `tesseract` CLI.
class OcrService : public QObject {
    Q_OBJECT

public:
    enum Status { None = 0, Pending, Ready, Failed };

    struct Result {
        Status status = None;
        QString text;
        QString errorMessage;
    };

    explicit OcrService(QObject *parent = nullptr);

    /// Run OCR on \a image asynchronously.  The result is delivered via
    /// the \c ocrFinished signal, tagged with \a itemName.
    void requestOcr(const QString &itemName, const QImage &image);

    // ---- sidecar file helpers ----

    /// Return the sidecar path for a given `.mpaste` path.
    static QString sidecarPath(const QString &mpastePath);

    /// Read a previously cached result.  Returns a result with
    /// status == None if the sidecar does not exist.
    static Result readSidecar(const QString &mpastePath);

    /// Write a result to the sidecar file.
    static bool writeSidecar(const QString &mpastePath, const Result &result);

    /// Remove the sidecar file (called when an item is deleted).
    static bool removeSidecar(const QString &mpastePath);

    /// Linux: whether recognition on this computer works, and if not, what
    /// is missing. Starts `mpaste-ocr --check`, so call it off the main thread.
    enum LocalState { LocalReady = 0, LocalNoHelper, LocalNoRuntime };
    static LocalState localState();
    /// The command that installs ONNX Runtime: "sudo apt install libonnxruntime1.23".
    static QString runtimeInstallCommand();

signals:
    /// Emitted on the main thread when OCR completes or fails.
    void ocrFinished(const QString &itemName, const OcrService::Result &result);

private:
    static Result runOcrWindows(const QImage &image);
    static Result runOcrLinux(const QImage &image);
    static Result runOcrBaidu(const QImage &image,
                              const QString &apiKey,
                              const QString &secretKey);
};

#endif // MPASTE_OCR_SERVICE_H
