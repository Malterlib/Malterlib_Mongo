
#include "Malterlib_Mongo_App_MongoManager_Server.h"
#include "Malterlib_Mongo_App_MongoManager_BackupInstance.h"

#include <Mib/Concurrency/Actor/Timer>
#include <Mib/Cloud/BackupManager>

namespace NMib::NMongo::NMongoManager
{
	struct CMongoBackupManagerActor : public CBackupManagerActorInterface
	{
	private:
		struct CBackupState
		{
			TCActor<CMongoBackupInstanceActor> m_Backup;
			CActorSubscription m_Subscription;
			TCFunction<void ()> m_fOnSuccess;
			TCFunction<void ()> m_fOnUploaded;
		};
		
	public:		
		
		CMongoBackupManagerActor
			(
				CMongoConnectionSettings const &_MongoConnectionSettings
				, CStr const &_MongoExecutable
				, uint32 _BackupInterval
				, TCActor<CDistributedActorTrustManager> const &_TrustManager
			)
			: mp_MongoConnectionSettings(_MongoConnectionSettings)
			, mp_MongoExecutable(_MongoExecutable)
			, mp_BackupInterval(_BackupInterval)
			, mp_pCanDestroy(fg_Construct())
			, mp_TrustManager(_TrustManager)
		{
		}
		
		TCContinuation<void> f_StartBackup() override
		{
			TCContinuation<void> Result;
			DLogWithCategory(BackupManager, Info, "Starting initial full backup");
			fp_ScheduleOldCleanup();
			
			fp_ScheduleCurrentBackup
				(
					[this, Result]() // _fOnSuccess
					{
						Result.f_SetResult();
						
						// Start regular backup schedule
						fg_TimerActor()
							(
								&CTimerActor::f_RegisterTimer
								, 60.0
								, fg_ThisActor(this)
								, [this]
								{
									fp_RunBackupSchedule();
								}
							)
							> [this](TCAsyncResult<CActorSubscription> &&_Result)
							{
								mp_TimerCallback = fg_Move(*_Result);
							}
						;
					}
					, nullptr // _fOnUploaded
				)
			;
			return Result;
		}
		
		TCContinuation<void> f_Destroy() override
		{
			TCSharedPointer<CCanDestroyTracker> pCanDestroy = fg_Move(mp_pCanDestroy);
			mp_TimerCallback.f_Clear();

			mp_FileWriteActor->f_Destroy([pCanDestroy](TCAsyncResult<void> &&){});
			
			fp_StopBackup(mp_OldBackup, "Destroy old", pCanDestroy);
			fp_StopBackup(mp_CurrentBackup, "Destroy current", pCanDestroy);
			
			return pCanDestroy->m_Continuation;
		}
		
	private:
		
