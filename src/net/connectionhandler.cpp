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

#include "defines.h"
#include "net/connectionhandler.hpp"
#include "net/messagein.hpp"
#include "net/messageout.hpp"
#include "net/netcomputer.hpp"
#include "utils/logger.h"

#ifdef SCRIPT_SUPPORT
#include "script.h"
#endif


std::string
ip4ToString(unsigned int ip4addr)
{
    std::stringstream ss;
    ss << (ip4addr & 0x000000ff) << "."
       << ((ip4addr & 0x0000ff00) >> 8) << "."
       << ((ip4addr & 0x00ff0000) >> 16) << "."
       << ((ip4addr & 0xff000000) >> 24);
    return ss.str();
}

bool ConnectionHandler::startListen(enet_uint16 port)
{
    // Bind the server to the default localhost.
    address.host = ENET_HOST_ANY;
    address.port = port;

    LOG_INFO("Listening on port " << port << "...", 0);
    host = enet_host_create(&address    /* the address to bind the server host to */,
                            MAX_CLIENTS /* allow up to MAX_CLIENTS clients and/or outgoing connections */,
                            0           /* assume any amount of incoming bandwidth */,
                            0           /* assume any amount of outgoing bandwidth */);

    return host;
}

void ConnectionHandler::stopListen()
{
    // - Disconnect all clients (close sockets)

    // TODO: probably there's a better way.
    ENetPeer *currentPeer;

    for (currentPeer = host->peers;
         currentPeer < &host->peers[host->peerCount];
         ++currentPeer)
    {
       if (currentPeer->state == ENET_PEER_STATE_CONNECTED)
       {
            enet_peer_disconnect(currentPeer, 0);
            enet_host_flush(host);
            enet_peer_reset(currentPeer);
       }
    }
    enet_host_destroy(host);
    // FIXME: memory leak on NetComputers
}

void ConnectionHandler::flush()
{
    enet_host_flush(host);
}

void ConnectionHandler::process()
{
    ENetEvent event;
    // Process Enet events and do not block.
    while (enet_host_service(host, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
            {
                LOG_INFO("A new client connected from " <<
                         ip4ToString(event.peer->address.host) << ":" <<
                         event.peer->address.port << " to port " <<
                         host->address.port, 0);
                NetComputer *comp = computerConnected(event.peer);
                clients.push_back(comp);

                // Store any relevant client information here.
                event.peer->data = (void *)comp;
            } break;

            case ENET_EVENT_TYPE_RECEIVE:
            {
                NetComputer *comp = (NetComputer*) event.peer->data;

#ifdef SCRIPT_SUPPORT
                // This could be good if you wanted to extend the
                // server protocol using a scripting language. This
                // could be attained by using allowing scripts to
                // "hook" certain messages.

                //script->message(buffer);
#endif

                // If the scripting subsystem didn't hook the message
                // it will be handled by the default message handler.

                // Make sure that the packet is big enough (> short)
                if (event.packet->dataLength >= 2) {
                    MessageIn msg((char *)event.packet->data,
                                  event.packet->dataLength);
                    LOG_INFO("Received message " << msg.getId() << " ("
                            << event.packet->dataLength << " B) from "
                            << *comp, 2);

                    processMessage(comp, msg);
                } else {
                    LOG_ERROR("Message too short from " << *comp, 0);
                }

                /* Clean up the packet now that we're done using it. */
                enet_packet_destroy(event.packet);
            } break;

            case ENET_EVENT_TYPE_DISCONNECT:
            {
                NetComputer *comp = (NetComputer *)event.peer->data;
                LOG_INFO(ip4ToString(event.peer->address.host) << " disconnected.", 0);
                // Reset the peer's client information.
                computerDisconnected(comp);
                clients.erase(std::find(clients.begin(), clients.end(), comp));
                event.peer->data = NULL;
            } break;

            default: break;
        }
    }
}

void ConnectionHandler::sendToEveryone(const MessageOut &msg)
{
    for (NetComputers::iterator i = clients.begin(), i_end = clients.end();
         i != i_end; ++i) {
        (*i)->send(msg);
    }
}

unsigned int ConnectionHandler::getClientNumber()
{
    return clients.size();
}