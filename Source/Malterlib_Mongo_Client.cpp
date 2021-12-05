// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>

#ifdef DPlatformFamily_Windows
#include <winsock2.h>
#include <Windows.h>
#pragma warning(disable:4267)
#pragma comment(lib, "Dnsapi.lib")
#else
#include <signal.h>
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

	constinit NMib::NStorage::TCAggregate<CMongoClientInit> g_MongoClientInit = {DAggregateInit};
}

namespace NMib::NMongo
{
	bool CMongoConnectionSettings::f_Compatible(CMongoConnectionSettings const &_Settings) const
	{
		if (!m_bEnableSSL && !_Settings.m_bEnableSSL)
			return true;

		return NStorage::fg_TupleReferences(m_CACertificatePath, m_ClientCertificatePath, m_UserName, m_bEnableSSL)
			== NStorage::fg_TupleReferences(_Settings.m_CACertificatePath, _Settings.m_ClientCertificatePath, _Settings.m_UserName, _Settings.m_bEnableSSL)
		;
	}

	NStr::CStr CMongoConnectionSettings::fs_GetConnectionString(NContainer::TCVector<CMongoServerHost> const &_Hosts)
	{
		using namespace NStr;

		CStr ConnectionString;

		for (auto &Host : _Hosts)
			fg_AddStrSep(ConnectionString, "{}:{}"_f << Host.m_Host << Host.m_Port, ",");

		return ConnectionString;
	}

	NStr::CStr CMongoConnectionSettings::f_GetConnectionString() const
	{
		if (m_bEnableSrv)
		{
			DMibCheck(m_Hosts.f_GetLen() == 1);
			if (m_Hosts.f_GetLen() == 1)
				return m_Hosts[0].m_Host;
		}
		return fs_GetConnectionString(m_Hosts);
	}

	CMongoServerHost const &CMongoConnectionSettings::f_GetSingleHost() const
	{
		DMibCheck(m_Hosts.f_GetLen() == 1);
		return m_Hosts[0];
	}

	CMongoConnectionSettings CMongoConnectionSettings::f_ForConnectionString(NStr::CStr const &_ConnectionString) const
	{
		CMongoConnectionSettings ConnectionSettings = *this;
		ConnectionSettings.m_Hosts.f_Clear();

		for (auto HostPort : _ConnectionString.f_Split(","))
		{
			CMongoServerHost ServerHost;
			ServerHost.m_Host = fg_GetStrSep(HostPort, ":");
			if (HostPort.f_IsEmpty())
				ServerHost.m_Port = CMongoServerHost::mc_DefaultPort;
			else
				ServerHost.m_Port = HostPort.f_ToInt(CMongoServerHost::mc_DefaultPort);

			ConnectionSettings.m_Hosts.f_Insert(ServerHost);
		}

		return ConnectionSettings;
	}

	NWeb::NHTTP::CURL CMongoConnectionSettings::f_GetUrl(NStr::CStr const &_Database) const
	{
		NWeb::NHTTP::CURL Url;
		Url.f_SetScheme(m_bEnableSrv ? "mongodb+srv" : "mongodb");
		Url.f_SetHost(f_GetConnectionString(), true);
		if (_Database)
			Url.f_SetPath({_Database});
		NContainer::TCVector<NWeb::NHTTP::CURL::CQueryEntry> Query
			{
				{
					{"retryWrites", "true"}
					, {"w", "majority"}
				}
			}
		;

		if (m_bEnableSSL)
		{
			Url.f_SetUsername(m_UserName);

			Query.f_Insert({"authMechanism", "MONGODB-X509"});
			Query.f_Insert({"authSource", "$external"});
			Query.f_Insert({"tls", "true"});

			if (m_ClientCertificatePath)
				Query.f_Insert({"tlsCertificateKeyFile", m_ClientCertificatePath});

			if (m_CACertificatePath)
				Query.f_Insert({"tlsCAFile", m_CACertificatePath});
		}

		if (m_ReplicaSet)
			Query.f_Insert({"replicaSet", m_ReplicaSet});

		Url.f_SetQuery(Query);

		return Url;
	}

