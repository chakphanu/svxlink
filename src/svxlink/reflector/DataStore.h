/**
@file    DataStore.h
@brief   Abstract interface for persistent key-value storage
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

#ifndef DATA_STORE_INCLUDED
#define DATA_STORE_INCLUDED


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
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

namespace DataStore
{


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
@brief  Abstract interface for persistent key-value storage
@author SvxLink Contributors
@date   2025

This interface provides a generic key-value storage abstraction that can be
implemented by different backends (SQLite, MySQL, Redis, etc.).

The storage is organized into tables, where each table contains key-value
pairs. Values are stored as JSON for flexibility.

Thread-safety: Implementations should be thread-safe.

Example usage:
@code
std::unique_ptr<IDataStore> store = createDataStore("sqlite", "/path/to/db.sqlite");
store->open();

Json::Value user;
user["name"] = "SM0ABC";
user["enabled"] = true;
store->set("users", "SM0ABC", user);

Json::Value result;
if (store->get("users", "SM0ABC", result))
{
    std::cout << "Found user: " << result["name"].asString() << std::endl;
}
@endcode
*/
class IDataStore
{
  public:
    /**
     * @brief   Virtual destructor
     */
    virtual ~IDataStore(void) {}

    /**
     * @brief   Open the data store connection
     * @return  true on success, false on error
     *
     * Must be called before any other operations.
     * For SQLite, this creates the database file if it doesn't exist.
     */
    virtual bool open(void) = 0;

    /**
     * @brief   Close the data store connection
     * @return  true on success, false on error
     *
     * Flushes any pending writes and releases resources.
     */
    virtual bool close(void) = 0;

    /**
     * @brief   Check if the data store is open
     * @return  true if open
     */
    virtual bool isOpen(void) const = 0;

    /**
     * @brief   Force write any pending data to persistent storage
     * @return  true on success, false on error
     */
    virtual bool flush(void) = 0;

    /**
     * @brief   Get a value from the store
     * @param   table  The table/namespace name
     * @param   key    The key to look up
     * @param   value  Output parameter for the value
     * @return  true if found, false if not found or error
     */
    virtual bool get(const std::string& table, const std::string& key,
                     Json::Value& value) = 0;

    /**
     * @brief   Set a value in the store
     * @param   table  The table/namespace name
     * @param   key    The key to set
     * @param   value  The value to store
     * @return  true on success, false on error
     *
     * Creates the entry if it doesn't exist, updates if it does.
     */
    virtual bool set(const std::string& table, const std::string& key,
                     const Json::Value& value) = 0;

    /**
     * @brief   Remove a value from the store
     * @param   table  The table/namespace name
     * @param   key    The key to remove
     * @return  true on success (including if key didn't exist)
     */
    virtual bool remove(const std::string& table, const std::string& key) = 0;

    /**
     * @brief   Check if a key exists
     * @param   table  The table/namespace name
     * @param   key    The key to check
     * @return  true if the key exists
     */
    virtual bool exists(const std::string& table, const std::string& key) = 0;

    /**
     * @brief   Get all keys in a table
     * @param   table  The table/namespace name
     * @return  Vector of all keys in the table
     */
    virtual std::vector<std::string> keys(const std::string& table) = 0;

    /**
     * @brief   Query values from a table with optional filter
     * @param   table   The table/namespace name
     * @param   filter  Optional filter function (return true to include)
     * @return  Vector of matching values
     *
     * If filter is nullptr, returns all values in the table.
     */
    virtual std::vector<Json::Value> query(
        const std::string& table,
        const std::function<bool(const Json::Value&)>& filter = nullptr) = 0;

    /**
     * @brief   Count entries in a table
     * @param   table  The table/namespace name
     * @return  Number of entries
     */
    virtual size_t count(const std::string& table) = 0;

    /**
     * @brief   Set multiple values in a single transaction
     * @param   table  The table/namespace name
     * @param   items  Map of key -> value pairs to set
     * @return  true if all items were set successfully
     *
     * This is atomic - either all items are set or none are.
     */
    virtual bool setMultiple(const std::string& table,
                             const std::map<std::string, Json::Value>& items) = 0;

    /**
     * @brief   Remove all entries from a table
     * @param   table  The table/namespace name
     * @return  true on success
     */
    virtual bool clear(const std::string& table) = 0;

};  /* class IDataStore */


/**
 * @brief   Factory function to create a data store
 * @param   type               The backend type ("sqlite", "memory")
 * @param   connection_string  Connection string (file path for SQLite)
 * @return  Unique pointer to the data store, or nullptr on error
 *
 * Supported types:
 * - "sqlite": SQLite database (connection_string is file path)
 * - "memory": In-memory store for testing (connection_string ignored)
 */
std::unique_ptr<IDataStore> createDataStore(const std::string& type,
                                            const std::string& connection_string);


}  /* namespace DataStore */

#endif /* DATA_STORE_INCLUDED */


/*
 * This file has not been truncated
 */
