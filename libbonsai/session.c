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
 * @brief   session management
 *
 * @author  Bob Carroll (bob.carroll@alum.rit.edu)
 */

#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include <session.h>
#include <log.h>

#define MAX_SESSIONS    10240

static session_t **_sessions = NULL;
static pthread_mutex_t _sessionmtx = PTHREAD_MUTEX_INITIALIZER;
static int _tail = -1;

/**
 * Creates or resumes a client session.
 *
 * @param id    a unique session ID
 *
 * @return a session structure, or NULL on error
 */
session_t *session_init(const char *id)
{
    session_t *result = NULL;
    int i;

    if (!id)
        return NULL;

    pthread_mutex_lock(&_sessionmtx);

    if (!_sessions) {
        _sessions = (session_t **)calloc(MAX_SESSIONS, sizeof(session_t *));
        log_debug("initialised session storage with size %d", MAX_SESSIONS);
    }

    for (i = 0; i <= _tail && i < MAX_SESSIONS; i++) {
        if (strcmp(_sessions[i]->id, id) == 0) {
            result = _sessions[i];
            break;
        }
    }

    if (!result && i == MAX_SESSIONS)
        log_error("no session slots available!");
    else if (!result) {
        log_info("allocating session %d with ID %s", i, id);
        result = _sessions[i] = (session_t *)malloc(sizeof(session_t));
        bzero(result, sizeof(session_t));
        _tail = i;

        result->id = strdup(id);
        result->refcount++;
        result->lastseen = time(NULL);
    } else {
        log_debug("re-using session %d with ID %s", i, id);
        result->refcount++;
        result->lastseen = time(NULL);
    }

    pthread_mutex_unlock(&_sessionmtx);

    return result;
}

/**
 * Releases a handle to the given session.
 *
 * @param session
 */
void session_close(session_t *session)
{
    if (!session)
        return;

    pthread_mutex_lock(&_sessionmtx);
    session->refcount--;
    log_debug("released session handle for %s (%d remaining)", session->id, session->refcount);
    pthread_mutex_unlock(&_sessionmtx);
}

/**
 * Sets the user ID for the given session.
 *
 * @param session   a session structure
 * @param userid    the new user ID (cannot be NULL)
 */
void session_bind_user(session_t *session, const char *userid)
{
    if (!session || session->userid || !userid)
        return;

    pthread_mutex_lock(&_sessionmtx);
    log_info("binding session %s to user %s", session->id, userid);
    session->userid = strdup(userid);
    pthread_mutex_unlock(&_sessionmtx);
}

/**
 * Initialises the authentication context for the given session. If
 * authctx is NULL, then the function determines if the context is
 * already initialised. If authctx is not NULL but empty, then it will
 * receive the previous authentication context.
 *
 * Once a session's authentication context is initialised, it cannot
 * be changed. Subsequent calls with a non-empty authctx buffer will
 * always return an error.
 *
 * @param session   a session structure
 * @param scope     authentication scope
 * @param authctx   authenication context input/output buffer
 *
 * @return 1 if initialised, 0 if not initialised, or -1 on error
 */
int session_auth_init(session_t *session, const char *scope, ntlmctx_t **authctx)
{
    ntlmctx_t *buf = NULL;
    int result;

    if (!session)
        return -1;

    pthread_mutex_lock(&_sessionmtx);
    buf = session->authctx;

    while (buf) {
        if (!strcasecmp(buf->scope, scope))
            break;

        buf = buf->next;
    }

    if (!authctx) {
        result = (buf != NULL);
        pthread_mutex_unlock(&_sessionmtx);
        return result;
    } else if (authctx && *authctx && buf) {
        log_error("got an auth context but one already exists");
        pthread_mutex_unlock(&_sessionmtx);
        return -1;
    } else if (authctx && !*authctx && buf) {
        log_debug("returning previous authentication context");
        *authctx = buf;
        pthread_mutex_unlock(&_sessionmtx);
        return 1;
    } else if (authctx && !*authctx) {
        log_debug("no previous authentication context exists");
        pthread_mutex_unlock(&_sessionmtx);
        return 0;
    }

    log_debug("setting new authentication context");
    log_debug("session=%s, scope=%s", session->id, scope);

    if (session->authctx) {
        buf = session->authctx;

        while (buf) {
            if (!buf->next) {
                buf->next = *authctx;
                break;
            }

            buf = buf->next;
        }
    } else
        session->authctx = *authctx;

    pthread_mutex_unlock(&_sessionmtx);
    return 1;
}

/**
 * Determines if the session is authenticated.
 *
 * @param session   a session structure
 *
 * @return true if authenticated, false otherwise
 */
int session_auth_check(session_t *session)
{
    int result;

    pthread_mutex_lock(&_sessionmtx);
    result = (session->userid != NULL);
    pthread_mutex_unlock(&_sessionmtx);

    return result;
}

