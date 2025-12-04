/**
@file    MuteList.h
@brief   Thread-safe persistent mute list for SvxReflector
@author  SvxLink Contributors
@date    2025

\verbatim
SvxReflector - An audio reflector for connecting SvxLink Servers
Copyright (C) 2003-2025 Tobias Blomberg / SM0SVX

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
\endverbatim
*/

#ifndef MUTE_LIST_INCLUDED
#define MUTE_LIST_INCLUDED


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <chrono>
#include <ctime>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "DataStore.h"




/****************************************************************************
 *
 * Forward declarations
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Defines & typedefs
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Exported Global Variables
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Class definitions
 *
 ****************************************************************************/

/**
@brief  Thread-safe persistent mute list
@author SvxLink Contributors
@date   2025

This class manages muted callsigns. Muted nodes cannot send audio through
the reflector (TX blocked), but can still receive audio from others.

The list is persisted using the IDataStore interface.
All operations are thread-safe.

Mute types:
- Temporary: expires after specified duration
- Forever: permanent until manually unmuted (expires_at = 0)
*/
class MuteList
{
  public:
    /**
     * @brief   Mute entry with optional expiry
     *
     * expires_at = 0 means permanent (forever) mute
     * expires_at > 0 means temporary mute until that time
     */
    struct Entry
    {
      std::string   callsign;
      std::string   reason;
      time_t        muted_at;
      time_t        expires_at;   // 0 = permanent/forever

      Entry(void) : muted_at(0), expires_at(0) {}

      /**
       * @brief   Check if this mute entry has expired
       * @return  true if expired, false if permanent or not yet expired
       */
      bool isExpired(void) const;

      /**
       * @brief   Check if this is a permanent (forever) mute
       * @return  true if no expiry set (expires_at == 0)
       */
      bool isPermanent(void) const { return expires_at == 0; }
    };

    /**
     * @brief   Constructor
     * @param   store Pointer to IDataStore for persistence (nullptr = no persistence)
     *
     * Loads existing mute list from store if present.
     * Caller retains ownership of the store pointer.
     */
    explicit MuteList(DataStore::IDataStore* store = nullptr);

    /**
     * @brief   Destructor
     */
    ~MuteList(void);

    // Disable copy (contains mutex)
    MuteList(const MuteList&);
    MuteList& operator=(const MuteList&);

    /**
     * @brief   Mute a callsign temporarily
     * @param   callsign The callsign to mute (case-insensitive)
     * @param   duration_sec Mute duration in seconds
     * @param   reason   Reason for the mute (optional)
     * @return  true if added, false if already muted
     */
    bool mute(const std::string& callsign, unsigned duration_sec,
              const std::string& reason = "");

    /**
     * @brief   Mute a callsign forever (until manually unmuted)
     * @param   callsign The callsign to mute (case-insensitive)
     * @param   reason   Reason for the mute (optional)
     * @return  true if added, false if already muted
     */
    bool muteForever(const std::string& callsign,
                     const std::string& reason = "");

    /**
     * @brief   Unmute a callsign
     * @param   callsign The callsign to unmute
     * @return  true if removed, false if not found
     */
    bool unmute(const std::string& callsign);

    /**
     * @brief   Check if a callsign is muted
     * @param   callsign The callsign to check
     * @return  true if currently muted (not expired)
     */
    bool isMuted(const std::string& callsign) const;

    /**
     * @brief   Get the mute entry for a callsign
     * @param   callsign The callsign to look up
     * @param   entry    Output parameter for the entry
     * @return  true if found and not expired
     */
    bool get(const std::string& callsign, Entry& entry) const;

    /**
     * @brief   List all muted callsigns
     * @return  Vector of all current (non-expired) mute entries
     */
    std::vector<Entry> list(void) const;

    /**
     * @brief   Get count of muted callsigns
     * @return  Number of currently muted callsigns
     */
    size_t size(void) const;

    /**
     * @brief   Clear all muted callsigns
     */
    void clear(void);

    /**
     * @brief   Reload the mute list from data store
     * @return  true on success
     */
    bool load(void);

    /**
     * @brief   Remove expired mutes
     * @return  Number of entries removed
     */
    size_t pruneExpired(void);

  private:
    static const char*                  TABLE_NAME;
    DataStore::IDataStore*              m_store;
    std::map<std::string, Entry>        m_entries;  // In-memory fallback
    mutable std::mutex                  m_mutex;

    // Check if datastore is available for real-time lookups
    bool hasDataStore(void) const { return m_store != nullptr; }

    // Convert callsign to uppercase for consistent storage
    static std::string normalizeCallsign(const std::string& callsign);

    // Internal add (caller must hold mutex)
    bool addUnlocked(const std::string& callsign, const std::string& reason,
                     unsigned duration_sec, bool permanent);

    // Convert Entry to JSON
    static Json::Value entryToJson(const Entry& entry);

    // Convert JSON to Entry
    static bool jsonToEntry(const Json::Value& json, Entry& entry);

};  /* class MuteList */


#endif /* MUTE_LIST_INCLUDED */


/*
 * This file has not been truncated
 */
