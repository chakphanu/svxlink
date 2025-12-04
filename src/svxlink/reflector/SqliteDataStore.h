/**
@file    SqliteDataStore.h
@brief   SQLite implementation of IDataStore interface
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

#ifndef SQLITE_DATA_STORE_INCLUDED
#define SQLITE_DATA_STORE_INCLUDED


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <string>
#include <mutex>
#include <sqlite3.h>


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
@brief  SQLite implementation of IDataStore interface
@author SvxLink Contributors
@date   2025

This class implements the IDataStore interface using SQLite as the backend.
Data is stored in a single table with (table_name, key, value) structure,
where value is a JSON blob.

Thread-safety: All operations are protected by a mutex.

Database schema:
@code
CREATE TABLE IF NOT EXISTS kv_store (
    tbl TEXT NOT NULL,
    key TEXT NOT NULL,
    value TEXT NOT NULL,
    created_at INTEGER DEFAULT (strftime('%s', 'now')),
    updated_at INTEGER DEFAULT (strftime('%s', 'now')),
    PRIMARY KEY (tbl, key)
);
@endcode
*/
class SqliteDataStore : public IDataStore
{
  public:
    /**
     * @brief   Constructor
     * @param   db_path  Path to the SQLite database file
     *
     * The database file will be created if it doesn't exist.
     */
    explicit SqliteDataStore(const std::string& db_path);

    /**
     * @brief   Destructor
     *
     * Closes the database connection if open.
     */
    virtual ~SqliteDataStore(void);

    // Disable copy
    SqliteDataStore(const SqliteDataStore&) = delete;
    SqliteDataStore& operator=(const SqliteDataStore&) = delete;

    // IDataStore interface implementation
    virtual bool open(void) override;
    virtual bool close(void) override;
    virtual bool isOpen(void) const override;
    virtual bool flush(void) override;

    virtual bool get(const std::string& table, const std::string& key,
                     Json::Value& value) override;
    virtual bool set(const std::string& table, const std::string& key,
                     const Json::Value& value) override;
    virtual bool remove(const std::string& table, const std::string& key) override;
    virtual bool exists(const std::string& table, const std::string& key) override;

    virtual std::vector<std::string> keys(const std::string& table) override;
    virtual std::vector<Json::Value> query(
        const std::string& table,
        const std::function<bool(const Json::Value&)>& filter = nullptr) override;
    virtual size_t count(const std::string& table) override;

    virtual bool setMultiple(const std::string& table,
                             const std::map<std::string, Json::Value>& items) override;
    virtual bool clear(const std::string& table) override;

  private:
    std::string             m_db_path;
    sqlite3*                m_db;
    mutable std::mutex      m_mutex;

    // Prepared statements (lazy initialized)
    sqlite3_stmt*           m_stmt_get;
    sqlite3_stmt*           m_stmt_set;
    sqlite3_stmt*           m_stmt_remove;
    sqlite3_stmt*           m_stmt_exists;
    sqlite3_stmt*           m_stmt_keys;
    sqlite3_stmt*           m_stmt_count;
    sqlite3_stmt*           m_stmt_clear;

    /**
     * @brief   Initialize the database schema
     * @return  true on success
     */
    bool initSchema(void);

    /**
     * @brief   Prepare all SQL statements
     * @return  true on success
     */
    bool prepareStatements(void);

    /**
     * @brief   Finalize all prepared statements
     */
    void finalizeStatements(void);

    /**
     * @brief   Convert Json::Value to string for storage
     * @param   value  The JSON value
     * @return  JSON string representation
     */
    std::string jsonToString(const Json::Value& value) const;

    /**
     * @brief   Parse JSON string from storage
     * @param   str    The JSON string
     * @param   value  Output JSON value
     * @return  true on success
     */
    bool stringToJson(const std::string& str, Json::Value& value) const;

};  /* class SqliteDataStore */


}  /* namespace DataStore */

#endif /* SQLITE_DATA_STORE_INCLUDED */


/*
 * This file has not been truncated
 */
