// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedApp>
#include <Mib/Daemon/Daemon>
#include <Mib/Process/ProcessLaunch>
#include <Mib/Mongo/Client>

#include "Malterlib_Mongo_App_MongoManager_Backup.h"
#include "Malterlib_Mongo_App_MongoManager_Helpers.h"

namespace NMib::NMongo::NMongoManager
{
	struct CMongoManagerActor : public CActor
	{
	public:
		enum EMode
		{
			EMode_Normal
			, EMode_RunRestore
			, EMode_UpdateReplicationConfig
			, EMode_SetupPermissions
		};
		
		CMongoManagerActor(CDistributedAppState const &_AppState);
		~CMongoManagerActor();
		TCContinuation<void> f_Destroy() override;
		TCContinuation<void> f_RestoreMongo(CTime const &_RestoreTime);
		TCContinuation<void> f_Startup(EMode _Mode);
		TCContinuation<void> f_UpdateReplicationConfig();
		TCContinuation<void> f_SetupPermissions();
		
		static void fs_SetupEnvironment(CProcessLaunchParams &_Params);
		
	private:
		enum ELogVerbosity
		{
			ELogVerbosity_None
			, ELogVerbosity_Errors
			, ELogVerbosity_Messages 
			, ELogVerbosity_All
		};
		
		void fp_StartMongoBackup();
		TCContinuation<void> fp_SetupPrerequisites_Mongo();
		CStr fp_GetMongoExecutable(CStr const &_ExecutableName) const;
		void fp_RunMongoScriptInternal(CStr const &_Script, CStr const &_LogCategory, CStr const &_Database, fp32 _Timeout, TCContinuation<void> const &_Continuation, CClock const &_Clock);
		TCContinuation<void> fp_RunMongoScript(CStr const &_Script, CStr const &_LogCategory, CStr const &_Database, fp32 _Timeout);
		TCContinuation<void> fp_StartMongo();
		CStr fp_GetDataPath(CStr const &_Path) const;
		CStr fp_ConcatOutput(CStr const &_StdOut, CStr const &_StdErr) const;
		mint fp_GetMongoFileLimits() const;
		mint fp_GetMongoThreadLimits() const;
		TCContinuation<CStr> fp_LaunchTool
			(
				CStr const &_Executable
				, CStr const &_WorkingDir
				, TCVector<CStr> const &_Params
				, CStr const &_LogCategory
 				, ELogVerbosity _LogVerbosity
				, bool _bSeparateStdErr = true
				, CStr const &_Home = {}
				, CStr const &_User = {}
			)
		;
		TCContinuation<CStr> fp_RunToolForVersionCheck
			(
				CStr const &_Tool
				, TCVector<CStr> const &_Arguments
			)
		;
		TCContinuation<void> fp_DestroyApp_Mongo();
		static void fsp_SetupUser(CUser &_User);
		TCContinuation<void> fp_ExtractExeFS() const;
		TCContinuation<void> fp_CheckVersion(CStr const &_Tool, CStr const &_Argument, CStr const &_ParseString, CVersion const &_NeededVersion);
		TCContinuation<void> fp_CleanupOldProcesses();
		
		EMode mp_Mode;
		
		TCActor<CSeparateThreadActor> mp_pFileActor;
		
		TCSharedPointer<CCanDestroyTracker> mp_pCanDestroyTracker;
		CDistributedAppState mp_AppState;

		CMongoConnectionSettings mp_MongoConnectionSettings{"localhost", 25017};
		CUser mp_MongoUser{"hx_mongo"};
		CVersion mp_Version_MongoDB{3, 2, 0};
		bool mp_bEnableSSL = true;
		
		// Mongo
		TCActor<CProcessLaunchActor> mp_pMongoLaunch;
		CActorSubscription mp_MongoLaunchSubscription;
		CStr mp_MongoReplicaName = "DefaultReplica";

		// Mongo backup
		TCActor<CBackupManagerActorInterface> mp_pMongoBackupManagerActor;
		
		// Tool launches
		TCLinkedList<CToolLaunch> mp_ToolLaunches;
	};
}
