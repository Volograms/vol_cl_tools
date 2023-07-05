/** @file main.h
 * Volograms VOLS to Wavefront OBJ converter.
 *
 * vol2obj   | Vologram frame to OBJ+image converter.
 * --------- | ----------------------------------------------------------------
 * Version   | 0.8.0
 * Authors   | Anton Gerdelan  <anton@volograms.com>
 *           | Jan Ondřej      <jan@volograms.com>
 * Copyright | 2023-2021, Volograms (http://volograms.com/)
 * Language  | C99, C++11
 * Files     | 1
 * Licence   | The MIT License. Note that dependencies have separate licences.
 *           | See LICENSE.md for details.
 *
 * Usage Instructions
 * ------------------
 * For single-file volograms:
 *     ./vol2obj.bin -c MYFILE.VOLS -f FRAME_NUMBER
 *
 * For older multi-file volograms:
 *     ./vol2obj.bin -h HEADER.VOLS -s SEQUENCE.VOLS -v VIDEO.MP4 -f FRAME_NUMBER
 *
 *   - Where `FRAME_NUMBER` is a frame you'd like to extract from the sequence, with 0 being the first frame.
 *   - If you request a frame outside range an error will be reported.
 *   - You can also output every frame with the `--all` option, or a specific range of frames using
 *     `-f FIRST -l LAST`
 *     for frame numbers `FIRST` to `LAST`, inclusive.
 *
 * Compilation
 * ------------------
 *
 * `make vol2obj`
 *
 * History
 * -----------
 * - 0.8.0   (2023/07/05) - `--combined` flag. vols v1.3 support with Basis Universal textures (and drag-and-drop support). Disk space check.
 * - 0.7.1   (2023/06/20) - Support for Volograms without normals.
 * - 0.7.0   (2022/07/29) - `--prefix` flag, and updated vol_libs, updated cl param parsing system.
 * - 0.6.0   (2022/06/17) - Fix for drag-and-drop not finding the new 1k video texture files.
 * - 0.5.0   (2022/04/22) - Includes drag-and-drop of vologram folders for Windows.
 * - 0.4.3   (2022/02/09) - Small tweak .obj format to enable texture display in Windows 3d viewer.
 * - 0.4.2   (2022/01/06) - Tweaks to Windows builds to remove warnings and errors on git-bash & msvc.
 * - 0.4.1   (2022/01/06) - Fix to normals (x axis flip).
 * - 0.4.0   (2022/01/06) - `--output_dir` cl flag.
 * - 0.3.0   (2021/12/16) - First release build, and a fix to mirrored-on-x bug (0.3.1).
 * - 0.2.0   (2021/12/15) - Basic command-line flags and an `--all` option.
 * - 0.1.0   (2021/11/12) - First version with number and repo.
 */

#include "vol_av.h"    // Volograms' texture video library.
#include "vol_basis.h" // Volograms' Basis Universal wrapper library.
#include "vol_geom.h"  // Volograms' .vols file parsing library.

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
// Not "#if defined(_WIN32) || defined(_WIN64)" because we have strncasecmp in MinGW.
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#else
#include <strings.h> // strcasecmp
#endif               /* endif _MSC_VER. */

#if defined( _WIN32 ) || defined( _WIN64 )
#include <direct.h>
// #include <fileapi.h> // Already pulled in by windows.h. Including explicitly drags in winnt.h which causes some build warnings.
#include <windows.h>
#else
#include <dirent.h>   // DIR
#include <sys/stat.h> // mkdir
#define _XOPEN_SOURCE_EXTENDED 1
#include <sys/statvfs.h>
#include <errno.h>
#endif /* endif _WIN32 || _WIN64. */

#define MAX_FILENAME_LEN 4096
#define MAX_SUBPATH_LEN 1024

/** Colour formatting of printfs for status messages. */
static const char* STRC_DEFAULT = "\x1B[0m";
static const char* STRC_RED     = "\x1B[31m";
static const char* STRC_GREEN   = "\x1B[32m";
static const char* STRC_YELLOW  = "\x1B[33m";

static const int _dims_presize = 2048;
static uint8_t* _output_blocks_ptr;

static bool _bytes_free_on_disk( const char* path_str, uint64_t* free_sz_ptr, uint64_t* total_sz_ptr ) {
  if ( !free_sz_ptr || !total_sz_ptr ) { return false; }
#if defined( _WIN32 ) || defined( _WIN64 )
  uint64_t free_byte_to_caller = 0, total_bytes = 0, free_bytes = 0;
  if ( !GetDiskFreeSpaceEx( path_str, (PULARGE_INTEGER)&free_byte_to_caller, (PULARGE_INTEGER)&total_bytes, (PULARGE_INTEGER)&free_bytes ) ) { return false; }
  *free_sz_ptr  = free_byte_to_caller;
  *total_sz_ptr = total_bytes;
#else
  struct statvfs buf;
  if ( -1 == statvfs( path_str, &buf ) ) { return false; }
  *free_sz_ptr  = (uint64_t)buf.f_bavail * (uint64_t)buf.f_frsize;
  *total_sz_ptr = (uint64_t)buf.f_blocks * (uint64_t)buf.f_frsize;
#endif
  return true;
}

typedef enum _log_type { _LOG_TYPE_INFO = 0, _LOG_TYPE_DEBUG, _LOG_TYPE_WARNING, _LOG_TYPE_ERROR, _LOG_TYPE_SUCCESS } _log_type;

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

// TODO(Anton) add Basis & KTX.
typedef enum img_fmt_t {
  IMG_FMT_PPM = 0, // homemade
  IMG_FMT_JPG,     // uses library
  IMG_FMT_MAX      // just for counting # image formats
} img_fmt_t;

