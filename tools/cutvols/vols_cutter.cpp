/** @file vols_cutter.cpp
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

#include "vols_cutter.hpp"

Sequence::Sequence()
    : folder_name_VOLS_( "" ), dir_output_( "" ), print_values_( false ), first_frame_( 0 ), last_frame_( 0 ),
      framecountindex_( 0 ), header_{ "", 0, 0, "", "", "", 0, 0, false, false, 0, 0, 0, std::vector<float>( 3 ), std::vector<float>( 4 ), 1.0f }, sequence_( 0 ) {}

int Sequence::readHeaderFileVOLS() {
  std::fstream file( folder_name_VOLS_ + "header.vols", std::ios::in | std::ios::binary );

  if ( file.is_open() ) {
    std::vector<char> buffer( std::istreambuf_iterator<char>( file ), {} );

    char* a = new char[buffer.size()];
    std::copy( buffer.begin(), buffer.end(), a );

    unsigned int index              = 0;
    unsigned int format_name_length = a[index];
    index += sizeof( char );

    for ( int i = 0; i < format_name_length; i++ ) header_.format_name += a[index + i];
    index += format_name_length * sizeof( char );

    header_.version = *( reinterpret_cast<int*>( &a[index] ) );
    index += sizeof( int );
    header_.compression = *( reinterpret_cast<int*>( &a[index] ) );
    index += sizeof( int );

    int mesh_name_length = a[index];
    index += sizeof( char );
    for ( int i = 0; i < mesh_name_length; i++ ) header_.mesh_name += a[index + i];
    index += mesh_name_length * sizeof( char );

    int material_name_length = a[index];
    index += sizeof( char );
    for ( int i = 0; i < material_name_length; i++ ) header_.material_name += a[index + i];
    index += material_name_length * sizeof( char );

    int shader_name_length = a[index];
    index += sizeof( char );
    std::string shader_name = "";
    for ( int i = 0; i < shader_name_length; i++ ) header_.shader_name += a[index + i];
    index += shader_name_length * sizeof( char );

    header_.topology = *( reinterpret_cast<int*>( &a[index] ) );
    index += sizeof( int );
    framecountindex_    = index;
    header_.frame_count = *( reinterpret_cast<int*>( &a[index] ) );
    index += sizeof( int );
    header_.hasNormals = char( a[index] );
    index += sizeof( bool );
    header_.isTextured = char( a[index] );
    index += sizeof( bool );
    header_.texture_width = *( reinterpret_cast<unsigned short*>( &a[index] ) );
    index += sizeof( unsigned short );
    header_.texture_height = *( reinterpret_cast<unsigned short*>( &a[index] ) );
    index += sizeof( unsigned short );
    header_.texture_format = *( reinterpret_cast<unsigned short*>( &a[index] ) );
    index += sizeof( unsigned short );
    header_.translation[0] = *( reinterpret_cast<float*>( &a[index] ) );
    index += sizeof( float );
    header_.translation[1] = *( reinterpret_cast<float*>( &a[index] ) );
    index += sizeof( float );
    header_.translation[2] = *( reinterpret_cast<float*>( &a[index] ) );
    index += sizeof( float );
    header_.rotation[0] = *( reinterpret_cast<float*>( &a[index] ) );
    index += sizeof( float );
    header_.rotation[1] = *( reinterpret_cast<float*>( &a[index] ) );
    index += sizeof( float );
    header_.rotation[2] = *( reinterpret_cast<float*>( &a[index] ) );
    index += sizeof( float );
    header_.rotation[3] = *( reinterpret_cast<float*>( &a[index] ) );
    index += sizeof( float );
    header_.scale = *( reinterpret_cast<float*>( &a[index] ) );

    if ( print_values_ ) {
      std::cout << "\n----------------" << '\n';
      std::cout << "HEADER FILE" << '\n';
      std::cout << "----------------" << '\n';
      std::cout << "File size: " << buffer.size() << '\n';
      std::cout << "Format Name: " << this->getHeaderInfo().format_name << '\n';
      std::cout << "Version: " << this->getHeaderInfo().version << '\n';
      std::cout << "Compression: " << this->getHeaderInfo().compression << '\n';
      std::cout << "Mesh Name: " << this->getHeaderInfo().mesh_name << '\n';
      std::cout << "Material Name: " << this->getHeaderInfo().material_name << '\n';
      std::cout << "Shader Name: " << this->getHeaderInfo().shader_name << '\n';
      std::cout << "Topology: " << this->getHeaderInfo().topology << '\n';
      std::cout << "Number of Frames: " << this->getHeaderInfo().frame_count << '\n';
      std::cout << "Has Normals: " << this->getHeaderInfo().hasNormals << '\n';
      std::cout << "Is Textured: " << this->getHeaderInfo().isTextured << '\n';
      std::cout << "Texture Width: " << this->getHeaderInfo().texture_width << '\n';
      std::cout << "Texture Height: " << this->getHeaderInfo().texture_height << '\n';
      std::cout << "Texture Format: " << this->getHeaderInfo().texture_format << '\n';
      std::cout << "Translation: ";
      for ( int i = 0; i < getHeaderInfo().translation.size(); ++i ) std::cout << getHeaderInfo().translation[i] << ' ';

      std::cout << "\nRotation: ";
      for ( int i = 0; i < getHeaderInfo().rotation.size(); ++i ) std::cout << getHeaderInfo().rotation[i] << ' ';

      std::cout << "\ns: " << getHeaderInfo().scale << '\n';
      std::cout << "----------------"
                << "\n";
    }
  }

  return 0;
}

int Sequence::readSequenceFileVOLS() {
  std::ifstream file( folder_name_VOLS_ + "sequence_0.vols", std::ios::in | std::ios::binary );

  if ( file.is_open() ) {
    if ( print_values_ ) {
      std::cout << "\n----------------" << '\n';
      std::cout << "SEQUENCE FILE" << '\n';
      std::cout << "----------------" << '\n';
    }

    for ( int i = 0; i < this->getHeaderInfo().frame_count; ++i ) {
      Frame frame{ 0, 0, 0, 0, std::vector<char>( 0 ), 0, std::vector<char>( 0 ), 0, std::vector<char>( 0 ), 0, std::vector<char>( 0 ), 0, std::vector<char>( 0 ), 0 };

      file.read( (char*)&frame.frame_number, sizeof( int ) );
      file.read( (char*)&frame.size_mesh_in_bytes, sizeof( int ) );
      file.read( (char*)&frame.keyframe, sizeof( char ) );
      file.read( (char*)&frame.size_vertices_in_bytes, sizeof( int ) );

      frame.data_vectices.resize( frame.size_vertices_in_bytes );
      file.read( &( frame.data_vectices[0] ), frame.size_vertices_in_bytes );

      if ( (int)frame.keyframe == 0 ) {
        if ( this->getHeaderInfo().hasNormals ) {
          file.read( (char*)&frame.size_normals_in_bytes, sizeof( int ) );
          frame.data_normals.resize( frame.size_normals_in_bytes );
          file.read( &( frame.data_normals[0] ), frame.size_normals_in_bytes );
        }
      }

      if ( (int)frame.keyframe == 1 ) {
        if ( this->getHeaderInfo().hasNormals ) {
          file.read( (char*)&frame.size_normals_in_bytes, sizeof( int ) );
          frame.data_normals.resize( frame.size_normals_in_bytes );
          file.read( &( frame.data_normals[0] ), frame.size_normals_in_bytes );
        }

        file.read( (char*)&frame.size_indinces_in_bytes, sizeof( int ) );
        frame.data_indinces.resize( frame.size_indinces_in_bytes );
        file.read( &( frame.data_indinces[0] ), frame.size_indinces_in_bytes );

        file.read( (char*)&frame.size_uvs_in_bytes, sizeof( int ) );
        frame.data_uvs.resize( frame.size_uvs_in_bytes );
        file.read( &( frame.data_uvs[0] ), frame.size_uvs_in_bytes );

        if ( this->getHeaderInfo().isTextured ) {
          file.read( (char*)&frame.size_textures_in_bytes, sizeof( int ) );
          frame.data_textures.resize( frame.size_textures_in_bytes );
          file.read( &( frame.data_textures[0] ), frame.size_textures_in_bytes );
        }
      }

      file.read( (char*)&frame.frame_data_size, sizeof( int ) );

      sequence_.push_back( frame );

      if ( print_values_ ) {
        std::cout << "Frame Number: " << frame.frame_number << '\n';
        std::cout << "Mesh data (in bytes): " << frame.size_mesh_in_bytes << '\n';
        std::cout << "Keyframe: " << frame.keyframe << '\n';
        std::cout << "Size of vertices (in bytes): " << frame.size_vertices_in_bytes << '\n';
        std::cout << "Size of vertices: " << frame.data_vectices.size() << '\n';
        std::cout << "Size of normals (in bytes): " << frame.size_normals_in_bytes << '\n';
        std::cout << "Size of normals: " << frame.data_normals.size() << '\n';
        std::cout << "Size of indices (in bytes): " << frame.size_indinces_in_bytes << '\n';
        std::cout << "Size of indices: " << frame.data_indinces.size() << '\n';
        std::cout << "Size of UVs (in bytes): " << frame.size_uvs_in_bytes << '\n';
        std::cout << "Size of UVs: " << frame.data_uvs.size() << '\n';
        std::cout << "Frame data size: " << frame.frame_data_size << '\n';
        std::cout << "------------------" << '\n';
      }
    }

    file.close();
    return 0;
  } else {
    std::cerr << "Failed to load " << folder_name_VOLS_ + "sequence_0.vols" << '\n';
    return 1;
  }
}

int Sequence::writeUpdatedHeadertoVOLS() {
  // load current .VOL file
  std::fstream fs( folder_name_VOLS_ + "header.vols", std::ios::in | std::ios::binary );

  if ( fs.is_open() ) {
    std::cout << "Updating the header file..." << '\n';
    // Copy the data in a char* array buffer
    std::string path = dir_output_ + "header.vols";
    std::ofstream of( path, std::ios::out );

    fs.seekg( 0, fs.end );
    long size = fs.tellg();
    fs.seekg( 0 );
    char* buf = new char[size];
    fs.read( buf, size );
    of.write( buf, size );

    // Move pointer to the position where the value will change
    of.seekp( framecountindex_ );

    int diff = last_frame_ - first_frame_ + 1;
    of.write( reinterpret_cast<const char*>( &diff ), sizeof( int ) );
    of.close();
    fs.close();
    delete[] buf;
  }

  std::cout << "Successfully updated " << dir_output_ + "header.vols" << '\n';

  return 0;
}

int Sequence::writeCutSequencetoVOLS() {
  std::cout << "Writing frames between " << first_frame_ << " and " << last_frame_ << " to sequence_0.vols"
            << "..." << '\n';
  std::string path = dir_output_ + "sequence_0.vols";

  FILE* f_ptr = fopen( path.c_str(), "wb" );
  if ( !f_ptr ) {
    fprintf( stderr, "ERROR: opening output file `%s` - check permissions\n", path.c_str() );
    return -1;
  }

  // The first frame should always be set to be a keyframe
  for ( int i = first_frame_, adjusted_frame_number = 0; i <= last_frame_; i++, adjusted_frame_number++ ) {
    void* verts_ptr   = (void*)this->getFrames()[i].data_vectices.data();
    void* normals_ptr = this->getHeaderInfo().hasNormals ? (void*)this->getFrames()[i].data_normals.data() : NULL;
    void* uvs_ptr     = NULL;
    void* indices_ptr = NULL;

    // V12 = Size of Vertices Data + Normals Data + Indices Data + UVs Data + Texture Data + 4 Bytes for each “Size of  Array” in Vertices, Normals, Indices,
    // UVs and Texture (if present)
    int vertices_sz     = this->getFrames()[i].size_vertices_in_bytes;
    int normals_sz      = 0;
    int uvs_sz          = 0;
    int indices_sz      = 0;
    int data_section_sz = vertices_sz + 4;
    if ( this->getHeaderInfo().hasNormals ) {
      normals_sz = this->getFrames()[i].size_normals_in_bytes;
      data_section_sz += normals_sz + 4;
    }
    int keyframe = this->getFrames()[i].keyframe;
    // if first frame isn't a keyframe we need to turn it into one using the previous keyframe's data
    int replacement_frame            = -1;
    bool need_to_replace_first_frame = false;
    if ( i == first_frame_ && keyframe == 0 ) { // NOTE(Anton) valid keyframes are values 1 or 2 (end keyframe)
      need_to_replace_first_frame = true;
      keyframe                    = 1;
      // look for previous keyframe
      for ( int j = first_frame_ - 1; j >= 0; j-- ) {
        if ( this->getFrames()[j].keyframe != 0 ) {
          std::cout << "Frame " << i << " is not a keyframe. Copying indices, normals and UVs from keyframe " << j << ".\n";
          replacement_frame = j;
          break;
        }
      } // endfor j
      if ( replacement_frame == -1 ) {
        fprintf( stderr, "FATAL ERROR: no valid keyframe found before frame %i in sequence\n", i );
        exit( 1 );
      }
    } // endif replacing with keyframe

    // keyframes contain indices and normals.
    if ( keyframe ) {
      // if replacing the first frame with a keyframe, use that as a source instead.
      int index  = need_to_replace_first_frame ? replacement_frame : i;
      uvs_sz     = this->getFrames()[index].size_uvs_in_bytes;
      indices_sz = this->getFrames()[index].size_indinces_in_bytes;
      data_section_sz += uvs_sz + 4 + indices_sz + 4;
      uvs_ptr     = (void*)this->getFrames()[index].data_uvs.data();
      indices_ptr = (void*)this->getFrames()[index].data_indinces.data();
    }

    // NOTE(Anton) moved all File I/O in one place to reduce confusion
    // and added validation of writing
    // and switched to fwrite because the C++ pointer casts were very verbose
    if ( 1 != fwrite( &adjusted_frame_number, 4, 1, f_ptr ) ) { goto bad_file_write; } // Frame Number int
    if ( 1 != fwrite( &data_section_sz, 4, 1, f_ptr ) ) { goto bad_file_write; }       // Mesh Data Size (in bytes) int
    if ( 1 != fwrite( &keyframe, 1, 1, f_ptr ) ) { goto bad_file_write; }              // Keyframe byte
    if ( 1 != fwrite( &vertices_sz, 4, 1, f_ptr ) ) { goto bad_file_write; }           // Vertex array size int
    if ( 1 != fwrite( verts_ptr, vertices_sz, 1, f_ptr ) ) { goto bad_file_write; }    // Vertex array
    if ( this->getHeaderInfo().hasNormals ) {
      if ( 1 != fwrite( &normals_sz, 4, 1, f_ptr ) ) { goto bad_file_write; }          // Normals array size int
      if ( 1 != fwrite( normals_ptr, normals_sz, 1, f_ptr ) ) { goto bad_file_write; } // Normals array
    }
    if ( keyframe ) {
      if ( 1 != fwrite( &indices_sz, 4, 1, f_ptr ) ) { goto bad_file_write; }          // Indices array size int
      if ( 1 != fwrite( indices_ptr, indices_sz, 1, f_ptr ) ) { goto bad_file_write; } // Indices array
      if ( 1 != fwrite( &uvs_sz, 4, 1, f_ptr ) ) { goto bad_file_write; }              // UVs array size int
      if ( 1 != fwrite( uvs_ptr, uvs_sz, 1, f_ptr ) ) { goto bad_file_write; }         // UVs array
    }
    if ( 1 != fwrite( &data_section_sz, 4, 1, f_ptr ) ) { goto bad_file_write; } // Frame Data Size (in bytes)

    continue;

  bad_file_write:
    fprintf( stderr, "ERROR: writing file `%s` - check permissions\n", path.c_str() );
    fclose( f_ptr );
    return -1;
  } // endfor frames in cut sequence

  fclose( f_ptr );

  return 0;
}
