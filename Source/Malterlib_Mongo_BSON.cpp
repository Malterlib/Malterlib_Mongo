// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>
#include <mongo/client/dbclient.h>
#include "Malterlib_Mongo_BSON.h"

using namespace mongo;

namespace NMib
{
	namespace NMongo
	{
		namespace
		{
			template <typename tf_CJSON>
			void fg_ToBSONMemberCommon(BSONObjBuilder &_Builder, NStr::CStr const &_Name, tf_CJSON const &_Value);
			void fg_ToBSONMember(BSONObjBuilder &_Builder, NStr::CStr const &_Name, NEncoding::CEJSON const &_Value);
			template <typename tf_CJSON>
			void fg_ToBSONMember(BSONObjBuilder &_Builder, NStr::CStr const &_Name, tf_CJSON const &_Value);
			template <typename tf_CJSON>
			void fg_ToBSONImplArray(BSONObjBuilder &_Builder, tf_CJSON const &_JSON);
			template <typename tf_CJSON>
			void fg_ToBSONImpl(BSONObjBuilder &_Builder, tf_CJSON const &_JSON);
			template <typename tf_CJSON>
			BSONObj fg_ToBSONImpl(tf_CJSON const &_JSON);
			
			StringData fg_ToStringData(NStr::CStr const &_Data)
			{
				return StringData(_Data.f_GetStr(), _Data.f_GetLen());
			}
			
			template <typename tf_CJSON>
			void fg_ToBSONMemberCommon(BSONObjBuilder &_Builder, NStr::CStr const &_Name, tf_CJSON const &_Value)
			{
				auto &Name = _Name;
				auto &Value = _Value;
				switch (Value.f_Type())
				{
				case NEncoding::EJSONType_Null:
					_Builder.appendNull(fg_ToStringData(Name));
					break;
				case NEncoding::EJSONType_String:
					_Builder.append(fg_ToStringData(Name), fg_ToStringData(Value.f_String()));
					break;
				case NEncoding::EJSONType_Integer:
					_Builder.append(fg_ToStringData(Name), Value.f_Integer());
					break;
				case NEncoding::EJSONType_Float:
					_Builder.append(fg_ToStringData(Name), Value.f_Float().f_Get());
					break;
				case NEncoding::EJSONType_Boolean:
					_Builder.append(fg_ToStringData(Name), Value.f_Boolean());
					break;
				case NEncoding::EJSONType_Object:
					{
						BSONObjBuilder Builder;
						fg_ToBSONImpl(Builder, Value);
						_Builder.append(fg_ToStringData(Name), Builder.obj());
					}
					break;
				case NEncoding::EJSONType_Array:
					{
						BSONObjBuilder Builder;
						fg_ToBSONImplArray(Builder, Value);
						_Builder.append(fg_ToStringData(Name), BSONArray(Builder.obj()));
					}
					break;
				}
			}