/** Command-line flags. */
typedef struct cl_flag_t {
  const char* long_str;  // e.g. "--header"
  const char* short_str; // e.g. "-h"
  const char* help_str;  // e.g. "Required for multi-file volograms. The next argument gives the path to the header.vols file.\n"
  int n_required_args;   // Number of parameters following that are required.
} cl_flag_t;

/// Globals for parsing the command line arguments in a function.
static int my_argc;
static char** my_argv;

/** Convience enum to index into the array of command-line flags by readable name. */
typedef enum cl_flag_enum_t {
  CL_ALL_FRAMES,
  CL_COMBINED,
  CL_HEADER,
  CL_HELP,
  CL_FIRST,
  CL_LAST,
  CL_OUTPUT_DIR,
  CL_PREFIX,
  CL_SEQUENCE,
  CL_VIDEO,
  CL_MAX
} cl_flag_enum_t;

/** All command line flags are specified here. Note that this order must correspond to the ordering in cl_flag_enum_t. */
static cl_flag_t _cl_flags[CL_MAX] = {
  { "--all", "-a", "Create output files for, and process, all frames found in the sequence.\nIf given, then paramters -f and -l are ignored.\n", 0 }, // CL_ALL_FRAMES
  { "--combined", "-c", "Required for single-file volograms. The next argument gives the path to your myfile.vols.\n", 1 },        // CL_COMBINED
  { "--header", "-h", "Required for multi-file volograms. The next argument gives the path to the header.vols file.\n", 1 },       // CL_HEADER
  { "--help", NULL, "Prints this text.\n", 0 },                                                                                    // CL_HELP
  { "--first", "-f",                                                                                                               // CL_FIRST
    "The next argument gives the frame number of the first frame to process (frames start at 0).\n"                                //
    "If the -l parameter is not given then only this single frame is processed.\n"                                                 //
    "Default value 0.\n",                                                                                                          //
    1 },                                                                                                                           //
  { "--last", "-l",                                                                                                                // CL_LAST
    "The next argument gives the frame number of the last frame to process.\n"                                                     //
    "Can be used with -f to process a range of frames from first to last, inclusive.\n",                                           //
    1 },                                                                                                                           //
  { "--output-dir", "-o",                                                                                                          // CL_OUTPUT_DIR
    "The next argument gives the path to a directory to write output files into.\n"                                                //
    "Default is the current working directory.\n",                                                                                 //
    1 },                                                                                                                           //
  { "--prefix", "-p",                                                                                                              // CL_PREFIX
    "The next argument gives the prefix to use for output filenames.\n"                                                            //
    "Default is output_frame_.\n",                                                                                                 //
    1 },                                                                                                                           //
  { "--sequence", "-s", "Required for multi-file volograms. The next argument gives the path to the sequence_0.vols file.\n", 1 }, // CL_SEQUENCE
  { "--video", "-v", "Required for multi-file volograms. The next argument gives the path to the video texture file.\n", 1 }       // CL_VIDEO
};

/** Used to print all the options in the command line flags struct for the help text. */
static void _print_cl_flags( void ) {
  printf( "Options:\n" );
  for ( int i = 0; i < CL_MAX; i++ ) {
    if ( _cl_flags[i].long_str ) { printf( "%s", _cl_flags[i].long_str ); }
    if ( _cl_flags[i].long_str && _cl_flags[i].short_str ) { printf( ", " ); }
    if ( _cl_flags[i].short_str ) { printf( "%s", _cl_flags[i].short_str ); }
    if ( _cl_flags[i].long_str || _cl_flags[i].short_str || _cl_flags[i].short_str ) { printf( "\n" ); }
    if ( _cl_flags[i].help_str ) { printf( "%s\n", _cl_flags[i].help_str ); }
  }
}

static bool _check_cl_option( int argv_idx, const char* long_str, const char* short_str ) {
  if ( long_str && ( 0 == strcasecmp( long_str, my_argv[argv_idx] ) ) ) { return true; }
  if ( short_str && ( 0 == strcasecmp( short_str, my_argv[argv_idx] ) ) ) { return true; }
  return false;
}

/** If command-line options are valid, their index in argv is stored here, otherwise it is 0. */
static int _option_arg_indices[CL_MAX];

/** Loop over all the command line arguments and make sure they all have the right bits with them and there are not unknowns.
 * Registers any valid params found, with their index in argv, in _option_arg_indices.
 * @returns Returns false if anything is out of order, or an unrecognised flag is found.
 */
static bool _evaluate_params() {
  for ( int argv_idx = 1; argv_idx < my_argc; argv_idx++ ) {
    bool found_valid_arg = false;
    // If starts with a '-' check if a known option.
    if ( '-' != my_argv[argv_idx][0] ) {
      _printlog( _LOG_TYPE_WARNING, "Argument '%s' is an invalid option. Perhaps a '-' is missing? Run with --help for details.\n", my_argv[argv_idx] );
      return false;
    }
    // Check if it matches any known options (including if its a repeat of an earlier-specified option).
    for ( int clo_idx = 0; clo_idx < CL_MAX; clo_idx++ ) {
      if ( !_check_cl_option( argv_idx, _cl_flags[clo_idx].long_str, _cl_flags[clo_idx].short_str ) ) { continue; }
      // If valid check it has the correct number of following params e.g. -h has one, and we don't interpret the next option flag as this one's parameter.
      if ( _cl_flags[clo_idx].n_required_args > 0 ) {
        for ( int following_idx = 1; following_idx < _cl_flags[clo_idx].n_required_args + 1; following_idx++ ) {
          if ( argv_idx + _cl_flags[clo_idx].n_required_args >= my_argc || '-' == my_argv[argv_idx + following_idx][0] ) {
            printf( "argvidx = %i, nargs= %i, argc = %i\n", argv_idx, _cl_flags[clo_idx].n_required_args, my_argc );
            _printlog( _LOG_TYPE_WARNING, "Argument '%s' is not followed by a valid parameter. Run with --help for details.\n", my_argv[argv_idx] );
            return false;
          }
        }
      }
      // If all good, register the index of the option. so e.g. _option_arg_indices[CL_HEADER] = 2
      _option_arg_indices[clo_idx] = argv_idx;
      argv_idx += _cl_flags[clo_idx].n_required_args;
      found_valid_arg = true;
      break;
    } // endfor clo_idx
    if ( !found_valid_arg ) {
      _printlog( _LOG_TYPE_WARNING, "Argument '%s' is an unknown option. Run with --help for details.\n", my_argv[argv_idx] );
      return false;
    }
  } // endfor argv_idx
  return true;
}

