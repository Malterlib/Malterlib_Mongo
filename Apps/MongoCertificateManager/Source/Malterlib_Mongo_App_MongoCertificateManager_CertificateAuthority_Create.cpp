// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_App_MongoCertificateManager.h"

#include <Mib/Encoding/JSONShortcuts>
#include <Mib/CommandLine/TableRenderer>
#include <Mib/Concurrency/LogError>

namespace NMib::NMongo::NMongoCertificateManager
{
	void CMongoCertificateManagerActor::fp_Authority_StoreSecrets
		(
			TCVector<TCTrustedActor<CSecretsManager>> const &_SecretManagers
			, CStr const &_Name
			, int32 _Serial
			, EPublicKeyType _KeyType
			, CTime const &_Created
			, CTime const &_Modified
			, CCertificateAndKey const &_Certificate
			, TCFutureMap<TCWeakDistributedActor<CSecretsManager>, CSecretsManager::CSetSecretPropertiesResult> &o_StoreResultsAsync
		)
	{
		TCMap<CStrSecure, CStrSecure> Secrets;
		Secrets["PrivateKey"] = _Certificate.m_Key.f_ToString();
		Secrets["Certificate"] = _Certificate.m_Certificate.f_ToString();

		CSecretsManager::CSecretProperties Properties;

		Properties.f_SetSecret(Secrets);
		Properties.f_SetSemanticID(CStrSecure::CFormat(CStr(mc_pAuthoritySemanticPrefix) + "{}") << _Name);
		Properties.f_SetMetadata("Serial", _Serial);
		Properties.f_SetMetadata("KeyType", fsp_EllipticCurveTypeToStr(_KeyType));
		Properties.f_SetTags(TCSet<CStrSecure>{"Private"});

		Properties.m_Created = _Created;
		Properties.m_Modified = _Modified;
		Properties.m_Immutable = true;

		CSecretsManager::CSecretID SecretID;
		SecretID.m_Folder = mc_pAuthorityFolder;
		SecretID.m_Name = _Name;

		for (auto &SecretManager : _SecretManagers)
		{
			auto WeakSecretsManager = SecretManager.m_Actor.f_Weak();
			SecretManager.m_Actor.f_CallActor(&CSecretsManager::f_SetSecretProperties)(SecretID, fg_TempCopy(Properties))
				> o_StoreResultsAsync[WeakSecretsManager];
			;
		}
	}

	TCFuture<uint32> CMongoCertificateManagerActor::fp_CommandLine_AuthorityCreate(CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		auto Auditor = f_Auditor();

		[[maybe_unused]] auto EllipticCurveType = fsp_EllipticCurveTypeFromStr(_Params["EllipticCurveType"].f_String());

		CStr Name = _Params["Name"].f_String();

		if (!CAuthority::fs_IsValidName(Name))
			co_return Auditor.f_Exception("'{}' is not a valid certificate authority name"_f << Name);

		auto OnResume = co_await fg_OnResume
			(
				[&]() -> NException::CExceptionPointer
				{
					if (mp_State.m_bStoppingApp || f_IsDestroyed())
						return DMibErrorInstance("Startup aborted");

					if (mp_SecretsManagerSubscription.m_Actors.f_IsEmpty())
						return DMibErrorInstance("No secret managers connected");

					return {};
				}
			)
		;

		CCertificateAndKey CaCertificate = co_await
			(
				g_ConcurrentDispatch / [EllipticCurveType, Name]
				{
					CByteVector CaCertData;
					CSecureByteVector CaKeyData;

					CCertificateSignOptions SignOptions;
					SignOptions.m_Days = 365*100;

					CCertificateOptions Options;
					Options.m_CommonName = "Malterlib MongoDB CA {} - {nfh,sj16,sf0}"_f <<  Name << fg_GetHighEntropyRandomInteger<uint64>();
					//Options.m_RelativeDistinguishedNames = RelativeDistinguishedNames;
					Options.m_KeySetting = fsp_EllipticCurveTypeToKeySettings(EllipticCurveType);
					Options.f_MakeCA();
					SignOptions.f_AddExtension_SubjectKeyIdentifier();

					CCertificateAndKey Certificate;

					CCertificate::fs_GenerateSelfSignedCertAndKey
						(
							Options
							, Certificate.m_Certificate
							, Certificate.m_Key
							, SignOptions
						)
					;

					return Certificate;
				}
			)
		;

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

		if (mp_Authorities.f_FindEqual(Name))
			co_return Auditor.f_Exception("Certificate authority '{}' already exists"_f << Name);

		int32 Serial = 2; // CA certificate gets serial 1, so let's start the client certificates on serial 2
		{
			TCFutureMap<TCWeakDistributedActor<CSecretsManager>, CSecretsManager::CSetSecretPropertiesResult> StoreResultsAsync;
			fp_Authority_StoreSecrets
				(
					AllSecretManagers
					, Name
					, Serial
					, EllipticCurveType
					, Now
					, Now
					, CaCertificate
					, StoreResultsAsync
				)
			;

			auto StoreResults = co_await fg_AllDoneWrapped(StoreResultsAsync);
			for (auto &StoreResult : StoreResults)
			{
				auto &WeakSecretsManager = StoreResults.fs_GetKey(StoreResult);

				if (!StoreResult)
				{
					SecretManagers.f_Remove(WeakSecretsManager);

					CStr ErrorDescription = "Failed to add certificate authority to secrets manager '{}': {}"_f
						<< SecretManagerDescriptions[WeakSecretsManager]
						<< StoreResult.f_GetExceptionStr()
					;

					DMibLog(Error, "{}", ErrorDescription);
					*_pCommandLine %= "{}\n"_f << ErrorDescription;
				}
			}

			if (SecretManagers.f_IsEmpty())
			{
				*_pCommandLine %= "None of the secret managers were successful, authority not added.\n";

				co_return 1;
			}

			Auditor.f_Info("Added Certificate Authority '{}'"_f << Name);
		}

		co_return 0;
	}
}
