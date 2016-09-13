// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>

#include "Malterlib_Mongo_Client.h"
#include "Malterlib_Mongo_BSON.h"

#include <mongo/client/dbclient.h>

#include <Mib/Concurrency/ActorCallbackManager>

namespace
{
	struct CMongoClientInit
	{
		CMongoClientInit()
		{
			mongo::client::initialize();
			atexit([]{mongo::client::shutdown();});
		}
		~CMongoClientInit()
		{
		}
		
	};
	NMib::NAggregate::TCAggregate<CMongoClientInit> g_MongoClientInit = {DAggregateInit};

}

using namespace mongo;

namespace NMib
{
	namespace NMongo
	{

		struct CMongoClientActor::CInternal
		{
			NStr::CStr m_ServerAddress;
			NStr::CStr m_DefaultDatabase;
			NPtr::TCUniquePointer<NThread::CThreadObject> m_pTailThread;
			mongo::DBClientConnection m_Connection;
			bool m_bConnected = false;
			
			NStr::CStr f_MakeSureConnected()
			{
				if (m_bConnected)
					return NStr::CStr();
				
				try
				{
					std::string Error;
					if (!m_Connection.connect(m_ServerAddress.f_GetStr(), Error))
						return Error.c_str();
					
					m_bConnected = true;
					return {};
				}
				catch (std::exception const &_Exception)
				{
					if (_Exception.what())
						return _Exception.what();
					
					return "Unknown mongo error";
				}
			}
			
			std::string f_GetNamespace(NStr::CStr const &_Collection) const
			{
				NStr::CStr DatabaseAndConnection;
				
				if	(_Collection.f_FindChar('.') >= 0)
					DatabaseAndConnection = _Collection;
				else
				{
					DatabaseAndConnection = m_DefaultDatabase;
					DatabaseAndConnection += ".";
					DatabaseAndConnection += _Collection;
				}
				
				return DatabaseAndConnection.f_GetStr();
			}
		};

		CMongoClientActor::CMongoClientActor(NStr::CStr const &_ServerAddress, NStr::CStr const &_DefaultDatabase)
			: mp_pInternal(fg_Construct())
		{
			auto &Internal = *mp_pInternal;
			Internal.m_ServerAddress = _ServerAddress;
			Internal.m_DefaultDatabase = _DefaultDatabase;
		}

		CMongoClientActor::~CMongoClientActor()
		{
		}

		void CMongoClientActor::fp_ConnectToServer()
		{
			auto &Internal = *mp_pInternal;
			Internal.m_Connection.setWriteConcern(WriteConcern::journaled);
			Internal.f_MakeSureConnected();
		}

		void CMongoClientActor::f_Construct()
		{
			*g_MongoClientInit;
			fg_ThisActor(this)(&CMongoClientActor::fp_ConnectToServer) > NConcurrency::fg_DiscardResult();
		}

		NConcurrency::TCContinuation<void> CMongoClientActor::f_Destroy()
		{
			NConcurrency::TCContinuation<void> Continuation;
			
			auto &Internal = *mp_pInternal;
			if (Internal.m_pTailThread)
			{
				Internal.m_pTailThread->f_Stop(false);
				Internal.m_Connection.abort();
				Internal.m_pTailThread->f_Stop(true);
				Internal.m_pTailThread.f_Clear();
			}
			
			Continuation.f_SetResult();
			return Continuation;			
		}

