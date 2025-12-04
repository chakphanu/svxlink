/**
@file    MuteList.cpp
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


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cassert>
#include <json/json.h>


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

#include "MuteList.h"


/****************************************************************************
 *
 * Namespaces to use
 *
 ****************************************************************************/

using namespace std;


/****************************************************************************
 *
 * Defines & typedefs
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Local class definitions
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Local functions
 *
 ****************************************************************************/

namespace {
  // Convert time_t to ISO 8601 string
  std::string timeToString(time_t t)
  {
    if (t == 0)
    {
      return "";
    }
    std::tm tm_val;
    gmtime_r(&t, &tm_val);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_val);
    return std::string(buf);
  }

  // Parse ISO 8601 string to time_t
  time_t stringToTime(const std::string& str)
  {
    if (str.empty())
    {
      return 0;
    }
    std::tm tm_val;
    memset(&tm_val, 0, sizeof(tm_val));
    if (strptime(str.c_str(), "%Y-%m-%dT%H:%M:%SZ", &tm_val) == nullptr)
    {
      return 0;
    }
    return timegm(&tm_val);
  }
}


/****************************************************************************
 *
 * Exported Global Variables
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Local Global Variables
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Class constants
 *
 ****************************************************************************/

const char* MuteList::TABLE_NAME = "mutes";


/****************************************************************************
 *
 * Public member functions
 *
 ****************************************************************************/

bool MuteList::Entry::isExpired(void) const
{
  if (expires_at == 0)
  {
    return false;  // Permanent mute never expires
  }
  return time(nullptr) > expires_at;
} /* MuteList::Entry::isExpired */


MuteList::MuteList(DataStore::IDataStore* store)
  : m_store(store)
{
  // Note: store can be nullptr for in-memory only mode
  if (m_store != nullptr)
  {
    // Precondition: data store must be open
    assert(m_store->isOpen() && "Data store must be open before use");
    load();
  }
} /* MuteList::MuteList */


MuteList::~MuteList(void)
{
  // No need to save - IDataStore handles persistence immediately
} /* MuteList::~MuteList */


bool MuteList::mute(const std::string& callsign, unsigned duration_sec,
                    const std::string& reason)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return addUnlocked(callsign, reason, duration_sec, false);
} /* MuteList::mute */


bool MuteList::muteForever(const std::string& callsign, const std::string& reason)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return addUnlocked(callsign, reason, 0, true);
} /* MuteList::muteForever */


bool MuteList::unmute(const std::string& callsign)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string cs = normalizeCallsign(callsign);
  if (cs.empty())
  {
    return false;
  }

  // Real-time lookup from datastore if available
  if (hasDataStore())
  {
    assert(m_store != nullptr);
    Json::Value json;
    if (!m_store->get(TABLE_NAME, cs, json))
    {
      return false;  // Not found in datastore
    }

    // Remove from data store
    m_store->remove(TABLE_NAME, cs);

    // Also remove from in-memory fallback if present
    m_entries.erase(cs);

    std::cout << "MuteList: Unmuted " << cs << std::endl;
    return true;
  }

  // Fallback to in-memory storage if no datastore
  std::map<std::string, Entry>::iterator it = m_entries.find(cs);
  if (it == m_entries.end())
  {
    return false;  // Not found
  }

  m_entries.erase(it);

  std::cout << "MuteList: Unmuted " << cs << std::endl;

  return true;
} /* MuteList::unmute */


bool MuteList::isMuted(const std::string& callsign) const
{
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string cs = normalizeCallsign(callsign);
  if (cs.empty())
  {
    return false;
  }

  // Real-time lookup from datastore if available
  if (hasDataStore())
  {
    assert(m_store != nullptr);
    Json::Value json;
    if (m_store->get(TABLE_NAME, cs, json))
    {
      Entry entry;
      if (jsonToEntry(json, entry))
      {
        return !entry.isExpired();
      }
    }
    return false;
  }

  // Fallback to in-memory storage if no datastore
  std::map<std::string, Entry>::const_iterator it = m_entries.find(cs);
  if (it == m_entries.end())
  {
    return false;
  }
  return !it->second.isExpired();
} /* MuteList::isMuted */


