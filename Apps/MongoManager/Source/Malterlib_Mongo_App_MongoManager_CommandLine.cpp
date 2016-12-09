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
				, [this](NEncoding::CEJSON const &_Parameters) -> TCContinuation<CDistributedAppCommandLineResults>
				{
					return fp_CommandLine_Restore(_Parameters);
				}
				, true
			)
		;
		DefaultSection.f_RegisterCommand
			(
				{
					"Names"_= {"--setup-permissions"}
					, "Description"_= "Sets up permissions for a empty database by adding the admin user.\n"
				}
				, [this](NEncoding::CEJSON const &_Parameters) -> TCContinuation<CDistributedAppCommandLineResults>
				{
					return fp_CommandLine_SetupPermissions(_Parameters);
				}
				, true
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
				, [this](NEncoding::CEJSON const &_Parameters) -> TCContinuation<CDistributedAppCommandLineResults>
				{
					return fp_CommandLine_UpdateReplicationConfig(_Parameters);
				}
				, true
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
				, [this](NEncoding::CEJSON const &_Parameters) -> TCContinuation<CDistributedAppCommandLineResults>
				{
					return fp_CommandLine_JoinReplica(_Parameters);
				}
				, true
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
				
				TCVector<uint8> Data;
				Data.f_SetLen(EntrySize);
				Stream.f_ConsumeBytes(Data.f_GetArray(), EntrySize);
				
				auto OplogEntry = fg_FromBSON(mongo::BSONObj{reinterpret_cast<char const *>(Data.f_GetArray())});
				
				if (auto pTimestamp = OplogEntry.f_GetMember("ts", EEJSONType_UserType))
				{
					if (pTimestamp->f_UserType().m_Type == "Timestamp")
					{
						CTime Time = CTimeConvert::fs_FromCreateFromUnixSeconds(pTimestamp->f_UserType().m_Value["Seconds"].f_Integer());
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
	
	TCContinuation<CDistributedAppCommandLineResults> CMongoManagerDaemonActor::fp_CommandLine_Restore(NEncoding::CEJSON const &_Params)
	{
		TCContinuation<CDistributedAppCommandLineResults> Continuation;
		
		CTime Time;
		{
			auto &RestoreTime = _Params["RestoreTime"];
			if (RestoreTime.f_IsDate())
				Time = RestoreTime.f_Date();
		}
		
		mp_pManager(&CMongoManagerActor::f_RestoreMongo, Time) > Continuation % "Error restoring from backup" / [Continuation]()
			{
				CDistributedAppCommandLineResults Results;
				Results.f_AddStdErr("Restore finished successfully\n");
				Continuation.f_SetResult(fg_Move(Results));
			}
		;
		return Continuation;
	}
	
	TCContinuation<CDistributedAppCommandLineResults> CMongoManagerDaemonActor::fp_CommandLine_UpdateReplicationConfig(NEncoding::CEJSON const &_Params)
	{
		TCContinuation<CDistributedAppCommandLineResults> Continuation;
 		mp_pManager(&CMongoManagerActor::f_UpdateReplicationConfig) > Continuation / [Continuation]
			{
				CDistributedAppCommandLineResults Results;
				Results.f_AddStdErr("Replication config updated successfully\n");
				Continuation.f_SetResult(Results);
			}
		;
		return Continuation;
	}
	
	TCContinuation<CDistributedAppCommandLineResults> CMongoManagerDaemonActor::fp_CommandLine_SetupPermissions(NEncoding::CEJSON const &_Params)
	{
		TCContinuation<CDistributedAppCommandLineResults> Continuation;
 		mp_pManager(&CMongoManagerActor::f_SetupPermissions) > Continuation / [Continuation]
			{
				CDistributedAppCommandLineResults Results;
				Results.f_AddStdErr("Permissions setup successfully\n");
				Continuation.f_SetResult(Results);
			}
		;
		return Continuation;
	}
	
	TCContinuation<CDistributedAppCommandLineResults> CMongoManagerDaemonActor::fp_CommandLine_JoinReplica(NEncoding::CEJSON const &_Params)
	{
		CJoinReplicaOptions Options;
		if (auto pValue = _Params.f_GetMember("MongoPort"))
		{
			int64 MongoPort = pValue->f_Integer();
			if (MongoPort <= 0 || MongoPort > 65535)
				return DMibErrorInstance(fg_Format("Invalid network port: {}", MongoPort));
			Options.m_Port = MongoPort;
		}
		if (auto pValue = _Params.f_GetMember("MongoReplicaName"))
		{
			if (pValue->f_String().f_IsEmpty())
				return DMibErrorInstance("Replica name cannot be empty");
			Options.m_ReplicaName = pValue->f_String();
		}
		if (auto pValue = _Params.f_GetMember("CanVote"))
			Options.m_CanVote = pValue->f_Boolean(); 
		if (auto pValue = _Params.f_GetMember("Priority"))
		{
			fp64 Priority = pValue->f_Integer();
			if (Priority < 0.0 || Priority > 1000.0)
				return DMibErrorInstance("Priority out of range");
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
			return DMibErrorInstance("You must specify replica memeber to join");
		
		TCContinuation<CDistributedAppCommandLineResults> Continuation;
 		mp_pManager(&CMongoManagerActor::f_JoinReplica, Options) > Continuation / [Continuation]
			{
				CDistributedAppCommandLineResults Results;
				Results.f_AddStdErr("Replica set successfully joined\n");
				Continuation.f_SetResult(Results);
			}
		;
		return Continuation;
	}
}