static img_fmt_t _img_fmt = IMG_FMT_JPG;             // Image format to use for output.
static int _jpeg_quality  = 95;                      // Arbitrary choice of 95% quality v size based on GIMP's default.
static vol_av_video_t _av_info;                      // Audio-video information from vol_av library.
static vol_geom_info_t _geom_info;                   // Mesh information from vol_geom library.
static char* _input_header_filename;                 // e.g. `header.vols`
static char* _input_sequence_filename;               // e.g. `sequence.vols`
static char* _input_combined_filename;               // e.g. `combined.vols`
static char* _input_video_filename;                  // e.g. `texture_1024.webm`
static char _output_dir_path[MAX_SUBPATH_LEN];       // e.g. `my_output/`
static char _output_mesh_filename[MAX_FILENAME_LEN]; // e.g. `output_frame_00000000.obj`
static char _output_mtl_filename[MAX_FILENAME_LEN];  // e.g. `output_frame_00000000.mtl`
static char _output_img_filename[MAX_FILENAME_LEN];  // e.g. `output_frame_00000000.jpg`
static char _material_name[MAX_SUBPATH_LEN];         // e.g. `volograms_mtl_00000000`
static char _prefix_str[MAX_SUBPATH_LEN];            // defaults to `output_frame_`

/// A homemade P3 ASCII PPM image writer. These images are very large, so only useful for debugging purposes.
static bool write_rgb_image_to_ppm( const char* filename, const uint8_t* image_ptr, int w, int h ) {
  FILE* f_ptr = fopen( filename, "w" );
  if ( !f_ptr ) { return false; }
  fprintf( f_ptr, "P3\n%i %i\n255\n", w, h );
  for ( int y = 0; y < h; y++ ) {
    for ( int x = 0; x < w; x++ ) {
      int idx = ( y * w + x ) * 3;
      fprintf( f_ptr, "%i %i %i ", image_ptr[idx + 0], image_ptr[idx + 1], image_ptr[idx + 2] );
    }
    fprintf( f_ptr, "\n" );
  }
  fclose( f_ptr );
  return true;
}

/** Writes the latest pixel buffer into a file in the appropriate format.
 * @param w,h,n Height and width of image, and number of colours channels, respectively.
 */
static bool _write_video_frame_to_image( const char* output_image_filename, const uint8_t* pixels_ptr, int w, int h, int n ) {
  if ( !output_image_filename || !pixels_ptr || w <= 0 || h <= 0 ) { return false; }

  { // Size check.
    uint64_t avail_bytes = 0, total_bytes = 0;
    const char* ptr = NULL;
    if ( _output_dir_path[0] != '\0' ) { ptr = _output_dir_path; }
    if ( !_bytes_free_on_disk( ptr, &avail_bytes, &total_bytes ) ) {
      _printlog( _LOG_TYPE_WARNING, "WARNING: Could not retrieve bytes available on disk.\n" );
    } else {
      uint64_t avail_mb  = avail_bytes / ( 1024 * 1024 );
      uint64_t total_mb  = total_bytes / ( 1024 * 1024 );
      uint64_t min_bytes = w * h * n;
      if ( avail_bytes <= min_bytes ) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Out of space on disk for writing image frames. Available space %u/%u MB\n", (uint32_t)avail_mb, (uint32_t)total_mb );
        return false;
      }
    }
  } // endblock Size check.

  char full_path[MAX_FILENAME_LEN];
  sprintf( full_path, "%s%s", _output_dir_path, output_image_filename );

  switch ( _img_fmt ) {
  case IMG_FMT_PPM: {
    if ( !write_rgb_image_to_ppm( full_path, pixels_ptr, w, h ) ) {
      _printlog( _LOG_TYPE_ERROR, "ERROR: Writing frame image file `%s`.\n", full_path );
      return false;
    }
  } break;
  case IMG_FMT_JPG: {
    if ( !stbi_write_jpg( full_path, w, h, n, pixels_ptr, _jpeg_quality ) ) {
      _printlog( _LOG_TYPE_ERROR, "ERROR: Writing frame image file `%s`.\n", full_path );
      return false;
    }
  } break;
  default: {
    _printlog( _LOG_TYPE_ERROR, "ERROR: No valid image format selected\n" );
    return false;
  } break;
  } // endswitch

  _printlog( _LOG_TYPE_INFO, "Wrote image file `%s`\n", full_path );

  return true;
}

