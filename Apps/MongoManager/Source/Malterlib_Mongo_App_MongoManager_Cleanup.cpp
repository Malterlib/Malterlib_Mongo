
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

			auto fAddExtension = [](CStr const &_File)
				{
#ifdef DPlatformFamily_Windows
					return _File + ".exe";
#else
					return _File;
#endif
				}
			;

			// Kill individual processes
			nKilled += CProcessLaunch::fs_KillProcessesInDirectory(fAddExtension("mongod"));
			if (nKilled)
				DLog(Error, "Cleaned up {} old processes", nKilled);
		}
	}

	TCFuture<void> CMongoManagerActor::fp_CleanupOldProcesses()
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
