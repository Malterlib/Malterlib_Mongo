// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedDaemon>
#include <Mib/Concurrency/ActorSequencerActor>
#include <Mib/Concurrency/Actor/Timer>
#include <Mib/Cloud/SecretsManager>
#include <Mib/Cryptography/Certificate>

namespace NMib::NMongo::NMongoCertificateManager
{
	struct CMongoCertificateManagerActor : public CDistributedAppActor
	{
		CMongoCertificateManagerActor();
		~CMongoCertificateManagerActor();

	private:
		using EStatusSeverity = CDistributedAppSensorReporter::EStatusSeverity;

		struct CStatus
		{
			CStr m_Description;
			EStatusSeverity m_Severity = CDistributedAppSensorReporter::EStatusSeverity_Ok;
		};

		struct CCertificateAndKey
		{
			CByteVector m_Certificate;
			CSecureByteVector m_Key;
		};

		struct CAuthority
		{
			CStr const &f_GetName() const;
			CSecretsManager::CSecretID f_GetSecretID() const;
			static bool fs_IsValidName(CStr const &_Name);

			// Stored
			CCertificateAndKey m_Certificate;
			EPublicKeyType m_EllipticCurveType = EPublicKeyType_EC_secp521r1;
			int32 m_Serial = 2;
			CTime m_Created;
			CTime m_LastModified;

			// Temporary
			CStatus m_Status;
			TCMap<TCWeakDistributedActor<CSecretsManager>, CTime> m_SecretsManagers;
			CDistributedAppSensorReporter::CSensorReporter m_SensorReporter_Status;
			CDistributedAppSensorReporter::CSensorReporter m_SensorReporter_Expire;
			bool m_bSensorsRegistered = false;
		};

		struct CUserKey
		{
			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const
			{
				o_Str += typename tf_CStr::CFormat("{}/{}") << m_Authority << m_Name;
			}

			auto operator <=> (CUserKey const &_Right) const = default;

			CSecretsManager::CSecretID f_GetSecretID() const;
			CStr f_GetSecretIDName() const;

			CStr m_Authority;
			CStr m_Name;
		};

		enum EUserType : uint32
		{
			EUserType_User = 0
			, EUserType_Server
		};

		struct CUser
		{
			CUserKey const &f_GetKey() const
			{
				return TCMap<CUserKey, CUser>::fs_GetKey(*this);
			}
			static bool fs_IsValidName(CStr const &_Name, EUserType _Type);

			// Stored
			CCertificateAndKey m_Certificate;
			EPublicKeyType m_EllipticCurveType = EPublicKeyType_EC_secp521r1;
			CTime m_Created;
			CTime m_LastModified;
			EUserType m_Type = EUserType_User;

			// Temporary
			CStatus m_Status;
			TCMap<TCWeakDistributedActor<CSecretsManager>, CTime> m_SecretsManagers;
			CDistributedAppSensorReporter::CSensorReporter m_SensorReporter_Status;
			CDistributedAppSensorReporter::CSensorReporter m_SensorReporter_Expire;
			bool m_bSensorsRegistered = false;
		};

		void fp_BuildCommandLine(CDistributedAppCommandLineSpecification &o_CommandLine) override;

		TCFuture<void> fp_Destroy() override;

		TCFuture<void> fp_StartApp(NEncoding::CEJSONSorted const _Params) override;
		TCFuture<void> fp_StopApp() override;
		TCFuture<void> fp_RegisterSensors();

		TCFuture<uint32> fp_CommandLine_AuthorityCreate(CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);
		TCFuture<uint32> fp_CommandLine_AuthorityList(CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);
		TCFuture<uint32> fp_CommandLine_AuthorityResync(CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);

		TCFuture<uint32> fp_CommandLine_UserCreate(CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);
		TCFuture<uint32> fp_CommandLine_UserList(CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);
		TCFuture<uint32> fp_CommandLine_UserResync(CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);
		TCFuture<uint32> fp_CommandLine_UserReissue(CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);

		static EPublicKeyType fsp_EllipticCurveTypeFromStr(CStr const &_String);
		static CStr fsp_EllipticCurveTypeToStr(EPublicKeyType _Type);
		static CPublicKeySetting fsp_EllipticCurveTypeToKeySettings(EPublicKeyType _Type);

		TCFuture<void> fp_SecretsManagerAddedWithRetry(TCDistributedActor<CSecretsManager> _SecretsManager, CTrustedActorInfo _Info);

