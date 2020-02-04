#include "mapper.h"
#include <spdlog/spdlog.h>

using namespace std;
using namespace mapper::link;

namespace mapper
{

bool Mapper::run(rapidjson::Document &cfg)
{
    spdlog::info("[Mapper::run] start.");

    // create services
    spdlog::trace("[Mapper::run] create services");
    if (!Service::create(cfg, mServiceList))
    {
        spdlog::error("[Mapper::run] create services fail.");
        return false;
    }

    spdlog::trace("[Mapper::run] join services");
    join();

    spdlog::info("[Mapper::run] stop.");

    return true;
}

void Mapper::join()
{
    // join net manager
    spdlog::trace("[Mapper::join] start join services.");
    for (auto &service : mServiceList)
    {
        service->join();
        spdlog::debug("[Mapper::join] close service[{}]", service->name());
        service->close();
    }
    spdlog::debug("[Mapper::join] stop all join");
}

void Mapper::stop()
{
    // close services
    spdlog::trace("[Mapper::stop] stop services.");
    for (auto &service : mServiceList)
    {
        service->stop();
    }
    spdlog::debug("[Mapper::stop] services closed");
}

} // namespace mapper
