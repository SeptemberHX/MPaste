// input: Depends on Qt Multimedia classes for audio playback.
// output: Provides a self-contained copy-sound player that monitors audio device changes.
// pos: Widget-layer helper owned by MPasteWidget.
// update: If I change, update this header block.
#ifndef COPYSOUNDPLAYER_H
#define COPYSOUNDPLAYER_H

#include <QObject>
#include <QAudioDevice>
#include <QAudioOutput>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QDateTime>
#include <QUrl>

class CopySoundPlayer : public QObject {
    Q_OBJECT

public:
    explicit CopySoundPlayer(bool soundEnabled, const QUrl &soundSource, QObject *parent = nullptr);
    ~CopySoundPlayer() override = default;

    /// Play the copy sound unless the setting is disabled or burst-suppressed.
    void playCopySoundIfNeeded(int wId, const QByteArray &fingerprint = QByteArray());

private:
    void rebuildSoundPlaybackChain(const QAudioDevice &device);
    void syncSoundOutputDevice();

    QMediaPlayer *player_ = nullptr;
    QAudioOutput *audioOutput_ = nullptr;
    QMediaDevices *mediaDevices_ = nullptr;
    QUrl soundSource_;
    qint64 lastSoundPlayAtMs_ = 0;

    static constexpr qint64 SOUND_BURST_WINDOW_MS = 500;
};

#endif // COPYSOUNDPLAYER_H
