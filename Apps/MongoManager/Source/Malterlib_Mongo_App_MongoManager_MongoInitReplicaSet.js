var status = rs.status();

if (status.ok != 0 ||status.info != "run rs.initiate(...) if not yet done for the set")
	throw new Error("Expected database with no replica set config, not this: " + JSON.stringify(status, null, '\t'));

var memberConfig = scriptConfig.replicationConfig;
memberConfig["_id"] = 0;

var rsConfig = {
	_id: scriptConfig.replicaName,
	members: [memberConfig],
	settings: { 
		getLastErrorModes: {} 
	},
};

var errorModes = rsConfig.settings.getLastErrorModes[scriptConfig.selfTag] = {};
errorModes[scriptConfig.selfTag] = 1;

var initResult = rs.initiate(rsConfig);
if (!initResult.ok)
	throw new Error ("Failed to initialize mongo replication: " + initResult.errmsg)

// Wait for mongo replica to come up into primary state
status = rs.status();
while (status.ok != 1 || status.myState != 1) {
	sleep(100);
	status = rs.status();
}

sleep(1000); // Mongodb 3.0 lies about being in primary state

if (scriptConfig.verbose) {
	print("Resulting replica set config:");
	printjson(rs.conf());
	print("Resulting replica set status:");
	printjson(rs.status());
}
