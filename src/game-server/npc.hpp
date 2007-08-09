/*
 *  The Mana World Server
 *  Copyright 2007 The Mana World Development Team
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

#ifndef _TMWSERV_GAMESERVER_NPC_HPP_
#define _TMWSERV_GAMESERVER_NPC_HPP_

#include "game-server/being.hpp"

class Script;
class Character;

/**
 * Class describing a non-player character.
 */
class NPC : public Being
{
    public:
        NPC(int id, Script *);

        void update();

        /**
         * Prompts NPC.
         */
        void prompt(Character *, bool restart);

        /**
         * Selects an NPC proposition.
         */
        void select(Character *, int);

        /**
         * Gets NPC ID.
         */
        int getNPC() const
        { return mID; }

    private:
        Script *mScript;    /**< Script describing NPC behavior. */
        unsigned short mID; /**< ID of the NPC. */
};

#endif