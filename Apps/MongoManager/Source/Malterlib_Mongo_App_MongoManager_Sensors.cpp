// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Mongo_App_MongoManager_Server.h"

#include <Mib/Concurrency/LogError>
#include <Mib/Encoding/JsonShortcuts>

namespace NMib::NMongo::NMongoManager
{
	TCFuture<void> CMongoManagerActor::fp_OpenSensors()
	{
		switch (mp_Mode)
		{
		case EMode_RunRestore:
		case EMode_UpdateReplicationConfig:
		case EMode_WithoutReplicaSet:
		case EMode_SetupPermissions:
		case EMode_JoinReplicaSet:
			co_return {};
		case EMode_Normal:
			break;
		}

		CDistributedAppSensorReporter::CSensorInfo SensorInfo;
		SensorInfo.m_Identifier = "org.malterlib.mongo.mongo-manager.replica-status";
		SensorInfo.m_IdentifierScope = mp_AppState.m_RootDirectory;
		SensorInfo.m_Name = "Mongo Manager Replica Status";
		SensorInfo.m_ExpectedReportInterval = 1_minutes;
		SensorInfo.m_Type = NConcurrency::CDistributedAppSensorReporter::ESensorDataType_Status;
		SensorInfo.m_Flags = NConcurrency::CDistributedAppSensorReporter::ESensorInfoFlag::mc_PreventRebootOnError
			| NConcurrency::CDistributedAppSensorReporter::ESensorInfoFlag::mc_PreventRebootOnWarning
			| NConcurrency::CDistributedAppSensorReporter::ESensorInfoFlag::mc_PreventRebootOnOutdated
		;

		mp_ReplicaStatusReporter = co_await mp_AppState.m_AppActor(&CDistributedAppActor::f_OpenSensorReporter, fg_Move(SensorInfo));

		co_return {};
	}

	CFutureCoroutineContextOnResumeScopeAwaiter CMongoManagerActor::fp_CheckSensorDependencies() const
	{
		return fg_OnResume
			(
				[this]() -> NException::CExceptionPointer
				{
					if (mp_bStopped || f_IsDestroyed())
						return DMibImpExceptionInstance(CExceptionActorIsBeingDestroyed, "Shutting down");

					return {};
				}
			)
		;
	}

	void CMongoManagerActor::fp_SetStatus(CDistributedAppSensorReporter::EStatusSeverity _Severity, CStr const &_Description)
	{
		if (_Severity == CDistributedAppSensorReporter::EStatusSeverity_Error)
			DMibLogWithCategory(ReplicaStatus, Error, _Description);
		else if (_Severity == CDistributedAppSensorReporter::EStatusSeverity_Warning)
			DMibLogWithCategory(ReplicaStatus, Warning, _Description);
		else if (mp_ReplicaStatusLastSeverity != CDistributedAppSensorReporter::EStatusSeverity_Ok)
			DMibLogWithCategory(ReplicaStatus, Info, _Description);

		mp_ReplicaStatusLastSeverity = _Severity;

		if (!mp_ReplicaStatusReporter || !mp_ReplicaStatusReporter->m_fReportReadings)
		{
			DMibLogWithCategory(ReplicaStatus, Error, "No status reporter to report status to: {}: {}", CDistributedAppSensorReporter::fs_StatusSeverityToString(_Severity), _Description);
			return;
		}

		TCVector<CDistributedAppSensorReporter::CSensorReading> SensorReadings;
		SensorReadings.f_Insert().m_Data = CDistributedAppSensorReporter::CStatus{.m_Severity = _Severity, .m_Description = _Description};;

		mp_ReplicaStatusReporter->m_fReportReadings(fg_Move(SensorReadings)) > fg_LogError("MongoManager", "Error reporting sensor reading");
	}

	TCFuture<void> CMongoManagerActor::fp_ScheduleReplicaStatusChecks()
	{
		if (!mp_ReplicaStatusReporter)
			co_return {};

		auto CheckDependencies = co_await fp_CheckSensorDependencies();

		mp_ReplicaStatusMongoClient = fg_ConstructActor<CMongoClientActor>(fg_Construct("Replica status mongo client connection"), mp_MongoConnectionSettings, "admin");

		co_await fp_UpdateReplicaStatus();

		DMibLogWithCategory(ReplicaStatus, Info, "Initial replica status reported");

		mp_ReplicaStatusTimer = co_await fg_RegisterTimer
			(
				1_minutes
				, [this] -> TCFuture<void>
				{
					co_await fp_UpdateReplicaStatus();

					co_return {};
				}
			)
		;

		co_return {};
	}

	TCFuture<void> CMongoManagerActor::fp_UpdateReplicaStatus()
	{
		auto Result = co_await fp_UpdateReplicaStatusPerform().f_Wrap();

		if (!Result)
			fp_SetStatus(NConcurrency::CDistributedAppSensorReporter::EStatusSeverity_Error, "Error updating replica status: {}"_f << Result.f_GetExceptionStr());

		co_return {};
	}

	namespace
	{
		enum class EReplicaMemberState : uint32
		{
			mc_Startup = 0
			, mc_Primary = 1
			, mc_Secondary = 2
			, mc_Recovering = 3
			, mc_Startup2 = 4
			, mc_Unknown = 5
			, mc_Arbiter = 6
			, mc_Down = 7
			, mc_Rollback = 8
			, mc_Removed = 9
		};

		struct CReplicaMember
		{
			CStr m_Name;
			CTime m_OpTime;
			CStr m_StateStr;
			EReplicaMemberState m_State = EReplicaMemberState::mc_Startup;
		};
	}

