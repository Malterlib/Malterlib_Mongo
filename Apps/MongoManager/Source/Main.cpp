// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include <Mib/Core/Application>
#include <Mib/Daemon/Daemon>
#include <Mib/Concurrency/DistributedDaemon>

#include "Malterlib_Mongo_App_MongoManagerDaemon.h"

using namespace NMib;
using namespace NMib::NMongo::NMongoManager;

class CMongoManager : public CApplication
{
	aint f_Main()
	{
		NConcurrency::CDistributedDaemon Daemon
			{
				"MalterlibMongoManager"
				, "Malterlib Mongo Manager"
				, "Manages mongo database daemon"
				, []
				{
					return fg_ConstructActor<CMongoManagerDaemonActor>();
				}
			}
		;
		return Daemon.f_Run();
	}
};

DAppImplement(CMongoManager);
