# vol_cl_tools
Command-line tools for converting [Volograms](https://www.volograms.com/)' 3D format.

Tools can be built and run for GNU/Linux, MacOS, and Microsoft Windows environments.

## Contents ##

| Tool    | Version | Description                                                                                   |
| ------- | ------- | --------------------------------------------------------------------------------------------- |
| vol2obj | 0.4.1   | Convert a frame from a vologram sequence to a Wavefront .obj file + .mtl material + jpg file. |

Further tools to be added: obj2vol, sequence cutting and manipulation. 

```
lib/                 -- Core vologram processing libraries, vol_av (video textures) and vol_geom (vologram meshes).
samples/             -- Simple example volograms;
samples/cone_hdr.vol -- Vologram header for a 1-frame 3D cone.
samples/cone_seq.vol -- Vologram sequence for the 1-frame 3D cone.
samples/counter.mp4  -- Video texture of numbered frames, useful for debugging.
samples/counter.webm -- A WebM VP9 encoding of the same video texture, for comparison.
samples/cube_hdr.vol -- Vologram header for a 1-frame 3D cube.
samples/cube_seq.vol -- Vologram sequence for the 1-frame 3D cube.
samples/quad_hdr.vol -- Vologram header for a 1-frame 3D rectangle.
samples/quad_seq.vol -- Vologram sequence for the 1-frame 3D rectangle.
third_party/         -- Third-party libraries used by tools.
tools/vol2obj/       -- The vol2obj tool.
LICENSE              -- Licence details for this project.
Makefile             -- GNU Makefile to build tools with Clang or GCC.
README.md            -- This file.
```

## Compiling the Tools

* Make sure that Git and [Git LFS](https://git-lfs.github.com/) are installed on your system.
* Clone this repository.
* Install FFmpeg development libraries:
    * For Windows these can be found under the `thirdparty/ffmpeg_lgpl_free/` sub-directory, and you don't need to do anything.
    * On Ubuntu `sudo apt-get install build-essential clang libavcodec-dev libavdevice-dev libavformat-dev libavutil-dev libswscale-dev`
    * On MacOS `brew install ffmpeg`.

* To build all tools programs with Clang.

```
make
```

### Troubleshooting

Homebrew on M1 Macs may require environment variables to be set: 

```
export CPATH=/opt/homebrew/include
export LIBRARY_PATH=/opt/homebrew/lib
```

There is a more detailed answer at https://apple.stackexchange.com/questions/414622/installing-a-c-c-library-with-homebrew-on-m1-macs

## Running an Example with vol2obj

Included in the `samples/` directory are several shapes, encoded as 1-frame volograms for testing, and video textures in MP4 and WebM using numbered frames, which can be useful for debugging sequences.

As a demonstration, we can convert one of these small samples to a Wavefront OBJ,
and open it in 3D modelling software.

If we run the vol2obj program from a terminal, without arguments, it will tell us the inputs required:

```
$  ./vol2obj.bin 
Usage ./vol2obj.bin [OPTIONS] -h HEADER.VOLS -s SEQUENCE.VOLS -v VIDEO.MP4
Options:
  --all             Process all frames in vologram.
                    If given then paramters -f and -l are ignored.
  -f N              Process the frame number given by N (frames start at 0). Default value 0.
                    If the -l parameter is not given then only this single frame is processed.
  -l N              Process up to specific frame number given by N.
                    Can be used in conjunction with -f to process a range of frames from -f to -l (first to last), inclusive.
  --output_dir      Specify a directory to write output files to. The default is the current working directory.
  --help            This text.
```

Volograms are currently split into 3 files: { header, sequence, video texture }.
We give vol2obj a matching header and sequence from the samples folder, and use the "counter" test video as a video texture.
We can specify a single frame to output with e.g. `-f 0`, for the first frame, or use `--all` to write out all of the frames as separate OBJ files.

```
./vol2obj.bin -h samples/cube_hdr.vol -s samples/cube_seq.vol -v samples/counter.webm -f 0
```

We should get as output the following files:

```
output_frame_00000000.jpg  -- JPEG texture.
output_frame_00000000.mtl  -- Wavefront MTL material file.
output_frame_00000000.obj  -- Wavefront OBJ mesh file. 
```

We can now import the .obj into Blender (or any other 3D software), and confirm that the cube shape has loaded and the first frame from the video has been applied as a texture:

![Blender shows our cube sample, and also found the MTL and JPG texture, showing the "0000" text from the first video frame..](cubeblender.png)

### Caveats with vol2obj

* If header and sequence files mismatch, expect an error.

## Security and Fuzzing

* The core code in this repository has been fuzzed using [AFL](https://github.com/google/AFL).
* Third party image writing code has not been fuzzed yet, and should not be considered secure at this stage, for use in internal asset pipelines, but of course is fine for test or hobby use.

## Licence ##

Copyright 2021, Volograms.
The MIT License. See the `LICENSE` file for details.

### Dependencies

* This software uses unaltered code of <a href=http://ffmpeg.org>FFmpeg</a> licensed under the <a href=http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>LGPLv2.1</a> and its source code can be found at [github.com/FFmpeg/FFmpeg](https://github.com/FFmpeg/FFmpeg).

See the `thirdparty/ffmpeg/LICENSE.md` file for details.

* The LGPL build of Windows FFmpeg included in this repository uses the binary of the H264 codec [openh264](https://github.com/cisco/openh264) from Cisco, which has the BSD-2-Clause Licence.

> Copyright (c) 2013, Cisco Systems
> All rights reserved.
> 
> Redistribution and use in source and binary forms, with or without modification,
> are permitted provided that the following conditions are met:
> 
> * Redistributions of source code must retain the above copyright notice, this
>   list of conditions and the following disclaimer.
> 
> * Redistributions in binary form must reproduce the above copyright notice, this
>   list of conditions and the following disclaimer in the documentation and/or
>   other materials provided with the distribution.
> 
> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
> ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
> WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
> DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
> ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
> (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
> LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
> ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
> (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
> SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

* This software uses stb_image_write by Sean Barrett to output texture images. This software is used here under the terms of the MIT License "ALTERNATIVE A":

> ALTERNATIVE A - MIT License
> Copyright (c) 2017 Sean Barrett
> Permission is hereby granted, free of charge, to any person obtaining a copy of
> this software and associated documentation files (the "Software"), to deal in
> the Software without restriction, including without limitation the rights to
> use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
> of the Software, and to permit persons to whom the Software is furnished to do
> so, subject to the following conditions:
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.
