//tcpechosrv.c - A concurrent TCP echo server using threads

#include <stdio.h>
#include <stdlib.h>
#include <string.h>     /* for fgets */
#include <strings.h>    /* for bzero, bcopy */
#include <unistd.h>     /* for read, write */
#include <sys/socket.h> /* for socket use */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>
#include <limits.h>
#include <errno.h>
#include <sys/select.h>
#include <signal.h>
#define MAXLINE 8192 /* max text line length */
#define MAXBUF 8192  /* max I/O buffer size */
#define LISTENQ 1024 /* second argument to listen() */
#define MINBUF 20
#define MIDBUF 150
#define ROOT 47

#define CHECK(X) ({int __val = (X); (__val == (-1) ? ({fprintf(stderr, "ERROR ("__FILE__":%d) -- %s\n", __LINE__, strerror(errno)); exit(-1); -1;}) : __val);})

typedef struct clnt_rq_s {
    char Method[MIDBUF];
    char URL[MIDBUF];
    char Version[MIDBUF];
} clnt_rq_t;

typedef struct cnt_status_s {
    char Connection[MINBUF];
    char Status[MINBUF];
} cnt_status_t;

typedef struct cmd_flag_s {
    int method;
    bool connection_status;
    bool connection_force_close;
}cmd_flag_t;


enum method{error = -1, get = 0,post,head};
bool caught_alrm = false;
 
int open_listenfd(int port);
void echo(int connfd);
void *thread(void *vargp);
bool is_timeout(int fd, fd_set accept_fd);
static void signal_init();
static void signal_handler(int signal);
static void exiting(int fd);

int main(int argc, char **argv)
{
    int listenfd, *connfdp, port, clientlen = sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    pthread_t tid;
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    port = atoi(argv[1]);
    signal_init();
    alarm(10);
    listenfd = open_listenfd(port);
    while (1)
    {  
        connfdp = malloc(sizeof(int));
        printf("new connection???\n");
        *connfdp = accept(listenfd, (struct sockaddr *)&clientaddr, (socklen_t * )&clientlen);
        if(errno==EINTR){
            printf("HEHE %d\n", errno);
            free(connfdp);
            continue;
        }
        
        printf("new connection\n");
        CHECK(pthread_create(&tid, NULL, thread, connfdp));
    }
}

static void exiting(int fd){
    if(fd==-1){
        printf("bad fd\n");
        return;
    }
    CHECK(shutdown(fd, SHUT_RDWR));
    close(fd);
}


static void signal_handler(int signal){
    int errno_cp = errno;
    if(signal == SIGALRM){
        caught_alrm = true;
    }
    errno = errno_cp;
}
static void signal_init(){
    struct sigaction time_action;
    memset(&time_action, 0, sizeof(struct sigaction));
    time_action.sa_handler = signal_handler;
    if(sigaction(SIGALRM, &time_action, NULL)!=0){
        printf("Error %d (%s) registering for SIGALRM\n", errno, strerror(errno));
    }
}

/* thread routine */
void *thread(void *vargp)
{
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self());
    free(vargp);
    printf("new thread\n");
    alarm(10);
    echo(connfd);
    exiting(connfd);
    printf("Connection: Close\n");
    return NULL;
}

void cmd_to_idx(char *method, int *cmd_idx){
    if(strcmp(method, "GET")==0){
        *cmd_idx = get;
    }else if(strcmp(method, "POST")==0){
        *cmd_idx = post;
    }else if(strcmp(method, "HEAD")==0){
        *cmd_idx = head;
    }else{
        *cmd_idx = error;
    }
}

void connection_check(char *in, bool *status, bool *force_close){
    cnt_status_t cnt;
    char in_cp[MIDBUF];
    *status = false;
    *force_close = false;
    strcpy(in_cp, in);
    sscanf(in_cp, "%s %s", cnt.Connection,cnt.Status);
    if((strcmp(cnt.Connection, "Connection:")==0) && (strcmp(cnt.Status, "Keep-alive")==0)){
        *status = true;
        *force_close = false;
    }else if((strcmp(cnt.Connection, "Connection:")==0) && (strcmp(cnt.Status, "Close")==0)){
        *status = true;
        *force_close = true;
    }
}


void error_handling(char *Version, int connfd){
    char header[MAXBUF];
    int content_len = 0;
    char content[] = "<html><head></head><body><h1>500 Internal Server Error<h1></body></html>";
    uint32_t header_len=0;
    content_len = strlen(content);
    header_len = snprintf(header, MAXBUF, "HTTP/1.1 500 Internal Server Error\r\n");
    header_len+= snprintf(header+header_len, MAXBUF-header_len, "Content-Type: text/html\r\n");
    header_len+= snprintf(header+header_len, MAXBUF-header_len, "Content-Length: %u\r\n\r\n", content_len);
    CHECK(send(connfd, header, strlen(header), 0));
    CHECK(send(connfd, content, content_len,0));
}

