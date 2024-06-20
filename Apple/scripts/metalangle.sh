#!/bin/bash -e

METALANGLE_VERSION=3-0.0.8

. scripts/sdk.sh
mkdir -p deps; cd deps

if [ ! -d MetalANGLE ]; then
	wget https://github.com/kakashidinho/metalangle/releases/download/gles$METALANGLE_VERSION/MetalANGLE.framework.mac.zip
	unzip MetalANGLE.framework.mac.zip
	mkdir -p MetalANGLE/include
	mv MetalANGLE.framework/Headers/* MetalANGLE/include
	rm MetalANGLE.framework.mac.zip
	rm -rf MetalANGLE.framework

	wget https://github.com/kakashidinho/metalangle/releases/download/gles$METALANGLE_VERSION/libMetalANGLE.a.mac.zip
	unzip libMetalANGLE.a.mac.zip
	mv libMetalANGLE_static_mac.a MetalANGLE/
	rm libMetalANGLE.a.mac.zip
fi

echo "MetalANGLE build successful"