	NContainer::TCVector<NStr::CStr> CMongoConnectionSettings::f_GetToolParams(bool _bTlsSupported) const
	{
		NContainer::TCVector<NStr::CStr> Params;
		Params << NContainer::fg_CreateVector<NStr::CStr>
			(
				"--host"
				, f_GetConnectionString()
			)
		;

		if (m_bEnableSSL)
		{
			Params << NContainer::fg_CreateVector<NStr::CStr>
				(
					(_bTlsSupported ? "--tls" : "--ssl")
					, "--authenticationMechanism"
					, "MONGODB-X509"
					, "--authenticationDatabase"
					, "$external"
				)
			;

			if (!m_CACertificatePath.f_IsEmpty())
				Params.f_Insert({(_bTlsSupported ? "--tlsCAFile" : "--sslCAFile"), m_CACertificatePath});

			if (!m_ClientCertificatePath.f_IsEmpty())
				Params.f_Insert({(_bTlsSupported ? "--tlsCertificateKeyFile" : "--sslPEMKeyFile"), m_ClientCertificatePath});
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
			mongocxx::options::tls TLSOptions;

			TLSOptions.allow_invalid_certificates(false);

			if (_ConnectionSettings.m_CACertificatePath)
				TLSOptions.ca_file(_ConnectionSettings.m_CACertificatePath.f_GetStr());
			TLSOptions.pem_file(_ConnectionSettings.m_ClientCertificatePath.f_GetStr());

			Options.tls_opts(fg_Move(TLSOptions));

			return Options;
		}

		mongocxx::uri fg_GetConnectionURI(CMongoConnectionSettings const &_ConnectionSettings, NStr::CStr const &_DefaultDatabase)
		{
			NStr::CStr URI = _ConnectionSettings.f_GetUrl(_DefaultDatabase).f_Encode();

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

	NConcurrency::TCFuture<void> CMongoClientActor::fp_Destroy()
	{
		NConcurrency::TCPromise<void> Promise;

		auto &Internal = *mp_pInternal;
		if (Internal.m_pTailThread)
		{
			Internal.m_pTailThread->f_Stop(false);
#ifndef DPlatformFamily_Windows
			pthread_kill((pthread_t)Internal.m_pTailThread->f_GetThreadID(), SIGUSR2);
#else
			Internal.m_pConnection->abort();
#endif
			Internal.m_pTailThread->f_Stop(true);
			Internal.m_pTailThread.f_Clear();
			Internal.m_pConnection.f_Clear();
		}

		Promise.f_SetResult();
		return Promise.f_MoveFuture();
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

	NConcurrency::TCFuture<NConcurrency::CActorSubscription> CMongoClientActor::f_TailQuery
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
		NConcurrency::TCPromise<NConcurrency::CActorSubscription> Result;

		auto &Internal = *mp_pInternal;
		if (Internal.m_pTailThread)
			return Result <<= DMibErrorInstance("Tailing query already running");

		NStr::CStr Error = Internal.f_MakeSureConnected();
		if (!Error.f_IsEmpty())
			return Result <<= DMibErrorInstance(fg_Format("Failed to connect to MongoDB server: {}", Error));

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
						[WeakThis = fg_ThisActor(pThis).f_Weak(), this]
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
#ifndef DPlatformFamily_Windows
												pthread_kill((pthread_t)Internal.m_pTailThread->f_GetThreadID(), SIGUSR2);
#else
												Internal.m_pConnection->abort();
#endif
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

#ifndef DPlatformFamily_Windows
							auto SignalSubscription = NSys::fg_System_RegisterForThreadSignal
								(
									SIGUSR2
									, [&]
									{
										if (Internal.m_pConnection)
											Internal.m_pConnection->abort();
									}
								)
							;
#endif
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

		return Result.f_MoveFuture();
	}

	NConcurrency::TCFuture<NContainer::TCVector<NEncoding::CEJSON>> CMongoClientActor::f_Query
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
		NConcurrency::TCPromise<NContainer::TCVector<NEncoding::CEJSON>> Promise;

		auto &Internal = *mp_pInternal;
		if (Internal.m_pTailThread)
			return Promise <<= DMibErrorInstance("Tailing query already running");
		NStr::CStr Error = Internal.f_MakeSureConnected();
		if (!Error.f_IsEmpty())
			return Promise <<= DMibErrorInstance(fg_Format("Failed to connect to MongoDB server: {}", Error));

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

			return Promise <<= fg_Move(ToReturn);
		}
		catch (std::exception const &_Exception)
		{
			const ch8 *pError = "Unknown mongo error";
			if (_Exception.what())
				pError = _Exception.what();

			return Promise <<= DMibErrorInstance(NStr::fg_Format("Mongo query failed: {}", pError));
		}
	}

