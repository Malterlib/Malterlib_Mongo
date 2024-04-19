// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_App_MongoManager_Server.h"

#include <Mib/File/ExeFS>
#include <Mib/File/VirtualFS>
#include <Mib/File/VirtualFSs/MalterlibFS>
#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Concurrency/LogError>

namespace NMib::NMongo::NMongoManager
{
	CMongoManagerActor::CMongoManagerActor(CDistributedAppState &_AppState)
		: mp_AppState(_AppState)
		, mp_pCanDestroyTracker(fg_Construct())
	{
#ifdef DPlatformFamily_macOS
		CStr Path = fg_GetSys()->f_GetEnvironmentVariable("PATH");

		CStr OriginalPath = Path;

		if (Path.f_Find("/usr/local/bin") < 0)
			Path = "/usr/local/bin:" + Path;
		if (Path.f_Find("/opt/homebrew/bin") < 0)
			Path = "/opt/homebrew/bin:" + Path;

		if (Path != OriginalPath)
			fg_GetSys()->f_SetEnvironmentVariable("PATH", Path);
#endif
	}

	CMongoManagerActor::~CMongoManagerActor()
	{
	}

	TCFuture<void> CMongoManagerActor::f_Startup(EMode _Mode, CStr const &_OverrideReplicaName, uint16 _Port, TCOptional<bool> const &_VerboseMongoScrips)
	{
		mp_Mode = _Mode;

		auto &Config = mp_AppState.m_ConfigDatabase.m_Data;

		if (_VerboseMongoScrips)
			mp_bVerboseMongoScripts = *_VerboseMongoScrips;
		else if (auto *pValue = Config.f_GetMember("VerboseMongoScripts", EJSONType_Boolean))
			mp_bVerboseMongoScripts = pValue->f_Boolean();

		if (_Port)
			mp_MongoConnectionSettings.m_Hosts[0].m_Port = _Port;
		else if (auto *pValue = Config.f_GetMember("MongoPort", EJSONType_Integer))
			mp_MongoConnectionSettings.m_Hosts[0].m_Port = pValue->f_Integer();

		if (!_OverrideReplicaName.f_IsEmpty())
			mp_MongoReplicaName = _OverrideReplicaName;
		else if (auto pValue = mp_AppState.m_ConfigDatabase.m_Data.f_GetMember("ReplicaName", EJSONType_String))
			mp_MongoReplicaName = pValue->f_String();

		if (auto pValue = mp_AppState.m_ConfigDatabase.m_Data.f_GetMember("MongoVersion", EJSONType_String))
			mp_MongoVersion = pValue->f_String();

		if (mp_Mode == EMode_SetupPermissions || mp_Mode == EMode_RunRestore)
			mp_bEnableSSL = false;
		else if (auto pValue = mp_AppState.m_ConfigDatabase.m_Data.f_GetMember("EnableSSL", EJSONType_Boolean))
			mp_bEnableSSL = pValue->f_Boolean();
		
		co_await fp_OpenSensors();

		CStr MongoDirectory = fp_GetDataPath("mongo");

		mp_MongoConnectionSettings.m_CACertificatePath = MongoDirectory + "/certificates/MongoCA.crt";
		mp_MongoConnectionSettings.m_ClientCertificatePath = MongoDirectory + "/certificates/admin.pem";
		mp_MongoConnectionSettings.m_bEnableSSL = mp_bEnableSSL;

		DLog(Info, "Extracting ExeFS");

		co_await (self(&CMongoManagerActor::fp_CleanupOldProcesses) % "Failed to clean up old processes");
		co_await (self(&CMongoManagerActor::fp_ExtractExeFS) % "Failed to extract ExeFS");

		DLog(Info, "Done extracting ExeFS");

		auto [Version, Dummy1, Dummy2] = co_await
			(
				self(&CMongoManagerActor::fp_CheckVersion, fp_GetMongoExecutable("mongod"), "--version", "db version v{}.{}.{}\n", mp_Version_MongoDB)
				+ self(&CMongoManagerActor::fp_SetupPrerequisites_Mongo)
				+ self(&CMongoManagerActor::fp_DetermineHostname)
			)
		;

		DLog(Info, "MongoDB client connection URL: {}", mp_MongoConnectionSettings.f_GetUrl("").f_Encode());

		mp_Version_MongoDB = Version;

		co_await self(&CMongoManagerActor::fp_StartMongo);

		if (mp_Mode == EMode_UpdateReplicationConfig || mp_Mode == EMode_SetupPermissions)
			co_return {};

		co_await self
			(
				&CMongoManagerActor::fp_RunMongoScript
				, mp_MongoConnectionSettings
				, "MongoWaitForPrimary"
				, "local"
				, 5.0*60.0
				, CEJSONSorted{"expectReplica"_= mp_Mode != EMode_JoinReplicaSet}
			)
		;

		if (mp_Mode == EMode_Normal)
			CMongoManagerActor::fp_StartMongoBackup();

		co_await fp_ScheduleReplicaStatusChecks();

		co_return {};
	}

