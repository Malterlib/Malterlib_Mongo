// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Concurrency/LogError>

#include "Malterlib_Mongo_App_MongoCertificateManager.h"

namespace NMib::NMongo::NMongoCertificateManager
{
	TCFuture<void> CMongoCertificateManagerActor::fp_SecretsManagerAdded
		(
			TCDistributedActor<CSecretsManager> const &_SecretsManager
			, CTrustedActorInfo const &_Info
		)
	{
		co_await self(&CMongoCertificateManagerActor::fp_Authority_SecretsManagerAdded, _SecretsManager, _Info);
		co_await self(&CMongoCertificateManagerActor::fp_User_SecretsManagerAdded, _SecretsManager, _Info);

		co_return {};
	}

	TCFuture<void> CMongoCertificateManagerActor::fp_SecretsManagerAddedWithRetry(TCDistributedActor<CSecretsManager> const &_SecretsManager, CTrustedActorInfo const &_Info)
	{
		auto Result = co_await self(&CMongoCertificateManagerActor::fp_SecretsManagerAdded, _SecretsManager, _Info).f_Wrap();

		if (Result)
		{
			mp_LastSecretsManagerError.f_Remove(_SecretsManager);

			co_return {};
		}

		auto &LastError = mp_LastSecretsManagerError[_SecretsManager];

		auto Error = Result.f_GetExceptionStr();

		if (Error != LastError)
		{
			DLogWithCategory(Mib/Mongo/MongoCertificateManager, Error, "Failed to handle secrets manager added for '{}' (will retry every 10 seconds): {}", _Info.m_HostInfo, Error);
			LastError = Error;
		}

		if (!mp_SecretsManagerSubscription.m_Actors.f_FindEqual(_SecretsManager))
			co_return {};

		if (mp_RetryingSecretsManagers(_SecretsManager).f_WasCreated())
		{
			fg_Timeout(10.0) > [=]
				{
					mp_RetryingSecretsManagers.f_Remove(_SecretsManager);

					if (!mp_SecretsManagerSubscription.m_Actors.f_FindEqual(_SecretsManager))
						return;

					self(&CMongoCertificateManagerActor::fp_SecretsManagerAddedWithRetry, _SecretsManager, _Info) > fg_LogError("SecretsManager", "Failed to handle secrets manager added");
				}
			;
		}

		co_return {};
	}

	TCFuture<void> CMongoCertificateManagerActor::fp_SecretsManagerRemoved(TCWeakDistributedActor<CActor> const &_SecretsManager, CTrustedActorInfo const &_ActorInfo)
	{
		for (auto &Authority : mp_Authorities)
		{
			Authority.m_SecretsManagers.f_Remove(_SecretsManager);
			if (Authority.m_SecretsManagers.f_IsEmpty())
				fp_Authority_UpdateStatus(Authority, CDistributedAppSensorReporter::EStatusSeverity_Warning, "No secret manager connected");
		}
		for (auto &User : mp_Users)
		{
			User.m_SecretsManagers.f_Remove(_SecretsManager);
			if (User.m_SecretsManagers.f_IsEmpty())
				fp_User_UpdateStatus(User, CDistributedAppSensorReporter::EStatusSeverity_Warning, "No secret manager connected");
		}

		mp_LastSecretsManagerError.f_Remove(_SecretsManager);
		mp_RetryingSecretsManagers.f_Remove(_SecretsManager);

		if (auto *pSubscription = mp_UserSubscriptions.f_FindEqual(_SecretsManager))
		{
			(**pSubscription).f_Destroy() > fg_DiscardResult();
			mp_UserSubscriptions.f_Remove(_SecretsManager);
		}
		
		if (auto *pSubscription = mp_AuthoritySubscriptions.f_FindEqual(_SecretsManager))
		{
			(**pSubscription).f_Destroy() > fg_DiscardResult();
			mp_AuthoritySubscriptions.f_Remove(_SecretsManager);
		}

		co_return {};
	}
}
