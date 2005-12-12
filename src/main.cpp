/*
 *  The Mana World Server
 *  Copyright 2004 The Mana World Development Team
 *
 *  This file is part of The Mana World.
 *
 *  The Mana World  is free software; you can redistribute  it and/or modify it
 *  under the terms of the GNU General  Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or any later version.
 *
 *  The Mana  World is  distributed in  the hope  that it  will be  useful, but
 *  WITHOUT ANY WARRANTY; without even  the implied warranty of MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 *  more details.
 *
 *  You should  have received a  copy of the  GNU General Public  License along
 *  with The Mana  World; if not, write to the  Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 *  $Id$
 */


#include <iostream>

#include <SDL.h>
#include <SDL_net.h>

#include <cstdlib>

#ifdef __USE_UNIX98
#include "../config.h"
#endif

#include "netsession.h"
#include "connectionhandler.h"
#include "accounthandler.h"
#include "gamehandler.h"
#include "chathandler.h"
#include "storage.h"
#include "configuration.h"
#include "state.h"

#include "skill.h"

#include "utils/logger.h"


// Scripting
#ifdef SCRIPT_SUPPORT

extern "C" void Init_Tmw();

#if defined (SQUIRREL_SUPPORT)
std::string scriptLanguage = "squirrel";
#elif defined (RUBY_SUPPORT)
#include <ruby.h>
int rubyStatus;
std::string scriptLanguage = "ruby";
#elif defined (LUA_SUPPORT)
std::string scriptLanguage = "lua";
#else
#error "Scripting enabled, but no language selected"
#endif

#else
std::string scriptLanugage = "none";
#endif // SCRIPT_SUPPORT

#define LOG_FILE        "tmwserv.log"

#define TMW_WORLD_TICK  SDL_USEREVENT
#define SERVER_PORT     9601


SDL_TimerID worldTimerID; /**< Timer ID of world timer */
int worldTime = 0;        /**< Current world time in 100ms ticks */
bool running = true;      /**< Determines if server keeps running */

Skill skillTree("base");  /**< Skill tree */

Configuration config;     /**< XML config reader */
AccountHandler *accountHandler = new AccountHandler(); /**< Account message handler */
ChatHandler *chatHandler = new ChatHandler(); /**< Communications (chat) messaqge handler */
GameHandler *gameHandler = new GameHandler(); /**< Core game message handler */

ConnectionHandler connectionHandler; /**< Primary connection handler */

/**
 * SDL timer callback, sends a <code>TMW_WORLD_TICK</code> event.
 */
Uint32 worldTick(Uint32 interval, void *param)
{
    // Push the custom world tick event
    SDL_Event event;
    event.type = TMW_WORLD_TICK;

    if (SDL_PushEvent(&event)) {
        LOG_WARN("couldn't push world tick into event queue!")
    }

    return interval;
}


/**
 * Initializes the server.
 */
void initialize()
{
    // initialize the logger.
    using namespace tmwserv::utils;
    Logger::instance().setLogFile(LOG_FILE);
    // write the messages to both the screen and the log file.
    Logger::instance().setTeeMode(true);

    // initialize SDL.
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == -1) {
        LOG_FATAL("SDL_Init: " << SDL_GetError())
        exit(1);
    }

    // set SDL to quit on exit.
    atexit(SDL_Quit);

    // initialize SDL_net.
    if (SDLNet_Init() == -1) {
        LOG_FATAL("SDLNet_Init: " << SDLNet_GetError())
        exit(2);
    }

    // initialize world timer at 10 times per second.
    worldTimerID = SDL_AddTimer(100, worldTick, NULL);

    // initialize scripting subsystem.
#ifdef RUBY_SUPPORT
    LOG_INFO("Script Language: " << scriptLanguage)

    // initialize ruby
    ruby_init();
    ruby_init_loadpath();
    ruby_script("tmw");

    // initialize bindings
    Init_Tmw();

    // run test script
    rb_load_file("scripts/init.rb");
    rubyStatus = ruby_exec();
#else
    LOG_WARN("No Scripting Language Support.")
#endif

#if defined (MYSQL_SUPPORT)
    LOG_INFO("Using MySQL DB Backend.")
#elif defined (POSTGRESQL_SUPPORT)
    LOG_INFO("Using PostGreSQL DB Backend.")
#elif defined (SQLITE_SUPPORT)
    LOG_INFO("Using SQLite DB Backend.")
