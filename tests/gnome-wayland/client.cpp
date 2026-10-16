// Native Wayland clipboard producer and paste target for the isolated integration test.
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QTextEdit>
#include <QTimer>
#include <QDataStream>
#include <QDateTime>
#include <QPixmap>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--verify-icon") {
        QFile file(QString::fromLocal8Bit(argv[2]));
        if (!file.open(QIODevice::ReadOnly)) return 2;
        QDataStream in(&file);
        QString magic, name;
        quint32 version, flags;
        QDateTime time;
        QPixmap icon;
        in >> magic >> version >> flags >> time >> name >> icon;
        if (in.status() != QDataStream::Ok || icon.isNull()) return 3;
        const QImage image = icon.toImage();
        return image.pixelColor(image.width() / 2, image.height() / 2) == QColor("#12abcd") ? 0 : 4;
    }
    if (argc != 2) return 2;
    QGuiApplication::setDesktopFileName(QStringLiteral("org.mpaste.IconTest"));
    const QString base = QString::fromLocal8Bit(argv[1]);
    QTextEdit edit;
    edit.setWindowTitle("MPaste Test Target");
    edit.resize(600, 400);
    edit.show();
    QObject::connect(&edit, &QTextEdit::textChanged, [&] {
        QFile text(base + "/pasted.txt");
        if (text.open(QIODevice::WriteOnly)) text.write(edit.toPlainText().toUtf8());
        QFile html(base + "/pasted.html");
        if (html.open(QIODevice::WriteOnly)) html.write(edit.toHtml().toUtf8());
    });
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        QFile input(base + "/command.json");
        if (!input.open(QIODevice::ReadOnly)) return;
        const auto command = QJsonDocument::fromJson(input.readAll()).object();
        input.close();
        input.remove();
        auto *mime = new QMimeData;
        if (command.contains("text")) mime->setText(command["text"].toString());
        if (command.contains("html")) mime->setHtml(command["html"].toString());
        if (command.contains("image")) {
            QImage image(32, 32, QImage::Format_ARGB32);
            image.fill(Qt::red);
            mime->setImageData(image);
        }
        if (command.contains("urls"))
            mime->setUrls({QUrl::fromLocalFile(command["urls"].toString())});
        QApplication::clipboard()->setMimeData(mime);
    });
    timer.start(100);
    return app.exec();
}
