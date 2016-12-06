
#include "Malterlib_Mongo_App_MongoManager_Server.h"
#include "Malterlib_Mongo_App_MongoManager_BackupInstance.h"
 
namespace NMib::NMongo::NMongoManager
{
	void CMongoBackupInstanceActor::fp_OnOplogTailing()
	{
		fg_ThisActor(this)(&CMongoBackupInstanceActor::fp_DumpDatabase)
			+ fg_ThisActor(this)(&CMongoBackupInstanceActor::fp_DumpDistribution)
			> [this](TCAsyncResult<void> &&_DatabaseResult, TCAsyncResult<void> &&_DistributionResult)
			{
				if (_DatabaseResult && _DistributionResult)
				{
					DLogWithCategory(Backup, Info, "Finished dumping, compressing");
					
					fg_ThisActor(this)(&CMongoBackupInstanceActor::fp_CompressDump)
						>[this](TCAsyncResult<void> &&_Result)
						{
							if (!_Result)
							{
								CStr ExceptionStr = _Result.f_GetExceptionStr();
								mp_OnEventCallback(CBackupCallbackEvent_Error(fg_Format("Failed to compress dump: {}", ExceptionStr)));
								DLogWithCategory(Backup, Error, "Failed to compress dump, aborting backup: {}", ExceptionStr);
								return;
							}
							
							fp_UploadDumpToServers();
						}
					;
				}
				else
				{
					CStr ErrorDatabase = "Successful";
					CStr ErrorDistribution = "Successful";
					if (!_DatabaseResult)
						ErrorDatabase = _DatabaseResult.f_GetExceptionStr();
					if (!_DistributionResult)
						ErrorDistribution = _DistributionResult.f_GetExceptionStr();
					DLogWithCategory(Backup, Error, "Failed to dump, aborting backup: Database: {} Distribution: {}", ErrorDatabase, ErrorDistribution);
					
					mp_OnEventCallback(CBackupCallbackEvent_Error(fg_Format("Database dump failed: Database: {} Distribution: {}", ErrorDatabase, ErrorDistribution)));
				}
			}
		;
	}
	
	TCContinuation<void> CMongoBackupInstanceActor::fp_DumpDistribution()
	{
		TCSharedPointer<CCanDestroyTracker> pCanDestroy = mp_pCanDestroy;
		
		if (!pCanDestroy)
			return TCContinuation<void>::fs_Finished();

		TCContinuation<void> Continuation;

		DLogWithCategory(Backup, Info, "Dumping distribution files");
		
		CStr SourceDirectory = CFile::fs_GetProgramDirectory();
		CStr OutputDirectory = mp_BackupDirectory + "/Package";

		mp_FileWriteActor
			(
				&CActor::f_DispatchWithReturn<TCContinuation<void>>
				, [pCanDestroy, SourceDirectory, OutputDirectory]
				{
					return TCContinuation<void>::fs_RunProtected<CExceptionFile>()
						> [&]
						{
							CFile::fs_CopyFiles(CFile::fs_GetProgramPath(), OutputDirectory, false);
							CFile::fs_CopyFiles(SourceDirectory + "/*.dylib", OutputDirectory, false);
							CFile::fs_CopyFiles(SourceDirectory + "/*.so", OutputDirectory, false);
							CFile::fs_CopyFiles(SourceDirectory + "/FavroMongoManagerState.json", OutputDirectory, false);
							CFile::fs_CopyFiles(SourceDirectory + "/FavroMongoManagerConfig.json", OutputDirectory, false);
							CFile::fs_CopyFiles(SourceDirectory + "/Source/MongoManager_Restore.sh", OutputDirectory, false);
							CFile::fs_CopyFiles(SourceDirectory + "/CommandLineTrustDatabase.MongoManager/*", OutputDirectory + "/CommandLineTrustDatabase.FavroManager", true);
							CFile::fs_CopyFiles(SourceDirectory + "/TrustDatabase.MongoManager/*", OutputDirectory + "/TrustDatabase.MongoManager", true);
							
							DLogWithCategory(Backup, Info, "Done dumping distribution files");
						}
					;
				}
			) > [Continuation](TCAsyncResult<void> &&_Error)
			{
				Continuation.f_SetResult(fg_Move(_Error));
			}
		;

		return Continuation;
	}
	
