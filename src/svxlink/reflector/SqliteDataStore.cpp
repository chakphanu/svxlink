/**
@file    SqliteDataStore.cpp
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


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <iostream>
#include <sstream>
#include <cassert>


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

#include "SqliteDataStore.h"


/****************************************************************************
 *
 * Namespaces to use
 *
 ****************************************************************************/

using namespace std;
using namespace DataStore;


/****************************************************************************
 *
 * Defines & typedefs
 *
 ****************************************************************************/

// SQL statements
static const char* SQL_CREATE_TABLE =
    "CREATE TABLE IF NOT EXISTS kv_store ("
    "  tbl TEXT NOT NULL,"
    "  key TEXT NOT NULL,"
    "  value TEXT NOT NULL,"
    "  created_at INTEGER DEFAULT (strftime('%s', 'now')),"
    "  updated_at INTEGER DEFAULT (strftime('%s', 'now')),"
    "  PRIMARY KEY (tbl, key)"
    ")";

static const char* SQL_CREATE_INDEX_TABLE =
    "CREATE INDEX IF NOT EXISTS idx_kv_table ON kv_store(tbl)";

static const char* SQL_CREATE_INDEX_UPDATED =
    "CREATE INDEX IF NOT EXISTS idx_kv_updated ON kv_store(updated_at)";

static const char* SQL_GET =
    "SELECT value FROM kv_store WHERE tbl = ? AND key = ?";

static const char* SQL_SET =
    "INSERT OR REPLACE INTO kv_store (tbl, key, value, updated_at) "
    "VALUES (?, ?, ?, strftime('%s', 'now'))";

static const char* SQL_REMOVE =
    "DELETE FROM kv_store WHERE tbl = ? AND key = ?";

static const char* SQL_EXISTS =
    "SELECT 1 FROM kv_store WHERE tbl = ? AND key = ? LIMIT 1";

static const char* SQL_KEYS =
    "SELECT key FROM kv_store WHERE tbl = ?";

static const char* SQL_COUNT =
    "SELECT COUNT(*) FROM kv_store WHERE tbl = ?";

static const char* SQL_CLEAR =
    "DELETE FROM kv_store WHERE tbl = ?";

static const char* SQL_QUERY =
    "SELECT value FROM kv_store WHERE tbl = ?";


/****************************************************************************
 *
 * Local class definitions
 *
 ****************************************************************************/

namespace {

/**
 * @brief   RAII wrapper for sqlite3_stmt*
 *
 * This class provides automatic finalization of SQLite prepared statements
 * when the object goes out of scope, preventing resource leaks.
 */
class SqliteStatement
{
  public:
    /**
     * @brief   Construct from existing prepared statement
     * @param   stmt  Prepared statement (takes ownership)
     */
    explicit SqliteStatement(sqlite3_stmt* stmt = nullptr)
      : m_stmt(stmt)
    {
    }

    /**
     * @brief   Destructor - automatically finalizes statement
     */
    ~SqliteStatement(void)
    {
      finalize();
    }

    // Non-copyable
    SqliteStatement(const SqliteStatement&) = delete;
    SqliteStatement& operator=(const SqliteStatement&) = delete;

    // Movable
    SqliteStatement(SqliteStatement&& other) noexcept
      : m_stmt(other.m_stmt)
    {
      other.m_stmt = nullptr;
    }

    SqliteStatement& operator=(SqliteStatement&& other) noexcept
    {
      if (this != &other)
      {
        finalize();
        m_stmt = other.m_stmt;
        other.m_stmt = nullptr;
      }
      return *this;
    }

    /**
     * @brief   Get raw statement pointer
     * @return  sqlite3_stmt pointer
     */
    sqlite3_stmt* get(void) const { return m_stmt; }

    /**
     * @brief   Check if statement is valid
     * @return  true if statement is not null
     */
    bool isValid(void) const { return m_stmt != nullptr; }

    /**
     * @brief   Reset statement for reuse
     */
    void reset(void)
    {
      if (m_stmt != nullptr)
      {
        sqlite3_reset(m_stmt);
      }
    }

