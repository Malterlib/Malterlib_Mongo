#!/usr/bin/env bash

set -e

OutputDir="$1"
IntermediateDir="$2"

if [[ "$OutputDir" == "" ]]; then
	echo "No output dir specified"
	exit 1
fi

if [[ "$IntermediateDir" == "" ]]; then
	IntermediateDir="/CompiledFiles/BuildMongo"
	rm -rf "$IntermediateDir"
fi

SysName=$(uname -s)
ProcessorArch=$(uname -m)

if [[ $SysName ==  Darwin* ]] ; then
	OutputPlatform=OSX
	NumCPUs=`getconf _NPROCESSORS_ONLN`
	BuildPlatform=OSX10.7
	StripCommand="strip -u -r"
elif [[ $SysName ==  Linux* ]] ; then
	OutputPlatform=Linux
	NumCPUs=`getconf _NPROCESSORS_ONLN`
	ExtraLDFlags="-lstdc++"
	ExtraBoringSSLFlags="-fPIC"
	BuildPlatform=Linux2.6
	StripCommand="strip --strip-unneeded"
else
	echo "Couldn't detect system"
	exit 1
fi

if [[ $ProcessorArch == i*86 ]] ; then
	BuildArch=x86
elif [[ $ProcessorArch == x86_64 ]] ; then
	BuildArch=x64
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

OutputBinDir="$OutputDir/$OutputPlatform/mongo/bin/"
mkdir -p "$OutputBinDir"

function BuildBoringSSL()
{
	#rm -rf "$OpenSSLBuildDir"
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

function BuildMongo()
{
	pushd "$MalterlibRoot/External/mongo" > /dev/null

	python -mpip install --user -r buildscripts/requirements.txt

	ToBuild="mongo mongod"
	buildscripts/scons.py $ToBuild -j $NumCPUs --release --disable-warnings-as-errors --ssl --ssl-provider=openssl --ssl-static --ssl-boringssl "--ssl-lib-dir=$OpenSSLBuildDir/bin" "--ssl-include-dir=$MalterlibRoot/External/boringssl/include"

	for Tool in $ToBuild ; do
		cp -f $Tool $OutputBinDir
		$StripCommand "$OutputBinDir/$Tool"
	done

	popd > /dev/null
}

function BuildTools()
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
	export CGO_LDFLAGS="-L$OpenSSLBuildDir/bin -s -w"
	export CGO_CFLAGS="-I$MalterlibRoot/External/boringssl/include"

	pushd "$MalterlibRoot/External/mongo-tools" > /dev/null

	./build.sh "ssl boringssl"

	Tools="mongodump bsondump mongoexport mongoimport mongorestore mongotop"
	for Tool in $Tools ; do
		cp -f bin/$Tool "$OutputBinDir"
		#$StripCommand "$OutputBinDir/$Tool" # -s -w takes care of this
	done

	popd > /dev/null
}

BuildBoringSSL
BuildMongo
BuildTools
