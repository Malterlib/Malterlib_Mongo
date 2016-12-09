var masterInfo = rs.isMaster();

if (!masterInfo || !masterInfo.primary)
	throw new Error("No primary found in master info: " + JSON.stringify(masterInfo, null, '\t'));
	
print(masterInfo.primary);
