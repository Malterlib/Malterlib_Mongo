// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_App_MongoCertificateManager.h"

#include <Mib/Encoding/JSONShortcuts>
#include <Mib/CommandLine/TableRenderer>
#include <Mib/Concurrency/LogError>

namespace NMib::NMongo::NMongoCertificateManager
{
	bool CMongoCertificateManagerActor::CAuthority::fs_IsValidName(CStr const &_Name)
	{
		return fg_IsValidHostname(_Name);
	}

	CStr CMongoCertificateManagerActor::CUserKey::f_GetSecretIDName() const
	{
		return "{}#{}"_f << m_Authority << m_Name;
	}

	CSecretsManager::CSecretID CMongoCertificateManagerActor::CUserKey::f_GetSecretID() const
	{
		CSecretsManager::CSecretID SecretID;
		SecretID.m_Folder = mc_pUserFolder;
		SecretID.m_Name = f_GetSecretIDName();

		return SecretID;
	}
	
	void CMongoCertificateManagerActor::fp_User_StoreSecrets
		(
			TCVector<TCTrustedActor<CSecretsManager>> const &_SecretManagers
			, CAuthority const &_Authority
			, CUserKey const &_UserKey
			, EPublicKeyType _KeyType
			, EUserType _Type
			, CTime const &_Created
			, CTime const &_Modified
			, CCertificateAndKey const &_Certificate
			, TCActorResultMap<TCWeakDistributedActor<CSecretsManager>, CSecretsManager::CSetSecretPropertiesResult> &o_StoreResultsAsync
		)
	{
		TCMap<CStrSecure, CStrSecure> Secrets;
		Secrets["PrivateKey"] = _Certificate.m_Key.f_ToString();
		Secrets["Certificate"] = _Certificate.m_Certificate.f_ToString();
		Secrets["CA"] = _Authority.m_Certificate.m_Certificate.f_ToString();

		CSecretsManager::CSecretProperties Properties;

		Properties.f_SetSecret(Secrets);
		Properties.f_SetSemanticID(CStrSecure::CFormat(CStr(mc_pUserSemanticPrefix) + "{}") << _UserKey.f_GetSecretIDName());
		Properties.f_SetMetadata("KeyType", fsp_EllipticCurveTypeToStr(_KeyType));
		Properties.f_SetMetadata("Type", fsp_UserTypeToStr(_Type));
		Properties.f_SetTags(TCSet<CStrSecure>{"Private"});

		Properties.m_Created = _Created;
		Properties.m_Modified = _Modified;

		CSecretsManager::CSecretID SecretID = _UserKey.f_GetSecretID();

		for (auto &SecretManager : _SecretManagers)
		{
			auto WeakSecretsManager = SecretManager.m_Actor.f_Weak();
			SecretManager.m_Actor.f_CallActor(&CSecretsManager::f_SetSecretProperties)(SecretID, fg_TempCopy(Properties))
				> o_StoreResultsAsync.f_AddResult(WeakSecretsManager);
			;
		}
	}

	auto CMongoCertificateManagerActor::fp_GenerateUserCertificate(CCertificateAndKey _Certificate, EPublicKeyType _EllipticCurveType, CStr _UserName, EUserType _UserType)
		-> TCFuture<CCertificateAndKey>
	{
		co_return co_await
			(
				g_ConcurrentDispatch / [=]
				{
					TCMap<CStr, CStr> RelativeDistinguishedNames;

					RelativeDistinguishedNames["OU"] = [&]
						{
							switch (_UserType)
							{
							case EUserType_User: return "mongo.user";
							case EUserType_Server: return "mongo.server";
							}
							return "invalid";
						}
						()
					;
					RelativeDistinguishedNames["O"] = "malterlib.org";

					CCertificateSignOptions SignOptions;
					SignOptions.m_Serial = fg_GetRandom();
					SignOptions.m_Days = 365*10;
					SignOptions.f_AddExtension_AuthorityKeyIdentifier();

					CCertificateOptions Options;
					Options.m_CommonName = _UserName;
					Options.m_RelativeDistinguishedNames = RelativeDistinguishedNames;
					//Options.m_Hostnames = Subjects;
					Options.m_KeySetting = fsp_EllipticCurveTypeToKeySettings(_EllipticCurveType);

					Options.f_AddExtension_BasicConstraints(false);
					switch (_UserType)
					{
					case EUserType_User:
						{
							Options.f_AddExtension_KeyUsage(EKeyUsage_DigitalSignature);
							Options.f_AddExtension_ExtendedKeyUsage(EExtendedKeyUsage_ClientAuth);
						}
						break;
					case EUserType_Server:
						{
							Options.m_Hostnames.f_Insert(_UserName);
						}
						break;
					}

					CCertificateAndKey UserCertificate;

					CByteVector CertRequestData;

					CCertificate::fs_GenerateClientCertificateRequest(Options, CertRequestData, UserCertificate.m_Key);

					CCertificate::fs_SignClientCertificate(_Certificate.m_Certificate, _Certificate.m_Key, CertRequestData, UserCertificate.m_Certificate, SignOptions);

					return UserCertificate;
				}
			)
		;
	}

