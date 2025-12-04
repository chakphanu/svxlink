/**
@file    AdminHandler.cpp
@brief   HTTP Admin API handler for SvxReflector
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
#include <algorithm>
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

#include "AdminHandler.h"
#include "Reflector.h"
#include "ReflectorClient.h"


/****************************************************************************
 *
 * Namespaces to use
 *
 ****************************************************************************/

using namespace std;
using namespace Async;


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
  /**
   * @brief   Check if a string starts with a prefix
   * @param   str    The string to check
   * @param   prefix The prefix to look for
   * @return  true if str starts with prefix
   */
  bool startsWith(const std::string& str, const std::string& prefix)
  {
    if (str.length() < prefix.length())
    {
      return false;
    }
    return str.compare(0, prefix.length(), prefix) == 0;
  }

  /**
   * @brief   Extract path component from URL
   * @param   target The request target (may include query string)
   * @return  The path without query string
   */
  std::string extractPath(const std::string& target)
  {
    size_t query_pos = target.find('?');
    if (query_pos != std::string::npos)
    {
      return target.substr(0, query_pos);
    }
    return target;
  }
};


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

AdminHandler::AdminHandler(Reflector& ref, Async::Config& cfg)
  : m_reflector(ref), m_cfg(cfg)
{
  // Read authentication token from config
  if (!m_cfg.getValue("GLOBAL", "ADMIN_API_TOKEN", m_auth_token))
  {
    std::cout << "*** WARNING: ADMIN_API_TOKEN not set. "
              << "Admin API will be disabled." << std::endl;
  }

  // Read mute list database path from config
  std::string mute_db;
  if (m_cfg.getValue("GLOBAL", "ADMIN_MUTE_FILE", mute_db) &&
      !mute_db.empty())
  {
    std::cout << "Admin mute list database: " << mute_db << std::endl;

    // Create SQLite data store
    m_data_store = DataStore::createDataStore("sqlite", mute_db);
    if (m_data_store && m_data_store->open())
    {
      m_mute_list.reset(new MuteList(m_data_store.get()));
      m_user_database.reset(new UserDatabase(m_data_store.get()));
      m_user_database->mergeFromConfig(&m_cfg);
    }
    else
    {
      std::cerr << "*** ERROR: Failed to open mute list database: " << mute_db
                << std::endl;
      m_mute_list.reset(new MuteList());
    }
  }
  else
  {
    std::cout << "Admin mute list persistence disabled (no file configured)"
              << std::endl;
    m_mute_list.reset(new MuteList());
  }
} /* AdminHandler::AdminHandler */


AdminHandler::~AdminHandler(void)
{
  // Close data store
  if (m_data_store)
  {
    m_data_store->close();
  }
} /* AdminHandler::~AdminHandler */


bool AdminHandler::handleRequest(Async::HttpServerConnection* con,
                                  Async::HttpServerConnection::Request& req)
{
  std::string path = extractPath(req.target);

  // Check if this is an admin API request
  if (!startsWith(path, "/admin/"))
  {
    return false;
  }

  // Check if admin API is enabled
  if (m_auth_token.empty())
  {
    sendError(con, 503, "Admin API is not configured");
    return true;
  }

  // Validate authentication token
  if (!validateToken(req))
  {
    sendError(con, 401, "Unauthorized: Invalid or missing token");
    return true;
  }

  // Route to appropriate handler
  if (path == "/admin/mute")
  {
    if (req.method == "POST")
    {
      handleMute(con, req);
    }
    else if (req.method == "GET")
    {
      handleMuteList(con, req);
    }
    else
    {
      sendError(con, 405, "Method not allowed");
    }
  }
  else if (path == "/admin/unmute")
  {
    if (req.method == "POST")
    {
      handleUnmute(con, req);
    }
    else
    {
      sendError(con, 405, "Method not allowed");
    }
  }
  else if (path == "/admin/kick")
  {
    if (req.method == "POST")
    {
      handleKick(con, req);
    }
    else
    {
      sendError(con, 405, "Method not allowed");
    }
  }
  else if (path == "/admin/status")
  {
    if (req.method == "GET")
    {
      handleStatus(con, req);
    }
    else
    {
      sendError(con, 405, "Method not allowed");
    }
  }
  else if (path == "/admin/config")
  {
    if (req.method == "GET")
    {
      handleConfigGet(con, req);
    }
    else if (req.method == "POST")
    {
      handleConfigSet(con, req);
    }
    else
    {
      sendError(con, 405, "Method not allowed");
    }
  }
  else if (path == "/admin/users")
  {
    if (req.method == "GET")
    {
      handleUsersList(con, req);
    }
    else if (req.method == "POST")
    {
      handleUserCreate(con, req);
    }
    else
    {
      sendError(con, 405, "Method not allowed");
    }
  }
  else if (startsWith(path, "/admin/users/"))
  {
    std::string callsign = path.substr(13); // Extract callsign after "/admin/users/"
    if (callsign.empty())
    {
      sendError(con, 400, "Callsign is required");
    }
    else if (req.method == "GET")
    {
      handleUserGet(con, req, callsign);
    }
    else if (req.method == "PUT")
    {
      handleUserUpdate(con, req, callsign);
    }
    else if (req.method == "DELETE")
    {
      handleUserDelete(con, req, callsign);
    }
    else
    {
      sendError(con, 405, "Method not allowed");
    }
  }
  else
  {
    sendError(con, 404, "Admin API endpoint not found");
  }

  return true;
} /* AdminHandler::handleRequest */


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

