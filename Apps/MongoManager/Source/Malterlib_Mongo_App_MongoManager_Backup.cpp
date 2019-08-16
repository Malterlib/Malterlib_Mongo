
#include "Malterlib_Mongo_App_MongoManager_Server.h"
#include "Malterlib_Mongo_App_MongoManager_BackupInstance.h"

#include <Mib/Concurrency/Actor/Timer>
#include <Mib/Concurrency/ActorSubscription>
#include <Mib/Cloud/BackupManager>
#include <Mib/Cryptography/RandomID>

namespace NMib::NMongo::NMongoManager
{
	struct CMongoBackupManagerActor : public CBackupManagerActorInterface
	{
	private:

	public:
		
		CMongoBackupManagerActor
			(
				CMongoConnectionSettings const &_MongoConnectionSettings
				, CStr const &_MongoExecutable
				, TCDistributedActorInterface<CDistributedAppInterfaceBackup> &&_BackupInterface
			)
			: mp_MongoConnectionSettings(_MongoConnectionSettings)
			, mp_MongoExecutable(_MongoExecutable)
			, mp_pCanDestroy(fg_Construct())
			, mp_BackupInterface(fg_Move(_BackupInterface))
		{
		}
		
		TCFuture<void> f_StartBackup(CActorSubscription &&_ManifestFinished, CStr const &_BackupRoot) override
		{
			TCPromise<void> Promise;

			DLogWithCategory(MongoManager/Backup, Info, "Starting initial full backup");

			fp_CleanupOldBackups() > Promise / [=, ManifestFinished = fg_Move(_ManifestFinished)]() mutable
				{
					mp_Backup = fg_ConstructActor<CMongoBackupInstanceActor>(mp_MongoConnectionSettings, mp_MongoExecutable, mp_BackupInterface.f_GetActor());
					mp_Backup(&CMongoBackupInstanceActor::f_StartBackup, fg_Move(ManifestFinished), _BackupRoot) > Promise;
				}
			;
			
			return Promise.f_MoveFuture();
		}
		
		void f_MongoStopped() override
		{
			if (!mp_Backup)
				return;
			mp_Backup(&CMongoBackupInstanceActor::f_MongoStopped) > fg_DiscardResult();
		}
		
	private:
		TCFuture<void> fp_Destroy() override
		{
			co_await mp_FileWriteActor.f_Destroy();
			
			if (mp_Backup)
				co_await mp_Backup.f_Destroy();

			auto Future = mp_pCanDestroy->f_Future();
			mp_pCanDestroy.f_Clear();

			co_await fg_Move(Future);
			
			co_return {};
		}
		
		TCFuture<void> fp_CleanupOldBackups()
		{
			if (!mp_FileWriteActor)
				mp_FileWriteActor = fg_ConstructActor<CSeparateThreadActor>(fg_Construct("Global file write actor"));
			
			auto pCanDestroy = mp_pCanDestroy;
			DLogWithCategory(MongoManager/Backup, Info, "Scheduling remove of old backups");

			co_await
				(
					g_Dispatch(mp_FileWriteActor) / []
					{
						return TCFuture<void>::fs_RunProtected<CExceptionFile>() / [&]()
							{
								CFile::CFindFilesOptions Options{fg_Format("{}/Backup/*", CFile::fs_GetProgramDirectory()), false};
								Options.m_AttribMask = EFileAttrib_Directory;
								auto FoundFiles = CFile::fs_FindFiles(Options);
								CTime RemoveOlderThan = CTime::fs_NowUTC() - CTimeSpanConvert::fs_CreateWeekSpan(1);
								DLogWithCategory(MongoManager/Backup, Info, "Found {} old backups", FoundFiles.f_GetLen());
								for (auto &File : FoundFiles)
								{
									if (File.m_Attribs & EFileAttrib_Link)
										continue;

									CStr FileName = CFile::fs_GetFile(File.m_Path);
									aint nParsed = 0;
									uint64 Year;
									uint32 Month;
									uint32 Day;
									(CStr::CParse("{}-{}-{} ") >> Year >> Month >> Day).f_Parse(FileName, nParsed);

									if (nParsed != 3)
										continue; // Skip stray files

									CTime BackupTime = CTimeConvert::fs_CreateTime(Year, Month, Day);
									if (BackupTime < RemoveOlderThan)
									{
										try
										{
											CFile::fs_DeleteDirectoryRecursive(File.m_Path);
											DLogWithCategory(MongoManager/Backup, Info, "Removed backup: {}", File.m_Path);
										}
										catch (CExceptionFile const &_Exception)
										{
											DLogWithCategory(MongoManager/Backup, Error, "Failed to remove backup: {}:{\n}{}", File.m_Path, _Exception);
										}
									}
								}
							}
						;
					}
					% "Failed to clean up old backups"
				)
			;

			co_return {};
		}
		
	private:
		CMongoConnectionSettings mp_MongoConnectionSettings;
		CStr mp_MongoExecutable;
		
		TCActor<CSeparateThreadActor> mp_FileWriteActor;
		TCActor<CMongoBackupInstanceActor> mp_Backup;
		TCSharedPointer<CCanDestroyTracker> mp_pCanDestroy;
		
		TCDistributedActorInterface<CDistributedAppInterfaceBackup> mp_BackupInterface;
	};

	void CMongoManagerActor::fp_StartMongoBackup()
	{
		mp_bMongoBackupCanStart = true;
		
		for (auto &Pending : mp_PendingBackupStart)
			Pending.f_SetResult();
		
		mp_PendingBackupStart.f_Clear();
	}
	
	TCFuture<CActorSubscription> CMongoManagerActor::f_StartBackup
		(
			TCDistributedActorInterface<CDistributedAppInterfaceBackup> &&_BackupInterface
			, CActorSubscription &&_ManifestFinished
			, CStr const &_BackupRoot
		)
	{
		if (f_IsDestroyed())
			co_return DErrorInstance("Destroyed");

		if (mp_Mode != EMode_Normal)
			co_return {};
		
		if (auto pValue = mp_AppState.m_ConfigDatabase.m_Data.f_GetMember("BackupEnable", EJSONType_Boolean))
		{
			if (!pValue->f_Boolean())
				co_return {};
		}
		
		CStr BackupID = fg_RandomID();
		
		mp_MongoBackupManagerActors[BackupID];
		
		auto Subscription = g_ActorSubscription / [this, BackupID]() -> TCFuture<void>
			{
				auto pActor = mp_MongoBackupManagerActors.f_FindEqual(BackupID);
				if (!pActor || !*pActor)
					co_return {};
				
				co_await fg_Move(*pActor).f_Destroy();

				co_return {};
			}
		;
		
		if (!mp_bMongoBackupCanStart)
			co_await mp_PendingBackupStart.f_Insert().f_Future();

		auto *pBackupActor = mp_MongoBackupManagerActors.f_FindEqual(BackupID);
		if (!pBackupActor)
			co_return DErrorInstance("Backup actor gone");

		auto &BackupActor = *pBackupActor;

		BackupActor = fg_ConstructActor<CMongoBackupManagerActor>
			(
				mp_MongoConnectionSettings
				, fp_GetMongoExecutable("mongodump")
				, fg_Move(_BackupInterface)
			)
		;

		co_await BackupActor(&CBackupManagerActorInterface::f_StartBackup, fg_Move(_ManifestFinished), _BackupRoot);

		DLogWithCategory(MongoManager/Backup, Info, "Oplog is tailing");

		co_return fg_Move(Subscription);
	}
}
