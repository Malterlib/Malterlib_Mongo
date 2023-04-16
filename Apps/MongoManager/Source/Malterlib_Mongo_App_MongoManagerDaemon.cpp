// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_App_MongoManagerDaemon.h"
#include "Malterlib_Mongo_App_MongoManager_Server.h"

namespace NMib::NMongo::NMongoManager
{
	CMongoManagerDaemonActor::CMongoManagerDaemonActor()
		: CDistributedAppActor
		(
			CDistributedAppActor_Settings{"MongoManager"}
			.f_CommandLineBeforeAppStart(true)
		)
	{
	}
	
	CMongoManagerDaemonActor::~CMongoManagerDaemonActor()
	{
	}

	void CMongoManagerDaemonActor::fp_PopulateAppInterfaceInfo
		(
			CDistributedAppInterfaceServer::CRegisterInfo &o_RegisterInfo
			, CDistributedAppInterfaceServer::CConfigFiles &o_ConfigFiles
			, NEncoding::CEJSON const &_Params
		)
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
		o_RegisterInfo.m_Resources_MaxMapCount = 128000;
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
		
		co_await mp_pManager(&CMongoManagerActor::f_Startup, Mode, ReplicaName, Port, VerboseMongoScrips);

		co_return {};
	}
	
	TCFuture<void> CMongoManagerDaemonActor::fp_StopApp()
	{	
		if (mp_pManager)
		{
			DMibLogWithCategory(Mib/Mongo/MongoManager/Daemon, Info, "Shutting down");
			co_await (fg_Move(mp_pManager).f_Destroy() % "Failed to shut down server");
		}
		
		co_return {};
	}
	
	TCFuture<void> CMongoManagerDaemonActor::fp_PreStop()
	{
		if (!mp_pManager)
			co_return {};

		DMibLogWithCategory(Mib/Mongo/MongoManager/Daemon, Info, "Running pre-stop");
		
		co_await (mp_pManager(&CMongoManagerActor::f_PreStop) % "Failed to pre-stop server");

		co_return {};
	}
	
	TCFuture<CActorSubscription> CMongoManagerDaemonActor::fp_StartBackup
		(
			TCDistributedActorInterface<CDistributedAppInterfaceBackup> &&_BackupInterface
			, CActorSubscription &&_ManifestFinished
			, CStr const &_BackupRoot
		)
	{
		if (!mp_pManager)
			co_return DMibErrorInstance("App not started");

		co_return co_await (mp_pManager(&CMongoManagerActor::f_StartBackup, fg_Move(_BackupInterface), fg_Move(_ManifestFinished), _BackupRoot) % "Failed to start backup");
	}
}
