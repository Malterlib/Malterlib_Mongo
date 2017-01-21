// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_App_MongoManager_Server.h"

#include <Mib/File/ExeFS>
#include <Mib/File/VirtualFS>
#include <Mib/File/VirtualFSs/MalterlibFS>
#include <Mib/Encoding/JSONShortcuts>

namespace NMib::NMongo::NMongoManager
{
	CMongoManagerActor::CMongoManagerActor(CDistributedAppState const &_AppState)
		: mp_AppState(_AppState)
		, mp_pCanDestroyTracker(fg_Construct())
	{
#ifdef DPlatformFamily_OSX
		CStr Path = fg_GetSys()->f_GetEnvironmentVariable("PATH");
		if (Path.f_Find("/opt/local/bin") < 0)
			fg_GetSys()->f_SetEnvironmentVariable("PATH", "/opt/local/bin:" + Path);
#endif
	}
	
	CMongoManagerActor::~CMongoManagerActor()
	{
	}

	TCContinuation<void> CMongoManagerActor::f_Startup(EMode _Mode, CStr const &_OverrideReplicaName, uint16 _Port, TCOptional<bool> const &_VerboseMongoScrips)
	{
		mp_Mode = _Mode;
		
		auto &Config = mp_AppState.m_ConfigDatabase.m_Data;
		
		if (_VerboseMongoScrips)
			mp_bVerboseMongoScripts = *_VerboseMongoScrips;
		else if (auto *pValue = Config.f_GetMember("VerboseMongoScripts", EJSONType_Boolean))
			mp_bVerboseMongoScripts = pValue->f_Boolean();
		
		if (_Port)
			mp_MongoConnectionSettings.m_Port = _Port;
		else if (auto *pValue = Config.f_GetMember("MongoPort", EJSONType_Integer))
			mp_MongoConnectionSettings.m_Port = pValue->f_Integer();
		
		if (!_OverrideReplicaName.f_IsEmpty())
			mp_MongoReplicaName = _OverrideReplicaName;
		else if (auto pValue = mp_AppState.m_ConfigDatabase.m_Data.f_GetMember("ReplicaName", EJSONType_String))
			mp_MongoReplicaName = pValue->f_String();
		
		if (mp_Mode == EMode_SetupPermissions || mp_Mode == EMode_RunRestore)
			mp_bEnableSSL = false;
		else if (auto pValue = mp_AppState.m_ConfigDatabase.m_Data.f_GetMember("EnableSSL", EJSONType_Boolean))
			mp_bEnableSSL = pValue->f_Boolean();
		
		CStr MongoDirectory = fp_GetDataPath("mongo");
		mp_MongoConnectionSettings.m_Host = NProcess::NPlatform::fg_Process_GetHostName();
		mp_MongoConnectionSettings.m_CACertificatePath = MongoDirectory + "/certificates/MongoCA.crt";
		mp_MongoConnectionSettings.m_ClientCertificatePath = MongoDirectory + "/certificates/admin.pem";
		mp_MongoConnectionSettings.m_bEnableSSL = mp_bEnableSSL;

		mp_pFileActor = fg_ConstructActor<CSeparateThreadActor>(fg_Construct("File actor"));

		DLog(Info, "Extracting ExeFS");
		
		TCContinuation<void> Continuation;

		fp_CleanupOldProcesses() > Continuation % "Failed to clean up old processes" / [this, Continuation]
			{
				fp_ExtractExeFS() > Continuation % "Failed to extract ExeFS" / [this, Continuation]
					{
						DLog(Info, "Done extracting ExeFS");
						fp_CheckVersion(fp_GetMongoExecutable("mongod"), "--version", "db version v{}.{}.{}\n", mp_Version_MongoDB) 
							+ fp_SetupPrerequisites_Mongo()
							> Continuation / [this, Continuation]
							{
								fp_StartMongo() > Continuation / [this, Continuation]
									{
										CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
										if (mp_Mode == EMode_UpdateReplicationConfig || mp_Mode == EMode_SetupPermissions)
											Continuation.f_SetResult();
										else
										{
											fp_RunMongoScript(mp_MongoConnectionSettings, "MongoWaitForPrimary", "local", 5.0*60.0, {"expectReplica"_= mp_Mode != EMode_JoinReplicaSet}) 
												> Continuation / [Continuation, this]
												{
													Continuation.f_SetResult();
													if (mp_Mode == EMode_Normal)
														fp_StartMongoBackup();
												}
											;
										}
									}
								;
							}
						;
					}
				;
			}
		;
		
		return Continuation;
	}
	
