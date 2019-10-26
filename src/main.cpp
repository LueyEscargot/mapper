/**
 * @file main.cpp
 * @author Liu Yu (source@liuyu.com)
 * @brief main
 * @version 0.1
 * @date 2019-10-07
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#include <stdlib.h>
#include <stdio.h>
#include <exception>
#include <vector>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "config.h"
#include "define.h"
#include "mapper.h"

static const char *CONFIG_FILE = "config.ini";
static const char *LOG_FILE = "mapper.log";
static const auto LOG_LEVEL = spdlog::level::debug;

void init();
mapper::Config gConf;
mapper::Mapper gMapper;

int main(int argc, char *argv[])
{
    init();

    spdlog::info("[main] Start");

    // run mapper
    spdlog::debug("[main] run mapper");
    if (!gMapper.run(mapper::MAX_SESSIONS, &gConf.getMapData()))
    {
        spdlog::error("[main] run mapper fail");
        std::exit(EXIT_FAILURE);
    }

    spdlog::info("[main] Stop");

    return EXIT_SUCCESS;
}

void init()
{
    try
    {
        if (!gConf.parse(CONFIG_FILE))
        {
            printf("[init] load config file[%s] fail", CONFIG_FILE);
            std::exit(EXIT_FAILURE);
        }

        // init logger
        {
            auto level = spdlog::level::from_str(gConf.get("level", "log", "info"));

            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(LOG_FILE, true);
            spdlog::set_default_logger(
                std::make_shared<spdlog::logger>(
                    "mapper", spdlog::sinks_init_list({console_sink, file_sink})));
            spdlog::set_level(level);
            spdlog::flush_on(level);
        }
    }
    catch (std::exception &e)
    {
        printf("[init] catch an exception: %s", e.what());
        std::exit(EXIT_FAILURE);
    }
}
