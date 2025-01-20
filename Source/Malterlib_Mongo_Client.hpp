// Copyright © 2021 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib::NMongo
{
	template <typename tf_CStr>
	void CMongoServerHost::f_Format(tf_CStr &o_Str) const
	{
		o_Str += typename tf_CStr::CFormat("{}:{}") << m_Host << m_Port;
	}

	template <typename tf_CStream>
	void CMongoErrorData::f_Stream(tf_CStream &_Stream)
	{
		uint32 Version = EProtocolVersion_Current;
		_Stream % Version;
		_Stream % m_RawServerError;
		_Stream % m_ErrorCode;
	}

	namespace NPrivate
	{
		template <typename tf_CParam>
		tf_CParam fg_CopyMongoClientParam(tf_CParam const &_Param)
		{
			return fg_TempCopy(_Param);
		}

		template <typename tf_CParam>
		NStorage::TCUniquePointer<tf_CParam> fg_CopyMongoClientParam(NStorage::TCUniquePointer<tf_CParam> const &_pParam)
		{
			if (!_pParam)
				return {};

			return fg_Construct(*_pParam);
		}
	}

	template <typename tf_CReturn, typename ...tf_CParams>
	tf_CReturn CMongoClientActor::fs_WithConnectionRetry
		(
			tf_CReturn (CMongoClientActor::* _pMemberPointer)(tf_CParams ...)
			, NStorage::TCSharedPointer<CMongoClientRetryState> _pState
			, typename NTraits::TCRemoveReferenceAndQualifiers<tf_CParams>::CType ...p_Params
		)
	{
		static_assert(NConcurrency::NPrivate::TCIsFuture<tf_CReturn>::mc_Value);

		bool bFirstTime = true;

		NTime::CClock Clock{true};

		while (true)
		{
			if (!bFirstTime || !_pState->m_MongoClient)
			{
				if (_pState->m_MongoClient)
					co_await fg_Move(_pState->m_MongoClient).f_Destroy();

				_pState->m_MongoClient = fg_Construct(fg_Construct(_pState->m_ConnectionSettings, "local"), "MongoDB client connection (config)");
			}

			bFirstTime = false;

			auto TryResult = co_await _pState->m_MongoClient(_pMemberPointer, NPrivate::fg_CopyMongoClientParam(p_Params)...).f_Wrap();

			if (!TryResult)
			{
				auto pException = TryResult.f_GetException();
				if (!fg_ExceptionIsOfType<CExceptionMongo>(pException))
					co_return fg_Move(pException);

				auto MongoErrorData = CMongoErrorData::fs_FromException(pException);
				if (MongoErrorData && MongoErrorData->f_IsRecoverableConnectionError())
				{
					if (Clock.f_GetTime() > _pState->m_Timeout)
						DMibLog(Error, "Timed out waiting for MongoDB connection. Error: {} {}", TryResult.f_GetExceptionStr(), MongoErrorData->m_RawServerError);
					else
					{
						co_await NConcurrency::fg_Timeout(0.1);

						continue;
					}
				}

				if (_pState->m_MongoClient)
					co_await fg_Move(_pState->m_MongoClient).f_Destroy();

				co_return TryResult.f_GetException();
			}
			else
				co_return fg_Move(*TryResult);
		}

		co_return {};
	}
	
}
