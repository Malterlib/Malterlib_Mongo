// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_MongoCertificateDeploy_Internal.h"

namespace NMib::NMongo
{
	TCFuture<CActorSubscription> CMongoCertificateDeployActor::f_AddUser(CUserSettings _UserSettings)
	{
		auto &Internal = *mp_pInternal;

		CInternal::CUserKey UserKey;
		UserKey.m_Authority = _UserSettings.m_Authority;
		UserKey.m_Name = _UserSettings.m_Name;

		auto Mapped = Internal.m_Users(UserKey);

		if (!Mapped.f_WasCreated())
			co_return DMibErrorInstance("User already added: {}"_f << UserKey);

		auto &User = *Mapped;

		User.m_Settings = fg_Move(_UserSettings);

		Internal.f_UserUpdate_ForAllSecretsManagers(UserKey)
			> fg_LogError("Mib/Mongo/MongoCertificateDeploy", "Update User '{}' for all secrets managers had some failures"_f << UserKey)
		;

		co_return g_ActorSubscription / [pInternal = &Internal, UserKey]() -> TCFuture<void>
			{
				if (auto *pUser = pInternal->m_Users.f_FindEqual(UserKey))
				{
					co_await fg_Move(pUser->m_UserUpdateSequencer).f_Destroy().f_Wrap()
						> fg_LogWarning("Mib/Mongo/MongoCertificateDeploy", "Failed to destroy user update sequencer")
					;
					pInternal->m_Users.f_Remove(UserKey);
				}
				co_return {};
			}
		;
	}

	void CMongoCertificateDeployActor::CInternal::f_UpdateUserStatus(CUser &o_User, CHostInfo const &_HostInfo, EStatusSeverity _Severity, CStr const &_Status)
	{
		auto &Status = o_User.m_Statuses[_HostInfo];
		auto PreviousStatus = Status;
		Status.m_Description = _Status;
		Status.m_Severity = _Severity;
		if (Status != PreviousStatus && o_User.m_Settings.m_fOnStatusChange)
			o_User.m_Settings.m_fOnStatusChange(_HostInfo, Status) > fg_LogError("Mib/Mongo/MongoCertificateDeploy", "On status change failed");
	}

	auto CMongoCertificateDeployActor::CInternal::CUser::f_GetKey() const -> CUserKey const &
	{
		return TCMap<CUserKey, CUser>::fs_GetKey(*this);
	}

	CStr CMongoCertificateDeployActor::CInternal::CUser::f_GetSecretFolder() const
	{
		return "org.malterlib.mongo.certificate/{}/{}"_f << f_GetKey().m_Authority << f_GetKey().m_Name;
	}

	auto CMongoCertificateDeployActor::CInternal::CUser::f_GetCurrentStatus() const -> CUserStatus const *
	{
		if (!m_UserState)
			return nullptr;

		return m_Statuses.f_FindEqual(m_UserState->m_SecretsManagerHostInfo);
	}
}
