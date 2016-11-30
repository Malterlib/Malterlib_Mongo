
#include "Malterlib_Mongo_App_MongoManager_Server.h"
#include "Malterlib_Mongo_App_MongoManager_BackupInstance.h"

#include <Mib/Concurrency/Actor/Timer>
#include <Mib/Cloud/BackupManager>

namespace NMib::NMongo::NMongoManager
{
	namespace
	{
		void fg_CleanupOldProcesses()
		{
			mint nKilled = 0;
			// First try to gracefully stop manager processes
			nKilled += CProcessLaunch::fs_KillProcessesInDirectory("MongoManager*");

			// Kill individual processes
			nKilled += CProcessLaunch::fs_KillProcessesInDirectory("mongod");
			if (nKilled)
				DLog(Error, "Cleaned up {} old processes", nKilled);
		}
	}

	TCContinuation<void> CMongoManagerActor::fp_CleanupOldProcesses()
	{
		return fg_Dispatch
			(
				mp_pFileActor
				, []
				{
					fg_CleanupOldProcesses();
				}
			)
		;
	}
}
