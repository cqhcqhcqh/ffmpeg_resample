#ifndef RESAMPLETHREAD_H
#define RESAMPLETHREAD_H

#include <QThread>
extern "C" {
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

typedef struct {
    AVChannelLayout channel_layout;
    AVSampleFormat fmt;
    int bytesPerSample;
    int sample_rate = 0;
    const char *file;
} ResampleAudioSpec;

class ResampleThread : public QThread
{
    Q_OBJECT
private:
    void run() override;
public:
    ResampleThread(QObject *parent);
    ~ResampleThread();
signals:

};

#endif // RESAMPLETHREAD_H
