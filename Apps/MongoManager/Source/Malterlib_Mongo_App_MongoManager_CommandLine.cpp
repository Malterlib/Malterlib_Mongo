// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>
#include <Mib/Daemon/Daemon>
#include <Mib/Concurrency/DistributedActor>
#include <Mib/Concurrency/DistributedActorTrustManager>
#include <Mib/Concurrency/DistributedActorTrustManagerDatabases/JSONDirectory>
#include <Mib/Concurrency/Actor/Timer>
#include <Mib/Process/StdIn>
#include <Mib/Encoding/JSONShortcuts>

#include "Malterlib_Mongo_App_MongoManager.h"
#include "Malterlib_Mongo_App_MongoManager_Server.h"

namespace NMib::NMongo::NMongoManager
{
	void CMongoManagerDaemonActor::fp_BuildCommandLine(CDistributedAppCommandLineSpecification &o_CommandLine)
	{
		CDistributedAppActor::fp_BuildCommandLine(o_CommandLine);

		o_CommandLine.f_SetProgramDescription
			(
				"Malterlib Mongo Mongo Manager"
				, "Manages mongodb server daemon." 
			)
		;
		
		auto DefaultSection = o_CommandLine.f_GetDefaultSection();
		(void)DefaultSection;
	}
}
