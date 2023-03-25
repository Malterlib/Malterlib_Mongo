// Mongo URI parser in 3.4.1 refuses to accept $external, so use the correct DB here
externalDB = db.getSiblingDB("$external");
adminDB = db.getSiblingDB("admin");

setupPermissions = function() {
	adminUser = scriptConfig.mongoAdminDN;

	if (!adminDB.getRole("oplogger")) {
		adminDB.createRole({ role: "oplogger",
			privileges: [{ resource: { db: 'local', collection: 'oplog.rs' },
				actions: ['find'] },
			],
			roles: [{ role: 'read', db: 'local' }]
		});
	}
	if (!adminDB.getRole("anyActionOnAnyResource")) {
		adminDB.createRole({ role: "anyActionOnAnyResource",
			privileges: [{ resource: { anyResource: true },
				actions: ['anyAction'] },
			],
			roles: []
		});
	}

	adminRoles = [
		{ role: "root", db: "admin" },
		{ role: "read", db: "local" },
		{ role: "oplogger", db: "admin" },
		{ role: "anyActionOnAnyResource", db: "admin" },
	];

	if (externalDB.getUser(adminUser))
		externalDB.updateUser(adminUser, { roles: adminRoles });
	else
		externalDB.createUser({ user: adminUser, roles: adminRoles });
}

setupPermissions();
