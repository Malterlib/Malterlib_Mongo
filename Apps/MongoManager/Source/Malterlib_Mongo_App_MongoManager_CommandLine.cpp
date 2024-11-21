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
				"Malterlib Mongo Manager"
				, "Manages mongodb server daemon." 
			)
		;
		
		o_CommandLine.f_RegisterGlobalOptions
			(
				{
					"VerboseMongoScripts?"_o=
					{
						"Names"_o= {"--verbose-mongo-scripts"}
						, "Type"_o= false
						, "Description"_o= "Log verbose info from mongo scripts\n"
							"Defaults to false. Can also be specied in config file VerboseMongoScripts. Command line option overrides setting in config file."
					}
				}
			)
		;
		
		auto Section = o_CommandLine.f_AddSection("Mongo Manager", "Mongo Manager Commands");
		
		Section.f_RegisterDirectCommand
			(
				{
					"Names"_o= {"--list-restore-range"}
					, "Description"_o=
					fg_Format
					(
						"Lists the time range available for restore for the oplog.\n"
						"Expects the oplog to be located at: {}\n"
						, CFile::fs_GetProgramDirectory() + "/Oplog.bson" 
					)
				}
				, [this](NEncoding::CEJSONSorted const &_Params, CDistributedAppCommandLineClient &_CommandLineClient) -> uint32
				{
					return fp_CommandLine_ListRestoreRange(_Params);
				}
			)
		;
		Section.f_RegisterCommand
			(
				{
					"Names"_o= {"--restore"}
					, "Description"_o=
					fg_Format
					(
						"Restores the database from backup.\n"
						"Expects the dump to be located in directory: {}\n"
						"Expects the oplog to be located at: {}\n"
						"Should not be run when the daemon is started\n"
						, CFile::fs_GetProgramDirectory() + "/MongoDump"
						, CFile::fs_GetProgramDirectory() + "/Oplog.bson"
					)
					, "Parameters"_o=
					{
						"RestoreTime?"_o=
						{
							"Type"_o= COneOfType(CTime(), "")
							, "Default"_o= CTime()
							, "Description"_o= "Specify the time to playback the oplog to."
						}
					}
				}
				, [this](NEncoding::CEJSONSorted _Parameters, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
				{
					co_return co_await fp_CommandLine_Restore(fg_Move(_Parameters), fg_Move(_pCommandLine));
				}
				, EDistributedAppCommandFlag_RunLocalApp
			)
		;
		Section.f_RegisterCommand
			(
				{
					"Names"_o= {"--setup-permissions"}
					, "Description"_o= "Sets up permissions for a empty database by adding the admin user.\n"
					, "Options"_o=
					{
						"MongoPort?"_o=
						{
							"Names"_o= {"--port"}
							, "Type"_o= 0
							, "Description"_o= "Specify the port to run the mongo server on. Will overwrite MongoManagerConfig.json with stripped comments."
						}
					}
				}
				, [this](NEncoding::CEJSONSorted _Parameters, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
				{
					co_return co_await fp_CommandLine_SetupPermissions(fg_Move(_Parameters), fg_Move(_pCommandLine));
				}
				, EDistributedAppCommandFlag_RunLocalApp
			)
		;
		Section.f_RegisterCommand
			(
				{
					"Names"_o= {"--update-replication-config"}
					, "Description"_o=
					fg_Format
					(
						"Updates replication config.\n"
						"Use this in cases such as when hostname has changed and you need to update replication config to reflect this\n"
						"Should not be run when the daemon is started. Should not be run in a distributed replica.\n"
					)
				}
				, [this](NEncoding::CEJSONSorted _Parameters, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
				{
					co_return co_await fp_CommandLine_UpdateReplicationConfig(fg_Move(_Parameters), fg_Move(_pCommandLine));
				}
				, EDistributedAppCommandFlag_RunLocalApp
			)
		;
		Section.f_RegisterCommand
			(
				{
					"Names"_o= {"--run-backup"}
					, "Description"_o= "Run a backup without running from an AppManager.\n"
					, "Output"_o= "Backup ID.\n"
				}
				, [this](NEncoding::CEJSONSorted _Parameters, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
				{
					co_return co_await fp_CommandLine_RunBackup(fg_Move(_Parameters), fg_Move(_pCommandLine));
				}
				, EDistributedAppCommandFlag_None
			)
		;
		Section.f_RegisterCommand
			(
				{
					"Names"_o= {"--cancel-backups"}
					, "Description"_o= "Run a backup without running from an AppManager.\n"
					, "Parameters"_o=
					{
						"BackupIDs...?"_o=
						{
							"Type"_o= {1}
							, "Description"_o= "The backup IDs to cancel. Specify none to cancel all backups"
						}
					}
				}
				, [this](NEncoding::CEJSONSorted _Parameters, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
				{
					co_return co_await fp_CommandLine_CancelBackups(fg_Move(_Parameters), fg_Move(_pCommandLine));
				}
				, EDistributedAppCommandFlag_None
			)
		;
		
		Section.f_RegisterCommand
			(
				{
					"Names"_o= {"--join-replica-set"}
					, "Description"_o= "Joins mongo replica.\n"
					, "Options"_o=
					{
						"MongoPort?"_o=
						{
							"Names"_o= {"--port"}
							, "Type"_o= 0
							, "Description"_o= "Specify the port to run the mongo server on. Will overwrite MongoManagerConfig.json with stripped comments."
						}
						, "MongoReplicaName?"_o=
						{
							"Names"_o= {"--replica-name"}
							, "Type"_o= ""
							, "Description"_o= "Specify the name of the replica to join. Will overwrite MongoManagerConfig.json with stripped comments."
						}
						, "CanVote?"_o=
						{
							"Names"_o= {"--can-vote"}
							, "Type"_o= true
							, "Description"_o= "Specify whether this mongo instance should have a vote in the election process."
						}
						, "Priority?"_o=
						{
							"Names"_o= {"--priority"}
							, "Type"_o= 1.0
							, "Description"_o= "Specify the priority this mongo should have in the election process."
						}
						, "ArbiterOnly?"_o=
						{
							"Names"_o= {"--arbiter-only"}
							, "Type"_o= false
							, "Description"_o= "Specify that the member should be a arbiter only."
						}
						, "BuildIndexes?"_o=
						{
							"Names"_o= {"--build-indexes"}
							, "Type"_o= true
							, "Description"_o= "Specify that the member should not build indexes. Useful for backup only hosts."
						}
						, "Hidden?"_o=
						{
							"Names"_o= {"--hidden"}
							, "Type"_o= false
							, "Description"_o= "Hide this member so clients does not use it for queries."
						}
						, "ExtraTags?"_o=
						{
							"Names"_o= {"--extra-tags"}
							, "Type"_o= {"*"_o= ""}
							, "Description"_o= fg_Format("Specify extra tags to add to this member. The hostname '{}' will always be included as a tag.", NProcess::NPlatform::fg_Process_GetHostName())
						}
					}
					, "Parameters"_o=
					{
						"ReplicaMember"_o=
						{
							"Type"_o= ""
							, "Description"_o= "Specify a member of the replica to join.\n"
								"If you specify yourself here a new replica set will be created."
						}
					}
				}
				, [this](NEncoding::CEJSONSorted _Parameters, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
				{
					co_return co_await fp_CommandLine_JoinReplica(fg_Move(_Parameters), fg_Move(_pCommandLine));
				}
				, EDistributedAppCommandFlag_RunLocalApp
			)
		;
	}

	uint32 CMongoManagerDaemonActor::fp_CommandLine_ListRestoreRange(NEncoding::CEJSONSorted const &_Params)
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
	
	TCFuture<uint32> CMongoManagerDaemonActor::fp_CommandLine_Restore(NEncoding::CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		co_await fp_WaitForAppStartup();

		CTime Time;
		{
			auto &RestoreTime = _Params["RestoreTime"];
			if (RestoreTime.f_IsDate())
				Time = RestoreTime.f_Date();
		}
		
		co_await (mp_pManager(&CMongoManagerActor::f_RestoreMongo, Time) % "Error restoring from backup");

		*_pCommandLine += "Restore finished successfully\n";

		co_return 0;
	}
	
	TCFuture<uint32> CMongoManagerDaemonActor::fp_CommandLine_UpdateReplicationConfig
		(
			NEncoding::CEJSONSorted const _Params
			, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine
		)
	{
		co_await fp_WaitForAppStartup();

		co_await mp_pManager(&CMongoManagerActor::f_UpdateReplicationConfig);

		*_pCommandLine += "Replication config updated successfully\n";

		co_return {};
	}
	
	TCFuture<uint32> CMongoManagerDaemonActor::fp_CommandLine_SetupPermissions(NEncoding::CEJSONSorted const _Param, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		co_await fp_WaitForAppStartup();

		co_await mp_pManager(&CMongoManagerActor::f_SetupPermissions);

		*_pCommandLine += "Permissions setup successfully\n";

		co_return 0;
	}
	
	struct CDummyBackupInterface : public CDistributedAppInterfaceBackup
	{
		enum : uint32
		{
			EProtocolVersion_Min = 0x101
			, EProtocolVersion_Current = 0x101
		};

		CDummyBackupInterface(uint32 _BackupID)
			: m_BackupID{_BackupID}
		{
		}
		~CDummyBackupInterface() = default;

		NConcurrency::TCFuture<void> f_AppendManifest(CManifestConfig _Config) override
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
				NConcurrency::TCActorFunctorWithID<TCFuture<void> ()> _fOnInitialFinished
			) override
		{
			DLogWithCategory(MongoManager/Backup, Info, "(LocalBackup {}) Subscribe initial finished", m_BackupID);
			_fOnInitialFinished().f_DiscardResult();
			co_return {};
		}
		
		NConcurrency::TCFuture<NConcurrency::TCActorSubscriptionWithID<>> f_SubscribeBackupStopped
			(
				NConcurrency::TCActorFunctorWithID<TCFuture<void> ()> _fOnStopped
			) override
		{
			DLogWithCategory(MongoManager/Backup, Info, "(LocalBackup {}) Subscribe backup stopped", m_BackupID);
			co_return {};
		}
		
		uint32 m_BackupID = -1;
	};
	
	TCFuture<uint32> CMongoManagerDaemonActor::fp_CommandLine_RunBackup(NEncoding::CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		co_await fp_WaitForAppStartup();

		uint32 BackupID = mp_NextLocalBackup++;
		
		auto &LocalBackup = mp_LocalBackups[BackupID];
		
		LocalBackup.m_BackupInterface = mp_State.m_DistributionManager->f_ConstructActor<CDummyBackupInterface>(BackupID);

		auto Cleanup = g_OnScopeExitActor / [&]
			{
				mp_LocalBackups.f_Remove(BackupID);
			}
		;

		auto Subscription = co_await fp_StartBackup
			(
				LocalBackup.m_BackupInterface->f_ShareInterface<CDistributedAppInterfaceBackup>().f_GetActor()
				, nullptr
				, CFile::fs_GetProgramDirectory()
			)
		;

		Cleanup->f_Clear();

		auto *pLocalBackup = mp_LocalBackups.f_FindEqual(BackupID);

		if (!pLocalBackup)
			co_return DErrorInstance("Backup already cancelled");

		pLocalBackup->m_Subscription = fg_Move(Subscription);

		*_pCommandLine += "{}\n"_f << BackupID;

		co_return 0;
	}

	TCFuture<uint32> CMongoManagerDaemonActor::fp_CommandLine_CancelBackups(NEncoding::CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		co_await fp_WaitForAppStartup();

		TCSet<uint32> BackupIDs;
		if (auto pValue = _Params.f_GetMember("BackupIDs"))
		{
			for (auto &ID : pValue->f_Array())
				BackupIDs[ID.f_Integer()];
		}
		
		if (BackupIDs.f_IsEmpty())
			BackupIDs = mp_LocalBackups;
		
		TCSet<uint32> MissingBackupIDs;
		
		TCFutureMap<uint32, void> DestroyResults;
		
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
				LocalBackup.m_Subscription->f_Destroy() > DestroyResults[ID];
			
			mp_LocalBackups.f_Remove(pLocalBackup);
		}
		
		auto Results = co_await fg_AllDoneWrapped(DestroyResults);

		uint32 ExitStatus = 0;

		for (auto &Result : Results)
		{
			uint32 ID = Results.fs_GetKey(Result);

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

		co_return ExitStatus;
	}
	
	TCFuture<uint32> CMongoManagerDaemonActor::fp_CommandLine_JoinReplica(NEncoding::CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		co_await fp_WaitForAppStartup();

		CJoinReplicaOptions Options;

		if (auto pValue = _Params.f_GetMember("MongoPort"))
		{
			int64 MongoPort = pValue->f_Integer();
			if (MongoPort <= 0 || MongoPort > 65535)
				co_return DMibErrorInstance(fg_Format("Invalid network port: {}", MongoPort));
			Options.m_Port = MongoPort;
		}

		if (auto pValue = _Params.f_GetMember("MongoReplicaName"))
		{
			if (pValue->f_String().f_IsEmpty())
				co_return DMibErrorInstance("Replica name cannot be empty");
			Options.m_ReplicaName = pValue->f_String();
		}

		if (auto pValue = _Params.f_GetMember("CanVote"))
			Options.m_CanVote = pValue->f_Boolean();

		if (auto pValue = _Params.f_GetMember("Priority"))
		{
			fp64 Priority = pValue->f_Float();
			if (Priority < 0.0 || Priority > 1000.0)
				co_return DMibErrorInstance("Priority out of range");
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
			co_return DMibErrorInstance("You must specify replica memeber to join");

		co_await mp_pManager(&CMongoManagerActor::f_JoinReplica, Options);

		*_pCommandLine %= "Replica set successfully joined\n";

		co_return 0;
	}
}
