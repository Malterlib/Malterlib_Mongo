#!/usr/bin/env bash

# To build only a stage
# BuildOnly=BuildMongo ./build.sh

# To build up to and including a stage
# BuildIncremental=BuildMongo ./build.sh

set -ex

source ../../../Core/Scripts/Detect.sh

OutputDir="$1"
IntermediateDir="$2"
TempPkgConfigDir=

if [[ "$MongoBuildClean" == "" ]]; then
	MongoBuildClean=false
fi

if [[ "$OutputDir" == "" ]]; then
	echo "No output dir specified"
	exit 1
fi

if [[ "$IntermediateDir" == "" ]]; then
	IntermediateDir="/opt/CompiledFiles/BuildMongo"
	if [[ "$BuildIncremental" == "" ]] && [[ "$BuildOnly" == "" ]]; then
		rm -rf "$IntermediateDir"
	fi
fi

SysName=$(uname -s)
ProcessorArch=$(uname -m)

if [[ "$MalterlibPlatform" == "OSX" ]] ; then
	NumCPUs=`getconf _NPROCESSORS_ONLN`
	StripCommand="strip -u -r"
	CurlBuildCFlags="-mmacosx-version-min=10.11"
elif [[ "$MalterlibPlatform" == "Linux" ]] ; then
	NumCPUs=`getconf _NPROCESSORS_ONLN`
	ExtraLDFlags="-lstdc++ -lpthread"
	ExtraBoringSSLFlags="-fPIC"
	StripCommand="strip --strip-unneeded"
else
	echo "Couldn't detect system"
	exit 1
fi

if [[ $ProcessorArch == i*86 ]] ; then
	BuildArch=x86
elif [[ $ProcessorArch == x86_64 ]] ; then
	BuildArch=x64
elif [[ $ProcessorArch == aarch64 ]] ; then
	BuildArch=arm64
	SconsCFlags="CCFLAGS=-march=armv8-a+crc"
else
	echo $ProcessorArch is not a recognized architecture
	exit 1
fi

function AbsolutePath()
{
	pushd "$(dirname "$1")" > /dev/null
	printf "%s/%s\n" "$(pwd)" "$(basename "$1")"
	popd > /dev/null
}

MalterlibRoot=`AbsolutePath "../../../.."`
OpenSSLBuildDir="$IntermediateDir/boringssl"

OutputBinDir=""

function SetOutputBinDir()
{
	OutputBinDir="$OutputDir/$MalterlibPlatform/$MalterlibArch/mongo/$("$MalterlibRoot/External/mongo/build/install/bin/mongod" --version | head -1 | cut -dv -f3 | cut -d. -f1,2)/bin/"
}

function BuildBoringSSL()
{
	mkdir -p "$OpenSSLBuildDir"
	pushd "$OpenSSLBuildDir" > /dev/null

	export MACOSX_DEPLOYMENT_TARGET=10.11
	cmake -GNinja "$MalterlibRoot/External/boringssl" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="$ExtraBoringSSLFlags" -DCMAKE_C_FLAGS="$ExtraBoringSSLFlags"
	ninja
	#ninja -C "$OpenSSLBuildDir" run_tests

	popd > /dev/null

	mkdir -p "$OpenSSLBuildDir/bin"
	cp "$OpenSSLBuildDir/crypto/libcrypto.a" "$OpenSSLBuildDir/bin"
	cp "$OpenSSLBuildDir/ssl/libssl.a" "$OpenSSLBuildDir/bin"
	cp "$OpenSSLBuildDir/decrepit/libdecrepit.a" "$OpenSSLBuildDir/bin"
}

function SetupOpensslPkgConfig()
{
	TempPkgConfigDir="$IntermediateDir/OpenSSLpkg"
	mkdir -p "$TempPkgConfigDir"
	OpenSSLPkgConfig="$TempPkgConfigDir/openssl.pc"

	cat << EOF > "$OpenSSLPkgConfig"
prefix=/usr
exec_prefix=\${prefix}
libdir=$OpenSSLBuildDir/bin
includedir=$MalterlibRoot/External/boringssl/include

Name: OpenSSL
Version: 0.0
Description: Secure Sockets Layer and cryptography libraries and tools
Requires:
Libs: -L\${libdir} -ldecrepit -lssl -lcrypto -ldl $ExtraLDFlags
Cflags: -I\${includedir}
EOF

	export PKG_CONFIG_PATH="$TempPkgConfigDir:$PKG_CONFIG_PATH"
}

