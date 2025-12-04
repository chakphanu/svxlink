/**
@file    AdminHandler.h
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

#ifndef ADMIN_HANDLER_INCLUDED
#define ADMIN_HANDLER_INCLUDED


/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <string>
#include <memory>
#include <json/json.h>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncConfig.h>
#include <AsyncHttpServerConnection.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "DataStore.h"
#include "MuteList.h"
#include "UserDatabase.h"


/****************************************************************************
 *
 * Forward declarations
 *
 ****************************************************************************/

class Reflector;


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
@brief  HTTP Admin API handler for SvxReflector
@author SvxLink Contributors
@date   2025

This class handles HTTP Admin API requests for the reflector, providing
endpoints for administrative tasks such as muting/unmuting nodes, kicking
clients, and retrieving status information.

The API uses Bearer token authentication and JSON request/response format.

Supported endpoints:
- POST /admin/mute                - Mute a node
- POST /admin/unmute              - Unmute a node
- GET  /admin/mute                - List muted nodes
- POST /admin/kick                - Kick a node
- GET  /admin/status              - Get admin status
- POST /admin/config              - Set configuration value
- GET  /admin/config              - Get configuration value
- GET  /admin/users               - List all users
- GET  /admin/users/{callsign}    - Get user details
- POST /admin/users               - Create user
- PUT  /admin/users/{callsign}    - Update user
- DELETE /admin/users/{callsign}  - Delete user

Configuration variables:
- GLOBAL/ADMIN_API_TOKEN  - Bearer token for authentication
- GLOBAL/ADMIN_MUTE_FILE  - Path to mute list persistence file (optional)

Thread-safe: Yes (uses MuteList's internal locking)
*/
class AdminHandler
{
  public:
    /**
     * @brief   Constructor
     * @param   ref The reflector instance
     * @param   cfg The configuration object
     */
    AdminHandler(Reflector& ref, Async::Config& cfg);

    /**
     * @brief   Destructor
     */
    ~AdminHandler(void);

    /**
     * @brief   Handle an HTTP request
     * @param   con The HTTP connection
     * @param   req The HTTP request
     * @return  true if the request was handled, false otherwise
     *
     * This is the main entry point for HTTP requests. It checks if the
     * request target starts with "/admin/" and handles it accordingly.
     * Returns false for non-admin requests.
     */
    bool handleRequest(Async::HttpServerConnection* con,
                       Async::HttpServerConnection::Request& req);

    /**
     * @brief   Get the mute list
     * @return  Pointer to the mute list (may be null if not configured)
     */
    MuteList* muteList(void) { return m_mute_list.get(); }

    /**
     * @brief   Get the user database
     * @return  Pointer to the user database (may be null if not configured)
     */
    UserDatabase* userDatabase(void) { return m_user_database.get(); }

  private:
    Reflector&                                  m_reflector;
    Async::Config&                              m_cfg;
    std::string                                 m_auth_token;
    std::unique_ptr<DataStore::IDataStore>      m_data_store;
    std::unique_ptr<MuteList>                   m_mute_list;
    std::unique_ptr<UserDatabase>               m_user_database;

    // Disable copy
    AdminHandler(const AdminHandler&);
    AdminHandler& operator=(const AdminHandler&);

    /**
     * @brief   Validate the bearer token in the request
     * @param   req The HTTP request
     * @return  true if the token is valid
     */
    bool validateToken(const Async::HttpServerConnection::Request& req) const;

    /**
     * @brief   Send a JSON response
     * @param   con  The HTTP connection
     * @param   code The HTTP status code
     * @param   json The JSON response body
     */
    void sendJsonResponse(Async::HttpServerConnection* con, int code,
                          const Json::Value& json);

    /**
     * @brief   Send an error response
     * @param   con  The HTTP connection
     * @param   code The HTTP status code
     * @param   msg  The error message
     */
    void sendError(Async::HttpServerConnection* con, int code,
                   const std::string& msg);

    /**
     * @brief   Parse JSON from request body
     * @param   req  The HTTP request
     * @param   json Output parameter for parsed JSON
     * @return  true on success
     */
    bool parseJsonBody(const Async::HttpServerConnection::Request& req,
                       Json::Value& json);

    // Route handlers
    void handleMute(Async::HttpServerConnection* con,
                    const Async::HttpServerConnection::Request& req);
    void handleUnmute(Async::HttpServerConnection* con,
                      const Async::HttpServerConnection::Request& req);
    void handleMuteList(Async::HttpServerConnection* con,
                        const Async::HttpServerConnection::Request& req);
    void handleKick(Async::HttpServerConnection* con,
                    const Async::HttpServerConnection::Request& req);
    void handleStatus(Async::HttpServerConnection* con,
                      const Async::HttpServerConnection::Request& req);
    void handleConfigGet(Async::HttpServerConnection* con,
                         const Async::HttpServerConnection::Request& req);
    void handleConfigSet(Async::HttpServerConnection* con,
                         const Async::HttpServerConnection::Request& req);
    void handleUsersList(Async::HttpServerConnection* con,
                         const Async::HttpServerConnection::Request& req);
    void handleUserGet(Async::HttpServerConnection* con,
                       const Async::HttpServerConnection::Request& req,
                       const std::string& callsign);
    void handleUserCreate(Async::HttpServerConnection* con,
                          const Async::HttpServerConnection::Request& req);
    void handleUserUpdate(Async::HttpServerConnection* con,
                          const Async::HttpServerConnection::Request& req,
                          const std::string& callsign);
    void handleUserDelete(Async::HttpServerConnection* con,
                          const Async::HttpServerConnection::Request& req,
                          const std::string& callsign);

};  /* class AdminHandler */


#endif /* ADMIN_HANDLER_INCLUDED */


/*
 * This file has not been truncated
 */
