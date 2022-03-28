// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Cryptography/RandomID>
#include <Mib/CommandLine/TableRenderer>

#include "Malterlib_Mongo_App_MongoCertificateManager.h"

namespace NMib::NMongo::NMongoCertificateManager
{
	void CMongoCertificateManagerActor::fp_BuildCommandLine(CDistributedAppCommandLineSpecification &o_CommandLine)
	{
		CDistributedAppActor::fp_BuildCommandLine(o_CommandLine);

		o_CommandLine.f_SetProgramDescription
			(
				"Malterlib MongoCertificate Manager"
				, "Manages service certificates through MongoCertificate service."
			)
		;

		auto AuthorityManagement = o_CommandLine.f_AddSection("Service Management", "Commands to manage MongoCertificateManager authorities");

		auto SettingsOption_EllipticCurveType = "EllipticCurveType?"_=
			{
				"Names"_= {"--elliptic-curve-type"}
				, "Default"_= "secp521r1"
				, "Type"_= COneOf{"secp256r1", "secp384r1", "secp521r1", "X25519"}
				, "Description"_= "The type of elliptic curve to use for the EC certificate."
			}
		;
		auto SettingsOption_Authority = "Authority?"_=
			{
				"Names"_= {"--authority"}
				, "Default"_= ""
				, "Type"_= ""
				, "Description"_= "The certificate authority to use"
			}
		;

		auto fStripDefault = [](auto &&_Template)
			{
				auto Return = _Template;
				Return.m_Value.f_RemoveMember("Default");
				return Return;
			}
		;
		auto fStripOptional = [](auto &&_Template)
			{
				auto Return = _Template;
				Return.m_Key = Return.m_Key.f_Replace("?", "");
				return Return;
			}
		;

		AuthorityManagement.f_RegisterCommand
			(
				{
					"Names"_= {"--authority-create"}
					, "Description"_= "Create a certificate authority\n"
					, "Options"_=
					{
						"Name"_=
						{
							"Names"_= {"--name"}
							, "Type"_= ""
							, "Description"_= "Name of the certificate authority"
						}
						, SettingsOption_EllipticCurveType
					}
				}
				, [this](CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
				{
					return g_Future <<= self(&CMongoCertificateManagerActor::fp_CommandLine_AuthorityCreate, _Params, _pCommandLine);
				}
			)
		;
		AuthorityManagement.f_RegisterCommand
			(
				{
					"Names"_= {"--authority-list"}
					, "Description"_= "List certificate authorities."
					, "Options"_=
					{
						"Verbose?"_=
						{
							"Names"_= {"--verbose", "-v"}
							, "Default"_= false
							, "Description"_= "Display more extensive information about the ca."
						}
						, SettingsOption_Authority
						, CTableRenderHelper::fs_OutputTypeOption()
					}
				}
				, [this](CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
				{
					return g_Future <<= self(&CMongoCertificateManagerActor::fp_CommandLine_AuthorityList, _Params, _pCommandLine);
				}
			)
		;
		AuthorityManagement.f_RegisterCommand
			(
				{
					"Names"_= {"--authority-resync"}
					, "Description"_= "Update certificate authorities on out of date secret managers."
					, "Options"_=
					{
						SettingsOption_Authority
					}
				}
				, [this](CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
				{
					return g_Future <<= self(&CMongoCertificateManagerActor::fp_CommandLine_AuthorityResync, _Params, _pCommandLine);
				}
			)
		;

		auto UserManagement = o_CommandLine.f_AddSection("User Management", "Commands to manage MongoCertificateManager users");

		auto SettingsOption_User = "User?"_=
			{
				"Names"_= {"--user"}
				, "Default"_= ""
				, "Type"_= ""
				, "Description"_= "Name of the user"
			}
		;

		UserManagement.f_RegisterCommand
			(
				{
					"Names"_= {"--user-create"}
					, "Description"_= "Create a user\n"
					, "Options"_=
					{
						fStripOptional(fStripDefault(SettingsOption_Authority))
						, fStripOptional(fStripDefault(SettingsOption_User))
						, "Type?"_=
						{
							"Names"_= {"--type"}
							, "Default"_= "user"
							, "Type"_= COneOf{"user", "server"}
							, "Description"_= "The type of user to create."
						}
						, SettingsOption_EllipticCurveType
					}
				}
				, [this](CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
				{
					return g_Future <<= self(&CMongoCertificateManagerActor::fp_CommandLine_UserCreate, _Params, _pCommandLine);
				}
			)
		;
		UserManagement.f_RegisterCommand
			(
				{
					"Names"_= {"--user-list"}
					, "Description"_= "List certificate authorities."
					, "Options"_=
					{
						"Verbose?"_=
						{
							"Names"_= {"--verbose", "-v"}
							, "Default"_= false
							, "Description"_= "Display more extensive information about the ca."
						}
						, SettingsOption_Authority
						, SettingsOption_User
						, CTableRenderHelper::fs_OutputTypeOption()
					}
				}
				, [this](CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
				{
					return g_Future <<= self(&CMongoCertificateManagerActor::fp_CommandLine_UserList, _Params, _pCommandLine);
				}
			)
		;
		UserManagement.f_RegisterCommand
			(
				{
					"Names"_= {"--user-resync"}
					, "Description"_= "Update users on out of date secret managers."
					, "Options"_=
					{
						SettingsOption_Authority
						, SettingsOption_User
					}
				}
				, [this](CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
				{
					return g_Future <<= self(&CMongoCertificateManagerActor::fp_CommandLine_UserResync, _Params, _pCommandLine);
				}
			)
		;
		UserManagement.f_RegisterCommand
			(
				{
					"Names"_= {"--user-reissue-certificate"}
					, "Description"_= "Reissue certificates that are about to expire."
					, "Options"_=
					{
						"Days?"_=
						{
							"Names"_= {"--days", "-v"}
							, "Default"_= 365
							, "Description"_= "Reissue certificates that are about to expire within these number of days."
						}
						, SettingsOption_Authority
						, SettingsOption_User
					}
				}
				, [this](CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
				{
					return g_Future <<= self(&CMongoCertificateManagerActor::fp_CommandLine_UserReissue, _Params, _pCommandLine);
				}
			)
		;
	}
}
