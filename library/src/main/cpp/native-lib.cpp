#include <jni.h>
#include <string>
#include <android/log.h>
#include <unistd.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

const char *TAG = __FILE__;

jobject gCallback = NULL;
jmethodID gCallbackMethodId;
bool isStop = false;

void callback(JNIEnv *env, uint8_t *buf, int size, int width, int height);

extern "C"
jint
Java_com_potterhsu_rtsplibrary_RtspClient_play(
        JNIEnv *env,
        jobject,
        jstring endpoint,
        jobject cb) {
    isStop = true;
    while (NULL != gCallback) {
        sleep(1);
    }
    isStop = false;
    gCallback = env->NewGlobalRef(cb);
    jclass clz = env->GetObjectClass(gCallback);
    gCallbackMethodId = env->GetMethodID(clz, "onFrame", "([BIII)V");

    int video_stream_index = -1;
    SwsContext *img_convert_ctx;
    AVCodecContext* ccontext = avcodec_alloc_context3(NULL);
    AVFormatContext* context = avformat_alloc_context();
    AVFormatContext *oc = avformat_alloc_context();

    av_register_all();
    avformat_network_init();

    AVDictionary *option = NULL;
    av_dict_set(&option, "rtsp_transport", "tcp", 0);
    av_dict_set(&option, "stimeout", "5000000", 0);

    // Open RTSP
    const char *rtspUrl= env->GetStringUTFChars(endpoint, JNI_FALSE);
    env->ReleaseStringUTFChars(endpoint, rtspUrl);

    do {
        if (int err = avformat_open_input(&context, rtspUrl, NULL, &option) != 0) {
            __android_log_print(ANDROID_LOG_ERROR, TAG, "Cannot open input %s, error code: %d",
                                rtspUrl, err);
            break;
        }

        if (avformat_find_stream_info(context, NULL) < 0) {
            __android_log_print(ANDROID_LOG_ERROR, TAG, "Cannot find stream info");
            break;
        }

        // Search video stream
        for (int i = 0; i < context->nb_streams; i++) {
            if (context->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
                video_stream_index = i;
        }

        if (video_stream_index == -1) {
            __android_log_print(ANDROID_LOG_ERROR, TAG, "Video stream not found");
            break;
        }

        // Start reading packets from stream and write them to file
        av_read_play(context);

        AVCodec *codec = NULL;
        codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        if (!codec) {
            __android_log_print(ANDROID_LOG_ERROR, TAG, "Cannot find decoder H264");
            break;
        }

        avcodec_get_context_defaults3(ccontext, codec);
        avcodec_copy_context(ccontext, context->streams[video_stream_index]->codec);

        if (avcodec_open2(ccontext, codec, NULL) < 0) {
            __android_log_print(ANDROID_LOG_ERROR, TAG, "Cannot open codec");
            break;
        }

        img_convert_ctx = sws_getContext(ccontext->width, ccontext->height, ccontext->pix_fmt,
                                         ccontext->width, ccontext->height,
                                         AV_PIX_FMT_NV21, SWS_BICUBIC, NULL, NULL, NULL);

        size_t size = (size_t) avpicture_get_size(AV_PIX_FMT_YUV420P, ccontext->width,
                                                  ccontext->height);
        uint8_t *picture_buf = (uint8_t *) (av_malloc(size));
        AVFrame *pic = av_frame_alloc();
        avpicture_fill((AVPicture *) pic, picture_buf, AV_PIX_FMT_YUV420P, ccontext->width,
                       ccontext->height);

        size_t size2 = (size_t) avpicture_get_size(AV_PIX_FMT_NV21, ccontext->width,
                                                   ccontext->height);
        uint8_t *picture_buf2 = (uint8_t *) (av_malloc(size2));
        AVFrame *pic2 = av_frame_alloc();
        avpicture_fill((AVPicture *) pic2, picture_buf2, AV_PIX_FMT_NV21, ccontext->width,
                       ccontext->height);

        AVPacket packet;
        av_init_packet(&packet);
        AVStream *stream = NULL;
        while (!isStop && av_read_frame(context, &packet) >= 0) {
            if (packet.stream_index == video_stream_index) { // Packet is video
                if (stream == NULL) {
                    stream = avformat_new_stream(oc,
                                                 context->streams[video_stream_index]->codec->codec);
                    avcodec_copy_context(stream->codec,
                                         context->streams[video_stream_index]->codec);
                    stream->sample_aspect_ratio = context->streams[video_stream_index]->codec->sample_aspect_ratio;
                }

                int check = 0;
                packet.stream_index = stream->id;
                avcodec_decode_video2(ccontext, pic, &check, &packet);
                sws_scale(img_convert_ctx, (const uint8_t *const *) pic->data, pic->linesize, 0,
                          ccontext->height, pic2->data, pic2->linesize);

                callback(env, picture_buf2, size2, ccontext->width, ccontext->height);
            }
        }
        av_free_packet(&packet);

        av_free(pic);
        av_free(pic2);
        av_free(picture_buf);
        av_free(picture_buf2);

        av_read_pause(context);
    } while (0);

    av_dict_free(&option);

    avio_close(oc->pb);
    avformat_free_context(oc);
    avformat_close_input(&context);
    avcodec_close(ccontext);
    avcodec_free_context(&ccontext);

    jint result = isStop ? JNI_OK : JNI_ERR;

    env->DeleteGlobalRef(gCallback);
    gCallback = NULL;

    return result;
}

extern "C"
void
Java_com_potterhsu_rtsplibrary_RtspClient_stop(
        JNIEnv *env,
        jobject) {
    isStop = true;
}

void callback(JNIEnv *env, uint8_t *buf, int size, int width, int height) {
    jbyteArray gByteArray = env->NewByteArray(size);
    env->SetByteArrayRegion(gByteArray, 0, size, (jbyte *) buf);
    env->CallVoidMethod(gCallback, gCallbackMethodId, gByteArray, size, width, height);
    env->DeleteLocalRef(gByteArray);
}
