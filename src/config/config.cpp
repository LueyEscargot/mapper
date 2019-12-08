#include "config.h"
#include <errno.h>
#include <stdlib.h>
#include <fstream>
#include <string.h>
#include <sstream>
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{
namespace config
{

const char *Config::DEFAULT_CONFIG_FILE = "config.ini";
const char *Config::GLOBAL_SECTION = "global";

regex Config::REG_FOR_TRIM_LEADING_SPACE = regex(R"(^\s+)");
regex Config::REG_FOR_TRIM_TAIL_SPACE = regex(R"(\s+$)");
regex Config::REG_FOR_TRIM_COMMENTS = regex(R"(\s*(?:#.*$))");
regex Config::REG_SECTION = regex(R"(^\s*\[\s*(\w+)\s*]\s*$)");
regex Config::REG_CONFIG = regex(R"(^\s*(\w+)\s*=\s*(\w+)\s*$)");
regex Config::REG_VALID_UNSIGNED_NUMBER = regex(R"(^\s*\d+\s*$)");

Config::Config()
    : mConfigFile(DEFAULT_CONFIG_FILE),
      mCurSection(GLOBAL_SECTION)
{
}

Config::Config(int argc, char *argv[])
    : mCurSection(GLOBAL_SECTION),
      mAppName(argv[0])
{
    parse(argc, argv);
}

std::string Config::getSyntax()
{
    stringstream ss;

    ss << mAppName << " -c config -m mapData -s maxSessions -h" << endl
       << "    -c config: config file, default is .\\config.ini" << endl
       << "    -m mapData: sport:host:dport, for example: 8000:192.168.1.22:8000" << endl
       << "    -s maxSession: max client sessions, default is 1024." << endl
       << "    -c concurrency: max concurrent forked-processes, default is 0 - as many as cpu cores." << endl
       << "    -l log: trace, debug, info(default), warn, err, critical, off." << endl
       << "    -h show this help message.";

    return ss.str();
}

bool Config::parse(int argc, char *argv[])
{
    // get config file
    const char *fileName = getArg(argv, argv + argc, "-c");
    mConfigFile = fileName[0] ? fileName : DEFAULT_CONFIG_FILE;

    // parse config file
    if (!parse(mConfigFile.c_str()))
    {
        spdlog::error("[asdf] parse config file[{}] fail", mConfigFile);
        return false;
    }

    // add command line forward settings
    for (int i = 1; i < argc - 1; ++i)
    {
        if (strcasecmp(argv[i], "-m") == 0)
        {
            // parse and add forward settings
            shared_ptr<Forward> pForward = Forward::create(argv[++i]);
            if (pForward)
            {
                // save into global section
                mForwards[GLOBAL_SECTION].insert(move(pForward));
            }
        }
    }
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

vector<std::shared_ptr<Forward>> Config::getForwards(std::string section)
{
    std::vector<std::shared_ptr<Forward>> settings;
    if (section.compare("*") == 0)
    {
        // select all forward settings
        for (auto it : mForwards)
        {
            for (auto forward : it.second)
            {
                settings.push_back(forward);
            }
        }
    }
    else
    {
        // select forward settings which belong to specified section
        auto it = mForwards.find(section);
        if (it != mForwards.end())
        {
            for (auto forward : it->second)
            {
                settings.push_back(forward);
            }
        }
    }

    return move(settings);
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
    else if (shared_ptr<Forward> pForward = Forward::create(line))
    {
        mForwards[mCurSection].insert(move(pForward));
    }
    else
    {
        // drop
        spdlog::debug("[Config::parseLine] drop: [{}]", line.c_str());
    }
}

} // namespace config
} // namespace mapper
