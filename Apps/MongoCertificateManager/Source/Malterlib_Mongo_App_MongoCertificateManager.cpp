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

	TCFuture<void> CMongoCertificateManagerActor::fp_StartApp(NEncoding::CEJSONSorted const _Params)
	{
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> NException::CExceptionPointer
				{
					if (mp_State.m_bStoppingApp || f_IsDestroyed())
						return DMibErrorInstance("Startup aborted");
					return {};
				}
			)
		;

		mp_SecretsManagerSubscription = co_await mp_State.m_TrustManager->f_SubscribeTrustedActors<CSecretsManager>(CSecretsManager::EProtocolVersion_SupportMapSecrets);

		{
			auto Result = co_await mp_SecretsManagerSubscription.f_OnActor
				(
					g_ActorFunctor / [this](TCDistributedActor<CSecretsManager> _SecretsManager, CTrustedActorInfo _ActorInfo) -> TCFuture<void>
					{
						co_await fp_SecretsManagerAddedWithRetry(_SecretsManager, _ActorInfo);

						co_return {};
					}
					, g_ActorFunctor / [this](TCWeakDistributedActor<CActor> _SecretsManager, CTrustedActorInfo _ActorInfo) -> TCFuture<void>
					{
						fp_SecretsManagerRemoved(_SecretsManager, _ActorInfo)
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
					co_await (fp_Authority_UpdateSensors() + fp_User_UpdateSensors());

					co_return {};
				}
			)
		;

		co_await fp_User_UpdateSensors();
		co_await fp_Authority_UpdateSensors();

		co_return {};
	}

	TCFuture<void> CMongoCertificateManagerActor::fp_Destroy()
	{
		co_await fg_Move(mp_InitSensorReporterSequencer).f_Destroy().f_Wrap() > fg_LogError("Mib/Mongo/MongoCertificateManager", "Failed to destroy sequencer");

		co_await CDistributedAppActor::fp_Destroy();

		co_return {};
	}

	TCFuture<void> CMongoCertificateManagerActor::fp_StopApp()
	{
		TCFutureVector<void> Destroys;
		mp_SecretsManagerSubscription.f_Destroy() > Destroys;

		co_await fg_AllDoneWrapped(Destroys);

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
