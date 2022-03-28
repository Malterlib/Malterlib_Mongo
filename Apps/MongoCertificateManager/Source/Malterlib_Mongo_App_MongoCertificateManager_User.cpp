// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_App_MongoCertificateManager.h"

#include <Mib/Encoding/JSONShortcuts>
#include <Mib/CommandLine/TableRenderer>
#include <Mib/Concurrency/LogError>

namespace NMib::NMongo::NMongoCertificateManager
{
	bool CMongoCertificateManagerActor::CUser::fs_IsValidName(CStr const &_Name, EUserType _Type)
	{
		switch (_Type)
		{
		case EUserType_User: return fg_IsValidHostname(_Name);
		case EUserType_Server: return fg_IsValidLowerCaseHostname(_Name);
		}

		return false;
	}

	auto CMongoCertificateManagerActor::fsp_UserTypeFromStr(CStr const &_String) -> EUserType
	{
		if (_String == "user")
			return EUserType_User;
		else if (_String == "server")
			return EUserType_Server;
		else
			DMibError("Unknown user type: {}"_f << _String);
	}

	CStr CMongoCertificateManagerActor::fsp_UserTypeToStr(EUserType _Type)
	{
		switch (_Type)
		{
		case EUserType_User: return "user";
		case EUserType_Server: return "server";
		}
		return "Unknown";
	}

	TCFuture<void> CMongoCertificateManagerActor::fp_User_Add(TCDistributedActor<CSecretsManager> _SecretsManager, CSecretsManager::CSecretID _SecretID)
	{
		auto Properties = co_await _SecretsManager.f_CallActor(&CSecretsManager::f_GetSecretProperties)(_SecretID);

		if (!Properties.m_SemanticID)
		{
			DMibLog(Warning, "Invalid semantic ID for secret '{}'", _SecretID);
			co_return {};
		}

		if (!Properties.m_SemanticID->f_StartsWith(mc_pUserSemanticPrefix))
		{
			DMibLog(Warning, "Semantic ID doesn't start with expected prefix for secret '{}'", _SecretID);
			co_return {};
		}

		if (!Properties.m_Metadata)
		{
			DMibLog(Warning, "Missing meta data for secret '{}'", _SecretID);
			co_return {};
		}

		auto &MetaData = *Properties.m_Metadata;

		EUserType Type = EUserType_User;
		if (auto *pType = MetaData.f_FindEqual("Type"))
		{
			if (!pType->f_IsString())
			{
				DMibLog(Warning, "Invalid json type for Type in meta data for secret '{}'", _SecretID);
				co_return {};
			}

			try
			{
				Type = fsp_UserTypeFromStr(pType->f_String());
			}
			catch (CException const &)
			{
				DMibLog(Warning, "Invalid Type ({}) in meta data for secret  '{}'", pType->f_String(), _SecretID);
				co_return {};
			}
		}
		else
		{
			DMibLog(Warning, "Missing KeyType in meta data for secret '{}'", _SecretID);
			co_return {};
		}

		auto AuthorityAndNameStr = Properties.m_SemanticID->f_RemovePrefix(mc_pUserSemanticPrefix);
		auto AuthorityAndName = AuthorityAndNameStr.f_Split("#");

		if (AuthorityAndName.f_GetLen() != 2)
		{
			DMibLog(Warning, "Invalid user semantic ID for secret '{}': {}", _SecretID, AuthorityAndNameStr);
			co_return {};
		}

		CUserKey UserKey;
		UserKey.m_Authority = AuthorityAndName[0];
		UserKey.m_Name = AuthorityAndName[1];

		if (!CAuthority::fs_IsValidName(UserKey.m_Authority))
		{
			DMibLog(Warning, "Invalid authority semantic ID for secret '{}'", _SecretID);
			co_return {};
		}

		if (!CUser::fs_IsValidName(UserKey.m_Name, Type))
		{
			DMibLog(Warning, "Invalid user semantic ID for secret '{}'", _SecretID);
			co_return {};
		}

		if (AuthorityAndNameStr != _SecretID.m_Name)
		{
			DMibLog(Warning, "User/Authority name doesn't match semantic ID for secret '{}': {}", _SecretID, AuthorityAndNameStr);
			co_return {};
		}

		if (!Properties.m_Secret)
		{
			DMibLog(Warning, "Missing secret value for secret '{}'", _SecretID);
			co_return {};
		}

		if (Properties.m_Secret->f_GetTypeID() != CSecretsManager::ESecretType_StringMap)
		{
			DMibLog(Warning, "Secret value is of wrong type (expected string map) for secret '{}'", _SecretID);
			co_return {};
		}

		auto &Secrets = Properties.m_Secret->f_Get<CSecretsManager::ESecretType_StringMap>();

		auto *pPrivateKey = Secrets.f_FindEqual("PrivateKey");
		if (!pPrivateKey)
		{
			DMibLog(Warning, "Secret value is missing 'PrivateKey' for secret '{}'", _SecretID);
			co_return {};
		}

		auto *pCertificate = Secrets.f_FindEqual("Certificate");
		if (!pCertificate)
		{
			DMibLog(Warning, "Secret value is missing 'Certificate' for secret '{}'", _SecretID);
			co_return {};
		}

		EPublicKeyType EllipticCurveType = EPublicKeyType_EC_secp521r1;
		if (auto *pKeyType = MetaData.f_FindEqual("KeyType"))
		{
			if (!pKeyType->f_IsString())
			{
				DMibLog(Warning, "Invalid json type for KeyType in meta data for secret '{}'", _SecretID);
				co_return {};
			}

			try
			{
				EllipticCurveType = fsp_EllipticCurveTypeFromStr(pKeyType->f_String());
			}
			catch (CException const &)
			{
				DMibLog(Warning, "Invalid KeyType ({}) in meta data for secret  '{}'", pKeyType->f_String(), _SecretID);
				co_return {};
			}
		}
		else
		{
			DMibLog(Warning, "Missing KeyType in meta data for secret '{}'", _SecretID);
			co_return {};
		}

		if (!Properties.m_Modified)
		{
			DMibLog(Warning, "Missing modified time for secret '{}'", _SecretID);
			co_return {};
		}

		auto &ModifiedTime = *Properties.m_Modified;

		if (!Properties.m_Created)
		{
			DMibLog(Warning, "Missing created time for secret '{}'", _SecretID);
			co_return {};
		}

		auto &CreatedTime = *Properties.m_Created;

		auto &User = mp_Users[UserKey];
		if (!User.m_LastModified.f_IsValid() || ModifiedTime > User.m_LastModified)
		{
			User.m_Certificate.m_Key = CSecureByteVector::fs_FromString(*pPrivateKey);
			User.m_Certificate.m_Certificate = CByteVector::fs_FromString(*pCertificate);
			User.m_EllipticCurveType = EllipticCurveType;
			User.m_LastModified = ModifiedTime;
			User.m_Created = CreatedTime;
			User.m_Type = Type;
		}

		User.m_SecretsManagers[_SecretsManager.f_Weak()] = ModifiedTime;
		fp_User_UpdateStatus(User, CDistributedAppSensorReporter::EStatusSeverity_Ok, "OK");
		fp_User_UpdateSensor(User.f_GetKey()) > fg_LogError("Update sensors", "Falied to update user sensors");

		co_return {};
	}

