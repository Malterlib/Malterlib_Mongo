
#include <Mib/Concurrency/LogError>

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

		mp_BackupInterface.f_CallActor(&CDistributedAppInterfaceBackup::f_SubscribeBackupStopped)
			(
				g_ActorFunctor / [this, AllowDestroy = g_AllowWrongThreadDestroy]() -> TCFuture<void>
				{
					DMibLogWithCategory
						(
							MongoManager/Backup
							, Debug
							, "Received backup stopped notification"
						)
					;

					mp_bBackupStopped = true;
					co_return {};
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

	TCFuture<TCSharedPointer<CFile>> CMongoBackupInstanceActor::fp_OpenBackupFiles()
	{
		if (f_IsDestroyed())
			co_return DErrorInstance("Destroyed");

		auto pCanDestroy = mp_pCanDestroy;

		auto SequenceSubscription = co_await mp_WriteSequencer.f_Sequence();

		auto BlockingActorCheckout = fg_BlockingActor();
		auto pResult = co_await
			(
				g_Dispatch(BlockingActorCheckout) / [BackupDirectory = mp_BackupDirectory, OplogPath = mp_BackupDirectory + "/DynamicOplog.bson"]() -> TCFuture<TCSharedPointer<CFile>>
				{
					auto CaptureScope = co_await g_CaptureExceptions.f_Specific<CExceptionFile>();

					CFile::fs_CreateDirectory(BackupDirectory + "/Dump");

					CStr LatestSymlink = fg_Format("{}/Backup/Latest", CFile::fs_GetProgramDirectory());
					if (CFile::fs_FileExists(LatestSymlink))
						CFile::fs_DeleteFile(LatestSymlink);
					CFile::fs_CreateSymbolicLink(CFile::fs_GetFile(BackupDirectory), LatestSymlink, EFileAttrib_Directory, ESymbolicLinkFlag_Relative);

					TCSharedPointer<CFile> pOplogFile = fg_Construct();
					pOplogFile->f_Open(OplogPath, EFileOpen_Write | EFileOpen_NoLocalCache | EFileOpen_ShareRead);

					co_return fg_Move(pOplogFile);
				}
				% "Failed to open backup files"
			)
		;

		co_return fg_Move(pResult);
	}

	TCFuture<void> CMongoBackupInstanceActor::fp_MarkBackupFinished()
	{
		auto pCanDestroy = mp_pCanDestroy;

		DMibLogWithCategory
			(
				MongoManager/Backup
				, Debug
				, "Marking initial backup as finished"
			)
		;

		auto SequenceSubscription = co_await mp_WriteSequencer.f_Sequence();

		auto BlockingActorCheckout = fg_BlockingActor();
		co_await
			(
				g_Dispatch(BlockingActorCheckout) / [FinishedPath = mp_BackupDirectory + "/InitialFinished"]
				{
					CFile::fs_Touch(FinishedPath);
				}
			)
			.f_Wrap() > fg_LogError("MongoManager/Backup", "Failed to mark backup as finished");
		;

		co_return {};
	}

	TCFuture<void> CMongoBackupInstanceActor::f_StartBackup(CActorSubscription _ManifestFinished, CStr _BackupRoot)
	{
		if (f_IsDestroyed())
			co_return DErrorInstance("Destroyed");

		mp_MongoClient = fg_ConstructActor<CMongoClientActor>(fg_Construct("Mongo client connection"), mp_MongoConnectionSettings, "local");

		TCSharedPointer<CActorSubscription> pManifestFinished = fg_Construct(fg_Move(_ManifestFinished));

		auto pOplogFile = co_await fp_OpenBackupFiles();
		
		if (f_IsDestroyed())
			co_return DErrorInstance("Destroyed");

		co_await fp_TailOplog(pOplogFile);

		if (f_IsDestroyed())
			co_return DErrorInstance("Destroyed");

		CStr OplogPath = mp_BackupDirectory + "/DynamicOplog.bson";
		CStr RelativeOplogPath = CFile::fs_MakePathRelative(OplogPath, _BackupRoot);

		CDistributedAppInterfaceBackup::CManifestConfig ManifestConfig;
		ManifestConfig.m_IncludeWildcards.f_Clear();
		ManifestConfig.m_IncludeWildcards[RelativeOplogPath] = "Dump";

		mp_BackupInterface.f_CallActor(&CDistributedAppInterfaceBackup::f_AppendManifest)(ManifestConfig) > [pManifestFinished](TCAsyncResult<void> &&_Result)
			{
				if (!_Result)
					DLogWithCategory(MongoManager/Backup, Error, "Failed to append manifest: {}", _Result.f_GetExceptionStr());
			}
		;

		fp_DumpDatabase() > [=, this](TCAsyncResult<void> &&_Result) mutable
			{
				if (_Result)
				{
					DLogWithCategory(MongoManager/Backup, Info, "Finished dumping");

					CStr DumpPath = mp_BackupDirectory + "/Dump/^*";
					CStr RelativeDumpPath = CFile::fs_MakePathRelative(DumpPath, _BackupRoot);

					CDistributedAppInterfaceBackup::CManifestConfig ManifestConfig;
					ManifestConfig.m_IncludeWildcards.f_Clear();
					ManifestConfig.m_IncludeWildcards[RelativeDumpPath] = "Dump";

					mp_BackupInterface.f_CallActor(&CDistributedAppInterfaceBackup::f_AppendManifest)(ManifestConfig) > [pManifestFinished](TCAsyncResult<void> &&_Result)
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

					mp_BackupInterface.f_CallActor(&CDistributedAppInterfaceBackup::f_SubscribeInitialFinished)
						(
							g_ActorFunctor / [this, AllowDestroy = g_AllowWrongThreadDestroy]() -> TCFuture<void>
							{
								mp_bInitialBackupUploaded = true;
								co_await fp_MarkBackupFinished();
								co_return {};
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

		co_return {};
	}

	void CMongoBackupInstanceActor::f_MongoStopped()
	{
		mp_bMongoStopped = true;
	}

	TCFuture<void> CMongoBackupInstanceActor::fp_DeleteBackup()
	{
		if (!mp_bInitialBackupUploaded && mp_bInitialDumpFinished)
		{
			DLogWithCategory(MongoManager/Backup, Info, "Saving backup which has not yet finished transferring to remote server: {}", mp_BackupDirectory);
			co_return {}; // If we haven't uploaded this backup yet, keep it around and let the main backup actor clean it out after a week
		}

		if (!mp_bBackupStopped)
		{
			DLogWithCategory(MongoManager/Backup, Info, "Saving backup that has not yet been stopped: {}", mp_BackupDirectory);
			co_return {}; // If we haven't uploaded this backup yet, keep it around and let the main backup actor clean it out after a week
		}

		auto pCanDestroy = mp_pCanDestroy;

		auto SequenceSubscription = co_await mp_WriteSequencer.f_Sequence();

		auto BlockingActorCheckout = fg_BlockingActor();
		co_await
			(
				g_Dispatch(BlockingActorCheckout) / [BackupDirectory = mp_BackupDirectory, bInitialDumpFinished = mp_bInitialDumpFinished]() -> TCFuture<void>
				{
					auto CaptureScope = co_await g_CaptureExceptions.f_Specific<CExceptionFile>();

					CFile::fs_DeleteDirectoryRecursive(BackupDirectory, true);
					if (bInitialDumpFinished)
						DLogWithCategory(MongoManager/Backup, Info, "Deleted backup which has fully transferred to remote server: {}", BackupDirectory);
					else
						DLogWithCategory(MongoManager/Backup, Info, "Deleted backup which has not yet finished the full dump: {}", BackupDirectory);

					co_return {};
				}
				% "Failed to delete the backup"
			)
		;

		co_return {};
	}

	TCFuture<void> CMongoBackupInstanceActor::fp_Destroy()
	{
		CLogError LogError("MongoManager/Backup");

		{
			auto CanDestroyFuture = fg_Exchange(mp_pCanDestroy, nullptr)->f_Future();
			co_await fg_Move(CanDestroyFuture).f_Wrap() > LogError.f_Warning("Failed to destroy can destroy on instance");
		}

		if (mp_MongoTailSubscription)
		 	co_await fg_Exchange(mp_MongoTailSubscription, nullptr)->f_Destroy().f_Wrap() > LogError.f_Warning("Failed to destroy mongo tail subscription");

		if (!mp_bInitialBackupUploaded)
			DLogWithCategory(MongoManager/Backup, Warning, "Aborting backup before the initial full backup has finished uploading");

		{
			TCFutureVector<void> AllDestroyed;

			if (mp_DumpProcessLaunch)
				fg_Move(mp_DumpProcessLaunch).f_Destroy() > AllDestroyed;

			if (mp_MongoClient)
				fg_Move(mp_MongoClient).f_Destroy() > AllDestroyed;

			co_await fg_AllDone(AllDestroyed).f_Wrap() > LogError.f_Warning("Failed to destroy mongo dump launch or client");
		}

		co_await fp_DeleteBackup().f_Wrap() > LogError.f_Warning("Failed to delete backup");

		co_await fg_Move(mp_WriteSequencer).f_Destroy().f_Wrap() > LogError.f_Warning("Failed to destroy write sequencer");
		co_await fg_Move(mp_OplogWriteSequencer).f_Destroy().f_Wrap() > LogError.f_Warning("Failed to destroy oplog sequencer");

		co_return {};
	}
}
