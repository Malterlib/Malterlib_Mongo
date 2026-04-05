// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Mongo_App_MongoManager_Server.h"

#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Encoding/JsonShortcuts>

namespace NMib::NMongo::NMongoManager
{
	TCFuture<void> CMongoManagerActor::fp_Mongo_UpdateReplicationConfig(CMongoConnectionSettings _ConnectionSettings)
	{
		TCSharedPointer<CMongoClientRetryState> pState = fg_Construct(_ConnectionSettings);
		auto DestroyMongoClient = co_await fg_AsyncDestroy(pState);

		auto &MongoHost = _ConnectionSettings.f_GetSingleHost();

		CEJsonOrdered CurrentReplicaSet;
		{
			auto CurrentReplicaSets = co_await CMongoClientActor::fs_WithConnectionRetry
				(
					&CMongoClientActor::f_Query
					, pState
					, "local.system.replset"
					, EJsonType_Object
					, 1
					, 0
					, nullptr
					, nullptr
					, CMongoClientActor::EQueryOption_None
				)
			;

			if (!CurrentReplicaSets.f_IsEmpty())
				CurrentReplicaSet = fg_Move(CurrentReplicaSets.f_GetFirst());
			else
				CurrentReplicaSet = EJsonType_Object;
		}

		auto OldConfig = CurrentReplicaSet;

		co_await CMongoClientActor::fs_WithConnectionRetry
			(
				&CMongoClientActor::f_Remove
				, pState
				, "local.system.replset"
				, EJsonType_Object
				, CMongoClientActor::ERemoveOption_None
			)
		;

		CurrentReplicaSet["_id"] = mp_MongoReplicaName;
		auto &Members = CurrentReplicaSet["members"].f_Array();

		CStr Self = _ConnectionSettings.f_GetConnectionString();
		CStr SelfTag = Self.f_ReplaceChar('.', '_').f_ReplaceChar(':', '_');

		Members.f_Clear();
		Members.f_Insert() = CEJsonOrdered
			{
				"_id"_o= fsp_Mongo_SetInt32Value(0)
				, "host"_o= fg_Format("{}:{}", MongoHost.m_Host, MongoHost.m_Port)
				, "arbiterOnly"_o= false
				, "buildIndexes"_o= true
				, "hidden"_o= false
				, "priority"_o= 1.0
				, "tags"_o=
				{
					_o(SelfTag) = "1"
				}
				, "secondaryDelaySecs"_o= 0
				, "votes"_o= fsp_Mongo_SetInt32Value(1)
			}
		;

		CurrentReplicaSet["settings"]["getLastErrorModes"] = CEJsonOrdered(EJsonType_Object);
		CurrentReplicaSet["settings"]["getLastErrorModes"][SelfTag][SelfTag] = 1;
		CurrentReplicaSet["version"] = fsp_Mongo_SetInt32Value(fsp_Mongo_GetInt32Value(CurrentReplicaSet.f_GetMember("version")) + 1);

		co_await CMongoClientActor::fs_WithConnectionRetry
			(
				&CMongoClientActor::f_Insert
				, pState
				, "local.system.replset"
				, CurrentReplicaSet
				, CMongoClientActor::EInsertOption_None
			)
		;

		co_return {};
	}
}
