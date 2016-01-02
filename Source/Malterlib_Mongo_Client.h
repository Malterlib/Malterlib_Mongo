// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Encoding/EJSON>

namespace NMib
{
	namespace NMongo
	{
		class CMongoClientActor : public NConcurrency::CActor
		{
		public:
			typedef NConcurrency::CSeparateThreadActorHolder CActorHolder;

			CMongoClientActor(NStr::CStr const &_ServerAddress, NStr::CStr const &_DefaultDatabase);
			~CMongoClientActor();
			
			void f_Construct() override;
			NConcurrency::TCContinuation<void> f_Destroy() override;

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
				, EUpdateOption_Broadcast = DMibBit(2)
			};

			enum ERemoveOption
			{
				ERemoveOption_None = 0
				, ERemoveOption_JustOne = DMibBit(0)
				, ERemoveOption_Broadcast = DMibBit(1)
			};

			enum EInsertOption
			{
				EInsertOption_None = 0
				, EInsertOption_ContinueOnError = DMibBit(0)
			};
			
			NConcurrency::TCContinuation<NContainer::TCVector<NEncoding::CEJSON>> f_Query
				(
					NStr::CStr const &_Collection
					, NEncoding::CEJSON const &_Query
					, uint32 _nToReturn
					, uint32 _nToSkip
					, NPtr::TCUniquePointer<NEncoding::CEJSON> const &_Fields
					, EQueryOption _Options
				)
			;
			NConcurrency::TCContinuation<NConcurrency::CActorCallback> f_TailQuery
				(
					NStr::CStr const &_Collection
					, NEncoding::CEJSON const &_Query
					, NStr::CStr const &_OrderBy
					, NPtr::TCUniquePointer<NEncoding::CEJSON> _Fields
					, EQueryOption _Options
					, NConcurrency::TCActor<CActor> &&_CallbackActor
					, NFunction::TCFunction<void (NFunction::CThisTag &, NEncoding::CEJSON &&_Result)> &&_fOnResult
				)
			;
			NConcurrency::TCContinuation<uint64> f_Count(NStr::CStr const &_Collection, NEncoding::CEJSON const &_Query, uint32 _nToReturn, uint32 _nToSkip, EQueryOption _Options);
			NConcurrency::TCContinuation<void> f_Insert(NStr::CStr const &_Collection, NEncoding::CEJSON const &_Document, EInsertOption _Options);
			NConcurrency::TCContinuation<void> f_Update(NStr::CStr const &_Collection, NEncoding::CEJSON const &_Query, NEncoding::CEJSON const &_Update, EUpdateOption _Options);
			NConcurrency::TCContinuation<void> f_Remove(NStr::CStr const &_Collection, NEncoding::CEJSON const &_Query, ERemoveOption _Options);
			
		private:
			void fp_ConnectToServer();

			struct CInternal;
			NPtr::TCUniquePointer<CInternal> mp_pInternal;
		};
	}
}

#ifndef DMibPNoShortCuts
using namespace NMib::NMongo;
#endif
