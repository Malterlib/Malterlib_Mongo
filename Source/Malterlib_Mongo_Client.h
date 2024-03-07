// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Encoding/EJSON>
#include <Mib/Web/HTTP/URL>
#include <Mib/Concurrency/ActorFunctorWeak>

namespace NMib::NMongo
{
	struct CMongoServerHost
	{
		auto operator <=> (CMongoServerHost const &_Right) const = default;

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
	};

	class CMongoClientActor : public NConcurrency::CActor
	{
	public:
		typedef NConcurrency::CSeparateThreadActorHolder CActorHolder;

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
			NEncoding::CEJSONOrdered m_Query;
			NStr::CStr m_OrderBy;
			NStorage::TCOptional<NEncoding::CEJSONOrdered> m_Fields;
			NStorage::TCOptional<NEncoding::CEJSONOrdered> m_StartQuery;
			EQueryOption m_Options = EQueryOption_None;
		};

		NConcurrency::TCFuture<NContainer::TCVector<NEncoding::CEJSONOrdered>> f_Query
			(
				NStr::CStr const &_Collection
				, NEncoding::CEJSONOrdered const &_Query
				, uint32 _nToReturn
				, uint32 _nToSkip
				, NStorage::TCUniquePointer<NEncoding::CEJSONOrdered> const &_pFields
				, NStorage::TCUniquePointer<NEncoding::CEJSONOrdered> const &_pOrder
				, EQueryOption _Options
			)
		;
		NConcurrency::TCFuture<NEncoding::CEJSONOrdered> f_RunCommand
			(
				NStr::CStr const &_Database
				, NEncoding::CEJSONOrdered const &_Command
			)
		;
		NConcurrency::TCFuture<NConcurrency::CActorSubscription> f_TailQuery
			(
				CTailQueryParams &&_Params
				, NConcurrency::TCActorFunctorWeak<NConcurrency::TCFuture<void> (NEncoding::CEJSONOrdered &&_Result)> &&_fOnResult
			)
		;
		NConcurrency::TCFuture<uint64> f_Count
			(
				 NStr::CStr const &_Collection
				, NEncoding::CEJSONOrdered const &_Query
				, uint32 _nToReturn
				, uint32 _nToSkip
			)
		;
		NConcurrency::TCFuture<void> f_BatchInsert(NStr::CStr const &_Collection, NContainer::TCVector<NEncoding::CEJSONOrdered> const &_Documents, EInsertOption _Options);
		NConcurrency::TCFuture<void> f_Insert(NStr::CStr const &_Collection, NEncoding::CEJSONOrdered const &_Document, EInsertOption _Options);
		NConcurrency::TCFuture<CUpdateResult> f_Update(NStr::CStr const &_Collection, NEncoding::CEJSONOrdered const &_Query, NEncoding::CEJSONOrdered const &_Update, EUpdateOption _Options);
		NConcurrency::TCFuture<void> f_Remove(NStr::CStr const &_Collection, NEncoding::CEJSONOrdered const &_Query, ERemoveOption _Options);

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