bool AdminHandler::validateToken(
    const Async::HttpServerConnection::Request& req) const
{
  // Look for Authorization header
  auto it = req.headers.find("Authorization");
  if (it == req.headers.end())
  {
    return false;
  }

  const std::string& auth_header = it->second;
  const std::string bearer_prefix = "Bearer ";

  // Check if it's a Bearer token
  if (!startsWith(auth_header, bearer_prefix))
  {
    return false;
  }

  // Extract token
  std::string token = auth_header.substr(bearer_prefix.length());

  // Compare with configured token
  return token == m_auth_token;
} /* AdminHandler::validateToken */


void AdminHandler::sendJsonResponse(Async::HttpServerConnection* con,
                                     int code, const Json::Value& json)
{
  std::ostringstream os;
  Json::StreamWriterBuilder builder;
  builder["commentStyle"] = "None";
  builder["indentation"] = "  ";
  Json::StreamWriter* writer = builder.newStreamWriter();
  writer->write(json, &os);
  delete writer;

  Async::HttpServerConnection::Response res;
  res.setCode(code);
  res.setContent("application/json", os.str());
  con->write(res);
} /* AdminHandler::sendJsonResponse */


void AdminHandler::sendError(Async::HttpServerConnection* con, int code,
                              const std::string& msg)
{
  Json::Value error;
  error["error"] = msg;
  error["code"] = code;
  sendJsonResponse(con, code, error);
} /* AdminHandler::sendError */


bool AdminHandler::parseJsonBody(
    const Async::HttpServerConnection::Request& req, Json::Value& json)
{
  // Check if there is content to parse
  if (req.content.empty())
  {
    return false;
  }

  // Parse JSON from request body
  Json::CharReaderBuilder builder;
  std::string errors;
  std::istringstream content_stream(req.content);

  if (!Json::parseFromStream(builder, content_stream, &json, &errors))
  {
    std::cerr << "*** WARNING: Failed to parse JSON body: " << errors
              << std::endl;
    return false;
  }

  return true;
} /* AdminHandler::parseJsonBody */


void AdminHandler::handleMute(Async::HttpServerConnection* con,
                               const Async::HttpServerConnection::Request& req)
{
  Json::Value body;
  if (!parseJsonBody(req, body))
  {
    sendError(con, 400, "Invalid JSON body");
    return;
  }

  // Extract parameters
  if (!body.isMember("callsign") || !body["callsign"].isString())
  {
    sendError(con, 400, "Missing or invalid 'callsign' field");
    return;
  }

  std::string callsign = body["callsign"].asString();
  std::string reason = body.isMember("reason") ?
                       body["reason"].asString() : "Admin muted";

  bool success = false;
  if (body.isMember("duration") && body["duration"].isInt())
  {
    unsigned duration = body["duration"].asUInt();
    success = m_mute_list->mute(callsign, duration, reason);
  }
  else
  {
    success = m_mute_list->muteForever(callsign, reason);
  }

  if (success)
  {
    Json::Value response;
    response["success"] = true;
    response["callsign"] = callsign;
    response["message"] = "Node muted successfully";
    sendJsonResponse(con, 200, response);
  }
  else
  {
    sendError(con, 409, "Node is already muted");
  }
} /* AdminHandler::handleMute */


