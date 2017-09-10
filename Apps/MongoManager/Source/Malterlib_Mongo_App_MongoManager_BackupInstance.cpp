
#include "Malterlib_Mongo_App_MongoManager_Server.h"
#include "Malterlib_Mongo_App_MongoManager_BackupInstance.h"

namespace NMib::NMongo::NMongoManager
{
	CMongoBackupInstanceActor::CMongoBackupInstanceActor
		(
			CMongoConnectionSettings const &_MongoConnectionSettings
			, CStr const &_MongoExecutable
			, TCDistributedActor<CDistributedAppInterfaceBackup> const &_BackupInterface
		)
		: mp_MongoConnectionSettings(_MongoConnectionSettings)
		, mp_MongoExecutable(_MongoExecutable)
		, mp_pCanDestroy(fg_Construct())
		, mp_BackupInterface(_BackupInterface)
	{
		mp_BackupTime = NTime::CTime::fs_NowUTC();
		mp_BackupID = CDDPClient::fs_RandomID();
		
		mp_BackupKey.m_ID = mp_BackupID;
		mp_BackupKey.m_Time = mp_BackupTime;
		mp_BackupKey.m_FriendlyName = NProcess::NPlatform::fg_Process_GetComputerName();
		
		mp_BackupDirectory = fg_Format("{}/Backup/{tst.} - {}", CFile::fs_GetProgramDirectory(), mp_BackupTime, mp_BackupID);

		mp_FileWriteActor = fg_ConstructActor<CSeparateThreadActor>(fg_Construct("File write actor"));
		
		DCallActor
			(
				mp_BackupInterface
				, CDistributedAppInterfaceBackup::f_SubscribeBackupStopped
				, g_ActorFunctor > [this, AllowDestroy = g_AllowWrongThreadDestroy]() -> TCContinuation<void>
				{
					DMibLogWithCategory
						(
							MongoManager/Backup
							, Debug
							, "Received backup stopped notification"
						)
					;
					
					mp_bBackupStopped = true;
					return fg_Explicit();
				}
			)
			> [this](TCAsyncResult<TCActorSubscriptionWithID<>> &&_Subscription)
			{
				if (!_Subscription)
				{
					DLogWithCategory(MongoManager/Backup, Error, "Failed to subscribe to backup stopped: {}", _Subscription.f_GetExceptionStr());
					return;
				}
				
				mp_BackupStoppedSubscription = fg_Move(*_Subscription);
			}
		;
	}
	
	CMongoBackupInstanceActor::~CMongoBackupInstanceActor()
	{
	}
	
