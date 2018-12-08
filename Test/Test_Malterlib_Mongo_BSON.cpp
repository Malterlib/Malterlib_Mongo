// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Mongo/BSON>
#include <Mib/Encoding/JSONShortcuts>

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/builder/stream/array.hpp>

using namespace NMib::NMongo;
using namespace NMib::NEncoding;
using namespace NMib::NTime;
using namespace NMib::NContainer;
using namespace NMib::NCryptography;
using namespace NMib;

namespace
{
	class CBSON_Tests : public CTest
	{
	public:
		
		void f_DoTests()
		{
			DMibTestSuite("Conversion")
			{
				using namespace bsoncxx;
				
				CByteVector TestBinary{1,2,6,8};
				std::string TestOID = "012345678901234567890123";
				
				bsoncxx::document::value OriginalBSON = builder::stream::document{}
					<< "k_double" << 5.6
					<< "k_utf8" << "Value"
					<< "k_document" << builder::stream::open_document
						<< "k_double" << 5.7
						<< "k_utf8" << "Value1"
						<< "k_document" << builder::stream::open_document
							<< "k_double" << 5.8
							<< "k_utf8" << "Value2"
							<< builder::stream::close_document
						<< builder::stream::close_document
					<< "k_array" << builder::stream::open_array
						<< builder::stream::open_document
							<< "k_double" << 5.6
							<< "k_utf8" << "Value"
							<< builder::stream::close_document
						<< "String"
						<< types::b_date{std::chrono::milliseconds{CTimeConvert{CTimeConvert::fs_CreateTime(2001, 02, 03)}.f_UnixMilliseconds()}}
						<< builder::stream::close_array
					<< "k_array_empty" << builder::stream::open_array
						<< builder::stream::close_array
					<< "k_bool" << true
					<< "k_date" << types::b_date{std::chrono::milliseconds{CTimeConvert{CTimeConvert::fs_CreateTime(2001, 02, 03)}.f_UnixMilliseconds()}}
					<< "k_null" << types::b_null{}
					<< "k_int32" << types::b_int32{int32(556)}
					<< "k_int64" << types::b_int64{constant_int64(66554466556665)}
					<< "k_binary" << types::b_binary{binary_sub_type::k_binary, fg_AutoStaticCast(TestBinary.f_GetLen()), TestBinary.f_GetArray()}
					<< "k_binary k_function" << types::b_binary{binary_sub_type::k_function, fg_AutoStaticCast(TestBinary.f_GetLen()), TestBinary.f_GetArray()}
					<< "k_binary k_binary_deprecated" << types::b_binary{binary_sub_type::k_binary_deprecated, fg_AutoStaticCast(TestBinary.f_GetLen()), TestBinary.f_GetArray()}
					<< "k_binary k_uuid_deprecated" << types::b_binary{binary_sub_type::k_uuid_deprecated, fg_AutoStaticCast(TestBinary.f_GetLen()), TestBinary.f_GetArray()}
					<< "k_binary k_uuid" << types::b_binary{binary_sub_type::k_uuid, fg_AutoStaticCast(TestBinary.f_GetLen()), TestBinary.f_GetArray()}
					<< "k_binary k_md5" << types::b_binary{binary_sub_type::k_md5, fg_AutoStaticCast(TestBinary.f_GetLen()), TestBinary.f_GetArray()}
					<< "k_binary k_user" << types::b_binary{binary_sub_type::k_user, fg_AutoStaticCast(TestBinary.f_GetLen()), TestBinary.f_GetArray()}
					<< "k_undefined" << types::b_undefined{}
					<< "k_oid" << types::b_oid{oid{TestOID}}
					<< "k_regex" << types::b_regex{"RegEx", "ls"}
					<< "k_dbpointer" << types::b_dbpointer{"Collection", oid{TestOID}}
					<< "k_code" << types::b_code{"TestCode"}
					<< "k_symbol" << types::b_symbol{"TestSymbol"}
					<< "k_codewscope" << types::b_codewscope{"TestCode", builder::stream::document{} << "Test1" << "Test2"<< builder::stream::finalize}
					<< "k_timestamp" << types::b_timestamp{666, 555}
					<< "k_decimal128" << types::b_decimal128{decimal128{constant_uint64(55555555555), constant_uint64(66666666666)}}
					<< "k_minkey" << types::b_minkey{}
					<< "k_maxkey" << types::b_maxkey{}
					<< builder::stream::finalize
				;
				
				bsoncxx::array::value OriginalBSONArray = builder::stream::array{}
					<< "String"
					<< OriginalBSON
					<< builder::stream::finalize
				;
				
				CEJSON const ExpectedEJSON =
					{
						"k_double"_= 5.6
						, "k_utf8"_= "Value"
						, "k_document"_=
						{
							"k_double"_= 5.7
							, "k_utf8"_= "Value1"
							, "k_document"_=
							{
								"k_double"_= 5.8
								, "k_utf8"_= "Value2"
							}
						}
						, "k_array"_=
						{
							{
								"k_double"_= 5.6
								, "k_utf8"_= "Value"
							}
							, "String"
							, CTimeConvert::fs_CreateTime(2001, 02, 03)
						}
						, "k_array_empty"_= _[_]
						, "k_bool"_= true
						, "k_date"_= CTimeConvert::fs_CreateTime(2001, 02, 03)
						, "k_null"_= nullptr
						, "k_int32"_= CEJSONUserType{"int32", int32(556)}
						, "k_int64"_= int64(constant_int64(66554466556665))
						, "k_binary"_= TestBinary
						, "k_binary k_function"_= CEJSONUserType{"BinData", {"Data"__= fg_Base64Encode(TestBinary), "Type"__= "Function"}}
						, "k_binary k_binary_deprecated"_= CEJSONUserType{"BinData", {"Data"__= fg_Base64Encode(TestBinary), "Type"__= "ByteArrayDeprecated"}}
						, "k_binary k_uuid_deprecated"_= CEJSONUserType{"BinData", {"Data"__= fg_Base64Encode(TestBinary), "Type"__= "bdtUUID"}}
						, "k_binary k_uuid"_= CEJSONUserType{"BinData", {"Data"__= fg_Base64Encode(TestBinary), "Type"__= "newUUID"}}
						, "k_binary k_md5"_= CEJSONUserType{"BinData", {"Data"__= fg_Base64Encode(TestBinary), "Type"__= "MD5Type"}}
						, "k_binary k_user"_= CEJSONUserType{"BinData", {"Data"__= fg_Base64Encode(TestBinary), "Type"__= "bdtCustom"}}
						, "k_undefined"_= CEJSONUserType{"Undefined", 1}
						, "k_oid"_= CEJSONUserType{"jstOID", TestOID.c_str()}
						, "k_regex"_= CEJSONUserType{"RegEx", {"Regex"__= "RegEx", "RegexFlags"__= "ls"}}
						, "k_dbpointer"_= CEJSONUserType{"DBRef", {"NS"__= "Collection", "ObjectID"__= TestOID.c_str()}}
						, "k_code"_= CEJSONUserType{"Code", "TestCode"}
						, "k_symbol"_= CEJSONUserType{"Symbol", "TestSymbol"}
						, "k_codewscope"_= CEJSONUserType{"CodeWScope", {"Code"__= "TestCode", "Scope"__= {"Test1"__= "Test2"}}}
						, "k_timestamp"_= CEJSONUserType{"Timestamp", {"Seconds"__= 555, "Increment"__= 666}}
						, "k_decimal128"_= CEJSONUserType{"Decimal128", {"High"__= constant_uint64(55555555555), "Low"__= constant_uint64(66666666666)}}
						, "k_minkey"_= CEJSONUserType{"MinKey", 1}
						, "k_maxkey"_= CEJSONUserType{"MaxKey", 1}
					}
				;

				CEJSON const ExpectedEJSONArray =
					{
						"String"
						, ExpectedEJSON
					}
				;
				
				DMibExpect(fg_FromBSON(NMib::fg_TempCopy(OriginalBSON)), ==, ExpectedEJSON);
				DMibExpect(fg_FromBSON(NMib::fg_TempCopy(OriginalBSONArray)), ==, ExpectedEJSONArray);

				DMibExpect(fg_FromBSON(fg_ToBSON(ExpectedEJSON)), ==, ExpectedEJSON);
				DMibExpect(fg_FromBSON(fg_ToBSONArray(ExpectedEJSONArray)), ==, ExpectedEJSONArray);

				DMibExpect(fg_ToBSON(ExpectedEJSON).view(), ==, OriginalBSON.view());
				DMibExpect(fg_ToBSONArray(ExpectedEJSONArray).view(), ==, OriginalBSONArray.view());

				DMibExpect(fg_ToBSON(fg_FromBSON(NMib::fg_TempCopy(OriginalBSON))).view(), ==, OriginalBSON.view());
				DMibExpect(fg_ToBSONArray(fg_FromBSON(NMib::fg_TempCopy(OriginalBSONArray))).view(), ==, OriginalBSONArray.view());
			};
		}
	};

	DMibTestRegister(CBSON_Tests, Malterlib::Mongo);
}
