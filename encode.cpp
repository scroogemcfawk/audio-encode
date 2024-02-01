#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavutil/channel_layout.h>
    #include <libavutil/common.h>
    #include <libavutil/frame.h>
    #include <libavutil/samplefmt.h>
}

#include <stdfloat>
#include <iostream>

typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t i32;
typedef int64_t i64;

// select layout with the highest channel count
static int select_channel_layout(const AVCodec *codec)
{
    const u64 *p;
    u64 best_ch_layout = 0;
    int best_nb_channels   = 0;
    if (!codec->channel_layouts)
        // probably enough for this case
        return AV_CH_LAYOUT_STEREO;
    p = codec->channel_layouts;
    while (*p) {
        // thrying 5.1+ stereo
        int nb_channels = av_get_channel_layout_nb_channels(*p);
        if (nb_channels > best_nb_channels) {
            best_ch_layout    = *p;
            best_nb_channels = nb_channels;
        }
        p++;
    }
    return best_ch_layout;
}

// encode frame into packet and write packet to file
static void encode(AVCodecContext *ctx, AVFrame *frame, AVPacket *packet, FILE *fout)
{
    int ret;
    /* send the frame for encoding */
    ret = avcodec_send_frame(ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending the frame to the encoder\n");
        exit(1);
    }
    /* read all the available output packets (in general there may be any
     * number of them */
    while (ret >= 0) {
        ret = avcodec_receive_packet(ctx, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error encoding audio frame\n");
            exit(1);
        }
        fwrite(packet->data, 1, packet->size, fout);
        av_packet_unref(packet);
    }
}

const AVCodec* setup_codec() {
    auto codec = avcodec_find_encoder(AV_CODEC_ID_MP2); // does not work with OPUS
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }
    return codec;
}

AVCodecContext* setup_context(const AVCodec* codec) {
    auto context = avcodec_alloc_context3(codec);
    if (!context) {
        fprintf(stderr, "Could not allocate audio codec context\n");
        exit(1);
    }

    context->bit_rate = 96000; // default option - good quality
    context->sample_fmt = AV_SAMPLE_FMT_S16; // supported by opus
    context->sample_rate = 48000; // required for fullband

    // default
    context->channel_layout = select_channel_layout(codec);
    context->channels = av_get_channel_layout_nb_channels(context->channel_layout);

    return context;
}

AVFrame* setup_frame(AVCodecContext *context) {
    auto frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate audio frame\n");
        exit(1);
    }

    frame->nb_samples = context->frame_size;
    frame->format = context->sample_fmt;
    frame->channel_layout = context->channel_layout;

    if (av_frame_get_buffer(frame, 0) < 0) {
        fprintf(stderr, "Could not allocate audio data buffers\n");
        exit(1);
    }

    return frame;
}

int main(int argc, char **argv)
{
    if (argc <= 1) {
        std::cerr << "Usage: " << argv[0] <<" <output file>" << std::endl;
        exit(0);
    }

    const char *filename = argv[1];

    const AVCodec *codec = setup_codec();

    AVCodecContext *context = setup_context(codec);

    if (avcodec_open2(context, codec, NULL) < 0) {
        std::cerr << "Could not open codec" << std::endl;
        exit(1);
    }

    FILE *fout = fopen(filename, "wb");
    if (!fout) {
        std::cerr << "Could not open " << filename << std::endl;
        exit(1);
    }
    
    AVPacket *packet = av_packet_alloc();
    if (!packet) {
        std::cerr << "Could not allocate the packet" << std::endl;
        exit(1);
    }

    /* frame containing input raw audio */
    AVFrame *frame = setup_frame(context);
    
    u16 *samples;

    // generate tone ramp
    float t = 0;
    float tincr = 2 * M_PI * 440.0 / context->sample_rate;
    double audio_duration = 1.000; // s.ms
    for (i32 i = 0; i < context->sample_rate / 1000 * audio_duration; i++) {
        /* make sure the frame is writable -- makes a copy if the encoder
         * kept a reference internally */
        i32 ret = av_frame_make_writable(frame);
        if (ret < 0)
            exit(1);
        samples = (u16*)frame->data[0];
        for (i32 j = 0; j < context->frame_size; j++) {
            samples[2*j] = (int)(sin(t + ((t * t) / (440.0 * M_PI * audio_duration))) * 10000);
            for (i32 k = 1; k < context->channels; k++)
                samples[2*j + k] = samples[2*j];
            t += tincr;
        }
        encode(context, frame, packet, fout);
    }
    /* flush the encoder */
    encode(context, NULL, packet, fout);
    fclose(fout);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&context);
    return 0;
}