	NConcurrency::TCFuture<uint64> CMongoClientActor::f_Count
		(
			NStr::CStr const &_Collection
			, NEncoding::CEJSON const &_Query
			, uint32 _nToReturn
			, uint32 _nToSkip
		)
	{
		NConcurrency::TCPromise<uint64> Promise;

		auto &Internal = *mp_pInternal;
		if (Internal.m_pTailThread)
			return Promise <<= DMibErrorInstance("Tailing query already running");
		NStr::CStr Error = Internal.f_MakeSureConnected();
		if (!Error.f_IsEmpty())
			return Promise <<= DMibErrorInstance(fg_Format("Failed to connect to MongoDB server: {}", Error));

		mongocxx::options::count CountOptions;

		if (_nToSkip)
			CountOptions.skip(_nToSkip);

		if (_nToReturn)
			CountOptions.limit(_nToReturn);

		try
		{
			auto Collection = Internal.f_GetCollection(_Collection);

			uint64 Count = Collection.count_documents(fg_ToBSON(_Query), CountOptions);
			return Promise <<= Count;
		}
		catch (std::exception const &_Exception)
		{
			const ch8 *pError = "Unknown mongo error";
			if (_Exception.what())
				pError = _Exception.what();

			return Promise <<= DMibErrorInstance(NStr::fg_Format("MongoDB count failed: {}", pError));
		}
	}

	NConcurrency::TCFuture<void> CMongoClientActor::f_Insert(NStr::CStr const &_Collection, NEncoding::CEJSON const &_Document, EInsertOption _Options)
	{
		NConcurrency::TCPromise<void> Result;
		auto &Internal = *mp_pInternal;
		if (Internal.m_pTailThread)
			return Result <<= DMibErrorInstance("Tailing query already running");

		NStr::CStr Error = Internal.f_MakeSureConnected();
		if (!Error.f_IsEmpty())
			return Result <<= DMibErrorInstance(fg_Format("Failed to connect to MongoDB server: {}", Error));

		mongocxx::options::insert InsertOptions;
		if (_Options & EInsertOption_ContinueOnError)
			InsertOptions.ordered(false);
		else
			InsertOptions.ordered(true);

		try
		{
			auto Collection = Internal.f_GetCollection(_Collection);
			Collection.insert_one(fg_ToBSON(_Document), InsertOptions);
			return Result <<= g_Void;
		}
		catch (std::exception const &_Exception)
		{
			const ch8 *pError = "Unknown mongo error";
			if (_Exception.what())
				pError = _Exception.what();

			return Result <<= DMibErrorInstance(NStr::fg_Format("MongoDB insert failed: {}", pError));
		}
	}

