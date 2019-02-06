
#include "Malterlib_Mongo_App_MongoManager_Server.h"
#include "Malterlib_Mongo_App_MongoManager_BackupInstance.h"
 
namespace NMib::NMongo::NMongoManager
{
	TCFuture<void> CMongoBackupInstanceActor::fp_DumpDatabase()
	{
		TCSharedPointer<CCanDestroyTracker> pCanDestroy = mp_pCanDestroy;
		
		if (!pCanDestroy)
			return fg_Explicit();
		
		TCPromise<void> Promise;
		
		mp_DumpProcessLaunch = fg_ConstructActor<CProcessLaunchActor>();
		DLogWithCategory(Backup, Info, "Launching mongodump");
		
		TCVector<CStr> Params = mp_MongoConnectionSettings.f_GetToolParams();
		
		Params << fg_CreateVector<CStr>
			(
				"--oplog"
				, fg_Format("--out={}", mp_BackupDirectory + "/Dump")
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
		Launch.m_ToLog = CProcessLaunchActor::ELogFlag_Error | CProcessLaunchActor::ELogFlag_Info;
		Launch.m_Params.m_bCreateNewProcessGroup = true;
		
		CMongoManagerActor::fs_SetupEnvironment(Launch.m_Params);
		
		mp_DumpProcessLaunch(&CProcessLaunchActor::f_LaunchSimple, Launch)
			> Promise / [this, Promise, pCanDestroy](CProcessLaunchActor::CSimpleLaunchResult &&_Result)
			{
				if (_Result.m_ExitCode == 0)
				{
					DLogWithCategory(Backup, Info, "Full database dump finished");
					mp_bInitialDumpFinished = true;
					Promise.f_SetResult();
				}
				else
					Promise.f_SetException(DMibErrorInstance(fg_Format("Backup dump failed: {}", _Result.f_GetCombinedOut())));
			}
		;
		
		return Promise.f_MoveFuture();
	}
}
