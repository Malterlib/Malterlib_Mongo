// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>
#include <Mib/Concurrency/ActorSubscription>

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
#include <mongocxx/uri.hpp>

#include <mongocxx/exception/bulk_write_exception.hpp>
#include <mongocxx/exception/error_code.hpp>
#include <mongocxx/exception/logic_error.hpp>
#include <mongocxx/exception/operation_exception.hpp>
#include <mongocxx/exception/server_error_code.hpp>

#include <mongoc/mongoc.h>

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
	DMibImpErrorClassImplement(CExceptionMongo);

	static NException::CExceptionPointer fg_MongoExceptionToMalterlibException(NException::CExceptionPointer const &_pException, NStr::CStr const &_Description)
	{
		using namespace NStr;
		CMongoErrorData ErrorData;
		CStr ExceptionString;

		bool bHandled = NException::fg_VisitException
			<
				mongocxx::operation_exception
				, mongocxx::exception
				, std::exception
			>
			(
				_pException
				, [&]<typename tf_CException>(tf_CException &&_Exception)
				{
					using CExceptionType = NTraits::TCRemoveReferenceAndQualifiers<tf_CException>;

					if constexpr (NTraits::cIsSame<CExceptionType, mongocxx::operation_exception>)
					{
						ExceptionString = _Exception.what();
						if (auto ServerError = _Exception.raw_server_error())
							ErrorData.m_RawServerError = fg_FromBSON(ServerError->view());

						if (_Exception.code().category() == mongocxx::server_error_category())
							ErrorData.m_ErrorCode = (mongoc_error_code_t)_Exception.code().value();
					}
					else if constexpr (NTraits::cIsSame<CExceptionType, mongocxx::exception>)
					{
						ExceptionString = _Exception.what();
						if (_Exception.code().category() == mongocxx::server_error_category())
							ErrorData.m_ErrorCode = (mongoc_error_code_t)_Exception.code().value();
					}
					else if constexpr (NTraits::cIsSame<CExceptionType, std::exception>)
						ExceptionString = _Exception.what();
					else
						DMibFastCheck(false);
				}
			)
		;

		if (!bHandled)
			return DMibErrorInstanceMongo(gc_Str<"Unknown MongoDB error">.m_Str, fg_Move(ErrorData));


		return DMibErrorInstanceMongo("MongoDB {} failed: {}"_f << _Description << ExceptionString, fg_Move(ErrorData));
	}

	NStr::CStr CMongoErrorData::f_GetErrorCodeDescription() const
	{
		if (!m_ErrorCode)
			return {};

		switch (*m_ErrorCode)
		{
		case MONGOC_ERROR_STREAM_NAME_RESOLUTION: return NStr::gc_Str<"MONGOC_ERROR_STREAM_NAME_RESOLUTION">;
		case MONGOC_ERROR_STREAM_SOCKET: return NStr::gc_Str<"MONGOC_ERROR_STREAM_SOCKET">;
		case MONGOC_ERROR_STREAM_CONNECT: return NStr::gc_Str<"MONGOC_ERROR_STREAM_CONNECT">;
		case MONGOC_ERROR_STREAM_NOT_ESTABLISHED: return NStr::gc_Str<"MONGOC_ERROR_STREAM_NOT_ESTABLISHED">;
		case MONGOC_ERROR_SERVER_SELECTION_FAILURE: return NStr::gc_Str<"MONGOC_ERROR_SERVER_SELECTION_FAILURE">;
		case MONGOC_ERROR_STREAM_INVALID_TYPE: return NStr::gc_Str<"MONGOC_ERROR_STREAM_INVALID_TYPE">;
		case MONGOC_ERROR_STREAM_INVALID_STATE: return NStr::gc_Str<"MONGOC_ERROR_STREAM_INVALID_STATE">;
		case MONGOC_ERROR_CLIENT_NOT_READY: return NStr::gc_Str<"MONGOC_ERROR_CLIENT_NOT_READY">;
		case MONGOC_ERROR_CLIENT_TOO_BIG: return NStr::gc_Str<"MONGOC_ERROR_CLIENT_TOO_BIG">;
		case MONGOC_ERROR_CLIENT_TOO_SMALL: return NStr::gc_Str<"MONGOC_ERROR_CLIENT_TOO_SMALL">;
		case MONGOC_ERROR_CLIENT_GETNONCE: return NStr::gc_Str<"MONGOC_ERROR_CLIENT_GETNONCE">;
		case MONGOC_ERROR_CLIENT_AUTHENTICATE: return NStr::gc_Str<"MONGOC_ERROR_CLIENT_AUTHENTICATE">;
		case MONGOC_ERROR_CLIENT_NO_ACCEPTABLE_PEER: return NStr::gc_Str<"MONGOC_ERROR_CLIENT_NO_ACCEPTABLE_PEER">;
		case MONGOC_ERROR_CLIENT_IN_EXHAUST: return NStr::gc_Str<"MONGOC_ERROR_CLIENT_IN_EXHAUST">;
		case MONGOC_ERROR_PROTOCOL_INVALID_REPLY: return NStr::gc_Str<"MONGOC_ERROR_PROTOCOL_INVALID_REPLY">;
		case MONGOC_ERROR_PROTOCOL_BAD_WIRE_VERSION: return NStr::gc_Str<"MONGOC_ERROR_PROTOCOL_BAD_WIRE_VERSION">;
		case MONGOC_ERROR_CURSOR_INVALID_CURSOR: return NStr::gc_Str<"MONGOC_ERROR_CURSOR_INVALID_CURSOR">;
		case MONGOC_ERROR_QUERY_FAILURE: return NStr::gc_Str<"MONGOC_ERROR_QUERY_FAILURE">;
		case MONGOC_ERROR_BSON_INVALID: return NStr::gc_Str<"MONGOC_ERROR_BSON_INVALID">;
		case MONGOC_ERROR_MATCHER_INVALID: return NStr::gc_Str<"MONGOC_ERROR_MATCHER_INVALID">;
		case MONGOC_ERROR_NAMESPACE_INVALID: return NStr::gc_Str<"MONGOC_ERROR_NAMESPACE_INVALID">;
		case MONGOC_ERROR_NAMESPACE_INVALID_FILTER_TYPE: return NStr::gc_Str<"MONGOC_ERROR_NAMESPACE_INVALID_FILTER_TYPE">;
		case MONGOC_ERROR_COMMAND_INVALID_ARG: return NStr::gc_Str<"MONGOC_ERROR_COMMAND_INVALID_ARG">;
		case MONGOC_ERROR_COLLECTION_INSERT_FAILED: return NStr::gc_Str<"MONGOC_ERROR_COLLECTION_INSERT_FAILED">;
		case MONGOC_ERROR_COLLECTION_UPDATE_FAILED: return NStr::gc_Str<"MONGOC_ERROR_COLLECTION_UPDATE_FAILED">;
		case MONGOC_ERROR_COLLECTION_DELETE_FAILED: return NStr::gc_Str<"MONGOC_ERROR_COLLECTION_DELETE_FAILED">;
		case MONGOC_ERROR_COLLECTION_DOES_NOT_EXIST: return NStr::gc_Str<"MONGOC_ERROR_COLLECTION_DOES_NOT_EXIST">;
		case MONGOC_ERROR_GRIDFS_INVALID_FILENAME: return NStr::gc_Str<"MONGOC_ERROR_GRIDFS_INVALID_FILENAME">;
		case MONGOC_ERROR_SCRAM_NOT_DONE: return NStr::gc_Str<"MONGOC_ERROR_SCRAM_NOT_DONE">;
		case MONGOC_ERROR_SCRAM_PROTOCOL_ERROR: return NStr::gc_Str<"MONGOC_ERROR_SCRAM_PROTOCOL_ERROR">;
		case MONGOC_ERROR_QUERY_COMMAND_NOT_FOUND: return NStr::gc_Str<"MONGOC_ERROR_QUERY_COMMAND_NOT_FOUND">;
		case MONGOC_ERROR_QUERY_NOT_TAILABLE: return NStr::gc_Str<"MONGOC_ERROR_QUERY_NOT_TAILABLE">;
		case MONGOC_ERROR_SERVER_SELECTION_BAD_WIRE_VERSION: return NStr::gc_Str<"MONGOC_ERROR_SERVER_SELECTION_BAD_WIRE_VERSION">;
		case MONGOC_ERROR_SERVER_SELECTION_INVALID_ID: return NStr::gc_Str<"MONGOC_ERROR_SERVER_SELECTION_INVALID_ID">;
		case MONGOC_ERROR_GRIDFS_CHUNK_MISSING: return NStr::gc_Str<"MONGOC_ERROR_GRIDFS_CHUNK_MISSING">;
		case MONGOC_ERROR_GRIDFS_PROTOCOL_ERROR: return NStr::gc_Str<"MONGOC_ERROR_GRIDFS_PROTOCOL_ERROR">;
		//case MONGOC_ERROR_PROTOCOL_ERROR: return NStr::gc_Str<"MONGOC_ERROR_PROTOCOL_ERROR">;
		case MONGOC_ERROR_WRITE_CONCERN_ERROR: return NStr::gc_Str<"MONGOC_ERROR_WRITE_CONCERN_ERROR">;
		case MONGOC_ERROR_DUPLICATE_KEY: return NStr::gc_Str<"MONGOC_ERROR_DUPLICATE_KEY">;
		case MONGOC_ERROR_MAX_TIME_MS_EXPIRED: return NStr::gc_Str<"MONGOC_ERROR_MAX_TIME_MS_EXPIRED">;
		case MONGOC_ERROR_CHANGE_STREAM_NO_RESUME_TOKEN: return NStr::gc_Str<"MONGOC_ERROR_CHANGE_STREAM_NO_RESUME_TOKEN">;
		case MONGOC_ERROR_CLIENT_SESSION_FAILURE: return NStr::gc_Str<"MONGOC_ERROR_CLIENT_SESSION_FAILURE">;
		case MONGOC_ERROR_TRANSACTION_INVALID_STATE: return NStr::gc_Str<"MONGOC_ERROR_TRANSACTION_INVALID_STATE">;
		case MONGOC_ERROR_GRIDFS_CORRUPT: return NStr::gc_Str<"MONGOC_ERROR_GRIDFS_CORRUPT">;
		case MONGOC_ERROR_GRIDFS_BUCKET_FILE_NOT_FOUND: return NStr::gc_Str<"MONGOC_ERROR_GRIDFS_BUCKET_FILE_NOT_FOUND">;
		case MONGOC_ERROR_GRIDFS_BUCKET_STREAM: return NStr::gc_Str<"MONGOC_ERROR_GRIDFS_BUCKET_STREAM">;
		case MONGOC_ERROR_CLIENT_INVALID_ENCRYPTION_STATE: return NStr::gc_Str<"MONGOC_ERROR_CLIENT_INVALID_ENCRYPTION_STATE">;
		case MONGOC_ERROR_CLIENT_INVALID_ENCRYPTION_ARG: return NStr::gc_Str<"MONGOC_ERROR_CLIENT_INVALID_ENCRYPTION_ARG">;
		//case MONGOC_ERROR_CLIENT_API_ALREADY_SET: return NStr::gc_Str<"MONGOC_ERROR_CLIENT_API_ALREADY_SET">;
		case MONGOC_ERROR_CLIENT_API_FROM_POOL: return NStr::gc_Str<"MONGOC_ERROR_CLIENT_API_FROM_POOL">;
		case MONGOC_ERROR_POOL_API_ALREADY_SET: return NStr::gc_Str<"MONGOC_ERROR_POOL_API_ALREADY_SET">;
		case MONGOC_ERROR_POOL_API_TOO_LATE: return NStr::gc_Str<"MONGOC_ERROR_POOL_API_TOO_LATE">;
		case MONGOC_ERROR_CLIENT_INVALID_LOAD_BALANCER: return NStr::gc_Str<"MONGOC_ERROR_CLIENT_INVALID_LOAD_BALANCER">;
		//case MONGOC_ERROR_KMS_SERVER_HTTP: return NStr::gc_Str<"MONGOC_ERROR_KMS_SERVER_HTTP">;
		case MONGOC_ERROR_KMS_SERVER_BAD_JSON: return NStr::gc_Str<"MONGOC_ERROR_KMS_SERVER_BAD_JSON">;
		}

		return {};
	}

	bool CMongoErrorData::f_IsRecoverableConnectionError() const
	{
		if (!m_ErrorCode)
			return false;

		switch (*m_ErrorCode)
		{
		case MONGOC_ERROR_STREAM_NAME_RESOLUTION:
		case MONGOC_ERROR_STREAM_SOCKET:
		case MONGOC_ERROR_STREAM_CONNECT:
		case MONGOC_ERROR_STREAM_NOT_ESTABLISHED:
		case MONGOC_ERROR_SERVER_SELECTION_FAILURE:
			return true;

		case MONGOC_ERROR_STREAM_INVALID_TYPE:
		case MONGOC_ERROR_STREAM_INVALID_STATE:
		case MONGOC_ERROR_CLIENT_NOT_READY:
		case MONGOC_ERROR_CLIENT_TOO_BIG:
		case MONGOC_ERROR_CLIENT_TOO_SMALL:
		case MONGOC_ERROR_CLIENT_GETNONCE:
		case MONGOC_ERROR_CLIENT_AUTHENTICATE:
		case MONGOC_ERROR_CLIENT_NO_ACCEPTABLE_PEER:
		case MONGOC_ERROR_CLIENT_IN_EXHAUST:
		case MONGOC_ERROR_PROTOCOL_INVALID_REPLY:
		case MONGOC_ERROR_PROTOCOL_BAD_WIRE_VERSION:
		case MONGOC_ERROR_CURSOR_INVALID_CURSOR:
		case MONGOC_ERROR_QUERY_FAILURE:
		case MONGOC_ERROR_BSON_INVALID:
		case MONGOC_ERROR_MATCHER_INVALID:
		case MONGOC_ERROR_NAMESPACE_INVALID:
		case MONGOC_ERROR_NAMESPACE_INVALID_FILTER_TYPE:
		case MONGOC_ERROR_COMMAND_INVALID_ARG:
		case MONGOC_ERROR_COLLECTION_INSERT_FAILED:
		case MONGOC_ERROR_COLLECTION_UPDATE_FAILED:
		case MONGOC_ERROR_COLLECTION_DELETE_FAILED:
		case MONGOC_ERROR_COLLECTION_DOES_NOT_EXIST:
		case MONGOC_ERROR_GRIDFS_INVALID_FILENAME:
		case MONGOC_ERROR_SCRAM_NOT_DONE:
		case MONGOC_ERROR_SCRAM_PROTOCOL_ERROR:
		case MONGOC_ERROR_QUERY_COMMAND_NOT_FOUND:
		case MONGOC_ERROR_QUERY_NOT_TAILABLE:
		case MONGOC_ERROR_SERVER_SELECTION_BAD_WIRE_VERSION:
		case MONGOC_ERROR_SERVER_SELECTION_INVALID_ID:
		case MONGOC_ERROR_GRIDFS_CHUNK_MISSING:
		case MONGOC_ERROR_GRIDFS_PROTOCOL_ERROR:
		//case MONGOC_ERROR_PROTOCOL_ERROR:
		case MONGOC_ERROR_WRITE_CONCERN_ERROR:
		case MONGOC_ERROR_DUPLICATE_KEY:
		case MONGOC_ERROR_MAX_TIME_MS_EXPIRED:
		case MONGOC_ERROR_CHANGE_STREAM_NO_RESUME_TOKEN:
		case MONGOC_ERROR_CLIENT_SESSION_FAILURE:
		case MONGOC_ERROR_TRANSACTION_INVALID_STATE:
		case MONGOC_ERROR_GRIDFS_CORRUPT:
		case MONGOC_ERROR_GRIDFS_BUCKET_FILE_NOT_FOUND:
		case MONGOC_ERROR_GRIDFS_BUCKET_STREAM:
		case MONGOC_ERROR_CLIENT_INVALID_ENCRYPTION_STATE:
		case MONGOC_ERROR_CLIENT_INVALID_ENCRYPTION_ARG:
		//case MONGOC_ERROR_CLIENT_API_ALREADY_SET:
		case MONGOC_ERROR_CLIENT_API_FROM_POOL:
		case MONGOC_ERROR_POOL_API_ALREADY_SET:
		case MONGOC_ERROR_POOL_API_TOO_LATE:
		case MONGOC_ERROR_CLIENT_INVALID_LOAD_BALANCER:
		//case MONGOC_ERROR_KMS_SERVER_HTTP:
		case MONGOC_ERROR_KMS_SERVER_BAD_JSON:
			return false;
		}

		return false;
	}

	NStr::CStr CMongoErrorData::f_GetCodeName() const
	{
		if (!m_RawServerError.f_IsValid())
			return {};

		if (auto *pValue = m_RawServerError.f_GetMember("codeName", NEncoding::EJsonType_String))
			return pValue->f_String();

		return {};
	}

	NStorage::TCOptional<CMongoErrorData> CMongoErrorData::fs_FromException(NException::CExceptionPointer const &_pException)
	{
		NStorage::TCOptional<CMongoErrorData> Return;

		NException::fg_VisitException<CExceptionMongo>
			(
				_pException
				, [&](CExceptionMongo const &_Exception)
				{
					Return = _Exception.f_GetSpecific();
				}
			)
		;

		return Return;

	}

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

		if (m_bDirectConnection)
			Query.f_Insert({"directConnection", "true"});

		if (m_ReadPreference)
			Query.f_Insert({"readPreference", m_ReadPreference});

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

		NException::CExceptionPointer f_MakeSureConnected()
		{
			if (m_pConnection)
				return {};

			try
			{
				m_pConnection = fg_Construct(fg_GetConnectionURI(m_ConnectionSettings, m_DefaultDatabase), fg_GetConnectionOptions(m_ConnectionSettings, m_DefaultDatabase));

				return {};
			}
			catch (std::exception const &)
			{
				return fg_MongoExceptionToMalterlibException(NException::fg_CurrentException(), NStr::gc_Str<"connect">);
			}
		}

		decltype(auto) f_GetDatabase(NStr::CStr _Database) const
		{
			NStr::CStr Database = _Database;

			if (!Database)
				Database = m_DefaultDatabase;

			decltype(auto) DatabaseReturn = (*m_pConnection)[Database.f_GetStr()];

			return DatabaseReturn;
		}

		decltype(auto) f_GetCollection(NStr::CStr _Collection) const
		{
			NStr::CStr Database;
			NStr::CStr Collection;

			if (_Collection.f_FindChar('.') >= 0)
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
		fg_ThisActor(this)(&CMongoClientActor::fp_ConnectToServer).f_DiscardResult();
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

		co_return {};
	}

	namespace
	{
		mongocxx::options::find fg_QueryOptions
			(
				CMongoClientActor::EQueryOption _Options
				, auto const &_Fields
				, NStorage::TCUniquePointer<NEncoding::CEJsonOrdered> const &_pOrder
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

			if (_Fields)
				Options.projection(fg_ToBSON(*_Fields));

			if (_pOrder)
				Options.sort(fg_ToBSON(*_pOrder));

			return Options;
		}
	}

	NConcurrency::TCFuture<NConcurrency::CActorSubscription> CMongoClientActor::f_TailQuery
		(
			CTailQueryParams _Params
			, NConcurrency::TCActorFunctorWeak<NConcurrency::TCFuture<void> (NEncoding::CEJsonOrdered _Result)> _fOnResult
		)
	{
		auto &Internal = *mp_pInternal;
		if (Internal.m_pTailThread)
			co_return DMibErrorInstance("Tailing query already running");

		if (auto pError = Internal.f_MakeSureConnected())
			co_return fg_Move(pError);

		NStorage::TCUniquePointer<NEncoding::CEJsonOrdered> pOrder = fg_Construct();
		(*pOrder)["$natural"] = -1;

		NStorage::TCUniquePointer<NEncoding::CEJsonOrdered> pFields = fg_Construct();
		(*pFields)[_Params.m_OrderBy] = 1;

		auto StartQuery = _Params.m_Query;
		if (_Params.m_StartQuery)
		{
			if (_Params.m_StartQuery->f_Type() != NEncoding::EJsonType_Object)
				co_return DMibErrorInstance("Expected m_StartQuery to be an object");

			for (auto &Entry : _Params.m_StartQuery->f_Object())
				StartQuery[Entry.f_Name()] = Entry.f_Value();
		}

		auto Collection = _Params.m_Collection;

		auto QueryResult = co_await fg_ThisActor(this)(&CMongoClientActor::f_Query, Collection, StartQuery, 1, 0, fg_Move(pFields), fg_Move(pOrder), EQueryOption_None);

		NEncoding::CEJsonOrdered GetOnwardsFromValue;

		if (!QueryResult.f_IsEmpty() && QueryResult.f_GetFirst().f_IsValid())
			GetOnwardsFromValue = fg_Move(QueryResult.f_GetFirst()[_Params.m_OrderBy]);
		else if (_Params.m_StartQuery)
			co_return DMibErrorInstance("Start query didn't return any document");

		NStorage::TCSharedPointer<NConcurrency::TCActorFunctorWeak<NConcurrency::TCFuture<void> (NEncoding::CEJsonOrdered _Data)>> pOnDataCallback
			= fg_Construct(fg_Move(_fOnResult))
		;

		auto Subscription = NConcurrency::g_ActorSubscription / [this]
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
		;

		NConcurrency::TCPromiseFuturePair<NConcurrency::CActorSubscription> Result;

		Internal.m_pTailThread = NThread::CThreadObject::fs_StartThread
			(
				[
					this
					, _Params = fg_Move(_Params)
					, pOnDataCallback = fg_Move(pOnDataCallback)
					, Subscription = fg_Move(Subscription)
					, Result = fg_Move(Result.m_Promise)
					, GetOnwardsFromValue
				]
				(NThread::CThreadObject *_pThread) mutable -> aint
				{
					auto &Internal = *mp_pInternal;

					NEncoding::CEJsonOrdered Order;

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
					auto UserQuery = _Params.m_Query;

					Order["$natural"] = 1;

					CMongoClientActor::EQueryOption Options = _Params.m_Options
						| CMongoClientActor::EQueryOption_AwaitData
						| CMongoClientActor::EQueryOption_CursorTailable
						| CMongoClientActor::EQueryOption_NoCursorTimeout
					;

					if (GetOnwardsFromValue.f_IsValid())
					{
						UserQuery[_Params.m_OrderBy]["$gt"] = GetOnwardsFromValue;
						if (_Params.m_Collection == "local.oplog.rs" && _Params.m_OrderBy == "ts")
							Options |= CMongoClientActor::EQueryOption_OplogReplay;
					}

					auto QueryOptions = fg_QueryOptions(Options, _Params.m_Fields, nullptr);
					QueryOptions.sort(fg_ToBSON(Order));

					bool bDoneRegistration = false;

					while (_pThread->f_GetState() != NThread::EThreadState_EventWantQuit)
					{
						try
						{
							auto Collection = Internal.f_GetCollection(_Params.m_Collection);

							auto Cursor = Collection.find(fg_ToBSON(UserQuery), QueryOptions);

							if (!bDoneRegistration)
							{
								bDoneRegistration = true;
								Result.f_SetResult(fg_Move(Subscription));
							}

							while (_pThread->f_GetState() != NThread::EThreadState_EventWantQuit)
							{
								for (auto &&Document : Cursor)
								{
									auto Data = fg_FromBSON(Document);
									if (auto pValue = Data.f_Object().f_GetMember(_Params.m_OrderBy))
										UserQuery[_Params.m_OrderBy]["$gt"] = *pValue;

									(*pOnDataCallback)(fg_Move(Data)).f_DiscardResult();
								}
							}
						}
						catch (std::exception const &)
						{
							if (_pThread->f_GetState() == NThread::EThreadState_EventWantQuit)
								return 0;

							NEncoding::CEJsonOrdered Error;
							Error["error"] = NException::fg_ExceptionString(fg_MongoExceptionToMalterlibException(NException::fg_CurrentException(), NStr::gc_Str<"tail oplog">));

							(*pOnDataCallback)(fg_Move(Error)).f_DiscardResult();
						}
					}

					return 0;
				}
				, "Mongo client tailing thread"
			)
		;

		co_return co_await fg_Move(Result.m_Future);
	}

	NConcurrency::TCFuture<NContainer::TCVector<NEncoding::CEJsonOrdered>> CMongoClientActor::f_Query
		(
			NStr::CStr _Collection
			, NEncoding::CEJsonOrdered _Query
			, uint32 _nToReturn
			, uint32 _nToSkip
			, NStorage::TCUniquePointer<NEncoding::CEJsonOrdered> _pFields
			, NStorage::TCUniquePointer<NEncoding::CEJsonOrdered> _pOrder
			, EQueryOption _Options
		)
	{
		auto &Internal = *mp_pInternal;
		if (Internal.m_pTailThread)
			co_return DMibErrorInstance("Tailing query already running");

		if (auto pError = Internal.f_MakeSureConnected())
			co_return fg_Move(pError);

		auto QueryOptions = fg_QueryOptions(_Options, _pFields, _pOrder);

		if (_nToSkip)
			QueryOptions.skip(_nToSkip);

		if (_nToReturn)
			QueryOptions.limit(_nToReturn);

		try
		{
			auto Collection = Internal.f_GetCollection(_Collection);

			auto Cursor = Collection.find(fg_ToBSON(_Query), QueryOptions);

			NContainer::TCVector<NEncoding::CEJsonOrdered> ToReturn;
			for (auto &&Document : Cursor)
			   ToReturn.f_Insert(fg_FromBSON(Document));

			co_return fg_Move(ToReturn);
		}
		catch (std::exception const &)
		{
			co_return fg_MongoExceptionToMalterlibException(NException::fg_CurrentException(), NStr::gc_Str<"query">);
		}
	}

	NConcurrency::TCFuture<NEncoding::CEJsonOrdered> CMongoClientActor::f_RunCommand
		(
			NStr::CStr _Database
			, NEncoding::CEJsonOrdered _Command
		)
	{
		auto &Internal = *mp_pInternal;
		if (Internal.m_pTailThread)
			co_return DMibErrorInstance("Tailing query already running");

		if (auto pError = Internal.f_MakeSureConnected())
			co_return fg_Move(pError);

		try
		{
			auto Database = Internal.f_GetDatabase(_Database);

			auto ResultDocument = Database.run_command(fg_ToBSON(_Command));

			NEncoding::CEJsonOrdered ToReturn = fg_FromBSON(fg_Move(ResultDocument));

			co_return fg_Move(ToReturn);
		}
		catch (std::exception const &)
		{
			co_return fg_MongoExceptionToMalterlibException(NException::fg_CurrentException(), NStr::gc_Str<"run command">);
		}
	}

	NConcurrency::TCFuture<uint64> CMongoClientActor::f_Count
		(
			NStr::CStr _Collection
			, NEncoding::CEJsonOrdered _Query
			, uint32 _nToReturn
			, uint32 _nToSkip
		)
	{
		auto &Internal = *mp_pInternal;
		if (Internal.m_pTailThread)
			co_return DMibErrorInstance("Tailing query already running");

		if (auto pError = Internal.f_MakeSureConnected())
			co_return fg_Move(pError);

		mongocxx::options::count CountOptions;

		if (_nToSkip)
			CountOptions.skip(_nToSkip);

		if (_nToReturn)
			CountOptions.limit(_nToReturn);

		try
		{
			auto Collection = Internal.f_GetCollection(_Collection);

			uint64 Count = Collection.count_documents(fg_ToBSON(_Query), CountOptions);
			co_return Count;
		}
		catch (std::exception const &)
		{
			co_return fg_MongoExceptionToMalterlibException(NException::fg_CurrentException(), NStr::gc_Str<"count">);
		}
	}

	NConcurrency::TCFuture<void> CMongoClientActor::f_Insert(NStr::CStr _Collection, NEncoding::CEJsonOrdered _Document, EInsertOption _Options)
	{
		auto &Internal = *mp_pInternal;
		if (Internal.m_pTailThread)
			co_return DMibErrorInstance("Tailing query already running");

		if (auto pError = Internal.f_MakeSureConnected())
			co_return fg_Move(pError);

		mongocxx::options::insert InsertOptions;
		if (_Options & EInsertOption_ContinueOnError)
			InsertOptions.ordered(false);
		else
			InsertOptions.ordered(true);

		try
		{
			auto Collection = Internal.f_GetCollection(_Collection);
			Collection.insert_one(fg_ToBSON(_Document), InsertOptions);
			co_return {};
		}
		catch (std::exception const &)
		{
			co_return fg_MongoExceptionToMalterlibException(NException::fg_CurrentException(), NStr::gc_Str<"insert">);
		}
	}

	NConcurrency::TCFuture<void> CMongoClientActor::f_BatchInsert(NStr::CStr _Collection, NContainer::TCVector<NEncoding::CEJsonOrdered> _Documents, EInsertOption _Options)
	{
		auto &Internal = *mp_pInternal;
		if (Internal.m_pTailThread)
			co_return DMibErrorInstance("Tailing query already running");

		if (auto pError = Internal.f_MakeSureConnected())
			co_return fg_Move(pError);

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

			co_return {};
		}
		catch (std::exception const &)
		{
			co_return fg_MongoExceptionToMalterlibException(NException::fg_CurrentException(), NStr::gc_Str<"batch insert">);
		}
	}

	auto CMongoClientActor::f_Update
		(
			NStr::CStr _Collection
			, NEncoding::CEJsonOrdered _Query
			, NEncoding::CEJsonOrdered _Update
			, EUpdateOption _Options
		)
		-> NConcurrency::TCFuture<CUpdateResult>
	{
		auto &Internal = *mp_pInternal;
		if (Internal.m_pTailThread)
			co_return DMibErrorInstance("Tailing query already running");

		if (auto pError = Internal.f_MakeSureConnected())
			co_return fg_Move(pError);

		mongocxx::options::update UpdateOptions;

		if (_Options & EUpdateOption_Upsert)
			UpdateOptions.upsert(true);

		try
		{
			auto Collection = Internal.f_GetCollection(_Collection);

			bsoncxx::v_noabi::stdx::optional<mongocxx::result::update> UpdateResult;
			if (_Options & EUpdateOption_Multi)
				UpdateResult = Collection.update_one(fg_ToBSON(_Query), fg_ToBSON(_Update), UpdateOptions);
			else
				UpdateResult = Collection.update_many(fg_ToBSON(_Query), fg_ToBSON(_Update), UpdateOptions);

			if (!UpdateResult)
				co_return DMibErrorInstance("MongoDB update did not return any results");

			co_return CUpdateResult{UpdateResult->matched_count(), UpdateResult->modified_count()};
		}
		catch (std::exception const &)
		{
			co_return fg_MongoExceptionToMalterlibException(NException::fg_CurrentException(), NStr::gc_Str<"update">);
		}
	}

	NConcurrency::TCFuture<void> CMongoClientActor::f_Remove(NStr::CStr _Collection, NEncoding::CEJsonOrdered _Query, ERemoveOption _Options)
	{
		auto &Internal = *mp_pInternal;
		if (Internal.m_pTailThread)
			co_return DMibErrorInstance("Tailing query already running");

		if (auto pError = Internal.f_MakeSureConnected())
			co_return fg_Move(pError);

		try
		{
			auto Collection = Internal.f_GetCollection(_Collection);

			if (_Options & ERemoveOption_JustOne)
				Collection.delete_one(fg_ToBSON(_Query));
			else
				Collection.delete_many(fg_ToBSON(_Query));

			co_return {};
		}
		catch (std::exception const &)
		{
			co_return fg_MongoExceptionToMalterlibException(NException::fg_CurrentException(), NStr::gc_Str<"remove">);
		}
	}

	CMongoClientRetryState::CMongoClientRetryState(CMongoConnectionSettings const &_ConnectionSettings, fp64 _Timeout)
		: m_ConnectionSettings(_ConnectionSettings)
		, m_Timeout(_Timeout)
	{
	}

	NConcurrency::TCFuture<void> CMongoClientRetryState::f_Destroy()
	{
		return fg_Move(m_MongoClient).f_Destroy();
	}
}
