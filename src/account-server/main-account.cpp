/*
 *  The Mana Server
 *  Copyright (C) 2004-2010  The Mana World Development Team
 *  Copyright (C) 2010  The Mana Developers
 *
 *  This file is part of The Mana Server.
 *
 *  The Mana Server is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  any later version.
 *
 *  The Mana Server is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with The Mana Server.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include "account-server/accounthandler.hpp"
#include "account-server/serverhandler.hpp"
#include "account-server/storage.hpp"
#include "chat-server/chatchannelmanager.hpp"
#include "chat-server/chathandler.hpp"
#include "chat-server/guildmanager.hpp"
#include "chat-server/post.hpp"
#include "common/configuration.hpp"
#include "common/resourcemanager.hpp"
#include "net/bandwidth.hpp"
#include "net/connectionhandler.hpp"
#include "net/messageout.hpp"
#include "utils/logger.h"
#include "utils/processorutils.hpp"
#include "utils/stringfilter.h"
#include "utils/timer.h"

#include <cstdlib>
#include <getopt.h>
#include <signal.h>
#include <iostream>
#include <fstream>
#include <physfs.h>
#include <enet/enet.h>

using utils::Logger;

// Default options that automake should be able to override.
#define DEFAULT_LOG_FILE          "manaserv-account.log"
#define DEFAULT_STATS_FILE        "manaserv.stats"
#define DEFAULT_CONFIG_FILE       "manaserv.xml"
#define DEFAULT_ATTRIBUTEDB_FILE  "attributes.xml"

static bool running = true;        /**< Determines if server keeps running */

utils::StringFilter *stringFilter; /**< Slang's Filter */

/** Database handler. */
Storage *storage;

/** Communications (chat) message handler */
ChatHandler *chatHandler;

ChatChannelManager *chatChannelManager;
GuildManager *guildManager;
PostManager *postalManager;
BandwidthMonitor *gBandwidth;

/** Callback used when SIGQUIT signal is received. */
static void closeGracefully(int)
{
    running = false;
}

static void initializeConfiguration(std::string configPath = std::string())
{
    if (configPath.empty())
        configPath = DEFAULT_CONFIG_FILE;

    bool configFound = true;
    if (!Configuration::initialize(configPath))
    {
        configFound = false;

        // If the config file isn't the default and fail to load,
        // we try the default one with a warning.
        if (configPath.compare(DEFAULT_CONFIG_FILE))
        {
            LOG_WARN("Invalid config path: " << configPath
                     << ". Trying default value: " << DEFAULT_CONFIG_FILE ".");
            configPath = DEFAULT_CONFIG_FILE;
            configFound = true;

            if (!Configuration::initialize(configPath))
                  configFound = false;
        }

        if (!configFound)
        {
            LOG_FATAL("Refusing to run without configuration!" << std::endl
            << "Invalid config path: " << configPath << ".");
            exit(1);
        }
    }

    LOG_INFO("Using config file: " << configPath);

    // Check inter-server password.
    if (Configuration::getValue("net_password", "") == "")
        LOG_WARN("SECURITY WARNING: 'net_password' not set!");
}

/**
 * Initializes the server.
 */
static void initialize()
{
    // Reset to default segmentation fault handling for debugging purposes
    signal(SIGSEGV, SIG_DFL);

    // Used to close via process signals
#if (defined __USE_UNIX98 || defined __FreeBSD__)
    signal(SIGQUIT, closeGracefully);
#endif
    signal(SIGINT, closeGracefully);
    signal(SIGTERM, closeGracefully);

    // Set enet to quit on exit.
    atexit(enet_deinitialize);

#if defined LOG_FILE
    std::string logPath = LOG_FILE;
#else
    std::string logPath = DEFAULT_LOG_FILE;
#endif // defined LOG_FILE

    // Initialize PhysicsFS
    PHYSFS_init("");

    // Initialize the logger.
    Logger::setLogFile(logPath);

    // write the messages to both the screen and the log file.
    Logger::setTeeMode(true);

    LOG_INFO("Using log file: " << logPath);

    ResourceManager::initialize();

    // Open database
    try {
        storage = new Storage;
        storage->open();
    } catch (std::string &error) {
        LOG_FATAL("Error opening the database: " << error);
        exit(1);
    }

    // --- Initialize the managers
    stringFilter = new utils::StringFilter;  // The slang's and double quotes filter.
    chatChannelManager = new ChatChannelManager;
    guildManager = new GuildManager;
    postalManager = new PostManager;
    gBandwidth = new BandwidthMonitor;

    // --- Initialize the global handlers
    // FIXME: Make the global handlers global vars or part of a bigger
    // singleton or a local variable in the event-loop
    chatHandler = new ChatHandler;

    // --- Initialize enet.
    if (enet_initialize() != 0) {
        LOG_FATAL("An error occurred while initializing ENet");
        exit(2);
    }

    // Initialize the processor utility functions
    utils::processor::init();

    // Seed the random number generator
    std::srand( time(NULL) );
}


/**
 * Deinitializes the server.
 */