void AdminHandler::handleUnmute(
    Async::HttpServerConnection* con,
    const Async::HttpServerConnection::Request& req)
{
  Json::Value body;
  if (!parseJsonBody(req, body))
  {
    sendError(con, 400, "Invalid JSON body");
    return;
  }

  if (!body.isMember("callsign") || !body["callsign"].isString())
  {
    sendError(con, 400, "Missing or invalid 'callsign' field");
    return;
  }

  std::string callsign = body["callsign"].asString();
  bool success = m_mute_list->unmute(callsign);

  if (success)
  {
    Json::Value response;
    response["success"] = true;
    response["callsign"] = callsign;
    response["message"] = "Node unmuted successfully";
    sendJsonResponse(con, 200, response);
  }
  else
  {
    sendError(con, 404, "Node not found in mute list");
  }
} /* AdminHandler::handleUnmute */


void AdminHandler::handleMuteList(
    Async::HttpServerConnection* con,
    const Async::HttpServerConnection::Request& req)
{
  std::vector<MuteList::Entry> entries = m_mute_list->list();

  Json::Value response;
  response["count"] = static_cast<unsigned>(entries.size());
  response["muted_nodes"] = Json::Value(Json::arrayValue);

  for (const auto& entry : entries)
  {
    Json::Value node;
    node["callsign"] = entry.callsign;
    node["reason"] = entry.reason;
    node["muted_at"] = static_cast<Json::Int64>(entry.muted_at);
    node["expires_at"] = static_cast<Json::Int64>(entry.expires_at);
    node["permanent"] = entry.isPermanent();
    response["muted_nodes"].append(node);
  }

  sendJsonResponse(con, 200, response);
} /* AdminHandler::handleMuteList */


void AdminHandler::handleKick(Async::HttpServerConnection* con,
                               const Async::HttpServerConnection::Request& req)
{
  Json::Value body;
  if (!parseJsonBody(req, body))
  {
    sendError(con, 400, "Invalid JSON body");
    return;
  }

  if (!body.isMember("callsign") || !body["callsign"].isString())
  {
    sendError(con, 400, "Missing required field: callsign");
    return;
  }

  std::string callsign = body["callsign"].asString();
  std::string reason = "Kicked by admin";
  if (body.isMember("reason") && body["reason"].isString())
  {
    reason = body["reason"].asString();
  }

  // Find the client by callsign
  ReflectorClient* client = ReflectorClient::lookup(callsign);
  if (client == 0)
  {
    sendError(con, 404, "Node not found: " + callsign);
    return;
  }

  // Kick the client
  client->kick(reason);

  Json::Value response;
  response["success"] = true;
  response["message"] = "Node " + callsign + " has been kicked";
  response["callsign"] = callsign;
  response["reason"] = reason;
  sendJsonResponse(con, 200, response);
} /* AdminHandler::handleKick */


void AdminHandler::handleStatus(
    Async::HttpServerConnection* con,
    const Async::HttpServerConnection::Request& req)
{
  // Get detailed client information
  std::vector<Json::Value> clients;
  m_reflector.clientDetails(clients);

  Json::Value response;
  response["uptime"] = 0; // TODO: Implement uptime tracking
  response["connected_nodes"] = static_cast<unsigned>(clients.size());
  response["muted_nodes"] = static_cast<unsigned>(m_mute_list->size());
  response["admin_api_version"] = "1.1";

  // Build detailed nodes array with all client info
  Json::Value node_list(Json::arrayValue);
  for (const auto& client : clients)
  {
    Json::Value node;
    node["callsign"] = client["callsign"];
    node["id"] = client["id"];
    node["tg"] = client["tg"];
    node["monitored_tgs"] = client["monitored_tgs"];
    node["proto_ver"] = client["proto_ver"];
    node["ip"] = client["ip"];
    node["port"] = client["port"];
    node["is_blocked"] = client["is_blocked"];

    // Check if muted
    std::string callsign = client["callsign"].asString();
    node["is_muted"] = m_mute_list->isMuted(callsign);

    // Optional fields from status
    if (client.isMember("is_talker"))
    {
      node["is_talker"] = client["is_talker"];
    }
    else
    {
      node["is_talker"] = false;
    }

    if (client.isMember("rx"))
    {
      node["rx"] = client["rx"];
    }

    if (client.isMember("tx"))
    {
      node["tx"] = client["tx"];
    }

    if (client.isMember("qth_name"))
    {
      node["qth_name"] = client["qth_name"];
    }

    node_list.append(node);
  }
  response["nodes"] = node_list;

  sendJsonResponse(con, 200, response);
} /* AdminHandler::handleStatus */


