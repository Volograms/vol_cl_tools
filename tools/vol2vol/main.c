/** @file main.c
 * Volograms VOLS to VOLS converter with modifications.
 *
 * vol2vol   | Vologram to Vologram converter with modifications.
 * --------- | ----------------------------------------------------------------
 * Version   | 0.1.0
 * Authors   | Based on vol2obj by Anton Gerdelan  <anton@volograms.com>
 *           | Jan Ondřej      <jan@volograms.com>
 * Copyright | 2025, Volograms (https://volograms.com/)
 * Language  | C99, C++11
 * Files     | 1
 * Licence   | The MIT License. Note that dependencies have separate licences.
 *           | See LICENSE.md for details.
 *
 * Usage Instructions
 * ------------------
 * For single-file volograms:
 *     ./vol2vol.bin -i INPUT.VOLS -o OUTPUT.VOLS [OPTIONS]
 *
 * For older multi-file volograms:
 *     ./vol2vol.bin -h HEADER.VOLS -s SEQUENCE.VOLS -v VIDEO.MP4 -o OUTPUT.VOLS [OPTIONS]
 *
 * Options:
 *   --no-normals    Remove normals from the output vologram
 *   --texture-size  Resize texture to specified resolution (e.g., 512x512)
 *                   Uses BASIS Universal's high-quality resampling and preserves BASIS format
 *   --start-frame   Start frame for trimming (0-based, inclusive). Audio automatically trimmed to match.
 *   --end-frame     End frame for trimming (0-based, inclusive). Audio automatically trimmed to match.
 *   --help          Show this help message
 *
 * Compilation
 * ------------------
 *
 * `make vol2vol`
 */

#include "vol_av.h"    // Volograms' texture video library.
#include "vol_basis.h" // Volograms' Basis Universal wrapper library.
#include "vol_geom.h"  // Volograms' .vols file parsing library.
#include "basis_encoder_wrapper.h" // BASIS Universal encoder C wrapper.

// FFmpeg includes for audio processing
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/mem.h>
#include <libavutil/error.h>

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _MSC_VER
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#else
#include <strings.h> // strcasecmp
#endif

#if defined( _WIN32 ) || defined( _WIN64 )
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#define _XOPEN_SOURCE_EXTENDED 1
#include <sys/statvfs.h>
#include <errno.h>
#endif

#define MAX_FILENAME_LEN 4096
#define MAX_SUBPATH_LEN 1024

typedef enum _log_type { 
    _LOG_TYPE_INFO = 0, 
    _LOG_TYPE_DEBUG, 
    _LOG_TYPE_WARNING, 
    _LOG_TYPE_ERROR, 
    _LOG_TYPE_SUCCESS 
} _log_type;

/** Command-line flags enum */
typedef enum {
    CL_INPUT,
    CL_OUTPUT,
    CL_HEADER,
    CL_SEQUENCE,
    CL_VIDEO,
    CL_NO_NORMALS,
    CL_TEXTURE_SIZE,
    CL_START_FRAME,
    CL_END_FRAME,
    CL_HELP,
    CL_MAX
} cl_flag_enum_t;

/** Command-line flags structure */
typedef struct cl_flag_t {
    const char* long_str;
    const char* short_str;
    const char* help_str;
    int n_required_args;
} cl_flag_t;

/** Color formatting for console output */
static const char* STRC_DEFAULT = "\x1B[0m";
static const char* STRC_RED     = "\x1B[31m";
static const char* STRC_GREEN   = "\x1B[32m";
static const char* STRC_YELLOW  = "\x1B[33m";

/** Command line flags definition */
static cl_flag_t _cl_flags[CL_MAX] = {
    { "--input", "-i", "Input vols file (for single-file volograms).\n", 1 },
    { "--output", "-o", "Output vols file path.\n", 1 },
    { "--header", "-h", "Header file (for multi-file volograms).\n", 1 },
    { "--sequence", "-s", "Sequence file (for multi-file volograms).\n", 1 },
    { "--video", "-v", "Video texture file (for multi-file volograms).\n", 1 },
    { "--no-normals", "-n", "Remove normals from the output vologram.\n", 0 },
    { "--texture-size", "-t", "Resize texture to specified resolution (e.g., 512x512).\n", 1 },
    { "--start-frame", "-sf", "Start frame for trimming (0-based, inclusive). Audio automatically trimmed to match.\n", 1 },
    { "--end-frame", "-ef", "End frame for trimming (0-based, inclusive). Audio automatically trimmed to match.\n", 1 },
    { "--help", NULL, "Show this help message.\n", 0 }
};

/// Globals for command line parsing
static int my_argc;
static char** my_argv;
static int _option_arg_indices[CL_MAX];

// Input/output filenames
static char* _input_filename;
static char* _input_header_filename;
static char* _input_sequence_filename;
static char* _input_video_filename;
static char* _output_filename;

// Processing options
static bool _no_normals = false;
static int _texture_width = 0;   // 0 means no resizing
static int _texture_height = 0;  // 0 means no resizing
static int _start_frame = -1;    // -1 means no start frame limit
static int _end_frame = -1;      // -1 means no end frame limit

// Vol data structures
static vol_geom_info_t _geom_info;
static vol_av_video_t _av_info;

// Working memory for keyframe data
static vol_geom_frame_data_t _key_frame_data;

// Timing statistics for texture processing
static double _total_texture_processing_time_ms = 0.0;
static uint32_t _texture_processing_frame_count = 0;

// Structure to cache processed texture data
typedef struct {
    uint8_t* data;
    uint32_t size;
    uint32_t width;
    uint32_t height;
    bool processed;
} processed_texture_cache_t;

/**
 * Calculate mesh data size based on version and frame data
 * 
 * @param geom_info           Geometry info
 * @param frame_data          Frame data
 * @param is_keyframe         Whether this is a keyframe
 * @param processed_texture_size Size of processed texture data (0 if no texture)
 * @return                    Calculated mesh data size
 */
static uint32_t _calculate_mesh_data_size( const vol_geom_info_t* geom_info,
                                          const vol_geom_frame_data_t* frame_data,
                                          bool is_keyframe,
                                          uint32_t processed_texture_size ) {
    // V10, 11 = Size of Vertices Data + Normals Data + Indices Data + UVs Data + Texture Data (WITHOUT size fields)
    // V12+ = Size of Vertices Data + Normals Data + Indices Data + UVs Data + Texture Data + 4 Bytes for each "Size of Array"
    uint32_t mesh_data_sz = frame_data->vertices_sz; // vertices data
    
    // Add normals if not removing them and version >= 11
    if ( !_no_normals && geom_info->hdr.version >= 11 && geom_info->hdr.normals ) {
        mesh_data_sz += frame_data->normals_sz; // normals data
    }
    
    // Add keyframe-specific data
    if ( is_keyframe ) {
        mesh_data_sz += frame_data->indices_sz; // indices data
        mesh_data_sz += frame_data->uvs_sz;     // UVs data
    }
    
    // Add texture data if present
    if ( geom_info->hdr.version >= 11 && geom_info->hdr.textured && frame_data->texture_sz > 0 ) {
        mesh_data_sz += processed_texture_size; // texture data
    }
    
    // For version 12+, add size fields to mesh_data_sz
    if ( geom_info->hdr.version >= 12 ) {
        mesh_data_sz += sizeof( uint32_t ); // vertices size field
        if ( !_no_normals && geom_info->hdr.normals ) {
            mesh_data_sz += sizeof( uint32_t ); // normals size field
        }
        if ( is_keyframe ) {
            mesh_data_sz += 2 * sizeof( uint32_t ); // indices + UVs size fields
        }
        if ( geom_info->hdr.textured && frame_data->texture_sz > 0 ) {
            mesh_data_sz += sizeof( uint32_t ); // texture size field
        }
    }
    
    return mesh_data_sz;
}

