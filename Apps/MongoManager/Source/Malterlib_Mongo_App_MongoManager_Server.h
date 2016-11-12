// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedApp>
#include <Mib/Daemon/Daemon>

namespace NMib::NMongo::NMongoManager
{
	struct CMongoManagerDaemonActor::CServer : public CActor
	{
	public:
		CServer(CDistributedAppState &_AppState);
		~CServer();
		void f_Construct() override;
		TCContinuation<void> f_Destroy() override;
		
	private:
		TCSharedPointer<CCanDestroyTracker> mp_pCanDestroyTracker;
		CDistributedAppState mp_AppState;
	};
}