	TCFuture<void> CMongoManagerActor::fp_UpdateReplicaStatusPerform()
	{
		auto CheckDependencies = co_await fp_CheckSensorDependencies();

		auto [ConfigResultMutable, StatusResultMutable] = co_await
			(
				mp_ReplicaStatusMongoClient
				(
					&CMongoClientActor::f_RunCommand
					, gc_Str<"admin">.m_Str
					, CEJsonOrdered
					{
						"replSetGetConfig"_o= 1
					}
				)
				+ mp_ReplicaStatusMongoClient
				(
					&CMongoClientActor::f_RunCommand
					, gc_Str<"admin">.m_Str
					, CEJsonOrdered
					{
						"replSetGetStatus"_o= 1
					}
				)
			)
		;

		auto const &ConfigResult = ConfigResultMutable;
		auto const &StatusResult = StatusResultMutable;

		auto Capture = co_await (g_CaptureExceptions % "Error reading replica set status");

		auto nExpectedMembers = ConfigResult["config"]["members"].f_Array().f_GetLen();
		auto &StatusMembers = StatusResult["members"].f_Array();

		if (StatusMembers.f_GetLen() != nExpectedMembers)
		{
			fp_SetStatus
				(
					NConcurrency::CDistributedAppSensorReporter::EStatusSeverity_Error
					, "Status contains {} members while {} were expected"_f << StatusMembers.f_GetLen() << nExpectedMembers
				)
			;
			co_return {};
		}

		auto MajorityCount = StatusResult["majorityVoteCount"].f_UserType().m_Value.f_Integer();

		TCVector<CReplicaMember> Members;
		for (auto &InMember : StatusMembers)
		{
			auto &OutMember = Members.f_Insert();
			OutMember.m_Name = InMember["name"].f_String();
			OutMember.m_State = EReplicaMemberState(InMember["state"].f_UserType().m_Value.f_Integer());
			OutMember.m_StateStr = InMember["stateStr"].f_String();
			OutMember.m_OpTime = CTimeConvert::fs_FromUnixSeconds(InMember["optime"]["ts"].f_UserType().m_Value["Seconds"].f_Integer());
		}

		CStr StateStr;
		CDistributedAppSensorReporter::EStatusSeverity StateSeverity = CDistributedAppSensorReporter::EStatusSeverity_Ok;
		auto fAddSeverity = [&](CDistributedAppSensorReporter::EStatusSeverity _StateSeverity, CStr const &_Description)
			{
				StateSeverity = fg_Max(StateSeverity, _StateSeverity);
				fg_AddStrSep(StateStr, _Description, "\n");
			}
		;

		umint nPrimary = 0;
		umint nSecondary = 0;
		CTime BestOpTime = CTime::fs_StartOfTime();

		for (auto &Member : Members)
		{
			if (Member.m_State == EReplicaMemberState::mc_Primary)
			{
				BestOpTime = fg_Max(BestOpTime, Member.m_OpTime);
				++nPrimary;
			}
			else if (Member.m_State == EReplicaMemberState::mc_Secondary)
			{
				BestOpTime = fg_Max(BestOpTime, Member.m_OpTime);
				++nSecondary;
			}
		}

		if (nPrimary == 0)
			fAddSeverity(CDistributedAppSensorReporter::EStatusSeverity_Error, "No primary");
		else if (nPrimary > 1)
			fAddSeverity(CDistributedAppSensorReporter::EStatusSeverity_Error, "Multiple primaries: {}"_f << nPrimary);

		umint nVotingMembers = nPrimary + nSecondary;
		if (nVotingMembers < MajorityCount)
			fAddSeverity(CDistributedAppSensorReporter::EStatusSeverity_Error, "Not enough voting members. {} / {}"_f << nVotingMembers << MajorityCount);
		else if (nVotingMembers < nExpectedMembers)
			fAddSeverity(CDistributedAppSensorReporter::EStatusSeverity_Warning, "Not all members are up. {} / {}"_f << nVotingMembers << nExpectedMembers);

		if (StateSeverity != CDistributedAppSensorReporter::EStatusSeverity_Ok)
			fAddSeverity(StateSeverity, "");

		for (auto &Member : Members)
		{
			if (Member.m_State == EReplicaMemberState::mc_Primary || Member.m_State == EReplicaMemberState::mc_Secondary)
			{
				auto DiffSeconds = fg_Abs(Member.m_OpTime - BestOpTime).f_GetSecondsFraction();

				if (DiffSeconds > 30_seconds)
				{
					fAddSeverity
						(
							DiffSeconds > 5_minutes ? CDistributedAppSensorReporter::EStatusSeverity_Error : CDistributedAppSensorReporter::EStatusSeverity_Warning
							, "{} - Oplog time is off by {}"_f << Member.m_Name << NTime::fg_SecondsDurationToHumanReadable(DiffSeconds)
						)
					;
				}
			}
			else
				fAddSeverity(CDistributedAppSensorReporter::EStatusSeverity_Warning, "{} - {}"_f << Member.m_Name << Member.m_StateStr);
		}

		if (StateSeverity == CDistributedAppSensorReporter::EStatusSeverity_Ok)
			fAddSeverity(CDistributedAppSensorReporter::EStatusSeverity_Ok, "All members ok. {} / {}"_f << nVotingMembers << nExpectedMembers);

		fp_SetStatus(StateSeverity, StateStr);

		co_return {};
	}
}
