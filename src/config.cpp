#include "config.h"
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{

const char *Config::GLOBAL_SECTION = "__Global_Section__";

bool Config::parse(const char *cfgUri)
{
    spdlog::critical("[Config::parse] not implemented yet.");
    return true;
}

std::string Config::get(const char *_key, const char *_section)
{
    spdlog::critical("[Config::get] not implemented yet.");
    string strRet;
    return strRet;
}

} // namespace mapper
