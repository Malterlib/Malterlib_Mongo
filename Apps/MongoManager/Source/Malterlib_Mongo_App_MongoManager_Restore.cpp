
#include "Malterlib_Mongo_App_MongoManager_Server.h"

namespace NMib::NMongo::NMongoManager
{
	TCFuture<void> CMongoManagerActor::f_RestoreMongo(CTime const &_RestoreTime)
	{
		TCPromise<void> Promise;
		CStr DumpDirectory = CFile::fs_GetProgramDirectory() + "/MongoDump";
		
		fg_Dispatch
			(
				mp_pFileActor
				, [DumpDirectory]
				{
					CStr OplogFile = CFile::fs_GetProgramDirectory() + "/Oplog.bson";

					if (!CFile::fs_FileExists(DumpDirectory, EFileAttrib_Directory))
						DMibError(fg_Format("Dump directory '{}' does not exist", DumpDirectory));
					
					if (CFile::fs_FileExists(OplogFile))
						CFile::fs_CopyFile(OplogFile, DumpDirectory + "/oplog.bson");
					
				}
			)
			> Promise / [Promise, this, _RestoreTime, DumpDirectory]
			{
				auto Params = fg_CreateVector<CStr>
					(
						"--host"
						, NProcess::NPlatform::fg_Process_GetHostName()
						, "--port"
						, CStr::fs_ToStr(mp_MongoConnectionSettings.m_Port)
						, "--oplogReplay"
					)
				;
				
				if (_RestoreTime.f_IsValid())
				{
					Params.f_Insert("--oplogLimit");
					Params.f_Insert(fg_Format("{}:0", CTimeConvert(_RestoreTime).f_UnixSeconds()));
				}
				
				Params.f_Insert(DumpDirectory);
			
				CMongoManagerActor::fp_LaunchTool
					(
						fp_GetMongoExecutable("mongorestore")
						, CFile::fs_GetProgramDirectory()
						, Params
						, "Restore"
						, ELogVerbosity_All
						, false
					)
					> [Promise](TCAsyncResult<CStr> &&_StdOut)
					{
						if (!_StdOut)
							Promise.f_SetException(_StdOut);
						else
							Promise.f_SetResult();
					}
				;
			}
		;		

		return Promise.f_MoveFuture();
	}
}