/// Writes a Wavefront MTL (material) file to link up with the OBJ (mesh/object) file and texture image file.
static bool _write_mtl_file( const char* output_mtl_filename, const char* material_name, const char* image_filename ) {
  if ( !output_mtl_filename || !image_filename ) { return false; }

  /* http://www.paulbourke.net/dataformats/mtl/
An .mtl file must have one newmtl statement at the start of
each material description.
"name" is the name of the material.  Names may be any length but
cannot include blanks.  Underscores may be used in material names.

map_Kd -options args filename
Specifies that a color texture file or color procedural texture file is
linked to the diffuse reflectivity of the material.  During rendering,
the map_Kd value is multiplied by the Kd value.
  */

  char full_path[MAX_FILENAME_LEN];
  sprintf( full_path, "%s%s", _output_dir_path, output_mtl_filename );

  FILE* f_ptr = fopen( full_path, "w" );
  if ( !f_ptr ) {
    _printlog( _LOG_TYPE_ERROR, "ERROR: Opening file for writing `%s`\n", full_path );
    return false;
  }
  if ( fprintf( f_ptr, "newmtl %s\n", material_name ) < 0 ) {
    _printlog( _LOG_TYPE_ERROR, "ERROR: Writing to file `%s`, check permissions.\n", full_path );
    return false;
  }
  if ( fprintf( f_ptr, "map_Kd %s\nmap_Ka %s\n", image_filename, image_filename ) < 0 ) {
    _printlog( _LOG_TYPE_ERROR, "ERROR: Writing to file `%s`, check permissions.\n", full_path );
    return false;
  }
  fprintf( f_ptr, "Ka 0.1 0.1 0.1\n" );
  fprintf( f_ptr, "Kd 0.9 0.9 0.9\n" );
  fprintf( f_ptr, "Ks 0.0 0.0 0.0\n" );
  fprintf( f_ptr, "d 1.0\nTr 0.0\n" );
  fprintf( f_ptr, "Ns 0.0\n" );
  fclose( f_ptr );
  _printlog( _LOG_TYPE_INFO, "Wrote material file `%s`\n", full_path );

  return true;
}

/**
 * @param output_mtl_filename If NULL then no MTL section or link is added to the Obj.
 */
static bool _write_mesh_to_obj_file( const char* output_mesh_filename, const char* output_mtl_filename, const char* material_name, const float* vertices_ptr,
  int n_vertices, const float* texcoords_ptr, int n_texcoords, const float* normals_ptr, int n_normals, const void* indices_ptr, int n_indices, int index_type ) {
  if ( !output_mesh_filename ) { return false; }

  char full_path[MAX_FILENAME_LEN];
  sprintf( full_path, "%s%s", _output_dir_path, output_mesh_filename );

  FILE* f_ptr = fopen( full_path, "w" );
  if ( !f_ptr ) {
    _printlog( _LOG_TYPE_ERROR, "ERROR: Opening file for writing `%s`\n", full_path );
    return false;
  }

  if ( 0 == fprintf( f_ptr, "#Exported by Volograms vols2obj\n" ) ) { goto _wmo2f_fail; }
  // mtllib must go before usemtl or some viewers won't load the texture.
  if ( output_mtl_filename ) {
    if ( 0 == fprintf( f_ptr, "mtllib %s\n", output_mtl_filename ) ) { goto _wmo2f_fail; }
    if ( 0 == fprintf( f_ptr, "usemtl %s\n", material_name ) ) { goto _wmo2f_fail; }
  }

  assert( vertices_ptr && "Hey if there are no vertex points Anton should make sure that is accounted for in the f section" );
  if ( vertices_ptr ) {
    for ( int i = 0; i < n_vertices; i++ ) {
      float x = vertices_ptr[i * 3 + 0];
      float y = vertices_ptr[i * 3 + 1];
      float z = vertices_ptr[i * 3 + 2];
      // Reversed X. could instead reverse Z but then need to import in blender as "Z forward".
      if ( 0 == fprintf( f_ptr, "v %0.3f %0.3f %0.3f\n", -x, y, z ) ) { goto _wmo2f_fail; }
    }
  }
  assert( texcoords_ptr && "Hey if there are no UVs Anton should make sure that is accounted for in the f section" );
  if ( texcoords_ptr ) {
    for ( int i = 0; i < n_texcoords; i++ ) {
      float s = texcoords_ptr[i * 2 + 0];
      float t = texcoords_ptr[i * 2 + 1];
      if ( 0 == fprintf( f_ptr, "vt %0.3f %0.3f\n", s, t ) ) { goto _wmo2f_fail; }
    }
  }
  if ( normals_ptr ) {
    for ( int i = 0; i < n_normals; i++ ) {
      float x = normals_ptr[i * 3 + 0];
      float y = normals_ptr[i * 3 + 1];
      float z = normals_ptr[i * 3 + 2];
      if ( 0 == fprintf( f_ptr, "vn %0.3f %0.3f %0.3f\n", -x, y, z ) ) { goto _wmo2f_fail; }
    }
  }
  assert( indices_ptr && "Hey if there are no indices Anton should make sure that is accounted for in the f section" );
  if ( indices_ptr ) {
    // NOTE: If adding support for additional index types these may be required:
    // uint32_t* i_u32_ptr = (uint32_t*)indices_ptr;
    // uint8_t* i_u8_ptr   = (uint8_t*)indices_ptr;
    uint16_t* i_u16_ptr = (uint16_t*)indices_ptr;
    // OBJ spec:
    // "Faces are defined using lists of vertex, texture and normal indices in the format vertex_index/texture_index/normal_index for which each index starts at 1"
    for ( int i = 0; i < n_indices / 3; i++ ) {
      // Index types: { 0=unsigned byte, 1=unsigned short, 2=unsigned int }.
      /* Integer[] if # vertices >= 65535 (Unity Version < 2017.3 does not support Integer indices) Short[] if # vertices < 65535. */
      assert( index_type == 1 );                 // Can come back and support other index types later.
      int a = (int)( i_u16_ptr[i * 3 + 0] ) + 1; // Note: +1s here!
      int b = (int)( i_u16_ptr[i * 3 + 1] ) + 1;
      int c = (int)( i_u16_ptr[i * 3 + 2] ) + 1;
      // NOTE VOLS winding order is CW (similar to Unity) rather than typical CCW so let's reverse it for OBJ.
      if ( normals_ptr ) {
        // f v1/vt1/vn1 v2/vt2/vn2 v3/vt3/vn3 ...
        if ( 0 == fprintf( f_ptr, "f %i/%i/%i %i/%i/%i %i/%i/%i\n", c, c, c, b, b, b, a, a, a ) ) { goto _wmo2f_fail; }
      } else {
        // f v1/vt1 v2/vt2 v3/vt3 ...
        if ( 0 == fprintf( f_ptr, "f %i/%i %i/%i %i/%i\n", c, c, b, b, a, a ) ) { goto _wmo2f_fail; }
      }
    }
  }

  fclose( f_ptr );
  _printlog( _LOG_TYPE_INFO, "Wrote mesh file `%s`.\n", full_path );

  return true;

_wmo2f_fail:
  fclose( f_ptr );
  _printlog( _LOG_TYPE_ERROR, "ERROR: Could not write mesh file `%s`.\n", full_path );
  return false;
}

