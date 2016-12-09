
#include "Malterlib_Mongo_App_MongoManager_Server.h"
#include <Mib/Concurrency/Actor/Timer>
#include <Mib/Encoding/JSONShortcuts>

#include <Mib/Network/SSL>

namespace NMib::NMongo::NMongoManager
{
	TCContinuation<void> CMongoManagerActor::fp_SetupPrerequisites_Mongo()
	{
		TCContinuation<void> Continuation;
		CStr MongoDirectory = fp_GetDataPath("mongo");
		struct CMongoInfo
		{
			CUser m_User = {""};
			CStr m_AdminDN;
		};
		fg_Dispatch
			(
				mp_pFileActor
				, 
				[
					MongoDirectory
					, MongoUser = mp_MongoUser
					, bNeedAdmin = mp_bEnableSSL || mp_Mode == EMode_SetupPermissions
					, ConnectionSettings = mp_MongoConnectionSettings
				]() mutable
				{
					DLog(Info, "Setting up mongod");
					
					fsp_SetupUser(MongoUser);
					
					CFile::fs_CreateDirectory(MongoDirectory + "/db");
					CFile::fs_CreateDirectory(MongoDirectory + "/log");
					CFile::fs_CreateDirectory(MongoDirectory + "/.tmp");
					CFile::fs_SetOwnerAndGroupRecursive(MongoDirectory, MongoUser.m_Name, MongoUser.m_Name);
					if (CFile::fs_FileExists(MongoDirectory + "/certificates"))
					{
						CFile::fs_SetUnixAttributesRecursive
							(
								MongoDirectory + "/certificates"
								, EFileAttrib_UserRead
								, EFileAttrib_UserRead | EFileAttrib_UserExecute
							)
						;
					}

					CMongoInfo MongoInfo;
					
					if (!ConnectionSettings.m_ClientCertificatePath.f_IsEmpty() && CFile::fs_FileExists(ConnectionSettings.m_ClientCertificatePath))
						MongoInfo.m_AdminDN = CSSLContext::fs_GetCertificateDistinguishedName_RFC2253(CFile::fs_ReadFile(ConnectionSettings.m_ClientCertificatePath));
					else if (bNeedAdmin)
						DError(fg_Format("Could not find mongo admin user certificate at '{}'", ConnectionSettings.m_ClientCertificatePath));
					
					DLog(Info, "Setting up mongod was successful");
					
					MongoInfo.m_User = MongoUser;
					return MongoInfo;
				}
			)
			> Continuation % "Failed to set up mongod" / [this, Continuation](CMongoInfo &&_Info)
			{
				mp_MongoUser = fg_Move(_Info.m_User);
				if (!_Info.m_AdminDN.f_IsEmpty())
					mp_MongoConnectionSettings.m_UserName = _Info.m_AdminDN; 

				Continuation.f_SetResult();
			}
		;
		return Continuation;
	}
	
	CStr CMongoManagerActor::fp_GetMongoExecutable(CStr const &_ExecutableName) const
	{
#ifdef DMibMongo_UseInternalMongo
		return CFile::fs_GetProgramDirectory() + "/mongo/bin/" + _ExecutableName;
#else
		return _ExecutableName;
#endif
	}
	
	void CMongoManagerActor::fp_RunMongoScriptInternal
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
	{
		CStr MongoPath = fp_GetDataPath("mongo");
	
		TCContinuation<void> Continuation;
		
		TCVector<CStr> Params = _MongoConnectionSettings.f_GetToolParams();
		
		CEJSON Config = _Config;
		Config["replicaName"] = mp_MongoReplicaName;
		Config["mongoSelf"] = fg_Format("{}:{}", NProcess::NPlatform::fg_Process_GetHostName(), mp_MongoConnectionSettings.m_Port);
		Config["verbose"] = mp_bVerboseMongoScripts;
		
		bool bQuiet = false;
		if (auto pValue = _Config.f_GetMember("quiet"))
			bQuiet = pValue->f_Boolean();
		
		if (bQuiet)
			Params.f_Insert("--quiet"); 
		
		Params << fg_CreateVector<CStr>
			(
				"--eval"
				, fg_Format("var scriptConfig = {};", Config.f_ToString(nullptr))
				, _Database
				, _Script
			)
		;
		
		DLog(Info, "Running mongo script '{}'", _LogCategory);
		fp_LaunchTool
			(
				fp_GetMongoExecutable("mongo")
				, {}
				, fg_Move(Params)
				, _LogCategory
				, mp_bVerboseMongoScripts ? ELogVerbosity_All : ELogVerbosity_Errors 
				, false
				, MongoPath
				, mp_MongoUser.m_Name
			)
			> [=](TCAsyncResult<CStr> &&_StdOut)
			{
				if (!_StdOut)
				{
					if (_Timeout != 0.0f)
					{
						CStr ErrorString = _StdOut.f_GetExceptionStr();
						if (ErrorString.f_Find("exception: connect failed") >= 0)
						{
							if (_Clock.f_GetTime() < _Timeout)
							{
								// Retry
								fg_OneshotTimer
									(
										0.1
										, [=]
										{
											fp_RunMongoScriptInternal(_MongoConnectionSettings, _Script, _LogCategory, _Database, _Timeout, _Continuation, _Clock, _Config);
										}
										, self 
									)
								;
								return;
							}
						}
					}
					DLog(Error, "Mongo script '{}' failed: {}", _LogCategory, _StdOut.f_GetExceptionStr());
					_Continuation.f_SetException(fg_Move(_StdOut));
					return;
				}
				
				DLog(Info, "Mongo script '{}' finished successfully", _LogCategory);
				_Continuation.f_SetResult(fg_Move(*_StdOut));
			}
		;
	}
	
