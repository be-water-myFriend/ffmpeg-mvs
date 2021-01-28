ffmpeg-mvs README
=============

基于ffmpeg-4.3 release二次开发，增加对VC-1、Hevc、vp6、vp8、vp9编码格式视频的运动矢量提取功能；

## windows msvc 编译
  ./configure --toolchain=msvc --enable-shared --enable-version3 --enable-ffplay --enable-sdl2 --enable-gpl --prefix=host --extra-cflags="-I/d/env/SDL2-2.0.14/include/" --extra-   ldflags="-L/d/env/SDL2-2.0.14/lib/x64"
  make -j 8
  make install

## 测试
  ./ffplay.exe -i test_video/test_h264.mp4
