#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

// https
#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/err.h>

#include "../main.h"

#define HTTP_FLAG 0
#define HTTPS_FLAG 1

/* from debug.c */
extern int Debug(const int message_debug_level, const int user_debug_level, const char *fmt, ...);
extern int DebugInfo(const char *fmt, ...);
extern int DebugWarning(const char *fmtsring, ...);
extern int DebugError(const char *fmt, ...);

/* for the TCPSend and TCPRecv use */
SSL *GLOBAL_SSL;

static int TCPConnectionCreate(const char *host, int port)
{

    //struct hostent *he;
    struct sockaddr_in server_addr;
    int sock;
    int enable = 1;
    struct timeval recv_timeout;
    recv_timeout.tv_sec = RECV_TIME_OUT;
    recv_timeout.tv_usec = 0;
    //char *host_test = "192.168.1.1";

    //server_addr.sin_addr.s_addr = inet_addr(host_test);
    server_addr.sin_addr.s_addr = inet_addr(host);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    //server_addr.sin_addr = *((struct in_addr *)he->h_addr);
    //server_addr.sin_addr = *((struct in_addr *)he->h_addr_list);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        DebugError("Create socket failed: %s(%d)", strerror(errno), errno);
        if (errno == 1)
        {
            DebugWarning("This program should run as root user");
        }
        else if (errno == 24)
        {
            DebugWarning("You shoud check max file number use 'ulimit -n' in linux");
            DebugWarning("And change the max file number use 'ulimit -n <setting number>'");
        }
        return 0;
    }

    /* setsockopt sucess return 0 */
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)))
    {
        DebugError("setsockopt SO_REUSEADDR failed: %s(%d)", strerror(errno), errno);
        return 0;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(struct timeval)))
    {
        DebugError("setsockopt SO_RCVTIMEO failed: %s(%d)", strerror(errno), errno);
        return 0;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        close(sock);
        DebugError("Connect host failed: %s(%d)", strerror(errno), errno);
        return 0;
    }

    return sock;
}

static ssize_t TCPSend(const int socket, const char *buff, int flag)
{
    /*
     * in this function
     * 'socket' is the socket object to host
     * 'buff' is we want to sending to host string 
     * 'buff_size' is sizeof(buff)
     * 'flag' if use the http set 0
     *        else set 1 (like https)
     * 
     * return sended data size
     */

    ssize_t sent_total_size = 0;
    ssize_t sent_size = 0;
    size_t buff_size = sizeof(buff);

    // make sure the program had sending all data
    if (flag == HTTP_FLAG)
    {
        // http send
        while (sent_total_size < buff_size)
        {
            sent_size = send(socket, buff + sent_total_size, buff_size - sent_total_size, 0);
            if (sent_size < 0)
            {
                DebugError("TCP send data failed");
                return (size_t)NULL;
            }
            sent_total_size += sent_size;
        }
    }
    else if (flag == HTTPS_FLAG)
    {
        while (sent_total_size < buff_size)
        {
            sent_size = SSL_write(GLOBAL_SSL, buff, buff_size);
            if (sent_size < 0)
            {
                DebugError("TCP via ssl send data failed");
                return (size_t)NULL;
            }
            sent_total_size += sent_size;
        }
    }
    else
    {
        DebugError("TCPSend flag set wrong");
        return (size_t)NULL;
    }

    // function will return the sizeof send data bytes
    return sent_total_size;
}

