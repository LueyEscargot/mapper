
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

Forward::Forward(const std::string &protocol,
                 const std::string &interface,
                 const uint32_t servicePort,
                 const std::string &targetHost,
                 const uint32_t targetPort)
{
    init(protocol,
         interface,
         servicePort,
         targetHost,
         targetPort);
}

Forward::Forward(const Forward &src)
{
    init(src.protocol,
         src.interface,
         src.servicePort,
         src.targetHost,
         src.targetPort);
}

Forward::Forward(const Forward *src)
{
    init(src->protocol,
         src->interface,
         src->servicePort,
         src->targetHost,
         src->targetPort);
}

Forward &Forward::operator=(const Forward &src)
{
    init(src.protocol,
         src.interface,
         src.servicePort,
         src.targetHost,
         src.targetPort);
}

void Forward::init(const std::string &protocol,
                   const std::string &interface,
                   const uint32_t servicePort,
                   const std::string &targetHost,
                   const uint32_t targetPort)
{
    this->protocol = protocol;
    this->interface = interface;
    this->servicePort = servicePort;
    this->targetHost = targetHost;
    this->targetPort = targetPort;

    spdlog::trace("[Forward::init] {}", toStr());
}

shared_ptr<Forward> Forward::create(string setting)
{
    shared_ptr<Forward> pForward = make_shared<Forward>();
    if (!pForward)
    {
        spdlog::error("[Forward::create] create object fail");
        return nullptr;
    }

    if (!pForward->parse(setting))
    {
        spdlog::error("[Forward::create] parse [{}] fail", setting);
        return nullptr;
    }

    return pForward;
}

void Forward::release(shared_ptr<Forward> pForward)
{
    if (pForward)
    {
        pForward.reset();
    }
}

bool Forward::parse(string &setting)
{
    try
    {
        auto REG_FORWARD_SETTING = regex(R"(^\s*)"
                                         R"((((tcp|udp):)?([A-Za-z0-9._-]*):)?)" // protocol & interface
                                         R"((\d{1,5})\s*:)"                      // service port
                                         R"(\s*([A-Za-z0-9._-]+)\s*:)"           // target host
                                         R"(\s*(\d{1,5}))"                       // target port
                                         R"(\s*$)");
        smatch match;

        if (regex_match(setting, match, REG_FORWARD_SETTING))
        {
            // int i = 0;
            // for (auto item : match)
            // {
            //     spdlog::debug("[asdf] match[{}]: {}", i++, item.str());
            // }

            assert(match.size() == 8);
            string strProtocol = match[3];
            string strInterface = match[4];
            string strSport = match[5];
            string strIp = match[6];
            string strDport = match[7];

            strProtocol = strProtocol.empty() ? "tcp" : strProtocol;    // default protocl: tcp
            strInterface = strInterface.empty() ? "any" : strInterface; // default interface: any
            int sport = atoi(strSport.c_str());
            int dport = atoi(strDport.c_str());

            if (sport <= 0 || 0x10000 <= sport)
            {
                spdlog::error("[Config::parseLine] drop invalid mapping data(sport): {}", setting);
                return false;
            }
            else if (dport <= 0 || 0x10000 <= dport)
            {
                spdlog::error("[Config::parseLine] drop invalid mapping data(dport): {}", setting);
                return false;
            }
            else
            {
                init(strProtocol, strInterface, sport, strIp, dport);
                return true;
            }
        }
        else
        {
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
