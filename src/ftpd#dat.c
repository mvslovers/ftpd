/*
** FTPD Data Connection Management
**
** Handles PORT (active) and PASV (passive) data connections.
*/
#include "ftpd.h"
#include "ftpd#ses.h"
#include "ftpd#dat.h"

/* --------------------------------------------------------------------
** Parse PORT command arguments: h1,h2,h3,h4,p1,p2
** Stores address and port in session.
** Returns 0 on success, -1 on parse error.
** ----------------------------------------------------------------- */
int
ftpd_data_port(ftpd_session_t *sess, const char *arg)
{
    unsigned h1, h2, h3, h4, p1, p2;

    if (sscanf(arg, "%u,%u,%u,%u,%u,%u", &h1, &h2, &h3, &h4, &p1, &p2) != 6)
        return -1;

    if (h1 > 255 || h2 > 255 || h3 > 255 || h4 > 255 ||
        p1 > 255 || p2 > 255)
        return -1;

    /* Close any existing data connection */
    ftpd_data_close(sess);

    sess->data_addr = (h1 << 24) | (h2 << 16) | (h3 << 8) | h4;
    sess->data_port = (p1 << 8) | p2;
    sess->data_mode = DATA_PORT;

    ftpd_trace("PORT: %u.%u.%u.%u:%u", h1, h2, h3, h4, sess->data_port);

    return 0;
}

/* --------------------------------------------------------------------
** Open passive listener.
** Picks a port from the configured range, binds, listens.
** Sends 227 response with address:port.
** Returns 0 on success, -1 on error.
** ----------------------------------------------------------------- */
int
ftpd_data_pasv(ftpd_session_t *sess)
{
    struct sockaddr_in addr;
    int sock;
    int port;
    int len;
    unsigned a1, a2, a3, a4;
    int p1, p2;
    const char *pasv_addr;

    /* Close any existing data connection */
    ftpd_data_close(sess);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ftpd_log(LOG_ERROR, "%s: socket() failed, errno=%d", __func__, errno);
        return -1;
    }

    /* Try ports in the configured range */
    for (port = sess->server->config.pasv_lo;
         port <= sess->server->config.pasv_hi;
         port++) {

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = 0;  /* bind to any address */
        addr.sin_port = htons(port);

        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0)
            break;
    }

    if (port > sess->server->config.pasv_hi) {
        ftpd_log(LOG_ERROR, "%s: no port available in range %d-%d",
                 __func__, sess->server->config.pasv_lo,
                 sess->server->config.pasv_hi);
        closesocket(sock);
        return -1;
    }

    if (listen(sock, 1) < 0) {
        ftpd_log(LOG_ERROR, "%s: listen() failed, errno=%d", __func__, errno);
        closesocket(sock);
        return -1;
    }

    sess->pasv_sock = sock;
    sess->data_mode = DATA_PASV;

    /* Parse PASV address for response */
    pasv_addr = sess->server->config.pasv_addr;
    if (sscanf(pasv_addr, "%u.%u.%u.%u", &a1, &a2, &a3, &a4) != 4) {
        /* Fallback: get address from control socket */
        len = sizeof(addr);
        getsockname(sess->ctrl_sock, (struct sockaddr *)&addr, &len);
        a1 = (ntohl(addr.sin_addr.s_addr) >> 24) & 0xFF;
        a2 = (ntohl(addr.sin_addr.s_addr) >> 16) & 0xFF;
        a3 = (ntohl(addr.sin_addr.s_addr) >> 8) & 0xFF;
        a4 = ntohl(addr.sin_addr.s_addr) & 0xFF;
    }

    p1 = (port >> 8) & 0xFF;
    p2 = port & 0xFF;

    ftpd_session_reply(sess, FTP_227,
                       "Entering Passive Mode (%u,%u,%u,%u,%d,%d)",
                       a1, a2, a3, a4, p1, p2);

    ftpd_trace("PASV: listening on port %d", port);

    return 0;
}

