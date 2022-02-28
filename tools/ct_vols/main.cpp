/** @file main.h
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
 *
 *  History
 * -----------
 * - 0.3     (2022/02/28) - First public release on Volograms GitHub. Copyright and Licence header updated by Anton Gerdelan.
 * - 0.2                  - Small bug-fixes and usability tweaks by Anton Gerdelan.
 * - 0.1                  - Internal-use tool created by Konstantinos Amplianitis.
 *
 * Build Instructions
 * ---------------------
 * - Install dependencies:
 * 
 *   apt-get update && apt-get install -y --no-install-recommends cmake ffmpeg libboost-all-dev
 * 
 * - Install CMake https://cmake.org/
 * - Use the graphical interface to generate the desired build file.
 * - Or use the command-line. e.g.:
 *
 *   mkdir build/ && cd build/
 *   cmake ..
 *   make
 *
 * Usage Instructions
 * ---------------------
 * Run the `ct-vols` tool from a command line to get a list of options.
 * The tool takes, as input, a directory containing a vologram's files: header.vols, sequence.vols and a video texture.
 * Supply a range of frames to cut out of the sequence.
 * And a new vologram will be created in a given output directory, containing only those frames specified.
 * For example:
 *
 *   ./ct-vols -i my_vologram/ -o output/ -f 10 -l 20
 *
 * Will cut the frames 10-20 from the vologram contained in directory "my_vologram/" and create a directory called "output/"
 * where it will write the new vologram's files.
 *
 * The first frame in a sequnce is 0, not 1. The range is inclusive of first and last frames.
 * So to get the first 10 frames: `-f 0 -l 9`.
 */

#include <boost/program_options.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/filesystem.hpp>
#include "vols_cutter.hpp"

std::vector<boost::filesystem::path> get_all_files( boost::filesystem::path const& root, std::string const& ext ) {
  std::vector<boost::filesystem::path> paths;

  if ( boost::filesystem::exists( root ) && boost::filesystem::is_directory( root ) ) {
    for ( auto const& entry : boost::filesystem::directory_iterator( root ) ) {
      if ( boost::filesystem::is_regular_file( entry ) && entry.path().extension() == ext ) paths.emplace_back( entry.path().filename() );
    }
  }

  return paths;
}

int checkFrameRange( int first, int last ) {
  if ( first == last ) {
    std::cerr << "First and last frame cannot be the same." << '\n';
    return 1;
  }

  if ( first < 0 || last < 0 ) {
    std::cerr << "Frame index cannot be negative." << '\n';
    return 1;
  }

  if ( first > last ) {
    std::cerr << "First frame cannot be greater than the last frame." << '\n';
    return 1;
  }

  return 0;
}

