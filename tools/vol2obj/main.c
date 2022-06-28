/** @file main.h
 * Volograms VOLS to Wavefront OBJ converter.
 *
 * vol2obj   | Vologram frame to OBJ+image converter.
 * --------- | ----------
 * Version   | 0.7.0
 * Authors   | Anton Gerdelan <anton@volograms.com>
 * Copyright | 2021, Volograms (http://volograms.com/)
 * Language  | C99
 * Files     | 1
 * Licence   | The MIT License. See LICENSE.md for details.
 * Notes     | Internally this uses FFMPEG to stream audio/video from a webm file or other media.
 *
 * Current Limitations
 * -----------
 * * Only Volograms with normals are currently supported (this means older vologram captures won't convert yet).
 * * Only Volograms with 16-bit indices are currently supported (the most common variant).
 *
 * Usage Instructions
 * -----------
 * Usage:
 *
 *`./vol2obj.bin -h HEADER.VOLS -s SEQUENCE.VOLS -v VIDEO.MP4 -f FRAME_NUMBER`
 *
 * Where `FRAME_NUMBER` is a frame you'd like to extract from the sequence, with 0 being the first frame.
 * If you request a frame outside range an error will be reported.
 * You can also output every frame with the `--all` option, or a specific range of frames using `-f FIRST -l LAST`,
 * for frame numbers `FIRST` to `LAST`, inclusive.
 *
 * Compilation:
 *
 * `make vol2obj`
 *
 * or e.g.
 * `gcc -o vol2obj.bin tools/vol2obj/main.c -I lib/ -I thirdparty/ lib/vol_geom.c lib/vol_av.c -L ./ -lavcodec -lavdevice -lavformat -lavutil -lswscale -lm`
 * TODOs
 * -----------
 *
 * FEATURES
 * - no-normals volograms (older ones) support
 * - flags to request PNG or JPEG or DDS using eg stb_image_write
 * - other file writing libraries
 * - add support for non-u16 indices
 * - validate params and optional params to write_obj()
 *
 * TESTING
 * - fuzzing
 * - test a mesh that doesnt have normals
 * - and a mesh with 32-bit indices
 *
 * History
 * -----------
 * - 0.7.0   (2022/07/15) - --prefix flag, and updated vol_libs.
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

#include "vol_av.h"   // Volograms' vol_av texture video library.
#include "vol_geom.h" // Volograms' vol_geom .vols file parsing library.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
// not #if defined(_WIN32) || defined(_WIN64) because we have strncasecmp in mingw
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#else
#include <strings.h> // strcasecmp
#endif
#if defined( _WIN32 ) || defined( _WIN64 )
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>   // DIR
#include <sys/stat.h> // mkdir
#include <errno.h>
#endif

#define MAX_FILENAME_LEN 4096 // Maximum file path length

/** Different output image format options.
NOTE(Anton) We could add DDS/ktx/basis here.
*/
typedef enum img_fmt_t {
  IMG_FMT_PPM = 0, // homemade
  IMG_FMT_JPG,     // uses library
  IMG_FMT_MAX      // just for counting # image formats
} img_fmt_t;

/** Command-line flags. */
typedef struct cl_flag_t {
  const char* long_str;  // e.g. "--header"
  const char* short_str; // e.g. "-h"
  const char* help_str;  // e.g. "Required. The next argument gives the path to the header.vols file.\n"
} cl_flag_t;

/// Globals for parsing the command line arguments in a function.
static int my_argc;
static char** my_argv;

/** Convience enum to index into the array of command-line flags by readable name. */
typedef enum cl_flag_enum_t { CL_ALL_FRAMES, CL_HEADER, CL_HELP, CL_FIRST, CL_LAST, CL_OUTPUT_DIR, CL_PREFIX, CL_SEQUENCE, CL_VIDEO, CL_MAX } cl_flag_enum_t;