	TCContinuation<void> CMongoManagerActor::f_Destroy()
	{
		auto pCanDestroy = fg_Move(mp_pCanDestroyTracker);
		
		TCActorResultVector<void> Destroys;
		for (auto &ToolLaunch : mp_ToolLaunches)
			ToolLaunch.m_ProcessLaunch->f_Destroy2() > Destroys.f_AddResult();
		
		Destroys.f_GetResults()
			> [this, pCanDestroy](auto &&_Results)
			{
				TCActorResultVector<void> Destroys;
				if (mp_pMongoBackupManagerActor)
					mp_pMongoBackupManagerActor->f_Destroy2() > Destroys.f_AddResult();
				Destroys.f_GetResults() > [this, pCanDestroy](auto &&_Results)
					{
						fp_DestroyApp_Mongo() > [this, pCanDestroy](auto &&)
							{
							}
						;
					}
				;
			}
		;
		
		return pCanDestroy->m_Continuation;
	}
	
	void CMongoManagerActor::fsp_SetupUser(CUser &_User)
	{
		if (!NSys::fg_UserManagement_GroupExists(_User.m_Name, _User.m_GroupID))
			NSys::fg_UserManagement_CreateGroup(_User.m_Name, _User.m_GroupID);

		if (!NSys::fg_UserManagement_UserExists(_User.m_Name, _User.m_UserID))
		{
			NSys::fg_UserManagement_CreateUser
				(
					_User.m_Name
					, _User.m_Name
					, ""
					, _User.m_Name
					, "/dev/null"
					, _User.m_UserID
				)
			;
		}
	}
	
	TCContinuation<void> CMongoManagerActor::fp_ExtractExeFS() const
	{
		return fg_Dispatch
			(
				mp_pFileActor
				, []
				{
					CExeFS ExeFS;
					if (!fg_OpenExeFS(ExeFS))
						DError("Failed to open ExeFS");
					
					CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
					
					CFileSystemInterface_VirtualFS MalterlibFS(ExeFS.m_FileSystem);
					CFileSystemInterface_Disk DiskFS;
					
					MalterlibFS.f_CopyFilesWithAttribs("*", DiskFS, ProgramDirectory);
				}
			)
		;
	}
	
	TCContinuation<void> CMongoManagerActor::fp_CheckVersion(CStr const &_Tool, CStr const &_Argument, CStr const &_ParseString, CVersion const &_NeededVersion)
	{
		TCContinuation<void> Continuation;
		fp_RunToolForVersionCheck(_Tool, fg_CreateVector<CStr>(_Argument)) > Continuation % "Failed to check version" / [=](CStr &&_Data)
			{
				if (_Data.f_IsEmpty())
				{
					Continuation.f_SetException(DErrorInstance(fg_Format("Failed get version with: {} {}", _Tool, _Argument)));
					return;
				}
				
				CVersion Version;
				aint nParsed = 0;
				(CStr::CParse(_ParseString) >> Version.m_Major >> Version.m_Minor >> Version.m_Revision).f_Parse(_Data, nParsed);
				
				if (nParsed != 3)
				{
					Continuation.f_SetException(DErrorInstance(fg_Format("Failed to extract {} version from: {}", _Tool, _Data)));
					return;
				}
				
				if (Version < _NeededVersion)
				{
					Continuation.f_SetException(DErrorInstance(fg_Format("{} version {} is less than the required version of {}", _Tool, Version, _NeededVersion)));
					return;
				}
				DLog(Info, "{} version {} found", _Tool, Version);
				Continuation.f_SetResult();
			}
		;
		return Continuation;
	}
	
	TCContinuation<void> CMongoManagerActor::fp_DestroyApp_Mongo()
	{
		if (!mp_pMongoLaunch)
			return fg_Explicit();
		return mp_pMongoLaunch->f_Destroy2(); 
	}
	
	CStr CMongoManagerActor::fp_GetDataPath(CStr const &_Path) const
	{
		return CFile::fs_AppendPath(CFile::fs_GetProgramDirectory(), _Path);
	}

	mint CMongoManagerActor::fp_GetMongoFileLimits() const
	{
#ifdef DPlatformFamily_OSX
		return 10240;
#else
		return 64000;
#endif
	}

	mint CMongoManagerActor::fp_GetMongoThreadLimits() const
	{
		return 32000;
	}
}