	NConcurrency::TCFuture<void> CMongoClientActor::f_BatchInsert(NStr::CStr const &_Collection, NContainer::TCVector<NEncoding::CEJSON> const &_Documents, EInsertOption _Options)
	{
		NConcurrency::TCPromise<void> Promise;

		auto &Internal = *mp_pInternal;
		if (Internal.m_pTailThread)
			return Promise <<= DMibErrorInstance("Tailing query already running");

		NStr::CStr Error = Internal.f_MakeSureConnected();
		if (!Error.f_IsEmpty())
			return Promise <<= DMibErrorInstance(fg_Format("Failed to connect to MongoDB server: {}", Error));

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

			return Promise <<= g_Void;
		}
		catch (std::exception const &_Exception)
		{
			const ch8 *pError = "Unknown mongo error";
			if (_Exception.what())
				pError = _Exception.what();

			return Promise <<= DMibErrorInstance(NStr::fg_Format("MongoDB insert failed: {}", pError));
		}
	}

	auto CMongoClientActor::f_Update
		(
			NStr::CStr const &_Collection
			, NEncoding::CEJSON const &_Query
			, NEncoding::CEJSON const &_Update
			, EUpdateOption _Options
		)
		-> NConcurrency::TCFuture<CUpdateResult>
	{
		NConcurrency::TCPromise<CUpdateResult> Promise;

		auto &Internal = *mp_pInternal;
		if (Internal.m_pTailThread)
			return Promise <<= DMibErrorInstance("Tailing query already running");

		NStr::CStr Error = Internal.f_MakeSureConnected();
		if (!Error.f_IsEmpty())
			return Promise <<= DMibErrorInstance(fg_Format("Failed to connect to MongoDB server: {}", Error));

		mongocxx::options::update UpdateOptions;

		if (_Options & EUpdateOption_Upsert)
			UpdateOptions.upsert(true);

		try
		{
			auto Collection = Internal.f_GetCollection(_Collection);

			mongocxx::stdx::optional<mongocxx::result::update> UpdateResult;
			if (_Options & EUpdateOption_Multi)
				UpdateResult = Collection.update_one(fg_ToBSON(_Query), fg_ToBSON(_Update), UpdateOptions);
			else
				UpdateResult = Collection.update_many(fg_ToBSON(_Query), fg_ToBSON(_Update), UpdateOptions);

			if (!UpdateResult)
				return Promise <<= DMibErrorInstance("MongoDB update did not return any results");

			return Promise <<= CUpdateResult{UpdateResult->matched_count(), UpdateResult->modified_count()};
		}
		catch (std::exception const &_Exception)
		{
			const ch8 *pError = "Unknown mongo error";
			if (_Exception.what())
				pError = _Exception.what();

			return Promise <<= DMibErrorInstance(NStr::fg_Format("MongoDB update failed: {}", pError));
		}
	}

	NConcurrency::TCFuture<void> CMongoClientActor::f_Remove(NStr::CStr const &_Collection, NEncoding::CEJSON const &_Query, ERemoveOption _Options)
	{
		NConcurrency::TCPromise<void> Result;
		auto &Internal = *mp_pInternal;
		if (Internal.m_pTailThread)
			return Result <<= DMibErrorInstance("Tailing query already running");

		NStr::CStr Error = Internal.f_MakeSureConnected();
		if (!Error.f_IsEmpty())
			return Result <<= DMibErrorInstance(fg_Format("Failed to connect to MongoDB server: {}", Error));

		try
		{
			auto Collection = Internal.f_GetCollection(_Collection);

			if (_Options & ERemoveOption_JustOne)
				Collection.delete_one(fg_ToBSON(_Query));
			else
				Collection.delete_many(fg_ToBSON(_Query));

			return Result <<= g_Void;
		}
		catch (std::exception const &_Exception)
		{
			const ch8 *pError = "Unknown mongo error";
			if (_Exception.what())
				pError = _Exception.what();

			return Result <<= DMibErrorInstance(NStr::fg_Format("MongoDB remove failed: {}", pError));
		}
	}
}
