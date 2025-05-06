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
				
				CEJSONOrdered const ExpectedEJSON =
					{
						"k_double"_o= 5.6
						, "k_utf8"_o= "Value"
						, "k_document"_o=
						{
							"k_double"_o= 5.7
							, "k_utf8"_o= "Value1"
							, "k_document"_o=
							{
								"k_double"_o= 5.8
								, "k_utf8"_o= "Value2"
							}
						}
						, "k_array"_o= _o
						[
							_o=
							{
								"k_double"_o= 5.6
								, "k_utf8"_o= "Value"
							}
							, "String"
							, CTimeConvert::fs_CreateTime(2001, 02, 03)
						]
						, "k_array_empty"_o= _o[]
						, "k_bool"_o= true
						, "k_date"_o= CTimeConvert::fs_CreateTime(2001, 02, 03)
						, "k_null"_o= nullptr
						, "k_int32"_o= CEJSONUserTypeOrdered{"int32", int32(556)}
						, "k_int64"_o= int64(constant_int64(66554466556665))
						, "k_binary"_o= TestBinary
						, "k_binary k_function"_o= CEJSONUserTypeOrdered{"BinData", {"Data"_jo= fg_Base64Encode(TestBinary), "Type"_jo= "Function"}}
						, "k_binary k_binary_deprecated"_o= CEJSONUserTypeOrdered{"BinData", {"Data"_jo= fg_Base64Encode(TestBinary), "Type"_jo= "ByteArrayDeprecated"}}
						, "k_binary k_uuid_deprecated"_o= CEJSONUserTypeOrdered{"BinData", {"Data"_jo= fg_Base64Encode(TestBinary), "Type"_jo= "bdtUUID"}}
						, "k_binary k_uuid"_o= CEJSONUserTypeOrdered{"BinData", {"Data"_jo= fg_Base64Encode(TestBinary), "Type"_jo= "newUUID"}}
						, "k_binary k_md5"_o= CEJSONUserTypeOrdered{"BinData", {"Data"_jo= fg_Base64Encode(TestBinary), "Type"_jo= "MD5Type"}}
						, "k_binary k_user"_o= CEJSONUserTypeOrdered{"BinData", {"Data"_jo= fg_Base64Encode(TestBinary), "Type"_jo= "bdtCustom"}}
						, "k_undefined"_o= CEJSONUserTypeOrdered{"Undefined", 1}
						, "k_oid"_o= CEJSONUserTypeOrdered{"jstOID", TestOID.c_str()}
						, "k_regex"_o= CEJSONUserTypeOrdered{"RegEx", {"Regex"_jo= "RegEx", "RegexFlags"_jo= "ls"}}
						, "k_dbpointer"_o= CEJSONUserTypeOrdered{"DBRef", {"NS"_jo= "Collection", "ObjectID"_jo= TestOID.c_str()}}
						, "k_code"_o= CEJSONUserTypeOrdered{"Code", "TestCode"}
						, "k_symbol"_o= CEJSONUserTypeOrdered{"Symbol", "TestSymbol"}
						, "k_codewscope"_o= CEJSONUserTypeOrdered{"CodeWScope", {"Code"_jo= "TestCode", "Scope"_jo= {"Test1"_jo= "Test2"}}}
						, "k_timestamp"_o= CEJSONUserTypeOrdered{"Timestamp", {"Seconds"_jo= 555, "Increment"_jo= 666}}
						, "k_decimal128"_o= CEJSONUserTypeOrdered{"Decimal128", {"High"_jo= constant_uint64(55555555555), "Low"_jo= constant_uint64(66666666666)}}
						, "k_minkey"_o= CEJSONUserTypeOrdered{"MinKey", 1}
						, "k_maxkey"_o= CEJSONUserTypeOrdered{"MaxKey", 1}
					}
				;

				CEJSONOrdered const ExpectedEJSONArray = _o
					[
						"String"
						, ExpectedEJSON
					]
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
