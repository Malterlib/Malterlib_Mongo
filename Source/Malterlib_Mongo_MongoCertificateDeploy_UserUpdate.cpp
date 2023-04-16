// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_MongoCertificateDeploy_Internal.h"

#include <Mib/Cryptography/Certificate>

namespace NMib::NMongo
{
	using namespace NCryptography;
	using namespace NTime;
	using namespace NFile;

	TCFuture<void> CMongoCertificateDeployActor::CInternal::f_UserUpdate_AllUsersForAllSecretsManagers()
	{
		TCVector<CUserKey> UserKeys;
		for (auto &User : m_Users)
		{
			if (User.f_GetCurrentStatus() && User.f_GetCurrentStatus()->m_Severity == EStatusSeverity_Success)
				continue;

			UserKeys.f_Insert(User.f_GetKey());
		}

		if (UserKeys.f_IsEmpty())
			co_return {};

		DMibLogWithCategory(Mib/Mongo/MongoCertificateDeploy, Info, "Updating out of date users: {vs}", UserKeys);

		TCActorResultVector<void> Results;

		for (auto &UserKey : UserKeys)
			fg_CallSafe(this, &CInternal::f_UserUpdate_ForAllSecretsManagers, UserKey) > Results.f_AddResult();

		co_await (co_await Results.f_GetResults() | g_Unwrap);

		co_return {};
	}

