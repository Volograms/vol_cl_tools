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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>


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

bool process_video_file(const char* input_video_filename, const char* output_video_filename,
                       float fps, int start_frame, int end_frame) {
    if (!input_video_filename || !output_video_filename || fps <= 0 || start_frame < 0 || end_frame < start_frame) {
        return false;
    }
    
    // Calculate timing
    double start_time = (double)start_frame / fps;
    double end_time = (double)(end_frame + 1) / fps;  // +1 to include the end frame
    double duration = end_time - start_time;
    
    LOG(INFO, "Trimming video from %.3f to %.3f seconds (%.3f duration, frames %d to %d)\n", 
        start_time, end_time, duration, start_frame, end_frame);
    
    // Open input format context
    AVFormatContext* input_fmt_ctx = NULL;
    if (avformat_open_input(&input_fmt_ctx, input_video_filename, NULL, NULL) < 0) {
        LOG(ERROR, "ERROR: Failed to open input video file %s\n", input_video_filename);
        return false;
    }
    
    // Find stream info
    if (avformat_find_stream_info(input_fmt_ctx, NULL) < 0) {
        LOG(ERROR, "ERROR: Failed to find stream info\n");
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    // Create output format context
    AVFormatContext* output_fmt_ctx = NULL;
    if (avformat_alloc_output_context2(&output_fmt_ctx, NULL, NULL, output_video_filename) < 0) {
        LOG(ERROR, "ERROR: Failed to create output format context\n");
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    // Copy streams from input to output
    int* stream_mapping = av_calloc(input_fmt_ctx->nb_streams, sizeof(int));
    if (!stream_mapping) {
        LOG(ERROR, "ERROR: Failed to allocate stream mapping array\n");
        avformat_free_context(output_fmt_ctx);
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    int stream_index = 0;
    for (unsigned int i = 0; i < input_fmt_ctx->nb_streams; i++) {
        AVStream* in_stream = input_fmt_ctx->streams[i];
        AVCodecParameters* in_codecpar = in_stream->codecpar;
        
        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            stream_mapping[i] = -1;
            continue;
        }
        
        stream_mapping[i] = stream_index++;
        
        AVStream* out_stream = avformat_new_stream(output_fmt_ctx, NULL);
        if (!out_stream) {
            LOG(ERROR, "ERROR: Failed to create output stream\n");
            av_freep(&stream_mapping);
            avformat_free_context(output_fmt_ctx);
            avformat_close_input(&input_fmt_ctx);
            return false;
        }
        
        // Copy codec parameters
        if (avcodec_parameters_copy(out_stream->codecpar, in_codecpar) < 0) {
            LOG(ERROR, "ERROR: Failed to copy codec parameters\n");
            av_freep(&stream_mapping);
            avformat_free_context(output_fmt_ctx);
            avformat_close_input(&input_fmt_ctx);
            return false;
        }
        
        out_stream->codecpar->codec_tag = 0;
    }
    
    // Open output file
    if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&output_fmt_ctx->pb, output_video_filename, AVIO_FLAG_WRITE) < 0) {
            LOG(ERROR, "ERROR: Failed to open output video file %s\n", output_video_filename);
            av_freep(&stream_mapping);
            avformat_free_context(output_fmt_ctx);
            avformat_close_input(&input_fmt_ctx);
            return false;
        }
    }
    
    // Write output header
    if (avformat_write_header(output_fmt_ctx, NULL) < 0) {
        LOG(ERROR, "ERROR: Failed to write output header\n");
        av_freep(&stream_mapping);
        if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&output_fmt_ctx->pb);
        }
        avformat_free_context(output_fmt_ctx);
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    // Find video stream for keyframe seeking
    int video_stream_index = -1;
    for (unsigned int i = 0; i < input_fmt_ctx->nb_streams; i++) {
        if (input_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }
    
    // Seek to start time, ensuring we start from a keyframe
    int64_t seek_target = start_time * AV_TIME_BASE;
    LOG(DEBUG, "Seeking to timestamp %lld (%.3f seconds)\n", seek_target, start_time);
    
    if (video_stream_index >= 0) {
        // Seek to keyframe for video stream specifically
        int64_t video_seek_target = av_rescale_q(seek_target, AV_TIME_BASE_Q, 
                                                input_fmt_ctx->streams[video_stream_index]->time_base);
        if (av_seek_frame(input_fmt_ctx, video_stream_index, video_seek_target, AVSEEK_FLAG_BACKWARD) < 0) {
            LOG(WARNING, "WARNING: Failed to seek video stream, trying general seek\n");
            if (av_seek_frame(input_fmt_ctx, -1, seek_target, AVSEEK_FLAG_BACKWARD) < 0) {
                LOG(WARNING, "WARNING: Failed to seek to start time, processing from beginning\n");
            }
        }
    } else {
        // No video stream found, use general seek
        if (av_seek_frame(input_fmt_ctx, -1, seek_target, AVSEEK_FLAG_BACKWARD) < 0) {
            LOG(WARNING, "WARNING: Failed to seek to start time, processing from beginning\n");
        }
    }
    
    // Process packets
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        LOG(ERROR, "ERROR: Failed to allocate packet\n");
        av_freep(&stream_mapping);
        if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&output_fmt_ctx->pb);
        }
        avformat_free_context(output_fmt_ctx);
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    int64_t start_time_av = start_time * AV_TIME_BASE;
    int64_t end_time_av = end_time * AV_TIME_BASE;
    int64_t* stream_first_pts = av_calloc(input_fmt_ctx->nb_streams, sizeof(int64_t));
    int64_t* stream_first_dts = av_calloc(input_fmt_ctx->nb_streams, sizeof(int64_t));
    
    // Check for allocation failure
    if (!stream_first_pts || !stream_first_dts) {
        LOG(ERROR, "ERROR: Failed to allocate timestamp arrays\n");
        av_packet_free(&packet);
        av_freep(&stream_mapping);
        if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&output_fmt_ctx->pb);
        }
        avformat_free_context(output_fmt_ctx);
        avformat_close_input(&input_fmt_ctx);
        return false;
    }
    
    // Initialize first timestamp arrays
    for (unsigned int i = 0; i < input_fmt_ctx->nb_streams; i++) {
        stream_first_pts[i] = AV_NOPTS_VALUE;
        stream_first_dts[i] = AV_NOPTS_VALUE;
    }
    
    while (av_read_frame(input_fmt_ctx, packet) >= 0) {
        if (stream_mapping[packet->stream_index] < 0) {
            av_packet_unref(packet);
            continue;
        }
        
        AVStream* in_stream = input_fmt_ctx->streams[packet->stream_index];
        AVStream* out_stream = output_fmt_ctx->streams[stream_mapping[packet->stream_index]];
        
        // Convert packet timestamp to AV_TIME_BASE for range checking
        int64_t packet_time = av_rescale_q(packet->pts, in_stream->time_base, AV_TIME_BASE_Q);
        
        // Skip packets before our start time
        if (packet_time < start_time_av) {
            av_packet_unref(packet);
            continue;
        }
        
        // Stop processing packets after our end time
        if (packet_time > end_time_av) {
            av_packet_unref(packet);
            break;
        }
        
        // Record first timestamp for each stream to calculate offset
        if (stream_first_pts[packet->stream_index] == AV_NOPTS_VALUE) {
            stream_first_pts[packet->stream_index] = packet->pts;
            LOG(DEBUG, "Stream %d first PTS: %lld (%.3f seconds)\n", 
                packet->stream_index, packet->pts, 
                packet->pts * av_q2d(in_stream->time_base));
        }
        if (stream_first_dts[packet->stream_index] == AV_NOPTS_VALUE) {
            stream_first_dts[packet->stream_index] = packet->dts;
        }
        
        // Adjust packet timestamps to start from 0
        if (packet->pts != AV_NOPTS_VALUE) {
            packet->pts -= stream_first_pts[packet->stream_index];
        }
        if (packet->dts != AV_NOPTS_VALUE) {
            packet->dts -= stream_first_dts[packet->stream_index];
        }
        
        // Rescale timestamps to output timebase
        packet->pts = av_rescale_q_rnd(packet->pts, in_stream->time_base, out_stream->time_base, 
                                      AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        packet->dts = av_rescale_q_rnd(packet->dts, in_stream->time_base, out_stream->time_base, 
                                      AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        packet->duration = av_rescale_q(packet->duration, in_stream->time_base, out_stream->time_base);
        packet->pos = -1;
        packet->stream_index = stream_mapping[packet->stream_index];
        
        // Write packet to output
        if (av_interleaved_write_frame(output_fmt_ctx, packet) < 0) {
            LOG(WARNING, "WARNING: Failed to write video packet\n");
        }
        
        av_packet_unref(packet);
    }
    
    // Update output stream durations
    for (unsigned int i = 0; i < output_fmt_ctx->nb_streams; i++) {
        AVStream* out_stream = output_fmt_ctx->streams[i];
        out_stream->duration = av_rescale_q(duration * AV_TIME_BASE, AV_TIME_BASE_Q, out_stream->time_base);
    }
    
    // Update overall format context duration
    output_fmt_ctx->duration = duration * AV_TIME_BASE;
    
    // Cleanup timestamp arrays
    av_freep(&stream_first_pts);
    av_freep(&stream_first_dts);
    
    // Write trailer
    av_write_trailer(output_fmt_ctx);
    
    // Cleanup
    av_packet_free(&packet);
    av_freep(&stream_mapping);
    
    if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&output_fmt_ctx->pb);
    }
    
    avformat_free_context(output_fmt_ctx);
    avformat_close_input(&input_fmt_ctx);
    
    LOG(INFO, "Successfully trimmed video file\n");
    return true;
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
    int64_t start_timestamp = av_rescale_q(start_time * AV_TIME_BASE, AV_TIME_BASE_Q, 
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
    
    int64_t end_timestamp = av_rescale_q(end_time * AV_TIME_BASE, AV_TIME_BASE_Q, 
                                        input_stream->time_base);
    
    while (av_read_frame(input_fmt_ctx, packet) >= 0) {
        if (packet->stream_index == audio_stream_idx) {
            // Check if packet is within our time range
            if (packet->pts != AV_NOPTS_VALUE) {
                if (packet->pts >= start_timestamp && packet->pts <= end_timestamp) {
                    // Adjust packet stream index and timestamps for output
                    packet->stream_index = 0;
                    av_packet_rescale_ts(packet, input_stream->time_base, output_stream->time_base);
                    
                    // Write packet to output
                    if (av_write_frame(output_fmt_ctx, packet) < 0) {
                        LOG(WARNING, "WARNING: Failed to write audio packet\n");
                    }
                } else if (packet->pts > end_timestamp) {
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
