R"-----(#!/bin/bash

set -e

ScriptDir="$( cd "$( dirname "${{BASH_SOURCE[0]}" )" && pwd )"

MONGO_PORT="${{MONGO_PORT:-{2}}"
export HOME="$PWD/mongo"

MongoCommandName="${{1:-mongo}"
MongoCommand="$ScriptDir/mongo/{1}/bin/$MongoCommandName"

shift || true

if [[ "$1" == "--port" ]]; then
	MONGO_PORT=$2
	shift 2
fi

sudo -u {0} \
	$MongoCommand --host `hostname` --port $MONGO_PORT \
	--tls --authenticationMechanism MONGODB-X509 --authenticationDatabase "\$external" \
	--tlsCAFile "$ScriptDir/mongo/certificates/MongoCA.crt" --tlsCertificateKeyFile "$ScriptDir/mongo/certificates/admin.pem" \
	--username "O=favro.com,OU=mongo.user,CN=admin" "$@"

)-----"
