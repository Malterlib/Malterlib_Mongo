// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>
#include <Mib/Core/Application>
#include <Mib/Daemon/Daemon>
#include <Mib/Concurrency/DistributedDaemon>

#include "Malterlib_Mongo_App_MongoManager.h"

using namespace NMib;
using namespace NMib::NMongo::NMongoManager;

class CMongoManager : public CApplication
{
	aint f_Main()
	{
		NConcurrency::CDistributedDaemon Daemon
			{
				"MalterlibMongoMongoManager"
				, "Malterlib Mongo Mongo Manager"
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
