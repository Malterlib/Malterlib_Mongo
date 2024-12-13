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

		auto SettingsOption_EllipticCurveType = "EllipticCurveType?"_o=
			{
				"Names"_o= {"--elliptic-curve-type"}
				, "Default"_o= "secp521r1"
				, "Type"_o= COneOf{"secp256r1", "secp384r1", "secp521r1", "X25519"}
				, "Description"_o= "The type of elliptic curve to use for the EC certificate."
			}
		;

		auto SettingsOption_RSASize = "RSASize?"_o=
			{
				"Names"_o= {"--rsa-size"}
				, "Type"_o= 4096
				, "Description"_o= "The size of the RSA certificate."
			}
		;

		auto SettingsOption_Authority = "Authority?"_o=
			{
				"Names"_o= {"--authority"}
				, "Default"_o= ""
				, "Type"_o= ""
				, "Description"_o= "The certificate authority to use"
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
					"Names"_o= {"--authority-create"}
					, "Description"_o= "Create a certificate authority\n"
					, "Options"_o=
					{
						"Name"_o=
						{
							"Names"_o= {"--name"}
							, "Type"_o= ""
							, "Description"_o= "Name of the certificate authority"
						}
						, SettingsOption_EllipticCurveType
						, SettingsOption_RSASize
					}
				}
				, [this](CEJSONSorted &&_Params, NStorage::TCSharedPointer<CCommandLineControl> &&_pCommandLine)
				{
					return fp_CommandLine_AuthorityCreate(fg_Move(_Params), fg_Move(_pCommandLine));
				}
			)
		;
		AuthorityManagement.f_RegisterCommand
			(
				{
					"Names"_o= {"--authority-list"}
					, "Description"_o= "List certificate authorities."
					, "Options"_o=
					{
						"Verbose?"_o=
						{
							"Names"_o= {"--verbose", "-v"}
							, "Default"_o= false
							, "Description"_o= "Display more extensive information about the ca."
						}
						, SettingsOption_Authority
						, CTableRenderHelper::fs_OutputTypeOption()
					}
				}
				, [this](CEJSONSorted &&_Params, NStorage::TCSharedPointer<CCommandLineControl> &&_pCommandLine)
				{
					return fp_CommandLine_AuthorityList(fg_Move(_Params), fg_Move(_pCommandLine));
				}
			)
		;
		AuthorityManagement.f_RegisterCommand
			(
				{
					"Names"_o= {"--authority-resync"}
					, "Description"_o= "Update certificate authorities on out of date secret managers."
					, "Options"_o=
					{
						SettingsOption_Authority
					}
				}
				, [this](CEJSONSorted &&_Params, NStorage::TCSharedPointer<CCommandLineControl> &&_pCommandLine)
				{
					return fp_CommandLine_AuthorityResync(fg_Move(_Params), fg_Move(_pCommandLine));
				}
			)
		;

		auto UserManagement = o_CommandLine.f_AddSection("User Management", "Commands to manage MongoCertificateManager users");

		auto SettingsOption_User = "User?"_o=
			{
				"Names"_o= {"--user"}
				, "Default"_o= ""
				, "Type"_o= ""
				, "Description"_o= "Name of the user"
			}
		;

		UserManagement.f_RegisterCommand
			(
				{
					"Names"_o= {"--user-create"}
					, "Description"_o= "Create a user\n"
					, "Options"_o=
					{
						fStripOptional(fStripDefault(SettingsOption_Authority))
						, fStripOptional(fStripDefault(SettingsOption_User))
						, "Type?"_o=
						{
							"Names"_o= {"--type"}
							, "Default"_o= "user"
							, "Type"_o= COneOf{"user", "server"}
							, "Description"_o= "The type of user to create."
						}
						, SettingsOption_EllipticCurveType
						, SettingsOption_RSASize
					}
				}
				, [this](CEJSONSorted &&_Params, NStorage::TCSharedPointer<CCommandLineControl> &&_pCommandLine)
				{
					return fp_CommandLine_UserCreate(fg_Move(_Params), fg_Move(_pCommandLine));
				}
			)
		;
		UserManagement.f_RegisterCommand
			(
				{
					"Names"_o= {"--user-list"}
					, "Description"_o= "List certificate authorities."
					, "Options"_o=
					{
						"Verbose?"_o=
						{
							"Names"_o= {"--verbose", "-v"}
							, "Default"_o= false
							, "Description"_o= "Display more extensive information about the ca."
						}
						, SettingsOption_Authority
						, SettingsOption_User
						, CTableRenderHelper::fs_OutputTypeOption()
					}
				}
				, [this](CEJSONSorted &&_Params, NStorage::TCSharedPointer<CCommandLineControl> &&_pCommandLine)
				{
					return fp_CommandLine_UserList(fg_Move(_Params), fg_Move(_pCommandLine));
				}
			)
		;
		UserManagement.f_RegisterCommand
			(
				{
					"Names"_o= {"--user-resync"}
					, "Description"_o= "Update users on out of date secret managers."
					, "Options"_o=
					{
						SettingsOption_Authority
						, SettingsOption_User
					}
				}
				, [this](CEJSONSorted &&_Params, NStorage::TCSharedPointer<CCommandLineControl> &&_pCommandLine)
				{
					return fp_CommandLine_UserResync(fg_Move(_Params), fg_Move(_pCommandLine));
				}
			)
		;
		UserManagement.f_RegisterCommand
			(
				{
					"Names"_o= {"--user-reissue-certificate"}
					, "Description"_o= "Reissue certificates that are about to expire."
					, "Options"_o=
					{
						"Days?"_o=
						{
							"Names"_o= {"--days", "-v"}
							, "Default"_o= 365
							, "Description"_o= "Reissue certificates that are about to expire within these number of days."
						}
						, SettingsOption_Authority
						, SettingsOption_User
					}
				}
				, [this](CEJSONSorted &&_Params, NStorage::TCSharedPointer<CCommandLineControl> &&_pCommandLine)
				{
					return fp_CommandLine_UserReissue(fg_Move(_Params), fg_Move(_pCommandLine));
				}
			)
		;
	}
}
