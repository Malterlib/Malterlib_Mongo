// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>

#ifdef DPlatformFamily_Windows
#include <winsock2.h>
#include <Windows.h>
#pragma warning(disable:4267)
#endif

#include <bsoncxx/json.hpp>
#include <bsoncxx/document/view_or_value.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/builder/stream/array.hpp>
#include "Malterlib_Mongo_BSON.h"

using namespace bsoncxx;

namespace NMib::NMongo
{
	namespace
	{
		template <typename tf_CBuilder, typename tf_CJSON>
		void fg_ToBSONMemberCommon(tf_CBuilder &&_Builder, NStr::CStr const &_Name, tf_CJSON const &_Value);
		template <typename tf_CBuilder>
		void fg_ToBSONMember(tf_CBuilder &&_Builder, NStr::CStr const &_Name, NEncoding::CEJSON const &_Value);
		template <typename tf_CBuilder, typename tf_CJSON>
		void fg_ToBSONMember(tf_CBuilder &&_Builder, NStr::CStr const &_Name, tf_CJSON const &_Value);
		template <typename tf_CBuilder, typename tf_CJSON>
		void fg_ToBSONImplArray(tf_CBuilder &&_Builder, tf_CJSON const &_JSON);
		template <typename tf_CBuilder, typename tf_CJSON>
		void fg_ToBSONImpl(tf_CBuilder &&_Builder, tf_CJSON const &_JSON);
		template <typename tf_CBuilder, typename tf_CJSON>
		void fg_ToBSONValue(tf_CBuilder &&_Builder, tf_CJSON const &_Value);

		stdx::string_view fg_ToStringData(NStr::CStr const &_Data)
		{
			return stdx::string_view(_Data.f_GetStr(), _Data.f_GetLen());
		}

		template <typename tf_CBuilder, typename tf_CJSON>
		void fg_ToBSONValueCommon(tf_CBuilder &&_Builder, tf_CJSON const &_Value)
		{
			auto &Value = _Value;
			switch (Value.f_Type())
			{
			case NEncoding::EJSONType_Null:
				_Builder << types::b_null{};
				break;
			case NEncoding::EJSONType_String:
				_Builder << fg_ToStringData(Value.f_String());
				break;
			case NEncoding::EJSONType_Integer:
				_Builder << (std::int64_t)Value.f_Integer();
				break;
			case NEncoding::EJSONType_Float:
				_Builder << Value.f_Float().f_Get();
				break;
			case NEncoding::EJSONType_Boolean:
				_Builder << Value.f_Boolean();
				break;
			case NEncoding::EJSONType_Object:
				{
					auto BeginObject = _Builder << builder::stream::open_document;
					fg_ToBSONImpl
						(
							reinterpret_cast<bsoncxx::v_noabi::builder::stream::key_context<bsoncxx::v_noabi::builder::stream::key_context<bsoncxx::v_noabi::builder::stream::closed_context> > &>(BeginObject)
							, Value
						)
					;
					BeginObject << builder::stream::close_document;
				}
				break;
			case NEncoding::EJSONType_Array:
				{
					auto BeginArray = _Builder << builder::stream::open_array;
					fg_ToBSONImplArray
						(
							reinterpret_cast<bsoncxx::v_noabi::builder::stream::array_context<bsoncxx::v_noabi::builder::stream::key_context<bsoncxx::v_noabi::builder::stream::closed_context> > &>(BeginArray)
							, Value
						);
					BeginArray << builder::stream::close_array;
				}
				break;
			default:
				DMibError("Value not convertible to BSON");
			}
		}

		template <typename tf_CBuilder, typename tf_CJSON>
		void fg_ToBSONMemberCommon(tf_CBuilder &&_Builder, NStr::CStr const &_Name, tf_CJSON const &_Value)
		{
			fg_ToBSONValueCommon(_Builder << fg_ToStringData(_Name), _Value);
		}

