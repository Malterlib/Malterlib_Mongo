// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>

#ifdef DPlatformFamily_Windows
#include <winsock2.h>
#include <Windows.h>
#pragma warning(disable:4267)
#pragma comment(lib, "Dnsapi.lib")
#endif

#include "Malterlib_Mongo_Client.h"
#include "Malterlib_Mongo_BSON.h"

#include <mongocxx/instance.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/stdx.hpp>
#include <mongocxx/uri.hpp>

#include <Mib/Concurrency/ActorCallbackManager>

namespace
{
	struct CMongoClientInit
	{
		mongocxx::instance m_MongoInstance;
		
		CMongoClientInit()
		{
		}
		~CMongoClientInit()
		{
		}
		
	};

	NMib::NStorage::TCAggregate<CMongoClientInit> g_MongoClientInit = {DAggregateInit};
}

namespace NMib::NMongo
{
	CMongoConnectionSettings::CMongoConnectionSettings() = default;

	CMongoConnectionSettings::CMongoConnectionSettings(NStr::CStr const &_Host, uint16 _Port)
		: m_Host(_Host)
		, m_Port(_Port)
	{
	}

	bool CMongoConnectionSettings::f_Compatible(CMongoConnectionSettings const &_Settings) const
	{
		if (!m_bEnableSSL && !_Settings.m_bEnableSSL)
			return true;
		return NStorage::fg_TupleReferences(m_CACertificatePath, m_ClientCertificatePath, m_UserName, m_bEnableSSL)
			== NStorage::fg_TupleReferences(_Settings.m_CACertificatePath, _Settings.m_ClientCertificatePath, _Settings.m_UserName, _Settings.m_bEnableSSL)
		;
	}

	NStr::CStr CMongoConnectionSettings::f_GetConnectionString() const
	{
		return fg_Format("{}:{}", m_Host, m_Port);
	}

	CMongoConnectionSettings CMongoConnectionSettings::f_ForConnectionString(NStr::CStr const &_ConnectionString) const
	{
		CMongoConnectionSettings ConnectionSettings = *this;
		NStr::CStr ConnectString = _ConnectionString;
		ConnectionSettings.m_Host = fg_GetStrSep(ConnectString, ":");
		if (ConnectString.f_IsEmpty())
			ConnectionSettings.m_Port = 27017;
		else
			ConnectionSettings.m_Port = ConnectString.f_ToInt(uint16(27017));
		return ConnectionSettings;
	}

	NContainer::TCVector<NStr::CStr> CMongoConnectionSettings::f_GetToolParams() const
	{
		NContainer::TCVector<NStr::CStr> Params;
		Params << NContainer::fg_CreateVector<NStr::CStr>
			(
				"--host"
				, m_Host
				, "--port"
				, NStr::CStr::fs_ToStr(m_Port)
			)
		;

		if (m_bEnableSSL)
		{
			Params << NContainer::fg_CreateVector<NStr::CStr>
				(
					"--ssl"
					, "--authenticationMechanism"
					, "MONGODB-X509"
					, "--authenticationDatabase"
					, "$external"
				)
			;

			if (!m_CACertificatePath.f_IsEmpty())
				Params.f_Insert({"--sslCAFile", m_CACertificatePath});

			if (!m_ClientCertificatePath.f_IsEmpty())
				Params.f_Insert({"--sslPEMKeyFile", m_ClientCertificatePath});

			if (!m_UserName.f_IsEmpty())
				Params.f_Insert({"-u", m_UserName});
		}

		return Params;
	}

	namespace
	{
		mongocxx::options::client fg_GetConnectionOptions(CMongoConnectionSettings const &_ConnectionSettings, NStr::CStr const &_DefaultDatabase)
		{
			if (!_ConnectionSettings.m_bEnableSSL)
				return {};

			mongocxx::options::client Options;
			mongocxx::options::ssl SSLOptions;

			SSLOptions.allow_invalid_certificates(false);
			SSLOptions.ca_file(_ConnectionSettings.m_CACertificatePath.f_GetStr());
			SSLOptions.pem_file(_ConnectionSettings.m_ClientCertificatePath.f_GetStr());

			Options.ssl_opts(fg_Move(SSLOptions));

			return Options;
		}

