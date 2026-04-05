// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Encoding/EJson>
#include <Mib/Web/HTTP/URL>
#include <Mib/Concurrency/ActorFunctorWeak>

namespace NMib::NMongo
{
	struct CMongoErrorData
	{
		template <typename tf_CStream>
		void f_Stream(tf_CStream &_Stream);

		bool f_IsRecoverableConnectionError() const;
		NStr::CStr f_GetErrorCodeDescription() const;
		NStr::CStr f_GetCodeName() const;

		static NStorage::TCOptional<CMongoErrorData> fs_FromException(NException::CExceptionPointer const &_pException);

		enum : uint32
		{
			EProtocolVersion_Min = 0x101
			, EProtocolVersion_Current = 0x101
		};

		NEncoding::CEJsonOrdered m_RawServerError;
		NStorage::TCOptional<uint32> m_ErrorCode; // mongoc_error_code_t
	};

	DMibImpErrorSpecificClassDefine(CExceptionMongo, NMib::NException::CException, CMongoErrorData);

#	define DMibErrorMongo(d_Description, d_Specific) DMibImpErrorSpecific(NMib::NMongo::CExceptionMongo, d_Description, d_Specific)
#	define DMibErrorInstanceMongo(d_Description, d_Specific) DMibImpExceptionInstanceSpecific(NMib::NMongo::CExceptionMongo, d_Description, d_Specific)

	struct CMongoServerHost
	{
		auto operator <=> (CMongoServerHost const &_Right) const noexcept = default;

		template <typename tf_CStr>
		void f_Format(tf_CStr &o_Str) const;

		static constexpr uint16 mc_DefaultPort = 27017;

		NStr::CStr m_Host = "localhost";
		uint16 m_Port = mc_DefaultPort;
	};

	struct CMongoConnectionSettings
	{
		bool f_Compatible(CMongoConnectionSettings const &_Settings) const;
		NContainer::TCVector<NStr::CStr> f_GetToolParams(bool _bTlsSupported) const;
		NStr::CStr f_GetConnectionString() const;
		static NStr::CStr fs_GetConnectionString(NContainer::TCVector<CMongoServerHost> const &_Hosts);
		CMongoConnectionSettings f_ForConnectionString(NStr::CStr const &_ConnectionString) const;
		NWeb::NHTTP::CURL f_GetUrl(NStr::CStr const &_Database) const;
		CMongoServerHost const &f_GetSingleHost() const;

		NContainer::TCVector<CMongoServerHost> m_Hosts = {{"localhost", CMongoServerHost::mc_DefaultPort}};

		// Needs to be the same to be compatible
		NStr::CStr m_CACertificatePath;
		NStr::CStr m_ClientCertificatePath;
		NStr::CStr m_UserName;
		NStr::CStr m_ReplicaSet;
		NStr::CStr m_ReadPreference;
		bool m_bEnableSSL = false;
		bool m_bEnableSrv = false;
		bool m_bDirectConnection = false;
	};

	class CMongoClientActor;

	struct CMongoClientRetryState
	{
		CMongoClientRetryState(CMongoConnectionSettings const &_ConnectionSettings, fp64 _Timeout = 60.0);

		NConcurrency::TCFuture<void> f_Destroy();

		CMongoConnectionSettings m_ConnectionSettings;
		NConcurrency::TCActor<CMongoClientActor> m_MongoClient;
		fp64 m_Timeout = 60.0;
	};

	class CMongoClientActor : public NConcurrency::CActor
	{
	public:
		using CActorHolder = NConcurrency::CSeparateThreadActorHolder;

		CMongoClientActor(CMongoConnectionSettings const &_ConnectionSetting, NStr::CStr const &_DefaultDatabase);
		~CMongoClientActor();

		enum EQueryOption
		{
			EQueryOption_None = 0
			, EQueryOption_CursorTailable = DMibBit(1)
			, EQueryOption_SlaveOk = DMibBit(2)
			, EQueryOption_OplogReplay = DMibBit(3)
			, EQueryOption_NoCursorTimeout = DMibBit(4)
			, EQueryOption_AwaitData = DMibBit(5)
			, EQueryOption_Exhaust = DMibBit(6)
			, EQueryOption_PartialResults = DMibBit(7)
		};

