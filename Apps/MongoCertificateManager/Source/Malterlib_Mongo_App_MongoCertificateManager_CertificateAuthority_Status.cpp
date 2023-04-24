// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_App_MongoCertificateManager.h"

#include <Mib/Concurrency/LogError>

namespace NMib::NMongo::NMongoCertificateManager
{
	TCFuture<void> CMongoCertificateManagerActor::fp_Authority_UpdateSensor(CStr _Authority)
	{
		CAuthority *pAuthority = nullptr;

		auto OnResume = co_await fg_OnResume
			(
				[&]() -> NException::CExceptionPointer
				{
					if (mp_State.m_bStoppingApp || f_IsDestroyed())
						return DMibErrorInstance("Startup aborted");

					pAuthority = mp_Authorities.f_FindEqual(_Authority);

					if (!pAuthority)
						return DMibErrorInstance("Certificate authority '{}' deleted"_f << _Authority);

					return {};
				}
			)
		;

		if (!pAuthority->m_bSensorsRegistered)
			co_await fp_Authority_RegisterSensors(_Authority);

		if (!pAuthority->m_SensorReporter_Status.m_fReportReadings)
			co_return {};

		CTime Now = CTime::fs_NowUTC();
		CTime MinExpireTime = Now + CTimeSpanConvert::fs_CreateDaySpan(365);

		CDistributedAppSensorReporter::CStatus Status;
		try
		{
			auto ExpireTime = CCertificate::fs_GetCertificateExpirationTime(pAuthority->m_Certificate.m_Certificate);
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

		co_await pAuthority->m_SensorReporter_Expire.m_fReportReadings(fg_Move(SensorReadings));

		co_return {};
	}

	TCFuture<void> CMongoCertificateManagerActor::fp_Authority_UpdateSensors()
	{
		TCVector<CStr> Authorities;
		for (auto &Authority : mp_Authorities)
			Authorities.f_Insert(Authority.f_GetName());

		for (auto &Name : Authorities)
			fp_Authority_UpdateSensor(Name) > fg_LogError("Update sensors", "Falied to update authority sensors");

		co_return {};
	}

	TCFuture<void> CMongoCertificateManagerActor::fp_Authority_RegisterSensors(CStr _Authority)
	{
		CAuthority *pAuthority = nullptr;
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> NException::CExceptionPointer
				{
					pAuthority = mp_Authorities.f_FindEqual(_Authority);

					if (!pAuthority)
						return DMibErrorInstance("Authority '{}' deleted"_f << _Authority);

					return {};
				}
			)
		;

		if (pAuthority->m_bSensorsRegistered)
			co_return {};

		co_await mp_InitSensorReporterSequencer.f_Sequence();

		if (pAuthority->m_bSensorsRegistered)
			co_return {};

		{
			CDistributedAppSensorReporter::CSensorInfo SensorInfo;
			SensorInfo.m_Identifier = "org.malterlib.mongo.certificate-manager.authority.certificate-expire";
			SensorInfo.m_IdentifierScope = _Authority;
			SensorInfo.m_Name = "Mongo Authority Certificate Expire";
			SensorInfo.m_ExpectedReportInterval = 24_hours;
			SensorInfo.m_Type = NConcurrency::CDistributedAppSensorReporter::ESensorDataType_Status;
			pAuthority->m_SensorReporter_Expire = co_await self(&CMongoCertificateManagerActor::fp_OpenSensorReporter, fg_Move(SensorInfo));
		}
		{
			CDistributedAppSensorReporter::CSensorInfo SensorInfo;
			SensorInfo.m_Identifier = "org.malterlib.mongo.certificate-manager.authority.status";
			SensorInfo.m_IdentifierScope = _Authority;
			SensorInfo.m_Name = "Mongo Authority Status";
			SensorInfo.m_Type = NConcurrency::CDistributedAppSensorReporter::ESensorDataType_Status;
			pAuthority->m_SensorReporter_Status = co_await self(&CMongoCertificateManagerActor::fp_OpenSensorReporter, fg_Move(SensorInfo));
		}

		pAuthority->m_bSensorsRegistered = true;

		co_return {};
	}

	TCFuture<void> CMongoCertificateManagerActor::fp_Authority_UpdateStatusSensor(CStr _Authority, EStatusSeverity _Severity, CStr _Status)
	{
		CAuthority *pAuthority = nullptr;
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> NException::CExceptionPointer
				{
					pAuthority = mp_Authorities.f_FindEqual(_Authority);

					if (!pAuthority)
						return DMibErrorInstance("Authority '{}' deleted"_f << _Authority);

					return {};
				}
			)
		;

		if (!pAuthority->m_bSensorsRegistered)
			co_await fp_Authority_RegisterSensors(_Authority);

		if (!pAuthority->m_SensorReporter_Status.m_fReportReadings)
			co_return {};

		CDistributedAppSensorReporter::CStatus Status;
		Status.m_Severity = _Severity;
		Status.m_Description = _Status;

		TCVector<CDistributedAppSensorReporter::CSensorReading> SensorReadings;
		SensorReadings.f_Insert().m_Data = Status;

		co_await pAuthority->m_SensorReporter_Status.m_fReportReadings(fg_Move(SensorReadings));

		co_return {};
	}

	void CMongoCertificateManagerActor::fp_Authority_UpdateStatus(CAuthority &o_Authority, EStatusSeverity _Severity, CStr const &_Status)
	{
		auto Severity = _Severity;
		auto Status = _Status;
		CTime Modified;
		if (_Severity == CDistributedAppSensorReporter::EStatusSeverity_Ok)
		{
			for (auto &LastModified : o_Authority.m_SecretsManagers)
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

		if (Status != o_Authority.m_Status.m_Description || Severity != o_Authority.m_Status.m_Severity)
		{
			o_Authority.m_Status.m_Description = Status;
			o_Authority.m_Status.m_Severity = Severity;
			DLogWithCategory(Mib/Mongo/MongoCertificateManager, Info, "<{}> Changing certificate authority status: {}", o_Authority.f_GetName(), _Status);

			fp_Authority_UpdateStatusSensor(o_Authority.f_GetName(), Severity, Status) > fg_LogError("Update Status", "Update authority status sensor failed");
		}
	}
}
