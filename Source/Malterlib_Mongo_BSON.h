// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>
#include <bsoncxx/json.hpp>
#include <bsoncxx/document/view_or_value.hpp>
#include <bsoncxx/array/view_or_value.hpp>
#include <Mib/Encoding/EJSON>

namespace NMib
{
	namespace NMongo
	{
		bsoncxx::document::value fg_ToBSON(NEncoding::CEJSON const &_JSON);
		bsoncxx::array::value fg_ToBSONArray(NEncoding::CEJSON const &_JSON);
		NEncoding::CEJSON fg_FromBSON(bsoncxx::document::view_or_value _BSON);
		NEncoding::CEJSON fg_FromBSON(bsoncxx::array::view_or_value _BSON);
	}
}

#ifndef DMibPNoShortCuts
using namespace NMib::NMongo;
#endif