/** All command line flags are specified here. Note that this order must correspond to the ordering in cl_flag_enum_t. */
static cl_flag_t _cl_flags[CL_MAX] = {
  { "--all", "-a",                                                                                     // CL_ALL_FRAMES
    "Create output files for, and process, all frames found in the sequence.\n"                        //
    "If given then paramters -f and -l are ignored.\n" },                                              //
  { "--header", "-h", "Required. The next argument gives the path to the header.vols file.\n" },       // CL_HEADER
  { "--help", NULL, "Prints this text.\n" },                                                           // CL_HELP
  { "--first", "-f",                                                                                   // CL_FIRST
    "The next argument gives the frame number of the first frame to process (frames start at 0).\n"    //
    "If the -l parameter is not given then only this single frame is processed.\n"                     //
    "Default value 0.\n" },                                                                            //
  { "--last", "-l",                                                                                    // CL_LAST
    "The next argument gives the frame number of the last frame to process.\n"                         //
    "Can be used with -f to process a range of frames from first to last, inclusive.\n" },             //
  { "--output-dir", "-o",                                                                              // CL_OUTPUT_DIR
    "The next argument gives the path to a directory to write output files into.\n"                    //
    "Default is the current working directory.\n" },                                                   //
  { "--prefix", "-p",                                                                                  // CL_PREFIX
    "The next argument gives the prefix to use for output filenames.\n"                                //
    "Default is output_frame_.\n" },                                                                   //
  { "--sequence", "-s", "Required. The next argument gives the path to the sequence_0.vols file.\n" }, // CL_SEQUENCE
  { "--video", "-v", "Required. The next argument gives the path to the video texture file.\n" }       // CL_VIDEO
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

/** Look for a string amongst command-line arguments.
 * @param check_str The string to match (case insensitive).
 * @return If not found returns 0. Otherwise returns the index number into argv matching `check_str`.
 */
static int _check_param( const char* check_str ) {
  if ( !check_str ) { return 0; }
  for ( int i = 1; i < my_argc; i++ ) {
    if ( 0 == strcasecmp( check_str, my_argv[i] ) ) { return i; }
  }
  return 0;
}

/** Checks for either of the long --version or short -v string in command line arguments, and if one is found returns the index of that. Prefers the long string
 * index. */
static int _check_cl_option( const char* long_str, const char* short_str ) {
  int long_idx = 0, short_idx = 0;
  if ( long_str ) { long_idx = _check_param( long_str ); }
  if ( short_str ) { short_idx = _check_param( short_str ); }
  if ( long_idx ) { return long_idx; }
  return short_idx;
}

static img_fmt_t _img_fmt = IMG_FMT_JPG;             // Image format to use for output.
static int _jpeg_quality  = 95;                      // Arbitrary choice of 95% quality v size based on GIMP's default.
static vol_av_video_t _av_info;                      // Audio-video information from vol_av library.
static vol_geom_info_t _geom_info;                   // Mesh information from vol_geom library.
static char* _input_header_filename;                 // e.g. `header.vols`
static char* _input_sequence_filename;               // e.g. `sequence.vols`
static char* _input_video_filename;                  // e.g. `texture_1024.webm`
static char _output_dir_path[MAX_FILENAME_LEN];      // e.g. `my_output/`
static char _output_mesh_filename[MAX_FILENAME_LEN]; // e.g. `output_frame_00000000.obj`
static char _output_mtl_filename[MAX_FILENAME_LEN];  // e.g. `output_frame_00000000.mtl`
static char _output_img_filename[MAX_FILENAME_LEN];  // e.g. `output_frame_00000000.jpg`
static char _material_name[MAX_FILENAME_LEN];        // e.g. `volograms_mtl_00000000`
static char _prefix_str[MAX_FILENAME_LEN];           // defaults to `output_frame_`

/// A homemade P3 ASCII PPM image writer. These images are very large, so only useful for debugging purposes.
static bool write_rgb_image_to_ppm( const char* filename, uint8_t* image_ptr, int w, int h ) {
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

/// Writes the latest pixel buffer stored in _av_info into a file in the appropriate format.
static bool _write_video_frame_to_image( const char* output_image_filename ) {
  if ( !output_image_filename ) { return false; }

  char full_path[MAX_FILENAME_LEN];
  sprintf( full_path, "%s%s", _output_dir_path, output_image_filename );

  switch ( _img_fmt ) {
  case IMG_FMT_PPM: {
    if ( !write_rgb_image_to_ppm( full_path, _av_info.pixels_ptr, _av_info.w, _av_info.h ) ) {
      fprintf( stderr, "ERROR: Writing frame image file `%s`.\n", full_path );
      return false;
    }
  } break;
  case IMG_FMT_JPG: {
    if ( !stbi_write_jpg( full_path, _av_info.w, _av_info.h, 3, _av_info.pixels_ptr, _jpeg_quality ) ) {
      fprintf( stderr, "ERROR: Writing frame image file `%s`.\n", full_path );
      return false;
    }
  } break;
  default: fprintf( stderr, "ERROR: No valid image format selected\n" ); return false;
  } // endswitch

  printf( "Wrote image file `%s`\n", full_path );

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
    fprintf( stderr, "ERROR: Opening file for writing `%s`\n", full_path );
    return false;
  }
  if ( fprintf( f_ptr, "newmtl %s\n", material_name ) < 0 ) {
    fprintf( stderr, "ERROR: Writing to file `%s`, check permissions.\n", full_path );
    return false;
  }
  if ( fprintf( f_ptr, "map_Kd %s\nmap_Ka %s\n", image_filename, image_filename ) < 0 ) {
    fprintf( stderr, "ERROR: Writing to file `%s`, check permissions.\n", full_path );
    return false;
  }
  fprintf( f_ptr, "Ka 0.1 0.1 0.1\n" );
  fprintf( f_ptr, "Kd 0.9 0.9 0.9\n" );
  fprintf( f_ptr, "Ks 0.0 0.0 0.0\n" );
  fprintf( f_ptr, "d 1.0\nTr 0.0\n" );
  fprintf( f_ptr, "Ns 0.0\n" );
  fclose( f_ptr );
  printf( "Wrote material file `%s`\n", full_path );

  return true;
}

/**
 * @param output_mtl_filename If NULL then no MTL section or link is added to the Obj.
 */
static bool _write_mesh_to_obj_file( const char* output_mesh_filename, const char* output_mtl_filename, const char* material_name, const float* vertices_ptr,
  int n_vertices, const float* texcoords_ptr, int n_texcoords, const float* normals_ptr, int n_normals, const void* indices_ptr, int n_indices, int index_type ) {
  if ( !output_mesh_filename ) { return false; }

  // TODO(Anton) validate fprintfs here too

  char full_path[MAX_FILENAME_LEN];
  sprintf( full_path, "%s%s", _output_dir_path, output_mesh_filename );

  FILE* f_ptr = fopen( full_path, "w" );
  if ( !f_ptr ) {
    fprintf( stderr, "ERROR: Opening file for writing `%s`\n", full_path );
    return false;
  }

  fprintf( f_ptr, "#Exported by Volograms vols2obj\n" );
  if ( output_mtl_filename ) {
    fprintf( f_ptr, "mtllib %s\n", output_mtl_filename ); // mtllib must go before usemtl or some viewers won't load the texture.
    fprintf( f_ptr, "usemtl %s\n", material_name );
  }

  assert( vertices_ptr && "Hey if there are no vertex points Anton should make sure that is accounted for in the f section" );
  if ( vertices_ptr ) {
    for ( int i = 0; i < n_vertices; i++ ) {
      float x = vertices_ptr[i * 3 + 0];
      float y = vertices_ptr[i * 3 + 1];
      float z = vertices_ptr[i * 3 + 2];
      fprintf( f_ptr, "v %0.3f %0.3f %0.3f\n", -x, y, z ); // NOTE(Anton) reversed X. could instead reverse Z but then need to import in blender as "Z forward".
    }
  }
  assert( texcoords_ptr && "Hey if there are no UVs Anton should make sure that is accounted for in the f section" );
  if ( texcoords_ptr ) {
    for ( int i = 0; i < n_texcoords; i++ ) {
      float s = texcoords_ptr[i * 2 + 0];
      float t = texcoords_ptr[i * 2 + 1];
      fprintf( f_ptr, "vt %0.3f %0.3f\n", s, t );
    }
  }
  assert( normals_ptr && "Hey if there are no normals Anton should make sure that is accounted for in the f section" );
  if ( normals_ptr ) {
    for ( int i = 0; i < n_normals; i++ ) {
      float x = normals_ptr[i * 3 + 0];
      float y = normals_ptr[i * 3 + 1];
      float z = normals_ptr[i * 3 + 2];
      fprintf( f_ptr, "vn %0.3f %0.3f %0.3f\n", -x, y, z );
    }
  }
  assert( indices_ptr && "Hey if there are no indices Anton should make sure that is accounted for in the f section" );
  if ( indices_ptr ) {
    // TODO(Anton) when adding support for additional indices types these may be required:
    // uint32_t* i_u32_ptr = (uint32_t*)indices_ptr;
    // uint8_t* i_u8_ptr   = (uint8_t*)indices_ptr;
    uint16_t* i_u16_ptr = (uint16_t*)indices_ptr;
    // OBJ spec:
    // "Faces are defined using lists of vertex, texture and normal indices in the format vertex_index/texture_index/normal_index for which each index starts
    // at 1"
    for ( int i = 0; i < n_indices / 3; i++ ) {
      // index types: 0=unsigned byte, 1=unsigned short, 2=unsigned int.
      /* Integer[] if # vertices >= 65535 (Unity Version < 2017.3 does not support Integer indices) Short[] if # vertices < 65535 */
      assert( index_type == 1 );                 // NOTE(Anton) can come back and support others later
      int a = (int)( i_u16_ptr[i * 3 + 0] ) + 1; // note +1s here!
      int b = (int)( i_u16_ptr[i * 3 + 1] ) + 1;
      int c = (int)( i_u16_ptr[i * 3 + 2] ) + 1;
      // NOTE(Anton) VOLS winding order is CW (similar to Unity) rather than typical CCW so let's reverse it for OBJ
      fprintf( f_ptr, "f %i/%i/%i %i/%i/%i %i/%i/%i\n", c, c, c, b, b, b, a, a, a );
    }
  }

  fclose( f_ptr );
  printf( "Wrote mesh file `%s`\n", full_path );

  return true;
}

/**
 * @param output_mtl_filename If NULL then no MTL section or link is added to the Obj.
 */
static bool _write_geom_frame_to_mesh( const char* hdr_filename, const char* seq_filename, const char* output_mesh_filename, const char* output_mtl_filename,
  const char* material_name, int frame_idx ) {
  if ( !hdr_filename || !seq_filename || !output_mesh_filename || frame_idx < 0 ) { return false; }

  vol_geom_frame_data_t keyframe_data           = ( vol_geom_frame_data_t ){ .block_data_sz = 0 };
  vol_geom_frame_data_t intermediate_frame_data = ( vol_geom_frame_data_t ){ .block_data_sz = 0 };
  bool success                                  = true;

  int prev_key_idx = vol_geom_find_previous_keyframe( &_geom_info, frame_idx );

  uint8_t *points_ptr = NULL, *texcoords_ptr = NULL, *normals_ptr = NULL, *indices_ptr = NULL;
  int32_t points_sz = 0, texcoords_sz = 0, normals_sz = 0, indices_sz = 0;

  { // get data pointers
    // if our frame isn't a keyframe then we need to load up the previous keyframe's data first...
    if ( !vol_geom_read_frame( seq_filename, &_geom_info, prev_key_idx, &keyframe_data ) ) {
      printf( "ERROR: Reading geometry keyframe %i\n", prev_key_idx );
      return false;
    }

    points_ptr    = &keyframe_data.block_data_ptr[keyframe_data.vertices_offset];
    texcoords_ptr = &keyframe_data.block_data_ptr[keyframe_data.uvs_offset];
    if ( _geom_info.hdr.normals ) { normals_ptr = &keyframe_data.block_data_ptr[keyframe_data.normals_offset]; }
    indices_ptr  = &keyframe_data.block_data_ptr[keyframe_data.indices_offset];
    points_sz    = keyframe_data.vertices_sz;
    texcoords_sz = keyframe_data.uvs_sz;
    normals_sz   = keyframe_data.normals_sz;
    indices_sz   = keyframe_data.indices_sz;

    // ...and then add our frame's subset of the data second
    if ( prev_key_idx != frame_idx ) {
      // read the non-keyframe (careful with mem leaks)
      if ( !vol_geom_read_frame( seq_filename, &_geom_info, frame_idx, &intermediate_frame_data ) ) {
        printf( "ERROR: Reading geometry intermediate frame %i\n", frame_idx );
        return false;
      }
      points_ptr = &intermediate_frame_data.block_data_ptr[intermediate_frame_data.vertices_offset];
      points_sz  = intermediate_frame_data.vertices_sz;
      if ( _geom_info.hdr.normals ) {
        normals_ptr = &intermediate_frame_data.block_data_ptr[intermediate_frame_data.normals_offset];
        normals_sz  = intermediate_frame_data.normals_sz;
      }
    }
  } // endblock get data pointers

  // write the .obj
  int n_points    = points_sz / ( sizeof( float ) * 3 );
  int n_texcoords = texcoords_sz / ( sizeof( float ) * 2 );
  int n_normals   = normals_sz / ( sizeof( float ) * 3 );
  // NOTE(Anton) hacked this in so only supporting uint32_t indices for first pass
  int indices_type = 1;                               // 1 is uint16_t
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
    fprintf( stderr, "ERROR: Failed to write mesh file `%s`\n", output_mesh_filename );
    // not /returning/ false yet, but just setting a flag, because we still want to release resources
    success = false;
  } // endif write mesh

  return success;
}

