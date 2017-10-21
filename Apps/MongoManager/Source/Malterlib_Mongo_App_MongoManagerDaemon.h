// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedApp>
#include <Mib/Daemon/Daemon>

namespace NMib::NMongo::NMongoManager
{
	struct CMongoManagerActor;
	struct CMongoManagerDaemonActor : public CDistributedAppActor
	{
		CMongoManagerDaemonActor();
		~CMongoManagerDaemonActor();
		
	private:
		
		struct CLocalBackup
		{
			TCDistributedActor<CDistributedAppInterfaceBackup> m_BackupInterface;
			CActorSubscription m_Subscription;
		};
		
		TCContinuation<void> fp_StartApp(NEncoding::CEJSON const &_Params) override;
		TCContinuation<void> fp_StopApp() override;
		TCContinuation<void> fp_PreStop() override;
		void fp_PopulateAppInterfaceRegisterInfo(CDistributedAppInterfaceServer::CRegisterInfo &o_RegisterInfo, NEncoding::CEJSON const &_Params) override;
		TCContinuation<CActorSubscription> fp_StartBackup
			(
				TCDistributedActorInterface<CDistributedAppInterfaceBackup> &&_BackupInterface
				, CActorSubscription &&_ManifestFinished
				, CStr const &_BackupRoot
			) override
		;
		
		void fp_BuildCommandLine(CDistributedAppCommandLineSpecification &o_CommandLine) override; 
		uint32 fp_CommandLine_ListRestoreRange(NEncoding::CEJSON const &_Params);
		TCContinuation<uint32> fp_CommandLine_Restore(NEncoding::CEJSON const &_Params, NPtr::TCSharedPointer<CCommandLineControl> const &_pCommandLine);
		TCContinuation<uint32> fp_CommandLine_UpdateReplicationConfig(NEncoding::CEJSON const &_Params, NPtr::TCSharedPointer<CCommandLineControl> const &_pCommandLine);
		TCContinuation<uint32> fp_CommandLine_SetupPermissions(NEncoding::CEJSON const &_Params, NPtr::TCSharedPointer<CCommandLineControl> const &_pCommandLine);
		TCContinuation<uint32> fp_CommandLine_JoinReplica(NEncoding::CEJSON const &_Params, NPtr::TCSharedPointer<CCommandLineControl> const &_pCommandLine);
		TCContinuation<uint32> fp_CommandLine_RunBackup(NEncoding::CEJSON const &_Params, NPtr::TCSharedPointer<CCommandLineControl> const &_pCommandLine);
		TCContinuation<uint32> fp_CommandLine_CancelBackups(NEncoding::CEJSON const &_Params, NPtr::TCSharedPointer<CCommandLineControl> const &_pCommandLine);
		
		TCActor<CMongoManagerActor> mp_pManager;
		TCMap<uint32, CLocalBackup> mp_LocalBackups;
		uint32 mp_NextLocalBackup = 0;
	};
}
