
#include "Malterlib_Mongo_App_MongoManager_Server.h"
#include "Malterlib_Mongo_App_MongoManager_BackupInstance.h"

#include <Mib/Network/SSL>
#include <Mib/Network/Sockets/SSL>
#include <Mib/Concurrency/Actor/Timer>

namespace NMib::NMongo::NMongoManager
{

	CBackupState::CBackupState()
		: m_pFile(fg_Construct())
	{
		m_FileActor = fg_ConstructActor<CSeparateThreadActor>(fg_Construct("Backup state file actor"));
	}
	
	void CBackupState::f_Clear()
	{
		m_Outstanding = 0;
		m_Position = 0;
	}

	CBackupConnection::CBackupConnection()
		: m_pDestroyed(fg_Construct(false))
	{
	}
	
	CBackupConnection::~CBackupConnection()
	{
		*m_pDestroyed = true;
	}
	
	TCDistributedActor<NCloud::CBackupManager> const &CBackupConnection::f_GetBackupManager() const
	{
		return TCMap<TCDistributedActor<NCloud::CBackupManager>, CBackupConnection>::fs_GetKey(this);
	}

	void CBackupConnection::f_Clear(NCloud::CBackupManager::CBackupKey const &_BackupKey)
	{
		for (auto &State : m_State)
			State.f_Clear();
		bool bWasInitialized = m_bInitialized; 
		m_bInitialized = false;
		NCloud::CBackupManager::CStopBackup StopBackup;
		StopBackup.m_BackupKey = _BackupKey;
		auto &BackupManager = f_GetBackupManager();
		if (bWasInitialized)
			DCallActor(BackupManager, NCloud::CBackupManager::f_StopBackup, fg_Move(StopBackup)) > fg_DiscardResult();
	}

	void CMongoBackupInstanceActor::fp_BackupConnectionConnected(CBackupConnection *_pConnection)
	{
		auto BackupManager = _pConnection->f_GetBackupManager();
		
		NCloud::CBackupManager::CStartBackup StartBackup;
		StartBackup.m_BackupKey = mp_BackupKey;
		
		DCallActor(BackupManager, NCloud::CBackupManager::f_StartBackup, fg_Move(StartBackup)) 
			> [this, _pConnection, pDestroyed = _pConnection->m_pDestroyed]
			(NConcurrency::TCAsyncResult<NCloud::CBackupManager::CStartBackup::CResult> &&_Result)
			{
				if (*pDestroyed)
					return; 
				if (!_Result)
				{
					DLogWithCategory(Backup, Error, "Error starting backup on remote server: {}", _Result.f_GetExceptionStr());
					return;
				}
				auto &Result = *_Result;
				_pConnection->m_bInitialized = true;
				_pConnection->m_State[EBackupState_Dump].m_Position = Result.m_BackupSize;
				_pConnection->m_State[EBackupState_Oplog].m_Position = Result.m_OplogSize;
				_pConnection->m_FriendlyName = Result.m_FriendlyName;

				DLogWithCategory(Backup, Info, "Started backup on backup server '{}'", _pConnection->m_FriendlyName);
				
				fp_UploadBackupToServer(_pConnection, EBackupState_Dump);
				fp_UploadBackupToServer(_pConnection, EBackupState_Oplog);
			}
		;
	}
	
	ch8 const *CMongoBackupInstanceActor::fp_GetBackupName(EBackupState _Backup)
	{
		switch (_Backup)
		{
		case EBackupState_Dump:
			return "Full Dump";
		case EBackupState_Oplog:
			return "Oplog";
		default:
			DNeverGetHere;
		}
		
		return nullptr;
	}

	ch8 const *CMongoBackupInstanceActor::fp_GetBackupFileName(EBackupState _Backup)
	{
		switch (_Backup)
		{
		case EBackupState_Dump:
			return "backup";
		case EBackupState_Oplog:
			return "oplog";
		default:
			DNeverGetHere;
		}
		
		return nullptr;
	}