			void fg_ToBSONMember(BSONObjBuilder &_Builder, NStr::CStr const &_Name, NEncoding::CEJSON const &_Value)
			{
				auto &Name = _Name;
				auto &Value = _Value;
				switch (Value.f_EType())
				{
				case NEncoding::EEJSONType_Date:
					_Builder.appendDate(fg_ToStringData(Name), NTime::CTimeConvert(Value.f_Date()).f_UnixMilliseconds());
					break;
				case NEncoding::EEJSONType_UserType:
					{
						auto &UserType = Value.f_UserType();
						if (UserType.m_Type == "BinData")
						{
							NContainer::TCVector<uint8> Data;
							NDataProcessing::fg_Base64Decode(UserType.m_Value.f_GetMember("Data")->f_String(), Data);
							
							NStr::CStr Type = UserType.m_Value.f_GetMember("Type")->f_String();
							BinDataType DataType = BinDataGeneral;
							if (Type == "Function")
								DataType = Function;
							else if (Type == "ByteArrayDeprecated")
								DataType = ByteArrayDeprecated;
							else if (Type == "bdtUUID")
								DataType = bdtUUID;
							else if (Type == "newUUID")
								DataType = newUUID;
							else if (Type == "MD5Type")
								DataType = MD5Type;
							else if (Type == "bdtCustom")
								DataType = bdtCustom;

							if (DataType == ByteArrayDeprecated)
								_Builder.appendBinDataArrayDeprecated(Name.f_GetStr(), Data.f_GetArray(), Data.f_GetLen());
							else
								_Builder.appendBinData(fg_ToStringData(Name), Data.f_GetLen(), DataType, Data.f_GetArray());
						}
						else if (UserType.m_Type == "MaxKey")
							_Builder.appendMaxKey(fg_ToStringData(Name));
						else if (UserType.m_Type == "MinKey")
							_Builder.appendMinKey(fg_ToStringData(Name));
						else if (UserType.m_Type == "Timestamp")
						{
							_Builder.appendTimestamp
								(
									fg_ToStringData(Name)
									, Timestamp_t(UserType.m_Value.f_GetMember("Seconds")->f_Integer()
									, UserType.m_Value.f_GetMember("Increment")->f_Integer())
								)
							;
						}
						else if (UserType.m_Type == "CodeWScope")
						{
							auto &Code = UserType.m_Value.f_GetMember("Code")->f_String();
							_Builder.appendCodeWScope(fg_ToStringData(Name), fg_ToStringData(Code), fg_ToBSON(NEncoding::CEJSON::fs_FromJSON(*UserType.m_Value.f_GetMember("Scope"))));
						}
						else if (UserType.m_Type == "Symbol")
							_Builder.appendSymbol(fg_ToStringData(Name), fg_ToStringData(UserType.m_Value.f_String()));
						else if (UserType.m_Type == "Code")
							_Builder.appendCode(fg_ToStringData(Name), fg_ToStringData(UserType.m_Value.f_String()));
						else if (UserType.m_Type == "DBRef")
						{
							_Builder.appendDBRef
								(
									fg_ToStringData(Name)
									, fg_ToStringData(UserType.m_Value.f_GetMember("NS")->f_String())
									, OID(UserType.m_Value.f_GetMember("ObjectID")->f_String().f_GetStr())
								)
							;
						}
						else if (UserType.m_Type == "RegEx")
						{
							_Builder.appendRegex
								(
									fg_ToStringData(Name)
									, fg_ToStringData(UserType.m_Value.f_GetMember("Regex")->f_String())
									, fg_ToStringData(UserType.m_Value.f_GetMember("RegexFlags")->f_String())
								)
							;
						}
						else if (UserType.m_Type == "jstOID")
						{
							OID ObjectID(UserType.m_Value.f_String().f_GetStr());
							_Builder.appendOID(fg_ToStringData(Name), &ObjectID);
						}
						else if (UserType.m_Type == "Undefined")
							_Builder.appendUndefined(fg_ToStringData(Name));
						else
							_Builder.append(fg_ToStringData(Name), fg_ToBSONImpl(Value.f_ToJSON()));
					}
					break;
				case NEncoding::EEJSONType_Binary:
					_Builder.appendBinData(fg_ToStringData(Name), Value.f_Binary().f_GetLen(), BinDataGeneral, Value.f_Binary().f_GetArray());
					break;
				default:
					fg_ToBSONMemberCommon(_Builder, _Name, _Value);
					break;
				}
			}

			template <typename tf_CJSON>
			void fg_ToBSONMember(BSONObjBuilder &_Builder, NStr::CStr const &_Name, tf_CJSON const &_Value)
			{
				return fg_ToBSONMemberCommon(_Builder, _Name, _Value);
			}

			template <typename tf_CJSON>
			void fg_ToBSONImplArray(BSONObjBuilder &_Builder, tf_CJSON const &_JSON)
			{
				mint iArray = 0;
				for (auto iMember = _JSON.f_Array().f_GetIterator(); iMember; ++iMember)
					fg_ToBSONMember(_Builder, NStr::CStr::fs_ToStr(iArray++), *iMember);
			}


			template <typename tf_CJSON>
			void fg_ToBSONImpl(BSONObjBuilder &_Builder, tf_CJSON const &_JSON)
			{
				for (auto iMember = _JSON.f_Object().f_OrderedIterator(); iMember; ++iMember)
					fg_ToBSONMember(_Builder, iMember->f_Name(), iMember->f_Value());
			}


