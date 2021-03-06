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
 * @brief   team project collection bootstrapping
 *
 * @author  Bob Carroll (bob.carroll@alum.rit.edu)
 */

#include <ctype.h>

#include <pgcommon.h>
#include <log.h>

#include <tf/catalog.h>
#include <tf/location.h>
#include <tf/servicehost.h>
#include <tf/webservices.h>
#include <tf/xml.h>

#include <pcd.h>
#include <csd.h>

#define MAX_ROUTERS     1024

static SoapRouter **_routers = NULL;

/**
 * Start the given SOAP service.
 *
 * @param service   service to start
 * @param router    SOAP router
 * @param prefix    service path prefix
 * @param instid    host instance ID
 */
static void _start_service(tf_service *service, SoapRouter **router, const char *prefix,
    const char *instid)
{
    if (strcmp(service->type, TF_SERVICE_LOCATION_TYPE) == 0)
        location_service_init(router, prefix, service->relpath, instid);
    else if (strcmp(service->type, TF_SERVICE_REGISTRATION_TYPE) == 0)
        registration_service_init(router, prefix, service->relpath, instid);
    else if (strcmp(service->type, TF_SERVICE_STATUS_TYPE) == 0)
        status_service_init(router, prefix, service->relpath, instid);
    else if (strcmp(service->type, TF_SERVICE_AUTHORIZATION_TYPE) == 0)
        authz_service_init(router, prefix, service->relpath, instid, 1);
    else if (strcmp(service->type, TF_SERVICE_AUTHORIZATION3_TYPE) == 0)
        authz_service_init(router, prefix, service->relpath, instid, 3);
    else if (strcmp(service->type, TF_SERVICE_COMMON_STRUCT_TYPE) == 0)
        common_str_service_init(router, prefix, service->relpath, instid, 1);
    else if (strcmp(service->type, TF_SERVICE_COMMON_STRUCT3_TYPE) == 0)
        common_str_service_init(router, prefix, service->relpath, instid, 3);
    else if (strcmp(service->type, TF_SERVICE_PROCESS_TEMPL_TYPE) == 0)
        proc_tmpl_service_init(router, prefix, service->relpath, instid);
    else
        log_warn("cannot start unknown service type %s", service->type);
}

/**
 * Team Project Collection services initialisation.
 *
 * @param prefix    the URI prefix for services.
 * @param name      name of TPC to initialise
 * @param pguser    database connection user ID
 * @param pgpasswd  database connection password
 * @param dbconns   database connection count
 *
 * @return true on success, false otherwise
 */
int tpc_services_init(const char *prefix, const char *tpcname, const char *pguser, 
    const char *pgpasswd, int dbconns)
{
    pgctx *ctx;
    tf_host *host = NULL;
    tf_service **svcarr = NULL;
    tf_error dberr;
    char pcprefix[TF_LOCATION_SERVICE_REL_PATH_MAXLEN + 1024];
    char lpcname[TF_SERVICE_HOST_NAME_MAXLEN];
    int i, n;

    if (!prefix || !tpcname || !pguser || !pgpasswd || dbconns < 1)
        return;

    if (_routers) {
        log_warn("project collection services are already initialised!");
        return 1;
    }

    ctx = pg_context_acquire(NULL);
    if (!ctx) {
        log_critical("failed to obtain PG context!");
        return 0;
    }

    dberr = tf_fetch_single_host(ctx, tpcname, 1, &host);
    pg_context_release(ctx);

    if (dberr != TF_ERROR_SUCCESS) {
        log_warn("no project collections were found with name", tpcname);
        host = tf_free_host(host);
        return 0;
    }

    _routers = (SoapRouter **)calloc(MAX_ROUTERS, sizeof(SoapRouter *));

    if (!pg_connect(host->connstr, pguser, pgpasswd, dbconns, host->id)) {
        log_error("failed to connect to PG");
        host = tf_free_host(host);
        return 0;
    }

    log_info("initialising project collection services for %s", host->name);

    bzero(lpcname, TF_SERVICE_HOST_NAME_MAXLEN);
    strncpy(lpcname, host->name, TF_SERVICE_HOST_NAME_MAXLEN - 1);
    for (n = 0; lpcname[n]; n++)
        lpcname[n] = tolower(lpcname[n]);

    snprintf(pcprefix, TF_LOCATION_SERVICE_REL_PATH_MAXLEN + 1024, "%s/%s", prefix, lpcname);

    ctx = pg_context_acquire(host->id);
    if (!ctx) {
        log_critical("failed to obtain PG context!");
        host = tf_free_host(host);
        return 0;
    }

    dberr = tf_fetch_services(ctx, NULL, &svcarr);
    pg_context_release(ctx);

    if (dberr != TF_ERROR_SUCCESS || !svcarr[0]) {
        log_warn("failed to retrieve project collection services for %s", host->id);
        svcarr = tf_free_service_array(svcarr);
        host = tf_free_host(host);
        return 0;
    }

    for (n = 0; svcarr[n]; n++) {
        if (n == MAX_ROUTERS) {
            log_error(
                "unable to start service because the maximum count was reached (%d)", 
                MAX_ROUTERS);
            break;
        }

        if (svcarr[n]->reltosetting == TF_SERVICE_RELTO_CONTEXT)
            _start_service(svcarr[n], &_routers[n], pcprefix, host->id);
    }

    svcarr = tf_free_service_array(svcarr);
    host = tf_free_host(host);

    return 1;
}

