/** @file vols_cutter.hpp
 * Volograms VOLS to Wavefront OBJ converter.
 *
 * vol2obj   | Vologram frame to OBJ+image converter.
 * --------- | ----------
 * Version   | 0.3
 * Authors   | Konstantinos Amplianitis <kostas@volograms.com>
 * Copyright | 2022, Volograms (http://volograms.com/)
 * Language  | C++
 * Files     | 3
 * Licence   | The MIT License. See LICENSE.md for details.
 * Notes     | This program invokes command-line FFmpeg tool to convert video files.
 */

#ifndef VOLS_CUTTER_HPP_
#define VOLS_CUTTER_HPP_

#include <iostream>
#include <fstream>
#include <vector>

struct HeaderInfo {
  std::string format_name;
  int version;
  int compression;
  std::string mesh_name;
  std::string material_name;
  std::string shader_name;
  int topology;
  int frame_count;
  bool hasNormals;
  bool isTextured;
  unsigned short texture_width;
  unsigned short texture_height;
  unsigned short texture_format;
  std::vector<float> translation;
  std::vector<float> rotation;
  float scale;
};

struct Frame {
  int frame_number;
  int size_mesh_in_bytes;
  int keyframe; // NOTE(Anton) this should be 1 byte but code handles it as such.
  int size_vertices_in_bytes;
  std::vector<char> data_vectices;
  int size_normals_in_bytes;
  std::vector<char> data_normals;
  int size_indinces_in_bytes;
  std::vector<char> data_indinces;
  int size_uvs_in_bytes;
  std::vector<char> data_uvs;
  int size_textures_in_bytes;
  std::vector<char> data_textures;
  int frame_data_size;
}; // frame_init_values=;

class Sequence {
  public:
  Sequence();
  ~Sequence() {}

  // Define set functions.
  void setFolderVOLS( std::string& _file ) { folder_name_VOLS_ = _file; };

  void setOutputDir( std::string& _dir ) { dir_output_ = _dir; };

  void setFirstFrame( int _frameNo ) { first_frame_ = _frameNo; };

  void setLastFrame( int _frameNo ) { last_frame_ = _frameNo; };

  void print_values( bool _status = true ) { print_values_ = _status; }

  // Define get functions.
  const HeaderInfo getHeaderInfo() { return header_; };
  std::vector<Frame>& getFrames() { return sequence_; };

  // Define reading functions.
  int readHeaderFileVOLS();
  int readSequenceFileVOLS();

  // Define writing functions.
  int writeUpdatedHeadertoVOLS();
  int writeCutSequencetoVOLS();

  private:
  std::string folder_name_VOLS_;
  std::string dir_output_;
  bool print_values_;
  int first_frame_;
  int last_frame_;
  int framecountindex_;
  HeaderInfo header_{ "", 0, 0, "", "", "", 0, 0, false, false, 0, 0, 0, std::vector<float>( 3 ), std::vector<float>( 4 ), 1.0f };
  std::vector<Frame> sequence_;
};

#endif // VOLS_CUTTER_HPP_
