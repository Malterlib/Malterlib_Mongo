// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_App_MongoManagerDaemon.h"
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
		mp_pManager = fg_ConstructActor<CMongoManagerActor>(mp_State);
		CMongoManagerActor::EMode Mode = CMongoManagerActor::EMode_Normal;
		
		CStr Command = _Params["Command"].f_String();
		if (Command == "--restore")
			Mode = CMongoManagerActor::EMode_RunRestore;
		else if (Command == "--update-replication-config")
			Mode = CMongoManagerActor::EMode_UpdateReplicationConfig;
		else if (Command == "--setup-permissions")
			Mode = CMongoManagerActor::EMode_SetupPermissions;
		
		return mp_pManager(&CMongoManagerActor::f_Startup, Mode); 
	}
	
	TCContinuation<void> CMongoManagerDaemonActor::fp_StopApp()
	{	
		TCSharedPointer<CCanDestroyTracker> pCanDestroy = fg_Construct();
		
		if (mp_pManager)
		{
			DMibLogWithCategory(Mib/Mongo/MongoManager/Daemon, Info, "Shutting down");
			
			mp_pManager->f_Destroy2() > [pCanDestroy](TCAsyncResult<void> &&_Result)
				{
					if (!_Result)
						DMibLogWithCategory(Mib/Mongo/MongoManager/Daemon, Error, "Failed to shut down server: {}", _Result.f_GetExceptionStr());
				}
			;
			mp_pManager = nullptr;
		}
		
		return pCanDestroy->m_Continuation;
	}
}