		TCFuture<void> fp_SecretsManagerAdded(TCDistributedActor<CSecretsManager> _SecretsManager, CTrustedActorInfo _Info);
		TCFuture<void> fp_SecretsManagerRemoved(TCWeakDistributedActor<CActor> _SecretsManager, CTrustedActorInfo _ActorInfo);

		TCFuture<CCertificateAndKey> fp_GenerateUserCertificate(CCertificateAndKey _Certificate, EPublicKeyType _EllipticCurveType, CStr _UserName, EUserType _UserType);

		TCFuture<void> fp_Authority_UpdateSensor(CStr _Authority);
		TCFuture<void> fp_Authority_UpdateSensors();
		TCFuture<void> fp_Authority_RegisterSensors(CStr _Authority);
		TCFuture<void> fp_Authority_UpdateStatusSensor(CStr _Authority, EStatusSeverity _Severity, CStr _Status);
		void fp_Authority_UpdateStatus(CAuthority &o_Authority, EStatusSeverity _Severity, CStr const &_Status);
		TCFuture<void> fp_Authority_Add(TCDistributedActor<CSecretsManager> _SecretsManager, CSecretsManager::CSecretID _SecretID);
		TCFuture<void> fp_Authority_SecretsManagerAdded(TCDistributedActor<CSecretsManager> _SecretsManager, CTrustedActorInfo _Info);
		void fp_Authority_StoreSecrets
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
		;
		TCFuture<int32> fp_Authority_GetNewSerial(CStr _AuthorityName);
		
		static EUserType fsp_UserTypeFromStr(CStr const &_String);
		static CStr fsp_UserTypeToStr(EUserType _Type);

		TCFuture<void> fp_User_UpdateSensor(CUserKey _UserKey);
		TCFuture<void> fp_User_UpdateSensors();
		TCFuture<void> fp_User_RegisterSensors(CUserKey _UserKey);
		TCFuture<void> fp_User_UpdateStatusSensor(CUserKey _UserKey, EStatusSeverity _Severity, CStr _Status);
		void fp_User_UpdateStatus(CUser &o_User, EStatusSeverity _Severity, CStr const &_Status);
		TCFuture<void> fp_User_Add(TCDistributedActor<CSecretsManager> _SecretsManager, CSecretsManager::CSecretID _SecretID);
		TCFuture<void> fp_User_SecretsManagerAdded(TCDistributedActor<CSecretsManager> _SecretsManager, CTrustedActorInfo _Info);
		void fp_User_StoreSecrets
			(
				TCVector<TCTrustedActor<CSecretsManager>> const &_SecretManagers
				, CAuthority const &_Authority
				, CUserKey const &_Key
				, EPublicKeyType _KeyType
				, EUserType _Type
				, CTime const &_Created
				, CTime const &_Modified
				, CCertificateAndKey const &_Certificate
				, TCFutureMap<TCWeakDistributedActor<CSecretsManager>, CSecretsManager::CSetSecretPropertiesResult> &o_StoreResultsAsync
			)
		;

		static constexpr ch8 const *mc_pAuthoritySemanticPrefix = "org.malterlib.mongo.authority#";
		static constexpr ch8 const *mc_pAuthorityFolder = "org.malterlib.mongo.authority";

		static constexpr ch8 const *mc_pUserSemanticPrefix = "org.malterlib.mongo.user#";
		static constexpr ch8 const *mc_pUserFolder = "org.malterlib.mongo.user";

		TCMap<CStr, CAuthority> mp_Authorities;
		TCMap<CUserKey, CUser> mp_Users;

		TCTrustedActorSubscription<CSecretsManager> mp_SecretsManagerSubscription;
		TCMap<TCWeakDistributedActor<CSecretsManager>, CStr> mp_LastSecretsManagerError;
		TCSet<TCWeakDistributedActor<CSecretsManager>> mp_RetryingSecretsManagers;
		TCMap<TCWeakDistributedActor<CSecretsManager>, CActorSubscription> mp_UserSubscriptions;
		TCMap<TCWeakDistributedActor<CSecretsManager>, CActorSubscription> mp_AuthoritySubscriptions;

		CSequencer mp_InitSensorReporterSequencer{"MongoCertificateManagerActor InitSensorReporterSequencer"};
		CActorSubscription mp_SensorUpdateTimerSubscription;
	};
}
