// Copyright © 2024 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Mongo_App_MongoManager_Server.h"

namespace NMib::NMongo::NMongoManager
{
	TCFuture<void> CMongoManagerActor::f_RestoreMongo(CTime _RestoreTime)
	{
		CStr DumpDirectory = CFile::fs_GetProgramDirectory() + "/MongoDump";

		{
			auto BlockingActorCheckout = fg_BlockingActor();
			co_await
				(
					g_Dispatch(BlockingActorCheckout) / [DumpDirectory]
					{
						CStr OplogFile = CFile::fs_GetProgramDirectory() + "/Oplog.bson";

						if (!CFile::fs_FileExists(DumpDirectory, EFileAttrib_Directory))
							DMibError(fg_Format("Dump directory '{}' does not exist", DumpDirectory));

						if (CFile::fs_FileExists(OplogFile))
							CFile::fs_CopyFile(OplogFile, DumpDirectory + "/oplog.bson");

					}
				)
			;
		}

		auto &MongoHost = mp_MongoConnectionSettings.f_GetSingleHost();

		auto Params = fg_CreateVector<CStr>
			(
				"--host"
				, MongoHost.m_Host
				, "--port"
				, CStr::fs_ToStr(MongoHost.m_Port)
				, "--oplogReplay"
			)
		;

		if (_RestoreTime.f_IsValid())
		{
			Params.f_Insert("--oplogLimit");
			Params.f_Insert(fg_Format("{}:0", CTimeConvert(_RestoreTime).f_UnixSeconds()));
		}

		Params.f_Insert(DumpDirectory);

		co_await fp_LaunchTool
			(
				fp_GetMongoExecutable("mongorestore")
				, CFile::fs_GetProgramDirectory()
				, Params
				, CStr("Restore")
				, ELogVerbosity_All
				, false
				, CStr()
				, CStr()
				, CStr()
#ifdef DPlatformFamily_Windows
				, CStrSecure()
#endif
			)
		;

		co_return {};
	}
}