/**
 * @param output_mtl_filename
 * If NULL then no MTL section or link is added to the Obj.
 * @param use_vol_av
 * If true then output_image_filename must not be NULL.
 * @return
 * Returns false on error.
 */
static bool _write_geom_frame_to_mesh( const char* seq_filename, const char* combined_filename, const char* output_mesh_filename,
  const char* output_mtl_filename, const char* material_name, int frame_idx, bool use_vol_av, const char* output_image_filename ) {
  if ( !( seq_filename || combined_filename ) || !output_mesh_filename || frame_idx < 0 ) { return false; }
  if ( use_vol_av && !output_image_filename ) { return false; }

  vol_geom_frame_data_t keyframe_data           = ( vol_geom_frame_data_t ){ .block_data_sz = 0 };
  vol_geom_frame_data_t intermediate_frame_data = ( vol_geom_frame_data_t ){ .block_data_sz = 0 };
  bool success                                  = true;

  int prev_key_idx = vol_geom_find_previous_keyframe( &_geom_info, frame_idx );

  uint8_t *points_ptr = NULL, *texcoords_ptr = NULL, *normals_ptr = NULL, *indices_ptr = NULL, *texture_data_ptr = NULL;
  int32_t points_sz = 0, texcoords_sz = 0, normals_sz = 0, indices_sz = 0, texture_data_sz = 0;

  { // Get data pointers.
    // If our frame isn't a keyframe then we need to load up the previous keyframe's data first...
    if ( combined_filename ) {
      if ( !vol_geom_read_frame( combined_filename, &_geom_info, prev_key_idx, &keyframe_data ) ) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Reading geometry keyframe %i\n", prev_key_idx );
        return false;
      }
    } else if ( !vol_geom_read_frame( seq_filename, &_geom_info, prev_key_idx, &keyframe_data ) ) {
      _printlog( _LOG_TYPE_ERROR, "ERROR: Reading geometry keyframe %i\n", prev_key_idx );
      return false;
    }

    points_ptr    = &keyframe_data.block_data_ptr[keyframe_data.vertices_offset];
    texcoords_ptr = &keyframe_data.block_data_ptr[keyframe_data.uvs_offset];
    if ( _geom_info.hdr.normals ) { normals_ptr = &keyframe_data.block_data_ptr[keyframe_data.normals_offset]; }
    if ( !use_vol_av && _geom_info.hdr.textured && _geom_info.hdr.texture_compression > 0 ) {
      texture_data_ptr = &keyframe_data.block_data_ptr[keyframe_data.texture_offset];
    }
    indices_ptr     = &keyframe_data.block_data_ptr[keyframe_data.indices_offset];
    points_sz       = keyframe_data.vertices_sz;
    texcoords_sz    = keyframe_data.uvs_sz;
    normals_sz      = keyframe_data.normals_sz;
    texture_data_sz = keyframe_data.texture_sz;
    indices_sz      = keyframe_data.indices_sz;

    // ...and then add our frame's subset of the data second.
    if ( prev_key_idx != frame_idx ) {
      // Read the non-keyframe (careful with mem leaks).
      if ( combined_filename ) {
        if ( !vol_geom_read_frame( combined_filename, &_geom_info, prev_key_idx, &keyframe_data ) ) {
          _printlog( _LOG_TYPE_ERROR, "ERROR: Reading geometry keyframe %i\n", prev_key_idx );
          return false;
        }
      } else if ( !vol_geom_read_frame( seq_filename, &_geom_info, prev_key_idx, &keyframe_data ) ) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Reading geometry keyframe %i\n", prev_key_idx );
        return false;
      }
      points_ptr = &intermediate_frame_data.block_data_ptr[intermediate_frame_data.vertices_offset];
      points_sz  = intermediate_frame_data.vertices_sz;
      if ( _geom_info.hdr.normals ) {
        normals_ptr = &intermediate_frame_data.block_data_ptr[intermediate_frame_data.normals_offset];
        normals_sz  = intermediate_frame_data.normals_sz;
      }
      if ( !use_vol_av && _geom_info.hdr.textured && _geom_info.hdr.texture_compression > 0 ) {
        texture_data_ptr = &intermediate_frame_data.block_data_ptr[intermediate_frame_data.texture_offset];
        texture_data_sz  = intermediate_frame_data.texture_sz;
      }
    }
  } // endblock get data pointers.

  // Write the .obj.
  int n_points    = points_sz / ( sizeof( float ) * 3 );
  int n_texcoords = texcoords_sz / ( sizeof( float ) * 2 );
  int n_normals   = normals_sz / ( sizeof( float ) * 3 );
  // NOTE(Anton) hacked this in so only supporting uint32_t indices for first pass.
  int indices_type = 1;                               // 1 is uint16_t.
  int n_indices    = indices_sz / sizeof( uint16_t ); // NOTE(Anton) change if type changes!!!
  if ( !_write_mesh_to_obj_file(                      //
         output_mesh_filename,                        //
         output_mtl_filename,                         //
         material_name,                               //
         (float*)points_ptr,                          //
         n_points,                                    //
         (float*)texcoords_ptr,                       //
         n_texcoords,                                 //
         (float*)normals_ptr,                         //
         n_normals,                                   //
         (void*)indices_ptr,                          //
         n_indices,                                   //
         indices_type ) ) {
    _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to write mesh file `%s`\n", output_mesh_filename );
    // Not returning false yet, but just setting a flag, because we still want to release resources.
    success = false;
  } // endif write mesh.

  // And texture. Texture_compression { 0=raw, 1=basis, 2=ktx2 }.
  if ( !use_vol_av && _geom_info.hdr.textured && _geom_info.hdr.texture_compression > 0 ) {
    // TODO(Anton) Handle RAW and KTX2.
    int w = 0, h = 0, n = 4;
    int format = 13; // { 13 = cTFRGBA32, 3 = cTFBC3_RGBA }. Defined in basis_transcoder.h.
    if ( !vol_basis_transcode( format, texture_data_ptr, texture_data_sz, _output_blocks_ptr, _dims_presize * _dims_presize * n, &w, &h ) ) {
      fprintf( stderr, "ERROR transcoding image %i failed\n", frame_idx );
      return false;
    }
    if ( !_write_video_frame_to_image( _output_img_filename, _output_blocks_ptr, w, h, n ) ) {
      _printlog( _LOG_TYPE_ERROR, "ERROR: failed to write .basis texture frame %i to image file `%s`\n", frame_idx, _output_img_filename );
      success = false;
    }
  } // endif Texture/Basis.

  return success;
}

