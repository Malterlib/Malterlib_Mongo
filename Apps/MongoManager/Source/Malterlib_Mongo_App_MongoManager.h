// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedApp>
#include <Mib/Daemon/Daemon>

namespace NMib::NMongo::NMongoManager
{
	struct CMongoManagerDaemonActor : public CDistributedAppActor
	{
		CMongoManagerDaemonActor();
		~CMongoManagerDaemonActor();
		
		struct CServer;
	private:
		TCContinuation<void> fp_StartApp(NEncoding::CEJSON const &_Params) override;
		TCContinuation<void> fp_StopApp() override;
		void fp_BuildCommandLine(CDistributedAppCommandLineSpecification &o_CommandLine) override; 
		
		TCActor<CServer> mp_pServer;
	};
}
