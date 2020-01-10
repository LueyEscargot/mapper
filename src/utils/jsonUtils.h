/**
 * @file jsonUtils.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Class for JSON Utils
 * @version 1.0
 * @date 2020-01-09
 * 
 * @copyright Copyright (c) 2020
 * 
 */
#ifndef __MAPPER_UTILS_JSONUTILS_H__
#define __MAPPER_UTILS_JSONUTILS_H__

#include <sstream>
#include <string>
#include <rapidjson/document.h>
#include <rapidjson/schema.h>

namespace mapper
{
namespace utils
{

class JsonUtils
{
public:
    JsonUtils(){};

    static bool parse(rapidjson::Document &doc,
                      std::string configFile,
                      std::string schema,
                      std::stringstream *errmsg = nullptr);

    static bool validate(rapidjson::Document &doc, std::string schema, std::stringstream *errmsg = nullptr);
    static bool validate(rapidjson::Value *value, std::string schema, std::stringstream *errmsg = nullptr);

    static std::string get(rapidjson::Document &doc, std::string path, std::string defaultValue = "");
    static int32_t getAsInt32(rapidjson::Document &doc, std::string path, int32_t defaultValue = 0);
    static uint32_t getAsUint32(rapidjson::Document &doc, std::string path, uint32_t defaultValue = 0);
    static rapidjson::Value *getObj(rapidjson::Value *value, std::string path);
    static rapidjson::Value *getArray(rapidjson::Value *value, std::string path);
 
    static bool isExist(rapidjson::Document &doc, std::string path);
};

} // namespace utils
} // namespace mapper

#endif // __MAPPER_UTILS_JSONUTILS_H__