/**
 * Print log messages with color formatting
 */
 static void _printlog( _log_type log_type, const char* message_str, ... ) {
    FILE* stream_ptr = stdout;
    if ( _LOG_TYPE_ERROR == log_type ) {
        stream_ptr = stderr;
        fprintf( stderr, "%s", STRC_RED );
    } else if ( _LOG_TYPE_WARNING == log_type ) {
        stream_ptr = stderr;
        fprintf( stderr, "%s", STRC_YELLOW );
    } else if ( _LOG_TYPE_SUCCESS == log_type ) {
        fprintf( stderr, "%s", STRC_GREEN );
    }
    va_list arg_ptr;
    va_start( arg_ptr, message_str );
    vfprintf( stream_ptr, message_str, arg_ptr );
    va_end( arg_ptr );
    fprintf( stream_ptr, "%s", STRC_DEFAULT );
}

/**
 * Print all available command line flags
 */
static void _print_cl_flags( void ) {
    printf( "Options:\n" );
    for ( int i = 0; i < CL_MAX; i++ ) {
        if ( _cl_flags[i].long_str ) { printf( "%s", _cl_flags[i].long_str ); }
        if ( _cl_flags[i].long_str && _cl_flags[i].short_str ) { printf( ", " ); }
        if ( _cl_flags[i].short_str ) { printf( "%s", _cl_flags[i].short_str ); }
        if ( _cl_flags[i].long_str || _cl_flags[i].short_str ) { printf( "\n" ); }
        if ( _cl_flags[i].help_str ) { printf( "%s\n", _cl_flags[i].help_str ); }
    }
}

/**
 * Check if a command line option matches
 */
static bool _check_cl_option( int argv_idx, const char* long_str, const char* short_str ) {
    if ( long_str && ( 0 == strcasecmp( long_str, my_argv[argv_idx] ) ) ) { return true; }
    if ( short_str && ( 0 == strcasecmp( short_str, my_argv[argv_idx] ) ) ) { return true; }
    return false;
}

/**
 * Parse and validate command line arguments
 */
static bool _evaluate_params( int start_from_arg_idx ) {
    for ( int argv_idx = start_from_arg_idx; argv_idx < my_argc; argv_idx++ ) {
        bool found_valid_arg = false;
        
        if ( '-' != my_argv[argv_idx][0] ) {
            _printlog( _LOG_TYPE_WARNING, "Argument '%s' is an invalid option. Perhaps a '-' is missing? Run with --help for details.\n", my_argv[argv_idx] );
            return false;
        }
        
        for ( int clo_idx = 0; clo_idx < CL_MAX; clo_idx++ ) {
            if ( !_check_cl_option( argv_idx, _cl_flags[clo_idx].long_str, _cl_flags[clo_idx].short_str ) ) { 
                continue; 
            }
            
            // Check for required arguments
            if ( _cl_flags[clo_idx].n_required_args > 0 ) {
                for ( int following_idx = 1; following_idx < _cl_flags[clo_idx].n_required_args + 1; following_idx++ ) {
                    if ( argv_idx + _cl_flags[clo_idx].n_required_args >= my_argc || 
                         '-' == my_argv[argv_idx + following_idx][0] ) {
                        _printlog( _LOG_TYPE_WARNING, "Argument '%s' is not followed by a valid parameter. Run with --help for details.\n", my_argv[argv_idx] );
                        return false;
                    }
                }
            }
            
            _option_arg_indices[clo_idx] = argv_idx;
            argv_idx += _cl_flags[clo_idx].n_required_args;
            found_valid_arg = true;
            break;
        }
        
        if ( !found_valid_arg ) {
            _printlog( _LOG_TYPE_WARNING, "Argument '%s' is an unknown option. Run with --help for details.\n", my_argv[argv_idx] );
            return false;
        }
    }
    return true;
}
  
// Manual bilinear interpolation removed - now using BASIS Universal encoder's built-in high-quality resampling

/**
 * Process texture data - decode, resize if needed, and prepare for output
 * 
 * @param texture_data         Input texture data
 * @param texture_size         Size of input texture data
 * @param geom_info           Geometry info containing texture format information
 * @param output_data_ptr     Pointer to store output texture data (will be allocated)
 * @param output_size_ptr     Pointer to store output texture size
 * @param output_width_ptr    Pointer to store output texture width
 * @param output_height_ptr   Pointer to store output texture height
 * @return                    True on success, false on error
 */