bool MuteList::get(const std::string& callsign, Entry& entry) const
{
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string cs = normalizeCallsign(callsign);
  if (cs.empty())
  {
    return false;
  }

  // Real-time lookup from datastore if available
  if (hasDataStore())
  {
    assert(m_store != nullptr);
    Json::Value json;
    if (m_store->get(TABLE_NAME, cs, json))
    {
      if (jsonToEntry(json, entry) && !entry.isExpired())
      {
        return true;
      }
    }
    return false;
  }

  // Fallback to in-memory storage if no datastore
  std::map<std::string, Entry>::const_iterator it = m_entries.find(cs);
  if (it == m_entries.end())
  {
    return false;
  }

  if (it->second.isExpired())
  {
    return false;
  }

  entry = it->second;
  return true;
} /* MuteList::get */


std::vector<MuteList::Entry> MuteList::list(void) const
{
  std::lock_guard<std::mutex> lock(m_mutex);

  std::vector<Entry> result;

  // Real-time query from datastore if available
  if (hasDataStore())
  {
    assert(m_store != nullptr);
    std::vector<Json::Value> values = m_store->query(TABLE_NAME);
    result.reserve(values.size());

    for (size_t i = 0; i < values.size(); ++i)
    {
      Entry entry;
      if (jsonToEntry(values[i], entry) && !entry.isExpired())
      {
        result.push_back(entry);
      }
    }
    return result;
  }

  // Fallback to in-memory storage if no datastore
  result.reserve(m_entries.size());

  for (std::map<std::string, Entry>::const_iterator it = m_entries.begin();
       it != m_entries.end(); ++it)
  {
    if (!it->second.isExpired())
    {
      result.push_back(it->second);
    }
  }

  return result;
} /* MuteList::list */


size_t MuteList::size(void) const
{
  std::lock_guard<std::mutex> lock(m_mutex);

  // Real-time query from datastore if available
  if (hasDataStore())
  {
    assert(m_store != nullptr);
    std::vector<Json::Value> values = m_store->query(TABLE_NAME);
    size_t count = 0;

    for (size_t i = 0; i < values.size(); ++i)
    {
      Entry entry;
      if (jsonToEntry(values[i], entry) && !entry.isExpired())
      {
        ++count;
      }
    }
    return count;
  }

  // Fallback to in-memory storage if no datastore
  size_t count = 0;
  for (std::map<std::string, Entry>::const_iterator it = m_entries.begin();
       it != m_entries.end(); ++it)
  {
    if (!it->second.isExpired())
    {
      ++count;
    }
  }

  return count;
} /* MuteList::size */


void MuteList::clear(void)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  // Clear from data store first (primary storage)
  if (hasDataStore())
  {
    assert(m_store != nullptr);
    m_store->clear(TABLE_NAME);
  }

  // Also clear in-memory fallback
  if (!m_entries.empty())
  {
    m_entries.clear();
    std::cout << "MuteList: Cleared all entries" << std::endl;
  }
} /* MuteList::clear */


bool MuteList::load(void)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!hasDataStore())
  {
    return false;
  }
  assert(m_store != nullptr);

  m_entries.clear();

  // Query all entries from the data store
  std::vector<Json::Value> values = m_store->query(TABLE_NAME);

  for (size_t i = 0; i < values.size(); ++i)
  {
    Entry entry;
    if (!jsonToEntry(values[i], entry))
    {
      continue;  // Skip invalid entries
    }

    // Skip expired entries during load
    if (!entry.isExpired())
    {
      m_entries[entry.callsign] = entry;
    }
    else
    {
      // Remove expired entry from store
      m_store->remove(TABLE_NAME, entry.callsign);
    }
  }

  std::cout << "MuteList: Loaded " << m_entries.size() << " entries from data store"
            << std::endl;

  return true;
} /* MuteList::load */