			template <typename tf_CJSON>
			BSONObj fg_ToBSONImpl(tf_CJSON const &_JSON)
			{
				switch (_JSON.f_Type())
				{
				case NEncoding::EJSONType_Object:
					{
						BSONObjBuilder Builder;
						fg_ToBSONImpl(Builder, _JSON);
						return Builder.obj();
					}
					break;
				case NEncoding::EJSONType_Array:
					{
						BSONObjBuilder Builder;
						fg_ToBSONImplArray(Builder, _JSON);
						return Builder.obj();
					}
					break;
				case NEncoding::EJSONType_Invalid:
					{
						return BSONObj();
					}
					break;
				default:
					DMibError("Unsupported BSON root type");
				}
			}
			
			void fg_FromBSONImp(NEncoding::CEJSON &_JSON, BSONObj const &_BSON);
			void fg_FromBSONImp(NEncoding::CEJSON &_JSON, BSONArray const &_BSON);

			void fg_FromBSONImp(NEncoding::CEJSON &_JSON, BSONElement const &_Element)
			{
				switch (_Element.type())
				{
				case NumberDouble:
					_JSON = _Element._numberDouble();
					break;
				case String:
					_JSON = NStr::CStr(_Element.valuestr(), _Element.valuestrsize() - 1);
					break;
				case Object:
					{
						fg_FromBSONImp(_JSON, _Element.embeddedObject());
					}
					break;
				case Array:
					{
						auto Object = _Element.embeddedObject();
						fg_FromBSONImp(_JSON, static_cast<BSONArray const &>(Object));
					}
					break;
				case Bool:
					{
						_JSON = _Element.boolean();
					}
					break;
				case Date:
					{
						_JSON = NTime::CTimeConvert::fs_FromCreateFromUnixMilliseconds(_Element.date());
					}
					break;
				case jstNULL:
					{
						_JSON = nullptr;
					}
					break;
				case NumberInt:
					_JSON = int64(_Element._numberInt());
					break;
				case NumberLong:
					_JSON = int64(_Element._numberLong());
					break;
				case BinData:
					{
						int Length;
						char const *pData = _Element.binDataClean(Length);
						NContainer::TCVector<uint8> Data;
						Data.f_Insert((uint8 const *)pData, Length);
						
						if (_Element.binDataType() == BinDataGeneral)
						{
							_JSON = fg_Move(Data);
							break;
						}
						
						_JSON = NEncoding::EEJSONType_UserType;
						auto &UserType = _JSON.f_UserType();
						
						UserType.m_Type = "BinData";
						UserType.m_Value["Data"] = NDataProcessing::fg_Base64Encode(Data);
						
						NStr::CStr Type;
						switch (_Element.binDataType())
						{
						case Function:
							Type = "Function";
							break;
						case ByteArrayDeprecated:
							Type = "ByteArrayDeprecated";
							break;
						case bdtUUID:
							Type = "bdtUUID";
							break;
						case newUUID:
							Type = "newUUID";
							break;
						case MD5Type:
							Type = "MD5Type";
							break;
						case bdtCustom:
							Type = "bdtCustom";
							break;
						}
						
						UserType.m_Value["Type"] = Type;
					}
					break;
				case Undefined:
					{
						_JSON = NEncoding::EEJSONType_UserType;
						auto &UserType = _JSON.f_UserType();
						UserType.m_Type = "Undefined";
						UserType.m_Value = 1;
					}
					break;
				case jstOID:
					{
						mongo::OID ObjectID = _Element.__oid();
						
						_JSON = NEncoding::EEJSONType_UserType;
						auto &UserType = _JSON.f_UserType();
						
						UserType.m_Type = "jstOID";
						UserType.m_Value = ObjectID.toString().c_str();
					}
					break;
				case RegEx:
					{
						_JSON = NEncoding::EEJSONType_UserType;
						auto &UserType = _JSON.f_UserType();
						
						UserType.m_Type = "RegEx";
						UserType.m_Value["Regex"] = _Element.regex();
						UserType.m_Value["RegexFlags"] = _Element.regexFlags();
					}
					break;
				case DBRef:
					{
						auto pNamespace = _Element.dbrefNS();
						mongo::OID ObjectID = _Element.dbrefOID();
						
						_JSON = NEncoding::EEJSONType_UserType;
						auto &UserType = _JSON.f_UserType();
						
						UserType.m_Type = "DBRef";
						UserType.m_Value["NS"] = pNamespace;
						UserType.m_Value["ObjectID"] = ObjectID.toString().c_str();
					}
					break;
				case Code:
					{
						_JSON = NEncoding::EEJSONType_UserType;
						auto &UserType = _JSON.f_UserType();
						
						UserType.m_Type = "Code";
						UserType.m_Value = NStr::CStr(_Element.valuestr(), _Element.valuestrsize()-1);
					}
					break;
				case Symbol:
					{
						_JSON = NEncoding::EEJSONType_UserType;
						auto &UserType = _JSON.f_UserType();
						
						UserType.m_Type = "Symbol";
						UserType.m_Value = NStr::CStr(_Element.valuestr(), _Element.valuestrsize()-1);
					}
					break;
				case CodeWScope:
					{
						_JSON = NEncoding::EEJSONType_UserType;
						auto &UserType = _JSON.f_UserType();
						
						UserType.m_Type = "CodeWScope";
						UserType.m_Value["Code"] = NStr::CStr(_Element.codeWScopeCode(), _Element.codeWScopeCodeLen() - 1);
						NEncoding::CEJSON ScopeObject = fg_FromBSON(BSONObj(_Element.codeWScopeScopeData()));
						UserType.m_Value["Scope"] = ScopeObject.f_ToJSON();
					}
					break;
				case Timestamp:
					{
						_JSON = NEncoding::EEJSONType_UserType;
						auto &UserType = _JSON.f_UserType();
						
						UserType.m_Type = "Timestamp";
						auto Value = _Element.timestamp();
						UserType.m_Value["Seconds"] = Value.seconds();
						UserType.m_Value["Increment"] = Value.increment();
					}
					break;
				case MinKey:
					{
						_JSON = NEncoding::EEJSONType_UserType;
						auto &UserType = _JSON.f_UserType();
						UserType.m_Type = "MinKey";
						UserType.m_Value = 1;
					}
					break;
				case MaxKey:
					{
						_JSON = NEncoding::EEJSONType_UserType;
						auto &UserType = _JSON.f_UserType();
						UserType.m_Type = "MaxKey";
						UserType.m_Value = 1;
					}
					break;
				default:
					DMibNeverGetHere; // Not supported
					break;
				}
			}