/** Write frames between `first_frame_idx` and `last_frame_idx`,
 * or all of them, if `all_frames` is set,
 * to mesh, material, and image files.
 *
 * @return
 * Returns false on error.
 */
static bool _process_vologram( int first_frame_idx, int last_frame_idx, bool all_frames ) {
  bool use_vol_av = false;
  { // Mesh and video processing.
    if ( _input_combined_filename ) {
      if ( !vol_geom_create_file_info_from_file( _input_combined_filename, &_geom_info ) ) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to open combined vologram file=%s. Check for file mismatches.\n", _input_combined_filename );
        return false;
      }
    } else if ( !vol_geom_create_file_info( _input_header_filename, _input_sequence_filename, &_geom_info, true ) ) {
      _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to open geometry files header=%s sequence=%s. Check for header and sequenece file mismatches.\n",
        _input_header_filename, _input_sequence_filename );
      return false;
    }

    if ( _geom_info.hdr.version < 13 ) {
      use_vol_av = true;
    } else {
      if ( !vol_basis_init() ) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to initialise Basis transcoder.\n" );
        return false;
      }
    }

    int n_frames = _geom_info.hdr.frame_count;
    if ( first_frame_idx >= n_frames ) {
      _printlog( _LOG_TYPE_ERROR, "ERROR: Frame %i is not in range of geometry's %i frames\n", first_frame_idx, n_frames );
      return false;
    }
    last_frame_idx = all_frames ? n_frames - 1 : last_frame_idx;

    for ( int i = first_frame_idx; i <= last_frame_idx; i++ ) {
      sprintf( _output_mesh_filename, "%s%08i.obj", _prefix_str, i );
      sprintf( _output_mtl_filename, "%s%08i.mtl", _prefix_str, i );
      sprintf( _material_name, "vol_mtl_%08i", i );
      switch ( _img_fmt ) {
      case IMG_FMT_PPM: sprintf( _output_img_filename, "%s%08i.ppm", _prefix_str, i ); break;
      case IMG_FMT_JPG: sprintf( _output_img_filename, "%s%08i.jpg", _prefix_str, i ); break;
      default: _printlog( _LOG_TYPE_ERROR, "ERROR: No valid image format selected\n" ); return false;
      } // endswitch.

      // And geometry.
      if ( !_write_geom_frame_to_mesh( _input_sequence_filename, _input_combined_filename, _output_mesh_filename, _output_mtl_filename, _material_name, i,
             use_vol_av, _output_img_filename ) ) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to write geometry frame %i to file\n", i );
        return false;
      }
      // Material file.
      if ( !_write_mtl_file( _output_mtl_filename, _material_name, _output_img_filename ) ) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to write material file for frame %i\n", i );
        return false;
      }
    } // endfor frames.

    if ( !vol_geom_free_file_info( &_geom_info ) ) {
      _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to free geometry info\n" );
      return false;
    }
  }                   // endblock mesh processing.

  if ( use_vol_av ) { // Video Processing.
    if ( !vol_av_open( _input_video_filename, &_av_info ) ) {
      _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to open video file %s.\n", _input_video_filename );
      return false;
    }
    int n_frames = (int)vol_av_frame_count( &_av_info );

    if ( first_frame_idx >= n_frames ) {
      _printlog( _LOG_TYPE_ERROR, "ERROR: Frame %i is not in range of video's %i frames\n", first_frame_idx, n_frames );
      return false;
    }
    last_frame_idx = all_frames ? n_frames - 1 : last_frame_idx;

    // Skip up to first frame to write.
    for ( int i = 0; i < first_frame_idx; i++ ) {
      if ( !vol_av_read_next_frame( &_av_info ) ) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: Reading frames from video sequence.\n" );
        return false;
      }
    }

    // Write any frames in the range we want.
    for ( int i = first_frame_idx; i <= last_frame_idx; i++ ) {
      if ( use_vol_av ) {
        if ( !vol_av_read_next_frame( &_av_info ) ) {
          _printlog( _LOG_TYPE_ERROR, "ERROR: Reading frames from video sequence.\n" );
          return false;
        }
      }

      switch ( _img_fmt ) {
      case IMG_FMT_PPM: sprintf( _output_img_filename, "%s%08i.ppm", _prefix_str, i ); break;
      case IMG_FMT_JPG: sprintf( _output_img_filename, "%s%08i.jpg", _prefix_str, i ); break;
      default: _printlog( _LOG_TYPE_ERROR, "ERROR: No valid image format selected\n" ); return false;
      } // endswitch.
      if ( !_write_video_frame_to_image( _output_img_filename, _av_info.pixels_ptr, _av_info.w, _av_info.h, 3 ) ) {
        _printlog( _LOG_TYPE_ERROR, "ERROR: failed to write video frame %i to file\n", first_frame_idx );
        return false; // Make sure we stop the processing at this point rather than carry on.
      }
    }                 // endfor.

    if ( !vol_av_close( &_av_info ) ) {
      _printlog( _LOG_TYPE_ERROR, "ERROR: Failed to close video info\n" );
      return false;
    }
  } // endblock Video Processing.

  return true;
}