	TCFuture<uint32> CMongoCertificateManagerActor::fp_CommandLine_UserCreate(CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
	{
		auto Auditor = f_Auditor();

		[[maybe_unused]] auto EllipticCurveType = fsp_EllipticCurveTypeFromStr(_Params["EllipticCurveType"].f_String());

		CStr UserName = _Params["User"].f_String();
		CStr Authority = _Params["Authority"].f_String();
		CStr TypeString = _Params["Type"].f_String();

		if (!CAuthority::fs_IsValidName(Authority))
			co_return Auditor.f_Exception("'{}' is not a valid certificate authority name"_f << Authority);

		EUserType Type = EUserType_User;
		try
		{
			Type = fsp_UserTypeFromStr(TypeString);
		}
		catch (CException const &_Exception)
		{
			co_return Auditor.f_Exception(_Exception.f_GetErrorStr());
		}

		if (!CUser::fs_IsValidName(UserName, Type))
			co_return Auditor.f_Exception("'{}' is not a valid {} name"_f << UserName << (Type == EUserType_Server ? "server" : "user"));

		CUserKey UserKey;
		UserKey.m_Authority = Authority;
		UserKey.m_Name = UserName;

		if (TypeString == "user")
			Type = EUserType_User;
		else if (TypeString == "server")
			Type = EUserType_Server;

		CAuthority *pAuthority = nullptr;

		auto OnResume = co_await fg_OnResume
			(
				[&]() -> NException::CExceptionPointer
				{
					if (mp_State.m_bStoppingApp || f_IsDestroyed())
						return DMibErrorInstance("Startup aborted");

					if (mp_SecretsManagerSubscription.m_Actors.f_IsEmpty())
						return DMibErrorInstance("No secret managers connected");

					pAuthority = mp_Authorities.f_FindEqual(Authority);

					if (!pAuthority)
						return DMibErrorInstance("No such authority: '{}'"_f << Authority);

					return {};
				}
			)
		;

		auto UserCertificate = co_await fp_GenerateUserCertificate(pAuthority->m_Certificate, EllipticCurveType, UserName, Type);

		NTime::CTime Now = NTime::CTime::fs_NowUTC();

		auto AllSecretManagers = fg_VectorFromContainer(mp_SecretsManagerSubscription.m_Actors);

		TCMap<TCWeakDistributedActor<CSecretsManager>, CTime> SecretManagers;
		TCMap<TCWeakDistributedActor<CSecretsManager>, CStr> SecretManagerDescriptions;

		for (auto &SecretManager : AllSecretManagers)
		{
			auto WeakSecretsManager = SecretManager.m_Actor.f_Weak();
			SecretManagers[WeakSecretsManager] = Now;
			SecretManagerDescriptions[WeakSecretsManager] = "{}"_f << SecretManager.m_TrustInfo.m_HostInfo;
		}

		if (mp_Users.f_FindEqual(UserKey))
			co_return Auditor.f_Exception("User '{}' already exists"_f << UserName);

		{
			TCActorResultMap<TCWeakDistributedActor<CSecretsManager>, CSecretsManager::CSetSecretPropertiesResult> StoreResultsAsync;
			fp_User_StoreSecrets
				(
					AllSecretManagers
					, *pAuthority
					, UserKey
					, EllipticCurveType
					, Type
					, Now
					, Now
					, UserCertificate
					, StoreResultsAsync
				)
			;

			auto StoreResults = co_await StoreResultsAsync.f_GetResults();
			for (auto &StoreResult : StoreResults)
			{
				auto &WeakSecretsManager = StoreResults.fs_GetKey(StoreResult);

				if (!StoreResult)
				{
					SecretManagers.f_Remove(WeakSecretsManager);

					CStr ErrorDescription = "Failed to add user to secrets manager '{}': {}"_f
						<< SecretManagerDescriptions[WeakSecretsManager]
						<< StoreResult.f_GetExceptionStr()
					;

					DMibLog(Error, "{}", ErrorDescription);
					*_pCommandLine %= "{}\n"_f << ErrorDescription;
				}
			}

			if (SecretManagers.f_IsEmpty())
			{
				*_pCommandLine %= "None of the secret managers were successful, user was not added.\n";

				co_return 1;
			}

			Auditor.f_Info("Added User '{}'"_f << UserKey);
		}
		
		co_return 0;
	}
}
