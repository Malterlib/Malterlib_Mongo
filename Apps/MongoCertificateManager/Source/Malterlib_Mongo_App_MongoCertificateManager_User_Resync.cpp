// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Mongo_App_MongoCertificateManager.h"

#include <Mib/Encoding/JsonShortcuts>
#include <Mib/CommandLine/TableRenderer>

namespace NMib::NMongo::NMongoCertificateManager
{
	TCFuture<uint32> CMongoCertificateManagerActor::fp_CommandLine_UserResync(CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		auto Auditor = f_Auditor();

		CStr UserName = _Params["User"].f_String();
		CStr AuthorityName = _Params["Authority"].f_String();

		auto AllSecretManagers = fg_VectorFromContainer(mp_SecretsManagerSubscription.m_Actors);

		TCVector<CUserKey> Users;
		for (auto &User : mp_Users)
		{
			auto &UserKey = User.f_GetKey();
			if (!UserName.f_IsEmpty() && UserKey.m_Name != UserName)
				continue;

			if (!AuthorityName.f_IsEmpty() && UserKey.m_Authority != AuthorityName)
				continue;

			auto *pAuthority = mp_Authorities.f_FindEqual(UserKey.m_Authority);

			if (!pAuthority)
			{
				*_pCommandLine %= "Authority '{}' for user '{}' does not exist"_f << AuthorityName << UserKey.m_Name;
				continue;
			}

			Users.f_Insert(UserKey);
		}

		for (auto &UserKey : Users)
		{
			CUser *pUser = nullptr;
			CAuthority *pAuthority = nullptr;

			auto OnResume = co_await fg_OnResume
				(
					[&]() -> NException::CExceptionPointer
					{
						if (mp_State.m_bStoppingApp || f_IsDestroyed())
							return DMibErrorInstance("Startup aborted");

						pUser = mp_Users.f_FindEqual(UserKey);
						if (!pUser)
							return DMibErrorInstance("User '{}' deleted"_f << UserKey);

						pAuthority = mp_Authorities.f_FindEqual(UserKey.m_Authority);
						if (!pAuthority)
							return DMibErrorInstance("Authority '{}' deleted"_f << UserKey.m_Authority);

						return {};
					}
				)
			;

			TCMap<TCWeakDistributedActor<CSecretsManager>, CStr> SecretManagerDescriptions;

			for (auto &SecretManager : AllSecretManagers)
			{
				auto WeakSecretsManager = SecretManager.m_Actor.f_Weak();
				SecretManagerDescriptions[WeakSecretsManager] = "{}"_f << SecretManager.m_TrustInfo.m_HostInfo;
			}

			TCFutureMap<TCWeakDistributedActor<CSecretsManager>, CSecretsManager::CSetSecretPropertiesResult> StoreResultsAsync;

			fp_User_StoreSecrets
				(
					AllSecretManagers
					, *pAuthority
					, UserKey
					, pUser->m_PublicKeySetting
					, pUser->m_Type
					, pUser->m_Created
					, pUser->m_LastModified
					, pUser->m_Certificate
					, StoreResultsAsync
				)
			;

			{
				bool bUpdated = false;
				{
					auto StoreResults = co_await fg_AllDoneWrapped(StoreResultsAsync);

					for (auto &StoreResult : StoreResults)
					{
						auto &WeakSecretsManager = StoreResults.fs_GetKey(StoreResult);

						if (!StoreResult)
						{
							CStr ErrorDescription = "Failed to sync user to secrets manager '{}': {}"_f
								<< SecretManagerDescriptions[WeakSecretsManager]
								<< StoreResult.f_GetExceptionStr()
							;

							DMibLog(Error, "{}", ErrorDescription);
							*_pCommandLine %= "{}\n"_f << ErrorDescription;
							continue;
						}

						pUser->m_SecretsManagers[WeakSecretsManager] = pUser->m_LastModified;
						bUpdated = true;

						if (StoreResult->m_Flags & CSecretsManager::ESetSecretPropertiesResultFlag_Created)
						{
							Auditor.f_Info("Resync MongoDB certificate user: Created {}"_f << UserKey);
							*_pCommandLine %= "Created {} on {}\n"_f << UserKey << SecretManagerDescriptions[WeakSecretsManager];
						}
						else if (StoreResult->m_Flags & CSecretsManager::ESetSecretPropertiesResultFlag_Updated)
						{
							Auditor.f_Info("Resync MongoDB certificate user: Updated {}"_f << UserKey);
							*_pCommandLine %= "Updated {} on {}\n"_f << UserKey << SecretManagerDescriptions[WeakSecretsManager];
						}
					}
				}

				if (bUpdated)
					fp_User_UpdateStatus(*pUser, CDistributedAppSensorReporter::EStatusSeverity_Ok, "OK");
			}
		}

		co_return 0;
	}
}
