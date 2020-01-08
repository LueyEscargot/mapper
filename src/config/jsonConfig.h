/**
 * @file jsonConfig.h
 * @author Liu Yu (source@liuyu.com)
 * @brief Class for parse config data
 * @version 1.0
 * @date 2020-01-08
 * 
 * @copyright Copyright (c) 2020
 * 
 */
#ifndef __MAPPER_CONFIG_JSONCONFIG_H__
#define __MAPPER_CONFIG_JSONCONFIG_H__

#include <algorithm>
#include <sstream>
#include <string>
#include <rapidjson/document.h>
#include <rapidjson/schema.h>

namespace mapper
{
namespace config
{

class JsonConfig
{
protected:
    static const char *DEFAULT_CONFIG_FILE;
    static const char *DEFAULT_SCHEMA_FILE;

public:
    JsonConfig() {};

    bool parse(int argc, char *argv[]);
    bool parse(std::string configFile, std::string schemaFile);
    bool parse(std::string configFile, std::string schemaFile, std::stringstream & ss);
    std::string get(std::string path, std::string defaultValue = "");
    int32_t getAsInt32(std::string path, int32_t defaultValue = 0);
    uint32_t getAsUint32(std::string path, uint32_t defaultValue = 0);
    rapidjson::Value *getObj(std::string path);

    static bool validate(rapidjson::Document &doc, const char * schema);
    static bool validate(rapidjson::Document &doc, const char * schema, std::stringstream & ss);
    static std::string get(rapidjson::Document &doc, std::string path, std::string defaultValue = "");
    static int32_t getAsInt32(rapidjson::Document &doc, std::string path, int32_t defaultValue = 0);
    static uint32_t getAsUint32(rapidjson::Document &doc, std::string path, uint32_t defaultValue = 0);
    static rapidjson::Value *getObj(rapidjson::Document &doc, std::string path);

protected:
    inline bool hasArg(char **begin, char **end, const std::string &arg) { return std::find(begin, end, arg) != end; }
    inline const char *getArg(char **begin, char **end, const std::string &arg)
    {
        char **itr = std::find(begin, end, arg);
        return itr != end && ++itr != end ? *itr : "";
    }

    rapidjson::Document mRoot;
};

} // namespace config
} // namespace mapper

#endif // __MAPPER_CONFIG_JSONCONFIG_H__
