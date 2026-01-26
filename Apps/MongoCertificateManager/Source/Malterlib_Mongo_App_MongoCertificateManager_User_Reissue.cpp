// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_App_MongoCertificateManager.h"

#include <Mib/Encoding/JsonShortcuts>
#include <Mib/CommandLine/TableRenderer>

namespace NMib::NMongo::NMongoCertificateManager
{
	TCFuture<uint32> CMongoCertificateManagerActor::fp_CommandLine_UserReissue(CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		CStr UserName = _Params["User"].f_String();
		CStr AuthorityName = _Params["Authority"].f_String();
		int64 Days = _Params["Days"].f_Integer();

		CTime MinExpireTime = CTime::fs_NowUTC() + CTimeSpanConvert::fs_CreateDaySpan(Days);

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

			NTime::CTime ExpireTime;
			try
			{
				ExpireTime = CCertificate::fs_GetCertificateExpirationTime(pUser->m_Certificate.m_Certificate);
			}
			catch (CException const &_Exception)
			{
				*_pCommandLine %= "Failed to get certificate expire time for user '{}': {}\n"_f << UserKey << _Exception;
				continue;
			}

			if (ExpireTime >= MinExpireTime)
			{
				*_pCommandLine %= "{}: Already up to date. Will expire in {} days\n"_f << UserKey << CTimeSpanConvert(ExpireTime - CTime::fs_NowUTC()).f_GetDays();
				continue;
			}

			if (!AuthorityName.f_IsEmpty() && UserKey.m_Authority != AuthorityName)
				continue;

			TCMap<TCWeakDistributedActor<CSecretsManager>, CStr> SecretManagerDescriptions;

			for (auto &SecretManager : AllSecretManagers)
			{
				auto WeakSecretsManager = SecretManager.m_Actor.f_Weak();
				SecretManagerDescriptions[WeakSecretsManager] = "{}"_f << SecretManager.m_TrustInfo.m_HostInfo;
			}

			TCFutureMap<TCWeakDistributedActor<CSecretsManager>, CSecretsManager::CSetSecretPropertiesResult> StoreResultsAsync;

			auto UserCertificate = co_await fp_GenerateUserCertificate(pAuthority->m_Certificate, pUser->m_PublicKeySetting, pUser->f_GetKey().m_Name, pUser->m_Type);

			CTime LastModified = CTime::fs_NowUTC();

			fp_User_StoreSecrets
				(
					AllSecretManagers
					, *pAuthority
					, UserKey
					, pUser->m_PublicKeySetting
					, pUser->m_Type
					, pUser->m_Created
					, LastModified
					, UserCertificate
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
							CStr ErrorDescription = "{}: Failed to sync user to secrets manager: {}"_f
								<< SecretManagerDescriptions[WeakSecretsManager]
								<< StoreResult.f_GetExceptionStr()
							;

							DMibLog(Error, "{}", ErrorDescription);
							*_pCommandLine %= "{}\n"_f << ErrorDescription;
							continue;
						}

						pUser->m_SecretsManagers[WeakSecretsManager] = LastModified;
						bUpdated = true;

						if (StoreResult->m_Flags & CSecretsManager::ESetSecretPropertiesResultFlag_Created)
							*_pCommandLine %= "{}: Created on {}\n"_f << UserKey << SecretManagerDescriptions[WeakSecretsManager];
						else if (StoreResult->m_Flags & CSecretsManager::ESetSecretPropertiesResultFlag_Updated)
							*_pCommandLine %= "{}: Updated on {}\n"_f << UserKey << SecretManagerDescriptions[WeakSecretsManager];
					}
				}

				if (bUpdated)
					fp_User_UpdateStatus(*pUser, CDistributedAppSensorReporter::EStatusSeverity_Ok, "OK");
			}
		}

		co_return 0;
	}
}