void AdminHandler::handleConfigGet(
    Async::HttpServerConnection* con,
    const Async::HttpServerConnection::Request& req)
{
  Json::Value response;
  response["error"] = "Not yet implemented";
  response["message"] = "Config GET is not yet implemented";
  sendJsonResponse(con, 501, response);
} /* AdminHandler::handleConfigGet */


void AdminHandler::handleConfigSet(
    Async::HttpServerConnection* con,
    const Async::HttpServerConnection::Request& req)
{
  Json::Value response;
  response["error"] = "Not yet implemented";
  response["message"] = "Config SET is not yet implemented";
  sendJsonResponse(con, 501, response);
} /* AdminHandler::handleConfigSet */


void AdminHandler::handleUsersList(
    Async::HttpServerConnection* con,
    const Async::HttpServerConnection::Request& req)
{
  // Check if user database is available
  if (!m_user_database)
  {
    sendError(con, 503, "User database not configured");
    return;
  }

  std::vector<UserEntry> users = m_user_database->list();

  Json::Value response;
  response["count"] = static_cast<unsigned>(users.size());
  response["users"] = Json::Value(Json::arrayValue);

  for (const auto& user : users)
  {
    Json::Value user_json;
    user_json["callsign"] = user.callsign;
    user_json["group"] = user.group;
    user_json["enabled"] = user.enabled;
    user_json["created_at"] = static_cast<Json::Int64>(user.created_at);
    user_json["updated_at"] = static_cast<Json::Int64>(user.updated_at);
    user_json["last_seen"] = static_cast<Json::Int64>(user.last_seen);
    user_json["login_count"] = user.login_count;
    user_json["metadata"] = user.metadata;
    user_json["source"] = user.source;
    response["users"].append(user_json);
  }

  sendJsonResponse(con, 200, response);
} /* AdminHandler::handleUsersList */


void AdminHandler::handleUserGet(
    Async::HttpServerConnection* con,
    const Async::HttpServerConnection::Request& req,
    const std::string& callsign)
{
  // Check if user database is available
  if (!m_user_database)
  {
    sendError(con, 503, "User database not configured");
    return;
  }

  UserEntry user;
  if (!m_user_database->getUser(callsign, user))
  {
    sendError(con, 404, "User not found: " + callsign);
    return;
  }

  Json::Value response;
  response["callsign"] = user.callsign;
  response["group"] = user.group;
  response["enabled"] = user.enabled;
  response["created_at"] = static_cast<Json::Int64>(user.created_at);
  response["updated_at"] = static_cast<Json::Int64>(user.updated_at);
  response["last_seen"] = static_cast<Json::Int64>(user.last_seen);
  response["login_count"] = user.login_count;
  response["metadata"] = user.metadata;
  response["source"] = user.source;

  sendJsonResponse(con, 200, response);
} /* AdminHandler::handleUserGet */


void AdminHandler::handleUserCreate(
    Async::HttpServerConnection* con,
    const Async::HttpServerConnection::Request& req)
{
  // Check if user database is available
  if (!m_user_database)
  {
    sendError(con, 503, "User database not configured");
    return;
  }

  Json::Value body;
  if (!parseJsonBody(req, body))
  {
    sendError(con, 400, "Invalid JSON body");
    return;
  }

  // Validate required fields
  if (!body.isMember("callsign") || !body["callsign"].isString())
  {
    sendError(con, 400, "Missing or invalid 'callsign' field");
    return;
  }

  if (!body.isMember("group") || !body["group"].isString())
  {
    sendError(con, 400, "Missing or invalid 'group' field");
    return;
  }

  if (!body.isMember("password") || !body["password"].isString())
  {
    sendError(con, 400, "Missing or invalid 'password' field");
    return;
  }

  std::string callsign = body["callsign"].asString();
  std::string group = body["group"].asString();
  std::string password = body["password"].asString();

  // Extract optional enabled (default true)
  bool enabled = true;
  if (body.isMember("enabled") && body["enabled"].isBool())
  {
    enabled = body["enabled"].asBool();
  }

  // Extract optional metadata
  Json::Value metadata = Json::Value(Json::objectValue);
  if (body.isMember("metadata") && body["metadata"].isObject())
  {
    metadata = body["metadata"];
  }

  // Try to add user
  bool success = m_user_database->addUser(callsign, group, password, metadata);

  if (success)
  {
    // If enabled=false was specified, update user to disabled
    if (!enabled)
    {
      Json::Value updates;
      updates["enabled"] = false;
      m_user_database->updateUser(callsign, updates);
    }

    // Retrieve the newly created user to return complete data
    UserEntry user;
    m_user_database->getUser(callsign, user);

    Json::Value response;
    response["success"] = true;
    response["message"] = "User created successfully";
    response["user"]["callsign"] = user.callsign;
    response["user"]["group"] = user.group;
    response["user"]["enabled"] = user.enabled;
    response["user"]["created_at"] = static_cast<Json::Int64>(user.created_at);
    response["user"]["updated_at"] = static_cast<Json::Int64>(user.updated_at);
    response["user"]["last_seen"] = static_cast<Json::Int64>(user.last_seen);
    response["user"]["login_count"] = user.login_count;
    response["user"]["metadata"] = user.metadata;
    response["user"]["source"] = user.source;

    sendJsonResponse(con, 201, response);
  }
  else
  {
    sendError(con, 409, "User already exists: " + callsign);
  }
} /* AdminHandler::handleUserCreate */