		NConcurrency::TCContinuation<NConcurrency::CActorSubscription> CMongoClientActor::f_TailQuery
			(
				NStr::CStr const &_Collection
				, NEncoding::CEJSON const &_Query
				, NStr::CStr const &_OrderBy
				, NPtr::TCUniquePointer<NEncoding::CEJSON> _pFields
				, EQueryOption _Options
				, NConcurrency::TCActor<CActor> &&_CallbackActor
				, NFunction::TCFunctionMutable<void (NEncoding::CEJSON &&_Result)> &&_fOnResult
			)
		{
			NConcurrency::TCContinuation<NConcurrency::CActorSubscription> Result;
			auto &Internal = *mp_pInternal;
			if (Internal.m_pTailThread)
			{
				Result.f_SetException(DMibErrorInstance("Tailing query already running"));
				return Result;
			}
			NStr::CStr Error = Internal.f_MakeSureConnected();
			if (!Error.f_IsEmpty())
			{
				Result.f_SetException(DMibErrorInstance(fg_Format("Failed to connect to MongoDB server: {}", Error)));
				return Result;
			}
			
			NEncoding::CEJSON Query;
			
			Query["$query"] = _Query;
			Query["orderby"]["$natural"] = -1;
			
			NPtr::TCUniquePointer<NEncoding::CEJSON> pFields = fg_Construct();
			(*pFields)[_OrderBy] = 1;
			
			fg_ThisActor(this)(&CMongoClientActor::f_Query, _Collection, Query, 1, 0, fg_Move(pFields), EQueryOption_None)
				>
				[
					=
					, _pFields = fg_Move(_pFields)
					, _CallbackActor = fg_Move(_CallbackActor)
					, _fOnResult = fg_Move(_fOnResult)
				]
				(NConcurrency::TCAsyncResult<NContainer::TCVector<NEncoding::CEJSON>> &&_Result) mutable
				{
					auto &Internal = *mp_pInternal;
					
					if (!_Result)
					{
						Result.f_SetException(fg_Move(_Result));
						return;
					}
					
					NEncoding::CEJSON GetOnwardsFromValue;
					
					if (!_Result->f_IsEmpty() && _Result->f_GetFirst().f_IsValid())
						GetOnwardsFromValue = fg_Move(_Result->f_GetFirst()[_OrderBy]);
					
					COnScopeExitShared pOnExit = fg_OnScopeExitShared
						(
							[WeakThis=fg_ThisActor(this).f_Weak(), this]
							{
								auto This = WeakThis.f_Lock();
								if (This)
								{
									This
										(
											&CActor::f_Dispatch
											, [this]
											{
												auto &Internal = *mp_pInternal;
												if (Internal.m_pTailThread)
												{
													Internal.m_pTailThread->f_Stop(false);
													Internal.m_Connection.abort();
													Internal.m_pTailThread->f_Stop(true);
													Internal.m_pTailThread.f_Clear();
												}
											}
										) > NConcurrency::fg_DiscardResult()
									;
								}
							}
						)
					;
					
					NPtr::TCSharedPointer<NConcurrency::TCActorSubscriptionManager<void (NEncoding::CEJSON &&_Data), false, COnScopeExitShared>> pCallbackManager
						= fg_Construct(this, false)
					;
					
					auto Registration = pCallbackManager->f_Register(fg_Move(_CallbackActor), fg_Move(_fOnResult), pOnExit);
					
					Internal.m_pTailThread = NThread::CThreadObject::fs_StartThread
						(
							[
								this
								, _OrderBy
								, pInputFields = fg_Move(_pFields)
								, _Query
								, _Options
								, _Collection
								, pCallbackManager = fg_Move(pCallbackManager)
								, Registration = fg_Move(Registration)
								, Result
								, GetOnwardsFromValue
							](NThread::CThreadObject *_pThread) mutable -> aint
							{ 
								auto &Internal = *mp_pInternal;
								
								BSONObj Fields;
								BSONObj *pFields = nullptr;
								
								if (pFields)
								{
									Fields = fg_ToBSON(*pInputFields);
									pFields = &Fields;
								}
								
								NEncoding::CEJSON Query;
								
								auto &UserQuery = Query["$query"] = _Query;
								Query["orderby"]["$natural"] = 1;
								
								CMongoClientActor::EQueryOption Options = _Options
									| CMongoClientActor::EQueryOption_AwaitData
									| CMongoClientActor::EQueryOption_CursorTailable
									| CMongoClientActor::EQueryOption_NoCursorTimeout
								;
								
								if (GetOnwardsFromValue.f_IsValid())
								{
									UserQuery[_OrderBy]["$gt"] = GetOnwardsFromValue;
									if (_Collection == "local.oplog.rs" && _OrderBy == "ts")
										Options |= CMongoClientActor::EQueryOption_OplogReplay;
								}
								
								bool bDoneRegistration = false;
								
								try
								{
									while (true)
									{
										auto pCursor = Internal.m_Connection.query
											(
												Internal.f_GetNamespace(_Collection)
												, fg_ToBSON(Query)
												, 0
												, 0
												, pFields
												, Options
											)
										;
										
										if (!pCursor.get())
											break;
										
										if (!bDoneRegistration)
										{
											bDoneRegistration = true;
											Result.f_SetResult(fg_Move(Registration));
										}
										
										while (true)
										{
											while (pCursor->more())
											{
												auto Data = fg_FromBSON(pCursor->next());
												if (auto pValue = Data.f_Object().f_GetMember(_OrderBy))
													UserQuery[_OrderBy]["$gt"] = *pValue;
												fg_ThisActor(this)
													(
														&CActor::f_Dispatch
														, [=, Data = fg_Move(Data)]() mutable
														{
															(*pCallbackManager)(fg_Move(Data));
														}
													) > NConcurrency::fg_DiscardResult()
												;
											}
											if (pCursor->isDead())
												break;
										}
									}
								}
								catch (std::exception const &_Exception)
								{
									const ch8 *pError = "Unknown mongo error";
									if (_Exception.what())
										pError = _Exception.what();
									
									NEncoding::CEJSON Error;
									Error["error"] = pError;

									(*pCallbackManager)(fg_Move(Error));
								}
								
								return 0;
							}
							, "Mongo client tailing thread"
						)
					;
				}
			;
			
			return Result;
		}

