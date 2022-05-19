R"-----(#!/bin/bash

set -e

ScriptDir="$( cd "$( dirname "${{BASH_SOURCE[0]}" )" && pwd )"

MONGO_PORT="${{MONGO_PORT:-{2}}"
export HOME="$PWD/mongo"

MongoCommandName="${{1:-mongo}"
MongoCommand="$ScriptDir/mongo/{1}/bin/$MongoCommandName"

Arguments=("$@")

shift || true

if [ -f "$MongoCommand" ]; then
	Arguments=("$@")
else
	MongoCommand="$ScriptDir/mongo/{1}/bin/mongo"
fi

if [[ "$1" == "--port" ]]; then
	MONGO_PORT=$2
	shift 2
fi

if [[ "$MongoCommand" == "$ScriptDir/mongo/{1}/bin/mongo" ]]; then
	sudo -u {0} \
		$MongoCommand --host "{3}/`hostname`:$MONGO_PORT" \
		--tls --authenticationMechanism MONGODB-X509 --authenticationDatabase "\$external" \
		--tlsCAFile "$ScriptDir/mongo/certificates/MongoCA.crt" --tlsCertificateKeyFile "$ScriptDir/mongo/certificates/admin.pem" \
		--username "O=favro.com,OU=mongo.user,CN=admin" "${{Arguments[@]}"
else
	sudo -u {0} \
		$MongoCommand --host "{3}/`hostname`:$MONGO_PORT" \
		--ssl --authenticationMechanism MONGODB-X509 --authenticationDatabase "\$external" \
		--sslCAFile "$ScriptDir/mongo/certificates/MongoCA.crt" --sslPEMKeyFile "$ScriptDir/mongo/certificates/admin.pem" \
		--username "O=favro.com,OU=mongo.user,CN=admin" "${{Arguments[@]}"
fi

)-----"