	TCContinuation<TCSharedPointer<CFile>> CMongoBackupInstanceActor::fp_OpenBackupFiles()
	{
		if (!mp_pCanDestroy)
			return DErrorInstance("Destroyed");

		auto pCanDestroy = mp_pCanDestroy;
		
		TCContinuation<TCSharedPointer<CFile> > Continuation;
		mp_FileWriteActor
			(
				&CActor::f_DispatchWithReturn<TCContinuation<TCSharedPointer<CFile>>>
				,
				[
					BackupDirectory = mp_BackupDirectory
					, OplogPath = mp_BackupDirectory + "/DynamicOplog.bson"
				]
				{
					return TCContinuation<TCSharedPointer<CFile>>::fs_RunProtected<CExceptionFile>()
						> [&]()
						{
							CFile::fs_CreateDirectory(BackupDirectory + "/Dump");
							
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
			> [Continuation, pCanDestroy](TCAsyncResult<TCSharedPointer<CFile>> &&_Result) mutable
			{
				if (!_Result)
				{
					DLogWithCategory(MongoManager/Backup, Error, "Failed to open backup files: {}", _Result.f_GetExceptionStr());
					Continuation.f_SetException(_Result);
					return;
				}
				Continuation.f_SetResult(fg_Move(*_Result));
			}
		;
		
		return Continuation;
	}

	void CMongoBackupInstanceActor::fp_MarkBackupFinished()
	{
		auto pCanDestroy = mp_pCanDestroy;

		DMibLogWithCategory
			(
				MongoManager/Backup
				, Debug
				, "Marking initial backup as finished"
			)
		;
		
		TCContinuation<TCSharedPointer<CFile> > Continuation;
		g_Dispatch(mp_FileWriteActor) >[FinishedPath = mp_BackupDirectory + "/InitialFinished"]
			{
				CFile::fs_Touch(FinishedPath);
			}
			> [Continuation, pCanDestroy](TCAsyncResult<void> &&_Result) mutable
			{
				if (!_Result)
					DLogWithCategory(MongoManager/Backup, Error, "Failed to mark backup as finished: {}", _Result.f_GetExceptionStr());
			}
		;
	}
	
	TCContinuation<void> CMongoBackupInstanceActor::f_StartBackup(CActorSubscription &&_ManifestFinished, CStr const &_BackupRoot)
	{
		if (!mp_pCanDestroy)
			return DErrorInstance("Destroyed");
		
		mp_MongoClient = fg_ConstructActor<CMongoClientActor>(fg_Construct("Mongo client connection"), mp_MongoConnectionSettings, "local");
		
		TCSharedPointer<CActorSubscription> pManifestFinished = fg_Construct(fg_Move(_ManifestFinished));

		TCContinuation<void> Continuation;
		fp_OpenBackupFiles() > Continuation / [=](TCSharedPointer<CFile> &&_pOplogFile) mutable
			{
				if (!mp_pCanDestroy)
					return Continuation.f_SetException(DErrorInstance("Destroyed"));
				
				fp_TailOplog(_pOplogFile) > Continuation / [=]() mutable
					{
						if (!mp_pCanDestroy)
							return Continuation.f_SetException(DErrorInstance("Destroyed"));

						CStr OplogPath = mp_BackupDirectory + "/DynamicOplog.bson";
						CStr RelativeOplogPath = CFile::fs_MakePathRelative(OplogPath, _BackupRoot);
						
						CDirectoryManifestConfig ManifestConfig;
						ManifestConfig.m_IncludeWildcards.f_Clear();
						ManifestConfig.m_IncludeWildcards[RelativeOplogPath] = "Dump";
						
						DCallActor(mp_BackupInterface, CDistributedAppInterfaceBackup::f_AppendManifest, ManifestConfig) > [pManifestFinished](TCAsyncResult<void> &&_Result)
							{
								if (!_Result)
									DLogWithCategory(MongoManager/Backup, Error, "Failed to append manifest: {}", _Result.f_GetExceptionStr());
							}
						;
						Continuation.f_SetResult();
						
						fp_DumpDatabase() > [=](TCAsyncResult<void> &&_Result) mutable
							{
								if (_Result)
								{
									DLogWithCategory(MongoManager/Backup, Info, "Finished dumping");
									
									CStr DumpPath = mp_BackupDirectory + "/Dump/^*";
									CStr RelativeDumpPath = CFile::fs_MakePathRelative(DumpPath, _BackupRoot);
									
									CDirectoryManifestConfig ManifestConfig;
									ManifestConfig.m_IncludeWildcards.f_Clear();
									ManifestConfig.m_IncludeWildcards[RelativeDumpPath] = "Dump";

									DCallActor(mp_BackupInterface, CDistributedAppInterfaceBackup::f_AppendManifest, ManifestConfig) > [pManifestFinished](TCAsyncResult<void> &&_Result)
										{
											if (!_Result)
												DLogWithCategory(MongoManager/Backup, Error, "Failed to append manifest: {}", _Result.f_GetExceptionStr());

											DMibLogWithCategory
												(
													MongoManager/Backup
													, Debug
													, "Letting go of manifest finished subscription"
												)
											;
										}
									;
									
									DCallActor
										(
											mp_BackupInterface
											, CDistributedAppInterfaceBackup::f_SubscribeInitialFinished
											, g_ActorFunctor > [this, AllowDestroy = g_AllowWrongThreadDestroy]() -> TCContinuation<void>
											{
												mp_bInitialBackupUploaded = true;
												fp_MarkBackupFinished();
												return fg_Explicit();
											}
										)
										> [this, pManifestFinished](TCAsyncResult<TCActorSubscriptionWithID<>> &&_Subscription)
										{
											if (!_Subscription)
											{
												DLogWithCategory(MongoManager/Backup, Error, "Failed to subscribe to initial backup finished: (}", _Subscription.f_GetExceptionStr());
												return;
											}
											
											mp_InitialBackupFinishedSubscription = fg_Move(*_Subscription);
										}
									;
								}
								else
								{
									DLogWithCategory(MongoManager/Backup, Error, "Database dump failed: {}", _Result.f_GetExceptionStr());
								}
							}
						;
					}
				;
			}
		;
		
		return Continuation;
	}
	
	void CMongoBackupInstanceActor::f_MongoStopped()
	{
		mp_bMongoStopped = true;
	}

	TCContinuation<void> CMongoBackupInstanceActor::fp_DeleteBackup()
	{
		if (!mp_bInitialBackupUploaded && mp_bInitialDumpFinished)
		{
			DLogWithCategory(MongoManager/Backup, Info, "Saving backup which has not yet finished transferring to remote server: {}", mp_BackupDirectory);
			return TCContinuation<void>::fs_Finished(); // If we haven't uploaded this backup yet, keep it around and let the main backup actor clean it out after a week
		}
		
		if (!mp_bBackupStopped)
		{
			DLogWithCategory(MongoManager/Backup, Info, "Saving backup that has not yet been stopped: {}", mp_BackupDirectory);
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
					, bInitialDumpFinished = mp_bInitialDumpFinished
				]
				{
					return TCContinuation<void>::fs_RunProtected<CExceptionFile>()
						> [&]()
						{
							CFile::fs_DeleteDirectoryRecursive(BackupDirectory, true);
							if (bInitialDumpFinished)
								DLogWithCategory(MongoManager/Backup, Info, "Deleted backup which has fully transferred to remote server: {}", BackupDirectory);
							else
								DLogWithCategory(MongoManager/Backup, Info, "Deleted backup which has not yet finished the full dump: {}", BackupDirectory);
						}
					;
				}
			) 
			> [Result](TCAsyncResult<void> &&_Result)
			{
				if (!_Result)
					DLogWithCategory(MongoManager/Backup, Error, "Failed to delete the backup: {}", _Result.f_GetExceptionStr());
				Result.f_SetResult();
			}
		;
		
		return Result;
	}
	
	TCContinuation<void> CMongoBackupInstanceActor::fp_Destroy()
	{
		auto pCanDestroy = fg_Move(mp_pCanDestroy);
		mp_MongoTailSubscription.f_Clear();
		
		if (!mp_bInitialBackupUploaded)
			DLogWithCategory(MongoManager/Backup, Warning, "Aborting backup before the initial full backup has finished uploading");
		
		TCActorResultVector<void> AllDestroyed;
		
		if (mp_DumpProcessLaunch)
			mp_DumpProcessLaunch->f_Destroy() > AllDestroyed.f_AddResult();

		if (mp_MongoClient)
			mp_MongoClient->f_Destroy() > AllDestroyed.f_AddResult();
		
		AllDestroyed.f_GetResults()
			> [this, pCanDestroy](TCAsyncResult<TCVector<TCAsyncResult<void>>> &&_Results)
			{
				fg_ThisActor(this)(&CMongoBackupInstanceActor::fp_DeleteBackup)
					> [this, pCanDestroy](TCAsyncResult<void> &&_Result)
					{
						mp_FileWriteActor->f_Destroy() > pCanDestroy->f_Track();
					}
				;
			}
		;
			
		return pCanDestroy->m_Continuation;
	}
}
