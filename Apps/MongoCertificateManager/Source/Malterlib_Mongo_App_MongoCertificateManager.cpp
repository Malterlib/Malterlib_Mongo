// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Platform>

#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Concurrency/LogError>
#include "Malterlib_Mongo_App_MongoCertificateManager.h"

namespace NMib::NMongo::NMongoCertificateManager
{
	CMongoCertificateManagerActor::CMongoCertificateManagerActor()
		: CDistributedAppActor(CDistributedAppActor_Settings("MongoCertificateManager").f_AuditCategory("Malterlib/Mongo/MongoCertificateManager"))
	{
	}

	CMongoCertificateManagerActor::~CMongoCertificateManagerActor() = default;

	TCFuture<void> CMongoCertificateManagerActor::fp_StartApp(NEncoding::CEJSON const &_Params)
	{
		auto OnResume = g_OnResume / [&]
			{
				if (mp_State.m_bStoppingApp || f_IsDestroyed())
					DMibError("Startup aborted");
			}
		;

		mp_SecretsManagerSubscription = co_await mp_State.m_TrustManager->f_SubscribeTrustedActors<CSecretsManager>(CSecretsManager::EProtocolVersion_SupportMapSecrets);

		{
			auto Result = co_await mp_SecretsManagerSubscription.f_OnActor
				(
					g_ActorFunctor / [this](TCDistributedActor<CSecretsManager> const &_SecretsManager, CTrustedActorInfo const &_ActorInfo) -> TCFuture<void>
					{
						co_await self(&CMongoCertificateManagerActor::fp_SecretsManagerAddedWithRetry, _SecretsManager, _ActorInfo);

						co_return {};
					}
					, g_ActorFunctor / [this](TCWeakDistributedActor<CActor> const &_SecretsManager, CTrustedActorInfo &&_ActorInfo) -> TCFuture<void>
					{
						self(&CMongoCertificateManagerActor::fp_SecretsManagerRemoved, _SecretsManager, _ActorInfo)
							> fg_LogError("Mib/Mongo/MongoCertificateManager", "Failed to handle secrets manager removed")
						;

						co_return {};
					}
				)
				.f_Wrap()
			;

			if (!Result)
				DMibLog(Error, "Failed when subscripbing to secrets manager: {}", Result.f_GetExceptionStr());
		}


		mp_SensorUpdateTimerSubscription = co_await fg_RegisterTimer
			(
				24.0 * 60.0 * 60.0 // 24 h
				, [this]() -> TCFuture<void>
				{
					co_await
						(
							self(&CMongoCertificateManagerActor::fp_Authority_UpdateSensors)
							+ self(&CMongoCertificateManagerActor::fp_User_UpdateSensors)
						)
					;

					co_return {};
				}
			)
		;

		co_await fp_User_UpdateSensors();
		co_await fp_Authority_UpdateSensors();

		co_return {};
	}

	TCFuture<void> CMongoCertificateManagerActor::fp_StopApp()
	{
		TCActorResultVector<void> Destroys;
		mp_SecretsManagerSubscription.f_Destroy() > Destroys.f_AddResult();

		co_await Destroys.f_GetResults();

		co_return {};
	}

	EPublicKeyType CMongoCertificateManagerActor::fsp_EllipticCurveTypeFromStr(CStr const &_String)
	{
		if (_String == "secp256r1")
			return EPublicKeyType_EC_secp256r1;
		else if (_String == "secp384r1")
			return EPublicKeyType_EC_secp384r1;
		else if (_String == "secp521r1")
			return EPublicKeyType_EC_secp521r1;
		else if (_String == "X25519")
			return EPublicKeyType_EC_X25519;
		else
			DMibError("Unknown elliptic key type: {}"_f << _String);
	}

	CStr CMongoCertificateManagerActor::fsp_EllipticCurveTypeToStr(EPublicKeyType _Type)
	{
		switch (_Type)
		{
		case EPublicKeyType_EC_secp256r1: return "secp256r1";
		case EPublicKeyType_EC_secp384r1: return "secp384r1";
		case EPublicKeyType_EC_secp521r1: return "secp521r1";
		case EPublicKeyType_EC_X25519: return "X25519";
		default: break;
		}
		return "Unknown";
	}

	CPublicKeySetting CMongoCertificateManagerActor::fsp_EllipticCurveTypeToKeySettings(EPublicKeyType _Type)
	{
		switch (_Type)
		{
		case EPublicKeyType_EC_secp256r1: return CPublicKeySettings_EC_secp256r1{};
		case EPublicKeyType_EC_secp384r1: return CPublicKeySettings_EC_secp384r1{};
		case EPublicKeyType_EC_secp521r1: return CPublicKeySettings_EC_secp521r1{};
		case EPublicKeyType_EC_X25519: return CPublicKeySettings_EC_X25519{};
		default: break;
		}
		return CPublicKeySettings_EC_secp521r1{};
	}
}

namespace NMib::NMongo
{
	TCActor<CDistributedAppActor> fg_ConstructApp_MongoCertificateManager()
	{
		return fg_Construct<NMongoCertificateManager::CMongoCertificateManagerActor>();
	}
}
