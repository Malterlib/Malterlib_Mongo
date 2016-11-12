// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>
#include <Mib/Daemon/Daemon>
#include <Mib/Concurrency/DistributedActor>
#include <Mib/Concurrency/DistributedActorTrustManager>
#include <Mib/Concurrency/DistributedActorTrustManagerDatabases/JSONDirectory>

#include "Malterlib_Mongo_App_MongoManager.h"
#include "Malterlib_Mongo_App_MongoManager_Server.h"

namespace NMib::NMongo::NMongoManager
{
	CMongoManagerDaemonActor::CServer::CServer(CDistributedAppState &_AppState)
		: mp_AppState(_AppState)
		, mp_pCanDestroyTracker(fg_Construct())
	{
#ifdef DPlatformFamily_OSX
		CStr Path = NSys::fg_Process_GetEnvironmentVariable(CStr("PATH"));
		if (Path.f_Find("/opt/local/bin") < 0)
			NSys::fg_Process_SetEnvironmentVariable(CStr("PATH"), "/opt/local/bin:" + Path);
#endif
	}
	
	CMongoManagerDaemonActor::CServer::~CServer()
	{
	}
	
	void CMongoManagerDaemonActor::CServer::f_Construct()
	{
	}
	
	TCContinuation<void> CMongoManagerDaemonActor::CServer::f_Destroy()
	{
		auto pCanDestroy = fg_Move(mp_pCanDestroyTracker);
		return pCanDestroy->m_Continuation;
	}
}
