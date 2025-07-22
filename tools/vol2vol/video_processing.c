/** @file video_processing.c
 * Video and Audio Processing Functions for vol2vol
 *
 * Implementation of video and audio processing functions.
 *
 * Authors: Jan Ondřej <jan@volograms.com>
 * Copyright: 2025, Volograms (https://volograms.com/)
 * Language: C99
 * License: The MIT License
 */

#include "video_processing.h"

// FFmpeg includes for audio and video processing
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/mem.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/rational.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define LIBAVUTIL_VERSION_CHECK(maj, min, mic) (((LIBAVUTIL_VERSION_MAJOR >= maj) && (LIBAVUTIL_VERSION_MINOR >= min) && (LIBAVUTIL_VERSION_MICRO >= mic))? 1 : 0)


// External logging function from main.c
extern void _printlog(_log_type log_type, const char* message_str, ...);

// Internal logging macro
#define LOG(type, ...) _printlog(_LOG_TYPE_##type, __VA_ARGS__)

/**
 * Custom I/O context for reading from memory buffer
 */
typedef struct {
    uint8_t* data;
    size_t size;
    size_t pos;
} memory_buffer_t;

/**
 * Custom read function for FFmpeg AVIO context
 */
static int read_memory_buffer(void* opaque, uint8_t* buf, int buf_size) {
    memory_buffer_t* mem_buf = (memory_buffer_t*)opaque;
    if (mem_buf->pos >= mem_buf->size) {
        return AVERROR_EOF;
    }
    
    size_t bytes_to_read = buf_size;
    if (mem_buf->pos + bytes_to_read > mem_buf->size) {
        bytes_to_read = mem_buf->size - mem_buf->pos;
    }
    
    memcpy(buf, mem_buf->data + mem_buf->pos, bytes_to_read);
    mem_buf->pos += bytes_to_read;
    
    return (int)bytes_to_read;
}

/**
 * Custom seek function for FFmpeg AVIO context
 */
static int64_t seek_memory_buffer(void* opaque, int64_t offset, int whence) {
    memory_buffer_t* mem_buf = (memory_buffer_t*)opaque;
    
    int64_t new_pos = 0;
    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = mem_buf->pos + offset;
            break;
        case SEEK_END:
            new_pos = mem_buf->size + offset;
            break;
        case AVSEEK_SIZE:
            return mem_buf->size;
        default:
            return AVERROR(EINVAL);
    }
    
    if (new_pos < 0 || new_pos > (int64_t)mem_buf->size) {
        return AVERROR(EINVAL);
    }
    
    mem_buf->pos = (size_t)new_pos;
    return new_pos;
}

/**
 * Custom write function for output buffer
 */
typedef struct {
    uint8_t* data;
    size_t size;
    size_t capacity;
} output_buffer_t;

static int write_output_buffer(void* opaque, uint8_t* buf, int buf_size) {
    output_buffer_t* out_buf = (output_buffer_t*)opaque;
    
    // Expand buffer if needed
    while (out_buf->size + buf_size > out_buf->capacity) {
        out_buf->capacity = out_buf->capacity ? out_buf->capacity * 2 : 4096;
        out_buf->data = realloc(out_buf->data, out_buf->capacity);
        if (!out_buf->data) {
            return AVERROR(ENOMEM);
        }
    }
    
    memcpy(out_buf->data + out_buf->size, buf, buf_size);
    out_buf->size += buf_size;
    
    return buf_size;
}

static bool open_input(const char* filename, const uint8_t* buffer, size_t buffer_size, AVFormatContext** fmt_ctx) {
    *fmt_ctx = avformat_alloc_context();
    if (!*fmt_ctx) {
        LOG(ERROR, "ERROR: Could not allocate format context\n");
        return false;
    }

    if (buffer) {
        memory_buffer_t* mem_buf = av_malloc(sizeof(memory_buffer_t));
        mem_buf->data = (uint8_t*)buffer;
        mem_buf->size = buffer_size;
        mem_buf->pos = 0;

        size_t avio_buffer_size = 4096;
        uint8_t* avio_buffer = av_malloc(avio_buffer_size);
        AVIOContext* avio_ctx = avio_alloc_context(avio_buffer, (int)avio_buffer_size, 0, mem_buf, read_memory_buffer, NULL, seek_memory_buffer);
        if (!avio_ctx) {
            av_free(avio_buffer);
            av_free(mem_buf);
            LOG(ERROR, "ERROR: Failed to create AVIO context\n");
            return false;
        }
        (*fmt_ctx)->pb = avio_ctx;
    }

    if (avformat_open_input(fmt_ctx, filename, NULL, NULL) < 0) {
        LOG(ERROR, "ERROR: Failed to open input\n");
        if (buffer && (*fmt_ctx)->pb) {
            av_freep(&(*fmt_ctx)->pb->buffer);
            avio_context_free(&(*fmt_ctx)->pb);
        }
        avformat_free_context(*fmt_ctx);
        *fmt_ctx = NULL;
        return false;
    }

    if (avformat_find_stream_info(*fmt_ctx, NULL) < 0) {
        LOG(ERROR, "ERROR: Failed to find stream info\n");
        avformat_close_input(fmt_ctx);
        *fmt_ctx = NULL;
        return false;
    }
    return true;
}


typedef struct {
    AVCodecContext* dec_ctx;
    AVCodecContext* enc_ctx;
    int64_t last_pts;
    int64_t last_dts;
    struct SwsContext *sws_ctx;
    AVFrame *tmp_frame;
} StreamContext;

static void cleanup(AVFormatContext* ifmt_ctx, AVFormatContext* ofmt_ctx, StreamContext* stream_ctxs) {
    if (stream_ctxs) {
        for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
            if (stream_ctxs[i].dec_ctx) {
                avcodec_free_context(&stream_ctxs[i].dec_ctx);
            }
            if (stream_ctxs[i].enc_ctx) {
                avcodec_free_context(&stream_ctxs[i].enc_ctx);
            }
            if(stream_ctxs[i].sws_ctx) {
                sws_freeContext(stream_ctxs[i].sws_ctx);
            }
            if(stream_ctxs[i].tmp_frame) {
                av_frame_free(&stream_ctxs[i].tmp_frame);
            }
        }
        av_free(stream_ctxs);
    }
    if (ifmt_ctx) {
        avformat_close_input(&ifmt_ctx);
    }
    if (ofmt_ctx) {
        if (ofmt_ctx->pb) {
            avio_closep(&ofmt_ctx->pb);
        }
        avformat_free_context(ofmt_ctx);
    }
}


