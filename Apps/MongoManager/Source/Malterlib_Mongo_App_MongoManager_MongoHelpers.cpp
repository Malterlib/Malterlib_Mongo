// Copyright © 2024 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_App_MongoManager_Server.h"

#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Encoding/JsonShortcuts>

namespace NMib::NMongo::NMongoManager
{
	TCActor<CMongoClientActor> CMongoManagerActor::fp_MongoHelper_GetClient(CMongoConnectionSettings _ConnectionSettings)
	{
		return {fg_Construct(_ConnectionSettings, "local"), "MongoDB client connection (config)"};
	}

	CStr CMongoManagerActor::fsp_Mongo_GetErrorCodeName(CExceptionPointer &&_pException)
	{
		auto MongoErrorData = CMongoErrorData::fs_FromException(_pException);

		if (MongoErrorData)
			return MongoErrorData->f_GetCodeName();

		return {};
	}

	CEJsonOrdered CMongoManagerActor::fsp_Mongo_SetInt32Value(int32 _Value)
	{
		return CEJsonOrdered::CUserType{"int32", _Value};
	}

	int32 CMongoManagerActor::fsp_Mongo_GetInt32Value(CEJsonOrdered const *_pValue)
	{
		if (!_pValue)
			return 0;

		if (_pValue->f_IsUserType() && _pValue->f_UserType().m_Value.f_IsInteger())
			return _pValue->f_UserType().m_Value.f_Integer();
		else if (_pValue->f_IsInteger())
			return _pValue->f_Integer();
		else
			return 0;
	}

	TCFuture<CEJsonOrdered> CMongoManagerActor::fp_MongoHelper_GetReplicaSetStatus(TCSharedPointer<CMongoClientRetryState> _pState)
	{
		co_return co_await CMongoClientActor::fs_WithConnectionRetry
			(
				&CMongoClientActor::f_RunCommand
				, _pState
				, gc_Str<"admin">.m_Str
				, CEJsonOrdered
				{
					"replSetGetStatus"_o= 1
				}
			)
		;
	}

	bool CMongoManagerActor::fsp_MongoHelper_ReplicaSetStatusIsNotYetInitialized(TCAsyncResult<CEJsonOrdered> const &_Status)
	{
		return !_Status && fsp_Mongo_GetErrorCodeName(_Status.f_GetException()) == "NotYetInitialized";
	}

	TCFuture<void> CMongoManagerActor::fsp_MongoHelper_AssureNotYetInitialized(TCAsyncResult<CEJsonOrdered> _Status)
	{
		if (!fsp_MongoHelper_ReplicaSetStatusIsNotYetInitialized(_Status))
			co_return DMibErrorInstance("Expected database with no replica set config, not: {}"_f << _Status);

		co_return {};
	}

	TCFuture<CEJsonOrdered> CMongoManagerActor::fp_MongoHelper_GetReplicaSetConfig(TCSharedPointer<CMongoClientRetryState> _pState)
	{
		auto Result = co_await CMongoClientActor::fs_WithConnectionRetry
			(
				&CMongoClientActor::f_RunCommand
				, _pState
				, gc_Str<"admin">.m_Str
				, CEJsonOrdered
				{
					"replSetGetConfig"_o= 1
				}
			)
		;

		if (Result.f_GetMemberValue("ok", 0.0) != 1.0)
			co_return DMibErrorInstance("Replica set config is not OK: {}"_f << Result);

		auto pConfig = Result.f_GetMember("config", EJsonType_Object);
		if (!pConfig)
			co_return DMibErrorInstance("Replica set config doesn't contain 'config': {}"_f << Result);

		co_return fg_Move(*pConfig);
	}

	TCFuture<void> CMongoManagerActor::fp_MongoHelper_WaitForPrimary(TCSharedPointer<CMongoClientRetryState> _pState)
	{
		while (true)
		{
			auto const StatusResult = co_await fp_MongoHelper_GetReplicaSetStatus(_pState).f_Wrap();

			if (!StatusResult)
				co_return StatusResult.f_GetException();

			auto const Status = *StatusResult;

			if (auto *pStatus = Status.f_GetMember("ok", EEJsonType_Float))
			{
				if (pStatus->f_Float() != 1.0)
				{
					co_await fg_Timeout(0.1);

					continue;
				}
			}
			else
				co_return DMibErrorInstance("Couldn't find 'ok' with float type in replication status: {}"_f << Status);

			if (auto *pMembers = Status.f_GetMember("members", EEJsonType_Array))
			{
				bool bFoundPrimary = false;
				for (auto &Member : pMembers->f_Array())
				{
					if (auto *pState = Member.f_GetMember("state", EEJsonType_UserType))
					{
						if (!pState->f_UserType().m_Value.f_IsInteger())
							co_return DMibErrorInstance("Wrong type for 'state' in replication status: {}"_f << Status);

						auto State = pState->f_UserType().m_Value.f_Integer();

						if (State == 1)
						{
							bFoundPrimary = true;
							break;
						}
					}
					else
						co_return DMibErrorInstance("Couldn't find 'state' with UserType type in replication status: {}"_f << Status);
				}

				if (bFoundPrimary)
					break;
			}
			else
				co_return DMibErrorInstance("Couldn't find 'members' in replication status: {}"_f << Status);

			co_await fg_Timeout(0.1);
		}

		co_return {};
	}

	TCFuture<CEJsonOrdered> CMongoManagerActor::fp_MongoHelper_GetHello(TCSharedPointer<CMongoClientRetryState> _pState)
	{
		co_return co_await CMongoClientActor::fs_WithConnectionRetry
			(
				&CMongoClientActor::f_RunCommand
				, _pState
				, gc_Str<"admin">.m_Str
				, CEJsonOrdered
				{
					"hello"_o= 1
				}
			)
		;
	}