static bool _does_dir_exist( const char* dir_path ) {
#if defined( _WIN32 ) || defined( _WIN64 )
  DWORD attribs = GetFileAttributes( dir_path );
  return ( attribs != INVALID_FILE_ATTRIBUTES && ( attribs & FILE_ATTRIBUTE_DIRECTORY ) );
#else
  DIR* dir      = opendir( dir_path );
  if ( dir ) {
    closedir( dir );
    return true;
  }
  return false;
#endif
}

static bool _make_dir( const char* dir_path ) {
#if defined( _WIN32 ) || defined( _WIN64 )
  if ( 0 == _mkdir( dir_path ) ) {
    _printlog( _LOG_TYPE_INFO, "Created directory `%s`\n", dir_path );
    return true;
  }
#else
  if ( 0 == mkdir( dir_path, S_IRWXU ) ) { // Read-Write-eXecute permissions
    _printlog( _LOG_TYPE_INFO, "Created directory `%s`\n", dir_path );
    return true;
  }
#endif
  _printlog( _LOG_TYPE_ERROR, "ERROR: Creating directory `%s`\n", dir_path );
  return false;
}

/// Default string names for Volu video texture files.
#define VOL_VID_STR_2048 "texture_2048_h264.mp4"
#define VOL_VID_STR_1024 "texture_1024_h264.mp4"