bool process_video_file(const char* input_video_filename, const char* output_video_filename,
                       float fps, int start_frame, int end_frame) {
    if (!input_video_filename || !output_video_filename || fps <= 0 || start_frame < 0 || end_frame < start_frame) {
        return false;
    }
    
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    StreamContext* stream_ctxs = NULL;
    bool success = false;

    if (!open_input(input_video_filename, NULL, 0, &ifmt_ctx)) {
        return false;
    }

    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, output_video_filename);
    if (!ofmt_ctx) {
        LOG(ERROR, "ERROR: Could not create output context\n");
        cleanup(ifmt_ctx, NULL, NULL);
        return false;
    }

    stream_ctxs = av_calloc(ifmt_ctx->nb_streams, sizeof(StreamContext));
    if (!stream_ctxs) {
        LOG(ERROR, "ERROR: Failed to allocate stream contexts\n");
        cleanup(ifmt_ctx, ofmt_ctx, NULL);
        return false;
    }

    int* stream_mapping = av_mallocz_array(ifmt_ctx->nb_streams, sizeof(int));
    if (!stream_mapping) {
        LOG(ERROR, "ERROR: Failed to allocate stream mapping\n");
        cleanup(ifmt_ctx, ofmt_ctx, stream_ctxs);
        return false;
    }

    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
        stream_mapping[i] = -1;
        AVStream* in_stream = ifmt_ctx->streams[i];
        AVCodecParameters* in_codecpar = in_stream->codecpar;

        if (in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO && in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
            continue;
        }

        AVCodec* dec = avcodec_find_decoder(in_codecpar->codec_id);
        if (!dec) {
            LOG(ERROR, "ERROR: Failed to find decoder for stream %d\n", i);
            goto end;
        }
        AVCodecContext* dec_ctx = avcodec_alloc_context3(dec);
        avcodec_parameters_to_context(dec_ctx, in_codecpar);
        avcodec_open2(dec_ctx, dec, NULL);
        stream_ctxs[i].dec_ctx = dec_ctx;

        const AVCodec* enc = NULL;
        if (in_codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            const char* preferred_encoders[] = {
                "h264_qsv",
                "h264_nvenc",
                "h264_amf",
                "h264_videotoolbox",
                "libx264",
                "libopenh264",
                "h264_mf",
                NULL
            };

            for (int j = 0; preferred_encoders[j] != NULL; j++) {
                LOG(INFO, "Attempting to find video encoder '%s'...\n", preferred_encoders[j]);
                enc = avcodec_find_encoder_by_name(preferred_encoders[j]);
                if (enc) {
                    LOG(INFO, "Found supported encoder: '%s'\n", preferred_encoders[j]);
                    break;
                }
            }

            if (!enc) {
                LOG(ERROR, "Could not find any suitable H.264 encoder.\n");
                 // Debug: List all available encoders
                LOG(DEBUG, "--- AVAILABLE ENCODERS ---\n");
                const AVCodec* codec;
                void *iter = NULL;
                while ((codec = av_codec_iterate(&iter))) {
                    if (av_codec_is_encoder(codec)) {
                        LOG(DEBUG, "  - %s (%s)\n", codec->name, codec->long_name);
                    }
                }
                LOG(DEBUG, "--------------------------\n");
                goto end;
            }
        } else {
            LOG(INFO, "Attempting to find audio encoder 'aac'...\n");
            enc = avcodec_find_encoder_by_name("aac");
        }

        if (!enc) {
            LOG(ERROR, "ERROR: Failed to find encoder for stream %d (%s)\n", i, av_get_media_type_string(in_codecpar->codec_type));
            goto end;
        }

        AVStream* out_stream = avformat_new_stream(ofmt_ctx, enc);
        if (!out_stream) {
            LOG(ERROR, "ERROR: Failed to create new stream for stream %d\n", i);
            goto end;
        }
        stream_mapping[i] = out_stream->index;
        
        AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
        if (in_codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            AVRational framerate = av_d2q(fps, 60000);
            enc_ctx->height = dec_ctx->height;
            enc_ctx->width = dec_ctx->width;
            enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
            enc_ctx->pix_fmt = dec_ctx->pix_fmt;
            enc_ctx->time_base = av_inv_q(framerate);
            enc_ctx->framerate = framerate;
            
            // For hardware encoders, we must often convert to the nv12 pixel format.
            // Querying them for supported formats can cause crashes, so we force it.
            if (strcmp(enc->name, "h264_qsv") == 0 ||
                strcmp(enc->name, "h264_amf") == 0) {
                
                LOG(INFO, "Hardware encoder '%s' selected. Forcing conversion to nv12.\n", enc->name);
                stream_ctxs[i].sws_ctx = sws_getContext(dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
                                                        dec_ctx->width, dec_ctx->height, AV_PIX_FMT_NV12,
                                                        SWS_BILINEAR, NULL, NULL, NULL);
                if (!stream_ctxs[i].sws_ctx) {
                    LOG(ERROR, "Could not create SwsContext for pixel format conversion.\n");
                    goto end;
                }
                enc_ctx->pix_fmt = AV_PIX_FMT_NV12;

                stream_ctxs[i].tmp_frame = av_frame_alloc();
                if (!stream_ctxs[i].tmp_frame) {
                    LOG(ERROR, "Could not allocate temporary frame for conversion.\n");
                    goto end;
                }
                stream_ctxs[i].tmp_frame->format = AV_PIX_FMT_NV12;
                stream_ctxs[i].tmp_frame->width = dec_ctx->width;
                stream_ctxs[i].tmp_frame->height = dec_ctx->height;
                if (av_frame_get_buffer(stream_ctxs[i].tmp_frame, 32) < 0) {
                    LOG(ERROR, "Could not allocate buffer for temporary frame.\n");
                    goto end;
                }
            }
            
            av_opt_set(enc_ctx->priv_data, "preset", "slow", 0);
            if (strcmp(enc->name, "h264_nvenc") == 0) {
                av_opt_set(enc_ctx->priv_data, "cq", "23", 0);
                LOG(INFO, "Configuring h264_nvenc with cq=23 and preset=slow.\n");
            } else if (strcmp(enc->name, "h264_qsv") == 0) {
                // For QSV, global_quality is a more direct way to set quality.
                // It is a global quality factor, where lower is better. 25 is a good balance.
                enc_ctx->global_quality = 30; 
                av_opt_set(enc_ctx->priv_data, "look_ahead", "1", 0);
                LOG(INFO, "Configuring h264_qsv with global_quality=%d.\n", enc_ctx->global_quality);
            } else if (strcmp(enc->name, "libx264") == 0) {
                 av_opt_set(enc_ctx->priv_data, "crf", "19", 0);
                 LOG(INFO, "Configuring libx264 with crf=19 and preset=slow.\n");
            } else {
                // Fallback for other encoders (amf, videotoolbox, libopenh264, mf)
                long long target_bitrate = 20000000; // 20 Mbps fallback
                if (in_codecpar->bit_rate > target_bitrate) {
                    target_bitrate = in_codecpar->bit_rate;
                }
                enc_ctx->bit_rate = target_bitrate;
                LOG(INFO, "Configuring %s with target bitrate: %lld bps\n", enc->name, enc_ctx->bit_rate);
            }

            if (dec_ctx->gop_size > 0) {
                enc_ctx->gop_size = dec_ctx->gop_size;
                LOG(INFO, "Using original GOP size: %d\n", dec_ctx->gop_size);
            } else {
                enc_ctx->gop_size = (int)(fps + 0.5);
                LOG(INFO, "Original GOP size not available, calculating from FPS: %d\n", enc_ctx->gop_size);
            }
        } else {
            enc_ctx->sample_rate = dec_ctx->sample_rate;
            enc_ctx->channel_layout = dec_ctx->channel_layout;
            enc_ctx->channels = av_get_channel_layout_nb_channels(enc_ctx->channel_layout);
            enc_ctx->sample_fmt = enc->sample_fmts[0];
            enc_ctx->time_base = (AVRational){1, enc_ctx->sample_rate};
            if (in_codecpar->bit_rate > 0) {
                enc_ctx->bit_rate = in_codecpar->bit_rate;
                LOG(INFO, "Using original audio bitrate: %lld bps\n", in_codecpar->bit_rate);
            } else {
                enc_ctx->bit_rate = 128000; // 128 kbps as a fallback
                LOG(INFO, "Original audio bitrate not available, using default: 128 kbps\n");
            }
        }
        
        if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        
        LOG(INFO, "Opening encoder '%s'...\n", enc->name);
        int ret = avcodec_open2(enc_ctx, enc, NULL);
        if (ret < 0) {
            char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, err_buf, sizeof(err_buf));
            LOG(ERROR, "Failed to open encoder: %s\n", err_buf);
            goto end;
        }
        LOG(INFO, "Encoder opened successfully.\n");

        avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
        out_stream->time_base = enc_ctx->time_base;
        if (enc_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
            out_stream->r_frame_rate = enc_ctx->framerate;
            out_stream->avg_frame_rate = enc_ctx->framerate;
        }
        stream_ctxs[i].enc_ctx = enc_ctx;
    }

    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&ofmt_ctx->pb, output_video_filename, AVIO_FLAG_WRITE) < 0) {
            LOG(ERROR, "ERROR: Could not open output file %s\n", output_video_filename);
            goto end;
        }
    }
    
    if (avformat_write_header(ofmt_ctx, NULL) < 0) {
        LOG(ERROR, "ERROR: Failed to write output header\n");
        goto end;
    }

    double start_time = (double)start_frame / fps;
    double end_time = (double)(end_frame + 1) / fps;

    av_seek_frame(ifmt_ctx, -1, (int64_t)(start_time * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    int64_t* first_pts = NULL;
    
    first_pts = av_calloc(ifmt_ctx->nb_streams, sizeof(int64_t));
    if (!first_pts) {
        LOG(ERROR, "Failed to allocate memory for pts tracking\n");
        goto end;
    }
    for(unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) first_pts[i] = -1;
    int64_t video_frames_written = 0;

    while (av_read_frame(ifmt_ctx, pkt) >= 0) {
        int stream_index = pkt->stream_index;
        if (stream_mapping[stream_index] < 0) {
            av_packet_unref(pkt);
            continue;
        }
        AVStream* in_stream = ifmt_ctx->streams[stream_index];
        AVStream* out_stream = ofmt_ctx->streams[stream_mapping[stream_index]];
        StreamContext* s_ctx = &stream_ctxs[stream_index];
        
        if (!s_ctx->dec_ctx) {
            av_packet_unref(pkt);
            continue;
        }

        double ts_in_seconds = pkt->pts * av_q2d(in_stream->time_base);
        if (ts_in_seconds >= end_time) {
            av_packet_unref(pkt);
            break;
        }

        if (avcodec_send_packet(s_ctx->dec_ctx, pkt) == 0) {
            while (avcodec_receive_frame(s_ctx->dec_ctx, frame) == 0) {
                double frame_ts_sec = frame->pts * av_q2d(in_stream->time_base);
                if (frame_ts_sec < start_time) {
                    continue;
                }
                
                AVFrame* frame_to_encode = frame;
                if (s_ctx->sws_ctx) {
                    sws_scale(s_ctx->sws_ctx, (const uint8_t * const *)frame->data, frame->linesize, 0, frame->height, s_ctx->tmp_frame->data, s_ctx->tmp_frame->linesize);
                    frame_to_encode = s_ctx->tmp_frame;
                }

                if (s_ctx->enc_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
                    frame_to_encode->pts = video_frames_written++;
                } else if (s_ctx->enc_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
                    if (first_pts[stream_index] == -1) {
                        first_pts[stream_index] = frame->pts;
                    }
                    frame_to_encode->pts = av_rescale_q(frame->pts - first_pts[stream_index], in_stream->time_base, s_ctx->enc_ctx->time_base);
                }

                if (avcodec_send_frame(s_ctx->enc_ctx, frame_to_encode) == 0) {
                    AVPacket out_pkt = { 0 };
                    
                    while (avcodec_receive_packet(s_ctx->enc_ctx, &out_pkt) == 0) {
                        av_packet_rescale_ts(&out_pkt, s_ctx->enc_ctx->time_base, out_stream->time_base);
                        out_pkt.stream_index = stream_mapping[stream_index];
                        av_interleaved_write_frame(ofmt_ctx, &out_pkt);
                        av_packet_unref(&out_pkt);
                    }
                }
            }
        }
        av_packet_unref(pkt);
    }

    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
        if (stream_ctxs[i].enc_ctx) {
             if (avcodec_send_frame(stream_ctxs[i].enc_ctx, NULL) == 0) {
                AVPacket out_pkt = { 0 };
                while(avcodec_receive_packet(stream_ctxs[i].enc_ctx, &out_pkt) == 0) {
                     av_packet_rescale_ts(&out_pkt, stream_ctxs[i].enc_ctx->time_base, ofmt_ctx->streams[stream_mapping[i]]->time_base);
                     out_pkt.stream_index = stream_mapping[i];
                     av_interleaved_write_frame(ofmt_ctx, &out_pkt);
                     av_packet_unref(&out_pkt);
                }
             }
        }
    }

    av_write_trailer(ofmt_ctx);
    success = true;

end:
    av_packet_free(&pkt);
    av_frame_free(&frame);
    av_free(first_pts);
    av_free(stream_mapping);
    cleanup(ifmt_ctx, ofmt_ctx, stream_ctxs);
    return success;
}

