#include "mapper.h"
#include <time.h>
#include <spdlog/spdlog.h>

using namespace std;
using namespace mapper::link;

namespace mapper
{

const uint32_t Mapper::STATISTIC_INTERVAL = 1;

Mapper::Mapper()
    : mStop(false)
{
}

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

    spdlog::trace("[Mapper::run] start statistic routine");
    while (!mStop)
    {
        this_thread::sleep_for(chrono::seconds(STATISTIC_INTERVAL));

        time_t curTime = time(nullptr);
        for (auto &service : mServiceList)
        {
            spdlog::info("{}: {}", service->name(), service->getStatistic(curTime));
            service->resetStatistic();
        };
    }
    spdlog::trace("[Mapper::run] stop statistic routine");

    spdlog::trace("[Mapper::run] join sub services");
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
    mStop = true;

    // close services
    spdlog::trace("[Mapper::stop] stop services.");
    for (auto &service : mServiceList)
    {
        service->stop();
    }
    spdlog::debug("[Mapper::stop] services closed");
}

} // namespace mapper