    /**
     * @brief   Bind text value to parameter
     * @param   idx    Parameter index (1-based)
     * @param   value  Text value to bind
     */
    void bindText(int idx, const std::string& value)
    {
      if (m_stmt != nullptr)
      {
        sqlite3_bind_text(m_stmt, idx, value.c_str(), -1, SQLITE_TRANSIENT);
      }
    }

    /**
     * @brief   Execute one step of the statement
     * @return  SQLite result code
     */
    int step(void)
    {
      if (m_stmt == nullptr)
      {
        return SQLITE_ERROR;
      }
      return sqlite3_step(m_stmt);
    }

    /**
     * @brief   Get text column value safely
     * @param   col  Column index (0-based)
     * @return  Text value or empty string if null
     */
    std::string columnText(int col) const
    {
      if (m_stmt == nullptr)
      {
        return std::string();
      }
      const unsigned char* text = sqlite3_column_text(m_stmt, col);
      if (text == nullptr)
      {
        return std::string();
      }
      // sqlite3_column_text returns UTF-8 encoded text which is safe to cast
      return std::string(reinterpret_cast<const char*>(text));
    }

    /**
     * @brief   Get int64 column value
     * @param   col  Column index (0-based)
     * @return  Integer value or 0 if invalid
     */
    int64_t columnInt64(int col) const
    {
      if (m_stmt == nullptr)
      {
        return 0;
      }
      return sqlite3_column_int64(m_stmt, col);
    }

  private:
    sqlite3_stmt* m_stmt;

    void finalize(void)
    {
      if (m_stmt != nullptr)
      {
        sqlite3_finalize(m_stmt);
        m_stmt = nullptr;
      }
    }
};  /* class SqliteStatement */


/**
 * @brief   RAII scope guard for SQLite transactions
 *
 * This class provides automatic rollback of uncommitted transactions
 * when the object goes out of scope, ensuring transaction safety.
 */
class SqliteTransaction
{
  public:
    /**
     * @brief   Begin a transaction
     * @param   db  Database connection
     */
    explicit SqliteTransaction(sqlite3* db)
      : m_db(db), m_active(false), m_committed(false)
    {
      assert(db != nullptr);
      char* err = nullptr;
      if (sqlite3_exec(m_db, "BEGIN TRANSACTION", nullptr, nullptr, &err) == SQLITE_OK)
      {
        m_active = true;
      }
      else
      {
        if (err != nullptr)
        {
          std::cerr << "*** ERROR: Failed to begin transaction: " << err << std::endl;
          sqlite3_free(err);
        }
      }
    }

    /**
     * @brief   Destructor - auto rollback if not committed
     */
    ~SqliteTransaction(void)
    {
      if (m_active && !m_committed)
      {
        rollback();
      }
    }

    // Non-copyable, non-movable
    SqliteTransaction(const SqliteTransaction&) = delete;
    SqliteTransaction& operator=(const SqliteTransaction&) = delete;

    /**
     * @brief   Check if transaction is active
     * @return  true if transaction started successfully
     */
    bool isActive(void) const { return m_active; }

    /**
     * @brief   Commit the transaction
     * @return  true on success
     */
    bool commit(void)
    {
      if (!m_active || m_committed)
      {
        return false;
      }
      char* err = nullptr;
      if (sqlite3_exec(m_db, "COMMIT", nullptr, nullptr, &err) == SQLITE_OK)
      {
        m_committed = true;
        m_active = false;
        return true;
      }
      if (err != nullptr)
      {
        std::cerr << "*** ERROR: Failed to commit transaction: " << err << std::endl;
        sqlite3_free(err);
      }
      return false;
    }

    /**
     * @brief   Rollback the transaction
     */
    void rollback(void)
    {
      if (m_active && !m_committed)
      {
        sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
        m_active = false;
      }
    }

