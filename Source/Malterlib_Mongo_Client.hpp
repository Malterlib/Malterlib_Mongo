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
}
