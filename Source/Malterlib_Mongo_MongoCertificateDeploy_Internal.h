// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include "Malterlib_Mongo_MongoCertificateDeploy.h"

#include <Mib/Cloud/SecretsManager>
#include <Mib/Concurrency/ActorSubscription>
#include <Mib/Concurrency/ActorSequencerActor>
#include <Mib/Concurrency/LogError>

namespace NMib::NMongo
{
	using namespace NStr;
	using namespace NConcurrency;
	using namespace NContainer;
	using namespace NNetwork;
	using namespace NStorage;
	using namespace NCloud;
	using namespace NException;

	struct CMongoCertificateDeployActor::CInternal : public CActorInternal
	{
		CInternal
			(
				CMongoCertificateDeployActor *_pThis
				, TCActor<CActorDistributionManager> const &_DistributionManager
				, TCActor<CDistributedActorTrustManager> const &_TrustManager
			)
		;

		struct CUserState
		{
			TCDistributedActor<CSecretsManager> m_SecretsManager;
			CHostInfo m_SecretsManagerHostInfo;
		};

		struct CUserKey
		{
			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const
			{
				o_Str += typename tf_CStr::CFormat("{}/{}") << m_Authority << m_Name;
			}

			auto operator <=> (CUserKey const &_Right) const = default;

			CStr m_Authority;
			CStr m_Name;
		};

		struct CUser
		{
			CUserKey const &f_GetKey() const;

			CUserStatus const *f_GetCurrentStatus() const;
			CStr f_GetSecretFolder() const;

			CUserSettings m_Settings;
			TCOptional<CUserState> m_UserState;
			CSequencer m_UserUpdateSequencer{"MongoCertificateDeployActor User UserUpdateSequencer {}"_f << f_GetKey()};
			TCMap<CHostInfo, CUserStatus> m_Statuses;
		};

		struct CSecretsManagerState
		{
			CActorSubscription m_ChangesSubscription;
		};

		TCFuture<void> f_SecretsManagerAddedWithRetry(TCDistributedActor<CSecretsManager> const &_SecretsManager, CTrustedActorInfo const &_Info);
		TCFuture<void> f_SecretsManagerAdded(TCDistributedActor<CSecretsManager> const &_SecretsManager, CTrustedActorInfo const &_Info);
		TCFuture<void> f_SecretsManagerRemoved(TCWeakDistributedActor<CActor> const &_SecretsManager, CTrustedActorInfo const &_ActorInfo);

		void f_UpdateUserStatus(CUser &o_User, CHostInfo const &_HostInfo, EStatusSeverity _Severity, CStr const &_Status);

		[[nodiscard]] NException::CExceptionPointer f_UserUpdate_CheckPreconditions(CUserKey const &_UserKey, CUser *&o_pUser, CUserState *&o_pUserState);
		TCFuture<void> f_UserUpdate_ForSecretsManager(CUserKey const &_UserKey, TCDistributedActor<CSecretsManager> const &_SecretsManager, CHostInfo const &_SecretsManagerHostInfo);
		TCFuture<void> f_UserUpdate_ForAllSecretsManagers(CUserKey const &_UserKey);
		TCFuture<void> f_UserUpdate_AllUsersForAllSecretsManagers();
		CExceptionPointer f_UserUpdate_CheckCertificate(CStrSecure const &_Certificate, CSecretsManager::CSecretID const &_SecretID, CStr const &_Description);
		TCFuture<void> f_UserUpdate_UpdateFiles(CUserKey const &_UserKey, CCertificateFilesSettings const &_FileSettings);
		TCFuture<void> f_UserUpdate(CUserKey const &_UserKey);

		CMongoCertificateDeployActor *m_pThis;
		TCActor<CActorDistributionManager> m_DistributionManager;
		TCActor<CDistributedActorTrustManager> m_TrustManager;
		TCTrustedActorSubscription<CSecretsManager> m_SecretsManagerSubscription;
		TCMap<TCWeakDistributedActor<CActor>, CStr> m_LastSecretsManagerError;
		TCSet<TCWeakDistributedActor<CActor>> m_RetryingSecretsManagers;

		CActorSubscription m_TimerSubscription;

		TCMap<TCWeakDistributedActor<CActor>, CSecretsManagerState> m_SecretsManagerStates;

		TCMap<CUserKey, CUser> m_Users;

		bool m_bStarted = false;
	};
}