  private:
    sqlite3*  m_db;
    bool      m_active;
    bool      m_committed;
};  /* class SqliteTransaction */

/**
 * @brief   Safely get text column from raw SQLite statement
 * @param   stmt  The prepared statement
 * @param   col   Column index (0-based)
 * @return  Text value or empty string if null
 *
 * Helper function for use with raw sqlite3_stmt pointers that we don't own.
 */
inline std::string getColumnText(sqlite3_stmt* stmt, int col)
{
  const unsigned char* text = sqlite3_column_text(stmt, col);
  if (text == nullptr)
  {
    return std::string();
  }
  return std::string(reinterpret_cast<const char*>(text));
}

} // anonymous namespace


/****************************************************************************
 *
 * Local functions
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



/****************************************************************************
 *
 * Public member functions
 *
 ****************************************************************************/

SqliteDataStore::SqliteDataStore(const std::string& db_path)
  : m_db_path(db_path),
    m_db(nullptr),
    m_stmt_get(nullptr),
    m_stmt_set(nullptr),
    m_stmt_remove(nullptr),
    m_stmt_exists(nullptr),
    m_stmt_keys(nullptr),
    m_stmt_count(nullptr),
    m_stmt_clear(nullptr)
{
  // Precondition: db_path must not be empty
  assert(!db_path.empty() && "Database path must not be empty");
} /* SqliteDataStore::SqliteDataStore */


SqliteDataStore::~SqliteDataStore(void)
{
  close();
} /* SqliteDataStore::~SqliteDataStore */


bool SqliteDataStore::open(void)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_db != nullptr)
  {
    return true;  // Already open
  }

  int rc = sqlite3_open(m_db_path.c_str(), &m_db);
  if (rc != SQLITE_OK)
  {
    cerr << "*** ERROR: Failed to open SQLite database '" << m_db_path
         << "': " << sqlite3_errmsg(m_db) << endl;
    sqlite3_close(m_db);
    m_db = nullptr;
    return false;
  }

  // Enable WAL mode for better concurrent performance
  char* err_msg = nullptr;
  rc = sqlite3_exec(m_db, "PRAGMA journal_mode=WAL", nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK)
  {
    cerr << "*** WARNING: Failed to enable WAL mode: " << err_msg << endl;
    sqlite3_free(err_msg);
  }

  // Initialize schema
  if (!initSchema())
  {
    sqlite3_close(m_db);
    m_db = nullptr;
    return false;
  }

  // Prepare statements
  if (!prepareStatements())
  {
    sqlite3_close(m_db);
    m_db = nullptr;
    return false;
  }

  cout << "DataStore: Opened SQLite database '" << m_db_path << "'" << endl;
  return true;
} /* SqliteDataStore::open */


bool SqliteDataStore::close(void)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_db == nullptr)
  {
    return true;  // Already closed
  }

  finalizeStatements();

  int rc = sqlite3_close(m_db);
  if (rc != SQLITE_OK)
  {
    cerr << "*** ERROR: Failed to close SQLite database: "
         << sqlite3_errmsg(m_db) << endl;
    return false;
  }

  m_db = nullptr;
  cout << "DataStore: Closed SQLite database" << endl;
  return true;
} /* SqliteDataStore::close */


bool SqliteDataStore::isOpen(void) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_db != nullptr;
} /* SqliteDataStore::isOpen */


bool SqliteDataStore::flush(void)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_db == nullptr)
  {
    return false;
  }

  // Force WAL checkpoint
  int rc = sqlite3_wal_checkpoint_v2(m_db, nullptr, SQLITE_CHECKPOINT_FULL,
                                     nullptr, nullptr);
  return rc == SQLITE_OK;
} /* SqliteDataStore::flush */


bool SqliteDataStore::get(const std::string& table, const std::string& key,
                           Json::Value& value)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_db == nullptr || m_stmt_get == nullptr)
  {
    return false;
  }

  sqlite3_reset(m_stmt_get);
  sqlite3_bind_text(m_stmt_get, 1, table.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(m_stmt_get, 2, key.c_str(), -1, SQLITE_TRANSIENT);

  int rc = sqlite3_step(m_stmt_get);
  if (rc == SQLITE_ROW)
  {
    std::string json_str = getColumnText(m_stmt_get, 0);
    if (!json_str.empty())
    {
      return stringToJson(json_str, value);
    }
  }

  return false;
} /* SqliteDataStore::get */


