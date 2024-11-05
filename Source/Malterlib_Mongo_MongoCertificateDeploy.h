// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedActor>
#include <Mib/Concurrency/DistributedActorTrustManager>
#include <Mib/File/File>

namespace NMib::NMongo
{
	struct CMongoCertificateDeployActor : public NConcurrency::CActor
	{
		CMongoCertificateDeployActor
			(
				NConcurrency::TCActor<NConcurrency::CActorDistributionManager> const &_DistributionManager
				, NConcurrency::TCActor<NConcurrency::CDistributedActorTrustManager> const &_TrustManager
			)
		;
		~CMongoCertificateDeployActor();

		struct CCertificateFileSettings
		{
			NStr::CStr m_Path;
			NStr::CStr m_User;
			NStr::CStr m_Group;
			NFile::EFileAttrib m_Attributes = NFile::EFileAttrib_UnixAttributesValid | NFile::EFileAttrib_UserRead | NFile::EFileAttrib_UserWrite;
		};

		struct CCertificateFilesSettings
		{
			CCertificateFilesSettings();
			~CCertificateFilesSettings();

			CCertificateFileSettings m_Authority;
			CCertificateFileSettings m_Key;
			CCertificateFileSettings m_Certificate;
			CCertificateFileSettings m_KeyCertificate;
		};

		enum EStatusSeverity
		{
			EStatusSeverity_Info
			, EStatusSeverity_Success
			, EStatusSeverity_Warning
			, EStatusSeverity_Error
		};

		enum EUserType : uint32
		{
			EUserType_User = 0
			, EUserType_Server
		};

		struct CUserStatus
		{
			auto f_Tuple() const;
			bool operator == (CUserStatus const &_Right) const;

			NStr::CStr m_Description;
			EStatusSeverity m_Severity = EStatusSeverity_Info;
		};

		struct CDeployLocaction
		{
			NStr::CStr m_BasePath;
			NStr::CStr m_FileUser;
			NStr::CStr m_FileGroup;
		};

		struct CUserSettings
		{
			CUserSettings();

			void f_InitCommon(NStr::CStr const &_Authority, NStr::CStr const &_Identifier, NContainer::TCVector<CDeployLocaction> const &_Locations);
			void f_InitUser(NStr::CStr const &_Authority, NStr::CStr const &_UserName, NContainer::TCVector<CDeployLocaction> const &_Locations);
			void f_InitServer(NStr::CStr const &_Authority, NStr::CStr const &_HostName, NContainer::TCVector<CDeployLocaction> const &_Locations);

			NStr::CStr m_Authority;
			NStr::CStr m_Name;
			EUserType m_Type = EUserType_User;
			NContainer::TCVector<CCertificateFilesSettings> m_FilesSettings;

			NConcurrency::TCActorFunctor<NConcurrency::TCFuture<void> (NConcurrency::CHostInfo _HostInfo, CUserStatus _Status)> m_fOnStatusChange;
			NConcurrency::TCActorFunctor<NConcurrency::TCFuture<void> ()> m_fOnCertificateUpdated;
		};

		NConcurrency::TCFuture<void> f_Start();
		NConcurrency::TCFuture<NConcurrency::CActorSubscription> f_AddUser(CUserSettings _UserSettings);

	private:
		NConcurrency::TCFuture<void> fp_Destroy() override;

		struct CInternal;
		NStorage::TCUniquePointer<CInternal> mp_pInternal;
	};
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NMongo;
#endif
