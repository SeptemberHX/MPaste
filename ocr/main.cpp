// input: Depends on PaddleOcr and Qt Gui (for reading the picture).
// output: mpaste-ocr: prints the text in a picture, for MPaste to read.
// pos: A helper program next to MPaste on Linux; MPaste starts it for each
//      recognition, so the models are in memory only while it reads.
// update: If I change, update this header block and my folder README.md.
//
// mpaste-ocr --models DIR [--model tiny|small] [--threads N] PICTURE
// mpaste-ocr --check
// Prints the text as UTF-8 on standard output. Exit 0 on success, 2 for bad
// arguments, 3 when the models do not load, 4 when the picture does not, 5
// when ONNX Runtime is not installed (it is loaded at run time, so the
// package only suggests it). --check only loads ONNX Runtime.
#include "PaddleOcr.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QThread>

#include <cstdio>

namespace {
// PP-OCRv6_det_<model>.onnx and PP-OCRv6_rec_<model>.onnx: tiny or small.
QString modelFile(const QString &kind, const QString &model) {
    return QStringLiteral("PP-OCRv6_%1_%2.onnx").arg(kind, model);
}

int fail(int code, const QString &message) {
    std::fprintf(stderr, "mpaste-ocr: %s\n", message.toLocal8Bit().constData());
    return code;
}
}  // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    QString modelsDir;
    QString picture;
    QString model = QStringLiteral("tiny");
    int threads = qBound(1, QThread::idealThreadCount(), 4);
    if (args.contains(QStringLiteral("--check"))) {
        std::string why;
        if (!PaddleOcr::loadRuntime(&why)) {
            return fail(5, QString::fromStdString(why));
        }
        std::puts("ok");
        return 0;
    }
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == QLatin1String("--models") && i + 1 < args.size()) {
            modelsDir = args[++i];
        } else if (args[i] == QLatin1String("--model") && i + 1 < args.size()) {
            model = args[++i];
            if (model != QLatin1String("tiny") && model != QLatin1String("small")) {
                return fail(2, QStringLiteral("unknown model %1").arg(model));
            }
        } else if (args[i] == QLatin1String("--threads") && i + 1 < args.size()) {
            threads = qBound(1, args[++i].toInt(), 16);
        } else if (!args[i].startsWith(QLatin1String("--"))) {
            picture = args[i];
        }
    }
    if (modelsDir.isEmpty() || picture.isEmpty()) {
        return fail(2, QStringLiteral("usage: mpaste-ocr --models DIR [--model tiny|small] [--threads N] PICTURE"));
    }

    std::string runtimeError;
    if (!PaddleOcr::loadRuntime(&runtimeError)) {
        return fail(5, QString::fromStdString(runtimeError));
    }
    const QDir dir(modelsDir);
    PaddleOcr ocr;
    const std::string error = ocr.load(QFile::encodeName(dir.filePath(modelFile(QStringLiteral("det"), model))).toStdString(),
                                       QFile::encodeName(dir.filePath(modelFile(QStringLiteral("rec"), model))).toStdString(),
                                       threads);
    if (!error.empty()) {
        return fail(3, QString::fromStdString(error));
    }

    QImageReader reader(picture);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        return fail(4, QStringLiteral("cannot read %1: %2").arg(picture, reader.errorString()));
    }
    // Transparent parts read as white, as on a page.
    if (image.hasAlphaChannel()) {
        QImage flat(image.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter(&flat).drawImage(0, 0, image);
        image = flat;
    }
    image = image.convertToFormat(QImage::Format_BGR888);
    PaddleOcrImage::Bgr bgr;
    bgr.width = image.width();
    bgr.height = image.height();
    bgr.data = image.constBits();
    bgr.stride = std::size_t(image.bytesPerLine());

    const std::string text = ocr.read(bgr);
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fputc('\n', stdout);
    return 0;
}
