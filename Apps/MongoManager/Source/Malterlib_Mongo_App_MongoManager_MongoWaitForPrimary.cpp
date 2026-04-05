// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Mongo_App_MongoManager_Server.h"

#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Encoding/JsonShortcuts>

namespace NMib::NMongo::NMongoManager
{
	TCFuture<void> CMongoManagerActor::fp_Mongo_WaitForPrimary(CMongoConnectionSettings _ConnectionSettings, bool _bExpectReplica)
	{
		TCSharedPointer<CMongoClientRetryState> pState = fg_Construct(_ConnectionSettings, 5.0 * 60.0);
		auto DestroyMongoClient = co_await fg_AsyncDestroy(pState);

		if (!_bExpectReplica)
		{
			auto Status = co_await fp_MongoHelper_GetReplicaSetStatus(pState).f_Wrap();

			co_await fsp_MongoHelper_AssureNotYetInitialized(Status);

			co_return {};
		}

		co_await fp_MongoHelper_WaitForSelf(pState, false);

		auto const Status = co_await fp_MongoHelper_GetReplicaSetStatus(pState);

		auto &MongoHost = mp_MongoConnectionSettings.f_GetSingleHost();

		CStr MongoSelf = fg_Format("{}:{}", MongoHost.m_Host, MongoHost.m_Port);

		auto *pMembers = Status.f_GetMember("members", EJsonType_Array);
		if (!pMembers)
			co_return DMibErrorInstance("Replica set status doesn't include members: {}"_f << Status);

		bool bFoundSelf = false;
		for (auto &Member : pMembers->f_Array())
		{
			if (Member.f_GetMemberValue("name", CStr()).f_String() == MongoSelf)
			{
				bFoundSelf = true;
				break;
			}
		}

		if (!bFoundSelf)
			co_return DMibErrorInstance("Could not find self in replica set: {}"_f << Status);

		co_await fp_MongoHelper_WaitForPrimary(pState);

		co_return {};
	}
}