/** Write frames between `first_frame_idx` and `last_frame_idx`, or all of them, if `all_frames` is set,
 * to mesh, material, and image files.
 * @return Returns false on error.
 */
static bool _process_vologram( int first_frame_idx, int last_frame_idx, bool all_frames ) {
  { // mesh and video processing
    if ( !vol_geom_create_file_info( _input_header_filename, _input_sequence_filename, &_geom_info, true ) ) {
      fprintf( stderr, "ERROR: Failed to open geometry files %s %s\n", _input_header_filename, _input_sequence_filename );
      return false;
    }

    int n_frames = _geom_info.hdr.frame_count;
    if ( first_frame_idx >= n_frames ) {
      fprintf( stderr, "ERROR: Frame %i is not in range of geometry's %i frames\n", first_frame_idx, n_frames );
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
      default: fprintf( stderr, "ERROR: No valid image format selected\n" ); return false;
      } // endswitch

      // and geometry
      if ( !_write_geom_frame_to_mesh( _input_header_filename, _input_sequence_filename, _output_mesh_filename, _output_mtl_filename, _material_name, i ) ) {
        fprintf( stderr, "ERROR: Failed to write geometry frame %i to file\n", i );
        return false;
      }
      // material file
      if ( !_write_mtl_file( _output_mtl_filename, _material_name, _output_img_filename ) ) {
        fprintf( stderr, "ERROR: Failed to write material file for frame %i\n", i );
        return false;
      }
    }

    if ( !vol_geom_free_file_info( &_geom_info ) ) {
      fprintf( stderr, "ERROR: Failed to free geometry info\n" );
      return false;
    }
  } // endblock mesh processing

  { // video processing
    if ( !vol_av_open( _input_video_filename, &_av_info ) ) {
      fprintf( stderr, "ERROR: Failed to open video file %s\n", _input_video_filename );
      return false;
    }
    int n_frames = (int)vol_av_frame_count( &_av_info );
    if ( first_frame_idx >= n_frames ) {
      fprintf( stderr, "ERROR: Frame %i is not in range of video's %i frames\n", first_frame_idx, n_frames );
      return false;
    }
    last_frame_idx = all_frames ? n_frames - 1 : last_frame_idx;

    // skip up to first frame to write
    for ( int i = 0; i < first_frame_idx; i++ ) {
      if ( !vol_av_read_next_frame( &_av_info ) ) {
        fprintf( stderr, "ERROR: Reading frames from video sequence.\n" );
        return false;
      }
    }
    // write any frames in the range we want
    for ( int i = first_frame_idx; i <= last_frame_idx; i++ ) {
      if ( !vol_av_read_next_frame( &_av_info ) ) {
        fprintf( stderr, "ERROR: Reading frames from video sequence.\n" );
        return false;
      }
      switch ( _img_fmt ) {
      case IMG_FMT_PPM: sprintf( _output_img_filename, "%s%08i.ppm", _prefix_str, i ); break;
      case IMG_FMT_JPG: sprintf( _output_img_filename, "%s%08i.jpg", _prefix_str, i ); break;
      default: fprintf( stderr, "ERROR: No valid image format selected\n" ); return false;
      } // endswitch
      if ( !_write_video_frame_to_image( _output_img_filename ) ) {
        fprintf( stderr, "ERROR: failed to write video frame %i to file\n", first_frame_idx );
        return false; // make sure we stop the processing at this point rather than carry on.
      }
    }

    if ( !vol_av_close( &_av_info ) ) {
      fprintf( stderr, "ERROR: Failed to close video info\n" );
      return false;
    }
  } // endblock video processing
  return true;
}

