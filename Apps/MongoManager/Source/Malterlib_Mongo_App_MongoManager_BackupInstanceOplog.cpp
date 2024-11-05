
#include "Malterlib_Mongo_App_MongoManager_Server.h"
#include "Malterlib_Mongo_App_MongoManager_BackupInstance.h"

#include <Mib/Concurrency/LogError>
#include <Mib/Mongo/BSON>
 
namespace NMib::NMongo::NMongoManager
{
	TCFuture<void> CMongoBackupInstanceActor::fp_SavePendingOplogData(TCSharedPointer<CFile> _pBackupFile)
	{
		if (!mp_pCanDestroy)
			co_return {}; // Destroyed

		auto pCanDestroy = mp_pCanDestroy;

		auto SequnceSubscription = co_await mp_OplogWriteSequencer.f_Sequence();

		mp_PendingSaveScheduled = false;

		auto BlockingActorCheckout = fg_BlockingActor();
		co_await
			(
				g_Dispatch(BlockingActorCheckout) / [pBackupFile = _pBackupFile, Pending = fg_Move(mp_PendingOplogData)]() -> uint64
				{
					CByteVector Data;
					for (auto &JSONData : Pending)
					{
						auto BSON = fg_ToBSON(JSONData);
						
						Data.f_Insert((uint8 const *)BSON.view().data(), BSON.view().length());
					}
					pBackupFile->f_Write(Data.f_GetArray(), Data.f_GetLen());
#ifdef DPlatformFamily_macOS
					// Make file change notification notice change
					pBackupFile->f_SetLength(pBackupFile->f_GetLength());
#endif
					return pBackupFile->f_GetPosition();
				}
			)
		;

		co_return {};
	}
	
	TCFuture<void> CMongoBackupInstanceActor::fp_TailOplog(TCSharedPointer<CFile> _pBackupFile)
	{
		TCSharedPointer<CFile> pBackupFile = _pBackupFile;

		CEJSONOrdered Query;
		Query["fromMigrate"]["$exists"] = false;

		CMongoClientActor::CTailQueryParams TailQueryParams
			{
				.m_Collection = "local.oplog.rs"
				, .m_Query = Query
				, .m_OrderBy = "ts"
				, .m_Options = CMongoClientActor::EQueryOption_AwaitData | CMongoClientActor::EQueryOption_CursorTailable | CMongoClientActor::EQueryOption_NoCursorTimeout
			}
		;
	
		// Start by subscribing to the op log
		mp_MongoTailSubscription = co_await mp_MongoClient
			(
				&CMongoClientActor::f_TailQuery
				, fg_Move(TailQueryParams)
				, g_ActorFunctorWeak / [this, pBackupFile = fg_Move(pBackupFile)](NEncoding::CEJSONOrdered _Result) -> TCFuture<void>
				{
					if (!mp_pCanDestroy)
						co_return {}; // Destroyed
					
					auto pCanDestroy = mp_pCanDestroy;
					
					if (_Result.f_GetMember("error"))
					{
						if (!mp_bMongoStopped)
							DLogWithCategory(Backup, Error, "Error tailing oplog: {}", _Result["error"].f_AsString());
						co_return {};
					}
					
					mp_PendingOplogData.f_Insert(fg_Move(_Result));
					
					if (!mp_PendingSaveScheduled)
					{
						mp_PendingSaveScheduled = true;
						fg_ThisActor(this)(&CMongoBackupInstanceActor::fp_SavePendingOplogData, pBackupFile) > fg_LogError("MongoBackupInstance", "Error writing oplog to file");
					}

					co_return {};
				}
			)
		;

		co_return {};
	}
}
