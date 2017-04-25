
#include <Mib/Concurrency/ConcurrencyManager>

namespace NMib::NMongo::NMongoManager
{
	struct CBackupManagerActorInterface : public CActor
	{
		virtual TCContinuation<void> f_StartBackup(CActorSubscription &&_ManifestFinished, CStr const &_BackupRoot) = 0;
		virtual void f_MongoStopped() = 0;
	};
}