size_t MuteList::pruneExpired(void)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  size_t removed = 0;

  // Query from datastore if available for real-time expired entries
  if (hasDataStore())
  {
    assert(m_store != nullptr);
    std::vector<Json::Value> values = m_store->query(TABLE_NAME);

    for (size_t i = 0; i < values.size(); ++i)
    {
      Entry entry;
      if (jsonToEntry(values[i], entry) && entry.isExpired())
      {
        std::cout << "MuteList: Expired mute removed for " << entry.callsign
                  << std::endl;
        m_store->remove(TABLE_NAME, entry.callsign);
        m_entries.erase(entry.callsign);
        ++removed;
      }
    }
    return removed;
  }

  // Fallback to in-memory storage if no datastore
  std::map<std::string, Entry>::iterator it = m_entries.begin();
  while (it != m_entries.end())
  {
    if (it->second.isExpired())
    {
      std::string cs = it->first;
      std::cout << "MuteList: Expired mute removed for " << cs << std::endl;

      m_entries.erase(it++);
      ++removed;
    }
    else
    {
      ++it;
    }
  }

  return removed;
} /* MuteList::pruneExpired */


/****************************************************************************
 *
 * Protected member functions
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/

std::string MuteList::normalizeCallsign(const std::string& callsign)
{
  std::string result;
  result.reserve(callsign.size());

  for (size_t i = 0; i < callsign.size(); ++i)
  {
    char c = callsign[i];
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-')
    {
      result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
  }

  return result;
} /* MuteList::normalizeCallsign */


bool MuteList::addUnlocked(const std::string& callsign, const std::string& reason,
                           unsigned duration_sec, bool permanent)
{
  std::string cs = normalizeCallsign(callsign);
  if (cs.empty())
  {
    return false;
  }

  // Check if already muted and not expired (real-time lookup from datastore)
  if (hasDataStore())
  {
    assert(m_store != nullptr);
    Json::Value existing_json;
    if (m_store->get(TABLE_NAME, cs, existing_json))
    {
      Entry existing_entry;
      if (jsonToEntry(existing_json, existing_entry) && !existing_entry.isExpired())
      {
        return false;  // Already muted in datastore
      }
    }
  }
  else
  {
    // Fallback to in-memory storage if no datastore
    std::map<std::string, Entry>::iterator it = m_entries.find(cs);
    if (it != m_entries.end() && !it->second.isExpired())
    {
      return false;  // Already muted
    }
  }

  Entry entry;
  entry.callsign = cs;
  entry.reason = reason;
  entry.muted_at = time(nullptr);

  if (permanent)
  {
    entry.expires_at = 0;  // Forever
  }
  else
  {
    entry.expires_at = entry.muted_at + duration_sec;
  }

  // Save to data store first (primary storage)
  if (hasDataStore())
  {
    assert(m_store != nullptr);
    Json::Value json = entryToJson(entry);
    m_store->set(TABLE_NAME, cs, json);
  }

  // Also update in-memory fallback
  m_entries[cs] = entry;

  std::cout << "MuteList: Muted " << cs;
  if (permanent)
  {
    std::cout << " forever";
  }
  else
  {
    std::cout << " for " << duration_sec << " seconds";
  }
  if (!reason.empty())
  {
    std::cout << " - " << reason;
  }
  std::cout << std::endl;

  return true;
} /* MuteList::addUnlocked */


Json::Value MuteList::entryToJson(const Entry& entry)
{
  Json::Value json;
  json["callsign"] = entry.callsign;
  json["reason"] = entry.reason;
  json["muted_at"] = timeToString(entry.muted_at);

  if (entry.expires_at == 0)
  {
    json["expires_at"] = Json::nullValue;
  }
  else
  {
    json["expires_at"] = timeToString(entry.expires_at);
  }

  return json;
} /* MuteList::entryToJson */


bool MuteList::jsonToEntry(const Json::Value& json, Entry& entry)
{
  if (!json.isObject())
  {
    return false;
  }

  entry.callsign = json.get("callsign", "").asString();
  if (entry.callsign.empty())
  {
    return false;
  }

  entry.reason = json.get("reason", "").asString();
  entry.muted_at = stringToTime(json.get("muted_at", "").asString());
  if (entry.muted_at == 0)
  {
    entry.muted_at = time(nullptr);
  }

  // expires_at: null or empty string means permanent
  if (json["expires_at"].isNull() || json["expires_at"].asString().empty())
  {
    entry.expires_at = 0;  // Permanent
  }
  else
  {
    entry.expires_at = stringToTime(json["expires_at"].asString());
  }

  return true;
} /* MuteList::jsonToEntry */


/*
 * This file has not been truncated
 */
