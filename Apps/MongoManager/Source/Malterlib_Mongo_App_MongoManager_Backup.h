
#include <Mib/Concurrency/ConcurrencyManager>

namespace NMib::NMongo::NMongoManager
{
	struct CBackupManagerActorInterface : public CActor
	{
		virtual TCContinuation<void> f_StartBackup() = 0;
	};
}
