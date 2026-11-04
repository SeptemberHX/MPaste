// input: Depends on CopySoundPlayer.h, MPasteSettings, and PlaySound / pw-play / paplay.
// output: Implements copy-sound playback with burst suppression.
// pos: Widget-layer helper implementation.
// update: If I change, update this header block.
#include "CopySoundPlayer.h"
#include "utils/MPasteSettings.h"
#include <QDateTime>
#include <QDebug>
#include <QFile>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#else
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#endif

CopySoundPlayer::CopySoundPlayer(const QString &resourcePath, QObject *parent)
    : QObject(parent)
    , resourcePath_(resourcePath) {
}

CopySoundPlayer::~CopySoundPlayer() {
#ifdef Q_OS_WIN
    // PlaySound reads the sound from our buffer while it plays.
    if (!sound_.isEmpty()) {
        PlaySoundW(nullptr, nullptr, 0);
    }
#endif
}

bool CopySoundPlayer::play() {
    if (sound_.isEmpty()) {
        QFile file(resourcePath_);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "[sound] cannot read" << resourcePath_;
            return false;
        }
        sound_ = file.readAll();
    }
#ifdef Q_OS_WIN
    // Asynchronous, from memory; a new sound stops one still playing.
    return PlaySoundW(reinterpret_cast<LPCWSTR>(sound_.constData()), nullptr,
                      SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#else
    if (playerProgram_.isEmpty()) {
        for (const QString &candidate : {QStringLiteral("pw-play"), QStringLiteral("paplay")}) {
            playerProgram_ = QStandardPaths::findExecutable(candidate);
            if (!playerProgram_.isEmpty()) {
                break;
            }
        }
        if (playerProgram_.isEmpty()) {
            qWarning() << "[sound] no pw-play or paplay found; the copy sound is silent";
            playerProgram_ = QStringLiteral("-");
        }
    }
    if (playerProgram_ == QStringLiteral("-")) {
        return false;
    }
    if (soundFile_.isEmpty() || !QFile::exists(soundFile_)) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QDir().mkpath(dir);
        const QString path = dir + QStringLiteral("/copy-sound.wav");
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(sound_) != sound_.size()) {
            qWarning() << "[sound] cannot write" << path;
            return false;
        }
        soundFile_ = path;
    }
    // Its own short-lived process: nothing of it stays in ours.
    return QProcess::startDetached(playerProgram_, {soundFile_});
#endif
}

void CopySoundPlayer::playCopySoundIfNeeded(int wId, const QByteArray &fingerprint) {
    if (!MPasteSettings::getInst()->isPlaySound()) {
        qInfo() << "[clipboard-widget] play sound disabled";
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastSoundPlayAtMs_ < SOUND_BURST_WINDOW_MS) {
        qInfo().noquote() << QStringLiteral("[clipboard-widget] suppress sound by burst window wId=%1 fp=%2 deltaMs=%3")
            .arg(wId)
            .arg(fingerprint.isEmpty() ? QStringLiteral("-") : QString::fromLatin1(fingerprint.toHex().left(12)))
            .arg(now - lastSoundPlayAtMs_);
        return;
    }

    const bool started = play();
    lastSoundPlayAtMs_ = now;
    qInfo().noquote() << QStringLiteral("[clipboard-widget] play copy sound wId=%1 fp=%2 started=%3")
        .arg(wId)
        .arg(fingerprint.isEmpty() ? QStringLiteral("-") : QString::fromLatin1(fingerprint.toHex().left(12)))
        .arg(started);
}
