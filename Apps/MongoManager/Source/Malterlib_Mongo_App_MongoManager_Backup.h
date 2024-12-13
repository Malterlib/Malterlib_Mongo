// Copyright © 2024 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Concurrency/ConcurrencyManager>

namespace NMib::NMongo::NMongoManager
{
	struct CBackupManagerActorInterface : public CActor
	{
		virtual TCFuture<void> f_StartBackup(CActorSubscription _ManifestFinished, CStr _BackupRoot) = 0;
		virtual void f_MongoStopped() = 0;
	};
}
