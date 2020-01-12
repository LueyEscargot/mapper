
#include "forward.h"
#include <assert.h>
#include <regex>
#include <sstream>
#include <spdlog/spdlog.h>

using namespace std;

namespace mapper
{
namespace link
{

const regex Forward::REG_FORWARD_SETTING = regex(R"(^\s*)"
                                                 R"((((tcp|udp):)?([A-Za-z0-9._-]*):)?)" // protocol & interface
                                                 R"((\d{1,5})\s*:)"                      // service port
                                                 R"(\s*([A-Za-z0-9._-]+)\s*:)"           // target host
                                                 R"(\s*(\d{1,5}))"                       // target port
                                                 R"(\s*$)");

Forward::Forward(const std::string &protocol,
                 const std::string &interface,
                 const std::string service,
                 const std::string &targetHost,
                 const std::string targetService)
{
    init(protocol,
         interface,
         service,
         targetHost,
         targetService);
}

Forward::Forward(const Forward &src)
{
    init(src.protocol,
         src.interface,
         src.service,
         src.targetHost,
         src.targetService);
}

Forward::Forward(const Forward *src)
{
    init(src->protocol,
         src->interface,
         src->service,
         src->targetHost,
         src->targetService);
}

Forward &Forward::operator=(const Forward &src)
{
    init(src.protocol,
         src.interface,
         src.service,
         src.targetHost,
         src.targetService);
}

void Forward::init(const std::string &protocol,
                   const std::string &interface,
                   const std::string service,
                   const std::string &targetHost,
                   const std::string targetService)
{
    this->protocol = protocol;
    this->interface = interface;
    this->service = service;
    this->targetHost = targetHost;
    this->targetService = targetService;

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
            string strService = match[5];
            string strIp = match[6];
            string strPort = match[7];

            strProtocol = strProtocol.empty() ? "tcp" : strProtocol;    // default protocl: tcp
            strInterface = strInterface.empty() ? "any" : strInterface; // default interface: any
            int sport = atoi(strService.c_str());
            int dport = atoi(strPort.c_str());

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
                init(strProtocol, strInterface, strService, strIp, strPort);
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
       << service << ":"
       << targetHost << ":"
       << targetService << "]";

    return ss.str();
}

} // namespace link
} // namespace mapper
