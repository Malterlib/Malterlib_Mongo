// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_App_MongoCertificateManager.h"

#include <Mib/Concurrency/LogError>

namespace NMib::NMongo::NMongoCertificateManager
{
	TCFuture<void> CMongoCertificateManagerActor::fp_User_UpdateSensor(CUserKey _UserKey)
	{
		CUser *pUser = nullptr;

		auto OnResume = co_await fg_OnResume
			(
				[&]() -> NException::CExceptionPointer
				{
					if (mp_State.m_bStoppingApp || f_IsDestroyed())
						return DMibErrorInstance("Startup aborted");

					pUser = mp_Users.f_FindEqual(_UserKey);

					if (!pUser)
						return DMibErrorInstance("Certificate user '{}' deleted"_f << _UserKey);

					return {};
				}
			)
		;

		if (!pUser->m_bSensorsRegistered)
			co_await fp_User_RegisterSensors(_UserKey);

		if (!pUser->m_SensorReporter_Status.m_fReportReadings)
			co_return {};

		CTime Now = CTime::fs_NowUTC();
		CTime MinExpireTime = Now + CTimeSpanConvert::fs_CreateDaySpan(365);

		CDistributedAppSensorReporter::CStatus Status;
		try
		{
			auto ExpireTime = CCertificate::fs_GetCertificateExpirationTime(pUser->m_Certificate.m_Certificate);
			if (ExpireTime < Now)
			{
				Status.m_Severity = CDistributedAppSensorReporter::EStatusSeverity_Error;
				Status.m_Description = "Error: Certificate expired {} ago"_f << fg_SecondsDurationToHumanReadable(CTimeSpanConvert(Now - ExpireTime).f_GetSecondsFloat());
			}
			else if (ExpireTime < MinExpireTime)
			{
				Status.m_Severity = CDistributedAppSensorReporter::EStatusSeverity_Warning;
				Status.m_Description = "Warning: certificate will expire in {}"_f << fg_SecondsDurationToHumanReadable(CTimeSpanConvert(ExpireTime - Now).f_GetSecondsFloat());
			}
			else
			{
				Status.m_Severity = CDistributedAppSensorReporter::EStatusSeverity_Ok;
				Status.m_Description = "Certificate will expire in {}"_f << fg_SecondsDurationToHumanReadable(CTimeSpanConvert(ExpireTime - Now).f_GetSecondsFloat());
			}
		}
		catch (CException const &_Exception)
		{
			Status.m_Severity = CDistributedAppSensorReporter::EStatusSeverity_Error;
			Status.m_Description = "Error checkng certificate expiry: {}"_f << _Exception;
		}

		TCVector<CDistributedAppSensorReporter::CSensorReading> SensorReadings;
		SensorReadings.f_Insert().m_Data = Status;

		co_await pUser->m_SensorReporter_Expire.m_fReportReadings(fg_Move(SensorReadings));

		co_return {};
	}

	TCFuture<void> CMongoCertificateManagerActor::fp_User_UpdateSensors()
	{
		TCVector<CUserKey> Users;
		for (auto &User : mp_Users)
			Users.f_Insert(User.f_GetKey());

		for (auto &UserKey : Users)
			fp_User_UpdateSensor(UserKey) > fg_LogError("Update sensors", "Falied to update user sensors");

		co_return {};
	}

	TCFuture<void> CMongoCertificateManagerActor::fp_User_RegisterSensors(CUserKey _UserKey)
	{
		CUser *pUser = nullptr;
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> NException::CExceptionPointer
				{
					pUser = mp_Users.f_FindEqual(_UserKey);

					if (!pUser)
						return DMibErrorInstance("User '{}' deleted"_f << _UserKey);

					return {};
				}
			)
		;

		if (pUser->m_bSensorsRegistered)
			co_return {};

		co_await mp_InitSensorReporterSequencer.f_Sequence();

		if (pUser->m_bSensorsRegistered)
			co_return {};

		{
			CDistributedAppSensorReporter::CSensorInfo SensorInfo;
			SensorInfo.m_Identifier = "org.malterlib.mongo.certificate-manager.user.certificate-expire";
			SensorInfo.m_IdentifierScope = "{}"_f << _UserKey;
			SensorInfo.m_Name = "Mongo User Certificate Expire";
			SensorInfo.m_ExpectedReportInterval = 24.0 * 60.0 * 60.0;
			SensorInfo.m_Type = NConcurrency::CDistributedAppSensorReporter::ESensorDataType_Status;
			pUser->m_SensorReporter_Expire = co_await self(&CMongoCertificateManagerActor::fp_OpenSensorReporter, fg_Move(SensorInfo));
		}
		{
			CDistributedAppSensorReporter::CSensorInfo SensorInfo;
			SensorInfo.m_Identifier = "org.malterlib.mongo.certificate-manager.user.status";
			SensorInfo.m_IdentifierScope = "{}"_f << _UserKey;
			SensorInfo.m_Name = "Mongo User Status";
			SensorInfo.m_Type = NConcurrency::CDistributedAppSensorReporter::ESensorDataType_Status;
			pUser->m_SensorReporter_Status = co_await self(&CMongoCertificateManagerActor::fp_OpenSensorReporter, fg_Move(SensorInfo));
		}

		pUser->m_bSensorsRegistered = true;

		co_return {};
	}

	TCFuture<void> CMongoCertificateManagerActor::fp_User_UpdateStatusSensor(CUserKey _UserKey, EStatusSeverity _Severity, CStr _Status)
	{
		CUser *pUser = nullptr;
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> NException::CExceptionPointer
				{
					pUser = mp_Users.f_FindEqual(_UserKey);

					if (!pUser)
						return DMibErrorInstance("User '{}' deleted"_f << _UserKey);

					return {};
				}
			)
		;

		if (!pUser->m_bSensorsRegistered)
			co_await fp_User_RegisterSensors(_UserKey);

		if (!pUser->m_SensorReporter_Status.m_fReportReadings)
			co_return {};

		CDistributedAppSensorReporter::CStatus Status;
		Status.m_Severity = _Severity;
		Status.m_Description = _Status;

		TCVector<CDistributedAppSensorReporter::CSensorReading> SensorReadings;
		SensorReadings.f_Insert().m_Data = Status;

		co_await pUser->m_SensorReporter_Status.m_fReportReadings(fg_Move(SensorReadings));

		co_return {};
	}

	void CMongoCertificateManagerActor::fp_User_UpdateStatus(CUser &o_User, EStatusSeverity _Severity, CStr const &_Status)
	{
		auto Severity = _Severity;
		auto Status = _Status;
		CTime Modified;
		if (_Severity == CDistributedAppSensorReporter::EStatusSeverity_Ok)
		{
			for (auto &LastModified : o_User.m_SecretsManagers)
			{
				if (!Modified.f_IsValid())
					Modified = LastModified;

				if (LastModified != Modified)
				{
					Severity = CDistributedAppSensorReporter::EStatusSeverity_Warning;
					Status = "Not all managers are up to date";
					break;
				}
			}
		}

		if (Status != o_User.m_Status.m_Description || Severity != o_User.m_Status.m_Severity)
		{
			o_User.m_Status.m_Description = Status;
			o_User.m_Status.m_Severity = Severity;
			fp_User_UpdateStatusSensor(o_User.f_GetKey(), Severity, Status) > fg_LogError("Update Status", "Update user status sensor failed");

			DLogWithCategory(Mib/Mongo/MongoCertificateManager, Info, "<{}> Changing certificate authority status: {}", o_User.f_GetKey(), _Status);
		}
	}
}
