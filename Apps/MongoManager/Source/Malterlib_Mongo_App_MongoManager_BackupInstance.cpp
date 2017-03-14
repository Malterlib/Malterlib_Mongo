
#include "Malterlib_Mongo_App_MongoManager_Server.h"
#include "Malterlib_Mongo_App_MongoManager_BackupInstance.h"

namespace NMib::NMongo::NMongoManager
{
	CMongoBackupInstanceActor::CMongoBackupInstanceActor
		(
			CMongoConnectionSettings const &_MongoConnectionSettings
			, CStr const &_MongoExecutable
			, TCActor<CDistributedActorTrustManager> const &_TrustManager
		)
		: mp_MongoConnectionSettings(_MongoConnectionSettings)
		, mp_MongoExecutable(_MongoExecutable)
		, mp_pCanDestroy(fg_Construct())
		, mp_OnEventCallback(this, false)
		, mp_TrustManager(_TrustManager)
	{
		mp_BackupTime = NTime::CTime::fs_NowUTC();
		mp_BackupID = CDDPClient::fs_RandomID();
		
		mp_BackupKey.m_ID = mp_BackupID;
		mp_BackupKey.m_Time = mp_BackupTime;
		mp_BackupKey.m_FriendlyName = NProcess::NPlatform::fg_Process_GetComputerName();
		
		mp_BackupDirectory = fg_Format("{}/Backup/{tst.} - {}", CFile::fs_GetProgramDirectory(), mp_BackupTime, mp_BackupID);
		mp_BackupPath[EBackupState_Dump] = mp_BackupDirectory + "/Backup.tar.gz";
		mp_BackupPath[EBackupState_Oplog] = mp_BackupDirectory + "/DynamicOplog.bson";

		mp_FileWriteActor = fg_ConstructActor<CSeparateThreadActor>(fg_Construct("File write actor"));
	}
	
	CMongoBackupInstanceActor::~CMongoBackupInstanceActor()
	{
	}
	
	void CMongoBackupInstanceActor::fp_SubscribeToBackupServers()
	{
		if (!mp_pCanDestroy)
			return;
		DLogWithCategory(Backup, Info, "Subscribing to backup servers.");
		
		mp_TrustManager
			(
				&CDistributedActorTrustManager::f_SubscribeTrustedActors<NCloud::CBackupManager>
				, "com.malterlib/Cloud/BackupManager"
				, fg_ThisActor(this)
			)
			> [this](TCAsyncResult<TCTrustedActorSubscription<NCloud::CBackupManager>> &&_Subscription)
			{
				if (!_Subscription)
				{
					DLogWithCategory(Backup, Error, "Failed to subscribe to backup servers: {}", _Subscription.f_GetExceptionStr());
					return;
				}
				mp_BackupServerActorsSubscription = fg_Move(*_Subscription);
				
				mp_BackupServerActorsSubscription.f_OnActor
					(
						[this](TCDistributedActor<NCloud::CBackupManager> const &_BackupManager, CTrustedActorInfo const &_ActorInfo)
						{
							auto &Connection = mp_BackupManagers[_BackupManager];
							fp_BackupConnectionConnected(&Connection);
						}
					)
				;
				
				mp_BackupServerActorsSubscription.f_OnRemoveActor
					(
						[this](TCWeakDistributedActor<CActor> const &_RemovedActor)
						{
							auto pConnection = mp_BackupManagers.f_FindEqual(_RemovedActor);
							if (pConnection)
							{
								DLogWithCategory(Backup, Error, "Stop backup to backup server because actor was removed '{}'", pConnection->m_FriendlyName);
								pConnection->f_Clear(mp_BackupKey);
								mp_BackupManagers.f_Remove(pConnection);
							}
						}
					)
				;
			}
		;
	}		
	
