
#include "Malterlib_Mongo_App_MongoManager_Server.h"

#include <Mib/Web/DDPClient>
#include <Mib/Process/ProcessLaunchActor>
#include <Mib/Mongo/Client>
#include <Mib/Concurrency/ActorCallbackManager>
#include <Mib/Cloud/BackupManager>

namespace NMib::NMongo::NMongoManager
{
	struct CBackupState
	{
		uint64 m_Outstanding = 0;
		uint64 m_Position = 0;
		TCSharedPointer<CFile> m_pFile;
		TCActor<CSeparateThreadActor> m_FileActor;

		uint64 m_StartPosition = 0;
		bool m_bFirst = true;
		CClock m_Clock;
		
#if DMibEnableSafeCheck > 0
		uint64 m_LastDone = 0;
#endif
		
		CBackupState();
		void f_Clear();
	};
	
	enum EBackupState
	{
		EBackupState_Dump
		, EBackupState_Oplog
		, EBackupState_Max
	};
	
	struct CBackupConnection
	{
		CBackupConnection();
		~CBackupConnection();
		void f_Clear(NCloud::CBackupManager::CBackupKey const &_BackupKey);
		TCDistributedActor<NCloud::CBackupManager> const &f_GetBackupManager() const;
		
		TCSharedPointer<bool> m_pDestroyed;
		CStr m_FriendlyName;

		CBackupState m_State[EBackupState_Max];
		bool m_bInitialized = false;
		uint32 m_ProtocolVersion = 0;
	};
	
	enum EBackupCallback
	{
		EBackupCallback_Error
		, EBackupCallback_DumpUploadFinished
	};
	
	struct CBackupCallbackEvent_Error
	{
		CBackupCallbackEvent_Error(CStr const &_Error)
			: m_Error(_Error)
		{
		}
		
		CStr m_Error;
	};
	
	struct CBackupCallbackEvent_DumpUploadFinished
	{
	};
	
	using CBackupCallbackEvent = TCStreamableFixedVariant<EBackupCallback, CBackupCallbackEvent_Error, CBackupCallbackEvent_DumpUploadFinished>;

	struct CMongoBackupInstanceActor : public CActor
	{
		CMongoBackupInstanceActor
			(
				int32 _MongoPort
				, CStr const &_MongoExecutable
				, TCActor<CDistributedActorTrustManager> const &_TrustManager
			)
		;
		~CMongoBackupInstanceActor();

		TCContinuation<CActorSubscription> f_StartBackup(TCActor<CActor> const &_CallbackActor, TCFunction<void (CBackupCallbackEvent const &_Event)> &&_fOnEvent);
		
		TCContinuation<void> f_Destroy() override;
	private:
		void fp_SubscribeToBackupServers();
		
		void fp_UploadBackupToServer(CBackupConnection *_pConnection, EBackupState _Backup);
		void fp_BackupConnectionConnected(CBackupConnection *_pConnection);
		ch8 const *fp_GetBackupName(EBackupState _Backup);
		ch8 const *fp_GetBackupFileName(EBackupState _Backup);
		void fp_UploadDumpToServers();
		void fp_UploadOplogToServers();
		TCContinuation<void> fp_DumpDistribution();
		TCContinuation<void> fp_DumpDatabase();
		TCContinuation<void> fp_CompressDump();
		void fp_OnOplogTailing();
		void fp_TailOplog(TCSharedPointer<CFile> const &_pBackupFile, TCContinuation<CActorSubscription> const &_Continuation, CActorSubscription &&_ActorSubscription);
		void fp_SavePendingOplogData(TCSharedPointer<CFile> const &_pBackupFile);
		TCContinuation<void> fp_DeleteBackup();
		
	private:
		uint64 mp_FileSizes[EBackupState_Max];
		CStr mp_BackupPath[EBackupState_Max];
		bool mp_bInitialBackupFinished[EBackupState_Max] = {};
		bool mp_bInitialBackupUploaded[EBackupState_Max] = {};

		TCActor<CDistributedActorTrustManager> mp_TrustManager;
		TCTrustedActorSubscription<NCloud::CBackupManager> mp_BackupServerActorsSubscription;
		TCMap<TCDistributedActor<NCloud::CBackupManager>, CBackupConnection> mp_BackupManagers;
		
		TCActorSubscriptionManager<void (CBackupCallbackEvent const &_Event), false> mp_OnEventCallback;
		
		TCActor<CMongoClientActor> mp_MongoClient;
		TCActor<CProcessLaunchActor> mp_DumpProcessLaunch;
		TCActor<CProcessLaunchActor> mp_CompressProcessLaunch;
		TCActor<CSeparateThreadActor> mp_FileWriteActor;
		CActorSubscription mp_MongoTailCallback;
		int32 mp_MongoPort;

		NCloud::CBackupManager::CBackupKey mp_BackupKey;
		
		CStr mp_MongoExecutable;
		CStr mp_BackupDirectory;
		CTime mp_BackupTime;
		CStr mp_BackupID;
		TCSharedPointer<CCanDestroyTracker> mp_pCanDestroy;
		TCVector<NEncoding::CEJSON> mp_PendingOplogData;
		bool mp_PendingSaveScheduled = false;
	};
}