		TCContinuation<void> fp_CleanupOldBackups()
		{
			if (!mp_FileWriteActor)
				mp_FileWriteActor = fg_ConstructActor<CSeparateThreadActor>(fg_Construct("Global file write actor"));
			
			TCContinuation<void> Result;

			auto pCanDestroy = mp_pCanDestroy;
			DLogWithCategory(BackupManager, Info, "Scheduling remove of old backups");
			
			mp_FileWriteActor
				(
					&CActor::f_DispatchWithReturn<TCContinuation<void>>
					,
					[
						pCanDestroy
						, this
					]
					{
						return TCContinuation<void>::fs_RunProtected<CExceptionFile>()
							> [&]()
							{
								CFile::CFindFilesOptions Options{fg_Format("{}/Backup/*", CFile::fs_GetProgramDirectory()), false};
								Options.m_AttribMask = EFileAttrib_Directory;
								auto FoundFiles = CFile::fs_FindFiles(Options);
								CTime RemoveOlderThan = CTime::fs_NowUTC() - CTimeSpanConvert::fs_CreateWeekSpan(1);
								DLogWithCategory(BackupManager, Info, "Found {} old backups", FoundFiles.f_GetLen());
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
											DLogWithCategory(BackupManager, Info, "Removed backup: {}", File.m_Path);
										}
										catch (CExceptionFile const &_Exception)
										{
											DLogWithCategory(BackupManager, Error, "Failed to remove backup: {}:{\n}{}", File.m_Path, _Exception);
										}
									}
								}
							}
						;
					}
				) 
				> [this, Result](TCAsyncResult<void> &&_Result)
				{
					if (!_Result)
						DLogWithCategory(BackupManager, Error, "Failed to clean up old backups: {}", _Result.f_GetExceptionStr());
					Result.f_SetResult();
				}
			;
			
			return Result;
		}
		
		void fp_StopBackup(CBackupState &_Backup, CStr const &_Message, TCSharedPointer<CCanDestroyTracker> const &_pCanDestroy)
		{
			if (!_Backup.m_Backup)
				return;
			
			DLogWithCategory(BackupManager, Info, "Stopping backup ({})", _Message);
			_Backup.m_Backup->f_Destroy
				(
					[_pCanDestroy, _Message](TCAsyncResult<void> &&)
					{
						DLogWithCategory(BackupManager, Info, "Backup stopped ({})", _Message);
					}
				)
			;
			_Backup = CBackupState();
		}
		
		void fp_ScheduleOldCleanup()
		{
			fg_ThisActor(this)(&CMongoBackupManagerActor::fp_CleanupOldBackups) > fg_DiscardResult(); // Clean up any old backups
		}

		void fp_OnEventSubscription(TCWeakActor<CMongoBackupInstanceActor> const &_Actor, CActorSubscription &&_Subscription)
		{
			if (_Actor == mp_CurrentBackup.m_Backup)
				mp_CurrentBackup.m_Subscription = fg_Move(_Subscription);
			else if (_Actor == mp_OldBackup.m_Backup)
				mp_OldBackup.m_Subscription = fg_Move(_Subscription);
		}
		
		void fp_RescheduleCurrentBackup()
		{
			fg_TimerActor()
				(
					&CTimerActor::f_OneshotTimer
					, 60.0
					, fg_ThisActor(this)
					, [this, Actor = mp_CurrentBackup.m_Backup.f_Weak()]() mutable
					{
						if (Actor != mp_CurrentBackup.m_Backup)
							return;
						DLogWithCategory(BackupManager, Info, "Retrying backup");
						fp_ScheduleCurrentBackup(fg_Move(mp_CurrentBackup.m_fOnSuccess), fg_Move(mp_CurrentBackup.m_fOnUploaded));
					}
				)
				> fg_DiscardResult()
			;
		}
		
		void fp_OnEvent(TCWeakActor<CMongoBackupInstanceActor> const &_Actor, CBackupCallbackEvent const &_Event)
		{
			if (_Actor != mp_CurrentBackup.m_Backup)
				return;

			switch (_Event.f_GetTypeID())
			{
			case EBackupCallback_Error:
				{
					DLogWithCategory(BackupManager, Error, "Backup failed in dumping stage. Retrying in 60 s. The error was: {}", _Event.f_Get<EBackupCallback_Error>().m_Error);
					
					fp_RescheduleCurrentBackup();
				}
				break;
			case EBackupCallback_DumpUploadFinished:
				{
					if (mp_CurrentBackup.m_fOnUploaded)
					{
						mp_CurrentBackup.m_fOnUploaded();
						mp_CurrentBackup.m_fOnUploaded.f_Clear();
					}
				}
				break;
			default:
				{
					DNeverGetHere;
				}
				break;
			}
		}
		
		void fp_ScheduleCurrentBackup(TCFunction<void ()> _fOnSuccess, TCFunction<void ()> _fOnUploaded)
		{
			if (!mp_pCanDestroy)
				return;
			if (mp_CurrentBackup.m_Backup)
				fp_StopBackup(mp_CurrentBackup, "Reschedule current backup", mp_pCanDestroy);
			
			mp_LastBackupStart = CTime::fs_NowUTC();
			mp_CurrentBackup.m_fOnSuccess = fg_Move(_fOnSuccess);
			mp_CurrentBackup.m_fOnUploaded = fg_Move(_fOnUploaded);
			mp_CurrentBackup.m_Backup = fg_ConstructActor<CMongoBackupInstanceActor>(mp_MongoConnectionSettings, mp_MongoExecutable, mp_TrustManager);
			mp_CurrentBackup.m_Backup
				(
					&CMongoBackupInstanceActor::f_StartBackup
					, fg_ThisActor(this)
					, [this, CurrentBackupWeak = mp_CurrentBackup.m_Backup.f_Weak()](CBackupCallbackEvent const &_Event)
					{
						fp_OnEvent(CurrentBackupWeak, _Event);
					}
				)
				> [this, CurrentBackupWeak = mp_CurrentBackup.m_Backup.f_Weak()](TCAsyncResult<CActorSubscription> &&_Result) mutable
				{
					if (_Result)
					{
						fp_OnEventSubscription(CurrentBackupWeak, fg_Move(*_Result));
						if (CurrentBackupWeak != mp_CurrentBackup.m_Backup)
							return;
						if (mp_CurrentBackup.m_fOnSuccess)
						{
							mp_CurrentBackup.m_fOnSuccess();
							mp_CurrentBackup.m_fOnSuccess.f_Clear();
						}
					}
					else
					{
						if (CurrentBackupWeak != mp_CurrentBackup.m_Backup)
						{
							DLogWithCategory(BackupManager, Error, "Outdated backup failed: {}", _Result.f_GetExceptionStr());
							return;
						}
						DLogWithCategory(BackupManager, Error, "Backup failed. Retrying in 60 s. The error was: {}", _Result.f_GetExceptionStr());
						
						fp_RescheduleCurrentBackup();
					}
				}
			;
		}
		
		void fp_RunBackupSchedule()
		{
			if (!mp_pCanDestroy)
				return; // Destroyed
			
			CTime Now = CTime::fs_NowUTC();
			CTime NextBackup = mp_LastBackupStart;
			CTimeConvert::fs_RoundTimeToMinuteDown(NextBackup);
			NextBackup += CTimeSpanConvert::fs_CreateMinuteSpan(mp_BackupInterval);
			
			if (Now >= NextBackup)
			{
				fp_StopBackup(mp_OldBackup, "Abort old", mp_pCanDestroy);
				DLogWithCategory(BackupManager, Info, "Starting new full backup");
				mp_OldBackup = fg_Move(mp_CurrentBackup);

				fp_ScheduleCurrentBackup
					(
						[this] // _fOnSuccess
						{
							DLogWithCategory(BackupManager, Info, "Oplog for new backup is tailing");
						}
						, [this] // _fOnUploaded
						{
							DLogWithCategory(BackupManager, Info, "Initial dump for new backup is now uploaded");
							if (mp_pCanDestroy)
							{
								// The new backup is now uploaded, so we have overlap with old backup which can now be safely stopped.
								fp_StopBackup(mp_OldBackup, "Old", mp_pCanDestroy);
							}
						}
					)
				;
				fp_ScheduleOldCleanup();
			}
		}
		
	private:
		uint32 mp_BackupInterval = 1440;
		
		CMongoConnectionSettings mp_MongoConnectionSettings;
		CStr mp_MongoExecutable;
		CTime mp_LastBackupStart;
		
		TCActor<CSeparateThreadActor> mp_FileWriteActor;
		CBackupState mp_OldBackup;
		CBackupState mp_CurrentBackup;
		CActorSubscription mp_TimerCallback;
		TCSharedPointer<CCanDestroyTracker> mp_pCanDestroy;
		TCActor<CDistributedActorTrustManager> mp_TrustManager;
	};
	
	void CMongoManagerActor::fp_StartMongoBackup()
	{
		if (mp_pMongoBackupManagerActor)
			return;
		
		if (auto pValue = mp_AppState.m_ConfigDatabase.m_Data.f_GetMember("BackupEnable", EJSONType_Boolean))
		{
			if (!pValue->f_Boolean())
				return;
		}
		else
			return;
		
		uint32 BackupInterval = 1440;
		if (auto pValue = mp_AppState.m_ConfigDatabase.m_Data.f_GetMember("BackupInterval", EJSONType_Integer))
			BackupInterval = pValue->f_Integer();
		
		mp_pMongoBackupManagerActor = fg_ConstructActor<CMongoBackupManagerActor>
			(
				mp_MongoConnectionSettings
				, fp_GetMongoExecutable("mongodump")
				, BackupInterval
				, mp_AppState.m_TrustManager
			)
		;
		mp_pMongoBackupManagerActor(&CBackupManagerActorInterface::f_StartBackup)
			> [this](TCAsyncResult<void> &&_Result)
			{
				if (_Result)
				{
					DLogWithCategory(BackupManager, Info, "Oplog is tailing");
				}
			}
		;
	}
}
