/**
 * Bonsai - open source group collaboration and application lifecycle management
 * Copyright (c) 2011 Bob Carroll
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * @brief   authorisation functions
 *
 * @author  Bob Carroll (bob.carroll@alum.rit.edu)
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

#include <netapi.h>

#include <authz.h>
#include <log.h>

static pthread_mutex_t _ctxmtx = PTHREAD_MUTEX_INITIALIZER;
static struct libnetapi_ctx *_netapictx = NULL;
static char *_host = NULL;

/**
 * Initialises the NetApi context. This function is not re-entrant.
 *
 * @param host      hostname of the Samba server
 * @param username  service account username
 * @param passwd    service account password
 *
 * @return true on success, false otherwise
 */
int authz_init(const char *host, const char *username, const char *passwd)
{
    pthread_mutex_lock(&_ctxmtx);

    if (_netapictx || !host || !username || !passwd) {
        pthread_mutex_unlock(&_ctxmtx);
        return 0;
    }

    if (libnetapi_init(&_netapictx) != NET_API_STATUS_SUCCESS) {
        log_error("failed to initialise NetApi context");
        pthread_mutex_unlock(&_ctxmtx);
        return 0;
    }

    libnetapi_set_username(_netapictx, username);
    libnetapi_set_password(_netapictx, passwd);

    _host = strdup(host);

    pthread_mutex_unlock(&_ctxmtx);
    log_info("initialised NetApi context");

    return 1;
}

/**
 * Frees the NetApi context allocated with gcs_auth_init().
 */
void authz_free()
{
    pthread_mutex_lock(&_ctxmtx);

    if (_netapictx) {
        libnetapi_free(_netapictx);
        _netapictx = NULL;

        free(_host);
        _host = NULL;
    }

    pthread_mutex_unlock(&_ctxmtx);
}

/**
 * Lookup a user based on a user ID. Calling functions should
 * free the result with authz_free_buffer().
 *
 * @param userid    user name to lookup
 *
 * @return a user info structure or NULL on error
 */
userinfo_t *authz_lookup_user(const char *userid)
{
    struct USER_INFO_23 *buf = NULL;
    userinfo_t *result = NULL;
    NET_API_STATUS status;

    pthread_mutex_lock(&_ctxmtx);

    if (!userid || !_netapictx) {
        pthread_mutex_unlock(&_ctxmtx);
        return NULL;
    }

    status = NetUserGetInfo(_host, userid, 23, (uint8_t **)&buf);
    if (status != NET_API_STATUS_SUCCESS) {
        log_warn("NetApi lookup for user %s failed (%d)", userid, status);
        pthread_mutex_unlock(&_ctxmtx);
        return NULL;
    }

    result = (userinfo_t *)malloc(sizeof(userinfo_t));
    bzero(result, sizeof(userinfo_t));

    result->logon_name = strdup(userid);
    result->display_name = strdup(buf->usri23_full_name);

    ConvertSidToStringSid(buf->usri23_user_sid, &result->sid);

    NetApiBufferFree(buf);
    pthread_mutex_unlock(&_ctxmtx);

    log_debug("found user %s with SID %s", userid, result->sid);
    return result;
}

/**
 * Frees the given user info structure.
 *
 * @param buf
 */
void authz_free_buffer(userinfo_t *buf)
{
    if (!buf)
        return;

    if (buf->logon_name)
        free(buf->logon_name);

    if (buf->domain)
        free(buf->domain);

    if (buf->display_name)
        free(buf->display_name);

    if (buf->sid)
        free(buf->sid);

    free(buf);
}