bool SqliteDataStore::set(const std::string& table, const std::string& key,
                           const Json::Value& value)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_db == nullptr || m_stmt_set == nullptr)
  {
    return false;
  }

  std::string json_str = jsonToString(value);

  sqlite3_reset(m_stmt_set);
  sqlite3_bind_text(m_stmt_set, 1, table.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(m_stmt_set, 2, key.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(m_stmt_set, 3, json_str.c_str(), -1, SQLITE_TRANSIENT);

  int rc = sqlite3_step(m_stmt_set);
  if (rc != SQLITE_DONE)
  {
    cerr << "*** ERROR: Failed to set key '" << key << "' in table '" << table
         << "': " << sqlite3_errmsg(m_db) << endl;
    return false;
  }

  return true;
} /* SqliteDataStore::set */


bool SqliteDataStore::remove(const std::string& table, const std::string& key)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_db == nullptr || m_stmt_remove == nullptr)
  {
    return false;
  }

  sqlite3_reset(m_stmt_remove);
  sqlite3_bind_text(m_stmt_remove, 1, table.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(m_stmt_remove, 2, key.c_str(), -1, SQLITE_TRANSIENT);

  int rc = sqlite3_step(m_stmt_remove);
  return rc == SQLITE_DONE;
} /* SqliteDataStore::remove */


bool SqliteDataStore::exists(const std::string& table, const std::string& key)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_db == nullptr || m_stmt_exists == nullptr)
  {
    return false;
  }

  sqlite3_reset(m_stmt_exists);
  sqlite3_bind_text(m_stmt_exists, 1, table.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(m_stmt_exists, 2, key.c_str(), -1, SQLITE_TRANSIENT);

  int rc = sqlite3_step(m_stmt_exists);
  return rc == SQLITE_ROW;
} /* SqliteDataStore::exists */


std::vector<std::string> SqliteDataStore::keys(const std::string& table)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  std::vector<std::string> result;

  if (m_db == nullptr || m_stmt_keys == nullptr)
  {
    return result;
  }

  sqlite3_reset(m_stmt_keys);
  sqlite3_bind_text(m_stmt_keys, 1, table.c_str(), -1, SQLITE_TRANSIENT);

  while (sqlite3_step(m_stmt_keys) == SQLITE_ROW)
  {
    std::string key_str = getColumnText(m_stmt_keys, 0);
    if (!key_str.empty())
    {
      result.push_back(key_str);
    }
  }

  return result;
} /* SqliteDataStore::keys */


std::vector<Json::Value> SqliteDataStore::query(
    const std::string& table,
    const std::function<bool(const Json::Value&)>& filter)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  std::vector<Json::Value> result;

  if (m_db == nullptr)
  {
    return result;
  }

  // Use RAII wrapper for temporary statement - auto finalize on scope exit
  sqlite3_stmt* raw_stmt = nullptr;
  int rc = sqlite3_prepare_v2(m_db, SQL_QUERY, -1, &raw_stmt, nullptr);
  if (rc != SQLITE_OK)
  {
    cerr << "*** ERROR: Failed to prepare query statement: "
         << sqlite3_errmsg(m_db) << endl;
    return result;
  }

  // RAII wrapper takes ownership - auto finalize on scope exit
  SqliteStatement stmt(raw_stmt);

  stmt.bindText(1, table);

  while (stmt.step() == SQLITE_ROW)
  {
    std::string json_str = stmt.columnText(0);
    if (!json_str.empty())
    {
      Json::Value value;
      if (stringToJson(json_str, value))
      {
        // Apply filter if provided
        if (!filter || filter(value))
        {
          result.push_back(value);
        }
      }
    }
  }

  // No need for sqlite3_finalize - SqliteStatement destructor handles it
  return result;
} /* SqliteDataStore::query */