	TCFuture<void> CMongoCertificateManagerActor::fp_User_SecretsManagerAdded(TCDistributedActor<CSecretsManager> const &_SecretsManager, CTrustedActorInfo const &_Info)
	{
		CSecretsManager::CSubscribeToChanges SubscribeOptions;
		SubscribeOptions.m_SemanticID = CStr(mc_pUserSemanticPrefix) + "*";
		SubscribeOptions.m_TagsExclusive["Private"];
		SubscribeOptions.m_fOnChanges = g_ActorFunctor / [_SecretsManager, this](CSecretsManager::CSecretChanges &&_Changes) -> TCFuture<void>
			{
				TCActorResultVector<void> AddSecretResults;
				for (auto &Changed : _Changes.m_Changed)
				{
					auto &SecretID = _Changes.m_Changed.fs_GetKey(Changed);

					fp_User_Add(_SecretsManager, SecretID) > AddSecretResults.f_AddResult();
				}

				for (auto &Result : co_await AddSecretResults.f_GetResults())
				{
					if (!Result)
						DMibLog(Error, "Failed to add user '{}'", Result.f_GetExceptionStr());
				}

				for (auto &RemovedID : _Changes.m_Removed)
				{
					if (RemovedID.m_Folder != mc_pUserFolder)
						continue;

					auto AuthorityAndNameStr = RemovedID.m_Name;
					auto AuthorityAndName = AuthorityAndNameStr.f_Split("#");

					if (AuthorityAndName.f_GetLen() != 2)
						continue;

					CUserKey UserKey;
					UserKey.m_Authority = AuthorityAndName[0];
					UserKey.m_Name = AuthorityAndName[1];
					if (auto *pUser = mp_Users.f_FindEqual(UserKey))
					{
						if (pUser->m_SecretsManagers.f_FindEqual(_SecretsManager))
						{
							pUser->m_SecretsManagers.f_Remove(_SecretsManager);
							if (pUser->m_SecretsManagers.f_IsEmpty())
								mp_Users.f_Remove(UserKey);
						}
					}
				}

				co_return {};
			}
		;

		mp_UserSubscriptions[_SecretsManager.f_Weak()] = co_await _SecretsManager.f_CallActor(&CSecretsManager::f_SubscribeToChanges)(fg_Move(SubscribeOptions));

		co_return {};
	}
}
