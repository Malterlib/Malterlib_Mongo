// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Mongo/BSON>

#include "Malterlib_Mongo_App_MongoManagerDaemon.h"
#include "Malterlib_Mongo_App_MongoManager_Server.h"

namespace NMib::NMongo::NMongoManager
{
	void CMongoManagerDaemonActor::fp_BuildCommandLine(CDistributedAppCommandLineSpecification &o_CommandLine)
	{
		CDistributedAppActor::fp_BuildCommandLine(o_CommandLine);

		o_CommandLine.f_SetProgramDescription
			(
				"Malterlib Mongo Mongo Manager"
				, "Manages mongodb server daemon." 
			)
		;
		
		o_CommandLine.f_RegisterGlobalOptions
			(
				{
					"VerboseMongoScripts?"_=
					{
						"Names"_= {"--verbose-mongo-scripts"}
						, "Type"_= false
						, "Description"_= "Log verbose info from mongo scripts\n"
							"Defaults to false. Can also be specied in config file VerboseMongoScripts. Command line option overrides setting in config file."
					}
				}
			)
		;
		
		auto DefaultSection = o_CommandLine.f_GetDefaultSection();
		
		DefaultSection.f_RegisterDirectCommand
			(
				{
					"Names"_= {"--list-restore-range"}
					, "Description"_= 
					fg_Format
					(
						"Lists the time range available for restore for the oplog.\n"
						"Expects the oplog to be located at: {}\n"
						, CFile::fs_GetProgramDirectory() + "/Oplog.bson" 
					)
				}
				, [this](NEncoding::CEJSON const &_Params, CDistributedAppCommandLineClient &_CommandLineClient) -> uint32
				{
					return fp_CommandLine_ListRestoreRange(_Params);
				}
			)
		;
		DefaultSection.f_RegisterCommand
			(
				{
					"Names"_= {"--restore"}
					, "Description"_=
					fg_Format
					(
						"Restores the database from backup.\n"
						"Expects the dump to be located in directory: {}\n"
						"Expects the oplog to be located at: {}\n"
						"Should not be run when the daemon is started\n"
						, CFile::fs_GetProgramDirectory() + "/MongoDump"
						, CFile::fs_GetProgramDirectory() + "/Oplog.bson"
					)
					, "Parameters"_=
					{
						"RestoreTime?"_= 
						{
							"Type"_= COneOfType(CTime(), "")	
							, "Default"_= CTime()
							, "Description"_= "Specify the time to playback the oplog to."
						}
					}
				}
				, [this](NEncoding::CEJSON const &_Parameters, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
				{
					return g_Future <<= self(&CMongoManagerDaemonActor::fp_CommandLine_Restore, _Parameters, _pCommandLine);
				}
			 	, CDistributedAppCommandLineSpecification::ECommandFlag_RunLocalApp
			)
		;
		DefaultSection.f_RegisterCommand
			(
				{
					"Names"_= {"--setup-permissions"}
					, "Description"_= "Sets up permissions for a empty database by adding the admin user.\n"
				}
				, [this](NEncoding::CEJSON const &_Parameters, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
				{
					return g_Future <<= self(&CMongoManagerDaemonActor::fp_CommandLine_SetupPermissions, _Parameters, _pCommandLine);
				}
				, CDistributedAppCommandLineSpecification::ECommandFlag_RunLocalApp
			)
		;
		DefaultSection.f_RegisterCommand
			(
				{
					"Names"_= {"--update-replication-config"}
					, "Description"_=
					fg_Format
					(
						"Updates replication config.\n"
						"Use this in cases such as when hostname has changed and you need to update replication config to reflect this\n"
						"Should not be run when the daemon is started. Should not be run in a distributed replica.\n"
					)
				}
				, [this](NEncoding::CEJSON const &_Parameters, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
				{
					return g_Future <<= self(&CMongoManagerDaemonActor::fp_CommandLine_UpdateReplicationConfig, _Parameters, _pCommandLine);
				}
				, CDistributedAppCommandLineSpecification::ECommandFlag_RunLocalApp
			)
		;
		DefaultSection.f_RegisterCommand
			(
				{
					"Names"_= {"--run-backup"}
					, "Description"_= "Run a backup without running from an AppManager.\n"
					, "Output"_= "Backup ID.\n"
				}
				, [this](NEncoding::CEJSON const &_Parameters, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
				{
					return g_Future <<= self(&CMongoManagerDaemonActor::fp_CommandLine_RunBackup, _Parameters, _pCommandLine);
				}
				, CDistributedAppCommandLineSpecification::ECommandFlag_None
			)
		;
		DefaultSection.f_RegisterCommand
			(
				{
					"Names"_= {"--cancel-backups"}
					, "Description"_= "Run a backup without running from an AppManager.\n"
					, "Parameters"_=
					{
						"BackupIDs...?"_=
						{
							"Type"_= {1}
							, "Description"_= "The backup IDs to cancel. Specify none to cancel all backups"
						}
					}
				}
				, [this](NEncoding::CEJSON const &_Parameters, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
				{
					return g_Future <<= self(&CMongoManagerDaemonActor::fp_CommandLine_CancelBackups, _Parameters, _pCommandLine);
				}
				, CDistributedAppCommandLineSpecification::ECommandFlag_None
			)
		;
		
		DefaultSection.f_RegisterCommand
			(
				{
					"Names"_= {"--join-replica-set"}
					, "Description"_= "Joins mongo replica.\n"
					, "Options"_=
					{
						"MongoPort?"_= 
						{
							"Names"_= {"--port"}
							, "Type"_= 0	
							, "Description"_= "Specify the port to run the mongo server on. Will overwrite MongoManagerConfig.json with stripped comments."
						}
						, "MongoReplicaName?"_= 
						{
							"Names"_= {"--replica-name"}
							, "Type"_= ""	
							, "Description"_= "Specify the name of the replica to join. Will overwrite MongoManagerConfig.json with stripped comments."
						}
						, "CanVote?"_= 
						{
							"Names"_= {"--can-vote"}
							, "Type"_= true	
							, "Description"_= "Specify whether this mongo instance should have a vote in the election process."
						}
						, "Priority?"_= 
						{
							"Names"_= {"--priority"}
							, "Type"_= 1.0	
							, "Description"_= "Specify the priority this mongo should have in the election process."
						}
						, "ArbiterOnly?"_= 
						{
							"Names"_= {"--arbiter-only"}
							, "Type"_= false	
							, "Description"_= "Specify that the member should be a arbiter only."
						}
						, "BuildIndexes?"_= 
						{
							"Names"_= {"--build-indexes"}
							, "Type"_= true	
							, "Description"_= "Specify that the member should not build indexes. Useful for backup only hosts."
						}
						, "Hidden?"_= 
						{
							"Names"_= {"--hidden"}
							, "Type"_= false	
							, "Description"_= "Hide this member so clients does not use it for queries."
						}
						, "ExtraTags?"_= 
						{
							"Names"_= {"--extra-tags"}
							, "Type"_= {"*"_= ""}	
							, "Description"_= fg_Format("Specify extra tags to add to this member. The hostname '{}' will always be included as a tag.", NProcess::NPlatform::fg_Process_GetHostName())
						}
					}
					, "Parameters"_=
					{
						"ReplicaMember"_= 
						{
							"Type"_= ""	
							, "Description"_= "Specify a member of the replica to join.\n"
								"If you specify yourself here a new replica set will be created."
						}
					}
				}
				, [this](NEncoding::CEJSON const &_Parameters, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
				{
					return g_Future <<= self(&CMongoManagerDaemonActor::fp_CommandLine_JoinReplica, _Parameters, _pCommandLine);
				}
				, CDistributedAppCommandLineSpecification::ECommandFlag_RunLocalApp
			)
		;
	}

	uint32 CMongoManagerDaemonActor::fp_CommandLine_ListRestoreRange(NEncoding::CEJSON const &_Params)
	{
		CStr RestoreOplog = CFile::fs_GetProgramDirectory() + "/Oplog.bson";
		
		if (!CFile::fs_FileExists(RestoreOplog))
		{
			DConErrOut("No oplog found{\n}", 0);
			return 1;
		}

		CTime First;
		CTime Last;
		
		try
		{
			TCBinaryStreamFile<> Stream;
			
			Stream.f_Open(RestoreOplog, EFileOpen_Read | EFileOpen_ShareRead);
			
			while (!Stream.f_IsAtEndOfStream())
			{
				int32 EntrySize;
				Stream >> EntrySize;
				Stream.f_AddPosition(-int32(sizeof(int32)));
				
				CByteVector Data;
				Data.f_SetLen(EntrySize);
				Stream.f_ConsumeBytes(Data.f_GetArray(), EntrySize);
				
				auto OplogEntry = fg_FromBSON(bsoncxx::document::view{Data.f_GetArray(), Data.f_GetLen()});
				
				if (auto pTimestamp = OplogEntry.f_GetMember("ts", EEJSONType_UserType))
				{
					if (pTimestamp->f_UserType().m_Type == "Timestamp")
					{
						CTime Time = CTimeConvert::fs_FromUnixSeconds(pTimestamp->f_UserType().m_Value["Seconds"].f_Integer());
						if (!First.f_IsValid() || Time < First)
							First = Time;
						if (!Last.f_IsValid() || Time > First)
							Last = Time;
					}
				}
			}
		}
		catch (CException const &_Error)
		{
			DConErrOut("Warning: Exception reading oplog: {}{\n}", _Error.f_GetErrorStr());
		}
		
		DConOut("First oplog entry: {}{\n}", First.f_ToLocal());
		DConOut("Last oplog entry: {}{\n}", Last.f_ToLocal());
		
		return 0;
	}
	
	TCFuture<uint32> CMongoManagerDaemonActor::fp_CommandLine_Restore(NEncoding::CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
	{
		TCPromise<uint32> Promise;
		
		CTime Time;
		{
			auto &RestoreTime = _Params["RestoreTime"];
			if (RestoreTime.f_IsDate())
				Time = RestoreTime.f_Date();
		}
		
		mp_pManager(&CMongoManagerActor::f_RestoreMongo, Time) > Promise % "Error restoring from backup" / [=]()
			{
				*_pCommandLine += "Restore finished successfully\n";
				Promise.f_SetResult(0);
			}
		;
		return Promise.f_MoveFuture();
	}
	
	TCFuture<uint32> CMongoManagerDaemonActor::fp_CommandLine_UpdateReplicationConfig
		(
		 	NEncoding::CEJSON const &_Params
		 	, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine
		)
	{
		TCPromise<uint32> Promise;
 		mp_pManager(&CMongoManagerActor::f_UpdateReplicationConfig) > Promise / [=]
			{
				*_pCommandLine += "Replication config updated successfully\n";
				Promise.f_SetResult(0);
			}
		;
		return Promise.f_MoveFuture();
	}
	
	TCFuture<uint32> CMongoManagerDaemonActor::fp_CommandLine_SetupPermissions(NEncoding::CEJSON const &_Param, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
	{
		TCPromise<uint32> Promise;
 		mp_pManager(&CMongoManagerActor::f_SetupPermissions) > Promise / [=]
			{
				*_pCommandLine += "Permissions setup successfully\n";
				Promise.f_SetResult(0);
			}
		;
		return Promise.f_MoveFuture();
	}
	
	struct CDummyBackupInterface : public CDistributedAppInterfaceBackup
	{
		enum : uint32
		{
			EMinProtocolVersion = 0x101
			, EProtocolVersion = 0x101
		};

		CDummyBackupInterface(uint32 _BackupID)
			: m_BackupID{_BackupID}
		{
		}
		~CDummyBackupInterface() = default;

		NConcurrency::TCFuture<void> f_AppendManifest(NFile::CDirectoryManifestConfig const &_Config) override
		{
			CStr AppendData;
			AppendData += "\tRoot: {}\n"_f << _Config.m_Root;
			AppendData += "\tInclude wildcards: {vs}\n"_f << _Config.m_IncludeWildcards;
			AppendData += "\tExclude wildcards: {vs}\n"_f << _Config.m_ExcludeWildcards;
			AppendData += "\tAdd sync flags wildcards: {vs}\n"_f << _Config.m_AddSyncFlagsWildcards;
			AppendData += "\tRemove sync flags wildcards: {vs}\n"_f << _Config.m_RemoveSyncFlagsWildcards;
			
			DLogWithCategory(MongoManager/Backup, Info, "(LocalBackup {}) Append manifest:\n{}", m_BackupID, AppendData);
			co_return {};
		}
		
		NConcurrency::TCFuture<NConcurrency::TCActorSubscriptionWithID<>> f_SubscribeInitialFinished
			(
				NConcurrency::TCActorFunctorWithID<TCFuture<void> ()> &&_fOnInitialFinished
			) override
		{
			DLogWithCategory(MongoManager/Backup, Info, "(LocalBackup {}) Subscribe initial finished", m_BackupID);
			_fOnInitialFinished() > fg_DiscardResult();
			co_return {};
		}
		
		NConcurrency::TCFuture<NConcurrency::TCActorSubscriptionWithID<>> f_SubscribeBackupStopped
			(
				NConcurrency::TCActorFunctorWithID<TCFuture<void> ()> &&_fOnStopped
			) override
		{
			DLogWithCategory(MongoManager/Backup, Info, "(LocalBackup {}) Subscribe backup stopped", m_BackupID);
			co_return {};
		}
		
		uint32 m_BackupID = -1;
	};
	
	TCFuture<uint32> CMongoManagerDaemonActor::fp_CommandLine_RunBackup(NEncoding::CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
	{
		TCPromise<uint32> Promise;
		
		uint32 BackupID = mp_NextLocalBackup++;
		
		auto &LocalBackup = mp_LocalBackups[BackupID];
		
		LocalBackup.m_BackupInterface = mp_State.m_DistributionManager->f_ConstructActor<CDummyBackupInterface>(BackupID);
		
		fp_StartBackup
			(
				LocalBackup.m_BackupInterface->f_ShareInterface<CDistributedAppInterfaceBackup>().f_GetActor()
				, nullptr
				, CFile::fs_GetProgramDirectory()
			)
			> [=](TCAsyncResult<CActorSubscription> &&_Subscription)
			{
				if (!_Subscription)
				{
					mp_LocalBackups.f_Remove(BackupID);
					Promise.f_SetException(_Subscription);
					return;
				}
				
				auto *pLocalBackup = mp_LocalBackups.f_FindEqual(BackupID);
				
				if (!pLocalBackup)
					return Promise.f_SetException(DErrorInstance("Backup already cancelled"));
				
				auto &LocalBackup = *pLocalBackup;
				
				LocalBackup.m_Subscription = fg_Move(*_Subscription);
				
				*_pCommandLine += "{}\n"_f << BackupID;
				Promise.f_SetResult(0);
			}
		;
		
		return Promise.f_MoveFuture();
	}

	TCFuture<uint32> CMongoManagerDaemonActor::fp_CommandLine_CancelBackups(NEncoding::CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
	{
		TCPromise<uint32> Promise;
		
		TCSet<uint32> BackupIDs;
		if (auto pValue = _Params.f_GetMember("BackupIDs"))
		{
			for (auto &ID : pValue->f_Array())
				BackupIDs[ID.f_Integer()];
		}
		
		if (BackupIDs.f_IsEmpty())
			BackupIDs = mp_LocalBackups;
		
		TCSet<uint32> MissingBackupIDs;
		
		TCActorResultMap<uint32, void> DestroyResults;
		
		for (auto &ID : BackupIDs)
		{
			auto *pLocalBackup = mp_LocalBackups.f_FindEqual(ID);
			
			if (!pLocalBackup)
			{
				MissingBackupIDs[ID];
				continue;
			}
			
			auto &LocalBackup = *pLocalBackup;
			
			if (LocalBackup.m_Subscription)
				LocalBackup.m_Subscription->f_Destroy() > DestroyResults.f_AddResult(ID);
			
			mp_LocalBackups.f_Remove(pLocalBackup);
		}
		
		DestroyResults.f_GetResults() > Promise / [=](TCMap<uint32, TCAsyncResult<void>> &&_Results)
			{
				uint32 ExitStatus = 0;
				for (auto &Result : _Results)
				{
					uint32 ID = _Results.fs_GetKey(Result);
					
					if (!Result)
					{
						if (ExitStatus < 2)
							ExitStatus = 2;
						*_pCommandLine %= fg_Format("Error cancelling backup {}: {}\n", ID, Result.f_GetExceptionStr());
					}
				}
				
				if (!MissingBackupIDs.f_IsEmpty())
				{
					if (ExitStatus < 1)
						ExitStatus = 1;
					*_pCommandLine %= fg_Format("Non-existing or already cancelled backups: {vs}\n", MissingBackupIDs);
				}
				
				Promise.f_SetResult(ExitStatus);
			}
		;
	
		return Promise.f_MoveFuture();
	}
	
	TCFuture<uint32> CMongoManagerDaemonActor::fp_CommandLine_JoinReplica(NEncoding::CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
	{
		TCPromise<uint32> Promise;

		CJoinReplicaOptions Options;
		if (auto pValue = _Params.f_GetMember("MongoPort"))
		{
			int64 MongoPort = pValue->f_Integer();
			if (MongoPort <= 0 || MongoPort > 65535)
				return Promise <<= DMibErrorInstance(fg_Format("Invalid network port: {}", MongoPort));
			Options.m_Port = MongoPort;
		}
		if (auto pValue = _Params.f_GetMember("MongoReplicaName"))
		{
			if (pValue->f_String().f_IsEmpty())
				return Promise <<= DMibErrorInstance("Replica name cannot be empty");
			Options.m_ReplicaName = pValue->f_String();
		}
		if (auto pValue = _Params.f_GetMember("CanVote"))
			Options.m_CanVote = pValue->f_Boolean(); 
		if (auto pValue = _Params.f_GetMember("Priority"))
		{
			fp64 Priority = pValue->f_Integer();
			if (Priority < 0.0 || Priority > 1000.0)
				return Promise <<= DMibErrorInstance("Priority out of range");
			Options.m_Priority = Priority;
		}
		if (auto pValue = _Params.f_GetMember("ArbiterOnly"))
			Options.m_ArbiterOnly = pValue->f_Boolean(); 
		if (auto pValue = _Params.f_GetMember("BuildIndexes"))
			Options.m_BuildIndexes = pValue->f_Boolean(); 
		if (auto pValue = _Params.f_GetMember("Hidden"))
			Options.m_Hidden = pValue->f_Boolean(); 
		if (auto pValue = _Params.f_GetMember("ExtraTags"))
		{
			auto &ExtraTags = pValue->f_Object();
			TCMap<CStr, CStr> OutTags;
			for (auto &Tag : ExtraTags)
				OutTags[Tag.f_Name()] = Tag.f_Value().f_String();
			Options.m_ExtraTags = OutTags;
		}
		Options.m_MemberToJoin = _Params["ReplicaMember"].f_String();
		if (Options.m_MemberToJoin.f_IsEmpty())
			return Promise <<= DMibErrorInstance("You must specify replica memeber to join");
		
 		mp_pManager(&CMongoManagerActor::f_JoinReplica, Options) > Promise / [=]
			{
				*_pCommandLine %= "Replica set successfully joined\n";
				Promise.f_SetResult(0);
			}
		;
		return Promise.f_MoveFuture();
	}
}