size_t SqliteDataStore::count(const std::string& table)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_db == nullptr || m_stmt_count == nullptr)
  {
    return 0;
  }

  sqlite3_reset(m_stmt_count);
  sqlite3_bind_text(m_stmt_count, 1, table.c_str(), -1, SQLITE_TRANSIENT);

  int rc = sqlite3_step(m_stmt_count);
  if (rc == SQLITE_ROW)
  {
    return static_cast<size_t>(sqlite3_column_int64(m_stmt_count, 0));
  }

  return 0;
} /* SqliteDataStore::count */


bool SqliteDataStore::setMultiple(const std::string& table,
                                   const std::map<std::string, Json::Value>& items)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_db == nullptr || m_stmt_set == nullptr)
  {
    return false;
  }

  // Use RAII transaction - auto rollback on scope exit if not committed
  SqliteTransaction txn(m_db);
  if (!txn.isActive())
  {
    return false;
  }

  // Insert all items
  for (std::map<std::string, Json::Value>::const_iterator it = items.begin();
       it != items.end(); ++it)
  {
    std::string json_str = jsonToString(it->second);

    sqlite3_reset(m_stmt_set);
    sqlite3_bind_text(m_stmt_set, 1, table.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(m_stmt_set, 2, it->first.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(m_stmt_set, 3, json_str.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(m_stmt_set);
    if (rc != SQLITE_DONE)
    {
      cerr << "*** ERROR: Failed to set key '" << it->first
           << "': " << sqlite3_errmsg(m_db) << endl;
      // Transaction will auto-rollback when txn goes out of scope
      return false;
    }
  }

  // Commit transaction - if this fails, destructor will rollback
  return txn.commit();
} /* SqliteDataStore::setMultiple */


bool SqliteDataStore::clear(const std::string& table)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_db == nullptr || m_stmt_clear == nullptr)
  {
    return false;
  }

  sqlite3_reset(m_stmt_clear);
  sqlite3_bind_text(m_stmt_clear, 1, table.c_str(), -1, SQLITE_TRANSIENT);

  int rc = sqlite3_step(m_stmt_clear);
  return rc == SQLITE_DONE;
} /* SqliteDataStore::clear */


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

bool SqliteDataStore::initSchema(void)
{
  char* err_msg = nullptr;

  // Create table
  int rc = sqlite3_exec(m_db, SQL_CREATE_TABLE, nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK)
  {
    cerr << "*** ERROR: Failed to create kv_store table: " << err_msg << endl;
    sqlite3_free(err_msg);
    return false;
  }

  // Create indexes
  rc = sqlite3_exec(m_db, SQL_CREATE_INDEX_TABLE, nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK)
  {
    cerr << "*** ERROR: Failed to create table index: " << err_msg << endl;
    sqlite3_free(err_msg);
    return false;
  }

  rc = sqlite3_exec(m_db, SQL_CREATE_INDEX_UPDATED, nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK)
  {
    cerr << "*** ERROR: Failed to create updated_at index: " << err_msg << endl;
    sqlite3_free(err_msg);
    return false;
  }

  return true;
} /* SqliteDataStore::initSchema */