function BuildCurl()
{
	SetupOpensslPkgConfig

	export MACOSX_DEPLOYMENT_TARGET=10.11
	export PKG_CONFIG_PATH="$TempPkgConfigDir:$PKG_CONFIG_PATH"

	pushd "$MalterlibRoot/External/curl" > /dev/null
	autoreconf -fi
	CFLAGS="$CurlBuildCFlags" ./configure --disable-shared --with-ssl --without-brotli --without-nghttp2 --without-libidn2 --without-zstd --prefix "$IntermediateDir/curl_bin"
	make clean
	make -j$NumCPUs
	make install

	popd > /dev/null
}

function BuildMongo()
{
	pushd "$MalterlibRoot/External/mongo" > /dev/null

	if $MongoBuildClean; then
		rm -rf build
	fi

	python3 -m pip install -r etc/pip/compile-requirements.txt

	ToBuild="install-mongod install-mongo"

	CurlLibs="`pkg-config \"$IntermediateDir/curl_bin/lib/pkgconfig/libcurl.pc\" --libs-only-l --static | sed 's/-l//g'`"

	CurlFrameworks=""
	if [[ "$MalterlibPlatform" == "OSX" ]]; then
		CurlFrameworks="$CurlFrameworks SystemConfiguration"
	fi

	python3 buildscripts/scons.py LIBPATH="$IntermediateDir/curl_bin/lib $OpenSSLBuildDir/bin" \
		CPPPATH="$IntermediateDir/curl_bin/include" \
		FRAMEWORKS="$CurlFrameworks" \
		LIBS="$CurlLibs" \
		$SconsCFlags \
		$ToBuild -j $NumCPUs --release --disable-warnings-as-errors \
		--ssl --ssl-static --ssl-boringssl --ocsp-stapling=off \
		"--ssl-lib-dir=$OpenSSLBuildDir/bin" \
		"--ssl-include-dir=$MalterlibRoot/External/boringssl/include"

	SetOutputBinDir
	mkdir -p "$OutputBinDir"

	cp -f "$PWD/build/install/bin/"* "$OutputBinDir"

	for Tool in "$PWD/build/install/bin/"* ; do
		cp -f "$Tool" "$OutputBinDir"
		$StripCommand "${OutputBinDir}$(basename "$Tool")"
	done

	popd > /dev/null
}

function BuildTools()
{
	SetupOpensslPkgConfig

	export PKG_CONFIG_PATH="$TempPkgConfigDir:$PKG_CONFIG_PATH"
	export CGO_LDFLAGS="-L$OpenSSLBuildDir/bin -s -w"
	export CGO_CFLAGS="-I$MalterlibRoot/External/boringssl/include"

	pushd "$MalterlibRoot/External/mongo-tools" > /dev/null

	if $MongoBuildClean; then
		rm -rf bin vendor/pkg node_modules
	fi

	./make build
	# "ssl boringssl"

	for Tool in bin/* ; do
		cp -f "$Tool" "$OutputBinDir$(basename "$Tool")"
		#$StripCommand "$OutputBinDir/$Tool" # -s -w takes care of this
	done

	popd > /dev/null
}

function BuildStage()
{
	if [[ "$BuildOnly" != "" ]]; then
		if [[ "$BuildOnly" == "$1" ]]; then
			$1
			exit 0
		fi

		return
	fi

	$1

	if [[ "$BuildIncremental" == "$1" ]]; then
		exit 0
	fi
}

BuildStage BuildBoringSSL
BuildStage BuildCurl
BuildStage BuildMongo
SetOutputBinDir # For when BuildMongo is not run
BuildStage BuildTools