		template <typename tf_CBuilder>
		void fg_ToBSONValue(tf_CBuilder &&_Builder, NEncoding::CEJSON const &_Value)
		{
			auto &Value = _Value;
			switch (Value.f_EType())
			{
			case NEncoding::EEJSONType_Date:
				{
					_Builder << types::b_date{std::chrono::milliseconds{NTime::CTimeConvert{Value.f_Date()}.f_UnixMilliseconds()}};
				}
				break;
			case NEncoding::EEJSONType_UserType:
				{
					auto &UserType = Value.f_UserType();
					if (UserType.m_Type == "int32")
						_Builder << (std::int32_t)UserType.m_Value.f_Integer();
					else if (UserType.m_Type == "BinData")
					{
						NContainer::CByteVector Data;
						NEncoding::fg_Base64Decode(UserType.m_Value["Data"].f_String(), Data);

						mint Length = Data.f_GetLen();
						if (Length> mint(TCLimitsInt<uint32>::mc_Max))
							DMibError("Only 4 GiB of binary data supported by BSON");

						NStr::CStr Type = UserType.m_Value["Type"].f_String();
						binary_sub_type Subtype = binary_sub_type::k_binary;
						if (Type == "Function")
							Subtype = binary_sub_type::k_function;
						else if (Type == "ByteArrayDeprecated")
							Subtype = binary_sub_type::k_binary_deprecated;
						else if (Type == "bdtUUID")
							Subtype = binary_sub_type::k_uuid_deprecated;
						else if (Type == "newUUID")
							Subtype = binary_sub_type::k_uuid;
						else if (Type == "MD5Type")
							Subtype = binary_sub_type::k_md5;
						else if (Type == "bdtCustom")
							Subtype = binary_sub_type::k_user;
						else
							DMibError(NStr::fg_Format("Unknown BinData type: {}", Type));

						_Builder << types::b_binary{Subtype, static_cast<uint32_t>(Length), Data.f_GetArray()};
					}
					else if (UserType.m_Type == "Decimal128")
						_Builder << types::b_decimal128{decimal128{uint64(UserType.m_Value["High"].f_Integer()), uint64(UserType.m_Value["Low"].f_Integer())}};
					else if (UserType.m_Type == "MaxKey")
						_Builder << types::b_maxkey{};
					else if (UserType.m_Type == "MinKey")
						_Builder << types::b_minkey{};
					else if (UserType.m_Type == "Timestamp")
						_Builder << types::b_timestamp{uint32(UserType.m_Value["Increment"].f_Integer()), uint32(UserType.m_Value["Seconds"].f_Integer())};
					else if (UserType.m_Type == "CodeWScope")
					{
						auto &Code = UserType.m_Value["Code"].f_String();
						auto Scope = fg_ToBSON(NEncoding::CEJSON::fs_FromJSON(UserType.m_Value["Scope"]));
						_Builder << types::b_codewscope{fg_ToStringData(Code), Scope};
					}
					else if (UserType.m_Type == "Symbol")
						_Builder << types::b_symbol{fg_ToStringData(UserType.m_Value.f_String())};
					else if (UserType.m_Type == "Code")
						_Builder << types::b_code{fg_ToStringData(UserType.m_Value.f_String())};
					else if (UserType.m_Type == "DBRef")
					{
						_Builder << types::b_dbpointer
							{
								fg_ToStringData(UserType.m_Value["NS"].f_String())
								, oid{fg_ToStringData(UserType.m_Value["ObjectID"].f_String())}
							}
						;
					}
					else if (UserType.m_Type == "RegEx")
					{
						_Builder << types::b_regex
							{
								fg_ToStringData(UserType.m_Value["Regex"].f_String())
								, fg_ToStringData(UserType.m_Value["RegexFlags"].f_String())
							}
						;
					}
					else if (UserType.m_Type == "jstOID")
						_Builder << types::b_oid{oid{fg_ToStringData(UserType.m_Value.f_String())}};
					else if (UserType.m_Type == "Undefined")
						_Builder << types::b_undefined{};
					else
						fg_ToBSONValue(_Builder, Value.f_ToJSON());
				}
				break;
			case NEncoding::EEJSONType_Binary:
				{
					mint Length = Value.f_Binary().f_GetLen();
					if (Length> mint(TCLimitsInt<uint32>::mc_Max))
						DMibError("Only 4 GiB of binary data supported by BSON");
					_Builder << types::b_binary{binary_sub_type::k_binary, static_cast<uint32_t>(Length), Value.f_Binary().f_GetArray()};
				}
				break;
			default:
				fg_ToBSONValueCommon(_Builder, _Value);
				break;
			}
		}