static bool _process_texture_data( const uint8_t* texture_data, uint32_t texture_size,
                                  const vol_geom_info_t* geom_info,
                                  uint8_t** output_data_ptr, uint32_t* output_size_ptr,
                                  uint32_t* output_width_ptr, uint32_t* output_height_ptr ) {
    if ( !texture_data || !texture_size || !geom_info || !output_data_ptr || !output_size_ptr ) {
        return false;
    }
    
    // Initialize output parameters
    *output_data_ptr = NULL;
    *output_size_ptr = 0;
    *output_width_ptr = geom_info->hdr.texture_width;
    *output_height_ptr = geom_info->hdr.texture_height;
    
    // Check if we need to resize texture
    bool need_resize = (_texture_width > 0 && _texture_height > 0) &&
                       ((uint32_t)_texture_width != geom_info->hdr.texture_width ||
                        (uint32_t)_texture_height != geom_info->hdr.texture_height);
    
    // If no resizing needed, just copy the data
    if ( !need_resize ) {
        *output_data_ptr = malloc( texture_size );
        if ( !*output_data_ptr ) {
            return false;
        }
        memcpy( *output_data_ptr, texture_data, texture_size );
        *output_size_ptr = texture_size;
        return true;
    }
    
    // For BASIS textures with resizing, use BASIS Universal encoder
    if ( geom_info->hdr.version >= 13 && geom_info->hdr.texture_container_format == 1 ) {
        // BASIS texture - decode to RGBA32
        const int rgba_buffer_size = 8192 * 8192 * 4; // Maximum texture size
        uint8_t* rgba_data = malloc( rgba_buffer_size );
        if ( !rgba_data ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to allocate memory for RGBA texture data\n" );
            return false;
        }
        
        // Transcode BASIS to RGBA32 (format 13)
        int src_width, src_height;
        
        // Start timing texture transcoding
        clock_t transcode_start_time = clock();
        
        if ( !vol_basis_transcode( 13, (void*)texture_data, texture_size, rgba_data, rgba_buffer_size, &src_width, &src_height ) ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to transcode BASIS texture\n" );
            free( rgba_data );
            return false;
        }
        
        // Calculate and log transcoding time
        clock_t transcode_end_time = clock();
        double transcode_time_ms = ((double)(transcode_end_time - transcode_start_time)) / CLOCKS_PER_SEC * 1000.0;
                
        // Use BASIS Universal encoder for resizing and re-encoding
        // This preserves BASIS format and uses high-quality resampling
        bool use_uastc = (geom_info->hdr.texture_compression == 2);  // 2 = UASTC, 1 = ETC1S
        
        // Start timing texture encoding
        clock_t encode_start_time = clock();
        
        if ( !basis_encode_texture_with_resize( rgba_data, src_width, src_height,
                                               _texture_width, _texture_height, use_uastc, true,
                                               output_data_ptr, output_size_ptr ) ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to encode resized texture to BASIS format\n" );
            free( rgba_data );
            return false;
        }
        
        // Calculate and log encoding time
        clock_t encode_end_time = clock();
        double encode_time_ms = ((double)(encode_end_time - encode_start_time)) / CLOCKS_PER_SEC * 1000.0;
        
        _printlog( _LOG_TYPE_INFO, "Texture encoding completed in %.2f ms (OpenCL: %s)\n", 
                  encode_time_ms, basis_encoder_opencl_available() ? "enabled" : "disabled" );
        
        free( rgba_data );
        
        // Update output dimensions
        *output_width_ptr = _texture_width;
        *output_height_ptr = _texture_height;
        
        return true;
    } else {
        // For non-BASIS textures, resizing is not supported - just copy the data
        _printlog( _LOG_TYPE_WARNING, "WARNING: Texture resizing is only supported for BASIS textures in version 13+ volograms\n" );
        *output_data_ptr = malloc( texture_size );
        if ( !*output_data_ptr ) {
            return false;
        }
        memcpy( *output_data_ptr, texture_data, texture_size );
        *output_size_ptr = texture_size;
        return true;
    }
}

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
static int _read_memory_buffer(void* opaque, uint8_t* buf, int buf_size) {
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
static int64_t _seek_memory_buffer(void* opaque, int64_t offset, int whence) {
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

static int _write_output_buffer(void* opaque, uint8_t* buf, int buf_size) {
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
static bool _process_audio_data( const uint8_t* audio_data, uint32_t audio_size,
                                float fps, int start_frame, int end_frame,
                                uint8_t** output_data_ptr, uint32_t* output_size_ptr ) {
    if ( !audio_data || !audio_size || fps <= 0 || start_frame < 0 || end_frame < start_frame || 
         !output_data_ptr || !output_size_ptr ) {
        return false;
    }
    
    // Initialize output parameters
    *output_data_ptr = NULL;
    *output_size_ptr = 0;
    
    // Calculate timing
    double start_time = (double)start_frame / fps;
    double end_time = (double)(end_frame + 1) / fps;  // +1 to include the end frame
    
    _printlog( _LOG_TYPE_INFO, "Trimming audio from %.3f to %.3f seconds (frames %d to %d)\n", 
              start_time, end_time, start_frame, end_frame );
    
    // Set up input memory buffer
    memory_buffer_t input_mem_buf = { (uint8_t*)audio_data, audio_size, 0 };
    
    // Create custom I/O context for input
    uint8_t* avio_buffer = av_malloc(4096);
    if (!avio_buffer) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to allocate AVIO buffer\n" );
        return false;
    }
    
    AVIOContext* input_avio = avio_alloc_context(avio_buffer, 4096, 0, &input_mem_buf, 
                                                 _read_memory_buffer, NULL, _seek_memory_buffer);
    if (!input_avio) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to create input AVIO context\n" );
        av_free(avio_buffer);
        return false;
    }
    
    // Create input format context
    AVFormatContext* input_fmt_ctx = avformat_alloc_context();
    if (!input_fmt_ctx) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to allocate input format context\n" );
        avio_context_free(&input_avio);
        return false;
    }
    
    input_fmt_ctx->pb = input_avio;
    
    // Open input
    if (avformat_open_input(&input_fmt_ctx, NULL, NULL, NULL) < 0) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to open input audio stream\n" );
        avformat_free_context(input_fmt_ctx);
        avio_context_free(&input_avio);
        return false;
    }
    
    // Find stream info
    if (avformat_find_stream_info(input_fmt_ctx, NULL) < 0) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to find stream info\n" );
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
        _printlog( _LOG_TYPE_ERROR, "ERROR: No audio stream found\n" );
        avformat_close_input(&input_fmt_ctx);
        avio_context_free(&input_avio);
        return false;
    }
    
    // Set up output buffer
    output_buffer_t output_buf = { NULL, 0, 0 };
    
    // Create custom I/O context for output
    uint8_t* output_avio_buffer = av_malloc(4096);
    if (!output_avio_buffer) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to allocate output AVIO buffer\n" );
        avformat_close_input(&input_fmt_ctx);
        avio_context_free(&input_avio);
        return false;
    }
    
    AVIOContext* output_avio = avio_alloc_context(output_avio_buffer, 4096, 1, &output_buf, 
                                                  NULL, _write_output_buffer, NULL);
    if (!output_avio) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to create output AVIO context\n" );
        av_free(output_avio_buffer);
        avformat_close_input(&input_fmt_ctx);
        avio_context_free(&input_avio);
        return false;
    }
    
    // Create output format context
    AVFormatContext* output_fmt_ctx = NULL;
    if (avformat_alloc_output_context2(&output_fmt_ctx, NULL, "mp3", NULL) < 0) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to create output format context\n" );
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
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to create output stream\n" );
        avformat_free_context(output_fmt_ctx);
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        avio_context_free(&input_avio);
        return false;
    }
    
    // Copy codec parameters
    if (avcodec_parameters_copy(output_stream->codecpar, input_stream->codecpar) < 0) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to copy codec parameters\n" );
        avformat_free_context(output_fmt_ctx);
        avio_context_free(&output_avio);
        avformat_close_input(&input_fmt_ctx);
        avio_context_free(&input_avio);
        return false;
    }
    
    // Write output header
    if (avformat_write_header(output_fmt_ctx, NULL) < 0) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to write output header\n" );
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
        _printlog( _LOG_TYPE_WARNING, "WARNING: Failed to seek to start time, processing from beginning\n" );
    }
    
    // Process packets
    AVPacket packet;
    av_init_packet(&packet);
    
    int64_t end_timestamp = av_rescale_q(end_time * AV_TIME_BASE, AV_TIME_BASE_Q, 
                                        input_stream->time_base);
    
    while (av_read_frame(input_fmt_ctx, &packet) >= 0) {
        if (packet.stream_index == audio_stream_idx) {
            // Check if packet is within our time range
            if (packet.pts != AV_NOPTS_VALUE) {
                if (packet.pts >= start_timestamp && packet.pts <= end_timestamp) {
                    // Adjust packet stream index and timestamps for output
                    packet.stream_index = 0;
                    av_packet_rescale_ts(&packet, input_stream->time_base, output_stream->time_base);
                    
                    // Write packet to output
                    if (av_write_frame(output_fmt_ctx, &packet) < 0) {
                        _printlog( _LOG_TYPE_WARNING, "WARNING: Failed to write audio packet\n" );
                    }
                } else if (packet.pts > end_timestamp) {
                    // We've passed our end time, stop processing
                    av_packet_unref(&packet);
                    break;
                }
            }
        }
        av_packet_unref(&packet);
    }
    
    // Write trailer
    av_write_trailer(output_fmt_ctx);
    
    // Set output data
    if (output_buf.size > 0) {
        *output_data_ptr = output_buf.data;
        *output_size_ptr = (uint32_t)output_buf.size;
        
        _printlog( _LOG_TYPE_INFO, "Successfully trimmed audio from %u bytes to %u bytes\n", 
                  audio_size, *output_size_ptr );
    } else {
        _printlog( _LOG_TYPE_ERROR, "ERROR: No output audio data generated\n" );
        if (output_buf.data) {
            free(output_buf.data);
        }
    }
    
    // Cleanup
    avformat_free_context(output_fmt_ctx);
    avio_context_free(&output_avio);
    avformat_close_input(&input_fmt_ctx);
    avio_context_free(&input_avio);
    
    return (*output_data_ptr != NULL);
}