	TCFuture<void> CMongoManagerActor::f_PreStop()
	{
		DLog(Debug, "Pre-stop server");
		mp_bStopped = true;

		if (mp_ReplicaStatusTimer)
			co_await fg_Exchange(mp_ReplicaStatusTimer, nullptr)->f_Destroy();

		if (mp_ReplicaStatusReporter)
			co_await fg_Move(mp_ReplicaStatusReporter->m_fReportReadings).f_Destroy();

		if (mp_ReplicaStatusMongoClient)
			co_await fg_Move(mp_ReplicaStatusMongoClient).f_Destroy();

		TCActorResultVector<void> Destroys;
		for (auto &ToolLaunch : mp_ToolLaunches)
			ToolLaunch.m_ProcessLaunch.f_Destroy() > Destroys.f_AddResult();

		co_await Destroys.f_GetResults();
		co_await fp_DestroyApp_Mongo();

		DLog(Debug, "Pre-stop server done");

		co_return {};
	}

	TCFuture<void> CMongoManagerActor::fp_Destroy()
	{
		DLog(Debug, "Destroy server");

		if (mp_ReplicaStatusTimer)
			co_await fg_Exchange(mp_ReplicaStatusTimer, nullptr)->f_Destroy();

		if (mp_ReplicaStatusReporter)
			co_await fg_Move(mp_ReplicaStatusReporter->m_fReportReadings).f_Destroy();

		if (mp_ReplicaStatusMongoClient)
			co_await fg_Move(mp_ReplicaStatusMongoClient).f_Destroy();

		CLogError LogError("MongoManager");

		{
			auto CanDestroyFuture = fg_Exchange(mp_pCanDestroyTracker, nullptr)->f_Future();
			co_await fg_Move(CanDestroyFuture).f_Wrap() > LogError.f_Warning("Failed to destroy can destroy");
		}

		{
			TCActorResultVector<void> Destroys;
			for (auto &ToolLaunch : mp_ToolLaunches)
				ToolLaunch.m_ProcessLaunch.f_Destroy() > Destroys.f_AddResult();

			co_await Destroys.f_GetUnwrappedResults().f_Wrap() > LogError.f_Warning("Failed to destroy tool launches");
		}

		{
			TCActorResultVector<void> Destroys;

			for (auto &Pending : mp_PendingBackupStart)
				Pending.f_SetException(DMibErrorInstance("Aborted"));

			mp_PendingBackupStart.f_Clear();

			for (auto &Actor : mp_MongoBackupManagerActors)
			{
				if (!Actor)
					continue;
				Actor.f_Destroy() > Destroys.f_AddResult();
			}

			co_await Destroys.f_GetUnwrappedResults().f_Wrap() > LogError.f_Warning("Failed to destroy backup manager");;
		}

		co_await fp_DestroyApp_Mongo().f_Wrap() > LogError.f_Warning("Failed to mongo");;

		if (mp_CertificateDeploySubscription_Admin)
			co_await fg_Exchange(mp_CertificateDeploySubscription_Admin, nullptr)->f_Destroy().f_Wrap() > LogError.f_Warning("Failed to destroy admin user certificate deploy subscription");

		if (mp_CertificateDeploySubscription_Server)
			co_await fg_Exchange(mp_CertificateDeploySubscription_Server, nullptr)->f_Destroy().f_Wrap() > LogError.f_Warning("Failed to destroy server certificate deploy subscription");

		if (mp_CertificateDeployActor)
			co_await fg_Move(mp_CertificateDeployActor).f_Destroy().f_Wrap() > LogError.f_Warning("Failed to destroy certificate deploy actor");

		DLog(Debug, "Destroy server done");

		co_return {};
	}

#ifdef DPlatformFamily_Windows
	CStrSecure CMongoManagerActor::fp_GetUserPassword(CStr const &_User)
	{
		if (auto pUsers = mp_AppState.m_StateDatabase.m_Data.f_GetMember("Users", EJSONType_Object))
		{
			if (auto pUser = pUsers->f_GetMember(_User, EJSONType_Object))
			{
				if (auto pPassword = pUser->f_GetMember("Password", EJSONType_String))
					return pPassword->f_String();
			}
		}
		return {};
	}
#endif