static bool _does_dir_exist( const char* dir_path ) {
#if defined( _WIN32 ) || defined( _WIN64 )
  DWORD attribs = GetFileAttributes( dir_path );
  return ( attribs != INVALID_FILE_ATTRIBUTES && ( attribs & FILE_ATTRIBUTE_DIRECTORY ) );
#else
  DIR* dir = opendir( dir_path );
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
    printf( "Created directory `%s`\n", dir_path );
    return true;
  }
#else
  if ( 0 == mkdir( dir_path, S_IRWXU ) ) { // Read-Write-eXecute permissions
    printf( "Created directory `%s`\n", dir_path );
    return true;
  }
#endif
  fprintf( stderr, "ERROR: Creating directory `%s`\n", dir_path );
  return false;
}

/** @return Index of the first invalid-looking user -flag, or 0 if none found. */
static int _find_invalid_user_arg() {
  for ( int a_i = 1; a_i < my_argc; a_i++ ) {
    if ( my_argv[a_i][0] != '-' ) { continue; }
    for ( int c_i = 0; c_i < CL_MAX; c_i++ ) {
      if ( ( _cl_flags[c_i].short_str && 0 == strncasecmp( my_argv[a_i], _cl_flags[c_i].short_str, 4 ) ) ||
           ( _cl_flags[c_i].long_str && 0 == strncasecmp( my_argv[a_i], _cl_flags[c_i].long_str, 32 ) ) ) {
        return 0;
      }
    }
    return a_i;
  }
  return 0;
}

