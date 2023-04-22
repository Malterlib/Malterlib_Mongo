// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_MongoCertificateDeploy_Internal.h"

namespace NMib::NMongo
{
	using namespace NTime;
	using namespace NMib::NStr;

	CMongoCertificateDeployActor::CCertificateFilesSettings::CCertificateFilesSettings() = default;
	CMongoCertificateDeployActor::CCertificateFilesSettings::~CCertificateFilesSettings() = default;

	CMongoCertificateDeployActor::CUserSettings::CUserSettings() = default;

	void CMongoCertificateDeployActor::CUserSettings::f_InitCommon
		(
			NStr::CStr const &_Authority
			, NStr::CStr const &_Identifier
			, NContainer::TCVector<CDeployLocaction> const &_Locations
		)
	{
		m_Authority = _Authority;
		m_Name = _Identifier;

		for (auto &Location : _Locations)
		{
			auto &FileSettings = m_FilesSettings.f_Insert();
			FileSettings.m_Authority =
				{
					Location.m_BasePath / "MongoCA.crt"
					, Location.m_FileUser
					, Location.m_FileGroup
					, NFile::EFileAttrib_UnixAttributesValid | NFile::EFileAttrib_UserRead | NFile::EFileAttrib_GroupRead | NFile::EFileAttrib_EveryoneRead
				}
			;

			FileSettings.m_Key =
				{
					Location.m_BasePath / ("{}.key"_f << _Identifier)
					, Location.m_FileUser
					, Location.m_FileGroup
					, NFile::EFileAttrib_UnixAttributesValid | NFile::EFileAttrib_UserRead
				}
			;

			FileSettings.m_Certificate =
				{
					Location.m_BasePath / ("{}.crt"_f << _Identifier)
					, Location.m_FileUser
					, Location.m_FileGroup, NFile::EFileAttrib_UnixAttributesValid | NFile::EFileAttrib_UserRead | NFile::EFileAttrib_GroupRead | NFile::EFileAttrib_EveryoneRead
				}
			;

			FileSettings.m_KeyCertificate =
				{
					Location.m_BasePath / ("{}.pem"_f << _Identifier)
					, Location.m_FileUser
					, Location.m_FileGroup
					, NFile::EFileAttrib_UnixAttributesValid | NFile::EFileAttrib_UserRead
				}
			;
		}
	}

	void CMongoCertificateDeployActor::CUserSettings::f_InitUser
		(
			NStr::CStr const &_Authority
			, NStr::CStr const &_UserName
			, NContainer::TCVector<CDeployLocaction> const &_Locations
		)
	{
		m_Type = EUserType_User;

		f_InitCommon(_Authority, _UserName, _Locations);
	}

	void CMongoCertificateDeployActor::CUserSettings::f_InitServer
		(
			NStr::CStr const &_Authority
			, NStr::CStr const &_HostName
			, NContainer::TCVector<CDeployLocaction> const &_Locations
		)
	{
		m_Type = EUserType_Server;

		f_InitCommon(_Authority, _HostName, _Locations);
	}

	CMongoCertificateDeployActor::CInternal::CInternal
		(
			CMongoCertificateDeployActor *_pThis
			, TCActor<CActorDistributionManager> const &_DistributionManager
			, TCActor<CDistributedActorTrustManager> const &_TrustManager
			, TCActor<CSeparateThreadActor> const &_FileActor
		)
		: m_pThis(_pThis)
		, m_DistributionManager(_DistributionManager)
		, m_TrustManager(_TrustManager)
		, m_FileActor(_FileActor)
	{
		if (!m_FileActor)
		{
			m_FileActor = TCActor<CSeparateThreadActor>{fg_Construct(), "Certificate Deploy File Access"};
			m_bOwnsFileActor = true;
		}
	}

	CMongoCertificateDeployActor::CMongoCertificateDeployActor
		(
			TCActor<CActorDistributionManager> const &_DistributionManager
			, TCActor<CDistributedActorTrustManager> const &_TrustManager
			, TCActor<CSeparateThreadActor> const &_FileActor
		)
		: mp_pInternal(fg_Construct(this, _DistributionManager, _TrustManager, _FileActor))
	{
	}

	CMongoCertificateDeployActor::~CMongoCertificateDeployActor() = default;

	TCFuture<void> CMongoCertificateDeployActor::f_Start()
	{
		auto &Internal = *mp_pInternal;

		if (Internal.m_bStarted)
			co_return DMibErrorInstance("Already started");

		Internal.m_bStarted = true;

		Internal.m_SecretsManagerSubscription = co_await Internal.m_TrustManager->f_SubscribeTrustedActors<CSecretsManager>(CSecretsManager::EProtocolVersion_SupportMapSecrets);

		co_await Internal.m_SecretsManagerSubscription.f_OnActor
			(
				g_ActorFunctor / [pInternal = &Internal](TCDistributedActor<CSecretsManager> const &_SecretsManager, CTrustedActorInfo const &_ActorInfo) -> TCFuture<void>
				{
					co_await fg_CallSafe(*pInternal, &CInternal::f_SecretsManagerAddedWithRetry, _SecretsManager, _ActorInfo);

					co_return {};
				}
				, g_ActorFunctor / [pInternal = &Internal](TCWeakDistributedActor<CActor> const &_SecretsManager, CTrustedActorInfo &&_ActorInfo) -> TCFuture<void>
				{
					co_await fg_CallSafe(pInternal, &CInternal::f_SecretsManagerRemoved, _SecretsManager, _ActorInfo);

					co_return {};
				}
				, "Mib/Mongo/MongoCertificateDeploy"
				, "Error when calling {} for secrets manager added"
			)
		;

		// Retry every hour in case permission problems etc have been fixed
		Internal.m_TimerSubscription = co_await fg_RegisterTimer
			(
				CTimeSpanConvert::fs_CreateHourSpan(1).f_GetSeconds()
				, [this]() -> TCFuture<void>
				{
					auto Result = co_await fg_CallSafe(&*mp_pInternal, &CInternal::f_UserUpdate_AllUsersForAllSecretsManagers).f_Wrap();
					if (!Result)
						DMibLogWithCategory(Mib/Mongo/MongoCertificateDeploy, Error, "Update all users had some failures: {}", Result.f_GetExceptionStr());

					co_return {};
				}
			)
		;

		co_return {};
	}

	TCFuture<void> CMongoCertificateDeployActor::fp_Destroy()
	{
		auto &Internal = *mp_pInternal;

		CLogError LogError("Mib/Mongo/MongoCertificateDeploy");

		TCActorResultVector<void> Results;

		for (auto &State : Internal.m_SecretsManagerStates)
		{
			if (State.m_ChangesSubscription)
				fg_Exchange(State.m_ChangesSubscription, nullptr)->f_Destroy() > Results.f_AddResult();
		}

		if (Internal.m_TimerSubscription)
			Internal.m_TimerSubscription->f_Destroy() > Results.f_AddResult();

		Internal.m_SecretsManagerSubscription.f_Destroy() > Results.f_AddResult();
		if (Internal.m_bOwnsFileActor && Internal.m_FileActor)
			Internal.m_FileActor.f_Destroy() > Results.f_AddResult();

		co_await Results.f_GetUnwrappedResults().f_Wrap() > LogError.f_Warning("Failed to destroy certificater deploy actor");

		co_return {};
	}

}