static void deinitializeServer()
{
    // Write configuration file
    Configuration::deinitialize();

    // Destroy message handlers.
    AccountClientHandler::deinitialize();
    GameServerHandler::deinitialize();

    // Quit ENet
    enet_deinitialize();

    delete chatHandler;

    // Destroy Managers
    delete stringFilter;
    delete chatChannelManager;
    delete guildManager;
    delete postalManager;
    delete gBandwidth;

    // Get rid of persistent data storage
    delete storage;

    PHYSFS_deinit();
}

/**
 * Dumps statistics.
 */
static void dumpStatistics()
{
#if defined STATS_FILE
    std::string path = STATS_FILE;
#else
    std::string path = DEFAULT_STATS_FILE;
#endif

    std::ofstream os(path.c_str());
    os << "<statistics>\n";
    GameServerHandler::dumpStatistics(os);
    os << "</statistics>\n";
}

/**
 * Show command line arguments
 */
static void printHelp()
{
    std::cout << "manaserv" << std::endl << std::endl
              << "Options: " << std::endl
              << "  -h --help          : Display this help" << std::endl
              << "     --config <path> : Set the config path to use."
              << " (Default: ./manaserv.xml)" << std::endl
              << "     --verbosity <n> : Set the verbosity level" << std::endl
              << "                        - 0. Fatal Errors only." << std::endl
              << "                        - 1. All Errors." << std::endl
              << "                        - 2. Plus warnings." << std::endl
              << "                        - 3. Plus standard information." << std::endl
              << "                        - 4. Plus debugging information." << std::endl
              << "     --port <n>      : Set the default port to listen on" << std::endl;
    exit(0);
}

struct CommandLineOptions
{
    CommandLineOptions():
        configPath(DEFAULT_CONFIG_FILE),
        configPathChanged(false),
        verbosity(Logger::Warn),
        verbosityChanged(false),
        port(DEFAULT_SERVER_PORT),
        portChanged(false)
    {}

    std::string configPath;
    bool configPathChanged;

    Logger::Level verbosity;
    bool verbosityChanged;

    int port;
    bool portChanged;
};

/**
 * Parse the command line arguments
 */
static void parseOptions(int argc, char *argv[], CommandLineOptions &options)
{
    const char *optString = "h";

    const struct option longOptions[] =
    {
        { "help",       no_argument,       0, 'h' },
        { "config",     required_argument, 0, 'c' },
        { "verbosity",  required_argument, 0, 'v' },
        { "port",       required_argument, 0, 'p' },
        { 0, 0, 0, 0 }
    };

    while (optind < argc)
    {
        int result = getopt_long(argc, argv, optString, longOptions, NULL);

        if (result == -1)
            break;

        switch (result)
        {
            default: // Unknown option.
            case 'h':
                // Print help.
                printHelp();
                break;
            case 'c':
                // Change config filename and path.
                options.configPath = optarg;
                options.configPathChanged = true;
                break;
            case 'v':
                options.verbosity = static_cast<Logger::Level>(atoi(optarg));
                options.verbosityChanged = true;
                LOG_INFO("Using log verbosity level " << options.verbosity);
                break;
            case 'p':
                options.port = atoi(optarg);
                options.portChanged = true;
                break;
        }
    }
}


/**
 * Main function, initializes and runs server.
 */
int main(int argc, char *argv[])
{
#ifdef PACKAGE_VERSION
    LOG_INFO("The Mana Account+Chat Server v" << PACKAGE_VERSION);
#endif

    // Parse command line options
    CommandLineOptions options;
    parseOptions(argc, argv, options);

    initializeConfiguration(options.configPath);

    if (!options.verbosityChanged)
        options.verbosity = static_cast<Logger::Level>(
                            Configuration::getValue("log_accountServerLogLevel",
                                                    options.verbosity) );
    Logger::setVerbosity(options.verbosity);

    if (!options.portChanged)
        options.port = Configuration::getValue("net_accountServerPort",
                                               options.port);

    // General initialization
    initialize();

    std::string host = Configuration::getValue("net_listenHost", std::string());
    if (!AccountClientHandler::initialize(DEFAULT_ATTRIBUTEDB_FILE,
                                          options.port, host) ||
        !GameServerHandler::initialize(options.port + 1, host) ||
        !chatHandler->startListen(options.port + 2, host))
    {
        LOG_FATAL("Unable to create an ENet server host.");
        return 3;
    }

    // Dump statistics every 10 seconds.
    utils::Timer statTimer(10000);
    // Check for expired bans every 30 seconds
    utils::Timer banTimer(30000);

    // -------------------------------------------------------------------------
    // FIXME: for testing purposes only...
    // writing accountserver startup time and svn revision to database as global
    // world state variable
    const time_t startup = time(NULL);
    std::stringstream timestamp;
    timestamp << startup;
    storage->setWorldStateVar("accountserver_startup", timestamp.str());
    const std::string revision = "$Revision$";
    storage->setWorldStateVar("accountserver_version", revision);
    // -------------------------------------------------------------------------

    while (running)
    {
        AccountClientHandler::process();
        GameServerHandler::process();
        chatHandler->process(50);

        if (statTimer.poll())
            dumpStatistics();

        if (banTimer.poll())
            storage->checkBannedAccounts();
    }

    LOG_INFO("Received: Quit signal, closing down...");
    chatHandler->stopListen();
    deinitializeServer();

    return 0;
}