/// Default string names for Volu video texture files.
#define VOL_VID_STR_2048 "texture_2048_h264.mp4"
#define VOL_VID_STR_1024 "texture_1024_h264.mp4"

int main( int argc, char** argv ) {
  // paths for drag-and-drop directory
  char dad_hdr_str[MAX_FILENAME_LEN], dad_seq_str[MAX_FILENAME_LEN], dad_vid_str[MAX_FILENAME_LEN], test_vid_str[MAX_FILENAME_LEN];
  int first_frame = 0;
  int last_frame  = 0;
  bool all_frames = false;

  my_argc        = argc;
  my_argv        = argv;
  dad_hdr_str[0] = dad_seq_str[0] = dad_vid_str[0] = test_vid_str[0] = '\0';
  strcpy( _prefix_str, "output_frame_" ); // set the default

  // Check for invalid arguments.
  int inv_idx = _find_invalid_user_arg();
  if ( inv_idx > 0 ) {
    fprintf( stderr, "ERROR: Option `%s` is not recognised. Use option --help to print available options.\n", argv[inv_idx] );
    return 1;
  }

  // check for drag-and-drop directory
  if ( 2 == argc && _does_dir_exist( argv[1] ) ) {
    int len = strlen( argv[1] );
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
  } else {
    // Check for command line parameters
		
    if ( argc < 2 || _check_param( "--help" ) ) {
      printf( "Usage %s [OPTIONS] -h HEADER.VOLS -s SEQUENCE.VOLS -v VIDEO.MP4\n", argv[0] );
      _print_cl_flags();
      return 0;
    }

    int flag_indices[CL_MAX];
    memset( flag_indices, 0, sizeof( int ) * CL_MAX );
    for ( int i = 0; i < CL_MAX; i++ ) { flag_indices[i] = _check_cl_option( _cl_flags[i].long_str, _cl_flags[i].short_str ); }
    // Make sure mandatory flags are supplied.
    if ( flag_indices[CL_HEADER] <= 0 || flag_indices[CL_HEADER] + 1 >= argc ) {
      fprintf( stderr, "ERROR: Header argument, -h MYHEADER.vols, is mandatory.\n" );
      return 1;
    }
    if ( flag_indices[CL_SEQUENCE] <= 0 || flag_indices[CL_SEQUENCE] + 1 >= argc ) {
      fprintf( stderr, "ERROR: Sequence argument, -s MYSEQUENCE.vols, is mandatory.\n" );
      return 1;
    }
    if ( flag_indices[CL_VIDEO] <= 0 || flag_indices[CL_VIDEO] + 1 >= argc ) {
      fprintf( stderr, "ERROR: Video argument, -v MYVIDEO.vols, is mandatory.\n" );
      return 1;
    }
    // Check for this: `./vol2obj -h -s sequence -v video`, where `-s` is interpreted as the header path.
    for ( int i = 0; i < CL_MAX; i++ ) {
      if ( flag_indices[i] == flag_indices[CL_HEADER] + 1 ) {
        fprintf( stderr, "ERROR: Header argument, -h, must be followed by a path to a file.\n" );
        return 1;
      }
      if ( flag_indices[i] == flag_indices[CL_SEQUENCE] + 1 ) {
        fprintf( stderr, "ERROR: Sequence argument, -s, must be followed by a path to a file.\n" );
        return 1;
      }
      if ( flag_indices[i] == flag_indices[CL_VIDEO] + 1 ) {
        fprintf( stderr, "ERROR: Video argument, -v, must be followed by a path to a file.\n" );
        return 1;
      }
    }

    all_frames = flag_indices[CL_ALL_FRAMES] > 0;
    if ( flag_indices[CL_FIRST] ) {
      if ( flag_indices[CL_FIRST] >= argc - 1 ) {
        fprintf( stderr, "ERROR: -f parameter must be followed by a frame number.\n" );
        return 1;
      }
      first_frame = atoi( argv[flag_indices[CL_FIRST] + 1] );
      last_frame  = last_frame < first_frame ? first_frame : last_frame;
    }
    if ( flag_indices[CL_HEADER] ) {
      if ( flag_indices[CL_HEADER] >= argc - 1 ) {
        fprintf( stderr, "ERROR: -h parameter must be followed by a file path.\n" );
        return 1;
      }
      _input_header_filename = argv[flag_indices[CL_HEADER] + 1];
    }
    if ( flag_indices[CL_LAST] ) {
      if ( flag_indices[CL_LAST] >= argc - 1 ) {
        fprintf( stderr, "ERROR: -l parameter must be followed by a frame number.\n" );
        return 1;
      }
      last_frame  = atoi( argv[flag_indices[CL_LAST] + 1] );
      first_frame = first_frame >= last_frame ? last_frame : first_frame;
    }
    if ( flag_indices[CL_PREFIX] ) {
      if ( flag_indices[CL_PREFIX] >= argc - 1 ) {
        fprintf( stderr, "ERROR: --prefix parameter must be followed by a string of characters.\n" );
        return 1;
      }
      _prefix_str[0] = '\0';
      int plen       = (int)strlen( argv[flag_indices[CL_PREFIX] + 1] );
      int l          = plen < MAX_FILENAME_LEN - 1 ? plen : MAX_FILENAME_LEN - 1;
      strncat( _prefix_str, argv[flag_indices[CL_PREFIX] + 1], l );
      printf( "Using output prefix = `%s`\n", _prefix_str );

      // NOTE(Anton) - Could parse here to exclude invalid chars but we have to know something about the encoding; it could be UTF-8 or UTF-16.
    }
    if ( flag_indices[CL_OUTPUT_DIR] ) {
      if ( flag_indices[CL_OUTPUT_DIR] >= argc - 1 ) {
        fprintf( stderr, "ERROR: --output_dir parameter must be followed by a file path.\n" );
        return 1;
      }
      _output_dir_path[0] = '\0';
      int plen            = (int)strlen( argv[flag_indices[CL_OUTPUT_DIR] + 1] );
      int l               = plen < MAX_FILENAME_LEN - 1 ? plen : MAX_FILENAME_LEN - 1;
      strncat( _output_dir_path, argv[flag_indices[CL_OUTPUT_DIR] + 1], l );

      // remove any existing path slashes and put a *nix slash at the end
      if ( l > 0 && ( _output_dir_path[l - 1] == '/' || _output_dir_path[l - 1] == '\\' ) ) { _output_dir_path[l - 1] = '\0'; }
      if ( l > 1 && _output_dir_path[l - 2] == '\\' ) { _output_dir_path[l - 2] = '\0'; }
      strncat( _output_dir_path, "/", MAX_FILENAME_LEN - 1 );

      // if path doesn't exist try making that folder.
      if ( !_does_dir_exist( _output_dir_path ) ) {
        if ( !_make_dir( _output_dir_path ) ) { _output_dir_path[0] = '\0'; }
      }
      printf( "Using output directory = `%s`\n", _output_dir_path );
    }
    if ( flag_indices[CL_SEQUENCE] ) {
      if ( flag_indices[CL_SEQUENCE] >= argc - 1 ) {
        fprintf( stderr, "ERROR: -s parameter must be followed by a file path.\n" );
        return 1;
      }
      _input_sequence_filename = argv[flag_indices[CL_SEQUENCE] + 1];
    }
    if ( flag_indices[CL_VIDEO] ) {
      if ( flag_indices[CL_VIDEO] >= argc - 1 ) {
        fprintf( stderr, "ERROR: -v parameter must be followed by a file path.\n" );
        return 1;
      }
      _input_video_filename = argv[flag_indices[CL_VIDEO] + 1];
    }
  }

  if ( all_frames ) {
    first_frame = last_frame = 0;
    printf( "Converting\n  frames\t\t all\n  header\t\t`%s`\n  sequence\t\t`%s`\n  video texture\t`%s`\n", _input_header_filename, _input_sequence_filename,
      _input_video_filename );
  } else {
    printf( "Converting\n  frames\t\t %i-%i\n  header\t\t`%s`\n  sequence\t\t`%s`\n  video texture\t`%s`\n", first_frame, last_frame, _input_header_filename,
      _input_sequence_filename, _input_video_filename );
  }

  if ( !_process_vologram( first_frame, last_frame, all_frames ) ) { return 1; }

  printf( "Vologram processing completed.\n" );

  return 0;
}