bool process_audio_data(const uint8_t* audio_data, uint32_t audio_size,
                       float fps, int start_frame, int end_frame,
                       uint8_t** output_data_ptr, uint32_t* output_size_ptr) {
    if (!audio_data || !audio_size || fps <= 0 || start_frame < 0 || end_frame < start_frame || 
        !output_data_ptr || !output_size_ptr) {
        return false;
    }
    
    // Initialize output parameters
    *output_data_ptr = NULL;
    *output_size_ptr = 0;
    
    // Calculate timing
    double start_time = (double)start_frame / fps;
    double end_time = (double)(end_frame + 1) / fps;  // +1 to include the end frame
    
    LOG(INFO, "Trimming audio from %.3f to %.3f seconds (frames %d to %d)\n", 
        start_time, end_time, start_frame, end_frame);
    
    // Set up input memory buffer
    memory_buffer_t input_mem_buf = { (uint8_t*)audio_data, audio_size, 0 };
    
    // Create custom I/O context for input
    uint8_t* avio_buffer = av_malloc(4096);
    if (!avio_buffer) {
        LOG(ERROR, "ERROR: Failed to allocate AVIO buffer\n");
        return false;
    }
    
    AVIOContext* input_avio = avio_alloc_context(avio_buffer, 4096, 0, &input_mem_buf, 
                                                read_memory_buffer, NULL, seek_memory_buffer);
    if (!input_avio) {
        LOG(ERROR, "ERROR: Failed to create input AVIO context\n");
        av_free(avio_buffer);
        return false;
    }
    
    // Create input format context
    AVFormatContext* input_fmt_ctx = avformat_alloc_context();
    if (!input_fmt_ctx) {
        LOG(ERROR, "ERROR: Failed to allocate input format context\n");
        avio_context_free(&input_avio);
        return false;
    }
    
    input_fmt_ctx->pb = input_avio;
    
    // Open input
    if (avformat_open_input(&input_fmt_ctx, NULL, NULL, NULL) < 0) {
        LOG(ERROR, "ERROR: Failed to open input audio stream\n");
        avformat_free_context(input_fmt_ctx);
        avio_context_free(&input_avio);
        return false;
    }
    
    // Find stream info
    if (avformat_find_stream_info(input_fmt_ctx, NULL) < 0) {
        LOG(ERROR, "ERROR: Failed to find stream info\n");
        avformat_close_input(&input_fmt_ctx);
        avio_context_free(&input_avio);
        return false;
    }
    
    // Find audio stream
    int audio_stream_idx = -1;
    for (unsigned int i = 0; i < input_fmt_ctx->nb_streams; i++) {
        if (input_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_idx = i;
            break;
        }
    }
    
    if (audio_stream_idx == -1) {
        LOG(ERROR, "ERROR: No audio stream found\n");
        avformat_close_input(&input_fmt_ctx);
        avio_context_free(&input_avio);
        return false;
    }
    
    // Set up output buffer
    output_buffer_t output_buf = { NULL, 0, 0 };
    
    // Create custom I/O context for output
    uint8_t* output_avio_buffer = av_malloc(4096);
    if (!output_avio_buffer) {
        LOG(ERROR, "ERROR: Failed to allocate output AVIO buffer\n");
        avformat_close_input(&input_fmt_ctx);
        avio_context_free(&input_avio);
        return false;
    }
    
    AVIOContext* output_avio = avio_alloc_context(output_avio_buffer, 4096, 1, &output_buf, 
                                                 NULL, write_output_buffer, NULL);
    if (!output_avio) {
        LOG(ERROR, "ERROR: Failed to create output AVIO context\n");
        av_free(output_avio_buffer);
        avformat_close_input(&input_fmt_ctx);
        avio_context_free(&input_avio);
        return false;
    }
    
    // Create output format context
    AVFormatContext* output_fmt_ctx = NULL;
    if (avformat_alloc_output_context2(&output_fmt_ctx, NULL, "mp3", NULL) < 0) {
        LOG(ERROR, "ERROR: Failed to create output format context\n");
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        avio_context_free(&input_avio);
        return false;
    }
    
    output_fmt_ctx->pb = output_avio;
    
    // Copy stream from input to output
    AVStream* input_stream = input_fmt_ctx->streams[audio_stream_idx];
    AVStream* output_stream = avformat_new_stream(output_fmt_ctx, NULL);
    if (!output_stream) {
        LOG(ERROR, "ERROR: Failed to create output stream\n");
        avformat_free_context(output_fmt_ctx);
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        avio_context_free(&input_avio);
        return false;
    }
    
    // Copy codec parameters
    if (avcodec_parameters_copy(output_stream->codecpar, input_stream->codecpar) < 0) {
        LOG(ERROR, "ERROR: Failed to copy codec parameters\n");
        avformat_free_context(output_fmt_ctx);
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        avio_context_free(&input_avio);
        return false;
    }
    
    // Write output header
    if (avformat_write_header(output_fmt_ctx, NULL) < 0) {
        LOG(ERROR, "ERROR: Failed to write output header\n");
        avformat_free_context(output_fmt_ctx);
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        avio_context_free(&input_avio);
        return false;
    }
    
    // Seek to start time
    int64_t start_timestamp = av_rescale_q((int64_t)(start_time * AV_TIME_BASE), AV_TIME_BASE_Q, 
                                          input_stream->time_base);
    if (av_seek_frame(input_fmt_ctx, audio_stream_idx, start_timestamp, AVSEEK_FLAG_BACKWARD) < 0) {
        LOG(WARNING, "WARNING: Failed to seek to start time, processing from beginning\n");
    }
    
    // Process packets
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        LOG(ERROR, "ERROR: Failed to allocate packet for audio processing\n");
        avformat_free_context(output_fmt_ctx);
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        avio_context_free(&input_avio);
        return false;
    }
    
    int64_t end_timestamp = av_rescale_q((int64_t)(end_time * AV_TIME_BASE), AV_TIME_BASE_Q, 
                                        input_stream->time_base);
    
    while (av_read_frame(input_fmt_ctx, packet) >= 0) {
        if (packet->stream_index == audio_stream_idx) {
            // Check if packet is within our time range
            if (packet->pts != AV_NOPTS_VALUE) {
                if (packet->pts >= start_timestamp && packet->pts < end_timestamp) {
                    // Adjust packet stream index and timestamps for output
                    packet->stream_index = 0;
                    av_packet_rescale_ts(packet, input_stream->time_base, output_stream->time_base);
                    
                    // Write packet to output
                    if (av_write_frame(output_fmt_ctx, packet) < 0) {
                        LOG(WARNING, "WARNING: Failed to write audio packet\n");
                    }
                } else if (packet->pts >= end_timestamp) {
                    // We've passed our end time, stop processing
                    av_packet_unref(packet);
                    break;
                }
            }
        }
        av_packet_unref(packet);
    }
    
    // Write trailer
    av_write_trailer(output_fmt_ctx);
    
    // Set output data
    if (output_buf.size > 0) {
        *output_data_ptr = output_buf.data;
        *output_size_ptr = (uint32_t)output_buf.size;
        
        LOG(INFO, "Successfully trimmed audio from %u bytes to %u bytes\n", 
            audio_size, *output_size_ptr);
        
        // DEBUG: Write trimmed audio to file for debugging
        FILE* debug_audio = fopen("debug_trimmed_audio.mp3", "wb");
        if (debug_audio) {
            fwrite(output_buf.data, 1, output_buf.size, debug_audio);
            fclose(debug_audio);
            LOG(INFO, "DEBUG: Wrote trimmed audio to debug_trimmed_audio.mp3 (%u bytes)\n", *output_size_ptr);
        }
    } else {
        LOG(ERROR, "ERROR: No output audio data generated\n");
        if (output_buf.data) {
            free(output_buf.data);
        }
    }
    
    // Cleanup
    av_packet_free(&packet);
    avformat_free_context(output_fmt_ctx);
    avio_context_free(&output_avio);
    avformat_close_input(&input_fmt_ctx);
    avio_context_free(&input_avio);
    
    return (*output_data_ptr != NULL);
} 

