// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedApp>
#include <Mib/Daemon/Daemon>
#include <Mib/Process/ProcessLaunch>
#include <Mib/Mongo/Client>
#include <Mib/Storage/Optional>

#include "Malterlib_Mongo_App_MongoManager_Backup.h"
#include "Malterlib_Mongo_App_MongoManager_Helpers.h"

namespace NMib::NMongo::NMongoManager
{
	struct CJoinReplicaOptions
	{
		CStr m_MemberToJoin;

		TCOptional<uint16> m_Port;
		TCOptional<CStr> m_ReplicaName;
		TCOptional<fp64> m_Priority;
		TCOptional<TCMap<CStr, CStr>> m_ExtraTags;
		TCOptional<bool> m_CanVote;
		TCOptional<bool> m_ArbiterOnly;
		TCOptional<bool> m_BuildIndexes;
		TCOptional<bool> m_Hidden;
	};
	
	struct CMongoManagerActor : public CActor
	{
	public:
		using CActorHolder = CDelegatedActorHolder;
		
		enum EMode
		{
			EMode_Normal
			, EMode_RunRestore
			, EMode_UpdateReplicationConfig
			, EMode_SetupPermissions
			, EMode_JoinReplicaSet
		};
		
		CMongoManagerActor(CDistributedAppState &_AppState);
		~CMongoManagerActor();
		TCContinuation<void> f_Destroy() override;
		TCContinuation<void> f_RestoreMongo(CTime const &_RestoreTime);
		TCContinuation<void> f_Startup(EMode _Mode, CStr const &_OverrideReplicaName, uint16 _OverridePort, TCOptional<bool> const &_VerboseMongoScrips);
		TCContinuation<void> f_UpdateReplicationConfig();
		TCContinuation<void> f_SetupPermissions();
		TCContinuation<void> f_JoinReplica(CJoinReplicaOptions const &_Options);
		
		static void fs_SetupEnvironment(CProcessLaunchParams &_Params);
		
		static mint fs_GetMongoFileLimits();
		static mint fs_GetMongoThreadLimits();
		
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
		void fp_RunMongoScriptInternal
			(
				CMongoConnectionSettings const &_MongoConnectionSettings 
				, CStr const &_Script
				, CStr const &_LogCategory
				, CStr const &_Database
				, fp32 _Timeout
				, TCContinuation<CStr> const &_Continuation
				, CClock const &_Clock
				, CEJSON const &_Config
			)
		;
		TCContinuation<CStr> fp_RunMongoScript
			(
				CMongoConnectionSettings const &_MongoConnectionSettings
				, CStr const &_Script
				, CStr const &_Database
				, fp32 _Timeout
				, CEJSON const &_Config
			)
		;
		TCContinuation<void> fp_StartMongo();
		CStr fp_GetDataPath(CStr const &_Path) const;
		CStr fp_ConcatOutput(CStr const &_StdOut, CStr const &_StdErr) const;
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
		CDistributedAppState &mp_AppState;

		CMongoConnectionSettings mp_MongoConnectionSettings{"localhost", 25017};
		CUser mp_MongoUser{"hx_mongo"};
		CVersion mp_Version_MongoDB{3, 4, 0};
		bool mp_bEnableSSL = true;
		bool mp_bVerboseMongoScripts = false;
		
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