	void CMongoManagerActor::fsp_SetupUser
		(
			CUser &_User
#ifdef DPlatformFamily_Windows
			, CStrSecure &o_Password
#endif
		)
	{
		if (!NSys::fg_UserManagement_GroupExists(_User.m_GroupName, _User.m_GroupID))
			NSys::fg_UserManagement_CreateGroup(_User.m_GroupName, _User.m_GroupID);

		if (!NSys::fg_UserManagement_UserExists(_User.m_UserName, _User.m_UserID))
		{
#ifdef DPlatformFamily_Windows
			o_Password = fg_HighEntropyRandomID("23456789ABCDEFGHJKLMNPQRSTWXYZabcdefghijkmnopqrstuvwxyz&=*!@~^") + "2Dg&";
#endif
			NSys::fg_UserManagement_CreateUser
				(
					_User.m_GroupName
					, _User.m_UserName
#ifdef DPlatformFamily_Windows
					, o_Password
#else
					, ""
#endif
					, _User.m_UserName
					, "/dev/null"
					, _User.m_UserID
					, NSys::EUserManagementCreateUserFlag_None
				)
			;
		}
	}

	ch8 const *g_pMongoScript =
#		include "Mongo.sh"
	;

	TCFuture<void> CMongoManagerActor::fp_ExtractExeFS() const
	{
		auto BlockingActorCheckout = fg_BlockingActor();
		co_await
			(
				g_Dispatch(BlockingActorCheckout) /
				[
					UserName = mp_MongoUser.m_UserName
					, MongoVersion = mp_MongoVersion
					, MongoPort = mp_MongoConnectionSettings.f_GetSingleHost().m_Port
					, MongoReplicaName = mp_MongoReplicaName
				]
				{
					CExeFS ExeFS;
					if (!fg_OpenExeFS(ExeFS))
						DError("Failed to open ExeFS");

					CStr ProgramDirectory = CFile::fs_GetProgramDirectory();

					CFileSystemInterface_VirtualFS MalterlibFS(ExeFS.m_FileSystem);
					CFileSystemInterface_Disk DiskFS;

					MalterlibFS.f_CopyFilesWithAttribs("*", DiskFS, ProgramDirectory);

					CStr MongoScript = CStr::CFormat(g_pMongoScript) << UserName << MongoVersion << MongoPort << MongoReplicaName;
					CByteVector MongoScriptData;
					CFile::fs_WriteStringToVector(MongoScriptData, MongoScript, false);
					EFileAttrib Permissions
						= EFileAttrib_UnixAttributesValid
						| EFileAttrib_UserWrite
						| EFileAttrib_UserRead
						| EFileAttrib_UserExecute
						| EFileAttrib_GroupRead
						| EFileAttrib_GroupExecute
						| EFileAttrib_EveryoneRead
						| EFileAttrib_EveryoneExecute
					;
					CFile::fs_CopyFileDiff(MongoScriptData, ProgramDirectory / "mongo.sh", CTime::fs_NowUTC(), Permissions);
				}
			)
		;

		co_return {};
	}

	TCFuture<CVersion> CMongoManagerActor::fp_CheckVersion(CStr const &_Tool, CStr const &_Argument, CStr const &_ParseString, CVersion const &_NeededVersion)
	{
		TCPromise<CVersion> Promise;
		self(&CMongoManagerActor::fp_RunToolForVersionCheck, _Tool, fg_CreateVector<CStr>(_Argument)) > Promise % "Failed to check version" / [=](CStr &&_Data)
			{
				if (_Data.f_IsEmpty())
				{
					Promise.f_SetException(DErrorInstance(fg_Format("Failed get version with: {} {}", _Tool, _Argument)));
					return;
				}

				CVersion Version;
				aint nParsed = 0;
				(CStr::CParse(_ParseString) >> Version.m_Major >> Version.m_Minor >> Version.m_Revision).f_Parse(_Data, nParsed);

				if (nParsed != 3)
				{
					Promise.f_SetException(DErrorInstance(fg_Format("Failed to extract {} version from: {}", _Tool, _Data)));
					return;
				}

				if (Version < _NeededVersion)
				{
					Promise.f_SetException(DErrorInstance(fg_Format("{} version {} is less than the required version of {}", _Tool, Version, _NeededVersion)));
					return;
				}
				DLog(Info, "{} version {} found", _Tool, Version);
				Promise.f_SetResult(Version);
			}
		;
		return Promise.f_MoveFuture();
	}

	TCFuture<void> CMongoManagerActor::fp_DestroyApp_Mongo()
	{
		if (!mp_pMongoLaunch)
			co_return {};

		TCActorResultVector<void> Results;
		for (auto &Backup : mp_MongoBackupManagerActors)
		{
			if (!Backup)
				continue;

			Backup(&CBackupManagerActorInterface::f_MongoStopped) > Results.f_AddResult();
		}

		co_await Results.f_GetResults();

		if (!mp_pMongoLaunch)
			co_return{};

		co_await mp_pMongoLaunch.f_Destroy();

		co_return {};
	}

	CStr CMongoManagerActor::fp_GetDataPath(CStr const &_Path) const
	{
		return CFile::fs_AppendPath(CFile::fs_GetProgramDirectory(), _Path);
	}

	mint CMongoManagerActor::fs_GetMongoFileLimits()
	{
		return 64000;
	}

	mint CMongoManagerActor::fs_GetMongoThreadLimits()
	{
		return 32000;
	}
}
