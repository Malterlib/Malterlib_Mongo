
#include "Malterlib_Mongo_App_MongoManager_Server.h"
#include "Malterlib_Mongo_App_MongoManager_BackupInstance.h"
 
namespace NMib::NMongo::NMongoManager
{
	TCFuture<void> CMongoBackupInstanceActor::fp_DumpDatabase()
	{
		TCSharedPointer<CCanDestroyTracker> pCanDestroy = mp_pCanDestroy;
		
		if (!pCanDestroy)
			co_return {};
		
		mp_DumpProcessLaunch = fg_ConstructActor<CProcessLaunchActor>();
		DLogWithCategory(Backup, Info, "Launching mongodump");
		
		TCVector<CStr> Params = mp_MongoConnectionSettings.f_GetToolParams(false);
		
		Params << fg_CreateVector<CStr>
			(
				CStr("--uri={}"_f << mp_MongoConnectionSettings.f_GetUrl({}))
				, "--forceTableScan"
				, "--quiet"
				, "--oplog"
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
		Launch.m_ToLog = CProcessLaunchActor::ELogFlag_All;
		Launch.m_Params.m_bCreateNewProcessGroup = true;
		
		CMongoManagerActor::fs_SetupEnvironment(Launch.m_Params);
		
		auto LaunchResult = co_await mp_DumpProcessLaunch(&CProcessLaunchActor::f_LaunchSimple, Launch);

		if (LaunchResult.m_ExitCode != 0)
			co_return DMibErrorInstance(fg_Format("Backup dump failed: {}", LaunchResult.f_GetCombinedOut()));

		DLogWithCategory(Backup, Info, "Full database dump finished");
		mp_bInitialDumpFinished = true;

		co_return {};
	}
}
