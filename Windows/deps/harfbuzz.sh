#!/bin/bash -e

HARFBUZZ_VERSION=12.3.2

. ./sdk.sh

mkdir -p output/harfbuzz/lib
mkdir -p deps; cd deps

if [ ! -d harfbuzz-src ]; then
	git clone -b $HARFBUZZ_VERSION --depth 1 https://github.com/harfbuzz/harfbuzz.git harfbuzz-src
	mkdir harfbuzz-src/build
fi

cd harfbuzz-src/build

cmake .. \
	-DBUILD_SHARED_LIBS=FALSE \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_C_FLAGS_RELEASE="$CFLAGS" \
	-DFREETYPE_LIBRARY="../../freetype/lib/libfreetype.a ../../libpng/lib/libpng.a" \
	-DFREETYPE_INCLUDE_DIRS="../../freetype/include" \
	-DHB_HAVE_GLIB=OFF \
	-DHB_HAVE_GOBJECT=OFF \
	-DHB_HAVE_ICU=OFF \
	-DHB_HAVE_FREETYPE=ON \
	-DHB_BUILD_SUBSET=OFF

cmake --build . -j

# update headers
rm -rf ../../harfbuzz/include/
mkdir -p ../../harfbuzz/include/harfbuzz
cp ../src/*.h ../../harfbuzz/include/harfbuzz
# update lib
rm -rf ../../harfbuzz/lib/libharfbuzz.a
cp libharfbuzz.a ../../harfbuzz/lib/libharfbuzz.a

echo "Freetype build successful"