int main( int argc, char* argv[] ) {
  std::string folder_name;
  int first_frame;
  int last_frame;
  bool print_values;
  std::string output_directory;

  try {
    namespace po = boost::program_options;
    po::options_description desc( "Options" );
    desc.add_options()( "input,i", po::value<std::string>( &folder_name ), "Folder Name" )(
      "first-frame,f", po::value<int>( &first_frame ), "First Frame")("last-frame,l", po::value<int>( &last_frame ), "Last Frame")("print_values,p",
      po::value<bool>( &print_values ), "Print Intermediate Results")( "output_directory,o", po::value<std::string>( &output_directory ), "Output Directory" );

    po::variables_map vm;
    po::store( po::parse_command_line( argc, argv, desc ), vm );

    if ( vm.count( "help" ) || argc == 1 ) {
      BOOST_LOG_TRIVIAL( error ) << desc;
      return 1;
    }

    po::notify( vm );
  } catch ( std::exception& e ) {
    BOOST_LOG_TRIVIAL( error ) << "error: " << e.what();
    return 1;
  }

  BOOST_LOG_TRIVIAL( debug ) << "main(): Parameters provided to the executable: ";
  BOOST_LOG_TRIVIAL( debug ) << "[input | -i]"
                             << " " << folder_name;
  BOOST_LOG_TRIVIAL( debug ) << "[first-frame | -f]"
                             << " " << first_frame;
  BOOST_LOG_TRIVIAL( debug ) << "[last-frame | -l]"
                             << " " << last_frame;
  BOOST_LOG_TRIVIAL( debug ) << "[print_values | -p]"
                             << " " << print_values;
  BOOST_LOG_TRIVIAL( debug ) << "[output_directory | -o]"
                             << " " << output_directory;

  // Check frame values.
  if ( checkFrameRange( first_frame, last_frame ) == 1 ) { return 1; }

  // Create output director if doesn't exist.
  const char* path = output_directory.c_str();
  boost::filesystem::path dir( path );
  if ( boost::filesystem::create_directory( dir ) ) { std::cerr << "Directory Created: " << output_directory << std::endl; }

  Sequence seq;

  // Set parameters.
  seq.setFolderVOLS( folder_name );
  seq.setFirstFrame( first_frame );
  seq.setLastFrame( last_frame );
  seq.print_values( print_values );
  seq.setOutputDir( output_directory );

  // Read header file VOLS.
  if ( seq.readHeaderFileVOLS() == 1 ) {
    std::cerr << "Failed to read header file." << '\n';
    return 1;
  }

  // Read sequence file VOLS.
  if ( seq.readSequenceFileVOLS() == 1 ) {
    std::cerr << "Failed to read sequence file." << '\n';
    return 1;
  };

  // Update header file and save it as a new VOLS file.
  if ( seq.writeUpdatedHeadertoVOLS() == 1 ) {
    std::cerr << "Failed to write updated header file." << '\n';
    return 1;
  }

  // Write cut VOLS sequence file.
  if ( seq.writeCutSequencetoVOLS() == 1 ) {
    std::cerr << "Failed to write cutted sequence file." << '\n';
    return 1;
  }

  boost::filesystem::path input_folder( folder_name );

  // NOTE(Anton) Modified to be non-recursive because this was also picking up the OUTPUT files if they are in the same tree.
  std::vector<boost::filesystem::path> texture_files = get_all_files( input_folder, ".mp4" );

  printf( "input folder is `%s`\n", input_folder.c_str() );
  for ( int i = 0; i < texture_files.size(); i++ ) { printf( " texture file %i) input file name is `%s`\n", i, texture_files[i].string().c_str() ); }

  for ( int i = 0; i < texture_files.size(); ++i ) {
    std::cout << "Reading video texture file " << i << " : " << texture_files[i].string() << std::endl;
    size_t lastindex              = texture_files[i].string().find_last_of( "." );
    std::string rawname           = texture_files[i].string().substr( 0, lastindex );
    std::string output_video_file = output_directory + rawname + "_" + std::to_string( first_frame ) + "_" + std::to_string( last_frame ) + ".mp4";

    // NOTE(Anton) The first video frame here is 0, not 1. And the range is inclusive of first and last frames. So to get the first 10 frames: `-f 0 -l 9`.

    // NOTE(Anton) Here is a quick and dirty fix to the FFmpeg frame range command:
    // the + + + string bit was getting fiddly so here's an sprintf() instead.
    char ffmpeg_cmd_str[4096];
    // NOTE(Anton) for encoding e.g. debug videos for testing, remove "-profile:v baseline".
    // 'baseline' targets apps with low computing power eg mobiles - http://blog.mediacoderhq.com/h264-profiles-and-levels/
    sprintf( ffmpeg_cmd_str, "ffmpeg -y -i %s%s -profile:v baseline -vf select=\"between(n\\,%i\\,%i),setpts=PTS-STARTPTS\" %s", folder_name.c_str(),
      texture_files[i].string().c_str(), first_frame, last_frame, output_video_file.c_str() );
    printf( "ffmpeg_cmd_str = `%s`\n", ffmpeg_cmd_str );

    // std::string ffmpeg_cmd = "ffmpeg -y -i " + folder_name + texture_files[i].string() + " " + "-profile:v baseline -vf
    // select=\"between(n\\,5\\,100),setpts=PTS-STARTPTS\"" + " " + output_video_file; std::cout << ffmpeg_cmd << std::endl;

    std::system( ffmpeg_cmd_str );
    std::cout << "Saving video file to: " << output_video_file << std::endl;
  }

  return 0;
}