//printf("Method: %s URL: %s Version: %s\n", rq.Method, rq.URL, rq.Version);
//printf("status=%d, force_close=%d, method=%d\n", flag.connection_status, flag.connection_force_close, flag.method);
//sscanf(line, "%s %s %s", (*rq).Method, (*rq).URL, (*rq).Version); 
void parse_client_request(char *in, clnt_rq_t *rq, cmd_flag_t *flag){
    char in_cp[MAXBUF];
    char *line= NULL;
    char line_cp[MAXBUF];
    char *token[3], *p;
     int i =0;
    strcpy(in_cp, in);

    line = strtok(in_cp, "\r\n");
    strcpy(line_cp, line);

    p = strtok(line_cp, " ");
    while(p!=NULL){
        token[i++]=p;
        p=strtok(NULL, " ");
    }
    strcpy((*rq).Method,  token[0]);
    strcpy((*rq).URL,     token[1]);
    strcpy((*rq).Version, token[2]);

    cmd_to_idx((*rq).Method, &((*flag).method));
    if(((*flag).method)==error){
        printf("#%d invalid method\n", __LINE__);
        return;
    }

    while (line!= NULL){
        connection_check(line, &((*flag).connection_status), &((*flag).connection_force_close));
        line = strtok(NULL, "\r\n");
    }
    printf("Method: [%s] URL: [%s] Version: [%s]\n", rq->Method, rq->URL, rq->Version);
}

void null_checker(void *in, char *msg){
    if(in==NULL){
        printf("%s\n", msg);
        exit(-1);
    }
}

void get_file_transfer(char *URL, char *Version, int connfd){
    FILE *fd; 
    char *ftype;
    uint32_t header_len=0;
    uint32_t fsize = 0;
    char header[MAXBUF];
    char dir[MAXBUF]="www";
    char fbuffer[MAXBUF];
    size_t byte_read = 0;
    if(!URL||!Version)
        return;
    memset(fbuffer, 0, sizeof(fbuffer));
    if(*(URL) == ROOT){
        if(URL+1!=NULL){
            strcat(dir, URL);
        }

        fd = fopen(dir, "r");
        null_checker(fd,"No path found");  

        ftype = strrchr(URL, '.');
        if(!ftype){
            fd = fopen("www/index.html", "r");
            null_checker(fd,"No path found");  
            ftype = "html";
        }else{
            ftype++;
        }

        fseek(fd, 0, SEEK_END);
        fsize = ftell(fd);
        rewind(fd);

        header_len = snprintf(header, MAXBUF, "%s ", Version);
        header_len+= snprintf(header+header_len, MAXBUF-header_len, "200 OK\r\n");
        header_len+= snprintf(header+header_len, MAXBUF-header_len, "Content-Type: %s\r\n", ftype);
        header_len+= snprintf(header+header_len, MAXBUF-header_len, "Content-Length: %d\r\n", fsize);
        header_len+= snprintf(header+header_len, MAXBUF-header_len, "Connection:Keep-alive\r\n\r\n");
        if(send(connfd, header, header_len, 0)<0){
            error_handling(Version, connfd);
        }
        while((byte_read = fread(fbuffer, 1, MAXBUF, fd))> 0 || !feof(fd)) {
            if(send(connfd, fbuffer, byte_read, 0)<0){
                error_handling(Version, connfd);
            }
        }
        alarm(10);
    }

}

//https://stackoverflow.com/questions/28896388/implementing-execution-timeout-with-c-c
// bool is_timeout(int fd, fd_set accept_fd){
//     int ret = -1;
//     struct timeval timeout = {10,0};

//     FD_ZERO(&accept_fd);
//     FD_SET(fd, &accept_fd);
//     CHECK(ret = select(8, &accept_fd, NULL,NULL,&timeout));
//     if(ret==0) 
//         return true;
//     return false;
// }

void variable_init(cmd_flag_t *cmd_flag, clnt_rq_t *client_request){
    (*cmd_flag).connection_status = false;
    (*cmd_flag).connection_force_close = false;
    (*cmd_flag).method = error;
    memset(client_request, 0, sizeof(*client_request));
}
void echo(int connfd){
    cmd_flag_t cmd_flag;
    char buf[MAXLINE];
    clnt_rq_t client_request;
    while(!caught_alrm){
        variable_init(&cmd_flag, &client_request);
        CHECK(read(connfd, buf, MAXLINE));
        parse_client_request(buf, &client_request, &cmd_flag);
        get_file_transfer(client_request.URL,client_request.Version,connfd);
    }
    pthread_exit(0); 
}


/*
 * open_listenfd - open and return a listening socket on port
 * Returns -1 in case of failure
 */
int open_listenfd(int port)
{
    int listenfd, optval = 1;
    struct sockaddr_in serveraddr;

    /* Create a socket descriptor */
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;

    /* Eliminates "Address already in use" error from bind. */
    CHECK(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
                   (const void *)&optval, sizeof(int)));

    /* listenfd will be an endpoint for all requests to port
       on any IP address for this host */
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)port);
    CHECK(bind(listenfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)));

    /* Make it a listening socket ready to accept connection requests */
    CHECK(listen(listenfd, LISTENQ));
    return listenfd;
} /* end open_listenfd */