static ssize_t TCPRecv(int socket, char **rebuff, int flag)
{
    /*
     * This function will return the receive data length
     * if 'flag' is 0, use the http
     *           else set 1, use the https
     */

    ssize_t recv_total_size = 0;
    ssize_t recv_size = 0;
    char *buff = (char *)malloc(RECEIVE_DATA_SIZE);
    if (flag == HTTP_FLAG)
    {
        for (;;)
        {
            recv_size = recv(socket, buff, RECEIVE_DATA_SIZE, 0);
            if (recv_size < 0)
            {
                DebugError("TCP recv data failed");
                return (ssize_t)NULL;
            }
            else if (recv_size == 0)
            {
                // all data recv
                break;
            }

            buff = (char *)realloc(buff, sizeof(buff) + RECEIVE_DATA_SIZE);
            if (!buff)
            {
                DebugError("TCPRecv realloc failed");
                return (ssize_t)NULL;
            }
            recv_total_size += recv_size;
        }
    }
    else if (flag == HTTPS_FLAG)
    {
        for (;;)
        {
            recv_size = SSL_read(GLOBAL_SSL, buff, RECEIVE_DATA_SIZE);
            if (recv_size < 0)
            {
                DebugError("TCP via https recv data failed");
                return (ssize_t)NULL;
            }
            else if (recv_size == 0)
            {
                break;
            }

            buff = realloc(buff, RECEIVE_DATA_SIZE);
            if (!buff)
            {
                DebugError("TCPRecv via https realloc failed");
                return (ssize_t)NULL;
            }
            recv_total_size += recv_size;
        }
    }

    //DisplayInfo("%s", buff);
    *rebuff = buff;
    return recv_total_size;
}

static int TCPConnectClose(int socket)
{
    //shutdown(socket, SHUT_RDWR);
    close(socket);
    return 0;
}

void FreeHTTPMethodBuff(char *p)
{
    if (p)
    {
        free(p);
    }
}

size_t HTTPMethod(const char *url, const char *request, char **response, int debug_level)
{
    /*
     * use the HTTPMethod post method post 'request_data',
     * then, return the response_data size.
     */

    Debug(DEBUG_LEVEL_3, debug_level, "Enter HTTPMethod");

    /* from str.h */
    extern pSplitURLOutput *SplitURL(const char *url, pSplitURLOutput *output);
    extern void FreeSplitURLBuff(char *host, char *suffix);

    int sock;
    pSplitURLOutput sp;

    /* here use 5 * MAX_POST_DATA_LENGTH make sure the sprintf have the enough space */
    if (!url || !request)
    {
        DebugError("url or post_str not find");
        return (size_t)NULL;
    }
    if (SplitURL(url, &sp))
    {
        DebugError("ProcessURL failed");
        return (size_t)NULL;
    }

    Debug(DEBUG_LEVEL_2, debug_level, "host_addr: %s suffix:%s port:%d", sp->host, sp->suffix, sp->port);
    /* 1 connect */
    sock = TCPConnectionCreate(sp->host, sp->port);
    if (!sock)
    {
        DebugError("TCPConnectCreate failed");
        return (size_t)NULL;
    }

    /* 2 send */
    Debug(DEBUG_LEVEL_3, debug_level, "Sending data...");
    if (!TCPSend(sock, request, HTTP_FLAG))
    {
        DebugError("TCPSend failed");
    }

    /* 3 recv */
    Debug(DEBUG_LEVEL_3, debug_level, "Recvevicing data...");
    if (!TCPRecv(sock, response, HTTP_FLAG))
    {
        DebugError("TCPRecv failed");
    }
    Debug(DEBUG_LEVEL_2, debug_level, "Data: %s", response);

    /* 4 close */
    TCPConnectClose(sock);
    Debug(DEBUG_LEVEL_3, debug_level, "Exit HTTPMethod");

    return strlen(*response);
}

/* HTTPSMethod blow */
static SSL_CTX *InitCTX(SSL_CTX **output)
{
    const SSL_METHOD *method;

    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    method = SSLv23_client_method();
    (*output) = SSL_CTX_new(method);
    if (!(*output))
    {
        DebugError("InitCTX failed");
        ERR_print_errors_fp(stderr);
        return (SSL_CTX *)NULL;
    }
    return (*output);
}

