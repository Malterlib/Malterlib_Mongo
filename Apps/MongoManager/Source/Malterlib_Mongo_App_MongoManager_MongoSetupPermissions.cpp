// Copyright © 2024 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_App_MongoManager_Server.h"

#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Encoding/JsonShortcuts>

namespace NMib::NMongo::NMongoManager
{
	TCFuture<void> CMongoManagerActor::fp_Mongo_SetupPermissions(CMongoConnectionSettings _ConnectionSettings, CStr _UserName)
	{
		TCSharedPointer<CMongoClientRetryState> pState = fg_Construct(_ConnectionSettings);
		auto DestroyMongoClient = co_await fg_AsyncDestroy(pState);

		if (!(co_await fp_MongoHelper_GetRole(pState, "oplogger")).f_IsValid())
		{
			co_await fp_MongoHelper_CreateRole
				(
					pState
					, "oplogger"
					, CEJsonOrdered
					{
						"privileges"_o= _o
						[
							_o=
							{
								"resource"_o=
								{
									"db"_o= "local"
									, "collection"_o= "oplog.rs"
								}
								, "actions"_o= _o["find"]
							}
						]
						, "roles"_o= _o
						[
							_o=
							{
								"role"_o= "read"
								, "db"_o= "local"
							}
						]
					}
				)
			;
		}

		if (!(co_await fp_MongoHelper_GetRole(pState, "anyActionOnAnyResource")).f_IsValid())
		{
			co_await fp_MongoHelper_CreateRole
				(
					pState
					, "anyActionOnAnyResource"
					, CEJsonOrdered
					{
						"privileges"_o= _o
						[
							_o=
							{
								"resource"_o=
								{
									"anyResource"_o= true
								}
								, "actions"_o= _o["anyAction"]
							}
						]
						, "roles"_o= EJsonType_Array
					}
				)
			;
		}

		CEJsonOrdered AdminRoles = _o
			[
				_o=
				{
					"role"_o= "root"
					, "db"_o= "admin"
				}
				, _o=
				{
					"role"_o= "read"
					, "db"_o= "local"
				}
				, _o=
				{
					"role"_o= "oplogger"
					, "db"_o= "admin"
				}
				, _o=
				{
					"role"_o= "anyActionOnAnyResource"
					, "db"_o= "admin"
				}
			]
		;

		if (!(co_await fp_MongoHelper_GetUser(pState, _UserName)).f_IsValid())
		{
			co_await fp_MongoHelper_CreateUser
				(
					pState
					, _UserName
					, CEJsonOrdered
					{
						"roles"_o= AdminRoles
					}
				)
			;
		}
		else
		{
			co_await fp_MongoHelper_UpdateUser
				(
					pState
					, _UserName
					, CEJsonOrdered
					{
						"roles"_o= AdminRoles
					}
				)
			;
		}

		co_return {};
	}
}
