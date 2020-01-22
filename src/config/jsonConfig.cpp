// #include "jsonConfig.h"
// #include "string.h"
// #include <fstream>
// #include <rapidjson/error/en.h>
// #include <rapidjson/istreamwrapper.h>
// #include <rapidjson/pointer.h>
// #include <rapidjson/stringbuffer.h>
// #include "schema.def"

// using namespace std;
// using namespace rapidjson;

// namespace mapper
// {
// namespace config
// {

// const char *JsonConfig::DEFAULT_CONFIG_FILE = "config.json";

// bool JsonConfig::parse(int argc, char *argv[], stringstream *ss)
// {
//     // get config and schema file
//     const char *val;
//     string configFile = strlen(val = getArg(argv, argv + argc, "-c"))
//                             ? val
//                             : DEFAULT_CONFIG_FILE;

//     // parse config file
//     return parse(configFile, ss);
// }

// bool JsonConfig::parse(string configFile, stringstream *ss)
// {
//     ifstream ifsConfig(configFile);
//     if (ifsConfig)
//     {
//         IStreamWrapper isw(ifsConfig);
//         ParseResult ok = mRoot.ParseStream(isw);
//         if (ok)
//         {
//             if (validate(mRoot, CONFIG_SCHEMA, ss))
//             {
//                 return true;
//             }
//         }
//         else
//         {
//             if (ss)
//             {
//                 *ss << "parse config[" << configFile << "] fail: "
//                     << ok.Code() << ", " << GetParseError_En(ok.Code())
//                     << ", offset: " << ok.Offset() << endl;
//             }
//         }
//     }
//     else
//     {
//         if (ss)
//         {
//             *ss << "open config[" << configFile << "] fail: "
//                 << errno << ", " << strerror(errno) << endl;
//         }
//     }

//     return false;
// }

// string JsonConfig::get(string path, string defaultValue)
// {
//     return get(mRoot, path, defaultValue);
// }

// int32_t JsonConfig::getAsInt32(string path, int32_t defaultValue)
// {
//     return getAsInt32(mRoot, path, defaultValue);
// }

// uint32_t JsonConfig::getAsUint32(string path, uint32_t defaultValue)
// {
//     return getAsUint32(mRoot, path, defaultValue);
// }

// Value *JsonConfig::getObj(string path)
// {
//     return getObj(mRoot, path);
// }

// bool JsonConfig::validate(Document &doc, const char *schema)
// {
//     Document d;
//     ParseResult ok = d.Parse(schema);
//     if (ok)
//     {
//         SchemaDocument sd(d);
//         SchemaValidator validator(sd);

//         return validator.IsValid() && doc.Accept(validator);
//     }

//     return false;
// }

// bool JsonConfig::validate(Document &doc, const char *schema, stringstream *ss)
// {
//     Document d;
//     ParseResult ok = d.Parse(schema);
//     if (ok)
//     {
//         SchemaDocument sd(d);
//         SchemaValidator validator(sd);

//         if (validator.IsValid())
//         {
//             if (doc.Accept(validator))
//             {
//                 return true;
//             }
//             if (ss)
//             {
//                 StringBuffer sb;
//                 validator.GetInvalidSchemaPointer().StringifyUriFragment(sb);
//                 *ss << "Invalid schema: " << sb.GetString() << endl
//                     << "Invalid keyword: " << validator.GetInvalidSchemaKeyword() << endl;

//                 sb.Clear();
//                 validator.GetInvalidDocumentPointer().StringifyUriFragment(sb);
//                 *ss << "Invalid document: " << sb.GetString() << endl;
//             }
//         }
//     }

//     return false;
// }

// bool JsonConfig::validate(Value *value, const char *schema)
// {
//     Document doc;
//     doc.CopyFrom(*value, doc.GetAllocator());

//     return validate(doc, schema);
// }

// bool JsonConfig::validate(Value *value, const char *schema, stringstream *ss)
// {
//     Document doc;
//     doc.CopyFrom(*value, doc.GetAllocator());

//     return validate(doc, schema, ss);
// }

// string JsonConfig::get(Document &doc, string path, string defaultValue)
// {
//     auto value = Pointer(path.c_str()).Get(doc);
//     return value->IsString() ? value->GetString() : defaultValue;
// }

// int32_t JsonConfig::getAsInt32(Document &doc, string path, int32_t defaultValue)
// {
//     auto value = Pointer(path.c_str()).Get(doc);
//     return value->IsInt() && !value->IsUint() ? value->GetInt() : defaultValue;
// }

// uint32_t JsonConfig::getAsUint32(Document &doc, string path, uint32_t defaultValue)
// {
//     auto value = Pointer(path.c_str()).Get(doc);
//     return value->IsInt() ? value->GetInt() : defaultValue;
// }

// Value *JsonConfig::getObj(Value *value, string path)
// {
//     return Pointer(path.c_str()).Get(*value);
// }

// bool JsonConfig::isExist(Document &doc, std::string path)
// {
//     return getObj(&doc, path);
// }

// } // namespace config
// } // namespace mapper
