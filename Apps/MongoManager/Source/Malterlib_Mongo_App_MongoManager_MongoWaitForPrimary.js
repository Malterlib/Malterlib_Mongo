var status = rs.status();

if (!scriptConfig.expectReplica) {
	if (status.ok == 0 && status.codeName == "NotYetInitialized")
		quit(0);
	else
		throw new Error("Replication config not expected but found: " + JSON.stringify(status, null, "\t"));
}

findSelf = function() {
	for (var i = 0, n = status.members.length; i < n; i++) {
		var member = status.members[i];
		if (member.self && member.name == scriptConfig.mongoSelf)
			return true;
	}
	
	return false;
}

waitForSelf = function () {
	// Wait for self to be primary or secondary
	while (status.ok != 1 || status.myState > 2) {
		sleep(100);
		status = rs.status();
	}
}

waitForPrimary = function() {
	// Wait for any member to be primary
	for (;;) {
		for (var i = 0, n = status.members.length; i < n; i++) {
			var member = status.members[i];
			if (member.state == 1)
				return;
		}

		sleep(100);
		status = rs.status();
	}
}

waitForSelf();

if (!findSelf())
	throw new Error("Didn't find this server '" + scriptConfig.mongoSelf + "' in replication config. You will have to manually fix the problem. If this is a standalone server you can use --update-replication-config.")

waitForPrimary();

// Mongo lies about being up
sleep(1000);

if (scriptConfig.verbose) {
	print("Replica set config:");
	printjson(rs.conf());
	print("Replica set status:");
	printjson(rs.status());
}