		template <typename tf_CBuilder, typename tf_CJSON>
		void fg_ToBSONValue(tf_CBuilder &&_Builder, tf_CJSON const &_Value)
		{
			return fg_ToBSONValueCommon(_Builder, _Value);
		}

		template <typename tf_CBuilder>
		void fg_ToBSONMember(tf_CBuilder &&_Builder, NStr::CStr const &_Name, NEncoding::CEJSON const &_Value)
		{
			fg_ToBSONValue(_Builder << fg_ToStringData(_Name), _Value);
		}

		template <typename tf_CBuilder, typename tf_CJSON>
		void fg_ToBSONMember(tf_CBuilder &&_Builder, NStr::CStr const &_Name, tf_CJSON const &_Value)
		{
			return fg_ToBSONMemberCommon(_Builder, _Name, _Value);
		}

		template <typename tf_CBuilder, typename tf_CJSON>
		void fg_ToBSONImplArray(tf_CBuilder &&_Builder, tf_CJSON const &_JSON)
		{
			for (auto &Member : _JSON.f_Array())
				fg_ToBSONValue(_Builder, Member);
		}

		template <typename tf_CBuilder, typename tf_CJSON>
		void fg_ToBSONImpl(tf_CBuilder &&_Builder, tf_CJSON const &_JSON)
		{
			for (auto iMember = _JSON.f_Object().f_OrderedIterator(); iMember; ++iMember)
				fg_ToBSONMember(_Builder, iMember->f_Name(), iMember->f_Value());
		}

		NStr::CStr fg_FromStringData(stdx::string_view const &_Data)
		{
			return NStr::CStr(_Data.data(), _Data.length());
		}

		void fg_FromBSONImp(NEncoding::CEJSON &_JSON, bsoncxx::document::view const &_BSON);
		void fg_FromBSONImp(NEncoding::CEJSON &_JSON, bsoncxx::array::view const &_BSON);

