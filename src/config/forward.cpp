
#include "forward.h"
#include <assert.h>
#include <regex>
#include <sstream>
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{
namespace config
{

bool Forward::parse(string &setting)
{
    try
    {
        const char *REG_FULL = R"(^\s*(((tcp|udp):)?(\w*):)?(\d{1,5}):)"
                               R"(((\d{1,3}\.){3}\d{1,3}):(\d{1,5})\s*$)";

        regex re(REG_FULL);
        smatch match;
        if (regex_match(setting, match, re))
        {
            protocol = match[2];
            interface = match[3];
            servicePort = atoi(match[4].str().c_str());
            targetHost = match[5];
            targetPort = atoi(match[7].str().c_str());

            protocol = protocol.empty() ? "tcp" : protocol;
            interface = interface.empty() ? "any" : interface;

            return true;
        }
        else
        {
            spdlog::error("[Forward::parse] invalid forward setting: [{}]", setting);
            return false;
        }
    }
    catch (regex_error &e)
    {
        spdlog::error("[Forward::parse] catch an exception when parse[{}]: [{}]", setting, e.what());
        return false;
    }
}

string Forward::toStr()
{
    stringstream ss;

    ss << "["
       << protocol << ":"
       << interface << ":"
       << servicePort << ":"
       << targetHost << ":"
       << targetPort << "]";

    return ss.str();
}

} // namespace config
} // namespace mapper