	TCFuture<CEJsonOrdered> CMongoManagerActor::fp_MongoHelper_GetRole(TCSharedPointer<CMongoClientRetryState> _pState, CStr _Name)
	{
		auto Roles = co_await CMongoClientActor::fs_WithConnectionRetry
			(
				&CMongoClientActor::f_RunCommand
				, _pState
				, gc_Str<"admin">.m_Str
				, CEJsonOrdered
				{
					"rolesInfo"_o=
					{
						"role"_o= _Name
						, "db"_o= "admin"
					}
				}
			)
		;

		auto *pRoles = Roles.f_GetMember("roles", EJsonType_Array);
		if (!pRoles)
			co_return {};

		for (auto &Role : pRoles->f_Array())
		{
			if (Role.f_GetMemberValue("role", CStr()) == _Name)
				co_return fg_Move(Role);
		}

		co_return {};
	}

	TCFuture<void> CMongoManagerActor::fp_MongoHelper_CreateRole(TCSharedPointer<CMongoClientRetryState> _pState, CStr _Name, CEJsonOrdered _Role)
	{
		CEJsonOrdered CreateCommand
			{
				"createRole"_o= _Name
			}
		;

		for (auto &Member : _Role.f_Object())
			CreateCommand[Member.f_Name()] = fg_Move(Member.f_Value());

		co_await CMongoClientActor::fs_WithConnectionRetry
			(
				&CMongoClientActor::f_RunCommand
				, _pState
				, gc_Str<"admin">.m_Str
				, fg_Move(CreateCommand)
			)
		;

		co_return {};
	}

	TCFuture<CEJsonOrdered> CMongoManagerActor::fp_MongoHelper_GetUser(TCSharedPointer<CMongoClientRetryState> _pState, CStr _Name)
	{
		auto Roles = co_await CMongoClientActor::fs_WithConnectionRetry
			(
				&CMongoClientActor::f_RunCommand
				, _pState
				, gc_Str<"$external">.m_Str
				, CEJsonOrdered
				{
					"usersInfo"_o=
					{
						"user"_o= _Name
						, "db"_o= "$external"
					}
				}
			)
		;

		auto *pUsers = Roles.f_GetMember("users", EJsonType_Array);
		if (!pUsers)
			co_return {};

		for (auto &User : pUsers->f_Array())
		{
			if (User.f_GetMemberValue("user", CStr()) == _Name)
				co_return fg_Move(User);
		}

		co_return {};
	}

	TCFuture<void> CMongoManagerActor::fp_MongoHelper_CreateUser(TCSharedPointer<CMongoClientRetryState> _pState, CStr _Name, CEJsonOrdered _User)
	{
		CEJsonOrdered CreateCommand
			{
				"createUser"_o= _Name
			}
		;

		for (auto &Member : _User.f_Object())
			CreateCommand[Member.f_Name()] = fg_Move(Member.f_Value());

		co_await CMongoClientActor::fs_WithConnectionRetry
			(
				&CMongoClientActor::f_RunCommand
				, _pState
				, gc_Str<"$external">.m_Str
				, fg_Move(CreateCommand)
			)
		;

		co_return {};
	}

	TCFuture<void> CMongoManagerActor::fp_MongoHelper_UpdateUser(TCSharedPointer<CMongoClientRetryState> _pState, CStr _Name, CEJsonOrdered _User)
	{
		CEJsonOrdered UpdateCommand
			{
				"updateUser"_o= _Name
			}
		;

		for (auto &Member : _User.f_Object())
			UpdateCommand[Member.f_Name()] = fg_Move(Member.f_Value());

		co_await CMongoClientActor::fs_WithConnectionRetry
			(
				&CMongoClientActor::f_RunCommand
				, _pState
				, gc_Str<"$external">.m_Str
				, fg_Move(UpdateCommand)
			)
		;
		co_return {};
	}

	TCFuture<void> CMongoManagerActor::fp_MongoHelper_WaitForSelf(TCSharedPointer<CMongoClientRetryState> _pState, bool _bExpectNotInitializedWhenPolling)
	{
		while (true)
		{
			auto const StatusResult = co_await fp_MongoHelper_GetReplicaSetStatus(_pState).f_Wrap();

			if (!StatusResult)
			{
				if (_bExpectNotInitializedWhenPolling && fsp_MongoHelper_ReplicaSetStatusIsNotYetInitialized(StatusResult))
				{
					co_await fg_Timeout(0.1);

					continue;
				}
				else
					co_return StatusResult.f_GetException();
			}

			auto const &Status = *StatusResult;

			if (auto *pStatus = Status.f_GetMember("ok", EEJsonType_Float))
			{
				if (pStatus->f_Float() != 1.0)
				{
					co_await fg_Timeout(0.1);

					continue;
				}
			}
			else
				co_return DMibErrorInstance("Couldn't find 'ok' with float type in replication status: {}"_f << Status);

			if (auto *pState = Status.f_GetMember("myState", EEJsonType_UserType))
			{
				if (!pState->f_UserType().m_Value.f_IsInteger())
					co_return DMibErrorInstance("Wrong type for 'myState' in replication status: {}"_f << Status);

				auto State = pState->f_UserType().m_Value.f_Integer();

				if (State <= 2)
					break;
			}
			else
				co_return DMibErrorInstance("Couldn't find 'myState' with UserType type in replication status: {}"_f << Status);

			co_await fg_Timeout(0.1);
		}

		co_return {};
	}
}
