// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Concurrency/ConcurrencyManager>

namespace NMib::NMongo::NMongoManager
{
	struct CBackupManagerActorInterface : public CActor
	{
		virtual TCFuture<void> f_StartBackup(CActorSubscription _ManifestFinished, CStr _BackupRoot) = 0;
		virtual void f_MongoStopped() = 0;
	};
}
