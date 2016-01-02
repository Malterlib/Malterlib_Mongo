// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>
#include <mongo/client/dbclient.h>
#include <Mib/Encoding/EJSON>

namespace NMib
{
	namespace NMongo
	{
		mongo::BSONObj fg_ToBSON(NEncoding::CEJSON const &_JSON);
		NEncoding::CEJSON fg_FromBSON(mongo::BSONObj const &_BSON);
	}
}

#ifndef DMibPNoShortCuts
using namespace NMib::NMongo;
#endif
