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
#include <algorithm>
#include <exception>
#include <vector>
#include <sys/sysinfo.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "config/config.h"
#include "mapper.h"
#include "project.h"

static const char *CONFIG_FILE = "config.ini";
static const char *LOG_FILE = "mapper.log";
static const auto LOG_LEVEL = spdlog::level::debug;

void showSantax(char *argv[]);
bool hasArg(char **begin, char **end, const std::string &arg);
const char *getArg(char **begin, char **end, const std::string &arg);
void getArgMapData(int argc, char *argv[], const char *arg, std::vector<std::string> &argMapData);

mapper::Mapper gMapper;

int main(int argc, char *argv[])
{
    // show santax
    if (hasArg(argv, argv + argc, "-h"))
    {
        showSantax(argv);
        return 0;
    }

    // get log level from command line
    std::string strLogLevel = getArg(argv, argv + argc, "-l");
    strLogLevel = strLogLevel.empty() ? "info" : strLogLevel;
    auto logLevel = spdlog::level::from_str(strLogLevel);
    // init log
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(LOG_FILE, true);
    spdlog::set_default_logger(
        std::make_shared<spdlog::logger>(
            "mapper", spdlog::sinks_init_list({console_sink, file_sink})));
    spdlog::set_level(logLevel);
    spdlog::flush_on(logLevel);

    // init config object
    mapper::config::Config cfg(argc, argv);

    // set log level by config file
    strLogLevel = cfg.get("level", "log", "");
    if (!strLogLevel.empty())
    {
        logLevel = spdlog::level::from_str(strLogLevel);
        spdlog::set_level(logLevel);
        spdlog::flush_on(logLevel);
    }

    spdlog::trace("{}", projectDesc());
    spdlog::trace("Available Processors: {}", get_nprocs());
    spdlog::info("Mapper Start");

    // run mapper
    spdlog::debug("[main] run mapper");
    if (!gMapper.run(cfg))
    {
        spdlog::error("[main] run mapper fail");
        std::exit(EXIT_FAILURE);
    }

    spdlog::info("[main] Stop");

    return EXIT_SUCCESS;
}

void showSantax(char *argv[])
{
    printf("\n");
    printf("%s -c config -m mapData -s maxSessions -h\n", argv[0]);
    printf("    -c config: config file, default is .\\config.ini\n");
    printf("    -m mapData: sport:host:dport, for example: 8000:192.168.1.22:8000\n");
    printf("    -s maxSession: max client sessions, default is 1024.\n");
    printf("    -c concurrency: max concurrent forked-processes, default is 0 - as many as cpu cores.\n");
    printf("    -h show this help message.\n");
    printf("\n");
}

bool hasArg(char **begin, char **end, const std::string &arg)
{
    return std::find(begin, end, arg) != end;
}

const char *getArg(char **begin, char **end, const std::string &arg)
{
    char **itr = std::find(begin, end, arg);
    return itr != end && ++itr != end ? *itr : "";
}

void getArgMapData(int argc, char *argv[], const char *arg, std::vector<std::string> &argMapData)
{
    for (int i = 1; i < argc - 1; ++i)
    {
        if (strcasecmp(argv[i], arg) == 0)
        {
            argMapData.emplace_back(argv[++i]);
        }
    }
}