	TCFuture<void> CMongoCertificateDeployActor::CInternal::f_UserUpdate_ForSecretsManager
		(
			CUserKey const &_UserKey
			, TCDistributedActor<CSecretsManager> const &_SecretsManager
			, CHostInfo const &_SecretsManagerHostInfo
		)
	{
		CUser *pUser = nullptr;

		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (m_pThis->f_IsDestroyed())
						return DMibErrorInstance("Shutting down");

					pUser = m_Users.f_FindEqual(_UserKey);
					if (!pUser)
						return DMibErrorInstance("User no longer exists: {}"_f << _UserKey);

					if (!m_SecretsManagerSubscription.m_Actors.f_FindEqual(_SecretsManager))
						return DMibErrorInstance("Secret manager no longer exists");

					return {};
				}
			)
		;

		CUserState UserState;
		UserState.m_SecretsManager = _SecretsManager;
		UserState.m_SecretsManagerHostInfo = _SecretsManagerHostInfo;

		auto UpdateResult = co_await pUser->m_UserUpdateSequencer.f_RunSequenced
			(
				g_ActorFunctorWeak /
				[
					this
					, _UserKey
					, UserState = fg_Move(UserState)
				]
				(CActorSubscription &&_Subscription) mutable -> TCFuture<void>
				{
					auto *pUser = m_Users.f_FindEqual(_UserKey);

					if (!pUser)
						co_return {};

					if (!m_SecretsManagerSubscription.m_Actors.f_FindEqual(UserState.m_SecretsManager))
						co_return {};

					auto &User = *pUser;

					if (auto *pCurrentStatus = User.f_GetCurrentStatus())
					{
						if (pCurrentStatus->m_Severity == EStatusSeverity_Success && UserState.m_SecretsManager != User.m_UserState->m_SecretsManager)
						{
							f_UpdateUserStatus(User, UserState.m_SecretsManagerHostInfo, EStatusSeverity_Info, "Aborted, another secrets manager already succeeded");
							co_return {};
						}
					}

					User.m_UserState = fg_Move(UserState);

					co_await
						(
							fg_CallSafe(this, &CInternal::f_UserUpdate, User.f_GetKey())
							% ("Failed to update user '{}'"_f << User.f_GetKey())
						)
					;

					(void)_Subscription;

					co_return {};
				}
			)
			.f_Wrap()
		;

		if (!UpdateResult)
			f_UpdateUserStatus(*pUser, _SecretsManagerHostInfo, EStatusSeverity_Error, "Error updating user: {}"_f << UpdateResult.f_GetExceptionStr());

		co_return {};
	}

	TCFuture<void> CMongoCertificateDeployActor::CInternal::f_UserUpdate_ForAllSecretsManagers(CUserKey const &_UserKey)
	{
		CUser *pUser = nullptr;

		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (m_pThis->f_IsDestroyed())
						return DMibErrorInstance("Shutting down");

					pUser = m_Users.f_FindEqual(_UserKey);
					if (!pUser)
						return DMibErrorInstance("User no longer exists: {}"_f << _UserKey);

					return {};
				}
			)
		;

		TCActorResultVector<void> UpdateResults;
		for (auto &SecretsManager : m_SecretsManagerSubscription.m_Actors)
			fg_CallSafe(this, &CInternal::f_UserUpdate_ForSecretsManager, _UserKey, SecretsManager.m_Actor, SecretsManager.m_TrustInfo.m_HostInfo) > UpdateResults.f_AddResult();

		co_await (co_await UpdateResults.f_GetResults() | g_Unwrap);

		co_return {};
	}

	[[nodiscard]] CExceptionPointer CMongoCertificateDeployActor::CInternal::f_UserUpdate_CheckPreconditions(CUserKey const &_UserKey, CUser *&o_pUser, CUserState *&o_pUserState)
	{
		if (m_pThis->f_IsDestroyed())
			return DMibErrorInstance("Shutting down");

		o_pUser = m_Users.f_FindEqual(_UserKey);
		if (!o_pUser)
			return DMibErrorInstance("User no longer exists: {}"_f << _UserKey);

		if (!o_pUser->m_UserState)
			return DMibErrorInstance("User no longer connected to secrets manager");

		o_pUserState = &*o_pUser->m_UserState;

		return {};
	}

	CExceptionPointer CMongoCertificateDeployActor::CInternal::f_UserUpdate_CheckCertificate
		(
			CStrSecure const &_Certificate
			, CSecretsManager::CSecretID const &_SecretID
			, CStr const &_Description
		)
	{
		try
		{
			CByteVector CertificateData((uint8 const *)_Certificate.f_GetStr(), _Certificate.f_GetLen());

			auto IssueTime = CCertificate::fs_GetCertificateIssueTime(CertificateData);
			auto ExpirationTime = CCertificate::fs_GetCertificateExpirationTime(CertificateData);
			auto Now = CTime::fs_NowUTC();

			if (IssueTime > Now)
				return DMibErrorInstance("{} is not yet valid"_f << _Description).f_ExceptionPointer();

			if (ExpirationTime < Now)
				return DMibErrorInstance("{} has expired"_f << _Description).f_ExceptionPointer();
		}
		catch (CException const &_Exception)
		{
			return DMibErrorInstance("Exception checking certificate expiration time: {}"_f << _Exception).f_ExceptionPointer();
		}

		return nullptr;
	}

	TCFuture<void> CMongoCertificateDeployActor::CInternal::f_UserUpdate_UpdateFiles(CUserKey const &_UserKey, CCertificateFilesSettings const &_FileSettings)
	{
		CUser *pUser = nullptr;
		CUserState *pUserState = nullptr;
		CHostInfo *pHostInfo = nullptr;

		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (auto pException = f_UserUpdate_CheckPreconditions(_UserKey, pUser, pUserState))
						return pException;

					pHostInfo = &pUserState->m_SecretsManagerHostInfo;

					return {};
				}
			)
		;

		CSecretsManager::CSecretID SecretID;
		SecretID.m_Folder = "org.malterlib.mongo.user";
		SecretID.m_Name = "{}#{}"_f << _UserKey.m_Authority << _UserKey.m_Name;
		
		auto Secret = co_await
			(
				(
					pUserState->m_SecretsManager.f_CallActor(&CSecretsManager::f_GetSecret)(SecretID)
					% ("Get secret properties for {}"_f << SecretID)
				)
				.f_Dispatch()
			)
		;

		if (Secret.f_GetTypeID() != CSecretsManager::ESecretType_StringMap)
		{
			DMibLogWithCategory(Mib/Mongo/MongoCertificateDeploy, Warning, "{}: Secret is of wrong type (expected string map) for secret '{}'", pHostInfo, SecretID);
			co_return {};
		}

		auto &Secrets = Secret.f_Get<CSecretsManager::ESecretType_StringMap>();

		auto *pPrivateKey = Secrets.f_FindEqual("PrivateKey");
		if (!pPrivateKey)
			co_return DMibErrorInstance("{}: Secret is missing 'PrivateKey' for secret '{}'"_f <<  *pHostInfo << SecretID);

		auto pCertificate = Secrets.f_FindEqual("Certificate");
		if (!pCertificate)
			co_return DMibErrorInstance("{}: Secret is missing 'Certificate' for secret '{}'"_f <<  *pHostInfo << SecretID);

		auto pCertificateAuthority = Secrets.f_FindEqual("CA");
		if (!pCertificateAuthority)
			co_return DMibErrorInstance("{}: Secret is missing 'Certificate' for secret '{}'"_f <<  *pHostInfo << SecretID);

		if (auto pException = f_UserUpdate_CheckCertificate(*pCertificate, SecretID, "Certificate"))
			co_return pException;

		if (auto pException = f_UserUpdate_CheckCertificate(*pCertificateAuthority, SecretID, "Certificate authority"))
			co_return pException;

		auto bUpdated = co_await
			(
				g_Dispatch(m_FileActor) / [_FileSettings, PrivateKey = *pPrivateKey, Certificate = *pCertificate, CertificateAuthority = *pCertificateAuthority]() -> TCFuture<bool>
				{
					TCVector<TCTuple<CStr, CStr>> ToCommit;
					bool bChanged = false;
					auto fUpdateFile = [&](CStrSecure const &_Data, CCertificateFileSettings const &_FileSettings)
						{
							if (!_FileSettings.m_Path)
								return;
							
							CSecureByteVector FileData;
							FileData.f_Insert((uint8 const *)_Data.f_GetStr(), _Data.f_GetLen());
							CStr WriteFileName;
							if (!CFile::fs_FileExists(_FileSettings.m_Path) || !CFile::fs_FileIsSame(FileData, _FileSettings.m_Path))
							{
								CFile::fs_CreateDirectory(CFile::fs_GetPath(_FileSettings.m_Path));
								WriteFileName = _FileSettings.m_Path + ".tempupdate";
								CFile::fs_WriteFileSecure(WriteFileName, FileData);
								ToCommit.f_Insert({WriteFileName, _FileSettings.m_Path});
								bChanged = true;
							}
							else
								WriteFileName = _FileSettings.m_Path;

							auto Attribs = CFile::fs_GetAttributes(WriteFileName);
							if ((Attribs & EFileAttrib_AllUnixPermissions) != (_FileSettings.m_Attributes & EFileAttrib_AllUnixPermissions))
							{
								CFile::fs_SetAttributes(WriteFileName, (_FileSettings.m_Attributes & EFileAttrib_AllUnixPermissions) | EFileAttrib_UnixAttributesValid);
								bChanged = true;
							}

							if (_FileSettings.m_Group && CFile::fs_GetGroup(WriteFileName) != _FileSettings.m_Group)
							{
								CFile::fs_SetGroup(WriteFileName, _FileSettings.m_Group);
								bChanged = true;
							}

							if (_FileSettings.m_User && CFile::fs_GetOwner(WriteFileName) != _FileSettings.m_User)
							{
								CFile::fs_SetOwner(WriteFileName, _FileSettings.m_User);
								bChanged = true;
							}
						}
					;

					{
						auto CaptureScope = co_await g_CaptureExceptions;

						fUpdateFile(PrivateKey, _FileSettings.m_Key);
						fUpdateFile(Certificate, _FileSettings.m_Certificate);
						fUpdateFile(CertificateAuthority, _FileSettings.m_Authority);
						fUpdateFile(PrivateKey + Certificate, _FileSettings.m_KeyCertificate);

						for (auto &ToCommit : ToCommit)
						{
							if (CFile::fs_FileExists(fg_Get<1>(ToCommit)))
								CFile::fs_AtomicReplaceFile(fg_Get<0>(ToCommit), fg_Get<1>(ToCommit));
							else
								CFile::fs_RenameFile(fg_Get<0>(ToCommit), fg_Get<1>(ToCommit));
						}
					}

					co_return bChanged;
				}
			)
		;

		if (bUpdated && pUser->m_Settings.m_fOnCertificateUpdated)
			co_await pUser->m_Settings.m_fOnCertificateUpdated();

		co_return {};
	}

	TCFuture<void> CMongoCertificateDeployActor::CInternal::f_UserUpdate(CUserKey const &_UserKey)
	{
		CUser *pUser = nullptr;
		CUserState *pUserState = nullptr;

		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					return f_UserUpdate_CheckPreconditions(_UserKey, pUser, pUserState);
				}
			)
		;

		f_UpdateUserStatus(*pUser, pUserState->m_SecretsManagerHostInfo, EStatusSeverity_Info, "Secrets manager connected, updating files");

		TCActorResultVector<void> UpdateFilesResults;

		for (auto &FilesSettings : pUser->m_Settings.m_FilesSettings)
			fg_CallSafe(this, &CInternal::f_UserUpdate_UpdateFiles, _UserKey, FilesSettings) > UpdateFilesResults.f_AddResult();

		co_await (co_await UpdateFilesResults.f_GetResults() | g_Unwrap);

		f_UpdateUserStatus(*pUser, pUserState->m_SecretsManagerHostInfo, EStatusSeverity_Success, "All certificates deployed and up to date");

		co_return {};
	}
}
