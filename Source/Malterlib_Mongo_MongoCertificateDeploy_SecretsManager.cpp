// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_MongoCertificateDeploy_Internal.h"

namespace NMib::NMongo
{
	TCFuture<void> CMongoCertificateDeployActor::CInternal::f_SecretsManagerAddedWithRetry(TCDistributedActor<CSecretsManager> const &_SecretsManager, CTrustedActorInfo const &_Info)
	{
		auto Result = co_await fg_CallSafe(this, &CInternal::f_SecretsManagerAdded, _SecretsManager, _Info).f_Wrap();

		if (Result)
		{
			m_LastSecretsManagerError.f_Remove(_SecretsManager);
			co_return {};
		}

		auto &LastError = m_LastSecretsManagerError[_SecretsManager];

		auto Error = Result.f_GetExceptionStr();

		if (Error != LastError)
		{
			DMibLogWithCategory
				(
					Mib/Mongo/MongoCertificateDeploy
					, Error
					, "Failed to handle secrets manager added for '{}' (will retry every 10 seconds): {}"
					, _Info.m_HostInfo
					, Error
				)
			;
			LastError = Error;
			for (auto &User : m_Users)
				f_UpdateUserStatus(User, _Info.m_HostInfo, EStatusSeverity_Error, Error);
		}

		if (!m_SecretsManagerSubscription.m_Actors.f_FindEqual(_SecretsManager))
			co_return {};

		if (m_RetryingSecretsManagers(_SecretsManager).f_WasCreated())
		{
			fg_Timeout(10.0) > [=]
				{
					m_RetryingSecretsManagers.f_Remove(_SecretsManager);

					if (!m_SecretsManagerSubscription.m_Actors.f_FindEqual(_SecretsManager))
						return;

					fg_CallSafe(this, &CInternal::f_SecretsManagerAddedWithRetry, _SecretsManager, _Info)
						> fg_LogError("Mib/Mongo/MongoCertificateDeploy", "Failed to handle secret manager added (retry)")
					;
				}
			;
		}

		co_return {};
	}

