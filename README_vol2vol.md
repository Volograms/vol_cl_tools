# vol2vol - Vologram to Vologram Converter

## Overview

`vol2vol` is a command-line tool that converts Vologram (.vols) files to other Vologram files with modifications. This tool is particularly useful for preprocessing Vologram data, such as removing normals to reduce file size or applying other transformations.

## Features

- **Convert single-file volograms**: Process modern .vols files that contain all data in one file
- **Convert multi-file volograms**: Process older volograms with separate header, sequence, and video files (WIP)
- **Remove normals**: Option to strip normal vectors from the mesh data to reduce file size
- **Resize textures**: High-quality BASIS Universal texture resizing with format preservation
- **Preserve all other data**: Maintains textures, indices, UVs, and other mesh information

## Usage

### Single-file Volograms (Modern Format)
```bash
./vol2vol -i input.vols -o output.vols [OPTIONS]
```

### Multi-file Volograms (Legacy Format)
```bash
./vol2vol -h header.vols -s sequence_0.vols -v texture_1024.webm -o output.vols [OPTIONS]
```

### Options

- `-i, --input FILE`: Input vols file (for single-file volograms)
- `-o, --output FILE`: Output vols file path (required)
- `-h, --header FILE`: Header file (for multi-file volograms)
- `-s, --sequence FILE`: Sequence file (for multi-file volograms)
- `-v, --video FILE`: Video texture file (for multi-file volograms)
- `-n, --no-normals`: Remove normals from the output vologram
- `-t, --texture-size WIDTHxHEIGHT`: Resize texture to specified resolution (e.g., 512x512)
- `-sf, --start-frame N`: Start frame for video cutting (inclusive)
- `-ef, --end-frame N`: End frame for video cutting (inclusive)
- `--help`: Show help message

## Examples

### Remove normals from a single-file vologram
```bash
./vol2vol -i myvologram.vols -o myvologram_no_normals.vols --no-normals
```

### Resize texture to reduce file size
```bash
./vol2vol -i myvologram.vols -o myvologram_512.vols --texture-size 512x512
```

### Cut video to extract frames 10-50
```bash
./vol2vol -i myvologram.vols -o myvologram_cut.vols --start-frame 10 --end-frame 50
```

### Extract first 100 frames
```bash
./vol2vol -i myvologram.vols -o myvologram_first100.vols --end-frame 99
```

### Extract frames starting from frame 50
```bash
./vol2vol -i myvologram.vols -o myvologram_from50.vols --start-frame 50
```

### Combine options: resize texture, remove normals, and cut video
```bash
./vol2vol -i myvologram.vols -o myvologram_optimized.vols --texture-size 512x512 --no-normals --start-frame 10 --end-frame 100
```

<!-- ### Convert multi-file vologram to single-file format
```bash
./vol2vol -h header.vols -s sequence_0.vols -v texture_1024.webm -o converted.vols
```

### Process vologram without modifications (format conversion)
```bash
./vol2vol -i input.vols -o output.vols
``` -->

## Building

### Windows
```bash
.\build_msvc.bat
```

### Linux/macOS
```bash
make vol2vol
```

## Technical Details

The tool supports:
- Vologram format versions 1.0 through 1.3
- Both Unity-style and IFF-style file headers
- Basis Universal texture compression (v1.3+) with texture resizing
- High-quality Lanczos4 resampling for texture scaling
- KTX2 texture container format with Zstandard compression
- Audio data preservation
- Keyframe and intermediate frame processing
- Frame range selection with automatic keyframe conversion

### Frame Range Selection

When using `--start-frame` and `--end-frame` options:
- Frame indices are 0-based (first frame is 0)
- Both start and end frames are inclusive
- If start frame is not specified, it defaults to 0
- If end frame is not specified, it defaults to the last frame
- Frame range is automatically limited to the actual frame count
- Exported frames are renumbered sequentially starting from 0 (e.g., frames 50-100 become frames 0-50)
- The first frame in the selected range is automatically converted to a keyframe if needed
- When converting to keyframe, the tool uses the associated keyframe's indices and UVs while preserving the frame's vertices, normals, and texture data

## File Size Reduction

### Removing Normals
- Normals typically account for 30-40% of geometric data
- Useful when normal information is not needed for your application
- Trade-off: lighting quality may be reduced without normals

### Texture Resizing
- Reduces texture resolution while preserving BASIS format
- 1024x1024 → 512x512 can reduce file size by 50-75%
- Uses high-quality resampling to maintain visual quality
- Supported for BASIS Universal textures only (v1.3+)

## Limitations

- Does not support texture format conversion
- Requires all dependencies (FFmpeg, Basis Universal) for full functionality
