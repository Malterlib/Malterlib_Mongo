// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_App_MongoCertificateManager.h"

#include <Mib/Encoding/JsonShortcuts>
#include <Mib/CommandLine/TableRenderer>

namespace NMib::NMongo::NMongoCertificateManager
{
	TCFuture<uint32> CMongoCertificateManagerActor::fp_CommandLine_AuthorityResync(CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		auto Auditor = f_Auditor();

		CStr AuthorityName = _Params["Authority"].f_String();

		auto AllSecretManagers = fg_VectorFromContainer(mp_SecretsManagerSubscription.m_Actors);

		TCVector<CStr> Authorities;
		for (auto &Authority : mp_Authorities)
		{
			auto &Name = Authority.f_GetName();
			if (!AuthorityName.f_IsEmpty() && Name != AuthorityName)
				continue;

			Authorities.f_Insert(Name);
		}

		for (auto &Name : Authorities)
		{
			CAuthority *pAuthority = nullptr;

			auto OnResume = co_await fg_OnResume
				(
					[&]() -> NException::CExceptionPointer
					{
						if (mp_State.m_bStoppingApp || f_IsDestroyed())
							return DMibErrorInstance("Startup aborted");

						pAuthority = mp_Authorities.f_FindEqual(Name);

						if (!pAuthority)
							return DMibErrorInstance("Certificate authority '{}' deleted"_f << Name);

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

			fp_Authority_StoreSecrets
				(
					AllSecretManagers
					, Name
					, pAuthority->m_Serial
					, pAuthority->m_PublicKeySetting
					, pAuthority->m_Created
					, pAuthority->m_LastModified
					, pAuthority->m_Certificate
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
							CStr ErrorDescription = "Failed to sync certificate authority to secrets manager '{}': {}"_f
								<< SecretManagerDescriptions[WeakSecretsManager]
								<< StoreResult.f_GetExceptionStr()
							;

							DMibLog(Error, "{}", ErrorDescription);
							*_pCommandLine %= "{}\n"_f << ErrorDescription;
							continue;
						}

						pAuthority->m_SecretsManagers[WeakSecretsManager] = pAuthority->m_LastModified;
						bUpdated = true;

						if (StoreResult->m_Flags & CSecretsManager::ESetSecretPropertiesResultFlag_Created)
						{
							Auditor.f_Info("Resync MongoDB authority: Created {}"_f << Name);
							*_pCommandLine %= "Created {} on {}\n"_f << Name << SecretManagerDescriptions[WeakSecretsManager];
						}
						else if (StoreResult->m_Flags & CSecretsManager::ESetSecretPropertiesResultFlag_Updated)
						{
							Auditor.f_Info("Resync MongoDB authority: Updated {}"_f << Name);
							*_pCommandLine %= "Updated {} on {}\n"_f << Name << SecretManagerDescriptions[WeakSecretsManager];
						}
					}
				}

				if (bUpdated)
					fp_Authority_UpdateStatus(*pAuthority, CDistributedAppSensorReporter::EStatusSeverity_Ok, "OK");
			}
		}

		co_return 0;
	}
}
