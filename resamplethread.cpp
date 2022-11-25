#include "resamplethread.h"
#include <QDebug>
#include <QFile>

extern "C" {
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

#ifdef Q_OS_MAC
#define IN_FILE "/Users/caitou/Desktop/in.pcm"
#define OUT_FILE "/Users/caitou/Desktop/out.pcm"
#else
#define IN_FILE "C:\\Workspaces\\in.pcm"
#define OUT_FILE "C:\\Workspaces\\out.pcm"
#endif

#define ERROR_BUF(res) \
    char errbuf[1024]; \
    av_strerror(res, errbuf, sizeof(errbuf)); \

ResampleThread::ResampleThread(QObject *parent) : QThread(parent) {
    connect(this, &QThread::finished, this, &QThread::deleteLater);
}

ResampleThread::~ResampleThread() {
    disconnect();
    requestInterruption();
    quit();
    wait();

    qDebug() << "ResampleThread::~ResampleThread()";
}

void ResampleThread::run() {

   AVChannelLayout out_channel_layout = AV_CHANNEL_LAYOUT_STEREO;
   AVSampleFormat out_fmt = AV_SAMPLE_FMT_S16;
   int out_sample_rate = 48000;
   int out_bytesPerSample = av_get_bytes_per_sample(out_fmt) * out_channel_layout.nb_channels;

   AVChannelLayout in_channel_layout = AV_CHANNEL_LAYOUT_STEREO;
   AVSampleFormat in_fmt = AV_SAMPLE_FMT_FLT;
   int in_sample_rate = 44100;
   int in_bytesPerSample = av_get_bytes_per_sample(in_fmt) * in_channel_layout.nb_channels;

   SwrContext *swr_ctx = nullptr;
   int res = swr_alloc_set_opts2(&swr_ctx,
                                 &out_channel_layout,
                                 out_fmt,
                                 out_sample_rate,
                                 &in_channel_layout,
                                 in_fmt,
                                 in_sample_rate, 0, nullptr);
    if (res != 0) {
        ERROR_BUF(res);
        qDebug() << "swr_alloc_set_opt2 failure errbuf: " << errbuf;
        return;
    }

    swr_init(swr_ctx);

    QFile in(IN_FILE);
    QFile out(OUT_FILE);

    res = in.open(QFile::ReadOnly);
    if (res == 0) {
       qDebug() << "open file" << IN_FILE << "error code" << res;
       swr_free(&swr_ctx);
       return;
    }

    res = out.open(QFile::WriteOnly);
    if (res == 0) {
       qDebug() << "open file" << OUT_FILE << "error code" << res;
       in.close();
       swr_free(&swr_ctx);
       return;
    }

    uint8_t **in_audio_data = nullptr;
    /// 输入缓冲区的实际大小（字节数）
    int in_linesize = 0;

    uint8_t **out_audio_data = nullptr;
    /// 输出缓冲区的实际大小（字节数）
    int out_linesize = 0;

    int in_samples_nb = 1024;
     /// 根据输入的字节数来计算采样后的字节数
    int out_samples_nb_ = in_samples_nb * out_sample_rate / in_sample_rate + 1;
    int out_samples_nb = av_rescale_rnd(swr_get_delay(swr_ctx, in_sample_rate) +
                                            in_samples_nb, out_sample_rate, in_sample_rate, AV_ROUND_UP);
    qDebug() << "out_samples_nb_: " << out_samples_nb_ << "," << "out_samples_nb: " << out_samples_nb;

    int notAlign = 1;
    res = av_samples_alloc_array_and_samples(&in_audio_data,
                                             &in_linesize,
                                             in_channel_layout.nb_channels,
                                             in_samples_nb,
                                             in_fmt, notAlign);
    qDebug() << "in_audio linesize: " << in_linesize;

    if (res < 0) {
        ERROR_BUF(res);
        qDebug() << "av_samples_alloc_array_and_samples in_audio error:" << errbuf;
        out.close();
        in.close();
        swr_free(&swr_ctx);
        return;
    }
    res = av_samples_alloc_array_and_samples(&out_audio_data,
                                             &out_linesize,
                                             out_channel_layout.nb_channels,
                                             out_samples_nb,
                                             out_fmt, notAlign);
    qDebug() << "out_audio linesize: " << out_linesize;

    if (res < 0) {
        ERROR_BUF(res);
        qDebug() << "av_samples_alloc_array_and_samples out_audio error:" << errbuf;
        out.close();
        in.close();
        swr_free(&swr_ctx);
        return;
    }

    while ((res = in.read((char *)in_audio_data[0], in_linesize)) > 0) {
        // 实际读取的样本数量
        in_samples_nb = res / in_bytesPerSample;

        /// 重采样(返回值转换后的样本数量)
        res = swr_convert(swr_ctx, out_audio_data, out_samples_nb, (const uint8_t **)in_audio_data, in_samples_nb);
        qDebug() << "swr_convert samples nb: " << res;

        if (res < 0) {
            ERROR_BUF(res);
            qDebug() << "swr_convert error errbuf: " << errbuf;
            break;
        }
        int out_linesize = res * out_bytesPerSample;
        qDebug() << "out_linesize" << out_linesize;
        out.write((char *)out_audio_data[0], out_linesize);
    }

    // in and in_count can be set to 0 to flush the last few samples out at the end
    while ((res = swr_convert(swr_ctx, out_audio_data, out_samples_nb, nullptr, 0)) > 0) {
        qDebug() << "flush the last few samples: " << res;
        int out_linesize = res * out_bytesPerSample;
        out.write((char *)out_audio_data[0], out_linesize);
    }

    out.close();
    in.close();
    if (out_audio_data) {
        av_freep(&out_audio_data[0]);
        av_freep(&out_audio_data);
    }
    if (in_audio_data) {
        av_freep(&in_audio_data[0]);
        av_freep(&in_audio_data);
    }
    swr_free(&swr_ctx);
}