		enum EUpdateOption
		{
			EUpdateOption_None = 0
			, EUpdateOption_Upsert = DMibBit(0)
			, EUpdateOption_Multi = DMibBit(1)
		};

		enum ERemoveOption
		{
			ERemoveOption_None = 0
			, ERemoveOption_JustOne = DMibBit(0)
		};

		enum EInsertOption
		{
			EInsertOption_None = 0
			, EInsertOption_ContinueOnError = DMibBit(0)
		};

		struct CUpdateResult
		{
			int32 m_MatchedCount;
			int32 m_ModifiedCount;
		};

		struct CTailQueryParams
		{
			NStr::CStr m_Collection;
			NEncoding::CEJsonOrdered m_Query;
			NStr::CStr m_OrderBy;
			NStorage::TCOptional<NEncoding::CEJsonOrdered> m_Fields;
			NStorage::TCOptional<NEncoding::CEJsonOrdered> m_StartQuery;
			EQueryOption m_Options = EQueryOption_None;
		};

		NConcurrency::TCFuture<NContainer::TCVector<NEncoding::CEJsonOrdered>> f_Query
			(
				NStr::CStr _Collection
				, NEncoding::CEJsonOrdered _Query
				, uint32 _nToReturn
				, uint32 _nToSkip
				, NStorage::TCUniquePointer<NEncoding::CEJsonOrdered> _pFields
				, NStorage::TCUniquePointer<NEncoding::CEJsonOrdered> _pOrder
				, EQueryOption _Options
			)
		;
		NConcurrency::TCFuture<NEncoding::CEJsonOrdered> f_RunCommand
			(
				NStr::CStr _Database
				, NEncoding::CEJsonOrdered _Command
			)
		;
		NConcurrency::TCFuture<NConcurrency::CActorSubscription> f_TailQuery
			(
				CTailQueryParams _Params
				, NConcurrency::TCActorFunctorWeak<NConcurrency::TCFuture<void> (NEncoding::CEJsonOrdered _Result)> _fOnResult
			)
		;
		NConcurrency::TCFuture<uint64> f_Count
			(
				 NStr::CStr _Collection
				, NEncoding::CEJsonOrdered _Query
				, uint32 _nToReturn
				, uint32 _nToSkip
			)
		;
		NConcurrency::TCFuture<void> f_BatchInsert(NStr::CStr _Collection, NContainer::TCVector<NEncoding::CEJsonOrdered> _Documents, EInsertOption _Options);
		NConcurrency::TCFuture<void> f_Insert(NStr::CStr _Collection, NEncoding::CEJsonOrdered _Document, EInsertOption _Options);
		NConcurrency::TCFuture<CUpdateResult> f_Update(NStr::CStr _Collection, NEncoding::CEJsonOrdered _Query, NEncoding::CEJsonOrdered _Update, EUpdateOption _Options);
		NConcurrency::TCFuture<void> f_Remove(NStr::CStr _Collection, NEncoding::CEJsonOrdered _Query, ERemoveOption _Options);

		template <typename tf_CReturn, typename ...tf_CParams>
		static tf_CReturn fs_WithConnectionRetry
			(
				tf_CReturn (CMongoClientActor::* _pMemberPointer)(tf_CParams ...)
				, NStorage::TCSharedPointer<CMongoClientRetryState> _pState
				, NTraits::TCRemoveReferenceAndQualifiers<tf_CParams> ...p_Params
			)
		;

	private:
		NConcurrency::TCFuture<void> fp_Destroy() override;
		void fp_ConnectToServer();

		struct CInternal;
		NStorage::TCUniquePointer<CInternal> mp_pInternal;
	};
}

#include "Malterlib_Mongo_Client.hpp"

#ifndef DMibPNoShortCuts
	using namespace NMib::NMongo;
#endif
