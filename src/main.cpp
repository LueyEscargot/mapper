/**
 * @file main.cpp
 * @author Liu Yu (source@liuyu.com)
 * @brief main
 * @version 0.1
 * @date 2019-10-07
 * 
 * @copyright Copyright (c) 2019-2020
 * 
 */
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <algorithm>
#include <exception>
#include <sstream>
#include <vector>
#include <sys/sysinfo.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "schema.def"
#include "utils/jsonUtils.h"
#include "mapper.h"
#include "project.h"

static const char *CONFIG_FILE = "config.json";
static const char *LOG_FILE = "mapper.log";
static const auto LOG_LEVEL = spdlog::level::debug;

void showSantax(char *argv[]);
bool hasArg(char **begin, char **end, const std::string &arg);
const char *getArg(char **begin, char **end, const std::string &arg);
bool loadCfg(rapidjson::Document &doc, int argc, char *argv[]);
void onSigHandler(int s);
void setSigHandler();

mapper::Mapper *gpMapper = nullptr;

int main(int argc, char *argv[])
{
    // show santax
    if (hasArg(argv, argv + argc, "-h"))
    {
        showSantax(argv);
        return 0;
    }

    // set signal handler
    setSigHandler();

    // load config
    rapidjson::Document cfg;
    if (!loadCfg(cfg, argc, argv))
    {
        fprintf(stderr, "load config fail\n");
        std::exit(EXIT_FAILURE);
    }

    // init log
    auto sinkStr = mapper::utils::JsonUtils::get(cfg, "/log/sink");
    if (sinkStr.compare("file") == 0)
    {
        auto file = mapper::utils::JsonUtils::get(cfg, "/log/file");
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(file, true);
        spdlog::set_default_logger(
            std::make_shared<spdlog::logger>("mapper",
                                             spdlog::sinks_init_list({sink})));
    }
    else
    {
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        spdlog::set_default_logger(
            std::make_shared<spdlog::logger>("mapper",
                                             spdlog::sinks_init_list({sink})));
    }
    auto level = spdlog::level::from_str(mapper::utils::JsonUtils::get(cfg, "/log/level"));
    spdlog::set_level(level);
    spdlog::flush_on(level);

    // init config object
    // mapper::config::Config cfg(argc, argv);

    spdlog::trace("{}", projectDesc());
    spdlog::trace("Available Processors: {}", get_nprocs());
    spdlog::info("Mapper Start");

    // run mapper
    spdlog::debug("[main] run mapper");
    gpMapper = new mapper::Mapper();
    if (!gpMapper->run(cfg))
    {
        spdlog::error("[main] run mapper fail");
        std::exit(EXIT_FAILURE);
    }
    delete gpMapper;
    gpMapper = nullptr;

    spdlog::info("[main] Stop");

    return EXIT_SUCCESS;
}

void showSantax(char *argv[])
{
    printf("\n");
    printf("%s -c config [-n config] [-s schema] -h\n", argv[0]);
    printf("    -c config: config file, in JSON format, default is ./config.json\n");
    printf("    -n config: create new empty config file\n");
    printf("    -s schema: schema file, in JSON format, default is ./schema.json\n");
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

void onSigHandler(int s)
{
    switch (s)
    {
    case SIGINT:
        spdlog::info("[main] recv signal SIGINT(ctrl-c).");
        if (gpMapper)
        {
            gpMapper->stop();
        }
        break;
    case SIGPIPE:
        // skip
        break;
    default:
        spdlog::warn("[main] receive signal[{}]. drop it", s);
    }
}

void setSigHandler()
{
    // disable signal: SIGPIPE
    signal(SIGINT, SIG_IGN);
    // handle signal: SIGINT
    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = onSigHandler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, NULL);
    sigaction(SIGPIPE, &sigIntHandler, NULL);
}

bool loadCfg(rapidjson::Document &doc, int argc, char *argv[])
{
    std::string fileName = hasArg(argv, argv + argc, "-c")
                               ? getArg(argv, argv + argc, "-c")
                               : CONFIG_FILE;

    // parse config
    std::stringstream errmsg;
    if (mapper::utils::JsonUtils::parse(doc,
                                        fileName,
                                        mapper::CONFIG_SCHEMA,
                                        &errmsg))
    {
        return true;
    }
    else
    {
        fprintf(stderr, "load config fail:\n%s\n", errmsg.str().c_str());
        return false;
    }
}