/**
 * Write a vols file header to the output file
 */
static bool _write_vols_header( FILE* output_file, const vol_geom_file_hdr_t* hdr ) {
    if ( !output_file || !hdr ) {
        return false;
    }
    
    // Write format string - check if it's IFF-style (4 bytes) or Unity-style (size + string)
    if ( hdr->format.sz == 4 && 
         hdr->format.bytes[0] == 'V' && hdr->format.bytes[1] == 'O' && 
         hdr->format.bytes[2] == 'L' && hdr->format.bytes[3] == 'S' ) {
        // IFF-style VOLS magic number
        if ( 4 != fwrite( hdr->format.bytes, sizeof( char ), 4, output_file ) ) {
            return false;
        }
    } else {
        // Unity-style string with size prefix
        if ( 1 != fwrite( &hdr->format.sz, sizeof( uint8_t ), 1, output_file ) ) {
            return false;
        }
        if ( hdr->format.sz != fwrite( hdr->format.bytes, sizeof( char ), hdr->format.sz, output_file ) ) {
            return false;
        }
    }
    
    // Write header fields
    if ( 1 != fwrite( &hdr->version, sizeof( uint32_t ), 1, output_file ) ) return false;
    if ( 1 != fwrite( &hdr->compression, sizeof( uint32_t ), 1, output_file ) ) return false;
    
    // Write mesh_name, material, shader if version < 13
    if ( hdr->version < 13 ) {
        if ( 1 != fwrite( &hdr->mesh_name.sz, sizeof( uint8_t ), 1, output_file ) ) return false;
        if ( hdr->mesh_name.sz != fwrite( hdr->mesh_name.bytes, sizeof( char ), hdr->mesh_name.sz, output_file ) ) return false;
        
        if ( 1 != fwrite( &hdr->material.sz, sizeof( uint8_t ), 1, output_file ) ) return false;
        if ( hdr->material.sz != fwrite( hdr->material.bytes, sizeof( char ), hdr->material.sz, output_file ) ) return false;
        
        if ( 1 != fwrite( &hdr->shader.sz, sizeof( uint8_t ), 1, output_file ) ) return false;
        if ( hdr->shader.sz != fwrite( hdr->shader.bytes, sizeof( char ), hdr->shader.sz, output_file ) ) return false;
        
        if ( 1 != fwrite( &hdr->topology, sizeof( uint32_t ), 1, output_file ) ) return false;
    }
    
    if ( 1 != fwrite( &hdr->frame_count, sizeof( uint32_t ), 1, output_file ) ) return false;
    
    // Write normals and textured flags if version >= 11 (not 12!)
    if ( hdr->version >= 11 ) {
        uint8_t normals_flag = _no_normals ? 0 : hdr->normals;  // Apply no_normals modification
        if ( 1 != fwrite( &normals_flag, sizeof( uint8_t ), 1, output_file ) ) return false;
        if ( 1 != fwrite( &hdr->textured, sizeof( uint8_t ), 1, output_file ) ) return false;
    }
    
    // Write version 13 specific fields
    if ( hdr->version >= 13 ) {
        if ( 1 != fwrite( &hdr->texture_compression, sizeof( uint8_t ), 1, output_file ) ) return false;
        if ( 1 != fwrite( &hdr->texture_container_format, sizeof( uint8_t ), 1, output_file ) ) return false;
        if ( 1 != fwrite( &hdr->texture_width, sizeof( uint32_t ), 1, output_file ) ) return false;
        if ( 1 != fwrite( &hdr->texture_height, sizeof( uint32_t ), 1, output_file ) ) return false;
        if ( 1 != fwrite( &hdr->fps, sizeof( float ), 1, output_file ) ) return false;
        if ( 1 != fwrite( &hdr->audio, sizeof( uint32_t ), 1, output_file ) ) return false;
        if ( 1 != fwrite( &hdr->audio_start, sizeof( uint32_t ), 1, output_file ) ) return false;
        if ( 1 != fwrite( &hdr->frame_body_start, sizeof( uint32_t ), 1, output_file ) ) return false;
    } else if ( hdr->version >= 11 ) {
        // Write texture dimensions as uint16_t for versions < 13
        uint16_t w = (uint16_t)hdr->texture_width;
        uint16_t h = (uint16_t)hdr->texture_height;
        if ( 1 != fwrite( &w, sizeof( uint16_t ), 1, output_file ) ) return false;
        if ( 1 != fwrite( &h, sizeof( uint16_t ), 1, output_file ) ) return false;
        if ( 1 != fwrite( &hdr->texture_format, sizeof( uint16_t ), 1, output_file ) ) return false;
    }
    
    // Write translation, rotation, scale if version >= 12 and < 13
    if ( hdr->version >= 12 && hdr->version < 13 ) {
        if ( 3 != fwrite( hdr->translation, sizeof( float ), 3, output_file ) ) return false;
        if ( 4 != fwrite( hdr->rotation, sizeof( float ), 4, output_file ) ) return false;
        if ( 1 != fwrite( &hdr->scale, sizeof( float ), 1, output_file ) ) return false;
    }
    
    return true;
}

/**
 * Write a frame header to the output file
 */
static bool _write_frame_header( FILE* output_file, const vol_geom_frame_hdr_t* frame_hdr ) {
    if ( !output_file || !frame_hdr ) {
        return false;
    }
    
    if ( 1 != fwrite( &frame_hdr->frame_number, sizeof( uint32_t ), 1, output_file ) ) return false;
    if ( 1 != fwrite( &frame_hdr->mesh_data_sz, sizeof( uint32_t ), 1, output_file ) ) return false;
    if ( 1 != fwrite( &frame_hdr->keyframe, sizeof( uint8_t ), 1, output_file ) ) return false;
    
    return true;
}

/**
 * Write a frame body to the output file
 */
