/** @file video_processing.h
 * Video and Audio Processing Functions for vol2vol
 *
 * This file contains functions for processing video and audio files,
 * including trimming, format conversion, and timestamp handling.
 *
 * Authors: Jan Ondřej <jan@volograms.com>
 * Copyright: 2025, Volograms (https://volograms.com/)
 * Language: C99
 * License: The MIT License
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

// Log types for the logging system
typedef enum _log_type { 
    _LOG_TYPE_INFO = 0, 
    _LOG_TYPE_DEBUG, 
    _LOG_TYPE_WARNING, 
    _LOG_TYPE_ERROR, 
    _LOG_TYPE_SUCCESS 
} _log_type;

// Logging function declaration
void _printlog(_log_type log_type, const char* message_str, ...);


/**
 * Process video data - trim video to match frame range
 * 
 * @param input_video_filename Input video file path
 * @param output_video_filename Output video file path  
 * @param fps               Frames per second for time calculation
 * @param start_frame       Start frame for trimming
 * @param end_frame         End frame for trimming
 * @return                  True on success, false on error
 */
bool process_video_file(const char* input_video_filename, const char* output_video_filename,
                       float fps, int start_frame, int end_frame);

/**
 * Process audio data - trim MP3 audio to match frame range
 * 
 * @param audio_data         Input MP3 audio data
 * @param audio_size         Size of input audio data
 * @param fps               Frames per second for time calculation
 * @param start_frame       Start frame for trimming
 * @param end_frame         End frame for trimming
 * @param output_data_ptr   Pointer to store output audio data (will be allocated)
 * @param output_size_ptr   Pointer to store output audio size
 * @return                  True on success, false on error
 */
bool process_audio_data(const uint8_t* audio_data, uint32_t audio_size,
                       float fps, int start_frame, int end_frame,
                       uint8_t** output_data_ptr, uint32_t* output_size_ptr);

#ifdef __cplusplus
}
#endif 