static int ShowCerts(SSL *ssl, int debug_level)
{
    X509 *cert;
    char *line;

    cert = SSL_get_peer_certificate(ssl);
    if (cert)
    {
        Debug(DEBUG_LEVEL_2, debug_level, "Server certificates:");
        line = X509_NAME_oneline(X509_get_subject_name(cert), 0, 0);
        Debug(DEBUG_LEVEL_2, debug_level, "Subject: %s", line);
        free(line);
        line = X509_NAME_oneline(X509_get_issuer_name(cert), 0, 0);
        Debug(DEBUG_LEVEL_2, debug_level, "Issuer: %s", line);
        free(line);
        X509_free(cert);
    }
    else
    {
        DebugError("ShowCerts no certificates found");
        return 1;
    }
    return 0;
}

void FreeHTTPSMethodBuff(char *p)
{
    if (p)
    {
        free(p);
    }
}

size_t HTTPSMethod(const char *url, const char *request, char **response, int debug_level)
{
    /*
     * use the HTTPs post method post 'request_data',
     * then, return the response_data size.
     */

    Debug(DEBUG_LEVEL_3, debug_level, "Enter HTTPSMethod");

    // from ../core/str.h
    extern pSplitURLOutput *SplitURL(const char *url, pSplitURLOutput *output);
    extern void FreeSplitURLBuff(char *host, char *suffix);

    int sock;
    pSplitURLOutput sp;
    SSL_CTX *ctx;
    SSL *ssl;

    /// here use 5 * MAX_POST_DATA_LENGTH make sure the sprintf have the enough space
    if (!url || !request)
    {
        DebugError("url or post_str not find");
        return (size_t)NULL;
    }
    if (!SplitURL(url, &sp))
    {
        DebugError("ProcessURL failed");
        return (size_t)NULL;
    }

    // 1 ssl init
    if (!InitCTX(&ctx))
    {
        DebugError("HTTPSMethod InitCTX failed");
        return (size_t)NULL;
    }

    Debug(DEBUG_LEVEL_2, debug_level, "host_addr: %s suffix:%s port:%d", sp->host, sp->suffix, sp->port);
    // 2 connect
    sock = TCPConnectionCreate(sp->host, sp->port);
    if (!sock)
    {
        DebugError("TCPConnectCreate failed");
        return (size_t)NULL;
    }

    // 3 add ssl
    ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);
    if (SSL_connect(ssl) < 0)
    {
        DebugError("HTTPSMethod SSL_connect failed");
        ERR_print_errors_fp(stderr);
        return (size_t)NULL;
    }
    // use the global value
    GLOBAL_SSL = ssl;

    Debug(DEBUG_LEVEL_1, debug_level, "Connect with %s encryption", SSL_get_cipher(ssl));
    if (ShowCerts(ssl, debug_level))
    {
        // failed
        DebugError("HTTPSMethod ShowCert failed");
        return (size_t)NULL;
    }

    // 4 send
    Debug(DEBUG_LEVEL_3, debug_level, "Sending data...");
    if (!TCPSend(sock, request, HTTPS_FLAG))
    {
        DebugError("TCPSend failed");
        return (size_t)NULL;
    }

    // 5 recv
    Debug(DEBUG_LEVEL_3, debug_level, "Recvevicing data...");
    if (!TCPRecv(sock, response, HTTPS_FLAG))
    {
        DebugError("TCPRecv failed");
        return (size_t)NULL;
    }
    Debug(DEBUG_LEVEL_2, debug_level, "Data: %s", response);

    // 6 close
    TCPConnectClose(sock);
    Debug(DEBUG_LEVEL_3, debug_level, "Exit HttpPostMethod");
    SSL_CTX_free(ctx);

    return strlen(*response);
}

/*
 * test result:
 * 
 */
int main(int argc, char *argv[])
{

    const char *url = "192.168.1.1";
    const char *request = "";
    char **response;
    HTTPMethod(url, request, response, 3);

    return 0;
}