static bool _write_frame_body( FILE* output_file, const vol_geom_info_t* geom_info, 
                              const vol_geom_frame_data_t* frame_data, bool is_keyframe,
                              const processed_texture_cache_t* texture_cache ) {
    if ( !output_file || !geom_info || !frame_data ) {
        return false;
    }
    
    // Calculate total written size for debugging
    uint32_t total_written_sz = frame_data->vertices_sz;
    if ( !_no_normals && geom_info->hdr.version >= 11 && geom_info->hdr.normals ) {
        total_written_sz += frame_data->normals_sz;
    }
    if ( is_keyframe ) {
        total_written_sz += frame_data->indices_sz + frame_data->uvs_sz;
    }
    
    // Add texture data if present
    if ( geom_info->hdr.version >= 11 && geom_info->hdr.textured && frame_data->texture_sz > 0 ) {
        if ( texture_cache && texture_cache->processed ) {
            total_written_sz += sizeof( uint32_t ) + texture_cache->size;
        } else {
            total_written_sz += sizeof( uint32_t ) + frame_data->texture_sz;
        }
    }
    
    // Write vertices size and data
    if ( 1 != fwrite( &frame_data->vertices_sz, sizeof( uint32_t ), 1, output_file ) ) return false;
    if ( frame_data->vertices_sz != fwrite( &frame_data->block_data_ptr[frame_data->vertices_offset], 
                                           sizeof( uint8_t ), frame_data->vertices_sz, output_file ) ) return false;
    
    // Write normals size and data (only if not removing normals and version >= 11)
    if ( !_no_normals && geom_info->hdr.version >= 11 && geom_info->hdr.normals ) {
        if ( 1 != fwrite( &frame_data->normals_sz, sizeof( uint32_t ), 1, output_file ) ) return false;
        if ( frame_data->normals_sz > 0 && frame_data->normals_sz != fwrite( &frame_data->block_data_ptr[frame_data->normals_offset], 
                                                                           sizeof( uint8_t ), frame_data->normals_sz, output_file ) ) return false;
    }
    
    // Write keyframe-specific data
    if ( is_keyframe ) {
        // Write indices size and data
        if ( 1 != fwrite( &frame_data->indices_sz, sizeof( uint32_t ), 1, output_file ) ) return false;
        if ( frame_data->indices_sz != fwrite( &frame_data->block_data_ptr[frame_data->indices_offset], 
                                              sizeof( uint8_t ), frame_data->indices_sz, output_file ) ) return false;
        
        // Write UVs size and data
        if ( 1 != fwrite( &frame_data->uvs_sz, sizeof( uint32_t ), 1, output_file ) ) return false;
        if ( frame_data->uvs_sz != fwrite( &frame_data->block_data_ptr[frame_data->uvs_offset], 
                                          sizeof( uint8_t ), frame_data->uvs_sz, output_file ) ) return false;
    }
    
    // Write texture size and data if present (using cached data)
    if ( geom_info->hdr.version >= 11 && geom_info->hdr.textured && frame_data->texture_sz > 0 ) {
        if ( texture_cache && texture_cache->processed ) {
            // Use cached processed texture data
            if ( 1 != fwrite( &texture_cache->size, sizeof( uint32_t ), 1, output_file ) ) return false;
            if ( texture_cache->size != fwrite( texture_cache->data, sizeof( uint8_t ), texture_cache->size, output_file ) ) return false;
        }
    }
    
    // Write trailing mesh data size using the helper function
    uint32_t texture_size_for_calculation = 0;
    if ( texture_cache && texture_cache->processed ) {
        texture_size_for_calculation = texture_cache->size;
    } else if ( geom_info->hdr.version >= 11 && geom_info->hdr.textured && frame_data->texture_sz > 0 ) {
        texture_size_for_calculation = frame_data->texture_sz;
    }
    
    uint32_t trailing_mesh_data_sz = _calculate_mesh_data_size( geom_info, frame_data, is_keyframe, texture_size_for_calculation );
    if ( 1 != fwrite( &trailing_mesh_data_sz, sizeof( uint32_t ), 1, output_file ) ) return false;
    
    return true;
}

/**
 * Process the vologram and write it to the output file
 */
