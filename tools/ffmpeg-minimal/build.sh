#!/bin/sh
# Reproducible build of the minimal FFmpeg CLI shipped with TeslaCam Desk.
#
# Prerequisites (all local, no admin rights needed):
#   * Git Bash (sh, sed, awk, make via mingw32-make)
#   * MinGW-w64 cross toolchain in /c/Program Files/mingw64/bin
#     (gcc, g++, ar, ranlib, nm, strip, x86_64-w64-mingw32-*)
#   * NASM in $WORK/nasm-2.16.01 (https://www.nasm.us/pub/nasm/releasebuilds/2.16.01/win64/)
#   * x264 source at $WORK/x264-stable (https://code.videolan.org/videolan/x264, stable branch)
#   * FFmpeg 9.0.2 source at $WORK/ffmpeg-9.0.2 (https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz)
#
# WORK defaults to <repo>/.deps/ffmpeg-minimal. Run from anywhere:
#   sh tools/ffmpeg-minimal/build.sh
#
# Output: $WORK/ffmpeg-9.0.2/ffmpeg.exe (about 5.4 MB). Copy it to bin/ffmpeg.exe.

set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
WORK=${FFMPEG_MIN_WORK:-"$REPO/.deps/ffmpeg-minimal"}
MINGW="/c/Program Files/mingw64/bin"

for required in "$WORK/x264-stable" "$WORK/ffmpeg-9.0.2" "$WORK/nasm-2.16.01/nasm.exe"; do
  if [ ! -e "$required" ]; then
    echo "Missing prerequisite: $required" >&2
    echo "See the header of this script for download locations." >&2
    exit 1
  fi
done

# ---- Local tool shims (pkg-config + cross-prefixed binutils aliases) ----
TOOLBIN="$WORK/toolbin"
mkdir -p "$TOOLBIN" "$WORK/tmp"
cat > "$TOOLBIN/pkg-config" <<EOF
#!/bin/sh
ROOT="$WORK/x264-stable"
case "\$1" in --version) echo "0.29.2"; exit 0 ;; esac
for arg in "\$@"; do
  case "\$arg" in --*|-*) ;; *) PKG="\$arg"; break ;; esac
done
[ "\$PKG" = x264 ] || exit 1
for arg in "\$@"; do
  case "\$arg" in
    --exists|--atleast-version*|--exact-version*|--max-version*) exit 0 ;;
    --modversion) echo "0.165.x"; exit 0 ;;
    --variable=includedir|--variable=libdir) echo "\$ROOT"; exit 0 ;;
    --cflags|--cflags-only-I) echo "-I\$ROOT"; exit 0 ;;
    --libs|--libs-only-L|--libs-only-l) echo "-L\$ROOT -lx264"; exit 0 ;;
  esac
done
echo "-I\$ROOT -L\$ROOT -lx264"
exit 0
EOF
chmod +x "$TOOLBIN/pkg-config"
for pair in "nm:gcc-nm" "ar:gcc-ar" "ranlib:gcc-ranlib" "strip:strip"; do
  name=${pair%%:*}; real=${pair##*:}
  printf '#!/bin/sh\nexec "%s/%s.exe" "$@"\n' "$MINGW" "$real" > "$TOOLBIN/x86_64-w64-mingw32-$name"
  chmod +x "$TOOLBIN/x86_64-w64-mingw32-$name"
done

export TMPDIR="$WORK/tmp" TEMP="$WORK/tmp" TMP="$WORK/tmp"
export PATH="$TOOLBIN:$WORK/nasm-2.16.01:$MINGW:$PATH"
PKG="$TOOLBIN/pkg-config"

# ---- Build x264 static library ----
cd "$WORK/x264-stable"
if [ ! -f libx264.a ]; then
  ./configure --host=x86_64-w64-mingw32 --enable-static --disable-shared \
    --disable-cli --enable-pic --extra-cflags="-O3"
  mingw32-make -j4
fi

# ---- Build the minimal FFmpeg CLI ----
cd "$WORK/ffmpeg-9.0.2"
mingw32-make distclean >/dev/null 2>&1 || true
./configure \
  --arch=x86_64 --target-os=mingw32 --cross-prefix=x86_64-w64-mingw32- \
  --pkg-config="$PKG" \
  --enable-gpl --enable-libx264 \
  --disable-everything --disable-doc --disable-debug \
  --disable-network --disable-autodetect --enable-small \
  --enable-ffmpeg --disable-ffplay --disable-ffprobe \
  --enable-static --disable-shared \
  --enable-protocol=file \
  --enable-indev=lavfi \
  --enable-demuxer=mov,concat,image2 \
  --enable-muxer=mp4,image2,null \
  --enable-decoder=h264,mjpeg,wrapped_avframe \
  --enable-encoder=libx264,mjpeg \
  --enable-parser=h264,mjpeg \
  --enable-bsf=h264_mp4toannexb,extract_extradata \
  --enable-filter=scale,pad,setsar,fps,null,crop,testsrc2,format \
  --enable-swscale --enable-swresample \
  --extra-cflags="-I$WORK/x264-stable -O3" \
  --extra-ldflags="-L$WORK/x264-stable -static" \
  --extra-libs="-lx264"
mingw32-make -j4
ls -la ffmpeg.exe
echo "BUILD_OK: $WORK/ffmpeg-9.0.2/ffmpeg.exe"