bool SqliteDataStore::prepareStatements(void)
{
  int rc;

  rc = sqlite3_prepare_v2(m_db, SQL_GET, -1, &m_stmt_get, nullptr);
  if (rc != SQLITE_OK)
  {
    cerr << "*** ERROR: Failed to prepare GET statement: "
         << sqlite3_errmsg(m_db) << endl;
    return false;
  }

  rc = sqlite3_prepare_v2(m_db, SQL_SET, -1, &m_stmt_set, nullptr);
  if (rc != SQLITE_OK)
  {
    cerr << "*** ERROR: Failed to prepare SET statement: "
         << sqlite3_errmsg(m_db) << endl;
    return false;
  }

  rc = sqlite3_prepare_v2(m_db, SQL_REMOVE, -1, &m_stmt_remove, nullptr);
  if (rc != SQLITE_OK)
  {
    cerr << "*** ERROR: Failed to prepare REMOVE statement: "
         << sqlite3_errmsg(m_db) << endl;
    return false;
  }

  rc = sqlite3_prepare_v2(m_db, SQL_EXISTS, -1, &m_stmt_exists, nullptr);
  if (rc != SQLITE_OK)
  {
    cerr << "*** ERROR: Failed to prepare EXISTS statement: "
         << sqlite3_errmsg(m_db) << endl;
    return false;
  }

  rc = sqlite3_prepare_v2(m_db, SQL_KEYS, -1, &m_stmt_keys, nullptr);
  if (rc != SQLITE_OK)
  {
    cerr << "*** ERROR: Failed to prepare KEYS statement: "
         << sqlite3_errmsg(m_db) << endl;
    return false;
  }

  rc = sqlite3_prepare_v2(m_db, SQL_COUNT, -1, &m_stmt_count, nullptr);
  if (rc != SQLITE_OK)
  {
    cerr << "*** ERROR: Failed to prepare COUNT statement: "
         << sqlite3_errmsg(m_db) << endl;
    return false;
  }

  rc = sqlite3_prepare_v2(m_db, SQL_CLEAR, -1, &m_stmt_clear, nullptr);
  if (rc != SQLITE_OK)
  {
    cerr << "*** ERROR: Failed to prepare CLEAR statement: "
         << sqlite3_errmsg(m_db) << endl;
    return false;
  }

  return true;
} /* SqliteDataStore::prepareStatements */


void SqliteDataStore::finalizeStatements(void)
{
  if (m_stmt_get != nullptr)
  {
    sqlite3_finalize(m_stmt_get);
    m_stmt_get = nullptr;
  }
  if (m_stmt_set != nullptr)
  {
    sqlite3_finalize(m_stmt_set);
    m_stmt_set = nullptr;
  }
  if (m_stmt_remove != nullptr)
  {
    sqlite3_finalize(m_stmt_remove);
    m_stmt_remove = nullptr;
  }
  if (m_stmt_exists != nullptr)
  {
    sqlite3_finalize(m_stmt_exists);
    m_stmt_exists = nullptr;
  }
  if (m_stmt_keys != nullptr)
  {
    sqlite3_finalize(m_stmt_keys);
    m_stmt_keys = nullptr;
  }
  if (m_stmt_count != nullptr)
  {
    sqlite3_finalize(m_stmt_count);
    m_stmt_count = nullptr;
  }
  if (m_stmt_clear != nullptr)
  {
    sqlite3_finalize(m_stmt_clear);
    m_stmt_clear = nullptr;
  }
} /* SqliteDataStore::finalizeStatements */


std::string SqliteDataStore::jsonToString(const Json::Value& value) const
{
  Json::StreamWriterBuilder builder;
  builder["commentStyle"] = "None";
  builder["indentation"] = "";  // Compact output
  return Json::writeString(builder, value);
} /* SqliteDataStore::jsonToString */


bool SqliteDataStore::stringToJson(const std::string& str, Json::Value& value) const
{
  Json::CharReaderBuilder builder;
  std::string errors;
  std::istringstream stream(str);

  if (!Json::parseFromStream(builder, stream, &value, &errors))
  {
    cerr << "*** ERROR: Failed to parse JSON: " << errors << endl;
    return false;
  }

  return true;
} /* SqliteDataStore::stringToJson */


/****************************************************************************
 *
 * Factory function implementation
 *
 ****************************************************************************/

namespace DataStore
{

std::unique_ptr<IDataStore> createDataStore(const std::string& type,
                                            const std::string& connection_string)
{
  if (type == "sqlite")
  {
    return std::unique_ptr<IDataStore>(new SqliteDataStore(connection_string));
  }
  // Future: add "mysql", "redis", "memory" implementations

  cerr << "*** ERROR: Unknown datastore type: " << type << endl;
  return nullptr;
} /* createDataStore */

}  /* namespace DataStore */


/*
 * This file has not been truncated
 */
