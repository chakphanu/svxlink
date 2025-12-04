/**
@file    UserDatabase.cpp
@brief   User database management using IDataStore interface
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
#include <cassert>
#include <algorithm>
#include <list>
#include <map>


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

#include "UserDatabase.h"


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
 * Prototypes
 *
 ****************************************************************************/



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

const string UserDatabase::TABLE_NAME = "users";


/****************************************************************************
 *
 * Public member functions
 *
 ****************************************************************************/

UserDatabase::UserDatabase(DataStore::IDataStore* store)
  : m_store(store)
{
  assert(store != nullptr);
}  /* UserDatabase::UserDatabase */


UserDatabase::~UserDatabase(void)
{
}  /* UserDatabase::~UserDatabase */


bool UserDatabase::addUser(const string& callsign, const string& group,
                            const string& password, const Json::Value& metadata)
{
  lock_guard<mutex> lock(m_mutex);

  if (m_store->exists(TABLE_NAME, callsign))
  {
    return false;
  }

  UserEntry entry;
  entry.callsign = callsign;
  entry.group = group;
  entry.password = password;
  entry.enabled = true;
  entry.created_at = time(nullptr);
  entry.updated_at = entry.created_at;
  entry.last_seen = 0;
  entry.login_count = 0;
  entry.metadata = metadata;
  entry.source = "database";

  Json::Value json = entryToJson(entry);
  return m_store->set(TABLE_NAME, callsign, json);
}  /* UserDatabase::addUser */


bool UserDatabase::getUser(const string& callsign, UserEntry& entry)
{
  lock_guard<mutex> lock(m_mutex);

  Json::Value json;
  if (!m_store->get(TABLE_NAME, callsign, json))
  {
    return false;
  }

  return jsonToEntry(json, entry);
}  /* UserDatabase::getUser */


bool UserDatabase::updateUser(const string& callsign,
                               const Json::Value& updates)
{
  lock_guard<mutex> lock(m_mutex);

  Json::Value existing;
  if (!m_store->get(TABLE_NAME, callsign, existing))
  {
    return false;
  }

  // Merge updates into existing data
  vector<string> members = updates.getMemberNames();
  for (vector<string>::const_iterator it = members.begin();
       it != members.end(); ++it)
  {
    const string& key = *it;
    existing[key] = updates[key];
  }

  // Update timestamp
  existing["updated_at"] = Json::Value::Int64(time(nullptr));

  return m_store->set(TABLE_NAME, callsign, existing);
}  /* UserDatabase::updateUser */


bool UserDatabase::deleteUser(const string& callsign)
{
  lock_guard<mutex> lock(m_mutex);

  // Check if user exists first
  if (!m_store->exists(TABLE_NAME, callsign))
  {
    return false;
  }

  return m_store->remove(TABLE_NAME, callsign);
}  /* UserDatabase::deleteUser */


vector<UserEntry> UserDatabase::list(void)
{
  lock_guard<mutex> lock(m_mutex);

  vector<UserEntry> result;
  vector<Json::Value> values = m_store->query(TABLE_NAME);

  for (const Json::Value& json : values)
  {
    UserEntry entry;
    if (jsonToEntry(json, entry))
    {
      result.push_back(entry);
    }
  }

  return result;
}  /* UserDatabase::list */


bool UserDatabase::exists(const string& callsign)
{
  lock_guard<mutex> lock(m_mutex);

  return m_store->exists(TABLE_NAME, callsign);
}  /* UserDatabase::exists */


size_t UserDatabase::size(void)
{
  lock_guard<mutex> lock(m_mutex);

  return m_store->count(TABLE_NAME);
}  /* UserDatabase::size */


string UserDatabase::lookupUserKey(const string& callsign)
{
  UserEntry entry;
  if (!getUser(callsign, entry))
  {
    return string();
  }

  // Check if user is enabled (realtime lookup from datastore)
  if (!entry.enabled)
  {
    return string();  // Return empty to deny access
  }

  return entry.password;
}  /* UserDatabase::lookupUserKey */


void UserDatabase::recordLogin(const string& callsign,
                                const string& remote_ip,
                                const string& proto_ver)
{
  lock_guard<mutex> lock(m_mutex);

  Json::Value existing;
  if (!m_store->get(TABLE_NAME, callsign, existing))
  {
    return;
  }

  // Update last_seen and login_count
  existing["last_seen"] = Json::Value::Int64(time(nullptr));
  existing["updated_at"] = existing["last_seen"];

  unsigned login_count = 0;
  if (existing.isMember("login_count") && existing["login_count"].isUInt())
  {
    login_count = existing["login_count"].asUInt();
  }
  existing["login_count"] = login_count + 1;

  // Update metadata
  if (!existing.isMember("metadata") || !existing["metadata"].isObject())
  {
    existing["metadata"] = Json::Value(Json::objectValue);
  }
  existing["metadata"]["last_ip"] = remote_ip;
  existing["metadata"]["last_proto_ver"] = proto_ver;

  m_store->set(TABLE_NAME, callsign, existing);
}  /* UserDatabase::recordLogin */


