// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedApp>
#include <Mib/Daemon/Daemon>
#include <Mib/Process/ProcessLaunch>
#include <Mib/Mongo/Client>
#include <Mib/Mongo/MongoCertificateDeploy>
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
			, EMode_WithoutReplicaSet
		};

		CMongoManagerActor(CDistributedAppState &_AppState);
		~CMongoManagerActor();
		TCFuture<void> f_RestoreMongo(CTime _RestoreTime);
		TCFuture<void> f_Startup(EMode _Mode, CStr _OverrideReplicaName, uint16 _OverridePort, TCOptional<bool> _VerboseMongoScrips);
		TCFuture<void> f_UpdateReplicationConfig();
		TCFuture<void> f_SetupPermissions();
		TCFuture<void> f_JoinReplica(CJoinReplicaOptions _Options);
		TCFuture<void> f_PreStop();

		TCFuture<CActorSubscription> f_StartBackup
			(
				TCDistributedActorInterface<CDistributedAppInterfaceBackup> _BackupInterface
				, CActorSubscription _ManifestFinished
				, CStr _BackupRoot
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

		bool fp_ShouldUseReplica() const;

		TCFuture<void> fp_SetupPrerequisites_Mongo();
		CStr fp_GetMongoExecutable(CStr const &_ExecutableName) const;
		TCFuture<void> fp_StartMongo();
		TCFuture<void> fp_DetermineHostname();

		CStr fp_GetDataPath(CStr const &_Path) const;
		CStr fp_ConcatOutput(CStr const &_StdOut, CStr const &_StdErr) const;
		TCFuture<CStr> fp_LaunchTool
			(
				CStr _Executable
				, CStr _WorkingDir
				, TCVector<CStr> _Params
				, CStr _LogCategory
				, ELogVerbosity _LogVerbosity
				, bool _bSeparateStdErr = true
				, CStr _Home = {}
				, CStr _User = {}
				, CStr _Group = {}
#ifdef DPlatformFamily_Windows
				, CStrSecure _UserPassword = {}
#endif
			)
		;
		TCFuture<CStr> fp_RunToolForVersionCheck(CStr _Tool, TCVector<CStr> _Arguments);
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
		TCFuture<CVersion> fp_CheckVersion(CStr _Tool, CStr _Argument, CStr _ParseString, CVersion _NeededVersion);
		TCFuture<void> fp_CleanupOldProcesses();

		TCFuture<void> fp_OpenSensors();
		TCFuture<void> fp_ScheduleReplicaStatusChecks();
		void fp_SetStatus(CDistributedAppSensorReporter::EStatusSeverity _Severity, CStr const &_Description);
		TCFuture<void> fp_UpdateReplicaStatus();
		TCFuture<void> fp_UpdateReplicaStatusPerform();
		CFutureCoroutineContextOnResumeScopeAwaiter fp_CheckSensorDependencies() const;

		TCActor<CMongoClientActor> fp_MongoHelper_GetClient(CMongoConnectionSettings _ConnectionSettings);

		static CStr fsp_Mongo_GetErrorCodeName(CExceptionPointer &&_pException);
		static CEJsonOrdered fsp_Mongo_SetInt32Value(int32 _Value);
		static int32 fsp_Mongo_GetInt32Value(CEJsonOrdered const *_pValue);

		TCFuture<CEJsonOrdered> fp_MongoHelper_GetHello(TCSharedPointer<CMongoClientRetryState> _pState);
		TCFuture<void> fp_MongoHelper_WaitForPrimary(TCSharedPointer<CMongoClientRetryState> _pState);
		TCFuture<void> fp_MongoHelper_WaitForSelf(TCSharedPointer<CMongoClientRetryState> _pState, bool _bExpectNotInitializedWhenPolling);
		TCFuture<CEJsonOrdered> fp_MongoHelper_GetReplicaSetStatus(TCSharedPointer<CMongoClientRetryState> _pState);
		TCFuture<CEJsonOrdered> fp_MongoHelper_GetReplicaSetConfig(TCSharedPointer<CMongoClientRetryState> _pState);

		TCFuture<CEJsonOrdered> fp_MongoHelper_GetRole(TCSharedPointer<CMongoClientRetryState> _pState, CStr _Name);
		TCFuture<void> fp_MongoHelper_CreateRole(TCSharedPointer<CMongoClientRetryState> _pState, CStr _Name, CEJsonOrdered _Role);

		TCFuture<CEJsonOrdered> fp_MongoHelper_GetUser(TCSharedPointer<CMongoClientRetryState> _pState, CStr _Name);
		TCFuture<void> fp_MongoHelper_CreateUser(TCSharedPointer<CMongoClientRetryState> _pState, CStr _Name, CEJsonOrdered _User);
		TCFuture<void> fp_MongoHelper_UpdateUser(TCSharedPointer<CMongoClientRetryState> _pState, CStr _Name, CEJsonOrdered _User);

		TCFuture<CStr> fp_Mongo_GetPrimary(CMongoConnectionSettings _ConnectionSettings);
		TCFuture<void> fp_Mongo_InitReplicaSet(CMongoConnectionSettings _ConnectionSettings, CEJsonOrdered _ReplicationConfig, CStr _SelfTag);
		TCFuture<void> fp_Mongo_JoinReplicaSet
			(
				CMongoConnectionSettings _JoinConnectionSettings
				, CMongoConnectionSettings _LocalConnectionSettings
				, CEJsonOrdered _ReplicationConfig
				, CStr _SelfTag
			)
		;
		TCFuture<void> fp_Mongo_SetupPermissions(CMongoConnectionSettings _ConnectionSettings, CStr _UserName);
		TCFuture<void> fp_Mongo_UpdateReplicationConfig(CMongoConnectionSettings _ConnectionSettings);
		TCFuture<void> fp_Mongo_WaitForPrimary(CMongoConnectionSettings _ConnectionSettings, bool _bExpectReplica);

		static bool fsp_MongoHelper_ReplicaSetStatusIsNotYetInitialized(TCAsyncResult<CEJsonOrdered> const &_Status);
		static TCFuture<void> fsp_MongoHelper_AssureNotYetInitialized(TCAsyncResult<CEJsonOrdered> _Status);

		CMongoConnectionSettings fp_LocalConnectionSettings();

		EMode mp_Mode;

		TCActor<CResolveActor> mp_ResolveActor;
		NMib::NNetwork::CNetAddress mp_MongoLocalAddress;

		TCSharedPointer<CCanDestroyTracker> mp_pCanDestroyTracker;
		CDistributedAppState &mp_AppState;

		TCSharedPointer<CUniqueUserGroup> mp_pUniqueUserGroup = fg_Construct("/M/App/MongoManager", mp_AppState.m_RootDirectory);

		CMongoConnectionSettings mp_MongoConnectionSettings{{{NProcess::NPlatform::fg_Process_GetHostName(), 25017}}};
		CUser mp_MongoUser{mp_pUniqueUserGroup->f_GetUser("mib_mongo"), mp_pUniqueUserGroup->f_GetGroup("mib_mongo")};
		CVersion mp_Version_MongoDB{6, 0, 0};
		bool mp_bEnableSSL = true;
		bool mp_bVerboseMongoScripts = false;

		// Mongo
		TCActor<CProcessLaunchActor> mp_pMongoLaunch;
		CActorSubscription mp_MongoLaunchSubscription;
		CStr mp_MongoVersion = "6.0";
		CStr mp_MongoReplicaName = "DefaultReplica";
		bool mp_bStopped = false;

		// Mongo backup
		TCMap<CStr, TCActor<CBackupManagerActorInterface>> mp_MongoBackupManagerActors;
		TCVector<TCPromise<void>> mp_PendingBackupStart;
		bool mp_bMongoBackupCanStart = false;

		// Tool launches
		TCLinkedList<CToolLaunch> mp_ToolLaunches;

		// Certificate deploy
		TCActor<CMongoCertificateDeployActor> mp_CertificateDeployActor;
		CActorSubscription mp_CertificateDeploySubscription_Admin;
		CActorSubscription mp_CertificateDeploySubscription_Server;
		bool mp_bCertificateDeployActorStarted = false;

		TCOptional<CDistributedAppSensorReporter::CSensorReporter> mp_ReplicaStatusReporter;
		TCActor<CMongoClientActor> mp_ReplicaStatusMongoClient;
		CActorSubscription mp_ReplicaStatusTimer;
		CDistributedAppSensorReporter::EStatusSeverity mp_ReplicaStatusLastSeverity = CDistributedAppSensorReporter::EStatusSeverity_Ok;
	};
}
