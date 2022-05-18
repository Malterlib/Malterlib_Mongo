
#include "Malterlib_Mongo_App_MongoManager_Server.h"
#include <Mib/Concurrency/Actor/Timer>
#include <Mib/Encoding/JSONShortcuts>

#include <Mib/Cryptography/Certificate>

namespace NMib::NMongo::NMongoManager
{
	TCFuture<void> CMongoManagerActor::fp_SetupPrerequisites_Mongo()
	{
		TCPromise<void> Promise;
		CStr MongoDirectory = fp_GetDataPath("mongo");
		struct CMongoInfo
		{
			CUser m_User = {"", ""};
#ifdef DPlatformFamily_Windows
			CStrSecure m_Password;
#endif
			CStr m_AdminDN;
		};

		mp_CertificateDeployActor = fg_Construct(mp_AppState.m_DistributionManager, mp_AppState.m_TrustManager, mp_pFileActor);

		CStr CertificateAuthority = mp_AppState.m_ConfigDatabase.m_Data.f_GetMemberValue("CertificateAuthority", "MongoCA").f_String();
		auto MongoHost = mp_MongoConnectionSettings.f_GetSingleHost();

		{
			CMongoCertificateDeployActor::CUserSettings UserSettings;
			UserSettings.f_InitServer
				(
					CertificateAuthority
					, MongoHost.m_Host
					,
					{
						{
							.m_BasePath = MongoDirectory + "/certificates"
							, .m_FileUser = mp_MongoUser.m_UserName
							, .m_FileGroup = mp_MongoUser.m_GroupName
						}
					}
				)
			;

			UserSettings.m_fOnStatusChange = g_ActorFunctor / [](CHostInfo &&_HostInfo, CMongoCertificateDeployActor::CUserStatus &&_Status) -> TCFuture<void>
				{
					if (_Status.m_Severity == CMongoCertificateDeployActor::EStatusSeverity_Error)
						DMibLogWithCategory(Certificate, Error, "Server certificate: {}", _Status.m_Description);
					else if (_Status.m_Severity == CMongoCertificateDeployActor::EStatusSeverity_Warning)
						DMibLogWithCategory(Certificate, Warning, "Server certificate: {}", _Status.m_Description);
					else
						DMibLogWithCategory(Certificate, Info, "Server certificate: {}", _Status.m_Description);

					co_return {};
				}
			;

			UserSettings.m_fOnCertificateUpdated = g_ActorFunctor / [this]() -> TCFuture<void>
				{
					if (mp_bCertificateDeployActorStarted)
						DMibLogWithCategory(Certificate, Warning, "Server certificate updated, please schedule restart of MongoManager daemon");

					co_return {};
				}
			;

			mp_CertificateDeploySubscription_Server = co_await mp_CertificateDeployActor(&CMongoCertificateDeployActor::f_AddUser, fg_Move(UserSettings));
		}
		{
			CMongoCertificateDeployActor::CUserSettings UserSettings;
			UserSettings.f_InitUser
				(
					CertificateAuthority
					, "admin"
					, 
					{
						{
							.m_BasePath = MongoDirectory + "/certificates"
							, .m_FileUser = mp_MongoUser.m_UserName
							, .m_FileGroup = mp_MongoUser.m_GroupName
						}
					}
				)
			;

			UserSettings.m_fOnStatusChange = g_ActorFunctor / [](CHostInfo &&_HostInfo, CMongoCertificateDeployActor::CUserStatus &&_Status) -> TCFuture<void>
				{
					if (_Status.m_Severity == CMongoCertificateDeployActor::EStatusSeverity_Error)
						DMibLogWithCategory(Certificate, Error, "Admin certificate: {}", _Status.m_Description);
					else if (_Status.m_Severity == CMongoCertificateDeployActor::EStatusSeverity_Warning)
						DMibLogWithCategory(Certificate, Warning, "Admin certificate: {}", _Status.m_Description);
					else
						DMibLogWithCategory(Certificate, Info, "Admin certificate: {}", _Status.m_Description);

					co_return {};
				}
			;

			mp_CertificateDeploySubscription_Admin = co_await mp_CertificateDeployActor(&CMongoCertificateDeployActor::f_AddUser, fg_Move(UserSettings));
		}

		co_await mp_CertificateDeployActor(&CMongoCertificateDeployActor::f_Start);
		mp_bCertificateDeployActorStarted = true;

		auto Info = co_await
			(
				fg_Dispatch
				(
					mp_pFileActor
					,
					[
						MongoDirectory
						, MongoUser = mp_MongoUser
						, bNeedAdmin = mp_bEnableSSL || mp_Mode == EMode_SetupPermissions
						, ConnectionSettings = mp_MongoConnectionSettings
					]
					() mutable
					{
						DLog(Info, "Setting up mongod");

#ifdef DPlatformFamily_Windows
						CStrSecure Password;
						fsp_SetupUser(MongoUser, Password);
#else
						fsp_SetupUser(MongoUser);
#endif

						CFile::fs_CreateDirectory(MongoDirectory + "/db");
						CFile::fs_CreateDirectory(MongoDirectory + "/log");
						CFile::fs_CreateDirectory(MongoDirectory + "/.tmp");
						CFile::fs_SetOwnerAndGroupRecursive(MongoDirectory, MongoUser.m_UserName, MongoUser.m_GroupName);

						CMongoInfo MongoInfo;

						if (!ConnectionSettings.m_ClientCertificatePath.f_IsEmpty() && CFile::fs_FileExists(ConnectionSettings.m_ClientCertificatePath))
							MongoInfo.m_AdminDN = CCertificate::fs_GetCertificateDistinguishedName_RFC2253(CFile::fs_ReadFile(ConnectionSettings.m_ClientCertificatePath));
						else if (bNeedAdmin)
							DError(fg_Format("Could not find mongo admin user certificate at '{}'", ConnectionSettings.m_ClientCertificatePath));

						DLog(Info, "Setting up mongod was successful");

						MongoInfo.m_User = MongoUser;
#ifdef DPlatformFamily_Windows
						MongoInfo.m_Password = fg_Move(Password);
#endif
						return MongoInfo;
					}
				)
				% "Failed to set up mongod"
			)
		;

		mp_MongoUser = Info.m_User;

		if (!Info.m_AdminDN.f_IsEmpty())
			mp_MongoConnectionSettings.m_UserName = Info.m_AdminDN;

#ifdef DPlatformFamily_Windows
		if (!Info.m_Password.f_IsEmpty())
		{
			mp_AppState.m_StateDatabase.m_Data["Users"][mp_MongoUser.m_Name]["Password"] = _Info.m_Password;
			mp_AppState.f_SaveStateDatabase() > Promise;
			return;
		}
#endif
		co_return {};
	}
	