		void fg_FromBSONImp(NEncoding::CEJSON &_JSON, bsoncxx::document::element const &_Element)
		{
			switch (_Element.type())
			{
			case type::k_double:
				_JSON = _Element.get_double().value;
				return;
			case type::k_string:
				_JSON = fg_FromStringData(_Element.get_string().value);
				return;
			case type::k_document:
				{
					fg_FromBSONImp(_JSON, _Element.get_document().value);
				}
				return;
			case type::k_array:
				{
					fg_FromBSONImp(_JSON, _Element.get_array().value);
				}
				return;
			case type::k_bool:
				{
					_JSON = _Element.get_bool().value;
				}
				return;
			case type::k_date:
				{
					_JSON = NTime::CTimeConvert::fs_FromUnixMilliseconds(_Element.get_date().to_int64());
				}
				return;
			case type::k_null:
				{
					_JSON = nullptr;
				}
				return;
			case type::k_int32:
				{
					_JSON = NEncoding::EEJSONType_UserType;
					auto &UserType = _JSON.f_UserType();
					UserType.m_Type = "int32";
					UserType.m_Value = _Element.get_int32();
				}
				return;
			case type::k_int64:
				_JSON = int64(_Element.get_int64());
				return;
			case type::k_binary:
				{
					auto Binary = _Element.get_binary();

					NContainer::CByteVector Data;
					Data.f_Insert(Binary.bytes, Binary.size);

					if (Binary.sub_type == binary_sub_type::k_binary)
					{
						_JSON = fg_Move(Data);
						return;
					}

					_JSON = NEncoding::EEJSONType_UserType;
					auto &UserType = _JSON.f_UserType();

					UserType.m_Type = "BinData";
					UserType.m_Value["Data"] = NEncoding::fg_Base64Encode(Data);

					NStr::CStr Type;
					switch (Binary.sub_type)
					{
					case binary_sub_type::k_function:
						Type = "Function";
						break;
					case binary_sub_type::k_binary_deprecated:
						Type = "ByteArrayDeprecated";
						break;
					case binary_sub_type::k_uuid_deprecated:
						Type = "bdtUUID";
						break;
					case binary_sub_type::k_uuid:
						Type = "newUUID";
						break;
					case binary_sub_type::k_md5:
						Type = "MD5Type";
						break;
					case binary_sub_type::k_user:
						Type = "bdtCustom";
						break;
					case binary_sub_type::k_binary:
						DMibNeverGetHere;
						break;
					}

					UserType.m_Value["Type"] = Type;
				}
				return;
			case type::k_undefined:
				{
					_JSON = NEncoding::EEJSONType_UserType;
					auto &UserType = _JSON.f_UserType();
					UserType.m_Type = "Undefined";
					UserType.m_Value = 1;
				}
				return;
			case type::k_oid:
				{
					_JSON = NEncoding::EEJSONType_UserType;
					auto &UserType = _JSON.f_UserType();

					UserType.m_Type = "jstOID";
					UserType.m_Value = _Element.get_oid().value.to_string().c_str();
				}
				return;
			case type::k_regex:
				{
					_JSON = NEncoding::EEJSONType_UserType;
					auto &UserType = _JSON.f_UserType();

					auto RegEx = _Element.get_regex();

					UserType.m_Type = "RegEx";
					UserType.m_Value["Regex"] = fg_FromStringData(RegEx.regex);
					UserType.m_Value["RegexFlags"] = fg_FromStringData(RegEx.options);
				}
				return;
			case type::k_dbpointer:
				{
					auto DbPointer = _Element.get_dbpointer();

					_JSON = NEncoding::EEJSONType_UserType;
					auto &UserType = _JSON.f_UserType();

					UserType.m_Type = "DBRef";
					UserType.m_Value["NS"] = fg_FromStringData(DbPointer.collection);
					UserType.m_Value["ObjectID"] = DbPointer.value.to_string().c_str();
				}
				return;
			case type::k_code:
				{
					_JSON = NEncoding::EEJSONType_UserType;
					auto &UserType = _JSON.f_UserType();

					UserType.m_Type = "Code";
					UserType.m_Value = fg_FromStringData(_Element.get_code().code);
				}
				return;
			case type::k_symbol:
				{
					_JSON = NEncoding::EEJSONType_UserType;
					auto &UserType = _JSON.f_UserType();

					UserType.m_Type = "Symbol";
					UserType.m_Value = fg_FromStringData(_Element.get_symbol().symbol);
				}
				return;
			case type::k_codewscope:
				{
					_JSON = NEncoding::EEJSONType_UserType;
					auto &UserType = _JSON.f_UserType();

					auto SourceData = _Element.get_codewscope();

					UserType.m_Type = "CodeWScope";
					UserType.m_Value["Code"] = fg_FromStringData(SourceData.code);
					NEncoding::CEJSON Scope;
					fg_FromBSONImp(Scope, SourceData.scope);
					if (Scope.f_IsValid())
						UserType.m_Value["Scope"] = Scope.f_ToJSON();
				}
				return;
			case type::k_timestamp:
				{
					_JSON = NEncoding::EEJSONType_UserType;
					auto &UserType = _JSON.f_UserType();

					auto SourceData = _Element.get_timestamp();

					UserType.m_Type = "Timestamp";
					UserType.m_Value["Seconds"] = SourceData.timestamp;
					UserType.m_Value["Increment"] = SourceData.increment;
				}
				return;
			case type::k_decimal128:
				{
					_JSON = NEncoding::EEJSONType_UserType;
					auto &UserType = _JSON.f_UserType();

					auto SourceData = _Element.get_decimal128();

					UserType.m_Type = "Decimal128";
					UserType.m_Value["High"] = SourceData.value.high();
					UserType.m_Value["Low"] = SourceData.value.low();
				}
				return;
			case type::k_minkey:
				{
					_JSON = NEncoding::EEJSONType_UserType;
					auto &UserType = _JSON.f_UserType();
					UserType.m_Type = "MinKey";
					UserType.m_Value = 1;
				}
				return;
			case type::k_maxkey:
				{
					_JSON = NEncoding::EEJSONType_UserType;
					auto &UserType = _JSON.f_UserType();
					UserType.m_Type = "MaxKey";
					UserType.m_Value = 1;
				}
				return;
			}
			DMibNeverGetHere; // Not supported
		}

