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
#include <sys/wait.h>
#include <fcntl.h>

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


enum method{error = -1, get = 0,post,head};
bool child_caught_alrm = false;
bool parent_caught_alrm = false;

int open_listenfd(int port);
void handle_request(int connfd);
void handle_connection(int connfd);
bool is_timeout(int fd, fd_set accept_fd);
static void signal_init();
static void exiting(int fd);
void get_file_transfer(char *URL, char *Version, int connfd);
void error_handling(char *Version, int connfd);
pid_t ppid;

int main(int argc, char **argv){
    int listenfd, connfd, port, clientlen = sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    pid_t pid;
    if (argc != 2){
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    port = atoi(argv[1]);
    signal_init();
    ppid = getpid();
    listenfd = open_listenfd(port);
    while (!parent_caught_alrm){
        connfd = accept(listenfd, (struct sockaddr *)&clientaddr, (socklen_t * )&clientlen);
        if((connfd!=-1)){
            if((pid=fork())==0){
                handle_connection(connfd);
            }else if(pid==-1){
                exiting(connfd);
            }
        }
    }
    signal(SIGQUIT,SIG_IGN);
    kill(0,SIGQUIT);
    if(pid!=0)
        printf("Connection:Close\n");
}

static void exiting(int fd){
    if(fd==-1)
        return;
    CHECK(shutdown(fd, SHUT_RDWR));
    close(fd);
}


void signal_handler(int signal, siginfo_t *info, void *context){
    int errno_cp = errno;
    if(signal == SIGALRM){
        if((info->si_pid)==ppid){
            parent_caught_alrm = true;
        }else{
            kill(ppid, SIGALRM);
            child_caught_alrm = true;
        }
    }
    errno = errno_cp;
}

static void signal_init(){
    struct sigaction time_action;
    memset(&time_action, 0, sizeof(struct sigaction));
    time_action.sa_sigaction = (void*) signal_handler;
    sigemptyset(&time_action.sa_mask);
    time_action.sa_flags = SA_RESTART|SA_SIGINFO;
    if(sigaction(SIGALRM, &time_action, NULL)!=0){
        printf("Error %d (%s) registering for SIGALRM\n", errno, strerror(errno));
    }
}

void handle_connection(int connfd){
    handle_request(connfd);
    exiting(connfd);
    exit(0);
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

void connection_check(char *in){
    cnt_status_t cnt;
    char in_cp[MIDBUF];
    
    strcpy(in_cp, in);
    sscanf(in_cp, "%s %s", cnt.Connection,cnt.Status);
    if((strcmp(cnt.Connection, "Connection:")==0) && (strcmp(cnt.Status, "Keep-alive")==0)){
        alarm(10);
    }else if((strcmp(cnt.Connection, "Connection:")==0) && (strcmp(cnt.Status, "Close")==0)){
        kill(ppid, SIGALRM);
    }
}




//printf("Method: %s URL: %s Version: %s\n", rq.Method, rq.URL, rq.Version);
//printf("status=%d, force_close=%d, method=%d\n", flag.connection_status, flag.connection_force_close, flag.method);
//sscanf(line, "%s %s %s", (*rq).Method, (*rq).URL, (*rq).Version); 
void parse_client_request(char *in, clnt_rq_t *rq){
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

    while (line!= NULL){
        connection_check(line);
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


void handle_request(int connfd){
    char buf[MAXLINE];
    clnt_rq_t client_request;
    memset(&client_request, 0, sizeof(client_request));
    while((!child_caught_alrm)&&(connfd!=-1)){
        CHECK(read(connfd, buf, MAXLINE));
        parse_client_request(buf, &client_request);
        get_file_transfer(client_request.URL,client_request.Version,connfd);
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

/*
 * open_listenfd - open and return a listening socket on port
 * Returns -1 in case of failure
 */
int open_listenfd(int port)
{
    int listenfd, optval = 1;
    struct sockaddr_in serveraddr;
    int flags;
    /* Create a socket descriptor */
    listenfd = CHECK(socket(AF_INET, SOCK_STREAM, 0));
    flags = CHECK(fcntl(listenfd,F_GETFL));
    CHECK(fcntl(listenfd,F_SETFL, flags | O_NONBLOCK));
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
