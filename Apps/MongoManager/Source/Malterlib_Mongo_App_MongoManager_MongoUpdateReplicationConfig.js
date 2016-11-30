
renameReplsetConfig = function() {

	var rsConfig = db.system.replset.findOne();
	if (rsConfig == null)
		rsConfig = {};
	result = db.system.replset.remove({});
	
	if (result.writeConcernError) {
		throw new Error ("Failed to remove old replication config: " + result.writeConcernError.errmsg)
	}
	if (result.writeError) {
		throw new Error ("Failed to remove old replication config: " + result.writeError.errmsg)
	}
	
	rsConfig._id = MongoReplicaName;
	rsConfig.members = [{
			"_id": 0,
			"host": MongoHostName + ":" + MongoMongoPort,
			"arbiterOnly": false,
			"buildIndexes": true,
			"hidden": false,
			"priority": 1,
			"tags": {},
			"slaveDelay": 0,
			"votes": 1
	}];
	
	result = db.system.replset.insert(rsConfig);

	if (result.writeConcernError) {
		throw new Error ("Failed to add new replication config: " + result.writeConcernError.errmsg)
	}
	if (result.writeError) {
		throw new Error ("Failed to add new replication config: " + result.writeError.errmsg)
	}
}

renameReplsetConfig();
