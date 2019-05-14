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
#include <Mib/Security/UniqueUserGroup>
#include <Mib/Network/ResolveActor>

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
		TCFuture<void> f_RestoreMongo(CTime const &_RestoreTime);
		TCFuture<void> f_Startup(EMode _Mode, CStr const &_OverrideReplicaName, uint16 _OverridePort, TCOptional<bool> const &_VerboseMongoScrips);
		TCFuture<void> f_UpdateReplicationConfig();
		TCFuture<void> f_SetupPermissions();
		TCFuture<void> f_JoinReplica(CJoinReplicaOptions const &_Options);
		TCFuture<void> f_PreStop();

		TCFuture<CActorSubscription> f_StartBackup
			(
				TCDistributedActorInterface<CDistributedAppInterfaceBackup> &&_BackupInterface
				, CActorSubscription &&_ManifestFinished
				, CStr const &_BackupRoot
			)
		;

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
		TCFuture<void> fp_Destroy() override;

		void fp_StartMongoBackup();

		TCFuture<void> fp_SetupPrerequisites_Mongo();
		CStr fp_GetMongoExecutable(CStr const &_ExecutableName) const;
		void fp_RunMongoScriptInternal
			(
				CMongoConnectionSettings const &_MongoConnectionSettings
				, CStr const &_Script
				, CStr const &_LogCategory
				, CStr const &_Database
				, fp32 _Timeout
				, TCPromise<CStr> const &_Promise
				, CClock const &_Clock
				, CEJSON const &_Config
			)
		;
		TCFuture<CStr> fp_RunMongoScript
			(
				CMongoConnectionSettings const &_MongoConnectionSettings
				, CStr const &_Script
				, CStr const &_Database
				, fp32 _Timeout
				, CEJSON const &_Config
			)
		;
		TCFuture<void> fp_StartMongo();
		TCFuture<void> fp_DetermineHostname();

		CStr fp_GetDataPath(CStr const &_Path) const;
		CStr fp_ConcatOutput(CStr const &_StdOut, CStr const &_StdErr) const;
		TCFuture<CStr> fp_LaunchTool
			(
				CStr const &_Executable
				, CStr const &_WorkingDir
				, TCVector<CStr> const &_Params
				, CStr const &_LogCategory
 				, ELogVerbosity _LogVerbosity
				, bool _bSeparateStdErr = true
				, CStr const &_Home = {}
				, CStr const &_User = {}
			 	, CStr const &_Group = {}
#ifdef DPlatformFamily_Windows
				, CStrSecure const &_UserPassword = {}
#endif
			)
		;
		TCFuture<CStr> fp_RunToolForVersionCheck
			(
				CStr const &_Tool
				, TCVector<CStr> const &_Arguments
			)
		;
		TCFuture<void> fp_DestroyApp_Mongo();

		static void fsp_SetupUser
			(
				CUser &_User
#ifdef DPlatformFamily_Windows
				, CStrSecure &o_Password
#endif
			)
		;
#ifdef DPlatformFamily_Windows
		CStrSecure fp_GetUserPassword(CStr const &_User);
#endif
		TCFuture<void> fp_ExtractExeFS() const;
		TCFuture<void> fp_CheckVersion(CStr const &_Tool, CStr const &_Argument, CStr const &_ParseString, CVersion const &_NeededVersion);
		TCFuture<void> fp_CleanupOldProcesses();

		EMode mp_Mode;

		TCActor<CSeparateThreadActor> mp_pFileActor;
		TCActor<CResolveActor> mp_ResolveActor;
		NMib::NNetwork::CNetAddress mp_MongoLocalAddress;

		TCSharedPointer<CCanDestroyTracker> mp_pCanDestroyTracker;
		CDistributedAppState &mp_AppState;

		TCSharedPointer<CUniqueUserGroup> mp_pUniqueUserGroup = fg_Construct("/M/App/MongoManager", mp_AppState.m_RootDirectory);

		CMongoConnectionSettings mp_MongoConnectionSettings{NProcess::NPlatform::fg_Process_GetHostName(), 25017};
		CUser mp_MongoUser{mp_pUniqueUserGroup->f_GetUser("mib_mongo"), mp_pUniqueUserGroup->f_GetGroup("mib_mongo")};
		CVersion mp_Version_MongoDB{3, 4, 0};
		bool mp_bEnableSSL = true;
		bool mp_bVerboseMongoScripts = false;

		// Mongo
		TCActor<CProcessLaunchActor> mp_pMongoLaunch;
		CActorSubscription mp_MongoLaunchSubscription;
		CStr mp_MongoReplicaName = "DefaultReplica";
		bool mp_bStopped = false;

		// Mongo backup
		TCMap<CStr, TCActor<CBackupManagerActorInterface>> mp_MongoBackupManagerActors;
		TCVector<TCFunctionMovable<void (bool _bAbort)>> mp_PendingBackupStart;
		bool mp_bMongoBackupCanStart = false;

		// Tool launches
		TCLinkedList<CToolLaunch> mp_ToolLaunches;
	};
}
