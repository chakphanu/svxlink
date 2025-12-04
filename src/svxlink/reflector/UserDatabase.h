/**
@file    UserDatabase.h
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

#ifndef USER_DATABASE_INCLUDED
#define USER_DATABASE_INCLUDED


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <string>
#include <vector>
#include <mutex>
#include <ctime>
#include <json/json.h>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncConfig.h>


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
 * Namespace
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Forward declarations of classes inside of the declared namespace
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
@brief  User database entry structure
@author SvxLink Contributors
@date   2025

This structure represents a single user entry in the database.
*/
struct UserEntry
{
  std::string callsign;       /**< Primary key - user callsign */
  std::string group;          /**< Password group name */
  std::string password;       /**< Plain text password for HMAC */
  bool        enabled;        /**< Whether user is enabled */
  time_t      created_at;     /**< Timestamp when user was created */
  time_t      updated_at;     /**< Timestamp when user was last updated */
  time_t      last_seen;      /**< Timestamp when user last logged in */
  unsigned    login_count;    /**< Number of times user has logged in */
  Json::Value metadata;       /**< Extensible metadata storage */
  std::string source;         /**< "config" or "database" */
};  /* struct UserEntry */


/**
@brief  User database management class
@author SvxLink Contributors
@date   2025

This class manages user data using the IDataStore interface. It provides
thread-safe operations for adding, retrieving, updating, and deleting users.

The class supports two sources of user data:
- Configuration file: Users loaded from [USERS] and [PASSWORDS] sections
- Database: Users added programmatically or through admin interface

Users from configuration files have source="config" and are refreshed on
configuration reload. Users from database have source="database" and persist
across configuration reloads.

Thread-safety: All public methods are protected with a mutex.

Example usage:
@code
std::unique_ptr<DataStore::IDataStore> store =
    DataStore::createDataStore("sqlite", "/var/lib/svxreflector/users.db");
store->open();

UserDatabase userDb(store.get());

// Add a user
Json::Value metadata;
metadata["email"] = "sm0abc@example.com";
userDb.addUser("SM0ABC", "admins", "secret123", metadata);

// Get a user
UserEntry user;
if (userDb.getUser("SM0ABC", user))
{
    std::cout << "User: " << user.callsign << std::endl;
}

// Merge from configuration
Async::Config cfg;
cfg.open("/etc/svxlink/svxreflector.conf");
userDb.mergeFromConfig(&cfg);
@endcode
*/
class UserDatabase
{
  public:
    /**
     * @brief   Table name constant for users
     */
    static const std::string TABLE_NAME;

    /**
     * @brief   Constructor
     * @param   store  Pointer to IDataStore implementation
     *
     * Creates a new UserDatabase instance using the provided data store.
     * The data store must be opened before use.
     */
    UserDatabase(DataStore::IDataStore* store);

    /**
     * @brief   Destructor
     */
    ~UserDatabase(void);

    /**
     * @brief   Add a new user to the database
     * @param   callsign   User callsign (primary key)
     * @param   group      Password group name
     * @param   password   Plain text password for HMAC
     * @param   metadata   Optional metadata (default empty)
     * @return  true on success, false if user already exists or error
     *
     * Adds a new user with source="database". If the user already exists,
     * this method returns false.
     */
    bool addUser(const std::string& callsign, const std::string& group,
                 const std::string& password,
                 const Json::Value& metadata = Json::Value(Json::objectValue));

    /**
     * @brief   Get user entry by callsign
     * @param   callsign  User callsign to look up
     * @param   entry     Output parameter for user entry
     * @return  true if found, false otherwise
     *
     * Retrieves the user entry for the given callsign.
     */
    bool getUser(const std::string& callsign, UserEntry& entry);

    /**
     * @brief   Update user fields
     * @param   callsign  User callsign to update
     * @param   updates   JSON object with fields to update
     * @return  true on success, false if user not found or error
     *
     * Updates the specified fields of a user. The updates object should
     * contain only the fields to update, e.g.:
     * @code
     * Json::Value updates;
     * updates["enabled"] = false;
     * updates["metadata"]["reason"] = "suspended";
     * userDb.updateUser("SM0ABC", updates);
     * @endcode
     *
     * The updated_at timestamp is automatically set to current time.
     */
    bool updateUser(const std::string& callsign, const Json::Value& updates);

    /**
     * @brief   Delete user by callsign
     * @param   callsign  User callsign to delete
     * @return  true on success, false if user not found or error
     *
     * Removes the user from the database.
     */
    bool deleteUser(const std::string& callsign);

    /**
     * @brief   List all users
     * @return  Vector of all user entries
     *
     * Returns all users in the database.
     */
    std::vector<UserEntry> list(void);

    /**
     * @brief   Check if user exists
     * @param   callsign  User callsign to check
     * @return  true if user exists
     */
    bool exists(const std::string& callsign);

    /**
     * @brief   Get number of users in database
     * @return  Number of users
     */
    size_t size(void);

    /**
     * @brief   Look up user password for authentication
     * @param   callsign  User callsign
     * @return  Password string, or empty string if not found
     *
     * This is a convenience method for authentication code.
     * Returns the plain text password for HMAC verification.
     */
    std::string lookupUserKey(const std::string& callsign);

    /**
     * @brief   Record user login event
     * @param   callsign    User callsign
     * @param   remote_ip   Remote IP address
     * @param   proto_ver   Protocol version
     *
     * Updates last_seen timestamp and increments login_count.
     * Also updates metadata with last_ip and last_proto_ver.
     */
    void recordLogin(const std::string& callsign,
                     const std::string& remote_ip,
                     const std::string& proto_ver);

    /**
     * @brief   Record user logout event
     * @param   callsign  User callsign
     *
     * Currently a placeholder for future functionality.
     */
    void recordLogout(const std::string& callsign);

    /**
     * @brief   Merge users from configuration file
     * @param   cfg  Pointer to Async::Config object
     *
     * Reads [USERS] and [PASSWORDS] sections from configuration and adds
     * users with source="config". The [USERS] section maps callsigns to
     * groups, and [PASSWORDS] section maps groups to passwords.
     *
     * Example configuration:
     * @code
     * [USERS]
     * SM0ABC=admins
     * SM1XYZ=users
     *
     * [PASSWORDS]
     * admins=admin_secret
     * users=user_secret
     * @endcode
     *
     * Users with source="config" are removed and re-added on each call.
     * Users with source="database" are not affected.
     */
    void mergeFromConfig(Async::Config* cfg);

  private:
    DataStore::IDataStore*  m_store;      /**< Data store backend */
    std::mutex              m_mutex;      /**< Thread safety mutex */

    /**
     * @brief   Convert UserEntry to JSON
     * @param   entry  User entry to convert
     * @return  JSON representation
     */
    Json::Value entryToJson(const UserEntry& entry);

    /**
     * @brief   Convert JSON to UserEntry
     * @param   json   JSON object
     * @param   entry  Output parameter for user entry
     * @return  true on success, false on parse error
     */
    bool jsonToEntry(const Json::Value& json, UserEntry& entry);

};  /* class UserDatabase */


#endif /* USER_DATABASE_INCLUDED */


/*
 * This file has not been truncated
 */