			void fg_FromBSONImp(NEncoding::CEJSON &_JSON, BSONObj const &_BSON)
			{
				_JSON = NEncoding::EJSONType_Object;
				
				for (auto iElement = _BSON.begin(); iElement.more(); )
				{
					BSONElement Element = iElement.next();
					auto &Member = _JSON.f_Object().f_CreateMember(Element.fieldName());
					fg_FromBSONImp(Member, Element);
				}
			}

			void fg_FromBSONImp(NEncoding::CEJSON &_JSON, BSONArray const &_BSON)
			{
				_JSON = NEncoding::EJSONType_Array;
				
				for (auto iElement = _BSON.begin(); iElement.more(); )
				{
					BSONElement Element = iElement.next();
					auto &Member = _JSON.f_Array().f_Insert();
					fg_FromBSONImp(Member, Element);
				}
			}
		}
		BSONObj fg_ToBSON(NEncoding::CEJSON const &_JSON)
		{
			return fg_ToBSONImpl<NEncoding::CEJSON>(_JSON);
		}
		
		NEncoding::CEJSON fg_FromBSON(BSONObj const &_BSON)
		{
			NEncoding::CEJSON Ret;
			if (_BSON.couldBeArray())
				fg_FromBSONImp(Ret, static_cast<BSONArray const &>(_BSON));
			else
				fg_FromBSONImp(Ret, _BSON);
			return Ret;
		}
	}
}
