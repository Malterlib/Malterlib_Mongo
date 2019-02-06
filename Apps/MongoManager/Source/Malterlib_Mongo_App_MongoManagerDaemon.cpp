// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_App_MongoManagerDaemon.h"
#include "Malterlib_Mongo_App_MongoManager_Server.h"

namespace NMib::NMongo::NMongoManager
{
	CMongoManagerDaemonActor::CMongoManagerDaemonActor()
		: CDistributedAppActor(CDistributedAppActor_Settings{"MongoManager"})
	{
	}
	
	CMongoManagerDaemonActor::~CMongoManagerDaemonActor()
	{
	}

	void CMongoManagerDaemonActor::fp_PopulateAppInterfaceRegisterInfo(CDistributedAppInterfaceServer::CRegisterInfo &o_RegisterInfo, NEncoding::CEJSON const &_Params)
	{
		o_RegisterInfo.m_UpdateType = EDistributedAppUpdateType_OneAtATime;
		
		mint nMaxFilesNeeded = 8192;
		nMaxFilesNeeded += CMongoManagerActor::fs_GetMongoFileLimits();

		mint nFilesPerProc = 8192;
		nFilesPerProc = fg_Max(nFilesPerProc, CMongoManagerActor::fs_GetMongoFileLimits());
		
		mint nMaxThreads = 1024;
		nMaxThreads += CMongoManagerActor::fs_GetMongoThreadLimits();

		mint nMaxPids = 32; // Our own
		nMaxPids += 64000; // For mongod
		
		o_RegisterInfo.m_Resources_Files = nMaxFilesNeeded;
		o_RegisterInfo.m_Resources_Threads = nMaxThreads; 
		o_RegisterInfo.m_Resources_FilesPerProcess = nFilesPerProc;
		o_RegisterInfo.m_Resources_Processes = nMaxPids; 
	}
	
	TCFuture<void> CMongoManagerDaemonActor::fp_StartApp(NEncoding::CEJSON const &_Params)
	{
		mp_pManager = fg_ConstructActor<CMongoManagerActor>(fg_Construct(self), mp_State);
		CMongoManagerActor::EMode Mode = CMongoManagerActor::EMode_Normal;
		
		CStr Command = _Params["Command"].f_String();
		if (Command == "--restore")
			Mode = CMongoManagerActor::EMode_RunRestore;
		else if (Command == "--update-replication-config")
			Mode = CMongoManagerActor::EMode_UpdateReplicationConfig;
		else if (Command == "--setup-permissions")
			Mode = CMongoManagerActor::EMode_SetupPermissions;
		else if (Command == "--join-replica-set")
			Mode = CMongoManagerActor::EMode_JoinReplicaSet;
		
		CStr ReplicaName;
		if (auto pValue = _Params.f_GetMember("MongoReplicaName"))
			ReplicaName = pValue->f_String(); 
		
		uint16 Port = 0;
		if (auto pValue = _Params.f_GetMember("MongoPort"))
			Port = pValue->f_Integer(); 
		
		TCOptional<bool> VerboseMongoScrips;
		if (auto pValue = _Params.f_GetMember("VerboseMongoScripts"))
			VerboseMongoScrips = pValue->f_Boolean(); 
		
		return mp_pManager(&CMongoManagerActor::f_Startup, Mode, ReplicaName, Port, VerboseMongoScrips); 
	}
	
	TCFuture<void> CMongoManagerDaemonActor::fp_StopApp()
	{	
		TCSharedPointer<CCanDestroyTracker> pCanDestroy = fg_Construct();
		
		if (mp_pManager)
		{
			DMibLogWithCategory(Mib/Mongo/MongoManager/Daemon, Info, "Shutting down");
			
			mp_pManager->f_Destroy() > [pCanDestroy](TCAsyncResult<void> &&_Result)
				{
					if (!_Result)
						DMibLogWithCategory(Mib/Mongo/MongoManager/Daemon, Error, "Failed to shut down server: {}", _Result.f_GetExceptionStr());
				}
			;
			mp_pManager = nullptr;
		}
		
		return pCanDestroy->f_Future();
	}
	
	TCFuture<void> CMongoManagerDaemonActor::fp_PreStop()
	{
		if (!mp_pManager)
			return fg_Explicit();

		DMibLogWithCategory(Mib/Mongo/MongoManager/Daemon, Info, "Running pre-stop");
		
		TCPromise<void> Promise;
		mp_pManager(&CMongoManagerActor::f_PreStop) > [Promise](TCAsyncResult<void> &&_Result)
			{
				if (!_Result)
					DMibLogWithCategory(Mib/Mongo/MongoManager/Daemon, Error, "Failed to pre-stop down server: {}", _Result.f_GetExceptionStr());
				Promise.f_SetResult();
			}
		;

		return Promise.f_MoveFuture();
	}
	
	TCFuture<CActorSubscription> CMongoManagerDaemonActor::fp_StartBackup
		(
			TCDistributedActorInterface<CDistributedAppInterfaceBackup> &&_BackupInterface
			, CActorSubscription &&_ManifestFinished
			, CStr const &_BackupRoot
		)
	{
		if (!mp_pManager)
			return DMibErrorInstance("App not started");

		TCPromise<CActorSubscription> Promise;
		mp_pManager(&CMongoManagerActor::f_StartBackup, fg_Move(_BackupInterface), fg_Move(_ManifestFinished), _BackupRoot) > [Promise](TCAsyncResult<CActorSubscription> &&_Result)
			{
				if (!_Result)
				{
					DMibLogWithCategory(MongoManager/Daemon, Error, "Failed to start backup: {}", _Result.f_GetExceptionStr());
				}
				
				Promise.f_SetResult(fg_Move(_Result));
			}
		;
		
		return Promise.f_MoveFuture();
	}
}
