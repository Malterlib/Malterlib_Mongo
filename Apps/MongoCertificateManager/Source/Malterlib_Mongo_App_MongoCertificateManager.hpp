// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NMongo::NMongoCertificateManager
{
	template <typename tf_CStr>
	void CMongoCertificateManagerActor::CUserKey::f_Format(tf_CStr &o_Str) const
	{
		o_Str += typename tf_CStr::CFormat("{}/{}") << m_Authority << m_Name;
	}
}
