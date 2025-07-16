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

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
typedef enum cl_flag_enum_t {
    CL_INPUT,
    CL_OUTPUT,
    CL_HEADER,
    CL_SEQUENCE,
    CL_VIDEO,
    CL_NO_NORMALS,
    CL_TEXTURE_SIZE,
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

// Vol data structures
static vol_geom_info_t _geom_info;
static vol_av_video_t _av_info;

// Working memory for keyframe data
static uint8_t* _key_blob_ptr;
static vol_geom_frame_data_t _key_frame_data;
static int _prev_key_frame_loaded_idx = -1;


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
        if ( !vol_basis_transcode( 13, (void*)texture_data, texture_size, rgba_data, rgba_buffer_size, &src_width, &src_height ) ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to transcode BASIS texture\n" );
            free( rgba_data );
            return false;
        }
        
        _printlog( _LOG_TYPE_INFO, "Decoded BASIS texture: %dx%d\n", src_width, src_height );
        
        // Use BASIS Universal encoder for resizing and re-encoding
        // This preserves BASIS format and uses high-quality resampling
        bool use_uastc = (geom_info->hdr.texture_compression == 2);  // 2 = UASTC, 1 = ETC1S
        
        if ( !basis_encode_texture_with_resize( rgba_data, src_width, src_height,
                                               _texture_width, _texture_height, use_uastc,
                                               output_data_ptr, output_size_ptr ) ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to encode resized texture to BASIS format\n" );
            free( rgba_data );
            return false;
        }
        
        free( rgba_data );
        
        // Update output dimensions
        *output_width_ptr = _texture_width;
        *output_height_ptr = _texture_height;
        
        _printlog( _LOG_TYPE_INFO, "Successfully resized and re-encoded BASIS texture from %dx%d to %dx%d\n",
                  src_width, src_height, _texture_width, _texture_height );
        
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
 * Write frame data to the output file, applying modifications like removing normals
 */
static bool _write_frame_data( FILE* output_file, const vol_geom_frame_data_t* frame_data, bool is_keyframe,
                              const vol_geom_info_t* geom_info ) {
    if ( !output_file || !frame_data || !geom_info ) {
        return false;
    }
    
    // Calculate total size of written data (includes all size fields)
    uint32_t total_written_sz = sizeof( uint32_t ) + frame_data->vertices_sz; // vertices size field + data
    
    // Add normals if not removing them and version >= 11
    if ( !_no_normals && geom_info->hdr.version >= 11 && geom_info->hdr.normals ) {
        total_written_sz += sizeof( uint32_t ) + frame_data->normals_sz; // normals size field + data
    }
    
    // Add keyframe-specific data
    if ( is_keyframe ) {
        total_written_sz += sizeof( uint32_t ) + frame_data->indices_sz; // indices size field + data
        total_written_sz += sizeof( uint32_t ) + frame_data->uvs_sz;     // UVs size field + data
    }
    
    // Add texture data if present
    if ( geom_info->hdr.version >= 11 && geom_info->hdr.textured && frame_data->texture_sz > 0 ) {
        // Calculate the size of processed texture data
        uint32_t processed_texture_size = frame_data->texture_sz;
        if ( _texture_width > 0 && _texture_height > 0 && 
             geom_info->hdr.version >= 13 && geom_info->hdr.texture_container_format == 1 ) {
            // If we're resizing a BASIS texture to raw format, calculate new size
            processed_texture_size = _texture_width * _texture_height * 4; // RGBA
        }
        total_written_sz += sizeof( uint32_t ) + processed_texture_size; // texture size field + data
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
    
    // Write texture size and data if present
    if ( geom_info->hdr.version >= 11 && geom_info->hdr.textured && frame_data->texture_sz > 0 ) {
        uint8_t* processed_texture_data = NULL;
        uint32_t processed_texture_size = 0;
        uint32_t processed_width = 0;
        uint32_t processed_height = 0;
        
        // Process texture data (decode, resize if needed)
        if ( !_process_texture_data( &frame_data->block_data_ptr[frame_data->texture_offset], 
                                    frame_data->texture_sz, geom_info,
                                    &processed_texture_data, &processed_texture_size,
                                    &processed_width, &processed_height ) ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to process texture data\n" );
            return false;
        }
        
        // Write processed texture size and data
        if ( 1 != fwrite( &processed_texture_size, sizeof( uint32_t ), 1, output_file ) ) {
            free( processed_texture_data );
            return false;
        }
        if ( processed_texture_size != fwrite( processed_texture_data, sizeof( uint8_t ), processed_texture_size, output_file ) ) {
            free( processed_texture_data );
            return false;
        }
        
        free( processed_texture_data );
    }
    
    // Write trailing mesh data size (should match frame header mesh_data_sz)
    // We need to calculate this based on the version rules, not the total written size
    uint32_t trailing_mesh_data_sz = frame_data->vertices_sz; // vertices data
    
    // Add normals if not removing them and version >= 11
    if ( !_no_normals && geom_info->hdr.version >= 11 && geom_info->hdr.normals ) {
        trailing_mesh_data_sz += frame_data->normals_sz; // normals data
    }
    
    // Add keyframe-specific data
    if ( is_keyframe ) {
        trailing_mesh_data_sz += frame_data->indices_sz; // indices data
        trailing_mesh_data_sz += frame_data->uvs_sz;     // UVs data
    }
    
    // Add texture data if present
    if ( geom_info->hdr.version >= 11 && geom_info->hdr.textured && frame_data->texture_sz > 0 ) {
        // Calculate the size of processed texture data
        uint32_t processed_texture_size = frame_data->texture_sz;
        if ( _texture_width > 0 && _texture_height > 0 && 
             geom_info->hdr.version >= 13 && geom_info->hdr.texture_container_format == 1 ) {
            // If we're resizing a BASIS texture to raw format, calculate new size
            processed_texture_size = _texture_width * _texture_height * 4; // RGBA
        }
        trailing_mesh_data_sz += processed_texture_size; // texture data
    }
    
    // For version 12+, add size fields to mesh_data_sz
    if ( geom_info->hdr.version >= 12 ) {
        trailing_mesh_data_sz += sizeof( uint32_t ); // vertices size field
        if ( !_no_normals && geom_info->hdr.normals ) {
            trailing_mesh_data_sz += sizeof( uint32_t ); // normals size field
        }
        if ( is_keyframe ) {
            trailing_mesh_data_sz += 2 * sizeof( uint32_t ); // indices + UVs size fields
        }
        if ( geom_info->hdr.textured && frame_data->texture_sz > 0 ) {
            trailing_mesh_data_sz += sizeof( uint32_t ); // texture size field
        }
    }
    
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
    
    // Allocate working memory
    _key_blob_ptr = calloc( 1, _geom_info.biggest_frame_blob_sz );
    if ( !_key_blob_ptr ) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Allocating memory for maximally-sized frame blob.\n" );
        return false;
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
    
    // Write header with modifications
    vol_geom_file_hdr_t modified_hdr = _geom_info.hdr;
    
    // Update texture dimensions and format if resizing is requested
    if ( _texture_width > 0 && _texture_height > 0 && modified_hdr.textured ) {
        modified_hdr.texture_width = _texture_width;
        modified_hdr.texture_height = _texture_height;
        
        // If we're resizing a BASIS texture, output will be raw format
        if ( modified_hdr.version >= 13 && modified_hdr.texture_container_format == 1 ) {
            modified_hdr.texture_container_format = 0; // Raw format
            modified_hdr.texture_compression = 0;      // No compression
            _printlog( _LOG_TYPE_INFO, "Output texture format changed to raw due to resizing\n" );
        }
    }
    
    if ( !_write_vols_header( output_file, &modified_hdr ) ) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to write output file header.\n" );
        fclose( output_file );
        return false;
    }
    
    // Write audio data if present
    if ( _geom_info.hdr.audio && _geom_info.audio_data_ptr ) {
        if ( 1 != fwrite( &_geom_info.audio_data_sz, sizeof( uint32_t ), 1, output_file ) ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to write audio data size.\n" );
            fclose( output_file );
            return false;
        }
        if ( _geom_info.audio_data_sz != fwrite( _geom_info.audio_data_ptr, sizeof( uint8_t ), 
                                                _geom_info.audio_data_sz, output_file ) ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to write audio data.\n" );
            fclose( output_file );
            return false;
        }
    }
    
    // Process each frame
    const char* sequence_filename = _input_filename ? _input_filename : _input_sequence_filename;
    
    for ( uint32_t frame_idx = 0; frame_idx < _geom_info.hdr.frame_count; frame_idx++ ) {
        int key_idx = vol_geom_find_previous_keyframe( &_geom_info, frame_idx );
        bool is_keyframe = vol_geom_is_keyframe( &_geom_info, frame_idx );
        
        // Load keyframe data if needed
        if ( _prev_key_frame_loaded_idx != key_idx ) {
            if ( !vol_geom_read_frame( sequence_filename, &_geom_info, key_idx, &_key_frame_data ) ) {
                _printlog( _LOG_TYPE_ERROR, "ERROR: Reading geometry keyframe %i.\n", key_idx );
                fclose( output_file );
                return false;
            }
            memcpy( _key_blob_ptr, _key_frame_data.block_data_ptr, _key_frame_data.block_data_sz );
            _prev_key_frame_loaded_idx = key_idx;
        }
        
        // Read current frame
        vol_geom_frame_data_t frame_data;
        if ( key_idx != (int)frame_idx ) {
            if ( !vol_geom_read_frame( sequence_filename, &_geom_info, frame_idx, &frame_data ) ) {
                _printlog( _LOG_TYPE_ERROR, "ERROR: Reading geometry frame %i.\n", frame_idx );
                fclose( output_file );
                return false;
            }
        } else {
            frame_data = _key_frame_data;
        }
        
        // Create modified frame header
        vol_geom_frame_hdr_t modified_frame_hdr = _geom_info.frame_headers_ptr[frame_idx];
        
        // Calculate new mesh data size based on version and modifications
        // V10, 11 = Size of Vertices Data + Normals Data + Indices Data + UVs Data + Texture Data (WITHOUT size fields)
        // V12+ = Size of Vertices Data + Normals Data + Indices Data + UVs Data + Texture Data + 4 Bytes for each "Size of Array"
        uint32_t new_mesh_data_sz = frame_data.vertices_sz; // vertices data
        
        // Add normals if not removing them and version >= 11
        if ( !_no_normals && _geom_info.hdr.version >= 11 && _geom_info.hdr.normals ) {
            new_mesh_data_sz += frame_data.normals_sz; // normals data
        }
        
        // Add keyframe-specific data
        if ( is_keyframe ) {
            new_mesh_data_sz += frame_data.indices_sz; // indices data
            new_mesh_data_sz += frame_data.uvs_sz;     // UVs data
        }
        
        // Add texture data if present
        if ( _geom_info.hdr.version >= 11 && _geom_info.hdr.textured && frame_data.texture_sz > 0 ) {
            // Calculate the size of processed texture data
            uint32_t processed_texture_size = frame_data.texture_sz;
            if ( _texture_width > 0 && _texture_height > 0 && 
                 _geom_info.hdr.version >= 13 && _geom_info.hdr.texture_container_format == 1 ) {
                // If we're resizing a BASIS texture to raw format, calculate new size
                processed_texture_size = _texture_width * _texture_height * 4; // RGBA
            }
            new_mesh_data_sz += processed_texture_size; // texture data
        }
        
        // For version 12+, add size fields to mesh_data_sz
        if ( _geom_info.hdr.version >= 12 ) {
            new_mesh_data_sz += sizeof( uint32_t ); // vertices size field
            if ( !_no_normals && _geom_info.hdr.normals ) {
                new_mesh_data_sz += sizeof( uint32_t ); // normals size field
            }
            if ( is_keyframe ) {
                new_mesh_data_sz += 2 * sizeof( uint32_t ); // indices + UVs size fields
            }
            if ( _geom_info.hdr.textured && frame_data.texture_sz > 0 ) {
                new_mesh_data_sz += sizeof( uint32_t ); // texture size field
            }
        }
        
        modified_frame_hdr.mesh_data_sz = new_mesh_data_sz;
        
        // Write frame header
        if ( !_write_frame_header( output_file, &modified_frame_hdr ) ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to write frame %i header.\n", frame_idx );
            fclose( output_file );
            return false;
        }
        
        // Write frame data
        if ( !_write_frame_data( output_file, &frame_data, is_keyframe, &_geom_info ) ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to write frame %i data.\n", frame_idx );
            fclose( output_file );
            return false;
        }
        
        _printlog( _LOG_TYPE_INFO, "Processed frame %i/%i\n", frame_idx + 1, _geom_info.hdr.frame_count );
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
    
    if ( _key_blob_ptr ) {
        free( _key_blob_ptr );
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
    
    // Initialize BASIS Universal encoder if texture resizing is requested
    if ( _texture_width > 0 && _texture_height > 0 ) {
        if ( !basis_encoder_init_wrapper() ) {
            _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to initialize BASIS Universal encoder\n" );
            return 1;
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
    
    return 0;
} 