/* --------------------------------------------------------------------
** Establish data connection.
** For PORT: connect to client.
** For PASV: accept from passive listener.
** Returns 0 on success, -1 on error.
** ----------------------------------------------------------------- */
int
ftpd_data_open(ftpd_session_t *sess)
{
    struct sockaddr_in addr;
    int sock;
    int len;
    fd_set rfds;
    struct timeval tv;
    int rc;

    if (sess->data_mode == DATA_PASV) {
        /* Accept connection on passive socket */
        FD_ZERO(&rfds);
        FD_SET(sess->pasv_sock, &rfds);
        tv.tv_sec = 30;
        tv.tv_usec = 0;

        rc = select(sess->pasv_sock + 1, &rfds, NULL, NULL, &tv);
        if (rc <= 0) {
            ftpd_log(LOG_WARN, "%s: accept timeout", __func__);
            closesocket(sess->pasv_sock);
            sess->pasv_sock = -1;
            sess->data_mode = DATA_NONE;
            return -1;
        }

        len = sizeof(addr);
        sock = accept(sess->pasv_sock, (struct sockaddr *)&addr, &len);
        closesocket(sess->pasv_sock);
        sess->pasv_sock = -1;

        if (sock < 0) {
            ftpd_log(LOG_ERROR, "%s: accept() failed, errno=%d", __func__,
                     errno);
            sess->data_mode = DATA_NONE;
            return -1;
        }

        sess->data_sock = sock;
        ftpd_trace("PASV: data connection accepted");
    }
    else if (sess->data_mode == DATA_PORT) {
        /* Connect to client */
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            ftpd_log(LOG_ERROR, "%s: socket() failed, errno=%d", __func__,
                     errno);
            sess->data_mode = DATA_NONE;
            return -1;
        }

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(sess->data_addr);
        addr.sin_port = htons(sess->data_port);

        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            ftpd_log(LOG_ERROR, "%s: connect() failed, errno=%d", __func__,
                     errno);
            closesocket(sock);
            sess->data_mode = DATA_NONE;
            return -1;
        }

        sess->data_sock = sock;
        ftpd_trace("PORT: data connection established");
    }
    else {
        return -1;
    }

    return 0;
}

/* --------------------------------------------------------------------
** Close data connection and passive listener.
** ----------------------------------------------------------------- */
void
ftpd_data_close(ftpd_session_t *sess)
{
    if (sess->data_sock >= 0) {
        closesocket(sess->data_sock);
        sess->data_sock = -1;
    }
    if (sess->pasv_sock >= 0) {
        closesocket(sess->pasv_sock);
        sess->pasv_sock = -1;
    }
    sess->data_mode = DATA_NONE;
}

/* --------------------------------------------------------------------
** Send data on data connection.
** Returns number of bytes sent, or -1 on error.
** ----------------------------------------------------------------- */
int
ftpd_data_send(ftpd_session_t *sess, const void *buf, int len)
{
    int sent = 0;
    int rc;

    while (sent < len) {
        rc = send(sess->data_sock, (const char *)buf + sent, len - sent, 0);
        if (rc <= 0) {
            ftpd_log(LOG_ERROR, "%s: send failed, errno=%d", __func__, errno);
            return -1;
        }
        sent += rc;
    }

    sess->bytes_sent += sent;
    return sent;
}

/* --------------------------------------------------------------------
** Receive data from data connection.
** Returns number of bytes received, 0 on EOF, or -1 on error.
** ----------------------------------------------------------------- */
int
ftpd_data_recv(ftpd_session_t *sess, void *buf, int len)
{
    int rc;

    rc = recv(sess->data_sock, buf, len, 0);
    if (rc < 0) {
        ftpd_log(LOG_ERROR, "%s: recv failed, errno=%d", __func__, errno);
        return -1;
    }

    sess->bytes_recv += rc;
    return rc;
}

/* --------------------------------------------------------------------
** Send a formatted string on data connection with EBCDIC->ASCII
** translation (for LIST output etc.).
** Returns number of bytes sent, or -1 on error.
** ----------------------------------------------------------------- */
int
ftpd_data_printf(ftpd_session_t *sess, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    int len;
    int i;

    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (len <= 0)
        return 0;

    /* Translate EBCDIC -> ASCII if TYPE A */
    if (sess->type == XFER_TYPE_A) {
        for (i = 0; i < len; i++)
            buf[i] = ebc2asc[(unsigned char)buf[i]];
    }

    return ftpd_data_send(sess, buf, len);
}
