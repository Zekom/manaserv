/*
 *  The Mana World Server
 *  Copyright 2004 The Mana World Development Team
 *
 *  This file is part of The Mana World.
 *
 *  The Mana World is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  any later version.
 *
 *  The Mana World is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with The Mana World; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  $Id$
 */

#ifndef INVENTORY_H
#define INVENTORY_H

#include "game-server/character.hpp"
#include "net/messageout.hpp"

enum
{
// Equipment rules:
// 1 Brest equipment
    EQUIP_TORSO_SLOT = 0,
// 1 arms equipment
    EQUIP_ARMS_SLOT = 1,
// 1 head equipment
    EQUIP_HEAD_SLOT = 2,
// 1 legs equipment
    EQUIP_LEGS_SLOT = 3,
// 1 feet equipment
    EQUIP_FEET_SLOT = 4,
// 2 rings
    EQUIP_RING1_SLOT = 5,
    EQUIP_RING2_SLOT = 6,
// 1 necklace
    EQUIP_NECKLACE_SLOT = 7,
// Fight:
//   2 one-handed weapons
//   or 1 two-handed weapon
//   or 1 one-handed weapon + 1 shield.
    EQUIP_FIGHT1_SLOT = 8,
    EQUIP_FIGHT2_SLOT = 9,
// Projectile:
//   this item does not amount to one, it only indicates the chosen projectile.
    EQUIP_PROJECTILE_SLOT = 10,

    EQUIP_CLIENT_INVENTORY = 32
};

class GameClient;

/**
 * Class used to handle Character possessions and prepare outgoing messages.
 */
class Inventory
{
    public:

        /**
         * Creates a view on the possessions of a character.
         * @param delayed true if changes have to be cancelable.
         */
        Inventory(Character *, bool delayed = false);

        /**
         * Commits delayed changes.
         * Sends the update message to the client.
         */
        ~Inventory();

        /**
         * Commits delayed changes.
         */
        void commit();

        /**
         * Cancels delayed changes.
         */
        void cancel();

        /**
         * Sends a complete inventory update to the client.
         */
        void sendFull() const;

        /**
         * Equips item from given inventory slot.
         */
        void equip(int slot);

        /**
         * Unequips item from given equipment slot.
         */
        void unequip(int slot);

        /**
         * Gets the ID of projectiles. Removes one of these projectiles from
         * inventory.
         */
        int fireProjectile();

        /**
         * Inserts some items into the inventory.
         * @return number of items not inserted (to be dropped on floor?).
         */
        int insert(int itemId, int amount);

        /**
         * Removes some items from inventory.
         * @return number of items not removed.
         */
        int remove(int itemId, int amount);

        /**
         * Removes some items from inventory.
         * @return number of items not removed.
         */
        int removeFromSlot(int slot, int amount);

        /**
         * Counts number of items with given ID.
         */
        int count(int itemId) const;

        /**
         * Gets the ID of the items in a given slot.
         */
        int getItem(int slot) const;

    private:

        /**
         * Ensures we are working on a copy in delayed mode.
         */
        void prepare();

        /**
         * Updates the original in delayed mode.
         */
        void update();

        /**
         * Starts a new notification message.
         */
        void restart();

        /**
         * Fills some slots with items.
         * @return number of items not inserted.
         */
        int fillFreeSlot(int itemId, int amount, int MaxPerSlot);

        /**
         * Frees an inventory slot given by its real index.
         */
        void freeIndex(int index);

        /**
         * Gets the real index associated to a slot.
         */
        int getIndex(int slot) const;

        /**
         * Gets the slot number of an inventory index.
         */
        int getSlot(int index) const;

        /**
         * Replaces a whole slot of items from inventory.
         */
        void replaceInSlot(int slot, int itemId, int amount);

        Possessions *mPoss; /**< Pointer to the modified possessions. */
        MessageOut msg;     /**< Update message containing all the changes. */
        Character *mClient; /**< Character to notify. */
        bool mDelayed;      /**< Delayed changes. */
        bool mChangedLook;  /**< Need to notify of a visible equipment change. */
};


#endif
