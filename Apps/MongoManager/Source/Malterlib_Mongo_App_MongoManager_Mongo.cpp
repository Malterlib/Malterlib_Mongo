// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Mongo_App_MongoManager_Server.h"
#include <Mib/Concurrency/Actor/Timer>
#include <Mib/Encoding/JsonShortcuts>

#include <Mib/Cryptography/Certificate>

namespace NMib::NMongo::NMongoManager
{
	TCFuture<void> CMongoManagerActor::fp_SetupPrerequisites_Mongo()
	{
		CStr MongoDirectory = fp_GetDataPath("mongo");
		struct CMongoInfo
		{
			CUser m_User = {"", ""};
#ifdef DPlatformFamily_Windows
			CStrSecure m_Password;
#endif
			CStr m_AdminDN;
		};

		mp_CertificateDeployActor = fg_Construct(mp_AppState.m_DistributionManager, mp_AppState.m_TrustManager);

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

			UserSettings.m_fOnStatusChange = g_ActorFunctor / [](CHostInfo _HostInfo, CMongoCertificateDeployActor::CUserStatus _Status) -> TCFuture<void>
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

			UserSettings.m_fOnStatusChange = g_ActorFunctor / [](CHostInfo _HostInfo, CMongoCertificateDeployActor::CUserStatus _Status) -> TCFuture<void>
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

		CMongoInfo Info;
		{
			auto BlockingActorCheckout = fg_BlockingActor();
			Info = co_await
				(
					g_Dispatch(BlockingActorCheckout) /
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
					% "Failed to set up mongod"
				)
			;
		}

		mp_MongoUser = Info.m_User;

		if (!Info.m_AdminDN.f_IsEmpty())
			mp_MongoConnectionSettings.m_UserName = Info.m_AdminDN;

#ifdef DPlatformFamily_Windows
		if (!Info.m_Password.f_IsEmpty())
		{
			mp_AppState.m_StateDatabase.m_Data["Users"][mp_MongoUser.m_Name]["Password"] = _Info.m_Password;
			co_await mp_AppState.f_SaveStateDatabase();
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
		if (mp_pMongoLaunch)
			co_return {}; // Launch already in progress

		CStr MongoPath = fp_GetDataPath("mongo");
		CStr LogPath = MongoPath + "/log/mongo.log";
		CStr DatabasePath = MongoPath + "/db";
		auto &MongoHost = mp_MongoConnectionSettings.f_GetSingleHost();

		TCVector<CStr> Arguments;

		auto fAddValue = [&](CStr const &_Setting, CStr const &_Value)
			{
				Arguments.f_Insert(_Setting);
				Arguments.f_Insert(_Value);
			}
		;

		auto fAddFlag = [&](CStr const &_Flag)
			{
				Arguments.f_Insert(_Flag);
			}
		;

		if (fp_ShouldUseReplica())
			fAddValue("--replSet", mp_MongoReplicaName);

		fAddValue("--dbpath", DatabasePath);
		fAddValue("--logpath", LogPath);
		fAddFlag("--logappend");
		fAddValue("--logRotate", "rename");
		fAddValue("--port", CStr::fs_ToStr(MongoHost.m_Port));
		fAddValue("--storageEngine", "wiredTiger");

		if (mp_bEnableSSL)
		{
			CStr CertificateFile = MongoPath / ("certificates/{}.pem"_f << MongoHost.m_Host);
			fAddValue("--tlsMode", "requireTLS");
			fAddValue("--tlsCertificateKeyFile", CertificateFile);
			fAddValue("--tlsClusterFile", CertificateFile);
			fAddValue("--tlsCAFile", MongoPath / "certificates/MongoCA.crt");
			fAddValue("--tlsDisabledProtocols", "TLS1_0,TLS1_1");
			fAddValue("--clusterAuthMode", "x509");
			fAddValue("--setParameter", "opensslCipherConfig=AES256+EECDH:AES256+EDH:!aNULL:!SHA:!SHA256:!SHA384:!DSS");
			fAddValue("--setParameter", "opensslCipherSuiteConfig=TLS_AES_256_GCM_SHA384");
			fAddValue("--setParameter", "ocspEnabled=false");
			fAddFlag("--bind_ip_all");
		}
		else
			fAddValue("--bind_ip", mp_MongoLocalAddress.f_GetString(ENetAddressStringFlag::ENetAddressStringFlag_None)); // If not running SSL we just disable external access

#ifdef DPlatformFamily_macOS
		fAddValue("--oplogSize", "25804");
#endif

#ifdef DDebug
// Enable this for profiling all mongo queries
//		Arguments.f_Insert("--profile=2");
//		Arguments.f_Insert("--slowms=0");
#endif
		{
			fp64 MaxCacheSize = fp64::fs_Inf();
			if (auto *pValue = mp_AppState.m_ConfigDatabase.m_Data.f_GetMember("MaxCacheSize", EJsonType_Float))
				MaxCacheSize = pValue->f_Float();

			fp64 ReservedMemory = 2.0;
			if (auto *pValue = mp_AppState.m_ConfigDatabase.m_Data.f_GetMember("ReservedMemory", EJsonType_Float))
				ReservedMemory = pValue->f_Float();

			fp64 ReservedMemoryPerCore = 0.0;
			if (auto *pValue = mp_AppState.m_ConfigDatabase.m_Data.f_GetMember("ReservedMemoryPerCore", EJsonType_Float))
				ReservedMemoryPerCore = pValue->f_Float();

			fp64 MemoryAvailableGB = fp64(NProcess::NPlatform::fg_Process_GetPhysicalMemory()) / (1024.0*1024.0*1024.0);

			fp64 CacheSize = fg_Max(fg_Min(MaxCacheSize, MemoryAvailableGB - (ReservedMemory + ReservedMemoryPerCore * NSys::fg_Thread_GetVirtualCores())), 1.0);
			uint64 CacheSizeInt = fg_Max(1, CacheSize.f_ToInt());

			fAddValue("--wiredTigerCacheSizeGB", CStr::fs_ToStr(CacheSizeInt));
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

		TCPromiseFuturePair<void> Promise;

		CProcessLaunchActor::CLaunch Launch = CProcessLaunchParams::fs_LaunchExecutable
			(
				LaunchExecutable
				, LaunchArguments
				, MongoPath
				, [this, Promise = fg_Move(Promise.m_Promise)](CProcessLaunchStateChangeVariant const &_Change, fp64 _TimeSinceStart)
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
										, [this]() -> TCFuture<void>
										{
											if (mp_pMongoLaunch)
												mp_pMongoLaunch(&CProcessLaunchActor::f_StopProcess).f_DiscardResult();

											co_return {};
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
								fg_Timeout(10.0) > [this]() -> TCFuture<void>
									{
										if (!mp_pCanDestroyTracker.f_IsEmpty() && !mp_bStopped)
											fp_StartMongo().f_DiscardResult();

										co_return {};
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

		auto Subscription = co_await mp_pMongoLaunch(&CProcessLaunchActor::f_Launch, fg_Move(Launch), fg_ThisActor(this)).f_Wrap();
		if (!Subscription)
		{
			mp_pMongoLaunch.f_Clear();
			co_return fg_Move(Subscription).f_GetException();
		}
		mp_MongoLaunchSubscription = fg_Move(*Subscription);

		co_await fg_Move(Promise.m_Future);

		co_return {};
	}

	CMongoConnectionSettings CMongoManagerActor::fp_LocalConnectionSettings()
	{
		auto Return = mp_MongoConnectionSettings;
		Return.m_bDirectConnection = true;

		return Return;
	}

	TCFuture<void> CMongoManagerActor::f_UpdateReplicationConfig()
	{
		co_await fp_Mongo_UpdateReplicationConfig(fp_LocalConnectionSettings());

		co_return {};
	}

	TCFuture<void> CMongoManagerActor::f_SetupPermissions()
	{
		co_await fp_Mongo_SetupPermissions(fp_LocalConnectionSettings(), mp_MongoConnectionSettings.m_UserName);

		co_return {};
	}

	TCFuture<void> CMongoManagerActor::f_JoinReplica(CJoinReplicaOptions _Options)
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

		TCFutureVector<void> Results;

		if (bConfigChanged)
			mp_AppState.m_ConfigDatabase.f_Save() > Results;

		co_await fg_AllDoneWrapped(Results);

		CStr Self = mp_MongoConnectionSettings.f_GetConnectionString();
		CStr SelfTag = Self.f_ReplaceChar('.', '_').f_ReplaceChar(':', '_');

		CEJsonOrdered Config = {"selfTag"_o= SelfTag};
		CEJsonOrdered &ReplicationConfig = Config["replicationConfig"] = _o=
			{
				"host"_o= Self
				, "arbiterOnly"_o= _Options.m_ArbiterOnly.f_Get(false)
				, "buildIndexes"_o= _Options.m_BuildIndexes.f_Get(true)
				, "hidden"_o= _Options.m_Hidden.f_Get(false)
				, "priority"_o= _Options.m_Priority.f_Get(1.0)
				, "tags"_o=
				{
					_o(SelfTag) = "1"
				}
				, "secondaryDelaySecs"_o= 0
				, "votes"_o= _Options.m_CanVote.f_Get(true) ? 1 : 0
			}
		;

		if (_Options.m_ExtraTags)
		{
			CEJsonOrdered &Tags = ReplicationConfig["tags"];
			for (auto iTag = _Options.m_ExtraTags->f_GetIterator(); iTag; ++iTag)
				Tags[iTag.f_GetKey()] = *iTag;
		}

		auto JoinConnectionSettings = mp_MongoConnectionSettings.f_ForConnectionString(_Options.m_MemberToJoin);
		JoinConnectionSettings.m_bDirectConnection = true;

		if (JoinConnectionSettings.m_Hosts == mp_MongoConnectionSettings.m_Hosts)
		{
			co_await fp_Mongo_InitReplicaSet(fp_LocalConnectionSettings(), ReplicationConfig, SelfTag);

			co_return {};
		}

		DMibLog(Info, "Get Primary");

		auto Primary = co_await fp_Mongo_GetPrimary(JoinConnectionSettings);

		DMibLog(Info, "Primary: {}", Primary);

		auto ConnectionSettings = mp_MongoConnectionSettings.f_ForConnectionString(Primary);
		ConnectionSettings.m_bDirectConnection = true;

		DMibLog(Info, "Join connection setings: {}", ConnectionSettings.f_GetUrl("").f_Encode());
		DMibLog(Info, "Local connection settings: {}", fp_LocalConnectionSettings().f_GetUrl("").f_Encode());

		co_await fp_Mongo_JoinReplicaSet(ConnectionSettings, fp_LocalConnectionSettings(), ReplicationConfig, SelfTag);

		DMibLog(Info, "Join finished");

		co_return {};
	}
}