		mongocxx::uri fg_GetConnectionURI(CMongoConnectionSettings const &_ConnectionSettings, NStr::CStr const &_DefaultDatabase)
		{
			NStr::CStr URI = _ConnectionSettings.f_GetConnectionString();

			if (_ConnectionSettings.m_bEnableSSL && !_ConnectionSettings.m_UserName.f_IsEmpty())
			{
				URI = fg_Format
					(
						"mongodb://{}@{}:{}/{}?authMechanism=MONGODB-X509&ssl=true&authSource=$external"
						, _ConnectionSettings.m_UserName
						, _ConnectionSettings.m_Host
						, _ConnectionSettings.m_Port
						, _DefaultDatabase
					)
				;
			}
			else
				URI = fg_Format("mongodb://{}:{}/{}", _ConnectionSettings.m_Host, _ConnectionSettings.m_Port, _DefaultDatabase);

			return mongocxx::uri{URI.f_GetStr()};
		}
	}

	struct CMongoClientActor::CInternal
	{
		CInternal(CMongoConnectionSettings const &_ConnectionSettings, NStr::CStr const &_DefaultDatabase)
			: m_ConnectionSettings(_ConnectionSettings)
			, m_DefaultDatabase(_DefaultDatabase)
		{
		}

		NStr::CStr f_MakeSureConnected()
		{
			if (m_pConnection)
				return {};

			try
			{
				m_pConnection = fg_Construct(fg_GetConnectionURI(m_ConnectionSettings, m_DefaultDatabase), fg_GetConnectionOptions(m_ConnectionSettings, m_DefaultDatabase));

				return {};
			}
			catch (std::exception const &_Exception)
			{
				if (_Exception.what())
					return _Exception.what();

				return "Unknown mongo error";
			}
		}

		decltype(auto) f_GetCollection(NStr::CStr _Collection) const
		{
			NStr::CStr Database;
			NStr::CStr Collection;

			if	(_Collection.f_FindChar('.') >= 0)
			{
				Database = NStr::fg_GetStrSep(_Collection, ".");
				Collection = _Collection;
			}
			else
			{
				Database = m_DefaultDatabase;
				Collection = _Collection;
			}

			decltype(auto) CollectionReturn = (*m_pConnection)[Database.f_GetStr()][Collection.f_GetStr()];

			mongocxx::write_concern Concern;
			Concern.journal(true);
			CollectionReturn.write_concern(fg_Move(Concern));
			return CollectionReturn;
		}

		CMongoConnectionSettings m_ConnectionSettings;
		NStr::CStr m_DefaultDatabase;
		NStorage::TCUniquePointer<NThread::CThreadObject> m_pTailThread;
		NStorage::TCUniquePointer<mongocxx::client> m_pConnection;
	};

	CMongoClientActor::CMongoClientActor(CMongoConnectionSettings const &_ConnectionSettings, NStr::CStr const &_DefaultDatabase)
		: mp_pInternal(fg_Construct(_ConnectionSettings, _DefaultDatabase))
	{
		*g_MongoClientInit;
		fg_ThisActor(this)(&CMongoClientActor::fp_ConnectToServer) > NConcurrency::fg_DiscardResult();
	}

	CMongoClientActor::~CMongoClientActor()
	{
	}

	void CMongoClientActor::fp_ConnectToServer()
	{
		auto &Internal = *mp_pInternal;
		Internal.f_MakeSureConnected();
	}

	NConcurrency::TCContinuation<void> CMongoClientActor::fp_Destroy()
	{
		NConcurrency::TCContinuation<void> Continuation;

		auto &Internal = *mp_pInternal;
		if (Internal.m_pTailThread)
		{
			Internal.m_pTailThread->f_Stop(false);
			Internal.m_pConnection->abort();
			Internal.m_pTailThread->f_Stop(true);
			Internal.m_pTailThread.f_Clear();
			Internal.m_pConnection.f_Clear();
		}

		Continuation.f_SetResult();
		return Continuation;
	}

	namespace
	{
		mongocxx::options::find fg_QueryOptions
			(
				CMongoClientActor::EQueryOption _Options
				, NStorage::TCUniquePointer<NEncoding::CEJSON> const &_pFields
				, NStorage::TCUniquePointer<NEncoding::CEJSON> const &_pOrder
			)
		{
			mongocxx::options::find Options;

			if (_Options & CMongoClientActor::EQueryOption_CursorTailable)
			{
				if (_Options & CMongoClientActor::EQueryOption_AwaitData)
					Options.cursor_type(mongocxx::cursor::type::k_tailable_await);
				else
					Options.cursor_type(mongocxx::cursor::type::k_tailable);
			}
			else
				Options.cursor_type(mongocxx::cursor::type::k_non_tailable);

			if (_Options & CMongoClientActor::EQueryOption_NoCursorTimeout)
				Options.no_cursor_timeout(true);

			if (_Options & CMongoClientActor::EQueryOption_SlaveOk)
			{
				mongocxx::read_preference ReadPref;
				ReadPref.mode(mongocxx::read_preference::read_mode::k_nearest);
			}

			if (_Options & CMongoClientActor::EQueryOption_OplogReplay)
				Options.oplog_replay(true);

			if (_Options & CMongoClientActor::EQueryOption_Exhaust)
				Options.exhaust(true);

			if (_Options & CMongoClientActor::EQueryOption_PartialResults)
				Options.allow_partial_results(true);

			if (_pFields)
				Options.projection(fg_ToBSON(*_pFields));

			if (_pOrder)
				Options.sort(fg_ToBSON(*_pOrder));

			return Options;
		}
	}

