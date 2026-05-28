/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief SCPI over a raw TCP socket
 *
 * A line-oriented SCPI client: connect, write a command with a newline,
 * read a newline-terminated response.
 */

#define TE_LGR_USER "TAPI POWER SCPI"

#include "te_config.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/time.h>

#include "logger_api.h"
#include "te_alloc.h"
#include "te_str.h"
#include "te_string.h"

#include "tapi_power_scpi.h"

/* See description in tapi_power_scpi.h */
te_errno
tapi_power_scpi_open(const char *host, uint16_t port, int timeout_ms,
                     tapi_power_scpi *scpi)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *ai;
    char portstr[16];
    int fd = -1;
    int gai;

    scpi->fd = -1;
    scpi->timeout_ms = (timeout_ms > 0) ? timeout_ms : TAPI_POWER_SCPI_TIMEOUT_MS;

    snprintf(portstr, sizeof(portstr), "%u",
             port != 0 ? port : TAPI_POWER_SCPI_PORT);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    gai = getaddrinfo(host, portstr, &hints, &res);
    if (gai != 0)
    {
        ERROR("Cannot resolve instrument %s: %s", host, gai_strerror(gai));
        return TE_RC(TE_TAPI, TE_ENOENT);
    }

    for (ai = res; ai != NULL; ai = ai->ai_next)
    {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0)
    {
        ERROR("Cannot connect to instrument %s:%s: %s", host, portstr,
              strerror(errno));
        return TE_OS_RC(TE_TAPI, errno);
    }

    scpi->fd = fd;

    return 0;
}

/* See description in tapi_power_scpi.h */
te_errno
tapi_power_scpi_send(tapi_power_scpi *scpi, const char *command)
{
    te_string line = TE_STRING_INIT;
    size_t off = 0;
    te_errno rc = 0;

    if (scpi->fd < 0)
        return TE_RC(TE_TAPI, TE_EBADF);

    te_string_append(&line, "%s\n", command);
    while (off < line.len)
    {
        ssize_t n = send(scpi->fd, line.ptr + off, line.len - off, 0);

        if (n <= 0)
        {
            rc = TE_OS_RC(TE_TAPI, errno);
            ERROR("Cannot send '%s' to the instrument: %s", command,
                  strerror(errno));
            break;
        }
        off += n;
    }
    te_string_free(&line);

    return rc;
}

/* See description in tapi_power_scpi.h */
te_errno
tapi_power_scpi_query(tapi_power_scpi *scpi, const char *query,
                      te_string *response)
{
    struct timeval start;
    te_errno rc;

    rc = tapi_power_scpi_send(scpi, query);
    if (rc != 0)
        return rc;

    gettimeofday(&start, NULL);
    for (;;)
    {
        struct pollfd pfd = { .fd = scpi->fd, .events = POLLIN };
        struct timeval now;
        long left;
        char buf[512];
        ssize_t n;
        char *nl;

        gettimeofday(&now, NULL);
        left = scpi->timeout_ms - ((now.tv_sec - start.tv_sec) * 1000 +
                                   (now.tv_usec - start.tv_usec) / 1000);
        if (left <= 0)
        {
            ERROR("Timed out waiting for a response to '%s'", query);
            return TE_RC(TE_TAPI, TE_ETIMEDOUT);
        }

        if (poll(&pfd, 1, (int)left) <= 0)
        {
            ERROR("Timed out waiting for a response to '%s'", query);
            return TE_RC(TE_TAPI, TE_ETIMEDOUT);
        }

        n = recv(scpi->fd, buf, sizeof(buf), 0);
        if (n < 0)
            return TE_OS_RC(TE_TAPI, errno);
        if (n == 0)
        {
            ERROR("The instrument closed the connection during '%s'", query);
            return TE_RC(TE_TAPI, TE_ECONNRESET);
        }

        te_string_append_buf(response, buf, n);
        nl = (response->ptr != NULL) ? strchr(response->ptr, '\n') : NULL;
        if (nl != NULL)
        {
            /* Trim at the first newline and any trailing CR/space. */
            size_t len = nl - response->ptr;

            while (len > 0 && (response->ptr[len - 1] == '\r' ||
                               response->ptr[len - 1] == ' '))
                len--;
            te_string_cut(response, response->len - len);
            break;
        }
    }

    return 0;
}

/* See description in tapi_power_scpi.h */
te_errno
tapi_power_scpi_query_double(tapi_power_scpi *scpi, const char *query,
                            double *value)
{
    te_string resp = TE_STRING_INIT;
    char *end = NULL;
    te_errno rc;

    rc = tapi_power_scpi_query(scpi, query, &resp);
    if (rc != 0)
        goto out;

    if (resp.ptr == NULL || resp.len == 0)
    {
        rc = TE_RC(TE_TAPI, TE_EINVAL);
        goto out;
    }

    *value = strtod(resp.ptr, &end);
    if (end == resp.ptr)
    {
        ERROR("Response '%s' to '%s' is not a number", resp.ptr, query);
        rc = TE_RC(TE_TAPI, TE_EINVAL);
    }

out:
    te_string_free(&resp);

    return rc;
}

/* See description in tapi_power_scpi.h */
void
tapi_power_scpi_close(tapi_power_scpi *scpi)
{
    if (scpi->fd >= 0)
        close(scpi->fd);
    scpi->fd = -1;
}
