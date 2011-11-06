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
 * @brief   NTLM web service authentication
 *
 * @author  Bob Carroll (bob.carroll@alum.rit.edu)
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gcs/ntlmauth.h>
#include <gcs/log.h>

enum pipes { READ, WRITE };

/**
 * Initialises an NTLM authentication context. Callering functions 
 * should call gcs_ntlmauth_free() to free the result.
 *
 * @param helper    path to the NTLM helper tool
 *
 * @return a context or NULL on error
 */
gcs_ntlmctx *gcs_ntlmauth_init(const char *helper)
{
    int infd[2];
    int outfd[2];
    int errfd[2];

    if (!helper) {
        gcslog_error("missing helper application path!");
        return NULL;
    }

    if (access(helper, X_OK)) {
        gcslog_error("permission check failed for helper %s", helper);
        return NULL;
    }

    gcs_ntlmctx *result = (gcs_ntlmctx *)malloc(sizeof(gcs_ntlmctx));
    bzero(result, sizeof(gcs_ntlmctx));

    result->helper = strdup(helper);

    pipe(infd);
    pipe(outfd);
    pipe(errfd);

    gcslog_debug("spawning child process %s", helper);

    if ((result->pid = fork()) == -1) {
        gcslog_error("fork() failed with error %d", result->pid);

        close(infd[READ]);
        close(infd[WRITE]);
        close(outfd[READ]);
        close(outfd[WRITE]);
        close(errfd[READ]);
        close(errfd[WRITE]);

        gcs_ntlmauth_free(result);
        return NULL;
    }

    if (!result->pid) {
        dup2(infd[READ], STDIN_FILENO);
        dup2(outfd[WRITE], STDOUT_FILENO);
        dup2(errfd[WRITE], STDERR_FILENO);

        close(infd[READ]);
        close(infd[WRITE]);
        close(outfd[READ]);
        close(outfd[WRITE]);
        close(errfd[READ]);
        close(errfd[WRITE]);

        char *argv[] = { result->helper, "--helper-protocol=squid-2.5-ntlmssp", NULL };
        execv(argv[0], argv);
        exit(0);
    }

    close(infd[READ]);
    close(outfd[WRITE]);
    close(errfd[WRITE]);

    result->infd = infd[WRITE];
    result->outfd = outfd[READ];
    result->errfd = errfd[READ];

    return result;
}

/**
 * Frees the given NTLM context.
 *
 * @param ctx
 */
void gcs_ntlmauth_free(gcs_ntlmctx *ctx)
{
    if (!ctx)
        return;

    if (ctx->helper)
        free(ctx->helper);

    close(ctx->infd);
    close(ctx->outfd);
    close(ctx->errfd);

    free(ctx);
}

/**
 * Main challenge/response routine for NTLM authentication. Calling functions
 * are responsible for freeing result.
 *
 * Note that this function return false until negotiation is completed,
 * and does not indicate authentication failure. Calling functions should
 * assume authentication has failed and continue negotiating until the
 * function returns true.
 *
 * @param ctx           authentication context to use
 * @param challenge     challenge data from the client
 * @param response      output buffer for the server response
 *
 * @return true on successful authentication, false otherwise
 */
int gcs_ntlmauth_challenge(gcs_ntlmctx *ctx, const char *challenge, char **response)
{
    if (!ctx || !response || *response)
        return 0;

    if (!challenge) {
        gcslog_debug("empty challenge received, starting NTLM negotiation");
        ctx->state = NTLM_RESET;
    } else if (strlen(challenge) < 6 || strncmp(challenge, "NTLM ", 5)) {
        gcslog_error("NTLM challenge is malformed!");
        ctx->state = NTLM_RESET;
    }

    if (ctx->state != NTLM_RESET) {
        char data[1024];
        char msg[1024];
        char code[3];
        int datalen = 0;

        bzero(data, 1024);
        bzero(msg, 1024);
        bzero(code, 3);

        strncpy(data, challenge + 5, 1024);
        gcslog_trace("NTLM challenge data: %s", data);

        if (ctx->state == NTLM_NEGOTIATE) {
            /* TODO check to make sure this is in fact a type 1 NTLM message */
            write(ctx->infd, "YR\n", 3);
            datalen = read(ctx->outfd, data, 1023);
        } else if (ctx->state == NTLM_RESPONSE) {
            char kkdata[1024];
            snprintf(kkdata, 1024, "KK %s\n", data);

            write(ctx->infd, kkdata, strlen(kkdata));
            datalen = read(ctx->outfd, data, 1023);
        } else {
            data[0] = '\0';
            datalen = 0;
        }

        gcslog_trace("raw data received from helper: %s", data);

        if (datalen < 5 || data[2] != ' ' || data[datalen - 1] != '\n') {
            gcslog_error("response from helper is malformed!");
            ctx->state = NTLM_RESET;
            return 0;
        }

        strncpy(code, data, 2);
        strncpy(msg, data + 3, datalen - 4);

        if (ctx->state == NTLM_NEGOTIATE && !strcmp(code, "TT")) {
            char respdata[1024];
            snprintf(respdata, 1024, "NTLM %s", msg);

            gcslog_debug("sending challenge to client");
            ctx->state = NTLM_RESPONSE;
            *response = strdup(respdata);
        } else if (ctx->state == NTLM_RESPONSE && !strcmp(code, "AF")) {
            gcslog_info("authentication succeeded for %s", msg);
            ctx->state = NTLM_SUCCESS;
            *response = strdup(msg);
        } else if (ctx->state == NTLM_RESPONSE && !strcmp(code, "NA")) {
            gcslog_info("authentication failed: %s", msg);
            ctx->state = NTLM_RESET;
        } else if (!strcmp(code, "BH")) {
            /* TODO the helper probably should be restarted */
            gcslog_error("received error from helper: %s", msg);
            ctx->state = NTLM_RESET;
        } else {
            gcslog_error("authentication context reached an unexpected state");
            gcslog_debug("context_state=%d helper_code=%s", ctx->state, code);
            ctx->state = NTLM_RESET;
        }
    }

    if (ctx->state == NTLM_RESET) {
        ctx->state = NTLM_NEGOTIATE;
        *response = strdup("NTLM");
    }

    return (ctx->state == NTLM_SUCCESS);
}