	NConcurrency::TCContinuation<NConcurrency::CActorSubscription> CMongoClientActor::f_TailQuery
		(
			NStr::CStr const &_Collection
			, NEncoding::CEJSON const &_Query
			, NStr::CStr const &_OrderBy
			, NStorage::TCUniquePointer<NEncoding::CEJSON> _pFields
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

		NStorage::TCUniquePointer<NEncoding::CEJSON> pOrder = fg_Construct();
		(*pOrder)["$natural"] = -1;

		NStorage::TCUniquePointer<NEncoding::CEJSON> pFields = fg_Construct();
		(*pFields)[_OrderBy] = 1;

		fg_ThisActor(this)(&CMongoClientActor::f_Query, _Collection, _Query, 1, 0, fg_Move(pFields), fg_Move(pOrder), EQueryOption_None)
			>
			[
				=
				, pThis = this
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
						[WeakThis=fg_ThisActor(pThis).f_Weak(), this]
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
												Internal.m_pConnection->abort();
												Internal.m_pTailThread->f_Stop(true);
												Internal.m_pTailThread.f_Clear();
												Internal.m_pConnection.f_Clear();
											}
										}
									) > NConcurrency::fg_DiscardResult()
								;
							}
						}
					)
				;

				NStorage::TCSharedPointer<NConcurrency::TCActorSubscriptionManager<void (NEncoding::CEJSON &&_Data), false, COnScopeExitShared>> pCallbackManager
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

							NEncoding::CEJSON Order;

							auto UserQuery = _Query;

							Order["$natural"] = 1;

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

							auto QueryOptions = fg_QueryOptions(Options, pInputFields, nullptr);
							QueryOptions.sort(fg_ToBSON(Order));

							bool bDoneRegistration = false;

							while (_pThread->f_GetState() != NThread::EThreadState_EventWantQuit)
							{
								try
								{
									auto Collection = Internal.f_GetCollection(_Collection);

									auto Cursor = Collection.find(fg_ToBSON(UserQuery), QueryOptions);

									if (!bDoneRegistration)
									{
										bDoneRegistration = true;
										Result.f_SetResult(fg_Move(Registration));
									}

									while (_pThread->f_GetState() != NThread::EThreadState_EventWantQuit)
									{
										for (auto &&Document : Cursor)
										{
											auto Data = fg_FromBSON(Document);
											if (auto pValue = Data.f_Object().f_GetMember(_OrderBy))
												UserQuery[_OrderBy]["$gt"] = *pValue;

											NConcurrency::g_Dispatch(fg_ThisActor(this)) / [=, Data = fg_Move(Data)]() mutable
												{
													(*pCallbackManager)(fg_Move(Data)) > NConcurrency::fg_DiscardResult();
												}
												> NConcurrency::fg_DiscardResult()
											;
										}
									}
								}
								catch (std::exception const &_Exception)
								{
									if (_pThread->f_GetState() == NThread::EThreadState_EventWantQuit)
										return 0;

									const ch8 *pError = "Unknown mongo error";
									if (_Exception.what())
										pError = _Exception.what();

									NEncoding::CEJSON Error;
									Error["error"] = pError;

									NConcurrency::g_Dispatch(fg_ThisActor(this)) / [=, Error = fg_Move(Error)]() mutable
										{
											(*pCallbackManager)(fg_Move(Error)) > NConcurrency::fg_DiscardResult();
										}
										> NConcurrency::fg_DiscardResult()
									;
								}
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
			, NStorage::TCUniquePointer<NEncoding::CEJSON> const &_pFields
			, NStorage::TCUniquePointer<NEncoding::CEJSON> const &_pOrder
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

		auto QueryOptions = fg_QueryOptions(_Options, _pFields, _pOrder);

		if (_nToSkip)
			QueryOptions.skip(_nToSkip);

		if (_nToReturn)
			QueryOptions.limit(_nToReturn);

		try
		{
			auto Collection = Internal.f_GetCollection(_Collection);

			auto Cursor = Collection.find(fg_ToBSON(_Query), QueryOptions);

			NContainer::TCVector<NEncoding::CEJSON> ToReturn;
			for (auto &&Document : Cursor)
			   ToReturn.f_Insert(fg_FromBSON(Document));

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

	NConcurrency::TCContinuation<uint64> CMongoClientActor::f_Count
		(
			NStr::CStr const &_Collection
			, NEncoding::CEJSON const &_Query
			, uint32 _nToReturn
			, uint32 _nToSkip
			, NStorage::TCUniquePointer<NEncoding::CEJSON> const &_pOrder
			, EQueryOption _Options
		)
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

		auto QueryOptions = fg_QueryOptions(_Options, nullptr, _pOrder);

		if (_nToSkip)
			QueryOptions.skip(_nToSkip);

		if (_nToReturn)
			QueryOptions.limit(_nToReturn);

		try
		{
			auto Collection = Internal.f_GetCollection(_Collection);

			uint64 Count = Collection.count(fg_ToBSON(_Query));

			Result.f_SetResult(Count);
			return Result;
		}
		catch (std::exception const &_Exception)
		{
			const ch8 *pError = "Unknown mongo error";
			if (_Exception.what())
				pError = _Exception.what();

			Result.f_SetException(DMibErrorInstance(NStr::fg_Format("MongoDB count failed: {}", pError)));
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

		mongocxx::options::insert InsertOptions;
		if (_Options & EInsertOption_ContinueOnError)
			InsertOptions.ordered(false);
		else
			InsertOptions.ordered(true);

		try
		{
			auto Collection = Internal.f_GetCollection(_Collection);
			Collection.insert_one(fg_ToBSON(_Document), InsertOptions);

			Result.f_SetResult();
			return Result;
		}
		catch (std::exception const &_Exception)
		{
			const ch8 *pError = "Unknown mongo error";
			if (_Exception.what())
				pError = _Exception.what();

			Result.f_SetException(DMibErrorInstance(NStr::fg_Format("MongoDB insert failed: {}", pError)));
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

		mongocxx::options::insert InsertOptions;
		if (_Options & EInsertOption_ContinueOnError)
			InsertOptions.ordered(false);
		else
			InsertOptions.ordered(true);

		try
		{
			auto Collection = Internal.f_GetCollection(_Collection);

			std::vector<bsoncxx::document::value> AllDocuments;

			for (auto &Document : _Documents)
				AllDocuments.push_back(fg_ToBSON(Document));

			Collection.insert_many(AllDocuments);

			Result.f_SetResult();
			return Result;
		}
		catch (std::exception const &_Exception)
		{
			const ch8 *pError = "Unknown mongo error";
			if (_Exception.what())
				pError = _Exception.what();

			Result.f_SetException(DMibErrorInstance(NStr::fg_Format("MongoDB insert failed: {}", pError)));
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

		mongocxx::options::update UpdateOptions;

		if (_Options & EUpdateOption_Upsert)
			UpdateOptions.upsert(true);

		try
		{
			auto Collection = Internal.f_GetCollection(_Collection);

			if (_Options & EUpdateOption_Multi)
				Collection.update_one(fg_ToBSON(_Query), fg_ToBSON(_Update), UpdateOptions);
			else
				Collection.update_many(fg_ToBSON(_Query), fg_ToBSON(_Update), UpdateOptions);

			Result.f_SetResult();
			return Result;
		}
		catch (std::exception const &_Exception)
		{
			const ch8 *pError = "Unknown mongo error";
			if (_Exception.what())
				pError = _Exception.what();

			Result.f_SetException(DMibErrorInstance(NStr::fg_Format("MongoDB update failed: {}", pError)));
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
			auto Collection = Internal.f_GetCollection(_Collection);

			if (_Options & ERemoveOption_JustOne)
				Collection.delete_one(fg_ToBSON(_Query));
			else
				Collection.delete_many(fg_ToBSON(_Query));

			Result.f_SetResult();
			return Result;
		}
		catch (std::exception const &_Exception)
		{
			const ch8 *pError = "Unknown mongo error";
			if (_Exception.what())
				pError = _Exception.what();

			Result.f_SetException(DMibErrorInstance(NStr::fg_Format("MongoDB remove failed: {}", pError)));
			return Result;
		}
	}
}