		NConcurrency::TCContinuation<NContainer::TCVector<NEncoding::CEJSON>> CMongoClientActor::f_Query
			(
				NStr::CStr const &_Collection
				, NEncoding::CEJSON const &_Query
				, uint32 _nToReturn
				, uint32 _nToSkip
				, NPtr::TCUniquePointer<NEncoding::CEJSON> const &_pFields
				, EQueryOption _Options
			)
		{
			NConcurrency::TCContinuation<NContainer::TCVector<NEncoding::CEJSON>> Result;
			auto &Internal = *mp_pInternal;
			if (Internal.m_pTailThread)
			{
				Result.f_SetException(DMibErrorInstance("Tailing query already running"));
				return Result;
			}
			NStr::CStr Error = Internal.f_MakeSureConnected();
			if (!Error.f_IsEmpty())
			{
				Result.f_SetException(DMibErrorInstance(fg_Format("Failed to connect to MongoDB server: {}", Error)));
				return Result;
			}
			
			BSONObj Fields;
			BSONObj *pFields = nullptr;
			
			if (_pFields)
			{
				Fields = fg_ToBSON(*_pFields);
				pFields = &Fields;
			}
			try
			{
				auto pCursor = Internal.m_Connection.query(Internal.f_GetNamespace(_Collection), fg_ToBSON(_Query), _nToReturn, _nToSkip, pFields, _Options);
				NContainer::TCVector<NEncoding::CEJSON> ToReturn;
				while (pCursor->more())
				   ToReturn.f_Insert(fg_FromBSON(pCursor->next()));
				
				Result.f_SetResult(fg_Move(ToReturn));
				return Result;
			}
			catch (std::exception const &_Exception)
			{
				const ch8 *pError = "Unknown mongo error";
				if (_Exception.what())
					pError = _Exception.what();
				
				Result.f_SetException(DMibErrorInstance(NStr::fg_Format("Mongo query failed: {}", pError)));
				return Result;
			}
		}

