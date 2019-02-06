
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
			DLogWithCategory(MongoManager/Backup, Info, "Starting initial full backup");
			TCPromise<void> Promise;
			
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
			TCSharedPointer<CCanDestroyTracker> pCanDestroy = fg_Move(mp_pCanDestroy);

			mp_FileWriteActor->f_Destroy() > pCanDestroy->f_Track();
			
			if (mp_Backup)
				mp_Backup->f_Destroy() > pCanDestroy->f_Track();
			
			return pCanDestroy->f_Future();
		}
		
		TCFuture<void> fp_CleanupOldBackups()
		{
			if (!mp_FileWriteActor)
				mp_FileWriteActor = fg_ConstructActor<CSeparateThreadActor>(fg_Construct("Global file write actor"));
			
			TCPromise<void> Result;

			auto pCanDestroy = mp_pCanDestroy;
			DLogWithCategory(MongoManager/Backup, Info, "Scheduling remove of old backups");
			
			mp_FileWriteActor
				(
					&CActor::f_DispatchWithReturn<TCFuture<void>>
					, [pCanDestroy]
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
				) 
				> [Result](TCAsyncResult<void> &&_Result)
				{
					if (!_Result)
						DLogWithCategory(MongoManager/Backup, Error, "Failed to clean up old backups: {}", _Result.f_GetExceptionStr());
					Result.f_SetResult();
				}
			;
			
			return Result.f_MoveFuture();
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
		
		for (auto &fPending : mp_PendingBackupStart)
			fPending(false);
		
		mp_PendingBackupStart.f_Clear();
	}
	
	TCFuture<CActorSubscription> CMongoManagerActor::f_StartBackup
		(
			TCDistributedActorInterface<CDistributedAppInterfaceBackup> &&_BackupInterface
			, CActorSubscription &&_ManifestFinished
			, CStr const &_BackupRoot
		)
	{
		if (mp_bDestroyed)
			return DErrorInstance("Destroyed");

		if (mp_Mode != EMode_Normal)
			return fg_Explicit();
		
		if (auto pValue = mp_AppState.m_ConfigDatabase.m_Data.f_GetMember("BackupEnable", EJSONType_Boolean))
		{
			if (!pValue->f_Boolean())
				return fg_Explicit();
		}
		
		CStr BackupID = fg_RandomID();
		
		mp_MongoBackupManagerActors[BackupID];
		
		auto Subscription = g_ActorSubscription / [this, BackupID]() -> TCFuture<void>
			{
				auto pActor = mp_MongoBackupManagerActors.f_FindEqual(BackupID);
				if (!pActor || !*pActor)
					return fg_Explicit();
				
				return (*pActor)->f_Destroy();
			}
		;
		
		TCPromise<CActorSubscription> Promise;
		
		auto fStartBackup = [=, BackupInterface = fg_Move(_BackupInterface), Subscription = fg_Move(Subscription), ManifestFinished = fg_Move(_ManifestFinished)](bool _bAbort) mutable
			{
				if (_bAbort)
				{
					Promise.f_SetException(DErrorInstance("Destroyed"));
					return;
				}
				
				auto *pBackupActor = mp_MongoBackupManagerActors.f_FindEqual(BackupID);
				if (!pBackupActor)
				{
					Promise.f_SetException(DErrorInstance("Backup actor gone"));
					return;
				}
				
				auto &BackupActor = *pBackupActor;
				
				BackupActor = fg_ConstructActor<CMongoBackupManagerActor>
					(
						mp_MongoConnectionSettings
						, fp_GetMongoExecutable("mongodump")
						, fg_Move(BackupInterface)
					)
				;
				
				BackupActor(&CBackupManagerActorInterface::f_StartBackup, fg_Move(ManifestFinished), _BackupRoot)
					> Promise / [Subscription = fg_Move(Subscription), Promise]() mutable
					{
						DLogWithCategory(MongoManager/Backup, Info, "Oplog is tailing");
						Promise.f_SetResult(fg_Move(Subscription));
					}
				;
			}
		;
		
		if (mp_bMongoBackupCanStart)
			fStartBackup(false);
		else
			mp_PendingBackupStart.f_Insert(fg_Move(fStartBackup));
		
		return Promise.f_MoveFuture();
	}
}
