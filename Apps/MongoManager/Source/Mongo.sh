R"-----(#!/bin/bash

set -e

ScriptDir="$( cd "$( dirname "${{BASH_SOURCE[0]}" )" && pwd )"

MONGO_PORT="${{MONGO_PORT:-25017}"
export HOME="$PWD/mongo"

MongoCommand="$ScriptDir/mongo/bin/$1"
shift

if [[ "$1" == "--port" ]]; then
	MONGO_PORT=$2
	shift 2
fi

sudo -u {0} \
	$MongoCommand --host `hostname` --port $MONGO_PORT \
	--ssl --authenticationMechanism MONGODB-X509 --authenticationDatabase "\$external" \
	--sslCAFile "$ScriptDir/mongo/certificates/MongoCA.crt" --sslPEMKeyFile "$ScriptDir/mongo/certificates/admin.pem" \
	--username "O=favro.com,OU=mongo.user,CN=admin" "$@"

)-----"
