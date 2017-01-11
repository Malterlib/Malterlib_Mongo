// Mongo URI parser in 3.4.1 refuses to accept $external, so use the correct DB here
externalDB = db.getSiblingDB("$external");

setupPermissions = function() {
	adminUser = scriptConfig.mongoAdminDN;
	
	adminRoles = [
		{ role: "root", db: "admin" },
	];
	
	if (externalDB.getUser(adminUser))
		externalDB.updateUser(adminUser, { roles: adminRoles });
	else
		externalDB.createUser({ user: adminUser, roles: adminRoles });
}

setupPermissions();