#else
    LOG_WARN("No Database Backend Support.")
#endif

    // initialize configuration
    // initialize configuration defaults
    config.setValue("dbuser", "");
    config.setValue("dbpass", "");
    config.setValue("dbhost", "");

#ifdef WIN32
    std::string configPath = ".";
#else
    std::string configPath = getenv("HOME");
#endif
    configPath += "/.tmwserv.xml";
    config.init(configPath);
    LOG_INFO("Using Config File: " << configPath)
    LOG_INFO("Using Log File: " << LOG_FILE)
}


/**
 * Deinitializes the server.
 */
void deinitialize()
{
    // Write configuration file
    config.write();

    // Stop world timer
    SDL_RemoveTimer(worldTimerID);

    // Quit SDL_net
    SDLNet_Quit();

#ifdef RUBY_SUPPORT
    // Finish up ruby
    ruby_finalize();
    ruby_cleanup(rubyStatus);
#endif

    // destroy message handlers
    delete accountHandler;
    delete chatHandler;
    delete gameHandler;

    // Get rid of persistent data storage
    tmwserv::Storage::destroy();
}


/**
 * Main function, initializes and runs server.
 */
int main(int argc, char *argv[])
{
#ifdef __USE_UNIX98
    LOG_INFO("The Mana World Server v" << PACKAGE_VERSION)
#endif
    // Ready for server work...
    std::auto_ptr<NetSession> session(new NetSession());

    // Note: This is just an idea, we could also pass the connection handler
    // to the constructor of the account handler, upon which is would register
    // itself for the messages it handles.
    //

    // Register message handlers
    connectionHandler.registerHandler(CMSG_LOGIN, accountHandler);
    connectionHandler.registerHandler(CMSG_LOGOUT, accountHandler);
    connectionHandler.registerHandler(CMSG_REGISTER, accountHandler);
    connectionHandler.registerHandler(CMSG_UNREGISTER, accountHandler);
    connectionHandler.registerHandler(CMSG_CHAR_CREATE, accountHandler);
    connectionHandler.registerHandler(CMSG_CHAR_SELECT, accountHandler);
    connectionHandler.registerHandler(CMSG_CHAR_DELETE, accountHandler);
    connectionHandler.registerHandler(CMSG_CHAR_LIST, accountHandler);

    connectionHandler.registerHandler(CMSG_SAY, chatHandler);
    connectionHandler.registerHandler(CMSG_ANNOUNCE, chatHandler);

    connectionHandler.registerHandler(CMSG_PICKUP, gameHandler);
    connectionHandler.registerHandler(CMSG_USE_OBJECT, gameHandler);
    connectionHandler.registerHandler(CMSG_USE_ITEM, gameHandler); // NOTE: this is probably redundant (CMSG_USE_OBJECT)
    connectionHandler.registerHandler(CMSG_TARGET, gameHandler);
    connectionHandler.registerHandler(CMSG_WALK, gameHandler);
    connectionHandler.registerHandler(CMSG_START_TRADE, gameHandler);
    connectionHandler.registerHandler(CMSG_START_TALK, gameHandler);
    connectionHandler.registerHandler(CMSG_REQ_TRADE, gameHandler);
    connectionHandler.registerHandler(CMSG_EQUIP, gameHandler);

    initialize();

    session->startListen(&connectionHandler, SERVER_PORT);
    LOG_INFO("Listening on port " << SERVER_PORT << "...")

    using namespace tmwserv;

    // create storage wrapper
    Storage& store = Storage::instance("tmw");
    store.setUser(config.getValue("dbuser", ""));
    store.setPassword(config.getValue("dbpass", ""));
    store.close();
    store.open();
    //

    // create state machine
    State &state = State::instance();
    //

    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == TMW_WORLD_TICK) {
                // Move the world forward in time
                worldTime++;

                // Print world time at 10 second intervals to show we're alive
                if (worldTime % 100 == 0) {
                    LOG_INFO("World time: " << worldTime);
                }

                // - Handle all messages that are in the message queue
                // - Update all active objects/beings
                state.update(connectionHandler);
            }
            else if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        // We know only about 10 events will happen per second,
        // so give the CPU a break for a while.
        SDL_Delay(100);
    }

    LOG_INFO("Received: Quit signal, closing down...")
    session->stopListen(SERVER_PORT);

    deinitialize();
}