bool extract_audio_from_video(const char* video_filename, 
                             uint8_t** output_data_ptr, uint32_t* output_size_ptr) {
    if (!video_filename || !output_data_ptr || !output_size_ptr) {
        return false;
    }
    
    // Initialize output parameters
    *output_data_ptr = NULL;
    *output_size_ptr = 0;
    
    LOG(INFO, "Extracting audio from video file: %s\n", video_filename);
    
    // Open input video file
    AVFormatContext* input_fmt_ctx = NULL;
    if (!open_input(video_filename, NULL, 0, &input_fmt_ctx)) {
        return false;
    }
    
    // Find audio stream
    int audio_stream_idx = -1;
    for (unsigned int i = 0; i < input_fmt_ctx->nb_streams; i++) {
        if (input_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_idx = i;
            break;
        }
    }
    
    if (audio_stream_idx == -1) {
        LOG(INFO, "No audio stream found in video file\n");
        avformat_close_input(&input_fmt_ctx);
        return true; // Not an error, just no audio
    }
    
    AVStream* input_stream = input_fmt_ctx->streams[audio_stream_idx];
#if LIBAVUTIL_VERSION_CHECK(57, 28, 100)
    int channels = input_stream->codecpar->ch_layout.nb_channels;
#else
    int channels = input_stream->codecpar->channels;
#endif
    LOG(INFO, "Found audio stream: codec=%d, sample_rate=%d, channels=%d\n", 
        input_stream->codecpar->codec_id, 
        input_stream->codecpar->sample_rate,
        channels);
    
    // Set up output buffer for the entire audio stream
    output_buffer_t output_buf = { NULL, 0, 0 };
    
    // Create custom I/O context for output
    uint8_t* output_avio_buffer = av_malloc(4096);
    if (!output_avio_buffer) {
        LOG(ERROR, "ERROR: Failed to allocate output AVIO buffer\n");
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    AVIOContext* output_avio = avio_alloc_context(output_avio_buffer, 4096, 1, &output_buf, 
                                                 NULL, write_output_buffer, NULL);
    if (!output_avio) {
        LOG(ERROR, "ERROR: Failed to create output AVIO context\n");
        av_free(output_avio_buffer);
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    // Create output format context - using MP3 format for better OS compatibility
    AVFormatContext* output_fmt_ctx = NULL;
    if (avformat_alloc_output_context2(&output_fmt_ctx, NULL, "mp3", NULL) < 0) {
        LOG(ERROR, "ERROR: Failed to create output format context\n");
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    output_fmt_ctx->pb = output_avio;
    
    // Set up audio decoder for input stream
    const AVCodec* decoder = avcodec_find_decoder(input_stream->codecpar->codec_id);
    if (!decoder) {
        LOG(ERROR, "ERROR: Failed to find decoder for audio stream\n");
        avformat_free_context(output_fmt_ctx);
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    AVCodecContext* decoder_ctx = avcodec_alloc_context3(decoder);
    if (!decoder_ctx) {
        LOG(ERROR, "ERROR: Failed to allocate decoder context\n");
        avformat_free_context(output_fmt_ctx);
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    if (avcodec_parameters_to_context(decoder_ctx, input_stream->codecpar) < 0) {
        LOG(ERROR, "ERROR: Failed to copy decoder parameters\n");
        avcodec_free_context(&decoder_ctx);
        avformat_free_context(output_fmt_ctx);
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    if (avcodec_open2(decoder_ctx, decoder, NULL) < 0) {
        LOG(ERROR, "ERROR: Failed to open decoder\n");
        avcodec_free_context(&decoder_ctx);
        avformat_free_context(output_fmt_ctx);
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    // Set up MP3 encoder for output
    const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_MP3);
    if (!encoder) {
        LOG(ERROR, "ERROR: Failed to find MP3 encoder\n");
        avcodec_free_context(&decoder_ctx);
        avformat_free_context(output_fmt_ctx);
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    AVCodecContext* encoder_ctx = avcodec_alloc_context3(encoder);
    if (!encoder_ctx) {
        LOG(ERROR, "ERROR: Failed to allocate encoder context\n");
        avcodec_free_context(&decoder_ctx);
        avformat_free_context(output_fmt_ctx);
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    // Configure MP3 encoder
    encoder_ctx->bit_rate = 128000; // 128 kbps
    encoder_ctx->sample_rate = decoder_ctx->sample_rate;
#if LIBAVUTIL_VERSION_CHECK(57, 28, 100)
    av_channel_layout_copy(&encoder_ctx->ch_layout, &decoder_ctx->ch_layout);
#else
    encoder_ctx->channels = decoder_ctx->channels;
    encoder_ctx->channel_layout = decoder_ctx->channel_layout;
#endif
    encoder_ctx->sample_fmt = encoder->sample_fmts ? encoder->sample_fmts[0] : AV_SAMPLE_FMT_S16P;
    encoder_ctx->time_base = (AVRational){1, encoder_ctx->sample_rate};
    
    // DEBUG: Log encoder configuration
    LOG(INFO, "DEBUG: MP3 Encoder Config:\n");
    LOG(INFO, "  - Bit rate: %lld bps\n", encoder_ctx->bit_rate);
    LOG(INFO, "  - Sample rate: %d Hz\n", encoder_ctx->sample_rate);
#if LIBAVUTIL_VERSION_CHECK(57, 28, 100)
    LOG(INFO, "  - Channels: %d\n", encoder_ctx->ch_layout.nb_channels);
#else
    LOG(INFO, "  - Channels: %d\n", encoder_ctx->channels);
#endif
    LOG(INFO, "  - Sample format: %s\n", av_get_sample_fmt_name(encoder_ctx->sample_fmt));
    LOG(INFO, "  - Time base: %d/%d\n", encoder_ctx->time_base.num, encoder_ctx->time_base.den);
    
    if (avcodec_open2(encoder_ctx, encoder, NULL) < 0) {
        LOG(ERROR, "ERROR: Failed to open MP3 encoder\n");
        avcodec_free_context(&encoder_ctx);
        avcodec_free_context(&decoder_ctx);
        avformat_free_context(output_fmt_ctx);
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    // Create output stream and copy encoder parameters
    AVStream* output_stream = avformat_new_stream(output_fmt_ctx, encoder);
    if (!output_stream) {
        LOG(ERROR, "ERROR: Failed to create output stream\n");
        avcodec_free_context(&encoder_ctx);
        avcodec_free_context(&decoder_ctx);
        avformat_free_context(output_fmt_ctx);
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    if (avcodec_parameters_from_context(output_stream->codecpar, encoder_ctx) < 0) {
        LOG(ERROR, "ERROR: Failed to copy encoder parameters to stream\n");
        avcodec_free_context(&encoder_ctx);
        avcodec_free_context(&decoder_ctx);
        avformat_free_context(output_fmt_ctx);
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    output_stream->time_base = encoder_ctx->time_base;
    
    // Set up audio resampler/converter (swresample)
    SwrContext* swr_ctx = swr_alloc();
    if (!swr_ctx) {
        LOG(ERROR, "ERROR: Failed to allocate SwrContext\n");
        avcodec_free_context(&encoder_ctx);
        avcodec_free_context(&decoder_ctx);
        avformat_free_context(output_fmt_ctx);
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    // Configure resampler
#if LIBAVUTIL_VERSION_CHECK(57, 28, 100)
    av_opt_set_chlayout(swr_ctx, "in_chlayout", &decoder_ctx->ch_layout, 0);
    av_opt_set_chlayout(swr_ctx, "out_chlayout", &encoder_ctx->ch_layout, 0);
#else
    av_opt_set_int(swr_ctx, "in_channel_layout", decoder_ctx->channel_layout, 0);
    av_opt_set_int(swr_ctx, "out_channel_layout", encoder_ctx->channel_layout, 0);
#endif
    av_opt_set_int(swr_ctx, "in_sample_rate", decoder_ctx->sample_rate, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", encoder_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", decoder_ctx->sample_fmt, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", encoder_ctx->sample_fmt, 0);
    
    if (swr_init(swr_ctx) < 0) {
        LOG(ERROR, "ERROR: Failed to initialize SwrContext\n");
        swr_free(&swr_ctx);
        avcodec_free_context(&encoder_ctx);
        avcodec_free_context(&decoder_ctx);
        avformat_free_context(output_fmt_ctx);
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    LOG(INFO, "Audio resampler configured: %s@%dHz → %s@%dHz\n",
        av_get_sample_fmt_name(decoder_ctx->sample_fmt), decoder_ctx->sample_rate,
        av_get_sample_fmt_name(encoder_ctx->sample_fmt), encoder_ctx->sample_rate);
    
    // Write output header
    if (avformat_write_header(output_fmt_ctx, NULL) < 0) {
        LOG(ERROR, "ERROR: Failed to write output header\n");
        swr_free(&swr_ctx);
        avcodec_free_context(&encoder_ctx);
        avcodec_free_context(&decoder_ctx);
        avformat_free_context(output_fmt_ctx);
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    // Allocate frames and packet for transcoding
    AVPacket* input_packet = av_packet_alloc();
    AVPacket* output_packet = av_packet_alloc();
    AVFrame* decoded_frame = av_frame_alloc();
    AVFrame* resampled_frame = av_frame_alloc();
    
    if (!input_packet || !output_packet || !decoded_frame || !resampled_frame) {
        LOG(ERROR, "ERROR: Failed to allocate packets/frames for audio transcoding\n");
        av_packet_free(&input_packet);
        av_packet_free(&output_packet);
        av_frame_free(&decoded_frame);
        av_frame_free(&resampled_frame);
        swr_free(&swr_ctx);
        avcodec_free_context(&encoder_ctx);
        avcodec_free_context(&decoder_ctx);
        avformat_free_context(output_fmt_ctx);
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    // Resampled frame will be configured in each iteration
    
    LOG(INFO, "Starting audio transcoding from %s to MP3...\n", 
        avcodec_get_name(decoder_ctx->codec_id));
    
    // DEBUG: Also save the original audio stream without transcoding for comparison
    FILE* debug_original_audio = NULL;
    if (decoder_ctx->codec_id == AV_CODEC_ID_AAC) {
        debug_original_audio = fopen("debug_original_audio.aac", "wb");
        LOG(INFO, "DEBUG: Will save original AAC audio to debug_original_audio.aac\n");
    } else if (decoder_ctx->codec_id == AV_CODEC_ID_MP3) {
        debug_original_audio = fopen("debug_original_audio.mp3", "wb");
        LOG(INFO, "DEBUG: Will save original MP3 audio to debug_original_audio.mp3\n");
    }
    
    int64_t packet_count = 0;
    int64_t frame_count = 0;
    
    // Process all audio packets - decode and re-encode to MP3
    while (av_read_frame(input_fmt_ctx, input_packet) >= 0) {
        if (input_packet->stream_index == audio_stream_idx) {
            packet_count++;
            
            // DEBUG: Write original audio packet to debug file
            if (debug_original_audio) {
                fwrite(input_packet->data, 1, input_packet->size, debug_original_audio);
            }
            
            // Send packet to decoder
            int ret = avcodec_send_packet(decoder_ctx, input_packet);
            if (ret < 0) {
                LOG(WARNING, "WARNING: Error sending packet to decoder\n");
                av_packet_unref(input_packet);
                continue;
            }
            
            // Receive decoded frames
            while (ret >= 0) {
                ret = avcodec_receive_frame(decoder_ctx, decoded_frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                if (ret < 0) {
                    LOG(WARNING, "WARNING: Error receiving frame from decoder\n");
                    break;
                }
                
                frame_count++;
                
                // Clear any previous buffer allocation
                av_frame_unref(resampled_frame);
                
                // Calculate required samples for resampling
                int max_dst_nb_samples = av_rescale_rnd(
                    swr_get_delay(swr_ctx, decoded_frame->sample_rate) + decoded_frame->nb_samples,
                    encoder_ctx->sample_rate, decoded_frame->sample_rate, AV_ROUND_UP);
                
                // Set up resampled frame properties
                resampled_frame->format = encoder_ctx->sample_fmt;
                resampled_frame->sample_rate = encoder_ctx->sample_rate;
                resampled_frame->nb_samples = max_dst_nb_samples;
#if LIBAVUTIL_VERSION_CHECK(57, 28, 100)
                av_channel_layout_copy(&resampled_frame->ch_layout, &encoder_ctx->ch_layout);
#else
                resampled_frame->channels = encoder_ctx->channels;
                resampled_frame->channel_layout = encoder_ctx->channel_layout;
#endif
                
                // Allocate buffer for the resampled frame
                if (av_frame_get_buffer(resampled_frame, 0) < 0) {
                    LOG(WARNING, "WARNING: Error allocating resampled frame buffer\n");
                    continue;
                }
                
                // Convert the audio samples
                int converted_samples = swr_convert(swr_ctx,
                    resampled_frame->data, max_dst_nb_samples,
                    (const uint8_t**)decoded_frame->data, decoded_frame->nb_samples);
                
                if (converted_samples < 0) {
                    LOG(WARNING, "WARNING: Error converting audio samples\n");
                    av_frame_unref(resampled_frame);
                    continue;
                }
                
                // Update frame with actual number of converted samples
                resampled_frame->nb_samples = converted_samples;
                resampled_frame->pts = av_rescale_q(decoded_frame->pts, 
                    (AVRational){1, decoded_frame->sample_rate}, 
                    (AVRational){1, encoder_ctx->sample_rate});
                
                // Send resampled frame to encoder
                ret = avcodec_send_frame(encoder_ctx, resampled_frame);
                if (ret < 0) {
                    LOG(WARNING, "WARNING: Error sending frame to encoder\n");
                }
                
                // Frame will be unreferenced at the end of outer while loop
                
                // Receive encoded packets
                while (ret >= 0) {
                    ret = avcodec_receive_packet(encoder_ctx, output_packet);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    }
                    if (ret < 0) {
                        LOG(WARNING, "WARNING: Error receiving packet from encoder\n");
                        break;
                    }
                    
                    // Set correct stream index and timestamps
                    output_packet->stream_index = 0;
                    av_packet_rescale_ts(output_packet, encoder_ctx->time_base, output_stream->time_base);
                    
                    // Write to output
                    if (av_interleaved_write_frame(output_fmt_ctx, output_packet) < 0) {
                        LOG(WARNING, "WARNING: Failed to write MP3 packet\n");
                    }
                    
                    av_packet_unref(output_packet);
                }
            }
        }
        av_packet_unref(input_packet);
    }
    
    // Unref the resampled frame after main processing loop
    av_frame_unref(resampled_frame);
    
    // Flush decoder
    avcodec_send_packet(decoder_ctx, NULL);
    while (avcodec_receive_frame(decoder_ctx, decoded_frame) >= 0) {
        frame_count++;
        
        // Clear any previous buffer allocation
        av_frame_unref(resampled_frame);
        
        // Calculate required samples for resampling
        int max_dst_nb_samples = av_rescale_rnd(
            swr_get_delay(swr_ctx, decoded_frame->sample_rate) + decoded_frame->nb_samples,
            encoder_ctx->sample_rate, decoded_frame->sample_rate, AV_ROUND_UP);
        
        // Set up resampled frame properties
        resampled_frame->format = encoder_ctx->sample_fmt;
        resampled_frame->sample_rate = encoder_ctx->sample_rate;
        resampled_frame->nb_samples = max_dst_nb_samples;
#if LIBAVUTIL_VERSION_CHECK(57, 28, 100)
        av_channel_layout_copy(&resampled_frame->ch_layout, &encoder_ctx->ch_layout);
#else
        resampled_frame->channels = encoder_ctx->channels;
        resampled_frame->channel_layout = encoder_ctx->channel_layout;
#endif
        
        if (av_frame_get_buffer(resampled_frame, 0) >= 0) {
            int converted_samples = swr_convert(swr_ctx,
                resampled_frame->data, max_dst_nb_samples,
                (const uint8_t**)decoded_frame->data, decoded_frame->nb_samples);
            
            if (converted_samples >= 0) {
                resampled_frame->nb_samples = converted_samples;
                resampled_frame->pts = av_rescale_q(decoded_frame->pts, 
                    (AVRational){1, decoded_frame->sample_rate}, 
                    (AVRational){1, encoder_ctx->sample_rate});
                
                avcodec_send_frame(encoder_ctx, resampled_frame);
                while (avcodec_receive_packet(encoder_ctx, output_packet) >= 0) {
                    output_packet->stream_index = 0;
                    av_packet_rescale_ts(output_packet, encoder_ctx->time_base, output_stream->time_base);
                    av_interleaved_write_frame(output_fmt_ctx, output_packet);
                    av_packet_unref(output_packet);
                }
            }
        }
    }
    
    // Flush any remaining samples in the resampler
    while (1) {
        // Clear any previous buffer allocation
        av_frame_unref(resampled_frame);
        
        // Set up resampled frame properties
        resampled_frame->format = encoder_ctx->sample_fmt;
        resampled_frame->sample_rate = encoder_ctx->sample_rate;
        resampled_frame->nb_samples = encoder_ctx->frame_size ? encoder_ctx->frame_size : 1024;
#if LIBAVUTIL_VERSION_CHECK(57, 28, 100)
        av_channel_layout_copy(&resampled_frame->ch_layout, &encoder_ctx->ch_layout);
#else
        resampled_frame->channels = encoder_ctx->channels;
        resampled_frame->channel_layout = encoder_ctx->channel_layout;
#endif
        
        if (av_frame_get_buffer(resampled_frame, 0) < 0) break;
        
        int converted_samples = swr_convert(swr_ctx,
            resampled_frame->data, resampled_frame->nb_samples,
            NULL, 0);
        
        if (converted_samples <= 0) {
            break;
        }
        
        resampled_frame->nb_samples = converted_samples;
        avcodec_send_frame(encoder_ctx, resampled_frame);
        while (avcodec_receive_packet(encoder_ctx, output_packet) >= 0) {
            output_packet->stream_index = 0;
            av_packet_rescale_ts(output_packet, encoder_ctx->time_base, output_stream->time_base);
            av_interleaved_write_frame(output_fmt_ctx, output_packet);
            av_packet_unref(output_packet);
        }
    }
    
    // Flush encoder
    avcodec_send_frame(encoder_ctx, NULL);
    while (avcodec_receive_packet(encoder_ctx, output_packet) >= 0) {
        output_packet->stream_index = 0;
        av_packet_rescale_ts(output_packet, encoder_ctx->time_base, output_stream->time_base);
        av_interleaved_write_frame(output_fmt_ctx, output_packet);
        av_packet_unref(output_packet);
    }
    
    // Write trailer
    av_write_trailer(output_fmt_ctx);
    
    // Set output data
    if (output_buf.size > 0) {
        *output_data_ptr = output_buf.data;
        *output_size_ptr = (uint32_t)output_buf.size;
        
        LOG(INFO, "Successfully transcoded audio to MP3: %lld input packets, %lld frames, %u output bytes\n", 
            packet_count, frame_count, *output_size_ptr);
        
        // DEBUG: Write extracted audio to file for debugging
        FILE* debug_audio = fopen("debug_extracted_audio.mp3", "wb");
        if (debug_audio) {
            fwrite(output_buf.data, 1, output_buf.size, debug_audio);
            fclose(debug_audio);
            LOG(INFO, "DEBUG: Wrote extracted audio to debug_extracted_audio.mp3 (%u bytes)\n", *output_size_ptr);
        }
    } else {
        LOG(INFO, "No audio data extracted (stream may be empty)\n");
        if (output_buf.data) {
            free(output_buf.data);
        }
    }
    
    // Close debug original audio file
    if (debug_original_audio) {
        fclose(debug_original_audio);
        LOG(INFO, "DEBUG: Closed original audio debug file\n");
    }
    
    // Cleanup - unref frames before freeing them
    av_frame_unref(decoded_frame);
    av_frame_unref(resampled_frame);
    av_packet_free(&input_packet);
    av_packet_free(&output_packet);
    av_frame_free(&decoded_frame);
    av_frame_free(&resampled_frame);
    swr_free(&swr_ctx);
    avcodec_free_context(&encoder_ctx);
    avcodec_free_context(&decoder_ctx);
    avformat_free_context(output_fmt_ctx);
    avio_context_free(&output_avio);
    avformat_close_input(&input_fmt_ctx);
    
    return true; // Return true even if no audio data, as this is not an error
} 