	TCContinuation<CStr> CMongoManagerActor::fp_RunMongoScript
		(
			CMongoConnectionSettings const &_MongoConnectionSettings
			, CStr const &_Script
			, CStr const &_Database
			, fp32 _Timeout
			, CEJSON const &_Config
		)
	{
		CStr HostName = NProcess::NPlatform::fg_Process_GetHostName();
		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
		
		if (HostName.f_IsEmpty())
			return DErrorInstance(fg_Format("Failed to launch mongo for running {}: Hostname is empty", _Script));
		
		CClock Clock{true};
		
		TCContinuation<CStr> Continuation; 
		fp_RunMongoScriptInternal
			(	
				_MongoConnectionSettings
				, fg_Format("{}/Source/Malterlib_Mongo_App_MongoManager_{}.js", ProgramDirectory, _Script)
				, _Script
				, _Database
				, _Timeout
				, Continuation
				, Clock
				, _Config
			)
		;
		return Continuation;
	}

	TCContinuation<void> CMongoManagerActor::fp_StartMongo()
	{
		if (mp_pMongoLaunch)
			return fg_Explicit(); // Launch already in progress
		
		CStr MongoPath = fp_GetDataPath("mongo");
		CStr LogPath = MongoPath + "/log/mongo.log";
		CStr DatabasePath = MongoPath + "/db";

		TCVector<CStr> Arguments;
		if (mp_Mode != EMode_UpdateReplicationConfig && mp_Mode != EMode_SetupPermissions)
		{
			Arguments.f_Insert("--replSet");
			Arguments.f_Insert(mp_MongoReplicaName);
		}
		Arguments << fg_CreateVector<CStr>
			(
				"--dbpath"
				, DatabasePath
				, "--logpath"
				, LogPath
				, "--logappend"
				, "--logRotate"
				, "rename"
				, "--journal"
				, "--port"
				, CStr::fs_ToStr(mp_MongoConnectionSettings.m_Port)
				, "--storageEngine"
				, "wiredTiger"
			)
		;
		if (mp_bEnableSSL)
		{
			Arguments << fg_CreateVector<CStr>
				(
					"--sslMode"
					, "requireSSL"
					, "--sslPEMKeyFile"
					, fg_Format("{}/certificates/{}.pem", MongoPath, NProcess::NPlatform::fg_Process_GetHostName())
					, "--sslClusterFile"
					, fg_Format("{}/certificates/{}.pem", MongoPath, NProcess::NPlatform::fg_Process_GetHostName())
					, "--sslCAFile"
					, fg_Format("{}/certificates/MongoCA.crt", MongoPath)
					, "--sslDisabledProtocols"
					, "TLS1_0,TLS1_1"
					, "--clusterAuthMode"
					, "x509"
					, "--sslCipherConfig"
					, "AES256+EECDH:AES256+EDH:!aNULL:!SHA:!SHA256:!SHA384:!DSS"
				)
			;
		}
		else
		{
			// If not running SSL we just disable external access
			Arguments.f_Insert("--bind_ip");
			Arguments.f_Insert("127.0.0.1");
		}
		
#ifdef DPlatformFamily_OSX
		Arguments.f_Insert("--oplogSize");
		Arguments.f_Insert("25804");
#endif
		
#ifdef DDebug
// Enable this for profiling all mongo queries
//		Arguments.f_Insert("--profile=2");
//		Arguments.f_Insert("--slowms=0");
#endif

		{
			fp64 MaxCacheSize = fp64::fs_Inf();
			if (auto *pValue = mp_AppState.m_ConfigDatabase.m_Data.f_GetMember("MaxCacheSize", EJSONType_Float))
				MaxCacheSize = pValue->f_Float();  
			
			fp64 ReservedMemory = 2.0;
			if (auto *pValue = mp_AppState.m_ConfigDatabase.m_Data.f_GetMember("ReservedMemory", EJSONType_Float))
				ReservedMemory = pValue->f_Float();  

			fp64 ReservedMemoryPerCore = 0.0;
			if (auto *pValue = mp_AppState.m_ConfigDatabase.m_Data.f_GetMember("ReservedMemoryPerCore", EJSONType_Float))
				ReservedMemoryPerCore = pValue->f_Float();  

			fp64 MemoryAvailableGB = fp64(NProcess::NPlatform::fg_Process_GetPhysicalMemory()) / (1024.0*1024.0*1024.0);
			 
			fp64 CacheSize = fg_Max(fg_Min(MaxCacheSize, MemoryAvailableGB - (ReservedMemory + ReservedMemoryPerCore * NSys::fg_Thread_GetVirtualCores())), 1.0);
			uint64 CacheSizeInt = fg_Max(1, CacheSize.f_ToInt());
			Arguments.f_Insert("--wiredTigerCacheSizeGB");
			Arguments.f_Insert(CStr::fs_ToStr(CacheSizeInt));
		}
		
		TCContinuation<void> Continuation;
		
		CProcessLaunchActor::CLaunch Launch = CProcessLaunchParams::fs_LaunchExecutable
			(
				fp_GetMongoExecutable("mongod")
				, Arguments
				, MongoPath
				, [this, Continuation](CProcessLaunchStateChangeVariant const &_Change, fp64 _TimeSinceStart)
				{
					switch (_Change.f_GetTypeID())
					{
					case EProcessLaunchState_Launched:
						{
							if (mp_pCanDestroyTracker.f_IsEmpty())
							{
								fg_OneshotTimer
									(
										1.0 // Mongo is unreliable and doesn't listen to signals until after a while
										, [this]()
										{
											if (mp_pMongoLaunch)
												mp_pMongoLaunch(&CProcessLaunchActor::f_StopProcess) > fg_DiscardResult();
										}
									)
								;
								Continuation.f_SetException(DMibErrorInstance("Application is being destroyed"));
							}
							else
								Continuation.f_SetResult();
						}
						break;
					case EProcessLaunchState_Exited:
						{
							if (!mp_pCanDestroyTracker.f_IsEmpty())
							{
								DLogWithCategory(mongod, Info, "Scheduling relaunch of mongod in 10 seconds");
								fg_TimerActor()
									(
										&CTimerActor::f_OneshotTimer
										, 10.0
										, fg_ThisActor(this)
										, [this]()
										{
											if (!mp_pCanDestroyTracker.f_IsEmpty())
												fp_StartMongo();
										}
									)
									> fg_DiscardResult()
								;
							}
							mp_pMongoLaunch.f_Clear();
							mp_MongoLaunchSubscription.f_Clear();
						}
						break;
					case EProcessLaunchState_LaunchFailed:
						{
							Continuation.f_SetException(DMibErrorInstance(fg_Format("Mongod launch failed: {}", _Change.f_Get<EProcessLaunchState_LaunchFailed>())));
							mp_pMongoLaunch.f_Clear();
							mp_MongoLaunchSubscription.f_Clear();
						}
						break;
					}
				}
			)
		;
		
		Launch.m_ToLog = CProcessLaunchActor::ELogFlag_All;
		Launch.m_LogName = "mongod";
		Launch.m_Params.m_bCreateNewProcessGroup = true;
		
		auto &Params = Launch.m_Params;

		Params.m_bAllowExecutableLocate = true;
		Params.m_RunAsUser = mp_MongoUser.m_Name;
		Params.m_RunAsGroup = mp_MongoUser.m_Name;
		{
			auto &Limit = Params.m_Limits[EProcessLimit_OpenedFiles];
			Limit.m_Value = fp_GetMongoFileLimits();
			Limit.m_MaxValue = fp_GetMongoFileLimits();
		}
		{
			auto &Limit = Params.m_Limits[EProcessLimit_Threads];
			Limit.m_Value = fp_GetMongoThreadLimits();
			Limit.m_MaxValue = fp_GetMongoThreadLimits();
		}
		
		fs_SetupEnvironment(Params);
		Params.m_bMergeEnvironment = true;
		Params.m_Environment["HOME"] = MongoPath;
		Params.m_Environment["TMPDIR"] = MongoPath + "/.tmp";
		
		mp_pMongoLaunch = fg_ConstructActor<CProcessLaunchActor>();
		
		mp_pMongoLaunch(&CProcessLaunchActor::f_Launch, fg_Move(Launch), fg_ThisActor(this)) > [this, Continuation](TCAsyncResult<CActorSubscription> &&_Subscription)
			{
				if (!_Subscription)
				{
					Continuation.f_SetException(fg_Move(_Subscription));
					mp_pMongoLaunch.f_Clear();
					return;
				}
				mp_MongoLaunchSubscription = fg_Move(*_Subscription);
			}
		;
		
		return Continuation;
	}
	