void AdminHandler::handleUserUpdate(
    Async::HttpServerConnection* con,
    const Async::HttpServerConnection::Request& req,
    const std::string& callsign)
{
  // Check if user database is available
  if (!m_user_database)
  {
    sendError(con, 503, "User database not configured");
    return;
  }

  // Check if user exists
  if (!m_user_database->exists(callsign))
  {
    sendError(con, 404, "User not found: " + callsign);
    return;
  }

  Json::Value body;
  if (!parseJsonBody(req, body))
  {
    sendError(con, 400, "Invalid JSON body");
    return;
  }

  // Build updates object with only provided fields
  Json::Value updates = Json::Value(Json::objectValue);

  if (body.isMember("group") && body["group"].isString())
  {
    updates["group"] = body["group"];
  }

  if (body.isMember("password") && body["password"].isString())
  {
    updates["password"] = body["password"];
  }

  if (body.isMember("enabled") && body["enabled"].isBool())
  {
    updates["enabled"] = body["enabled"];
  }

  if (body.isMember("metadata") && body["metadata"].isObject())
  {
    updates["metadata"] = body["metadata"];
  }

  // Check if we're disabling the user
  bool disabling = body.isMember("enabled") && body["enabled"].isBool() &&
                   !body["enabled"].asBool();

  // Perform update
  bool success = m_user_database->updateUser(callsign, updates);

  if (success)
  {
    // If user was disabled, kick them if currently connected
    if (disabling)
    {
      ReflectorClient* client = ReflectorClient::lookup(callsign);
      if (client != nullptr)
      {
        client->kick("User account disabled");
        std::cout << callsign << ": Kicked (account disabled via API)"
                  << std::endl;
      }
    }

    // Retrieve updated user to return complete data
    UserEntry user;
    m_user_database->getUser(callsign, user);

    Json::Value response;
    response["success"] = true;
    response["message"] = disabling && ReflectorClient::lookup(callsign) == nullptr
                          ? "User disabled and kicked" : "User updated successfully";
    response["user"]["callsign"] = user.callsign;
    response["user"]["group"] = user.group;
    response["user"]["enabled"] = user.enabled;
    response["user"]["created_at"] = static_cast<Json::Int64>(user.created_at);
    response["user"]["updated_at"] = static_cast<Json::Int64>(user.updated_at);
    response["user"]["last_seen"] = static_cast<Json::Int64>(user.last_seen);
    response["user"]["login_count"] = user.login_count;
    response["user"]["metadata"] = user.metadata;
    response["user"]["source"] = user.source;

    sendJsonResponse(con, 200, response);
  }
  else
  {
    sendError(con, 500, "Failed to update user: " + callsign);
  }
} /* AdminHandler::handleUserUpdate */


void AdminHandler::handleUserDelete(
    Async::HttpServerConnection* con,
    const Async::HttpServerConnection::Request& req,
    const std::string& callsign)
{
  // Check if user database is available
  if (!m_user_database)
  {
    sendError(con, 503, "User database not configured");
    return;
  }

  // Try to delete user
  bool success = m_user_database->deleteUser(callsign);

  if (success)
  {
    Json::Value response;
    response["success"] = true;
    response["message"] = "User deleted successfully";
    response["callsign"] = callsign;
    sendJsonResponse(con, 200, response);
  }
  else
  {
    sendError(con, 404, "User not found: " + callsign);
  }
} /* AdminHandler::handleUserDelete */


/*
 * This file has not been truncated
 */
