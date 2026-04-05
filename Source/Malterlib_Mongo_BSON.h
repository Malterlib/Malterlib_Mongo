// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include <bsoncxx/json.hpp>
#include <bsoncxx/document/view_or_value.hpp>
#include <bsoncxx/array/view_or_value.hpp>
#include <Mib/Encoding/EJson>

namespace NMib::NMongo
{
	bsoncxx::document::value fg_ToBSON(NEncoding::CEJsonOrdered const &_Json);
	bsoncxx::array::value fg_ToBSONArray(NEncoding::CEJsonOrdered const &_Json);
	NEncoding::CEJsonOrdered fg_FromBSON(bsoncxx::document::view_or_value _BSON);
	NEncoding::CEJsonOrdered fg_FromBSON(bsoncxx::array::view_or_value _BSON);
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NMongo;
#endif
