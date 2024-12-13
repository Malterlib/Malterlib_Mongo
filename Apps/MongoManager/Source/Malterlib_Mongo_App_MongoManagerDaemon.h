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
		
		TCFuture<void> fp_StartApp(NEncoding::CEJSONSorted const _Params) override;
		TCFuture<void> fp_StopApp() override;
		TCFuture<void> fp_Destroy() override;
		TCFuture<void> fp_PreStop() override;
		void fp_PopulateAppInterfaceInfo
			(
				CDistributedAppInterfaceServer::CRegisterInfo &o_RegisterInfo
				, CDistributedAppInterfaceServer::CConfigFiles &o_ConfigFiles
				, NEncoding::CEJSONSorted const &_Params
			) override
		;
		TCFuture<CActorSubscription> fp_StartBackup
			(
				TCDistributedActorInterface<CDistributedAppInterfaceBackup> _BackupInterface
				, CActorSubscription _ManifestFinished
				, CStr _BackupRoot
			) override
		;
		
		void fp_BuildCommandLine(CDistributedAppCommandLineSpecification &o_CommandLine) override; 
		uint32 fp_CommandLine_ListRestoreRange(NEncoding::CEJSONSorted const &_Params);
		TCFuture<uint32> fp_CommandLine_Restore(NEncoding::CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);
		TCFuture<uint32> fp_CommandLine_UpdateReplicationConfig(NEncoding::CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);
		TCFuture<uint32> fp_CommandLine_SetupPermissions(NEncoding::CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);
		TCFuture<uint32> fp_CommandLine_JoinReplica(NEncoding::CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);
		TCFuture<uint32> fp_CommandLine_RunBackup(NEncoding::CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);
		TCFuture<uint32> fp_CommandLine_CancelBackups(NEncoding::CEJSONSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);
		
		TCActor<CMongoManagerActor> mp_pManager;
		TCMap<uint32, CLocalBackup> mp_LocalBackups;
		uint32 mp_NextLocalBackup = 0;
	};
}
