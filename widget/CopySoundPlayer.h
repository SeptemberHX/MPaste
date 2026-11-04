// input: Depends on MPasteSettings and the system's own sound playback.
// output: Provides a copy-sound player with burst suppression.
// pos: Widget-layer helper owned by ClipboardAppController.
// update: If I change, update this header block.
#ifndef COPYSOUNDPLAYER_H
#define COPYSOUNDPLAYER_H

#include <QByteArray>
#include <QObject>
#include <QString>

// Plays the copy sound, a short WAV, through the system instead of Qt
// Multimedia, which would load a media framework (FFmpeg and what it probes
// for) for 0.1 s of sound: on Windows PlaySound from memory, on Linux and
// other systems a short-lived pw-play or paplay on a copy of it in the cache.
class CopySoundPlayer : public QObject {
    Q_OBJECT

public:
    explicit CopySoundPlayer(const QString &resourcePath, QObject *parent = nullptr);
    ~CopySoundPlayer() override;

    /// Play the copy sound unless the setting is disabled or burst-suppressed.
    void playCopySoundIfNeeded(int wId, const QByteArray &fingerprint = QByteArray());

private:
    bool play();

    QString resourcePath_;
    QByteArray sound_;
#ifndef Q_OS_WIN
    QString soundFile_;
    QString playerProgram_;
#endif
    qint64 lastSoundPlayAtMs_ = 0;

    static constexpr qint64 SOUND_BURST_WINDOW_MS = 500;
};

#endif // COPYSOUNDPLAYER_H
