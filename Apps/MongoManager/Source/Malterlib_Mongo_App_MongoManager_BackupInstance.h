// Copyright © 2024 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_App_MongoManager_Server.h"

#include <Mib/Web/DDPClient>
#include <Mib/Process/ProcessLaunchActor>
#include <Mib/Mongo/Client>
#include <Mib/Cloud/BackupManager>

namespace NMib::NMongo::NMongoManager
{
	struct CMongoBackupInstanceActor : public CActor
	{
		CMongoBackupInstanceActor
			(
				CMongoConnectionSettings const &_MongoConnectionSettings
				, CStr const &_MongoExecutable
				, TCDistributedActor<CDistributedAppInterfaceBackup> const &_BackupInterface
			)
		;
		~CMongoBackupInstanceActor();

		TCFuture<void> f_StartBackup(CActorSubscription _ManifestFinished, CStr _BackupRoot);
		
		void f_MongoStopped();
		
	private:
		TCFuture<void> fp_Destroy() override;
		
		TCFuture<TCSharedPointer<CFile>> fp_OpenBackupFiles();
		TCFuture<void> fp_DumpDatabase();
		TCFuture<void> fp_TailOplog(TCSharedPointer<CFile> _pBackupFile);
		TCFuture<void> fp_SavePendingOplogData(TCSharedPointer<CFile> _pBackupFile);
		TCFuture<void> fp_DeleteBackup();
		TCFuture<void> fp_MarkBackupFinished();
		
	private:

		TCDistributedActor<CDistributedAppInterfaceBackup> mp_BackupInterface;
		TCActor<CMongoClientActor> mp_MongoClient;
		TCActor<CProcessLaunchActor> mp_DumpProcessLaunch;
		CActorSubscription mp_MongoTailSubscription;
		CMongoConnectionSettings mp_MongoConnectionSettings;
		CSequencer mp_WriteSequencer{"BackupInstance"};
		CSequencer mp_OplogWriteSequencer{"BackupInstance"};

		NCloud::CBackupManager::CBackupKey mp_BackupKey;
		
		CStr mp_MongoExecutable;
		CStr mp_BackupDirectory;
		CTime mp_BackupTime;
		CStr mp_BackupID;
		TCSharedPointer<CCanDestroyTracker> mp_pCanDestroy;
		TCVector<NEncoding::CEJSONOrdered> mp_PendingOplogData;
		CActorSubscription mp_InitialBackupFinishedSubscription;
		CActorSubscription mp_BackupStoppedSubscription;
		bool mp_PendingSaveScheduled = false;
		bool mp_bInitialDumpFinished = false;
		bool mp_bInitialBackupUploaded = false;
		bool mp_bBackupStopped = false;
		bool mp_bMongoStopped = false;
	};
}
