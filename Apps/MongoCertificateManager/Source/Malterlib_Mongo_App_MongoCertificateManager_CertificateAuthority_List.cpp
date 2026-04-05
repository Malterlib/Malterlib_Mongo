// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Mongo_App_MongoCertificateManager.h"

#include <Mib/Encoding/JsonShortcuts>
#include <Mib/CommandLine/TableRenderer>

namespace NMib::NMongo::NMongoCertificateManager
{
	TCFuture<uint32> CMongoCertificateManagerActor::fp_CommandLine_AuthorityList(CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		bool bVerbose = _Params["Verbose"].f_Boolean();
		CStr AuthorityName = _Params["Authority"].f_String();

		auto AnsiEncoding = _pCommandLine->f_AnsiEncoding();
		CTableRenderHelper TableRenderer = _pCommandLine->f_TableRenderer();

		TCVector<CStr> Headings;
		TCSet<umint> VerboseHeadings;

		auto fAddHeading = [&](CStr const &_Name, bool _bVerbose = true)
			{
				if (_bVerbose)
					VerboseHeadings[Headings.f_GetLen()];

				Headings.f_Insert(_Name);
			}
		;

		fAddHeading("Certificate Authority", false);
		fAddHeading("Key Type");
		fAddHeading("Serial");
		fAddHeading("Secret Managers");
		umint iMissingOnManagersHeading = Headings.f_GetLen();
		fAddHeading("{}{}Missing on Managers{}"_f << AnsiEncoding.f_StatusWarning() << AnsiEncoding.f_Bold() << AnsiEncoding.f_Default());
		fAddHeading("Status", false);

		TableRenderer.f_AddHeadingsVector(Headings);
		TableRenderer.f_SetOptions(CTableRenderHelper::EOption_Rounded | CTableRenderHelper::EOption_AvoidRowSeparators);
		bool bHasMissing = false;

		for (auto &Authority : mp_Authorities)
		{
			auto &Name = Authority.f_GetName();
			if (!AuthorityName.f_IsEmpty() && Name != AuthorityName)
				continue;

			CStr StatusDescription;
			switch (Authority.m_Status.m_Severity)
			{
			case CDistributedAppSensorReporter::EStatusSeverity_Info:
				StatusDescription = Authority.m_Status.m_Description;
				break;
			case CDistributedAppSensorReporter::EStatusSeverity_Ok:
				StatusDescription = "{}{}{}"_f << AnsiEncoding.f_StatusNormal() << Authority.m_Status.m_Description << AnsiEncoding.f_Default();
				break;
			case CDistributedAppSensorReporter::EStatusSeverity_Warning:
				StatusDescription = "{}{}{}"_f << AnsiEncoding.f_StatusWarning() << Authority.m_Status.m_Description << AnsiEncoding.f_Default();
				break;
			case CDistributedAppSensorReporter::EStatusSeverity_Error:
				StatusDescription = "{}{}{}"_f << AnsiEncoding.f_StatusError() << Authority.m_Status.m_Description << AnsiEncoding.f_Default();
				break;
			}

			TCVector<CStr> SecretManagers;

			for (auto &ModifiedTime : Authority.m_SecretsManagers)
			{
				auto &WeakManager = Authority.m_SecretsManagers.fs_GetKey(ModifiedTime);
				auto Manager = WeakManager.f_Lock();
				if (!Manager)
					continue;

				auto *pSecretManager = mp_SecretsManagerSubscription.m_Actors.f_FindEqual(Manager);
				DMibCheck(pSecretManager);

				if (!pSecretManager)
					continue;

				SecretManagers.f_Insert(pSecretManager->m_TrustInfo.m_HostInfo.f_GetDescColored(AnsiEncoding.f_Flags()));
			}

			TCVector<CStr> MissingSecretManagers;

			for (auto &Manager : mp_SecretsManagerSubscription.m_Actors)
			{
				auto WeakManager = Manager.m_Actor.f_Weak();

				if (Authority.m_SecretsManagers.f_FindEqual(WeakManager))
					continue;

				bHasMissing = true;

				MissingSecretManagers.f_Insert(Manager.m_TrustInfo.m_HostInfo.f_GetDescColored(AnsiEncoding.f_Flags()));
			}

			TableRenderer.f_AddRow
				(
					Name
					, fsp_PublicKeySettingToStr(Authority.m_PublicKeySetting)
					, "{}"_f << Authority.m_Serial
					, CStr::fs_Join(SecretManagers, "\n")
					, CStr::fs_Join(MissingSecretManagers, "\n")
					, StatusDescription
				)
			;
		}

		if (!bVerbose)
		{
			while (auto pLargest = VerboseHeadings.f_FindLargest())
			{
				TableRenderer.f_RemoveColumn(*pLargest);
				VerboseHeadings.f_Remove(pLargest);
			}
		}
		else
		{
			if (!bHasMissing)
				TableRenderer.f_RemoveColumn(iMissingOnManagersHeading);
		}

		TableRenderer.f_Output(_Params);

		co_return 0;
	}
}