	TCContinuation<void> CMongoManagerActor::f_UpdateReplicationConfig()
	{
		TCContinuation<void> Continuation; 
		fp_RunMongoScript(mp_MongoConnectionSettings, "MongoUpdateReplicationConfig", "local", 60.0, {}) 
			> Continuation / [Continuation]
			{
				Continuation.f_SetResult();
			}
		;
		return Continuation;
	}

	TCContinuation<void> CMongoManagerActor::f_SetupPermissions()
	{
		TCContinuation<void> Continuation; 
		fp_RunMongoScript(mp_MongoConnectionSettings, "MongoSetupPermissions", "$external", 60.0, {"mongoAdminDN"_= mp_MongoConnectionSettings.m_UserName})
			> Continuation / [Continuation]
			{
				Continuation.f_SetResult();
			}
		;
		return Continuation;
	}
	
	TCContinuation<void> CMongoManagerActor::f_JoinReplica(CJoinReplicaOptions const &_Options)
	{
		bool bConfigChanged = false;
		
		if (_Options.m_ReplicaName)
		{
			CStr const &ReplicaName = *_Options.m_ReplicaName;
			auto &ReplicaSetting = mp_AppState.m_ConfigDatabase.m_Data["ReplicaName"];
			if (!ReplicaSetting.f_IsString() || ReplicaSetting.f_String() != ReplicaName)
			{
				ReplicaSetting = ReplicaName;
				bConfigChanged = true;
			}
		}
		if (_Options.m_Port)
		{
			uint16 const &Port = *_Options.m_Port;
			auto &PortSetting = mp_AppState.m_ConfigDatabase.m_Data["MongoPort"];
			if (!PortSetting.f_IsInteger() || PortSetting.f_Integer() != Port)
			{
				PortSetting = Port;
				bConfigChanged = true;
			}
		}
		
		TCActorResultVector<void> Results;
		
		if (bConfigChanged)
			mp_AppState.m_ConfigDatabase.f_Save() > Results.f_AddResult();
		
		TCContinuation<void> Continuation;
		
		Results.f_GetResults() > Continuation / [Continuation, this, _Options]
			{
				CStr Self = mp_MongoConnectionSettings.f_GetConnectionString();
				CStr SelfTag = Self.f_ReplaceChar('.', '_').f_ReplaceChar(':', '_'); 
				
				CEJSON Config = {"selfTag"_= SelfTag};
				CEJSON &ReplicationConfig = Config["replicationConfig"] = 
					{
						"host"_= Self
						, "arbiterOnly"_= _Options.m_ArbiterOnly.f_Get(false) 
						, "buildIndexes"_= _Options.m_BuildIndexes.f_Get(true)
						, "hidden"_= _Options.m_Hidden.f_Get(false)
						, "priority"_= _Options.m_Priority.f_Get(1.0)
						, "tags"_=  
						{
							_[SelfTag] = "1" 
						}
						, "slaveDelay"_= 0
						, "votes"_= _Options.m_CanVote.f_Get(true) ? 1 : 0 
					}
				;
				
				if (_Options.m_ExtraTags)
				{
					CEJSON &Tags = ReplicationConfig["tags"]; 
					for (auto iTag = _Options.m_ExtraTags->f_GetIterator(); iTag; ++iTag)
						Tags[iTag.f_GetKey()] = *iTag;
				}
				
				auto ConnectionSettings = mp_MongoConnectionSettings.f_ForConnectionString(_Options.m_MemberToJoin);
				if (ConnectionSettings.m_Host == mp_MongoConnectionSettings.m_Host && ConnectionSettings.m_Port == mp_MongoConnectionSettings.m_Port)
				{
					fp_RunMongoScript(mp_MongoConnectionSettings, "MongoInitReplicaSet", "local", 60.0, Config) 
						> Continuation / [Continuation]
						{
							Continuation.f_SetResult();
						}
					;
					return;
				}
				
				fp_RunMongoScript(ConnectionSettings, "MongoGetPrimary", "local", 60.0, {"quiet"_= true})
					> Continuation / [this, Continuation, Config](CStr &&_Primary)
					{
						CStr Primary = _Primary.f_Trim();
						auto ConnectionSettings = mp_MongoConnectionSettings.f_ForConnectionString(Primary);
						fp_RunMongoScript(ConnectionSettings, "MongoJoinReplicaSet", "local", 60.0, Config) 
							> Continuation / [Continuation]
							{
								Continuation.f_SetResult();
							}
						;
					}
				;
			}
		;
		return Continuation;
	}
}