void UserDatabase::recordLogout(const string& callsign)
{
  // Placeholder for future functionality
  // Could update metadata with logout timestamp, session duration, etc.
}  /* UserDatabase::recordLogout */


void UserDatabase::mergeFromConfig(Async::Config* cfg)
{
  lock_guard<mutex> lock(m_mutex);

  if (cfg == nullptr)
  {
    return;
  }

  // First, remove all users with source="config"
  vector<string> keys_to_remove;
  vector<Json::Value> all_users = m_store->query(TABLE_NAME);
  for (const Json::Value& json : all_users)
  {
    if (json.isMember("source") && json["source"].asString() == "config")
    {
      if (json.isMember("callsign") && json["callsign"].isString())
      {
        keys_to_remove.push_back(json["callsign"].asString());
      }
    }
  }

  for (const string& key : keys_to_remove)
  {
    m_store->remove(TABLE_NAME, key);
  }

  // Read [PASSWORDS] section to build group -> password map
  map<string, string> group_passwords;
  std::list<string> password_entries = cfg->listSection("PASSWORDS");
  for (const string& group : password_entries)
  {
    string password;
    if (cfg->getValue("PASSWORDS", group, password))
    {
      group_passwords[group] = password;
    }
  }

  // Read [USERS] section and add users
  std::list<string> user_callsigns = cfg->listSection("USERS");
  for (const string& callsign : user_callsigns)
  {
    string group;
    if (!cfg->getValue("USERS", callsign, group))
    {
      cerr << "Warning: User " << callsign << " has no group defined"
           << endl;
      continue;
    }

    // Look up password for this group
    string password;
    map<string, string>::const_iterator it = group_passwords.find(group);
    if (it != group_passwords.end())
    {
      password = it->second;
    }
    else
    {
      cerr << "Warning: Group " << group << " has no password defined"
           << endl;
      // Continue anyway with empty password
    }

    // Don't overwrite users with source="database"
    Json::Value existing;
    if (m_store->get(TABLE_NAME, callsign, existing))
    {
      if (existing.isMember("source") &&
          existing["source"].asString() == "database")
      {
        continue;
      }
    }

    // Create new user entry
    UserEntry entry;
    entry.callsign = callsign;
    entry.group = group;
    entry.password = password;
    entry.enabled = true;
    entry.created_at = time(nullptr);
    entry.updated_at = entry.created_at;
    entry.last_seen = 0;
    entry.login_count = 0;
    entry.metadata = Json::Value(Json::objectValue);
    entry.source = "config";

    Json::Value json = entryToJson(entry);
    m_store->set(TABLE_NAME, callsign, json);
  }
}  /* UserDatabase::mergeFromConfig */


/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/

Json::Value UserDatabase::entryToJson(const UserEntry& entry)
{
  Json::Value json(Json::objectValue);

  json["callsign"] = entry.callsign;
  json["group"] = entry.group;
  json["password"] = entry.password;
  json["enabled"] = entry.enabled;
  json["created_at"] = Json::Value::Int64(entry.created_at);
  json["updated_at"] = Json::Value::Int64(entry.updated_at);
  json["last_seen"] = Json::Value::Int64(entry.last_seen);
  json["login_count"] = entry.login_count;
  json["metadata"] = entry.metadata;
  json["source"] = entry.source;

  return json;
}  /* UserDatabase::entryToJson */


bool UserDatabase::jsonToEntry(const Json::Value& json, UserEntry& entry)
{
  if (!json.isObject())
  {
    return false;
  }

  // Required fields
  if (!json.isMember("callsign") || !json["callsign"].isString())
  {
    return false;
  }
  entry.callsign = json["callsign"].asString();

  // Optional fields with defaults
  entry.group = json.isMember("group") && json["group"].isString() ?
                json["group"].asString() : "";

  entry.password = json.isMember("password") && json["password"].isString() ?
                   json["password"].asString() : "";

  entry.enabled = json.isMember("enabled") && json["enabled"].isBool() ?
                  json["enabled"].asBool() : true;

  entry.created_at = json.isMember("created_at") && json["created_at"].isInt64() ?
                     static_cast<time_t>(json["created_at"].asInt64()) : 0;

  entry.updated_at = json.isMember("updated_at") && json["updated_at"].isInt64() ?
                     static_cast<time_t>(json["updated_at"].asInt64()) : 0;

  entry.last_seen = json.isMember("last_seen") && json["last_seen"].isInt64() ?
                    static_cast<time_t>(json["last_seen"].asInt64()) : 0;

  entry.login_count = json.isMember("login_count") && json["login_count"].isUInt() ?
                      json["login_count"].asUInt() : 0;

  entry.metadata = json.isMember("metadata") && json["metadata"].isObject() ?
                   json["metadata"] : Json::Value(Json::objectValue);

  entry.source = json.isMember("source") && json["source"].isString() ?
                 json["source"].asString() : "database";

  return true;
}  /* UserDatabase::jsonToEntry */


/*
 * This file has not been truncated
 */
