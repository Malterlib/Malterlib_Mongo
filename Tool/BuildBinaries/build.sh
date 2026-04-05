#!/usr/bin/env bash
# Copyright © Unbroken AB
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

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

HomebrewPrefix=`brew --prefix || echo ""`

if [[ "$HomebrewPrefix" != "" ]]; then
	rm -f "$HomebrewPrefix/include/openssl"
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

if [[ "$MalterlibPlatform" == "macOS" ]] ; then
	NumCPUs=`getconf _NPROCESSORS_ONLN`
	ExtraLDFlags="-lc++"
	StripCommand="strip -u -r"
	CurlBuildCFlags="-mmacosx-version-min=10.14"
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
elif [[ $ProcessorArch == arm64 ]] ; then
	BuildArch=arm64
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
CurlBuildDir="$IntermediateDir/curl"
PythonEnvDir="$IntermediateDir/penv"

OutputBinDir=""

function SetOutputBinDir()
{
	OutputBinDir="$OutputDir/$MalterlibPlatform/$MalterlibArch/mongo/$("$MalterlibRoot/External/mongo/build/install/bin/mongod" --version | head -1 | cut -dv -f3 | cut -d. -f1,2)/bin/"
}

function BuildBoringSSL()
{
	mkdir -p "$OpenSSLBuildDir"
	pushd "$OpenSSLBuildDir" > /dev/null

	export MACOSX_DEPLOYMENT_TARGET=10.14
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
Libs: -L\${libdir} -ldl $ExtraLDFlags
Cflags: -I\${includedir}
EOF

	export PKG_CONFIG_PATH="$TempPkgConfigDir:$PKG_CONFIG_PATH"
}

function BuildCurl()
{
	SetupOpensslPkgConfig

	mkdir -p "$CurlBuildDir"
	pushd "$CurlBuildDir" > /dev/null

	export MACOSX_DEPLOYMENT_TARGET=10.14
	export PKG_CONFIG_PATH="$TempPkgConfigDir:$PKG_CONFIG_PATH"

	cmake -GNinja "$MalterlibRoot/External/curl" \
		-DUSE_NGHTTP2=OFF \
		-DUSE_LIBIDN2=OFF \
		-DCURL_ZSTD=OFF \
		-DCURL_DEFAULT_SSL_BACKEND=openssl \
		-DCURL_USE_LIBSSH2=OFF \
		-DCURL_USE_LIBPSL=OFF \
		-DCURL_USE_LIBSSH=OFF \
		-DCURL_USE_OPENSSL=ON \
		-DBUILD_LIBCURL_DOCS=OFF \
		-DBUILD_MISC_DOCS=OFF \
		-DENABLE_CURL_MANUAL=OFF \
		-DBUILD_STATIC_LIBS=ON \
		-DBUILD_STATIC_CURL=ON \
		-DBUILD_SHARED_LIBS=OFF \
		-DBUILD_CURL_EXE=OFF \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_CXX_FLAGS="$ExtraBoringSSLFlags" \
		-DCMAKE_C_FLAGS="$ExtraBoringSSLFlags" \
		"-DCMAKE_INSTALL_PREFIX=$IntermediateDir/curl_bin" \
		"-DOPENSSL_ROOT_DIR=$MalterlibRoot/External/boringssl" \
		"-DOPENSSL_LIBRARIES=$OpenSSLBuildDir/bin/libssl.a;$OpenSSLBuildDir/bin/libcrypto.a;$OpenSSLBuildDir/bin/libdecrepit.a" \
		"-DOPENSSL_SSL_LIBRARY=$OpenSSLBuildDir/bin/libssl.a" \
		"-DOPENSSL_DECREPIT_LIBRARY=$OpenSSLBuildDir/bin/libdecrepit.a" \
		"-DOPENSSL_CRYPTO_LIBRARY=$OpenSSLBuildDir/bin/libcrypto.a"
	ninja
	ninja install

	popd > /dev/null
}

function BuildMongo()
{
	pushd "$MalterlibRoot/External/mongo" > /dev/null

	if $MongoBuildClean; then
		rm -rf build
	fi

	PythonExe=`which python3.11 || echo ""`
	if [[ "$PythonExe" == "" ]]; then
		PythonExe="python3"
	fi

	echo PythonExe="$PythonExe"

	mkdir -p "$PythonEnvDir"

	"$PythonExe" -m venv "$PythonEnvDir"
	source "$PythonEnvDir/bin/activate"

	"$PythonExe" -m pip install -r etc/pip/compile-requirements.txt

	ToBuild="install-mongod install-mongo"

	CurlLibs="`pkg-config \"$IntermediateDir/curl_bin/lib/pkgconfig/libcurl.pc\" --libs-only-l --static | sed 's/-l//g'`"

	CurlFrameworks=""
	if [[ "$MalterlibPlatform" == "macOS" ]]; then
		CurlFrameworks="$CurlFrameworks SystemConfiguration"
	fi

	"$PythonExe" buildscripts/scons.py LIBPATH="$IntermediateDir/curl_bin/lib $OpenSSLBuildDir/bin" \
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

	for Tool in "$PWD/build/install/bin/"* ; do
		echo "Tool: $Tool"

		if [[ "$Tool" == "$PWD/build/install/bin/resmoke.py" ]]; then
			continue
		fi

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