		NConcurrency::TCContinuation<uint64> CMongoClientActor::f_Count(NStr::CStr const &_Collection, NEncoding::CEJSON const &_Query, uint32 _nToReturn, uint32 _nToSkip, EQueryOption _Options)
		{
			NConcurrency::TCContinuation<uint64> Result;
			auto &Internal = *mp_pInternal;
			if (Internal.m_pTailThread)
			{
				Result.f_SetException(DMibErrorInstance("Tailing query already running"));
				return Result;
			}
			NStr::CStr Error = Internal.f_MakeSureConnected();
			if (!Error.f_IsEmpty())
			{
				Result.f_SetException(DMibErrorInstance(fg_Format("Failed to connect to MongoDB server: {}", Error)));
				return Result;
			}
			
			try
			{
				uint64 Count = Internal.m_Connection.count(Internal.f_GetNamespace(_Collection), fg_ToBSON(_Query), _nToReturn, _nToSkip, _Options);

				std::string CountError = Internal.m_Connection.getLastError();
				
				if (!CountError.empty())
				{
					Result.f_SetException(DMibErrorInstance(NStr::fg_Format("MongeDB count failed: {}", CountError.c_str())));
					return Result;
				}
				
				Result.f_SetResult(Count);
				return Result;
			}
			catch (std::exception const &_Exception)
			{
				const ch8 *pError = "Unknown mongo error";
				if (_Exception.what())
					pError = _Exception.what();
				
				Result.f_SetException(DMibErrorInstance(NStr::fg_Format("Mongo count failed: {}", pError)));
				return Result;
			}
		}

		NConcurrency::TCContinuation<void> CMongoClientActor::f_Insert(NStr::CStr const &_Collection, NEncoding::CEJSON const &_Document, EInsertOption _Options)
		{
			NConcurrency::TCContinuation<void> Result;
			auto &Internal = *mp_pInternal;
			if (Internal.m_pTailThread)
			{
				Result.f_SetException(DMibErrorInstance("Tailing query already running"));
				return Result;
			}
			NStr::CStr Error = Internal.f_MakeSureConnected();
			if (!Error.f_IsEmpty())
			{
				Result.f_SetException(DMibErrorInstance(fg_Format("Failed to connect to MongoDB server: {}", Error)));
				return Result;
			}
			
			try
			{
				Internal.m_Connection.insert(Internal.f_GetNamespace(_Collection), fg_ToBSON(_Document));
				
				std::string InsertError = Internal.m_Connection.getLastError();
				
				if (!InsertError.empty())
				{
					Result.f_SetException(DMibErrorInstance(NStr::fg_Format("MongeDB insert failed: {}", InsertError.c_str())));
					return Result;
				}
				
				Result.f_SetResult();
				return Result;
			}
			catch (std::exception const &_Exception)
			{
				const ch8 *pError = "Unknown mongo error";
				if (_Exception.what())
					pError = _Exception.what();
				
				Result.f_SetException(DMibErrorInstance(NStr::fg_Format("Mongo insert failed: {}", pError)));
				return Result;
			}
		}

		NConcurrency::TCContinuation<void> CMongoClientActor::f_BatchInsert(NStr::CStr const &_Collection, NContainer::TCVector<NEncoding::CEJSON> const &_Documents, EInsertOption _Options)
		{
			NConcurrency::TCContinuation<void> Result;
			auto &Internal = *mp_pInternal;
			if (Internal.m_pTailThread)
			{
				Result.f_SetException(DMibErrorInstance("Tailing query already running"));
				return Result;
			}
			NStr::CStr Error = Internal.f_MakeSureConnected();
			if (!Error.f_IsEmpty())
			{
				Result.f_SetException(DMibErrorInstance(fg_Format("Failed to connect to MongoDB server: {}", Error)));
				return Result;
			}
			
			try
			{
				std::vector<BSONObj> AllDocuments;
				
				for (auto &Document : _Documents)
					AllDocuments.push_back(fg_ToBSON(Document));
				
				Internal.m_Connection.insert(Internal.f_GetNamespace(_Collection), AllDocuments, _Options);
				
				std::string InsertError = Internal.m_Connection.getLastError();
				
				if (!InsertError.empty())
				{
					Result.f_SetException(DMibErrorInstance(NStr::fg_Format("MongeDB insert failed: {}", InsertError.c_str())));
					return Result;
				}
				
				Result.f_SetResult();
				return Result;
			}
			catch (std::exception const &_Exception)
			{
				const ch8 *pError = "Unknown mongo error";
				if (_Exception.what())
					pError = _Exception.what();
				
				Result.f_SetException(DMibErrorInstance(NStr::fg_Format("Mongo insert failed: {}", pError)));
				return Result;
			}
		}

