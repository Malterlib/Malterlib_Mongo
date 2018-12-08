
#include "Malterlib_Mongo_App_MongoManager_Server.h"
#include "Malterlib_Mongo_App_MongoManager_BackupInstance.h"

#include <Mib/Mongo/BSON>
 
namespace NMib::NMongo::NMongoManager
{
	void CMongoBackupInstanceActor::fp_SavePendingOplogData(TCSharedPointer<CFile> const &_pBackupFile)
	{
		if (!mp_pCanDestroy)
			return; // Destroyed

		mp_PendingSaveScheduled = false;
		
		TCSharedPointer<CFile> pBackupFile = _pBackupFile;
		auto pCanDestroy = mp_pCanDestroy;
		
		fg_Dispatch
			(
				mp_FileWriteActor
				, [pCanDestroy, pBackupFile, Pending = fg_Move(mp_PendingOplogData)]() -> uint64
				{
					CByteVector Data;
					for (auto &JSONData : Pending)
					{
						auto BSON = fg_ToBSON(JSONData);
						
						Data.f_Insert((uint8 const *)BSON.view().data(), BSON.view().length());
					}
					pBackupFile->f_Write(Data.f_GetArray(), Data.f_GetLen());
#ifdef DPlatformFamily_OSX
					// Make file change notification notice change
					pBackupFile->f_SetLength(pBackupFile->f_GetLength());
#endif
					return pBackupFile->f_GetPosition();
				}
			)
			> [](TCAsyncResult<uint64> &&_Result)
			{
				if (!_Result)
				{
					DLogWithCategory(Backup, Error, "Error writing oplog to file: {}", _Result.f_GetExceptionStr());
					return;
				}
			}
		;
	}
	
	TCContinuation<void> CMongoBackupInstanceActor::fp_TailOplog(TCSharedPointer<CFile> const &_pBackupFile)
	{
		TCSharedPointer<CFile> pBackupFile = _pBackupFile;

		CEJSON Query;
		Query["fromMigrate"]["$exists"] = false;
	
		TCContinuation<void> Continuation;
		
		// Start by subscribing to the op log
		mp_MongoClient
			(
				&CMongoClientActor::f_TailQuery
				, "local.oplog.rs"
				, Query
				, "ts"
				, nullptr
				, CMongoClientActor::EQueryOption_AwaitData | CMongoClientActor::EQueryOption_CursorTailable | CMongoClientActor::EQueryOption_NoCursorTimeout
				, fg_ThisActor(this)
				,[this, pBackupFile = fg_Move(pBackupFile)](NEncoding::CEJSON &&_Result)
				{
					if (!mp_pCanDestroy)
						return; // Destroyed
					
					auto pCanDestroy = mp_pCanDestroy;
					
					if (_Result.f_GetMember("error"))
					{
						if (!mp_bMongoStopped)
							DLogWithCategory(Backup, Error, "Error tailing oplog: {}", _Result["error"].f_AsString());
						return;
					}
					
					mp_PendingOplogData.f_Insert(fg_Move(_Result));
					
					if (!mp_PendingSaveScheduled)
					{
						mp_PendingSaveScheduled = true;
						fg_ThisActor(this)(&CMongoBackupInstanceActor::fp_SavePendingOplogData, pBackupFile)
							> [pCanDestroy](TCAsyncResult<void> &&_Result)
							{
							}
						;
					}
				}
			)
			> Continuation / [this, Continuation](CActorSubscription &&_Subscription) mutable
			{
				mp_MongoTailSubscription = fg_Move(_Subscription);
				
				Continuation.f_SetResult();
			}
		;
		
		return Continuation;
	}
}
