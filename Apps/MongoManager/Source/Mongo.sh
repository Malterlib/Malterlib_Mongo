#!/bin/bash
# Copyright © 2024 Favro Holding AB
# Distributed under the MIT license, see license text in LICENSE.Malterlib

set -e

ScriptDir="$( cd "$( dirname "${{BASH_SOURCE[0]}" )" && pwd )"

MONGO_PORT="${{MONGO_PORT:-{2}}"
export HOME="$PWD/mongo"

MongoCommandName="${{1:-mongosh}"
MongoCommand="$ScriptDir/mongo/{1}/bin/$MongoCommandName"

Arguments=("$@")

shift || true

if [ -f "$MongoCommand" ]; then
	Arguments=("$@")
else
	MongoCommand="$ScriptDir/mongo/{1}/bin/mongosh"
fi

if [[ "$1" == "--port" ]]; then
	MONGO_PORT=$2
	shift 2
fi

if [[ '{3}' == '' ]]; then
	MongoHostName="`hostname`:$MONGO_PORT"
else
	MongoHostName="{3}/`hostname`:$MONGO_PORT"
fi

if [[ "$MongoCommand" == "$ScriptDir/mongo/{1}/bin/mongosh" ]]; then
	sudo -u {0} \
		$MongoCommand --host "$MongoHostName" \
		--tls --authenticationMechanism MONGODB-X509 --authenticationDatabase "\$external" \
		--tlsCAFile "$ScriptDir/mongo/certificates/MongoCA.crt" --tlsCertificateKeyFile "$ScriptDir/mongo/certificates/admin.pem" \
		"${{Arguments[@]}"
else
	sudo -u {0} \
		$MongoCommand --host "$MongoHostName" \
		--ssl --authenticationMechanism MONGODB-X509 --authenticationDatabase "\$external" \
		--sslCAFile "$ScriptDir/mongo/certificates/MongoCA.crt" --sslPEMKeyFile "$ScriptDir/mongo/certificates/admin.pem" \
		"${{Arguments[@]}"
fi