static bool _process_vologram( void ) {
    bool use_vol_av = false;
    
    // Open geometry file
    if ( _input_filename ) {
        if ( !vol_geom_create_file_info_from_file( _input_filename, &_geom_info ) ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to open combined vologram file=%s.\n", _input_filename );
            return false;
        }
    } else if ( !vol_geom_create_file_info( _input_header_filename, _input_sequence_filename, &_geom_info, true ) ) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to open geometry files header=%s sequence=%s.\n",
                  _input_header_filename, _input_sequence_filename );
        return false;
    }
    
    // Debug output: Show what was read from the file
    _printlog( _LOG_TYPE_INFO, "=== INPUT FILE DEBUG INFO ===\n" );
    _printlog( _LOG_TYPE_INFO, "File format: %.*s\n", _geom_info.hdr.format.sz, _geom_info.hdr.format.bytes );
    _printlog( _LOG_TYPE_INFO, "Version: %u\n", _geom_info.hdr.version );
    _printlog( _LOG_TYPE_INFO, "Compression: %u\n", _geom_info.hdr.compression );
    _printlog( _LOG_TYPE_INFO, "Total frames: %u\n", _geom_info.hdr.frame_count );
    
    if ( _geom_info.hdr.version >= 11 ) {
        _printlog( _LOG_TYPE_INFO, "Has normals: %s\n", _geom_info.hdr.normals ? "yes" : "no" );
        _printlog( _LOG_TYPE_INFO, "Has texture: %s\n", _geom_info.hdr.textured ? "yes" : "no" );
        _printlog( _LOG_TYPE_INFO, "Texture dimensions: %ux%u\n", _geom_info.hdr.texture_width, _geom_info.hdr.texture_height );
        
        if ( _geom_info.hdr.version >= 13 ) {
            _printlog( _LOG_TYPE_INFO, "Texture compression: %u\n", _geom_info.hdr.texture_compression );
            _printlog( _LOG_TYPE_INFO, "Texture container format: %u\n", _geom_info.hdr.texture_container_format );
            _printlog( _LOG_TYPE_INFO, "FPS: %.2f\n", _geom_info.hdr.fps );
            _printlog( _LOG_TYPE_INFO, "Has audio: %s\n", _geom_info.hdr.audio ? "yes" : "no" );
            _printlog( _LOG_TYPE_INFO, "Audio start: %u\n", _geom_info.hdr.audio_start );
            _printlog( _LOG_TYPE_INFO, "Frame body start: %u\n", _geom_info.hdr.frame_body_start );
        } else {
            _printlog( _LOG_TYPE_INFO, "Texture format: %u\n", _geom_info.hdr.texture_format );
        }
    }
    
    if ( _geom_info.hdr.version >= 12 && _geom_info.hdr.version < 13 ) {
        _printlog( _LOG_TYPE_INFO, "Translation: [%.3f, %.3f, %.3f]\n", 
                  _geom_info.hdr.translation[0], _geom_info.hdr.translation[1], _geom_info.hdr.translation[2] );
        _printlog( _LOG_TYPE_INFO, "Rotation: [%.3f, %.3f, %.3f, %.3f]\n", 
                  _geom_info.hdr.rotation[0], _geom_info.hdr.rotation[1], _geom_info.hdr.rotation[2], _geom_info.hdr.rotation[3] );
        _printlog( _LOG_TYPE_INFO, "Scale: %.3f\n", _geom_info.hdr.scale );
    }
        
    // Validate and adjust frame range based on actual frame count
    uint32_t total_frames = _geom_info.hdr.frame_count;
    
    // Set default values if not specified
    if ( _start_frame < 0 ) {
        _start_frame = 0;
    }
    if ( _end_frame < 0 ) {
        _end_frame = (int)total_frames - 1;
    }
    
    // Limit to actual frame count
    if ( _start_frame >= (int)total_frames ) {
        _start_frame = (int)total_frames - 1;
        _printlog( _LOG_TYPE_WARNING, "WARNING: Start frame limited to %d (last frame)\n", _start_frame );
    }
    if ( _end_frame >= (int)total_frames ) {
        _end_frame = (int)total_frames - 1;
        _printlog( _LOG_TYPE_WARNING, "WARNING: End frame limited to %d (last frame)\n", _end_frame );
    }
    
    // Ensure start <= end after limiting
    if ( _start_frame > _end_frame ) {
        _start_frame = _end_frame;
        _printlog( _LOG_TYPE_WARNING, "WARNING: Start frame adjusted to %d to match end frame\n", _start_frame );
    }
    
    uint32_t export_frame_count = (uint32_t)(_end_frame - _start_frame + 1);
    
    if ( _start_frame > 0 || _end_frame < (int)total_frames - 1 ) {
        _printlog( _LOG_TYPE_INFO, "Frame range: %d to %d (exporting %u of %u frames)\n", 
                  _start_frame, _end_frame, export_frame_count, total_frames );
    }
    
    // Initialize libraries based on version
    if ( _geom_info.hdr.version < 13 ) {
        use_vol_av = true;
        if ( !vol_av_open( _input_video_filename, &_av_info ) ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to open video file %s.\n", _input_video_filename );
            return false;
        }
    } else {
        if ( !vol_basis_init() ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to initialise Basis transcoder.\n" );
            return false;
        }
    }
    
    // Create output file
    FILE* output_file = fopen( _output_filename, "wb" );
    if ( !output_file ) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to create output file %s.\n", _output_filename );
        return false;
    }
    
    // Process audio data first to determine final audio size
    uint8_t* processed_audio_data = NULL;
    uint32_t processed_audio_size = 0;
    bool audio_allocated = false;
    
    if ( _geom_info.hdr.audio && _geom_info.audio_data_ptr ) {
        // Process audio if frame range is specified (automatic audio trimming)
        if ( _start_frame > 0 || _end_frame < (int)_geom_info.hdr.frame_count - 1 ) {
            _printlog( _LOG_TYPE_INFO, "Automatically trimming audio to match frame range %d to %d\n", _start_frame, _end_frame );
            
            if ( _process_audio_data( _geom_info.audio_data_ptr, _geom_info.audio_data_sz,
                                     _geom_info.hdr.fps, _start_frame, _end_frame,
                                     &processed_audio_data, &processed_audio_size ) ) {
                audio_allocated = true;
                _printlog( _LOG_TYPE_SUCCESS, "Successfully trimmed audio to match frames\n" );
            } else {
                _printlog( _LOG_TYPE_WARNING, "WARNING: Failed to trim audio, using original\n" );
                processed_audio_data = _geom_info.audio_data_ptr;
                processed_audio_size = _geom_info.audio_data_sz;
            }
        } else {
            // Use original audio data (no frame trimming)
            processed_audio_data = _geom_info.audio_data_ptr;
            processed_audio_size = _geom_info.audio_data_sz;
        }
    }
    
    // Write header with modifications
    vol_geom_file_hdr_t modified_hdr = _geom_info.hdr;
    
    // Update frame count for range selection
    modified_hdr.frame_count = export_frame_count;
    
    // Update texture dimensions if resizing is requested
    if ( _texture_width > 0 && _texture_height > 0 && modified_hdr.textured ) {
        modified_hdr.texture_width = _texture_width;
        modified_hdr.texture_height = _texture_height;
        
        // For BASIS textures, preserve the original format - don't change to raw
        // The BASIS encoder will output proper BASIS format with new dimensions
        if ( modified_hdr.version >= 13 && modified_hdr.texture_container_format == 1 ) {
            _printlog( _LOG_TYPE_INFO, "Texture will be resized to %dx%d while preserving BASIS format\n", 
                      _texture_width, _texture_height );
        }
    }
    
    // Calculate correct frame_body_start offset for version 13+ with audio
    if ( modified_hdr.version >= 13 && _geom_info.hdr.audio && _geom_info.audio_data_ptr ) {
        // Calculate v13 header size: 
        // format(4) + version(4) + compression(4) + frame_count(4) + normals(1) + textured(1) +
        // texture_compression(1) + texture_container_format(1) + texture_width(4) + texture_height(4) +
        // fps(4) + audio(4) + audio_start(4) + frame_body_start(4) = 44 bytes
        uint32_t header_size = 44;
        
        // Frame body starts after: header + audio_size_field + audio_data
        modified_hdr.frame_body_start = header_size + sizeof(uint32_t) + processed_audio_size;
        
        // Update audio_start to point right after header
        modified_hdr.audio_start = header_size;
        
        _printlog( _LOG_TYPE_INFO, "Audio processing: original size %u -> processed size %u\n", 
                  _geom_info.audio_data_sz, processed_audio_size );
        _printlog( _LOG_TYPE_INFO, "Updated header offsets - audio_start: %u, frame_body_start: %u\n", 
                  modified_hdr.audio_start, modified_hdr.frame_body_start );
    }
    
    if ( !_write_vols_header( output_file, &modified_hdr ) ) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to write output file header.\n" );
        if ( audio_allocated ) free( processed_audio_data );
        fclose( output_file );
        return false;
    }
    
    long header_end_pos = ftell( output_file );
    _printlog( _LOG_TYPE_INFO, "Header written, file position: %ld\n", header_end_pos );
    
    // Write audio data if present (already processed above)
    if ( _geom_info.hdr.audio && _geom_info.audio_data_ptr ) {
        _printlog( _LOG_TYPE_INFO, "Writing audio data to file...\n" );
        // Write processed audio data
        if ( 1 != fwrite( &processed_audio_size, sizeof( uint32_t ), 1, output_file ) ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to write audio data size.\n" );
            if ( audio_allocated ) free( processed_audio_data );
            fclose( output_file );
            return false;
        }
        if ( processed_audio_size != fwrite( processed_audio_data, sizeof( uint8_t ), 
                                            processed_audio_size, output_file ) ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to write audio data.\n" );
            if ( audio_allocated ) free( processed_audio_data );
            fclose( output_file );
            return false;
        }
        
        // Free processed audio data if it was allocated
        if ( audio_allocated ) {
            free( processed_audio_data );
        }
        
        long audio_end_pos = ftell( output_file );
        _printlog( _LOG_TYPE_INFO, "Audio data written, file position: %ld (should match frame_body_start: %u)\n", 
                  audio_end_pos, modified_hdr.frame_body_start );
    } else {
        _printlog( _LOG_TYPE_INFO, "No audio data to write\n" );
    }
    
    // Process each frame in the selected range
    const char* sequence_filename = _input_filename ? _input_filename : _input_sequence_filename;
    
    for ( uint32_t output_frame_idx = 0; output_frame_idx < export_frame_count; output_frame_idx++ ) {
        uint32_t input_frame_idx = (uint32_t)(_start_frame + output_frame_idx);

        // Read current frame
        vol_geom_frame_data_t frame_data;
        if ( !vol_geom_read_frame( sequence_filename, &_geom_info, input_frame_idx, &frame_data ) ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Reading geometry frame %i.\n", input_frame_idx );
            fclose( output_file );
            return false;
        }

        bool is_keyframe = vol_geom_is_keyframe( &_geom_info, input_frame_idx );
        
        // Process texture data once per frame and cache results
        processed_texture_cache_t texture_cache = { NULL, 0, 0, 0, false };
        if ( _geom_info.hdr.version >= 11 && _geom_info.hdr.textured && frame_data.texture_sz > 0 ) {
            // Start timing overall texture processing
            clock_t texture_start_time = clock();
            
            if ( !_process_texture_data( &frame_data.block_data_ptr[frame_data.texture_offset], 
                                        frame_data.texture_sz, &_geom_info,
                                        &texture_cache.data, &texture_cache.size,
                                        &texture_cache.width, &texture_cache.height ) ) {
                _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to process texture data for frame %i (input frame %u)\n", output_frame_idx, input_frame_idx );
                fclose( output_file );
                return false;
            }
            texture_cache.processed = true;
            
            // Calculate and log total texture processing time
            clock_t texture_end_time = clock();
            double texture_time_ms = ((double)(texture_end_time - texture_start_time)) / CLOCKS_PER_SEC * 1000.0;
            
            // Accumulate timing statistics
            _total_texture_processing_time_ms += texture_time_ms;
            _texture_processing_frame_count++;
            
            // _printlog( _LOG_TYPE_INFO, "Frame %u texture processing completed in %.2f ms total\n", 
            //           output_frame_idx, texture_time_ms );
        }
        
        // Special handling for first frame in range - must be a keyframe
        uint8_t* allocated_block = NULL;  // Track allocated memory for cleanup
        if ( (output_frame_idx == 0 || output_frame_idx == export_frame_count-1) && !is_keyframe ) {

            int key_idx = vol_geom_find_previous_keyframe( &_geom_info, input_frame_idx );

            // Load keyframe data if needed
            if ( !vol_geom_read_frame( sequence_filename, &_geom_info, key_idx, &_key_frame_data ) ) {
                _printlog( _LOG_TYPE_ERROR, "ERROR: Reading geometry keyframe %i.\n", key_idx );
                if ( texture_cache.data ) free( texture_cache.data );
                fclose( output_file );
                return false;
            }

            // Create a new combined data block:
            // - Vertices and normals from current frame
            // - Indices and UVs from keyframe
            // - Texture from current frame
            
            // Calculate sizes for the new block
            uint32_t vertices_size = sizeof(uint32_t) + frame_data.vertices_sz;  // size field + data
            uint32_t normals_size = 0;
            if ( _geom_info.hdr.normals && _geom_info.hdr.version >= 11 ) {
                normals_size = sizeof(uint32_t) + frame_data.normals_sz;  // size field + data
            }
            uint32_t indices_size = sizeof(uint32_t) + _key_frame_data.indices_sz;  // size field + data
            uint32_t uvs_size = sizeof(uint32_t) + _key_frame_data.uvs_sz;  // size field + data
            uint32_t texture_size = 0;
            if ( _geom_info.hdr.textured && _geom_info.hdr.version >= 11 ) {
                texture_size = sizeof(uint32_t) + texture_cache.size;  // size field + data
            }
            
            uint32_t total_size = vertices_size + normals_size + indices_size + uvs_size + texture_size;
            
            // Allocate new block
            allocated_block = malloc( total_size );
            if ( !allocated_block ) {
                _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to allocate memory for keyframe conversion\n" );
                if ( texture_cache.data ) free( texture_cache.data );
                fclose( output_file );
                return false;
            }

            uint32_t offset = 0;
            
            // Copy vertices (size + data)
            memcpy( &allocated_block[offset], &frame_data.vertices_sz, sizeof(uint32_t) );
            offset += sizeof(uint32_t);
            memcpy( &allocated_block[offset], &frame_data.block_data_ptr[frame_data.vertices_offset], frame_data.vertices_sz );
            frame_data.vertices_offset = offset;
            offset += frame_data.vertices_sz;
            
            // Copy normals if present (size + data)
            if ( normals_size > 0 ) {
                memcpy( &allocated_block[offset], &frame_data.normals_sz, sizeof(uint32_t) );
                offset += sizeof(uint32_t);
                memcpy( &allocated_block[offset], &frame_data.block_data_ptr[frame_data.normals_offset], frame_data.normals_sz );
                frame_data.normals_offset = offset;
                offset += frame_data.normals_sz;
            }
            
            // Copy indices from keyframe (size + data)
            memcpy( &allocated_block[offset], &_key_frame_data.indices_sz, sizeof(uint32_t) );
            offset += sizeof(uint32_t);
            memcpy( &allocated_block[offset], &_key_frame_data.block_data_ptr[_key_frame_data.indices_offset], _key_frame_data.indices_sz );
            frame_data.indices_sz = _key_frame_data.indices_sz;
            frame_data.indices_offset = offset;
            offset += _key_frame_data.indices_sz;
            
            // Copy UVs from keyframe (size + data)
            memcpy( &allocated_block[offset], &_key_frame_data.uvs_sz, sizeof(uint32_t) );
            offset += sizeof(uint32_t);
            memcpy( &allocated_block[offset], &_key_frame_data.block_data_ptr[_key_frame_data.uvs_offset], _key_frame_data.uvs_sz );
            frame_data.uvs_sz = _key_frame_data.uvs_sz;
            frame_data.uvs_offset = offset;
            offset += _key_frame_data.uvs_sz;

            // Copy texture if present (size + data)
            if ( texture_size > 0 ) {
                memcpy( &allocated_block[offset], &texture_cache.size, sizeof(uint32_t) );
                offset += sizeof(uint32_t);
                memcpy( &allocated_block[offset], texture_cache.data, texture_cache.size );
                frame_data.texture_offset = offset;
                offset += texture_cache.size;
            }
            
            // Update frame_data to use the new block
            frame_data.block_data_ptr = allocated_block;
            frame_data.block_data_sz = total_size;
            
            is_keyframe = true;
        }

        // Create modified frame header using cached texture size
        vol_geom_frame_hdr_t modified_frame_hdr = _geom_info.frame_headers_ptr[input_frame_idx];
        
        // Update frame number to be sequential starting from 0 for the exported range
        modified_frame_hdr.frame_number = output_frame_idx;
        
        if ( output_frame_idx == 0 && !modified_frame_hdr.keyframe) {
            modified_frame_hdr.keyframe = 1;
        } else if ( output_frame_idx == export_frame_count-1 && !modified_frame_hdr.keyframe) {
            modified_frame_hdr.keyframe = 2;
        }
        
        uint32_t new_mesh_data_sz = _calculate_mesh_data_size( &_geom_info, &frame_data, is_keyframe, texture_cache.size );
        modified_frame_hdr.mesh_data_sz = new_mesh_data_sz;

        // Write frame header
        if ( !_write_frame_header( output_file, &modified_frame_hdr ) ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to write frame header for frame %i (input frame %u).\n", output_frame_idx, input_frame_idx );
            if ( texture_cache.data ) free( texture_cache.data );
            if ( allocated_block ) free( allocated_block );
            fclose( output_file );
            return false;
        }
        
        // Write frame body using cached texture data
        if ( !_write_frame_body( output_file, &_geom_info, &frame_data, is_keyframe, &texture_cache ) ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to write frame body for frame %i (input frame %u).\n", output_frame_idx, input_frame_idx );
            if ( texture_cache.data ) free( texture_cache.data );
            if ( allocated_block ) free( allocated_block );
            fclose( output_file );
            return false;
        }
        
        // Free cached texture data
        if ( texture_cache.data ) {
            free( texture_cache.data );
        }
        
        // Free allocated keyframe conversion block
        if ( allocated_block ) {
            free( allocated_block );
        }
        
        _printlog( _LOG_TYPE_INFO, "Processed frame %i/%i (input frame %u)\n", output_frame_idx + 1, export_frame_count, input_frame_idx );
    }
    
    fclose( output_file );
    
    // Cleanup
    if ( use_vol_av ) {
        vol_av_close( &_av_info );
    }
    
    if ( !vol_geom_free_file_info( &_geom_info ) ) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to free geometry info.\n" );
        return false;
    }

    return true;
}

