#!/bin/bash

set -eu

ROOT_DIR=$(cd $(dirname "$0")/..; pwd)

. $ROOT_DIR/env.sh

pushd $ROOT_DIR/libmpv/ffmpeg

if [ "$1" == "build" ]; then
	echo -e "\nBuilding FFmpeg..."
elif [ "$1" == "clean" ]; then
	rm -rf .build
	exit 0
else
	exit 1
fi

mkdir -p .build
cd .build

# =============================================================================
# FFmpeg 裁剪说明 (B站场景白名单):
# - 视频: h264/hevc OHOS 硬解 (h264_oh/hevc_oh, 走 ohcodec) + 软解回退
#         av1 仅软解 (libdav1d), OHOS 平台无 av1 硬解实现
#         vp9/vp8/vpx 已移除 (B站不使用 vp9)
# - 音频: aac(含LATM)/mp3/opus/ac3/eac3 + pcm 基础 (B站直播+点播主流音频)
#         已移除: av3a_oh (Audio Vivid, 不需要), dca/flac/alac/vorbis (B站不用)
# - 直播: http-flv (flv demuxer), http-hls->ts (hls+mpegts demuxer),
#         dash-fmp4 (dash+mov demuxer, 需 libxml2)
# - 字幕: ass/ssa/srt/mov_text/webvtt
# - 协议: file/http/https/tls/tcp (mbedtls 提供 tls; crypto 协议不需要)
# - 截图: png/mjpeg
# - 动图: libwebp / libwebp_anim 编码器 + webp muxer (PiliPlus 动态截图)
# - 滤镜: 仅保留 lavfi 桥接基础 (PiliPlus OHOS 不使用 lavfi-complex);
#         trim/atrim 为 mpv encode 模式处理 --start/--end 所需
# =============================================================================
../configure \
  --prefix=$DEST \
  --arch=aarch64 \
  --cpu=armv8-a \
  --target-os=linux \
  --enable-static \
  --disable-shared \
  --enable-version3 \
  --enable-pic \
  --disable-doc \
  --disable-programs \
  \
  --enable-cross-compile \
  --cc="$CC" \
  --extra-cflags="-I$DEST/include $LTO_CFLAGS $PGO_CFLAGS" \
  --extra-ldflags="-L$DEST/lib $LTO_CFLAGS $PGO_LDFLAGS" \
  --pkg-config-flags=--static \
  \
  --disable-everything \
  --enable-avcodec \
  --enable-avformat \
  --enable-avfilter \
  --enable-swresample \
  --enable-swscale \
  --disable-avdevice \
  --disable-devices \
  \
  --enable-ohcodec \
  --enable-libdav1d \
  --enable-libwebp \
  --enable-mbedtls \
  --enable-libxml2 \
  --disable-vulkan \
  \
  --enable-decoder=h264,h264_oh,hevc,hevc_oh,libdav1d,mjpeg,png \
  --enable-decoder=aac,aac_latm,mp3,opus,ac3,eac3 \
  --enable-decoder=pcm_s16le,pcm_s16be,pcm_s32le,pcm_f32le,pcm_u8 \
  --enable-decoder=ass,ssa,subrip,mov_text,webvtt,text \
  \
  --enable-demuxer=mov,flv,mpegts,mp3,aac,hls,dash \
  --enable-demuxer=ass,ssa,srt,webvtt \
  \
  --enable-parser=h264,hevc,av1,aac,mpegaudio,ac3,opus,mjpeg,vp8 \
  \
  --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc,extract_extradata \
  \
  --enable-protocol=file,http,https,tls,tcp \
  \
  --enable-encoder=png,mjpeg,libwebp,libwebp_anim \
  --enable-muxer=png,mjpeg,webp \
  \
  --enable-filter=aresample,aformat,format,scale,anull,atrim,asetpts,setpts,volume,trim \
  --enable-filter=abuffer,abuffersink,buffersink,buffer
make -j$CORES
make install

popd