int main( int argc, char** argv ) {
  // Paths for drag-and-drop directory.
  char dad_hdr_str[MAX_FILENAME_LEN], dad_seq_str[MAX_FILENAME_LEN], dad_vid_str[MAX_FILENAME_LEN], test_vid_str[MAX_FILENAME_LEN];
  int first_frame = 0;
  int last_frame  = 0;
  bool all_frames = false;

  _output_blocks_ptr = (uint8_t*)malloc( _dims_presize * _dims_presize * 4 );
  if ( !_output_blocks_ptr ) {
    _printlog( _LOG_TYPE_ERROR, "ERROR Out of memory allocating block for output image.\n" );
    return 1;
  }

  my_argc        = argc;
  my_argv        = argv;
  dad_hdr_str[0] = dad_seq_str[0] = dad_vid_str[0] = test_vid_str[0] = '\0';
  strcpy( _prefix_str, "output_frame_" ); // Set the default filename prefix for images.

  // Check for drag-and-drop directory.
  if ( 2 == argc && _does_dir_exist( argv[1] ) ) {
    size_t len = strlen( argv[1] );
    strncat( dad_hdr_str, argv[1], MAX_FILENAME_LEN - 1 );
    strncat( dad_seq_str, argv[1], MAX_FILENAME_LEN - 1 );
    strncat( dad_vid_str, argv[1], MAX_FILENAME_LEN - 1 );
    if ( argv[1][len - 1] != '\\' && argv[1][len - 1] != '/' ) {
      strncat( dad_hdr_str, "/", MAX_FILENAME_LEN - 1 );
      strncat( dad_seq_str, "/", MAX_FILENAME_LEN - 1 );
      strncat( dad_vid_str, "/", MAX_FILENAME_LEN - 1 );
    }
    strncat( dad_hdr_str, "header.vols", MAX_FILENAME_LEN - 1 );
    strncat( dad_seq_str, "sequence_0.vols", MAX_FILENAME_LEN - 1 );
    // Try to use a 2k texture if one is in the folder, otherwise go for 1024x1024.
    strncat( test_vid_str, dad_vid_str, MAX_FILENAME_LEN - 1 );
    strncat( test_vid_str, VOL_VID_STR_2048, MAX_FILENAME_LEN - 1 );
    FILE* ft_ptr = fopen( test_vid_str, "rb" );
    if ( !ft_ptr ) {
      strncat( dad_vid_str, VOL_VID_STR_1024, MAX_FILENAME_LEN - 1 );
    } else {
      fclose( ft_ptr );
      strncat( dad_vid_str, VOL_VID_STR_2048, MAX_FILENAME_LEN - 1 );
    }
    _input_header_filename   = dad_hdr_str;
    _input_sequence_filename = dad_seq_str;
    _input_video_filename    = dad_vid_str;
  } else if ( 2 == argc ) {
    // Check for drag-and-drop of combined vols file.
    _input_combined_filename = my_argv[1];
    printf( " using -c as %s\n", _input_combined_filename );
  } else {
    // Check for command line parameters.
    if ( !_evaluate_params() ) { return 1; }
    { // Register any user-set options.
      if ( argc < 2 || _option_arg_indices[CL_HELP] ) {
        printf(
          "Usage for single-file volograms:\n"
          "%s [OPTIONS] -c MYFILE.VOLS\n\n"
          "Usage for multi-file volograms:\n"
          "%s [OPTIONS] -h HEADER.VOLS -s SEQUENCE.VOLS -v VIDEO.MP4\n\n",
          argv[0], argv[0] );
        _print_cl_flags();
        return 0;
      }
      all_frames = _option_arg_indices[CL_ALL_FRAMES] > 0;
      if ( _option_arg_indices[CL_COMBINED] ) {
        _input_combined_filename = my_argv[_option_arg_indices[CL_COMBINED] + 1];
      } else if ( !_option_arg_indices[CL_HEADER] && !_option_arg_indices[CL_SEQUENCE] ) {
        _printlog( _LOG_TYPE_WARNING, "Required argument --combined is missing. Run with --help for details.\n" );
        return 1;
      }
      if ( _option_arg_indices[CL_HEADER] ) {
        _input_header_filename = my_argv[_option_arg_indices[CL_HEADER] + 1];
      } else if ( !_option_arg_indices[CL_COMBINED] ) {
        _printlog( _LOG_TYPE_WARNING, "Required argument --header is missing. Run with --help for details.\n" );
        return 1;
      }
      if ( _option_arg_indices[CL_FIRST] ) {
        first_frame = atoi( my_argv[_option_arg_indices[CL_FIRST] + 1] );
        last_frame  = last_frame < first_frame ? first_frame : last_frame;
      }
      if ( _option_arg_indices[CL_LAST] ) {
        last_frame  = atoi( my_argv[_option_arg_indices[CL_LAST] + 1] );
        first_frame = first_frame >= last_frame ? last_frame : first_frame;
      }
      if ( _option_arg_indices[CL_OUTPUT_DIR] ) {
        _output_dir_path[0] = '\0';
        int plen            = (int)strlen( my_argv[_option_arg_indices[CL_OUTPUT_DIR] + 1] );
        int l               = plen < MAX_FILENAME_LEN - 1 ? plen : MAX_FILENAME_LEN - 1;
        strncat( _output_dir_path, my_argv[_option_arg_indices[CL_OUTPUT_DIR] + 1], l );
        // remove any existing path slashes and put a *nix slash at the end
        if ( l > 0 && ( _output_dir_path[l - 1] == '/' || _output_dir_path[l - 1] == '\\' ) ) { _output_dir_path[l - 1] = '\0'; }
        if ( l > 1 && _output_dir_path[l - 2] == '\\' ) { _output_dir_path[l - 2] = '\0'; }
        strncat( _output_dir_path, "/", MAX_SUBPATH_LEN - 1 );
        // If path doesn't exist try making that folder.
        if ( !_does_dir_exist( _output_dir_path ) ) {
          if ( !_make_dir( _output_dir_path ) ) {
            _output_dir_path[0] = '\0';
            return 1;
          }
        }
        _printlog( _LOG_TYPE_INFO, "Using output directory = `%s`\n", _output_dir_path );
      }
      if ( _option_arg_indices[CL_PREFIX] ) {
        _prefix_str[0] = '\0';
        int plen       = (int)strlen( my_argv[_option_arg_indices[CL_PREFIX] + 1] );
        int l          = plen < MAX_FILENAME_LEN - 1 ? plen : MAX_FILENAME_LEN - 1;
        strncat( _prefix_str, my_argv[_option_arg_indices[CL_PREFIX] + 1], l );
        _printlog( _LOG_TYPE_INFO, "Using output prefix = `%s`\n", _prefix_str );
        // NOTE(Anton) - Could parse here to exclude invalid chars but we have to know something about the encoding; it could be UTF-8 or UTF-16.
      }
      if ( _option_arg_indices[CL_SEQUENCE] ) {
        _input_sequence_filename = my_argv[_option_arg_indices[CL_SEQUENCE] + 1];
      } else if ( !_option_arg_indices[CL_COMBINED] ) {
        _printlog( _LOG_TYPE_WARNING, "Required argument --sequence is missing. Run with --help for details.\n" );
        return 1;
      }
      if ( _option_arg_indices[CL_VIDEO] ) {
        _input_video_filename = argv[_option_arg_indices[CL_VIDEO] + 1];
      } else if ( !_option_arg_indices[CL_COMBINED] ) {
        _printlog( _LOG_TYPE_WARNING, "Required argument --video is missing. Run with --help for details.\n" );
        return 1;
      }
    } // endblock register user-set options.
  }

  if ( all_frames ) {
    first_frame = last_frame = 0;
    _printlog( _LOG_TYPE_INFO, "Converting\n  frames\t\t all\n  header\t\t`%s`\n  sequence\t\t`%s`\n  video texture\t\t`%s`\n", _input_header_filename,
      _input_sequence_filename, _input_video_filename );
  } else {
    _printlog( _LOG_TYPE_INFO, "Converting\n  frames\t\t %i-%i\n  header\t\t`%s`\n  sequence\t\t`%s`\n  video texture\t\t`%s`\n", first_frame, last_frame,
      _input_header_filename, _input_sequence_filename, _input_video_filename );
  }

  if ( !_process_vologram( first_frame, last_frame, all_frames ) ) { return 1; }

  _printlog( _LOG_TYPE_SUCCESS, "Vologram processing completed.\n" );

  if ( _output_blocks_ptr ) { free( _output_blocks_ptr ); }

  return 0;
}