		void fg_FromBSONImp(NEncoding::CEJSON &_JSON, bsoncxx::document::view const &_BSON)
		{
			_JSON = NEncoding::EJSONType_Object;

			for (auto &&Element : _BSON)
			{
				auto &Member = _JSON.f_Object().f_CreateMember(fg_FromStringData(Element.key()));
				fg_FromBSONImp(Member, Element);
			}
		}

		void fg_FromBSONImp(NEncoding::CEJSON &_JSON, bsoncxx::array::view const &_BSON)
		{
			_JSON = NEncoding::EJSONType_Array;

			for (auto &&Element : _BSON)
			{
				auto &Member = _JSON.f_Array().f_Insert();
				fg_FromBSONImp(Member, reinterpret_cast<bsoncxx::document::element const &>(Element));
			}
		}
	}

	document::value fg_ToBSON(NEncoding::CEJSON const &_JSON)
	{
		switch (_JSON.f_Type())
		{
		case NEncoding::EJSONType_Object:
			{
				bsoncxx::builder::stream::document Builder;
				fg_ToBSONImpl(Builder, _JSON);
				return Builder << builder::stream::finalize;
			}
			break;
		case NEncoding::EJSONType_Invalid:
			{
				return bsoncxx::document::value(nullptr, 0, nullptr);
			}
			break;
		case NEncoding::EJSONType_Array:
			DMibError("Object contains array, use fg_ToBSONArray");
		default:
			DMibError("Unsupported BSON root type");
		}
	}

	bsoncxx::array::value fg_ToBSONArray(NEncoding::CEJSON const &_JSON)
	{
		switch (_JSON.f_Type())
		{
		case NEncoding::EJSONType_Array:
			{
				bsoncxx::builder::stream::array Builder;
				fg_ToBSONImplArray(Builder, _JSON);
				return Builder << builder::stream::finalize;
			}
			break;
		case NEncoding::EJSONType_Invalid:
			{
				return bsoncxx::array::value(nullptr, 0, nullptr);
			}
			break;
		case NEncoding::EJSONType_Object:
			DMibError("Object contains object, use fg_ToBSON");
		default:
			DMibError("Unsupported BSON root type");
		}
	}


	NEncoding::CEJSON fg_FromBSON(bsoncxx::document::view_or_value _BSON)
	{
		NEncoding::CEJSON Ret;
		fg_FromBSONImp(Ret, _BSON);
		return Ret;
	}
	NEncoding::CEJSON fg_FromBSON(bsoncxx::array::view_or_value _BSON)
	{
		NEncoding::CEJSON Ret;
		fg_FromBSONImp(Ret, _BSON);
		return Ret;
	}
}