	TCContinuation<CActorSubscription> CMongoBackupInstanceActor::f_StartBackup(TCActor<CActor> const &_CallbackActor, TCFunction<void (CBackupCallbackEvent const &_Event)> &&_fOnEvent)
	{
		TCContinuation<CActorSubscription> Result;
		if (!mp_pCanDestroy)
		{
			Result.f_SetException(DErrorInstance("Shutting down"));
			return Result;
		}
		fp_SubscribeToBackupServers();
		
		mp_MongoClient = fg_ConstructActor<CMongoClientActor>(fg_Construct("Mongo client connection"), mp_MongoConnectionSettings, "local");
		
		auto OnEventSubscription = mp_OnEventCallback.f_Register(_CallbackActor, fg_Move(_fOnEvent));

		auto pCanDestroy = mp_pCanDestroy;
		
		mp_FileWriteActor
			(
				&CActor::f_DispatchWithReturn<TCContinuation<TCSharedPointer<CFile>>>
				,
				[
					pCanDestroy
					, Result
					, BackupDirectory = mp_BackupDirectory
					, OplogPath = mp_BackupPath[EBackupState_Oplog]
				]
				{
					return TCContinuation<TCSharedPointer<CFile>>::fs_RunProtected<CExceptionFile>()
						> [&]()
						{
							CFile::fs_CreateDirectory(BackupDirectory + "/Package/MongoDump");
							
							CStr LatestSymlink = fg_Format("{}/Backup/Latest", CFile::fs_GetProgramDirectory());
							if (CFile::fs_FileExists(LatestSymlink))
								CFile::fs_DeleteFile(LatestSymlink);
							CFile::fs_CreateSymbolicLink(CFile::fs_GetFile(BackupDirectory), LatestSymlink, EFileAttrib_Directory, ESymbolicLinkFlag_Relative);

							TCSharedPointer<CFile> pOplogFile = fg_Construct();
							pOplogFile->f_Open(OplogPath, EFileOpen_Write | EFileOpen_NoLocalCache | EFileOpen_ShareRead);							
							return fg_Move(pOplogFile);
						}
					;
				}
			) 
			> [this, Result, OnEventSubscription = fg_Move(OnEventSubscription)](TCAsyncResult<TCSharedPointer<CFile>> &&_Result) mutable
			{
				if (!_Result)
				{
					DLogWithCategory(Backup, Error, "Failed to open backup files: {}", _Result.f_GetExceptionStr());
					Result.f_SetException(_Result);
					return;
				}
				CMongoBackupInstanceActor::fp_TailOplog(fg_Move(*_Result), Result, fg_Move(OnEventSubscription));
			}
		;
		
		return Result;
	}

	TCContinuation<void> CMongoBackupInstanceActor::fp_DeleteBackup()
	{
		bool bInitialDumpFinished = mp_bInitialBackupFinished[EBackupState_Dump];

		if (!mp_bInitialBackupUploaded[EBackupState_Dump] && bInitialDumpFinished)
		{
			DLogWithCategory(Backup, Info, "Saving backup which has not yet finished transferring to remote server: {}", mp_BackupDirectory);
			return TCContinuation<void>::fs_Finished(); // If we haven't uploaded this backup yet, keep it around and let the main backup actor clean it out after a week
		}
		
		TCContinuation<void> Result;

		auto pCanDestroy = mp_pCanDestroy;
		
		mp_FileWriteActor
			(
				&CActor::f_DispatchWithReturn<TCContinuation<void>>
				,
				[
					pCanDestroy
					, Result
					, BackupDirectory = mp_BackupDirectory
					, OplogPath = mp_BackupPath[EBackupState_Oplog]
					, bInitialDumpFinished
				]
				{
					return TCContinuation<void>::fs_RunProtected<CExceptionFile>()
						> [&]()
						{
							CFile::fs_DeleteDirectoryRecursive(BackupDirectory, true);
							if (bInitialDumpFinished)
								DLogWithCategory(Backup, Info, "Deleted backup which has fully transferred to remote server: {}", BackupDirectory);
							else
								DLogWithCategory(Backup, Info, "Deleted backup which has not yet finished the full dump: {}", BackupDirectory);
						}
					;
				}
			) 
			> [this, Result](TCAsyncResult<void> &&_Result)
			{
				if (!_Result)
					DLogWithCategory(Backup, Error, "Failed to delete the backup: {}", _Result.f_GetExceptionStr());
				Result.f_SetResult();
			}
		;
		
		return Result;
	}
	
	TCContinuation<void> CMongoBackupInstanceActor::fp_Destroy()
	{
		auto pCanDestroy = fg_Move(mp_pCanDestroy);
		mp_MongoTailCallback.f_Clear();
		
		if (!mp_bInitialBackupUploaded[EBackupState_Dump])
			DLogWithCategory(Backup, Error, "Aborting backup before the initial full backup has finished uploading");
		
		TCActorResultVector<void> AllDestroyed;
		for (auto &Connection :mp_BackupManagers)
			Connection.f_Clear(mp_BackupKey);
		
		if (mp_CompressProcessLaunch)
			mp_CompressProcessLaunch->f_Destroy(AllDestroyed.f_AddResult());
		if (mp_DumpProcessLaunch)
			mp_DumpProcessLaunch->f_Destroy(AllDestroyed.f_AddResult());
		if (mp_MongoClient)
			mp_MongoClient->f_Destroy(AllDestroyed.f_AddResult());
		
		AllDestroyed.f_GetResults()
			> [this, pCanDestroy](TCAsyncResult<TCVector<TCAsyncResult<void>>> &&_Results)
			{
				fg_ThisActor(this)(&CMongoBackupInstanceActor::fp_DeleteBackup)
					> [this, pCanDestroy](TCAsyncResult<void> &&_Result)
					{
						mp_FileWriteActor->f_Destroy([pCanDestroy](TCAsyncResult<void> &&){ });
					}
				;
			}
		;
			
		return pCanDestroy->m_Continuation;
	}
}
