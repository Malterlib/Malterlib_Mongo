var masterInfo = rs.isMaster();

if (!masterInfo || !masterInfo.ismaster)
	throw new Error("Trying to join replica set on non primary: " + JSON.stringify(masterInfo, null, '\t'));

var memberConfig = scriptConfig.replicationConfig;

var addResult = rs.add(memberConfig);
if (!addResult.ok)
	throw new Error ("Failed to add to replica set: " + addResult.errmsg)

var rsConfig = rs.conf();
var errorModes = rsConfig.settings.getLastErrorModes[scriptConfig.selfTag] = {};
errorModes[scriptConfig.selfTag] = 1;

var reconfigureResult = rs.reconfig(rsConfig);
if (!reconfigureResult.ok)
	throw new Error ("Failed to add add write concern: " + reconfigureResult.errmsg)

if (scriptConfig.verbose) {
	print("Resulting replica set config:");
	printjson(rs.conf());
	print("Resulting replica set status:");
	printjson(rs.status());
}