	TCContinuation<void> CMongoBackupInstanceActor::fp_CompressDump()
	{
		TCSharedPointer<CCanDestroyTracker> pCanDestroy = mp_pCanDestroy;
		
		if (!pCanDestroy)
			return TCContinuation<void>::fs_Finished();
		
		TCContinuation<void> Continuation;
		
		mp_CompressProcessLaunch = fg_ConstructActor<CProcessLaunchActor>();
		DLogWithCategory(Backup, Info, "Compressing dump");
		
		CStr DirectoryToCompress = mp_BackupDirectory + "/Package";
		CProcessLaunchActor::CSimpleLaunch Launch
			{
				"tar"
				, fg_CreateVector<CStr>
				(
					"czf"
					, mp_BackupPath[EBackupState_Dump]
					, "."
				)
				, DirectoryToCompress
			}
		;
		
		Launch.m_LogName = "CompressDump";
		Launch.m_ToLog = CProcessLaunchActor::ELogFlag_All;
		Launch.m_Params.m_bCreateNewProcessGroup = true;
		CMongoManagerActor::fs_SetupEnvironment(Launch.m_Params);
		
		mp_CompressProcessLaunch(&CProcessLaunchActor::f_LaunchSimple, Launch)
			> Continuation / [this, Continuation, pCanDestroy](CProcessLaunchActor::CSimpleLaunchResult &&_Result)
			{
				if (_Result.m_ExitCode != 0)
				{
					Continuation.f_SetException(DMibErrorInstance("Backup tar failed"));
					return;
				}
				
				fg_Dispatch
					(
						mp_FileWriteActor
						, [BackupPath = mp_BackupPath[EBackupState_Dump]]() -> uint64
						{
							return CFile::fs_GetFileSize(BackupPath);
						}
					) 
					> [this, Continuation](TCAsyncResult<uint64> &&_DumpSize)
					{
						if (!_DumpSize)
						{
							Continuation.f_SetException(fg_Move(_DumpSize));
							return;
						}
						
						mp_FileSizes[EBackupState_Dump] = *_DumpSize;
						DLogWithCategory(Backup, Info, "Backup tar compression finished");
						Continuation.f_SetResult();
					}
				;
			}
		;
		
		return Continuation;
	}

	TCContinuation<void> CMongoBackupInstanceActor::fp_DumpDatabase()
	{
		TCSharedPointer<CCanDestroyTracker> pCanDestroy = mp_pCanDestroy;
		
		if (!pCanDestroy)
			return TCContinuation<void>::fs_Finished();
		
		TCContinuation<void> Continuation;
		
		mp_DumpProcessLaunch = fg_ConstructActor<CProcessLaunchActor>();
		DLogWithCategory(Backup, Info, "Launching mongodump");
		
		TCVector<CStr> Params = mp_MongoConnectionSettings.f_GetToolParams();
		
		Params << fg_CreateVector<CStr>
			(
				"--quiet"
				, "--oplog"
				, fg_Format("--out={}", mp_BackupDirectory + "/Package/MongoDump")
			)
		;
		
		CProcessLaunchActor::CSimpleLaunch Launch
			{
				mp_MongoExecutable
				, Params
				, CFile::fs_GetPath(mp_MongoExecutable)
			}
		;

		Launch.m_LogName = "DumpDatabase";
		Launch.m_ToLog = CProcessLaunchActor::ELogFlag_All;
		Launch.m_Params.m_bCreateNewProcessGroup = true;
		
		CMongoManagerActor::fs_SetupEnvironment(Launch.m_Params);
		
		mp_DumpProcessLaunch(&CProcessLaunchActor::f_LaunchSimple, Launch)
			> Continuation / [this, Continuation, pCanDestroy](CProcessLaunchActor::CSimpleLaunchResult &&_Result)
			{
				if (_Result.m_ExitCode == 0)
				{
					DLogWithCategory(Backup, Info, "Full database dump finished");
					Continuation.f_SetResult();
				}
				else
					Continuation.f_SetException(DMibErrorInstance("Backup dump failed"));
			}
		;
		
		return Continuation;
	}
}