	CStr CMongoManagerActor::fp_GetMongoExecutable(CStr const &_ExecutableName) const
	{
#ifdef DMibMongo_UseInternalMongo
		return CFile::fs_GetProgramDirectory() / "mongo" / mp_MongoVersion / "bin" / _ExecutableName;
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
			, TCPromise<CStr> const &_Promise
			, CClock const &_Clock
			, CEJSON const &_Config
		)
	{
		CStr MongoPath = fp_GetDataPath("mongo");
	
		TCPromise<void> Promise;
		
		TCVector<CStr> Params = _MongoConnectionSettings.f_GetToolParams(true);
		auto &MongoHost = mp_MongoConnectionSettings.f_GetSingleHost();

		CEJSON Config = _Config;
		Config["replicaName"] = mp_MongoReplicaName;
		Config["mongoSelf"] = fg_Format("{}:{}", MongoHost.m_Host, MongoHost.m_Port);
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
		self
			(
			 	&CMongoManagerActor::fp_LaunchTool
				, fp_GetMongoExecutable("mongo")
				, CStr()
				, fg_Move(Params)
				, _LogCategory
				, mp_bVerboseMongoScripts ? ELogVerbosity_All : ELogVerbosity_None
				, false
				, MongoPath
				, mp_MongoUser.m_UserName
			 	, mp_MongoUser.m_GroupName
#ifdef DPlatformFamily_Windows
				, fp_GetUserPassword(mp_MongoUser.m_Name)
#endif
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
								DLog(Info, "Mongo script '{}' connection failed, retrying", _LogCategory);
								// Retry
								fg_OneshotTimer
									(
										1.0
										, [=]
										{
											fp_RunMongoScriptInternal(_MongoConnectionSettings, _Script, _LogCategory, _Database, _Timeout, _Promise, _Clock, _Config);
										}
										, self 
									)
								;
								return;
							}
						}
					}

					DLog(Error, "Mongo script '{}' failed: {}", _LogCategory, _StdOut.f_GetExceptionStr());
					_Promise.f_SetException(fg_Move(_StdOut));
					return;
				}
				
				DLog(Info, "Mongo script '{}' finished successfully", _LogCategory);
				_Promise.f_SetResult(fg_Move(*_StdOut));
			}
		;
	}
	
	TCFuture<CStr> CMongoManagerActor::fp_RunMongoScript
		(
			CMongoConnectionSettings const &_MongoConnectionSettings
			, CStr const &_Script
			, CStr const &_Database
			, fp32 _Timeout
			, CEJSON const &_Config
		)
	{
		TCPromise<CStr> Promise;

		auto &MongoHost = mp_MongoConnectionSettings.f_GetSingleHost();

		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
		
		if (MongoHost.m_Host.f_IsEmpty())
			return Promise <<= DErrorInstance(fg_Format("Failed to launch mongo for running {}: Hostname is empty", _Script));
		
		CClock Clock{true};
		
		fp_RunMongoScriptInternal
			(	
				_MongoConnectionSettings
				, fg_Format("{}/Source/Malterlib_Mongo_App_MongoManager_{}.js", ProgramDirectory, _Script)
				, _Script
				, _Database
				, _Timeout
				, Promise
				, Clock
				, _Config
			)
		;
		return Promise.f_MoveFuture();
	}

	TCFuture<void> CMongoManagerActor::fp_DetermineHostname()
	{
		mp_ResolveActor = fg_Construct();

		auto &MongoHost = mp_MongoConnectionSettings.f_GetSingleHost();

		auto Address = co_await mp_ResolveActor(&CResolveActor::f_Resolve, MongoHost.m_Host, NNetwork::ENetAddressType_TCPv4);

		if (Address.f_GetType() != NNetwork::ENetAddressType_TCPv4)
			co_return DMibErrorInstance("Hostname '{}' does not resolve to an IPV4 address"_f << MongoHost.m_Host);

		NNetwork::CNetAddressTCPv4 IPAddress;
		if (!Address.f_Get(IPAddress))
			co_return DMibErrorInstance("Hostname '{}' does not resolve to an valid IPV4 address"_f << MongoHost.m_Host);

		if (IPAddress.f_GetIP().m_IP[0] != 127)
			co_return DMibErrorInstance("Hostname '{}' does not resolve to a link local address. {} is not valid"_f << MongoHost.m_Host << Address);

		DLog(Info, "Hostname '{}' resolved to: {}", MongoHost.m_Host, Address);

		mp_MongoLocalAddress = Address;

		co_return {};
	}

	TCFuture<void> CMongoManagerActor::fp_StartMongo()
	{
		TCPromise<void> Promise;

		if (mp_pMongoLaunch)
			return Promise <<= g_Void; // Launch already in progress
		
		CStr MongoPath = fp_GetDataPath("mongo");
		CStr LogPath = MongoPath + "/log/mongo.log";
		CStr DatabasePath = MongoPath + "/db";
		auto &MongoHost = mp_MongoConnectionSettings.f_GetSingleHost();

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
				, CStr::fs_ToStr(MongoHost.m_Port)
				, "--storageEngine"
				, "wiredTiger"
			)
		;
		if (mp_bEnableSSL)
		{
			auto fSslToTls = [&](CStr const &_String, CStr const &_Alternate = {})
				{
					if (mp_Version_MongoDB >= CVersion(4, 4, 0))
					{
						if (_Alternate)
							return _Alternate;
						else
							return _String.f_Replace("ssl", "tls");
					}

					return _String;
				}
			;
			Arguments << fg_CreateVector<CStr>
				(
					fSslToTls("--sslMode")
					, fSslToTls("requireSSL", "requireTLS")
					, fSslToTls("--sslPEMKeyFile", "--tlsCertificateKeyFile")
					, fg_Format("{}/certificates/{}.pem", MongoPath, MongoHost.m_Host)
					, fSslToTls("--sslClusterFile")
					, fg_Format("{}/certificates/{}.pem", MongoPath, MongoHost.m_Host)
					, fSslToTls("--sslCAFile")
					, fg_Format("{}/certificates/MongoCA.crt", MongoPath)
#ifndef DMibMongo_SupportUnpatchedMongo
					, fSslToTls("--sslDisabledProtocols")
					, "TLS1_0,TLS1_1"
#endif
					, "--clusterAuthMode"
					, "x509"
#ifndef DMibMongo_SupportUnpatchedMongo // Need patched mongod (3.6)
					, "--setParameter"
					, "opensslCipherConfig=AES256+EECDH:AES256+EDH:!aNULL:!SHA:!SHA256:!SHA384:!DSS"
#endif
					, "--bind_ip_all"
				)
			;
		}
		else
		{
			// If not running SSL we just disable external access
			Arguments.f_Insert("--bind_ip");
			Arguments.f_Insert(mp_MongoLocalAddress.f_GetString());
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
		
#ifdef DPlatformFamily_Linux
		TCVector<CStr> LaunchArguments = {"--interleave=all"};
		LaunchArguments.f_Insert(fp_GetMongoExecutable("mongod"));
		LaunchArguments.f_Insert(Arguments);
		auto LaunchExecutable = "numactl";
#else
		auto LaunchArguments = Arguments;
		auto LaunchExecutable = fp_GetMongoExecutable("mongod");
#endif

		CProcessLaunchActor::CLaunch Launch = CProcessLaunchParams::fs_LaunchExecutable
			(
				LaunchExecutable
				, LaunchArguments
				, MongoPath
				, [this, Promise](CProcessLaunchStateChangeVariant const &_Change, fp64 _TimeSinceStart)
				{
					switch (_Change.f_GetTypeID())
					{
					case EProcessLaunchState_Launched:
						{
							if (mp_pCanDestroyTracker.f_IsEmpty() || mp_bStopped)
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
								Promise.f_SetException(DMibErrorInstance("Application is being destroyed"));
							}
							else
								Promise.f_SetResult();
						}
						break;
					case EProcessLaunchState_Exited:
						{
							if (!mp_pCanDestroyTracker.f_IsEmpty() && !mp_bStopped)
							{
								DLogWithCategory(mongod, Info, "Scheduling relaunch of mongod in 10 seconds");
								fg_Timeout(10.0) > [this]
									{
										if (!mp_pCanDestroyTracker.f_IsEmpty() && !mp_bStopped)
											self(&CMongoManagerActor::fp_StartMongo) > fg_DiscardResult();
									}
								;
							}
							mp_pMongoLaunch.f_Clear();
							mp_MongoLaunchSubscription.f_Clear();
						}
						break;
					case EProcessLaunchState_LaunchFailed:
						{
							Promise.f_SetException(DMibErrorInstance(fg_Format("Mongod launch failed: {}", _Change.f_Get<EProcessLaunchState_LaunchFailed>())));
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
		Params.m_bShowLaunched = false;
		Params.m_RunAsUser = mp_MongoUser.m_UserName;
#ifdef DPlatformFamily_Windows
		Params.m_RunAsUserPassword = fp_GetUserPassword(mp_MongoUser.m_Name);
#endif
		Params.m_RunAsGroup = mp_MongoUser.m_GroupName;
		{
			auto &Limit = Params.m_Limits[EProcessLimit_OpenedFiles];
			Limit.m_Value = fs_GetMongoFileLimits();
			Limit.m_MaxValue = fs_GetMongoFileLimits();
		}
		{
			auto &Limit = Params.m_Limits[EProcessLimit_Threads];
			Limit.m_Value = fs_GetMongoThreadLimits();
			Limit.m_MaxValue = fs_GetMongoThreadLimits();
		}
		
		fs_SetupEnvironment(Params);
		Params.m_bMergeEnvironment = true;
		Params.m_Environment["HOME"] = MongoPath;
		Params.m_Environment["TMPDIR"] = MongoPath + "/.tmp";
#ifdef DPlatformFamily_Windows
		Params.m_Environment["TMP"] = MongoPath + "/.tmp";
		Params.m_Environment["TEMP"] = MongoPath + "/.tmp";
#endif
		
		mp_pMongoLaunch = fg_ConstructActor<CProcessLaunchActor>();
		
		mp_pMongoLaunch(&CProcessLaunchActor::f_Launch, fg_Move(Launch), fg_ThisActor(this)) > [this, Promise](TCAsyncResult<CActorSubscription> &&_Subscription)
			{
				if (!_Subscription)
				{
					Promise.f_SetException(fg_Move(_Subscription));
					mp_pMongoLaunch.f_Clear();
					return;
				}
				mp_MongoLaunchSubscription = fg_Move(*_Subscription);
			}
		;
		
		return Promise.f_MoveFuture();
	}
	
	TCFuture<void> CMongoManagerActor::f_UpdateReplicationConfig()
	{
		TCPromise<void> Promise; 
		fp_RunMongoScript(mp_MongoConnectionSettings, "MongoUpdateReplicationConfig", "local", 60.0, {}) 
			> Promise / [Promise]
			{
				Promise.f_SetResult();
			}
		;
		return Promise.f_MoveFuture();
	}

	TCFuture<void> CMongoManagerActor::f_SetupPermissions()
	{
		TCPromise<void> Promise; 
		fp_RunMongoScript(mp_MongoConnectionSettings, "MongoSetupPermissions", "local", 60.0, {"mongoAdminDN"_= mp_MongoConnectionSettings.m_UserName})
			> Promise / [Promise]
			{
				Promise.f_SetResult();
			}
		;
		return Promise.f_MoveFuture();
	}
	
	TCFuture<void> CMongoManagerActor::f_JoinReplica(CJoinReplicaOptions const &_Options)
	{
		TCPromise<void> Promise;

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
		
		Results.f_GetResults() > Promise / [Promise, this, _Options]
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
				if (ConnectionSettings.m_Hosts == mp_MongoConnectionSettings.m_Hosts)
				{
					fp_RunMongoScript(mp_MongoConnectionSettings, "MongoInitReplicaSet", "local", 60.0, Config)
						> Promise / [Promise]
						{
							Promise.f_SetResult();
						}
					;
					return;
				}
				
				fp_RunMongoScript(ConnectionSettings, "MongoGetPrimary", "local", 60.0, {"quiet"_= true})
					> Promise / [this, Promise, Config](CStr &&_Primary)
					{
						CStr Primary = _Primary.f_Trim();
						auto ConnectionSettings = mp_MongoConnectionSettings.f_ForConnectionString(Primary);
						fp_RunMongoScript(ConnectionSettings, "MongoJoinReplicaSet", "local", 60.0, Config) 
							> Promise / [Promise]
							{
								Promise.f_SetResult();
							}
						;
					}
				;
			}
		;
		return Promise.f_MoveFuture();
	}
}
