// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Mongo_App_MongoManager_Server.h"

#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Encoding/JsonShortcuts>

namespace NMib::NMongo::NMongoManager
{
	TCFuture<CStr> CMongoManagerActor::fp_Mongo_GetPrimary(CMongoConnectionSettings _ConnectionSettings)
	{
		TCSharedPointer<CMongoClientRetryState> pState = fg_Construct(_ConnectionSettings);
		auto DestroyMongoClient = co_await fg_AsyncDestroy(pState);

		auto Return = co_await fp_MongoHelper_GetHello(pState);

		auto *pPrimary = Return.f_GetMember("primary", EJsonType_String);
		if (!pPrimary)
			co_return DMibErrorInstance("Primary not found in: {}"_f << Return);

		co_return pPrimary->f_String();
	}
}
