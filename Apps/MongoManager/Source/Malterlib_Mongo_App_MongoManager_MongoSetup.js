
initReplication = function() {
	sleep(1000);
	var rsConfig = {
		"_id": MongoReplicaName,
		"members":[{
			"_id": 0,
			"host": MongoHostName + ":" + MongoMongoPort,
			"arbiterOnly": false,
			"buildIndexes": true,
			"hidden": false,
			"priority": 1,
			"tags": {},
			"slaveDelay": 0,
			"votes": 1
		}]
	};
	initResult = rs.initiate(rsConfig);
	if (!initResult.ok && initResult.errmsg != "already initialized") {
		throw new Error ("Failed to initialize mongo replication: " + initResult.errmsg)
	}
	
	// Wait for mongo replica to come up into primary state
	status = rs.status();
	while (status.ok == 1 && status.myState != 1) {
		sleep(100);
		status = rs.status();
	}

	sleep(1000); // Mongodb 3.0 lies about being in primary state
	
	config = rs.config();
	
	var compareConfig = {};
	compareConfig._id = config._id;
 	compareConfig.members = config.members;
	
	for (member in compareConfig.members)
		delete compareConfig.members[member].slaveDelay;
	
	for (member in rsConfig.members)
		delete rsConfig.members[member].slaveDelay;
 	
	if (JSON.stringify(compareConfig) !== JSON.stringify(rsConfig))
		throw new Error ("Replication config differs from expected. Please restart daemon with 'UpdateReplicationConfig 1' in HansosftX.conf: \n" + JSON.stringify(compareConfig) + "\n!=\n" + JSON.stringify(rsConfig) + "\n")
}

initReplication();