	TCFuture<void> CMongoCertificateDeployActor::CInternal::f_SecretsManagerAdded(TCDistributedActor<CSecretsManager> const &_SecretsManager, CTrustedActorInfo const &_Info)
	{
		TCActorResultMap<CStr, void> UpdateResults;

		auto OnResume = g_OnResume / [&]
			{
				if (m_pThis->f_IsDestroyed())
					DMibError("Shutting down");

				if (!m_SecretsManagerSubscription.m_Actors.f_FindEqual(_SecretsManager))
					DMibError("Secrets manager removed");
			}
		;

		CSecretsManager::CSubscribeToChanges SubscribeToChanges;
		SubscribeToChanges.m_SemanticID = "org.malterlib.mongo.user#*";
		SubscribeToChanges.m_fOnChanges = g_ActorFunctor / [this, _SecretsManager, _Info](CSecretsManager::CSecretChanges &&_Changes) mutable -> TCFuture<void>
			{
				if (m_pThis->f_IsDestroyed())
					co_return {};

				if (!m_SecretsManagerSubscription.m_Actors.f_FindEqual(_SecretsManager))
					co_return {};

				constexpr static ch8 const *c_pUserSemanticPrefix = "org.malterlib.mongo.user#";

				TCSet<CUserKey> UsersToUpdate;
				for (auto &Properties : _Changes.m_Changed)
				{
					auto &SecretID = _Changes.m_Changed.fs_GetKey(Properties);
					if (!Properties.m_SemanticID)
						continue;

					if (!Properties.m_SemanticID->f_StartsWith(c_pUserSemanticPrefix))
					{
						DMibLogWithCategory(Mib/Mongo/MongoCertificateDeploy, Warning, "{}: Invalid semantic ID received: {}", _Info.m_HostInfo, *Properties.m_SemanticID);
						continue;
					}

					auto AuthorityAndNameStr = Properties.m_SemanticID->f_RemovePrefix(c_pUserSemanticPrefix);
					auto AuthorityAndName = AuthorityAndNameStr.f_Split("#");

					if (AuthorityAndName.f_GetLen() != 2)
					{
						DMibLogWithCategory(Mib/Mongo/MongoCertificateDeploy, Warning, "{}: Invalid user semantic ID for secret '{}': {}", _Info.m_HostInfo, SecretID, AuthorityAndNameStr);
						co_return {};
					}

					CUserKey UserKey;
					UserKey.m_Authority = AuthorityAndName[0];
					UserKey.m_Name = AuthorityAndName[1];

					if (!fg_IsValidHostname(UserKey.m_Authority))
					{
						DMibLogWithCategory(Mib/Mongo/MongoCertificateDeploy, Warning, "{}: Invalid authority semantic ID for secret '{}'", _Info.m_HostInfo, SecretID);
						co_return {};
					}

					if (!fg_IsValidHostname(UserKey.m_Name))
					{
						DMibLogWithCategory(Mib/Mongo/MongoCertificateDeploy, Warning, "{}: Invalid user semantic ID for secret '{}'", _Info.m_HostInfo, SecretID);
						co_return {};
					}

					if (AuthorityAndNameStr != SecretID.m_Name)
					{
						DMibLogWithCategory
							(
								Mib/Mongo/MongoCertificateDeploy
								, Warning
								, "{}: User/Authority name doesn't match semantic ID for secret '{}': {}"
								, _Info.m_HostInfo
								, SecretID
								, AuthorityAndNameStr
							)
						;
						co_return {};
					}

					if (!Properties.m_Secret)
					{
						DMibLogWithCategory(Mib/Mongo/MongoCertificateDeploy, Warning, "{}: Missing secret value for secret '{}'", _Info.m_HostInfo, SecretID);
						co_return {};
					}

					if (Properties.m_Secret->f_GetTypeID() != CSecretsManager::ESecretType_StringMap)
					{
						DMibLogWithCategory(Mib/Mongo/MongoCertificateDeploy, Warning, "{}: Secret value is of wrong type (expected string map) for secret '{}'", _Info.m_HostInfo, SecretID);
						co_return {};
					}

					auto &Secrets = Properties.m_Secret->f_Get<CSecretsManager::ESecretType_StringMap>();

					if (!Secrets.f_FindEqual("PrivateKey"))
					{
						DMibLogWithCategory(Mib/Mongo/MongoCertificateDeploy, Warning, "{}: Secret value is missing 'PrivateKey' for secret '{}'", _Info.m_HostInfo, SecretID);
						co_return {};
					}

					if (!Secrets.f_FindEqual("Certificate"))
					{
						DMibLogWithCategory(Mib/Mongo/MongoCertificateDeploy, Warning, "{}: Secret value is missing 'Certificate' for secret '{}'", _Info.m_HostInfo, SecretID);
						co_return {};
					}

					if (!Secrets.f_FindEqual("CA"))
					{
						DMibLogWithCategory(Mib/Mongo/MongoCertificateDeploy, Warning, "{}: Secret value is missing 'CA' for secret '{}'", _Info.m_HostInfo, SecretID);
						co_return {};
					}

					if (!m_Users.f_FindEqual(UserKey))
					{
						DMibLogWithCategory
							(
								Mib/Mongo/MongoCertificateDeploy
								, Warning
								, "Certificate deploy manager has access to a user '{}' certificate that it shouldn't have access to. Secrets manager: {}"
								, UserKey
								, _Info.m_HostInfo
							)
						;
						continue;
					}

					UsersToUpdate[UserKey];
				}

				for (auto &UserKey : UsersToUpdate)
				{
					co_await fg_CallSafe(this, &CInternal::f_UserUpdate_ForSecretsManager, UserKey, _SecretsManager, _Info.m_HostInfo).f_Wrap()
						> fg_LogError("Mib/Mongo/MongoCertificateDeploy", "Update user '{}' for secrets manager '{}' failed"_f << UserKey << _Info.m_HostInfo)
					;
				}

				co_return {};
			}
		;

		auto ChangesSubscription = co_await (_SecretsManager.f_CallActor(&CSecretsManager::f_SubscribeToChanges)(fg_Move(SubscribeToChanges)) % "Subscribe to secret changes");

		m_SecretsManagerStates[_SecretsManager].m_ChangesSubscription = fg_Move(ChangesSubscription);

		co_return {};
	}

	TCFuture<void> CMongoCertificateDeployActor::CInternal::f_SecretsManagerRemoved(TCWeakDistributedActor<CActor> const &_SecretsManager, CTrustedActorInfo const &_ActorInfo)
	{
		m_LastSecretsManagerError.f_Remove(_SecretsManager);
		m_RetryingSecretsManagers.f_Remove(_SecretsManager);

		auto *pSecretsManagerState = m_SecretsManagerStates.f_FindEqual(_SecretsManager);

		if (pSecretsManagerState && pSecretsManagerState->m_ChangesSubscription)
		{
			auto Subscription = fg_Exchange(pSecretsManagerState->m_ChangesSubscription, nullptr);

			m_SecretsManagerStates.f_Remove(pSecretsManagerState);

			co_await Subscription->f_Destroy().f_Wrap();
		}

		TCActorResultVector<void> UserUpdateResults;
		for (auto &User : m_Users)
		{
			if (!User.m_UserState)
				continue;

			if (User.m_UserState->m_SecretsManager == _SecretsManager)
			{
				User.m_UserState.f_Clear();

				f_UpdateUserStatus(User, _ActorInfo.m_HostInfo, EStatusSeverity_Warning, "Lost active secrets manager, waiting");

				fg_CallSafe(this, &CInternal::f_UserUpdate_ForAllSecretsManagers, User.f_GetKey()) > UserUpdateResults.f_AddResult();
			}
			else
				f_UpdateUserStatus(User, _ActorInfo.m_HostInfo, EStatusSeverity_Warning, "Lost secrets manager");
		}

		co_await UserUpdateResults.f_GetResults() | g_Unwrap;

		co_return {};
	}
}
