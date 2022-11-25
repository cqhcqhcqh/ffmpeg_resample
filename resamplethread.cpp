#include "resamplethread.h"
#include <QDebug>
#include <QFile>

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
    ResampleAudioSpec outParam;
    outParam.file = OUT_FILE;
    outParam.channel_layout = AV_CHANNEL_LAYOUT_STEREO;
    outParam.fmt = AV_SAMPLE_FMT_S16;
    outParam.sample_rate = 48000;
    outParam.bytesPerSample = av_get_bytes_per_sample(outParam.fmt) * outParam.channel_layout.nb_channels;

    ResampleAudioSpec inParam;
    inParam.file = IN_FILE;
    inParam.channel_layout = AV_CHANNEL_LAYOUT_STEREO;
    inParam.fmt = AV_SAMPLE_FMT_FLT;
    inParam.sample_rate = 41000;
    inParam.bytesPerSample = av_get_bytes_per_sample(inParam.fmt) * inParam.channel_layout.nb_channels;

    SwrContext *swr_ctx = nullptr;
    int res = swr_alloc_set_opts2(&swr_ctx,
                                  &outParam.channel_layout,
                                  outParam.fmt,
                                  outParam.sample_rate,
                                  &inParam.channel_layout,
                                  inParam.fmt,
                                  inParam.sample_rate, 0, nullptr);
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
    /// 重采样的本质是在单位时间内根据导出采样参数对单位时间内的原所有样本
    /// 重采样前后 pcm 的总时长是不会变的
    ///                样本数量是会发生变化的（采样率会影响样本数量，位深和通道数会影响前后 pcm 的大小）
    /// 所以 out_samples_nb 要根据 in_samples_nb 来计算
    /// 因为采样率的不同，如果要保证总时长不变的话，就需要保证这个公式相等 out_samples_nb / out_sample_rate = in_samples_nb / in_sample_rate;
    /// 计算出来的 out_samples_nb 就是 swr_convert 函数中需要指定的 out_count 大小
    int out_samples_nb_ = in_samples_nb * outParam.sample_rate / inParam.sample_rate + 1;
    // a * b / c
    int out_samples_nb = av_rescale_rnd(swr_get_delay(swr_ctx, inParam.sample_rate) +
                                            in_samples_nb, outParam.sample_rate, inParam.sample_rate, AV_ROUND_UP);
    qDebug() << "in_sample_rate" << inParam.sample_rate
             << "in_samples_nb: " << in_samples_nb;

    qDebug() << "out_sample_rate" << outParam.sample_rate
             << "out_samples_nb_: " << out_samples_nb_
             << "," << "out_samples_nb: " << out_samples_nb;
    // out_samples_nb_ 计算出来的值可能有问题 in_samples_nb * out_sample_rate 可能会溢出，而且 rounding 的方式也需要自己实现
    // but writing that directly can overflow, and does not support different rounding methods.
    // 使用 ffmpeg 的 `av_rescale_rnd` API 内部会有处理很多这种类似的问题

    int notAlign = 1;
    res = av_samples_alloc_array_and_samples(&in_audio_data,
                                             &in_linesize,
                                             inParam.channel_layout.nb_channels,
                                             in_samples_nb,
                                             inParam.fmt, notAlign);
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
                                             outParam.channel_layout.nb_channels,
                                             out_samples_nb,
                                             outParam.fmt, notAlign);
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
        in_samples_nb = res / inParam.bytesPerSample;

        /// 重采样(返回值转换后的样本数量)
        res = swr_convert(swr_ctx, out_audio_data, out_samples_nb, (const uint8_t **)in_audio_data, in_samples_nb);
        qDebug() << "swr_convert samples nb: " << res;

        /// number of samples output per channel, negative value on error
        /// 只有当 res 是负数的时候，才代表出错，res 为 0 的时候，可能代表某次重采样出来的样本数量是 0，并不代表错误
        if (res < 0) {
            ERROR_BUF(res);
            qDebug() << "swr_convert error errbuf: " << errbuf;
            break;
        }
        int out_linesize = res * outParam.bytesPerSample;
        /// 使用 `av_samples_get_buffer_size` 来计算（通道数，采样格式，是否字节对齐）多个样本对应的字节数
        /// 这个 API 是兼容 planar，有一些 AVSampleFormat 是通过 planar 来存储的（`av_sample_fmt_is_planar`）
        int out_linesize_buffer_size = av_samples_get_buffer_size(&out_linesize, outParam.channel_layout.nb_channels, res, outParam.fmt, notAlign);
        qDebug() << "out_linesize" << out_linesize << ", "
                 << "out_linesize_buffer_size" << out_linesize_buffer_size;
        out.write((char *)out_audio_data[0], out_linesize);
    }

    // in and in_count can be set to 0 to flush the last few samples out at the end
    while ((res = swr_convert(swr_ctx, out_audio_data, out_samples_nb, nullptr, 0)) > 0) {
        qDebug() << "flush the last few samples: " << res;
        int out_linesize = res * outParam.bytesPerSample;
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
