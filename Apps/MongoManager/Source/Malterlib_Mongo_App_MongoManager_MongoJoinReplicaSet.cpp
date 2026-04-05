// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Mongo_App_MongoManager_Server.h"

#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Encoding/JsonShortcuts>

namespace NMib::NMongo::NMongoManager
{
	TCFuture<void> CMongoManagerActor::fp_Mongo_JoinReplicaSet
		(
			CMongoConnectionSettings _JoinConnectionSettings
			, CMongoConnectionSettings _LocalConnectionSettings
			, CEJsonOrdered _ReplicationConfig
			, CStr _SelfTag
		)
	{
		{
			TCSharedPointer<CMongoClientRetryState> pState = fg_Construct(_JoinConnectionSettings);
			auto DestroyMongoClient = co_await fg_AsyncDestroy(pState);

			auto HelloInfo = co_await fp_MongoHelper_GetHello(pState).f_Wrap();

			auto fIsPrimary = [](CEJsonOrdered const &_Hello) -> bool
				{
					auto Master = _Hello.f_GetMemberValue("master", CStr()).f_String();
					auto Me = _Hello.f_GetMemberValue("me", CStr()).f_String();

					return Master && Master == Me;
				}
			;

			if (!HelloInfo || fIsPrimary(*HelloInfo))
				co_return DMibErrorInstance("Trying to join replica set on non primary: {}"_f << HelloInfo);

			auto ReplicaSetConfig = co_await fp_MongoHelper_GetReplicaSetConfig(pState);
			auto *pMembers = ReplicaSetConfig.f_GetMember("members", EJsonType_Array);
			if (!pMembers)
				co_return DMibErrorInstance("Replica set config doesn't contain 'members': {}"_f << ReplicaSetConfig);

			ReplicaSetConfig["version"] = fsp_Mongo_SetInt32Value(fsp_Mongo_GetInt32Value(ReplicaSetConfig.f_GetMember("version")) + 1);

			int32 MaxID = 0;
			for (auto &Member : pMembers->f_Array())
				MaxID = fg_Max(fsp_Mongo_GetInt32Value(Member.f_GetMember("_id")), MaxID);

			auto &NewMember = pMembers->f_Insert(_ReplicationConfig);
			NewMember["_id"] = fsp_Mongo_SetInt32Value(MaxID + 1);

			ReplicaSetConfig["settings"]["getLastErrorModes"][_SelfTag][_SelfTag] = 1;

			co_await CMongoClientActor::fs_WithConnectionRetry
				(
					&CMongoClientActor::f_RunCommand
					, pState
					, gc_Str<"admin">.m_Str
					, CEJsonOrdered
					{
						"replSetReconfig"_o= fg_TempCopy(ReplicaSetConfig)
					}
				)
			;
		}

		co_await g_AsyncDestroy;

		// Start / Restart local mongod

		TCSharedPointer<CMongoClientRetryState> pState = fg_Construct(_LocalConnectionSettings);
		auto DestroyMongoClient = co_await fg_AsyncDestroy(pState);

		while (true)
		{
			auto Result = co_await fp_MongoHelper_WaitForSelf(pState, true).f_Wrap();

			if (Result)
				break;
			else if (mp_bVerboseMongoScripts)
				DMibLog(Warning, "Error while waiting for self (retrying): {}", Result.f_GetExceptionStr());

			co_await fg_Timeout(1.0);
		}

		if (mp_bVerboseMongoScripts)
			DMibLog(Info, "Waiting for primary");

		co_await fp_MongoHelper_WaitForPrimary(pState);

		if (mp_bVerboseMongoScripts)
		{
			DMibLog(Info, "Resulting replica set config:\n\n{}", co_await fp_MongoHelper_GetReplicaSetConfig(pState));
			DMibLog(Info, "Resulting replica set status:\n\n{}", co_await fp_MongoHelper_GetReplicaSetStatus(pState).f_Wrap());
		}

		co_return {};
	}
}
