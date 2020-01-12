#include "jsonUtils.h"
#include <fstream>
#include <rapidjson/error/en.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/stringbuffer.h>

using namespace std;
using namespace rapidjson;

namespace mapper
{
namespace utils
{

bool JsonUtils::parse(Document &doc, string configFile, string schema, stringstream *errmsg)
{
    ifstream ifsConfig(configFile);
    if (ifsConfig)
    {
        IStreamWrapper isw(ifsConfig);
        ParseResult ok = doc.ParseStream(isw);
        if (ok)
        {
            return schema.length()
                       ? validate(doc, schema, errmsg)
                       : true;
        }
        else
        {
            if (errmsg)
            {
                *errmsg << "parse config[" << configFile << "] fail: "
                        << ok.Code() << ", " << GetParseError_En(ok.Code())
                        << ", offset: " << ok.Offset() << endl;
            }
        }
    }
    else
    {
        if (errmsg)
        {
            *errmsg << "open config[" << configFile << "] fail: "
                    << errno << ", " << strerror(errno) << endl;
        }
    }

    return false;
}

bool JsonUtils::validate(Document &doc, string schema, stringstream *errmsg)
{
    Document d;
    ParseResult ok = d.Parse(schema.c_str());
    if (ok)
    {
        SchemaDocument sd(d);
        SchemaValidator validator(sd);

        if (validator.IsValid())
        {
            if (doc.Accept(validator))
            {
                return true;
            }
            if (errmsg)
            {
                StringBuffer sb;
                validator.GetInvalidSchemaPointer().StringifyUriFragment(sb);
                *errmsg << "Invalid schema: " << sb.GetString() << endl
                        << "Invalid keyword: " << validator.GetInvalidSchemaKeyword() << endl;

                sb.Clear();
                validator.GetInvalidDocumentPointer().StringifyUriFragment(sb);
                *errmsg << "Invalid document: " << sb.GetString() << endl;
            }
        }
    }

    return false;
}

bool JsonUtils::validate(Value *value, string schema, stringstream *errmsg)
{
    Document doc;
    doc.CopyFrom(*value, doc.GetAllocator());

    return validate(doc, schema, errmsg);
}

string JsonUtils::get(Document &doc, string path, string defaultValue)
{
    auto value = Pointer(path.c_str()).Get(doc);
    return value ? (value->IsString()
                        ? value->GetString()
                        : defaultValue)
                 : defaultValue;
}

int32_t JsonUtils::getAsInt32(Document &doc, string path, int32_t defaultValue)
{
    auto value = Pointer(path.c_str()).Get(doc);
    return value ? (value->IsInt() && !value->IsUint()
                        ? value->GetInt()
                        : defaultValue)
                 : defaultValue;
}

uint32_t JsonUtils::getAsUint32(Document &doc, string path, uint32_t defaultValue)
{
    auto value = Pointer(path.c_str()).Get(doc);
    return value ? (value->IsInt()
                        ? value->GetInt()
                        : defaultValue)
                 : defaultValue;
}

Value *JsonUtils::getObj(Value *value, string path)
{
    return Pointer(path.c_str()).Get(*value);
}

Value *JsonUtils::getArray(Value *value, string path)
{
    auto array = getObj(value, path);
    return array ? (array->IsArray() ? array : nullptr) : nullptr;
}

bool JsonUtils::isExist(Document &doc, string path)
{
    return getObj(&doc, path);
}

} // namespace utils
} // namespace mapper
