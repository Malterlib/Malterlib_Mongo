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
	CMongoManagerDaemonActor::CMongoManagerDaemonActor()
		: CDistributedAppActor(CDistributedAppActor_Settings{"MongoManager", false})
	{
	}
	
	CMongoManagerDaemonActor::~CMongoManagerDaemonActor()
	{
	}

	TCContinuation<void> CMongoManagerDaemonActor::fp_StartApp(NEncoding::CEJSON const &_Params)
	{
		TCContinuation<void> Continuation;
		mp_pServer = fg_ConstructActor<CServer>(mp_State);
		Continuation.f_SetResult();
		return Continuation;				
	}
	
	TCContinuation<void> CMongoManagerDaemonActor::fp_StopApp()
	{	
		TCSharedPointer<CCanDestroyTracker> pCanDestroy = fg_Construct();
		
		if (mp_pServer)
		{
			DMibLogWithCategory(Mib/Mongo/MongoManager/Daemon, Info, "Shutting down");
			
			mp_pServer->f_Destroy
				(
					[pCanDestroy](TCAsyncResult<void> &&_Result)
					{
						if (!_Result)
							DMibLogWithCategory(Mib/Mongo/MongoManager/Daemon, Error, "Failed to shut down server: {}", _Result.f_GetExceptionStr());
					}
				)
			;
			mp_pServer = nullptr;
		}
		
		return pCanDestroy->m_Continuation;
	}
}
