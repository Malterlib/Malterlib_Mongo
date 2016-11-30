
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
					TCVector<uint8> Data;
					for (auto &JSONData : Pending)
					{
						auto BSON = fg_ToBSON(JSONData);
						Data.f_Insert((uint8 const *)BSON.objdata(), BSON.objsize());
					}
					pBackupFile->f_Write(Data.f_GetArray(), Data.f_GetLen());
					return pBackupFile->f_GetPosition();
				}
			)
			> [this](TCAsyncResult<uint64> &&_Result)
			{
				if (!_Result)
				{
					DLogWithCategory(Backup, Error, "Error writing oplog to file: {}", _Result.f_GetExceptionStr());
					return;
				}

				mp_FileSizes[EBackupState_Oplog] = *_Result;
				fp_UploadOplogToServers();
			}
		;
	}
	
	void CMongoBackupInstanceActor::fp_TailOplog(TCSharedPointer<CFile> const &_pBackupFile, TCContinuation<CActorSubscription> const &_Continuation, CActorSubscription &&_ActorSubscription)
	{
		TCSharedPointer<CFile> pBackupFile = _pBackupFile;

		CEJSON Query;
		Query["fromMigrate"]["$exists"] = false;
		
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
			> [this, Continuation = fg_Move(_Continuation), ActorSubscription = fg_Move(_ActorSubscription)](TCAsyncResult<CActorSubscription> &&_Result) mutable
			{
				if (!_Result)
				{
					Continuation.f_SetException(fg_Move(_Result));
					return ;
				}
				
				mp_MongoTailCallback = fg_Move(*_Result);
				
				// We are now listening to oplog changes, it's now safe to dump the database as we will get oplog ovelap
				fp_OnOplogTailing();
				
				// Don't report success until now as we don't want the application to start until backup is recording changes
				Continuation.f_SetResult(fg_Move(ActorSubscription));
			}
		;
	}
}
