// input: Depends on CopySoundPlayer.h, Qt Multimedia, and MPasteSettings.
// output: Implements copy-sound playback with device-change monitoring and burst suppression.
// pos: Widget-layer helper implementation.
// update: If I change, update this header block.
#include "CopySoundPlayer.h"
#include "utils/MPasteSettings.h"
#include <QDebug>

CopySoundPlayer::CopySoundPlayer(bool /*soundEnabled*/, const QUrl &soundSource, QObject *parent)
    : QObject(parent)
    , soundSource_(soundSource)
{
    rebuildSoundPlaybackChain(QMediaDevices::defaultAudioOutput());
}

void CopySoundPlayer::rebuildSoundPlaybackChain(const QAudioDevice &device) {
    if (player_) {
        player_->stop();
        player_->setAudioOutput(nullptr);
        delete player_;
        player_ = nullptr;
    }

    if (audioOutput_) {
        delete audioOutput_;
        audioOutput_ = nullptr;
    }

    player_ = new QMediaPlayer(this);
    audioOutput_ = new QAudioOutput(this);
    audioOutput_->setDevice(device);
    player_->setAudioOutput(audioOutput_);
    player_->setSource(soundSource_);
}

void CopySoundPlayer::syncSoundOutputDevice() {
    const QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();
    if (player_ && audioOutput_
        && audioOutput_->device().id() == defaultDevice.id()) {
        return;
    }

    rebuildSoundPlaybackChain(defaultDevice);
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

    syncSoundOutputDevice();
    if (player_->mediaStatus() == QMediaPlayer::EndOfMedia) {
        player_->setPosition(0);
    }
    if (player_->playbackState() == QMediaPlayer::PlayingState) {
        player_->stop();
    }
    player_->play();
    lastSoundPlayAtMs_ = now;
    qInfo().noquote() << QStringLiteral("[clipboard-widget] play copy sound wId=%1 fp=%2")
        .arg(wId)
        .arg(fingerprint.isEmpty() ? QStringLiteral("-") : QString::fromLatin1(fingerprint.toHex().left(12)));
}