	void CMongoBackupInstanceActor::fp_UploadBackupToServer(CBackupConnection *_pConnection, EBackupState _Backup)
	{
		if (!_pConnection->m_bInitialized || !mp_bInitialBackupFinished[_Backup])
			return;
		
		mint MaxOutstanding = 1024 * 1024;
		mint PacketSize = 64*1024;
		
		auto &State = _pConnection->m_State[_Backup];
		
		while (State.m_Outstanding < MaxOutstanding && State.m_Position < mp_FileSizes[_Backup])
		{
			uint64 Position = State.m_Position;
			mint ThisTime = fg_Min(mp_FileSizes[_Backup] - Position, PacketSize);
			
			if (State.m_bFirst)
			{
				State.m_bFirst = false;
				State.m_StartPosition = Position;
				State.m_Clock.f_Start();
			}
			
			State.m_Outstanding += ThisTime;
			State.m_Position += ThisTime;
			
			State.m_FileActor
				(
					&CActor::f_DispatchWithReturn<TCContinuation<TCVector<uint8>>>
					, 
					[
						pFile = State.m_pFile
						, DumpPath = this->mp_BackupPath[_Backup]
						, ThisTime
						, Position
					]
					()
					{
						return TCContinuation<TCVector<uint8>>::fs_RunProtected<CExceptionFile>()
							> [&]()
							{
								if (!pFile->f_IsValid())
									pFile->f_Open(DumpPath, EFileOpen_Read | EFileOpen_ShareRead | EFileOpen_NoLocalCache);
								
								pFile->f_SetPosition(Position);
								
								TCVector<uint8> Data;
								Data.f_SetLen(ThisTime);
								pFile->f_Read(Data.f_GetArray(), ThisTime);
								
								return Data;
							}
						;
					}
				)
				> [this, pDestroyed = _pConnection->m_pDestroyed, _Backup, _pConnection, Position, ThisTime](TCAsyncResult<TCVector<uint8>> &&_Result)
				{
					if (*pDestroyed)
						return;
					
					if (!_Result)
					{
						DLogWithCategory(Backup, Error, "Backup({}) Error reading file '{}' for upload: {}", _pConnection->m_FriendlyName, fp_GetBackupName(_Backup), _Result.f_GetExceptionStr());
						return;
					}

					bool bFinished = ((Position + _Result->f_GetLen()) == mp_FileSizes[_Backup]) && (_Backup != EBackupState_Oplog);
					
					NCloud::CBackupManager::CUploadData UploadData;
					UploadData.m_BackupKey = mp_BackupKey;
					UploadData.m_Flags = bFinished ? NCloud::CBackupManager::CUploadData::EFlag_Finished : NCloud::CBackupManager::CUploadData::EFlag_None;
					UploadData.m_Size = mp_FileSizes[_Backup];
					UploadData.m_Data = fg_Move(*_Result);
					UploadData.m_Position = Position;
					UploadData.m_File = fp_GetBackupFileName(_Backup);
					
					auto BackupManager = _pConnection->f_GetBackupManager();		

					DCallActor
						(
							BackupManager
							, NCloud::CBackupManager::f_UploadData
							, fg_Move(UploadData)
						)
						> [this, _pConnection, _Backup, pDestroyed, Position, ThisTime, bFinished](NConcurrency::TCAsyncResult<NCloud::CBackupManager::CUploadData::CResult> &&_Result)
						{
							if (*pDestroyed)
								return; 
							if (!_Result)
							{
								DLogWithCategory
									(
										Backup
										, Error
										, "Backup({}): Error uploading file '{}' on remote server: {}"
										, _pConnection->m_FriendlyName
										, fp_GetBackupName(_Backup)
										, _Result.f_GetExceptionStr()
									)
								;
								return;
							}
							auto &State = _pConnection->m_State[_Backup];
#if DMibEnableSafeCheck > 0
							DFastCheck(Position >= State.m_LastDone);
							State.m_LastDone = Position;
#endif
							State.m_Outstanding -= ThisTime;
							if (bFinished)
							{
								DFastCheck(State.m_Outstanding == 0);
								if (State.m_Outstanding == 0)
								{
									if (!mp_bInitialBackupUploaded[_Backup])
									{
										mp_bInitialBackupUploaded[_Backup] = true;
										mp_OnEventCallback(CBackupCallbackEvent_DumpUploadFinished{});
									}
									fp64 Time = State.m_Clock.f_GetTime();
									DLogWithCategory
										(
											Backup
											, Info
											, "Backup({}): File '{}' fully transfered to remote server. {ns } bytes at {fe2} MB/s"
											, _pConnection->m_FriendlyName
											, fp_GetBackupName(_Backup)
											, mp_FileSizes[_Backup] - State.m_StartPosition
											, (fp64(mp_FileSizes[_Backup] - State.m_StartPosition) / 1'000'000.0) / Time
										)
									;
								}
								else
								{
									DLogWithCategory
										(
											Backup
											, Error
											, "Backup({}): File '{}' invalid sequence of results received from remote server"
											, _pConnection->m_FriendlyName
											, fp_GetBackupName(_Backup)
										)
									;
								}
							}

							// Reschedule
							fp_UploadBackupToServer(_pConnection, _Backup);
						}
					;
				}
			;
		}
	}

	void CMongoBackupInstanceActor::fp_UploadDumpToServers()
	{
		if (!mp_pCanDestroy)
			return; // Destroyed
		
		mp_bInitialBackupFinished[EBackupState_Dump] = true;
		
		for (CBackupConnection &Connection : mp_BackupManagers)
			fp_UploadBackupToServer(&Connection, EBackupState_Dump);
	}
	
	void CMongoBackupInstanceActor::fp_UploadOplogToServers()
	{
		if (!mp_pCanDestroy)
			return; // Destroyed
		
		mp_bInitialBackupFinished[EBackupState_Oplog] = true;
		for (CBackupConnection &Connection : mp_BackupManagers)
			fp_UploadBackupToServer(&Connection, EBackupState_Oplog);
	}
}
