// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include <Mib/Core/Application>

#include "Malterlib_Mongo_App_MongoCertificateManager.h"

using namespace NMib;
using namespace NMib::NMongo::NMongoCertificateManager;

class CMongoCertificateManager : public CApplication
{
	aint f_Main()
	{
		NConcurrency::CDistributedDaemon Daemon
			{
				"MalterlibMongoMongoCertificateManager"
				, "Malterlib Mongo Certificate Manager"
				, "Manages issuing and reissuing of MongoDB certificates"
				, []
				{
					return fg_ConstructActor<CMongoCertificateManagerActor>();
				}
			}
		;

		return Daemon.f_Run();
	}
};

DAppImplement(CMongoCertificateManager);
