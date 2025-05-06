// Copyright © 2024 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_App_MongoManager_Server.h"

#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Encoding/JSONShortcuts>

namespace NMib::NMongo::NMongoManager
{
	TCFuture<void> CMongoManagerActor::fp_Mongo_InitReplicaSet(CMongoConnectionSettings _ConnectionSettings, CEJSONOrdered _ReplicationConfig, CStr _SelfTag)
	{
		TCSharedPointer<CMongoClientRetryState> pState = fg_Construct(_ConnectionSettings);
		auto DestroyMongoClient = co_await fg_AsyncDestroy(pState);

		auto Status = co_await fp_MongoHelper_GetReplicaSetStatus(pState).f_Wrap();

		if (!Status)
			DMibLog(Warning, "fp_MongoHelper_GetReplicaSetStatus: {}", fsp_Mongo_GetErrorCodeName(Status.f_GetException()));

		co_await fsp_MongoHelper_AssureNotYetInitialized(Status);

		auto MemberConfig = _ReplicationConfig;
		MemberConfig["_id"] = 0;

		CEJSONOrdered ExpectedConfig
			{
				"_id"_o= mp_MongoReplicaName
				, "members"_o=
				{
					_o
					[
						MemberConfig
					]
				}
				, "settings"_o=
				{
					"getLastErrorModes"_o= EJSONType_Object
				}
			}
		;

		ExpectedConfig["settings"]["getLastErrorModes"][_SelfTag][_SelfTag] = 1;

		co_await CMongoClientActor::fs_WithConnectionRetry
			(
				&CMongoClientActor::f_RunCommand
				, pState
				, gc_Str<"admin">.m_Str
				, CEJSONOrdered
				{
					"replSetInitiate"_o= fg_TempCopy(ExpectedConfig)
				}
			)
		;

		co_await fp_MongoHelper_WaitForPrimary(pState);

		if (mp_bVerboseMongoScripts)
		{
			DMibLog(Info, "Resulting replica set config:\n\n{}", co_await fp_MongoHelper_GetReplicaSetConfig(pState));
			DMibLog(Info, "Resulting replica set status:\n\n{}", co_await fp_MongoHelper_GetReplicaSetStatus(pState).f_Wrap());
		}

		co_return {};
	}
}
