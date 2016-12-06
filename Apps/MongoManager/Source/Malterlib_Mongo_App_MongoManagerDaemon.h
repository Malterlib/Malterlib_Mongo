// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedApp>
#include <Mib/Daemon/Daemon>

namespace NMib::NMongo::NMongoManager
{
	struct CMongoManagerActor;
	struct CMongoManagerDaemonActor : public CDistributedAppActor
	{
		CMongoManagerDaemonActor();
		~CMongoManagerDaemonActor();
		
	private:
		TCContinuation<void> fp_StartApp(NEncoding::CEJSON const &_Params) override;
		TCContinuation<void> fp_StopApp() override;
		void fp_BuildCommandLine(CDistributedAppCommandLineSpecification &o_CommandLine) override; 
		uint32 fp_CommandLine_ListRestoreRange(NEncoding::CEJSON const &_Params);
		TCContinuation<CDistributedAppCommandLineResults> fp_CommandLine_Restore(NEncoding::CEJSON const &_Params);
		TCContinuation<CDistributedAppCommandLineResults> fp_CommandLine_UpdateReplicationConfig(NEncoding::CEJSON const &_Params);
		TCContinuation<CDistributedAppCommandLineResults> fp_CommandLine_SetupPermissions(NEncoding::CEJSON const &_Params);
		
		TCActor<CMongoManagerActor> mp_pManager;
	};
}
