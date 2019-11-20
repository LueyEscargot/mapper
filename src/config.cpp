#include "config.h"
#include <errno.h>
#include <stdlib.h>
#include <fstream>
#include <string.h>
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{

const char *Config::GLOBAL_SECTION = "__Global_Section__";

regex Config::REG_FOR_TRIM_LEADING_SPACE = regex(R"(^\s+)");
regex Config::REG_FOR_TRIM_TAIL_SPACE = regex(R"(\s+$)");
regex Config::REG_FOR_TRIM_COMMENTS = regex(R"(\s*(?:#.*$))");
regex Config::REG_SECTION = regex(R"(^\s*\[\s*(\w+)\s*]\s*$)");
regex Config::REG_CONFIG = regex(R"(^\s*(\w+)\s*=\s*(\w+)\s*$)");
regex Config::REG_MAPPING = regex(R"(^\s*(\d{1,5})\s*:\s*((\d{1,3}\.){3}\d{1,3})\s*:\s*(\d{1,5})\s*$)");
regex Config::REG_VALID_IPV4 = regex(R"(((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?))");
regex Config::REG_VALID_UNSIGNED_NUMBER = regex(R"(^\s*\d+\s*$)");

Config::Config()
    : mCurSection(GLOBAL_SECTION)
{
}

bool Config::parse(const char *file, bool silence)
{
    // parse config file
    {
        ifstream ifs(file);
        if (!ifs.is_open())
        {
            if (!silence)
            {
                char cwd[PATH_MAX];
                spdlog::warn("[Config::parse] can not open config file[{}]. {} - {}",
                             file, errno, strerror(errno));
            }
            return false;
        }

        string line;
        while (getline(ifs, line))
        {
            parseLine(line);
        }
    }

    return true;
}

void Config::parse(const char *file, vector<string> &argMapData)
{
    // parse config file
    bool silence = !argMapData.empty();
    parse(file, silence);

    // parse map data from args
    for (auto mapData : argMapData)
    {
        parseLine(mapData);
    }
}

string Config::get(string key, string section, string defaultValue)
{
    if (section.empty())
    {
        section = GLOBAL_SECTION;
    }

    // get settings from specified section
    auto sec = mConfig.find(section);
    if (sec == mConfig.end())
    {
        return defaultValue;
    }
    auto settings = sec->second;

    // get value from settings
    auto setting = settings.find(key);
    if (setting == settings.end())
    {
        return defaultValue;
    }

    return setting->second;
}

uint32_t Config::getAsUint32(std::string key, std::string section, uint32_t defaultValue)
{
    string sVal = get(key, section);
    smatch m;
    if (regex_match(sVal, m, REG_VALID_UNSIGNED_NUMBER))
    {
        return atoi(sVal.c_str());
    }
    return defaultValue;
}

vector<MapData_t> Config::getMapData()
{
    std::vector<MapData_t> mapDatas;
    for (auto entry : mRawMapData)
    {
        auto &sport = entry.first;
        auto &host = entry.second.first;
        auto &dport = entry.second.second;

        mapDatas.emplace_back(sport, host, dport);
    }

    return mapDatas;
}

void Config::parseLine(string &line)
{
    // trim
    line = regex_replace(line, REG_FOR_TRIM_LEADING_SPACE, "");
    line = regex_replace(line, REG_FOR_TRIM_TAIL_SPACE, "");
    line = regex_replace(line, REG_FOR_TRIM_COMMENTS, "");
    if (line.empty())
    {
        return;
    }

    smatch m;
    if (regex_match(line, m, REG_SECTION))
    {
        // section
        assert(m.size() == 2);
        mCurSection = m[1];
    }
    else if (regex_match(line, m, REG_CONFIG))
    {
        // config
        assert(m.size() == 3);
        string key = m[1];
        string value = m[2];

        mConfig[mCurSection][key] = value;
    }
    else if (regex_match(line, m, REG_MAPPING))
    {
        // mapping data
        assert(m.size() == 5);
        string strSport = m[1];
        string strIp = m[2];
        string strDport = m[4];
        int sport = atoi(strSport.c_str());
        int dport = atoi(strDport.c_str());

        if (!regex_match(strIp, m, REG_VALID_IPV4))
        {
            spdlog::debug("[Config::parseLine] drop invalid mapping data(ip): {}", line);
        }
        else if (sport <= 0 || 0x10000 <= sport)
        {
            spdlog::debug("[Config::parseLine] drop invalid mapping data(sport): {}", line);
        }
        else if (dport <= 0 || 0x10000 <= dport)
        {
            spdlog::debug("[Config::parseLine] drop invalid mapping data(dport): {}", line);
        }
        else
        {
            mRawMapData[sport] = make_pair(strIp, dport);
        }
    }
    else
    {
        // drop
        printf("[Config::parseLine] drop: [%s]", line.c_str());
    }
}

} // namespace mapper
