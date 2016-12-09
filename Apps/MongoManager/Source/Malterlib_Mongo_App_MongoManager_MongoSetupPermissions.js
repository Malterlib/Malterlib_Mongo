setupPermissions = function() {
	adminUser = scriptConfig.mongoAdminDN;
	
	adminRoles = [
		{ role: "root", db: "admin" },
	];
	
	if (db.getUser(adminUser))
		db.updateUser(adminUser, { roles: adminRoles });
	else
		db.createUser({ user: adminUser, roles: adminRoles });
}

setupPermissions();