		NConcurrency::TCContinuation<void> CMongoClientActor::f_Update(NStr::CStr const &_Collection, NEncoding::CEJSON const &_Query, NEncoding::CEJSON const &_Update, EUpdateOption _Options)
		{
			NConcurrency::TCContinuation<void> Result;
			auto &Internal = *mp_pInternal;
			if (Internal.m_pTailThread)
			{
				Result.f_SetException(DMibErrorInstance("Tailing query already running"));
				return Result;
			}
			NStr::CStr Error = Internal.f_MakeSureConnected();
			if (!Error.f_IsEmpty())
			{
				Result.f_SetException(DMibErrorInstance(fg_Format("Failed to connect to MongoDB server: {}", Error)));
				return Result;
			}
			
			try
			{
				Internal.m_Connection.update(Internal.f_GetNamespace(_Collection), fg_ToBSON(_Query), fg_ToBSON(_Update), _Options);
				
				std::string UpdateError = Internal.m_Connection.getLastError();
				
				if (!UpdateError.empty())
				{
					Result.f_SetException(DMibErrorInstance(NStr::fg_Format("MongeDB update failed: {}", UpdateError.c_str())));
					return Result;
				}
				
				Result.f_SetResult();
				return Result;
			}
			catch (std::exception const &_Exception)
			{
				const ch8 *pError = "Unknown mongo error";
				if (_Exception.what())
					pError = _Exception.what();
				
				Result.f_SetException(DMibErrorInstance(NStr::fg_Format("Mongo update failed: {}", pError)));
				return Result;
			}
		}

		NConcurrency::TCContinuation<void> CMongoClientActor::f_Remove(NStr::CStr const &_Collection, NEncoding::CEJSON const &_Query, ERemoveOption _Options)
		{
			NConcurrency::TCContinuation<void> Result;
			auto &Internal = *mp_pInternal;
			if (Internal.m_pTailThread)
			{
				Result.f_SetException(DMibErrorInstance("Tailing query already running"));
				return Result;
			}
			NStr::CStr Error = Internal.f_MakeSureConnected();
			if (!Error.f_IsEmpty())
			{
				Result.f_SetException(DMibErrorInstance(fg_Format("Failed to connect to MongoDB server: {}", Error)));
				return Result;
			}
			
			try
			{
				Internal.m_Connection.remove(Internal.f_GetNamespace(_Collection), fg_ToBSON(_Query), _Options);
				
				std::string RemoveError = Internal.m_Connection.getLastError();
				
				if (!RemoveError.empty())
				{
					Result.f_SetException(DMibErrorInstance(NStr::fg_Format("MongeDB update failed: {}", RemoveError.c_str())));
					return Result;
				}
				
				Result.f_SetResult();
				return Result;
			}
			catch (std::exception const &_Exception)
			{
				const ch8 *pError = "Unknown mongo error";
				if (_Exception.what())
					pError = _Exception.what();
				
				Result.f_SetException(DMibErrorInstance(NStr::fg_Format("Mongo remove failed: {}", pError)));
				return Result;
			}
		}
	}
}