int main( int argc, char** argv ) {
    my_argc = argc;
    my_argv = argv;
    
    // Initialize option indices
    for ( int i = 0; i < CL_MAX; i++ ) {
        _option_arg_indices[i] = 0;
    }
    
    // Parse command line arguments
    if ( !_evaluate_params( 1 ) ) {
        return 1;
    }
    
    // Check for help
    if ( argc < 2 || _option_arg_indices[CL_HELP] ) {
        printf( "Usage:\n" );
        printf( "  For single-file volograms:\n" );
        printf( "    %s -i INPUT.VOLS -o OUTPUT.VOLS [OPTIONS]\n\n", argv[0] );
        printf( "  For multi-file volograms:\n" );
        printf( "    %s -h HEADER.VOLS -s SEQUENCE.VOLS -v VIDEO.MP4 -o OUTPUT.VOLS [OPTIONS]\n\n", argv[0] );
        _print_cl_flags();
        return 0;
    }
    
    // Validate required arguments
    if ( !_option_arg_indices[CL_OUTPUT] ) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Output file (-o) is required.\n" );
        return 1;
    }
    
    _output_filename = my_argv[_option_arg_indices[CL_OUTPUT] + 1];
    
    // Check input mode
    if ( _option_arg_indices[CL_INPUT] ) {
        _input_filename = my_argv[_option_arg_indices[CL_INPUT] + 1];
    } else {
        // Multi-file mode
        if ( !_option_arg_indices[CL_HEADER] || !_option_arg_indices[CL_SEQUENCE] || !_option_arg_indices[CL_VIDEO] ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: For multi-file mode, header (-h), sequence (-s), and video (-v) are required.\n" );
            return 1;
        }
        _input_header_filename = my_argv[_option_arg_indices[CL_HEADER] + 1];
        _input_sequence_filename = my_argv[_option_arg_indices[CL_SEQUENCE] + 1];
        _input_video_filename = my_argv[_option_arg_indices[CL_VIDEO] + 1];
    }
    
    // Set processing options
    _no_normals = _option_arg_indices[CL_NO_NORMALS] > 0;
    
    // Parse texture size option
    if ( _option_arg_indices[CL_TEXTURE_SIZE] ) {
        const char* texture_size_str = my_argv[_option_arg_indices[CL_TEXTURE_SIZE] + 1];
        if ( sscanf( texture_size_str, "%dx%d", &_texture_width, &_texture_height ) != 2 ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Invalid texture size format '%s'. Use WIDTHxHEIGHT (e.g., 512x512).\n", texture_size_str );
            return 1;
        }
        if ( _texture_width <= 0 || _texture_height <= 0 ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Texture dimensions must be positive integers.\n" );
            return 1;
        }
        if ( _texture_width > 8192 || _texture_height > 8192 ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Texture dimensions cannot exceed 8192x8192.\n" );
            return 1;
        }
        _printlog( _LOG_TYPE_INFO, "Texture will be resized to %dx%d\n", _texture_width, _texture_height );
    }
    
    // Parse start frame option
    if ( _option_arg_indices[CL_START_FRAME] ) {
        const char* start_frame_str = my_argv[_option_arg_indices[CL_START_FRAME] + 1];
        if ( sscanf( start_frame_str, "%d", &_start_frame ) != 1 ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Invalid start frame format '%s'. Use integer (e.g., 10).\n", start_frame_str );
            return 1;
        }
        if ( _start_frame < 0 ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Start frame must be non-negative.\n" );
            return 1;
        }
    }
    
    // Parse end frame option
    if ( _option_arg_indices[CL_END_FRAME] ) {
        const char* end_frame_str = my_argv[_option_arg_indices[CL_END_FRAME] + 1];
        if ( sscanf( end_frame_str, "%d", &_end_frame ) != 1 ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Invalid end frame format '%s'. Use integer (e.g., 100).\n", end_frame_str );
            return 1;
        }
        if ( _end_frame < 0 ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: End frame must be non-negative.\n" );
            return 1;
        }
    }
    
    // Validate frame range
    if ( _start_frame >= 0 && _end_frame >= 0 && _start_frame > _end_frame ) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Start frame (%d) must be less than or equal to end frame (%d).\n", _start_frame, _end_frame );
        return 1;
    }
    
    // Initialize BASIS Universal encoder if texture resizing is requested
    if ( _texture_width > 0 && _texture_height > 0 ) {
        // Enable OpenCL for faster texture encoding
        if ( !basis_encoder_init_wrapper(true) ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to initialize BASIS Universal encoder\n" );
            return 1;
        }
        
        // Check if OpenCL is available and report status
        if ( basis_encoder_opencl_available() ) {
            _printlog( _LOG_TYPE_SUCCESS, "OpenCL acceleration enabled for texture encoding\n" );
        } else {
            _printlog( _LOG_TYPE_WARNING, "OpenCL not available, using CPU-only texture encoding\n" );
        }
    }
    
    // Process the vologram
    if ( !_process_vologram() ) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to process vologram.\n" );
        return 1;
    }
    
    _printlog( _LOG_TYPE_SUCCESS, "Successfully converted vologram to %s", _output_filename );
    if ( _no_normals ) {
        _printlog( _LOG_TYPE_SUCCESS, " (normals removed)" );
    }
    _printlog( _LOG_TYPE_SUCCESS, "\n" );
    
    // Print texture processing performance summary
    if ( _texture_processing_frame_count > 0 ) {
        double avg_time_ms = _total_texture_processing_time_ms / _texture_processing_frame_count;
        _printlog( _LOG_TYPE_INFO, "\n=== TEXTURE PROCESSING PERFORMANCE SUMMARY ===\n" );
        _printlog( _LOG_TYPE_INFO, "Total frames processed: %u\n", _texture_processing_frame_count );
        _printlog( _LOG_TYPE_INFO, "Total processing time: %.2f ms\n", _total_texture_processing_time_ms );
        _printlog( _LOG_TYPE_INFO, "Average time per frame: %.2f ms\n", avg_time_ms );
        _printlog( _LOG_TYPE_INFO, "OpenCL acceleration: %s\n", basis_encoder_opencl_available() ? "enabled" : "disabled" );
        _printlog( _LOG_TYPE_INFO, "==============================================\n" );
    }
    
    return 0;
} 