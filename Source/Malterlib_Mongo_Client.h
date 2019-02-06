// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Encoding/EJSON>

namespace NMib::NMongo
{
	struct CMongoConnectionSettings
	{
		CMongoConnectionSettings();
		CMongoConnectionSettings(NStr::CStr const &_Host, uint16 _Port);

		bool f_Compatible(CMongoConnectionSettings const &_Settings) const;
		NContainer::TCVector<NStr::CStr> f_GetToolParams() const;
		NStr::CStr f_GetConnectionString() const;
		CMongoConnectionSettings f_ForConnectionString(NStr::CStr const &_ConnectionString) const;

		NStr::CStr m_Host = "localhost";
		uint16 m_Port = 27017;

		// Needs to be the same to be compatible
		NStr::CStr m_CACertificatePath;
		NStr::CStr m_ClientCertificatePath;
		NStr::CStr m_UserName;
		bool m_bEnableSSL = false;
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

		NConcurrency::TCFuture<NContainer::TCVector<NEncoding::CEJSON>> f_Query
			(
				NStr::CStr const &_Collection
				, NEncoding::CEJSON const &_Query
				, uint32 _nToReturn
				, uint32 _nToSkip
				, NStorage::TCUniquePointer<NEncoding::CEJSON> const &_pFields
				, NStorage::TCUniquePointer<NEncoding::CEJSON> const &_pOrder
				, EQueryOption _Options
			)
		;
		NConcurrency::TCFuture<NConcurrency::CActorSubscription> f_TailQuery
			(
				NStr::CStr const &_Collection
				, NEncoding::CEJSON const &_Query
				, NStr::CStr const &_OrderBy
				, NStorage::TCUniquePointer<NEncoding::CEJSON> _Fields
				, EQueryOption _Options
				, NConcurrency::TCActor<CActor> &&_CallbackActor
				, NFunction::TCFunctionMutable<void (NEncoding::CEJSON &&_Result)> &&_fOnResult
			)
		;
		NConcurrency::TCFuture<uint64> f_Count
			(
				 NStr::CStr const &_Collection
				, NEncoding::CEJSON const &_Query
				, uint32 _nToReturn
				, uint32 _nToSkip
				, NStorage::TCUniquePointer<NEncoding::CEJSON> const &_pOrder
				, EQueryOption _Options
			)
		;
		NConcurrency::TCFuture<void> f_BatchInsert(NStr::CStr const &_Collection, NContainer::TCVector<NEncoding::CEJSON> const &_Documents, EInsertOption _Options);
		NConcurrency::TCFuture<void> f_Insert(NStr::CStr const &_Collection, NEncoding::CEJSON const &_Document, EInsertOption _Options);
		NConcurrency::TCFuture<void> f_Update(NStr::CStr const &_Collection, NEncoding::CEJSON const &_Query, NEncoding::CEJSON const &_Update, EUpdateOption _Options);
		NConcurrency::TCFuture<void> f_Remove(NStr::CStr const &_Collection, NEncoding::CEJSON const &_Query, ERemoveOption _Options);

	private:
		NConcurrency::TCFuture<void> fp_Destroy() override;
		void fp_ConnectToServer();

		struct CInternal;
		NStorage::TCUniquePointer<CInternal> mp_pInternal;
	};
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NMongo;
#endif
