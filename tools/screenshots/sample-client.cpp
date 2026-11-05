// A made-up app for the README screenshots: it takes the desktop file id of
// a real app (so GNOME names it and gives it that app's icon), shows a
// window, and puts on the clipboard what take.py writes to command.json:
// {"text": ..., "html": ..., "image": "/path.png", "urls": ["/path"]}.
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMimeData>
#include <QTimer>
#include <QUrl>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (argc != 3) return 2;
    const QString base = QString::fromLocal8Bit(argv[1]);
    QGuiApplication::setDesktopFileName(QString::fromLocal8Bit(argv[2]));
    QLabel window(QStringLiteral("MPaste sample"));
    window.setWindowTitle(QStringLiteral("MPaste Sample"));
    window.resize(480, 320);
    window.show();
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        QFile input(base + "/command.json");
        if (!input.open(QIODevice::ReadOnly)) return;
        const QJsonObject command = QJsonDocument::fromJson(input.readAll()).object();
        input.close();
        input.remove();
        auto *mime = new QMimeData;
        if (command.contains("text")) mime->setText(command["text"].toString());
        if (command.contains("html")) mime->setHtml(command["html"].toString());
        if (command.contains("image")) mime->setImageData(QImage(command["image"].toString()));
        if (command.contains("urls")) {
            QList<QUrl> urls;
            for (const auto &path : command["urls"].toArray()) urls << QUrl::fromLocalFile(path.toString());
            mime->setUrls(urls);
        }
        QApplication::clipboard()->setMimeData(mime);
    });
    timer.start(100);
    return app.exec();
}
