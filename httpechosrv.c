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
#include <ctype.h>

#define MAXLINE 8192 /* max text line length */
#define MAXBUF 8192  /* max I/O buffer size */
#define LISTENQ 1024 /* second argument to listen() */
#define MINBUF 20
#define MIDBUF 150
#define ROOT 47
#define TIMEOUT 20

#define CHECK(X) ({int __val = (X); (__val == (-1) ? ({fprintf(stderr, "ERROR ("__FILE__":%d) -- %s\n", __LINE__, strerror(errno)); exit(-1); -1;}) : __val);})

typedef struct clnt_rq_s {
    char Method[MIDBUF];
    char URL[MIDBUF];
    char Version[MIDBUF];
} clnt_rq_t;



enum method{error = -1, get = 0,post,head};
bool child_caught_alrm = false;
bool parent_caught_alrm = false;

static int open_listenfd(int port);
static void signal_init();
static void exiting(int fd);
//static void null_checker(void *in, char *msg);
static void handle_request(int connfd);
static bool keepalive(char *status);
static bool supported_method(char *method);
static void get_file_transfer(clnt_rq_t request, int connfd);
static void error_handling(int connfd);
static void fork_handle_connection(int connfd, pid_t *pid);
static void parse_client_request(char *in, clnt_rq_t *rq);
static void connection_check(char *in);
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
        fork_handle_connection(connfd, &pid);
    }
    if(pid!=0){
        // signal(SIGQUIT,SIG_IGN);
        // kill(0,SIGQUIT);
        kill(0,SIGALRM);
        wait(NULL);
        printf("Connection:Close\n");
    }
}


static void fork_handle_connection(int connfd, pid_t *pid){
    if((connfd==-1))
        return;
    if(((*pid)=fork())==0){
        handle_request(connfd);
    }else if((*pid)==-1){
        error_handling(connfd);
    }
}


static void exiting(int fd){
    CHECK(shutdown(fd, SHUT_RDWR));
    close(fd);
    exit(0);
}

static void handle_request(int connfd){
    char buf[MAXLINE];
    clnt_rq_t client_request;
    memset(&client_request, 0, sizeof(client_request));
    while((!child_caught_alrm)&&(connfd!=-1)){
        CHECK(read(connfd, buf, MAXLINE));
        parse_client_request(buf, &client_request);
        connection_check(buf);
        get_file_transfer(client_request,connfd);
    }
    exiting(connfd);
}




static void connection_keyword(char *connection, char *status, bool *force_kill){
    if((strcmp(connection, "Connection:")==0)){
        if(keepalive(status)){
            alarm(TIMEOUT);
        }else if(strcmp(status, "close")==0){
            *force_kill = true;
        }
    }
}

static void connection_check(char *in){
    bool force_kill=false;
    char connection[MAXBUF];
    char status[MAXBUF];
    char in_cp[MAXBUF];
    char *p= NULL;
    strcpy(in_cp, in);
    p = strtok(in_cp, "\r\n");
    while (p!= NULL){
        sscanf(p, "%s %s", connection,status);
        connection_keyword(connection,status, &force_kill);
        p = strtok(NULL, "\r\n");
    }
    if(force_kill){
        kill(ppid, SIGALRM);
    }
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


//printf("Method: %s URL: %s Version: %s\n", rq.Method, rq.URL, rq.Version);
//printf("status=%d, force_close=%d, method=%d\n", flag.connection_status, flag.connection_force_close, flag.method);
//sscanf(line, "%s %s %s", (*rq).Method, (*rq).URL, (*rq).Version); 
//printf("__%d__\n%s\n",__LINE__,in_cp);
//printf("check %d\n", j++);
//int j=0;
static void parse_client_request(char *in, clnt_rq_t *rq){
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
    printf("Method: [%s] URL: [%s] Version: [%s]\n", rq->Method, rq->URL, rq->Version);
}



static void get_file_transfer(clnt_rq_t request, int connfd){
    FILE *fd; 
    char *ftype;
    uint32_t header_len=0;
    uint32_t fsize = 0;
    char header[MAXBUF];
    char dir[MAXBUF]="www";
    char fbuffer[MAXBUF];
    size_t byte_read = 0;
    const char post_data[] = "\n<html><body><pre><h1 style=\"color:blue;\">POSTDATA </h1></pre>";
    if(!request.URL ||!request.Version || !(supported_method(request.Method))){
        error_handling(connfd);
        alarm(TIMEOUT);
        return;      
    }
    memset(fbuffer, 0, sizeof(fbuffer));
    if(*(request.URL) == ROOT){
        if(request.URL+1!=NULL){
            strcat(dir, request.URL);
        }

        fd = fopen(dir, "r");
        if(fd==NULL){
            error_handling(connfd);
            alarm(TIMEOUT);
            return;        
        }

        ftype = strrchr(request.URL, '.');
        if(!ftype){
            fd = fopen("www/index.html", "r");
            ftype = "html";
        }else{
            ftype++;
        }
       
        fseek(fd, 0, SEEK_END);
        fsize = ftell(fd);
        rewind(fd);

        header_len = snprintf(header, MAXBUF, "%s ", request.Version);
        header_len+= snprintf(header+header_len, MAXBUF-header_len, "200 OK\r\n");
        header_len+= snprintf(header+header_len, MAXBUF-header_len, "Content-Type: %s\r\n", ftype);
        header_len+= snprintf(header+header_len, MAXBUF-header_len, "Content-Length: %d\r\n", fsize);
        header_len+= snprintf(header+header_len, MAXBUF-header_len, "Connection:Keep-alive\r\n\r\n");
        CHECK(send(connfd, header, header_len, 0)<0);
        if(!strcmp(ftype, "html")){
            byte_read = strlen(post_data);
            send(connfd, post_data, byte_read, 0);
        }
        
        //strcat(fbuffer, post_data);
        while((byte_read = fread(fbuffer, 1, MAXBUF, fd))> 0 || !feof(fd)) {
            CHECK(send(connfd, fbuffer, byte_read, 0)<0);
        }
    }else{
        error_handling(connfd);        
    }
    alarm(TIMEOUT);

}




static void error_handling(int connfd){
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

static bool keepalive(char *status){
    return (strcmp(status, "keep-alive")==0) || 
           (strcmp(status, "Keep-alive")==0) || 
           (strcmp(status, "Keep-Alive")==0) || 
           (strcmp(status, "Keepalive")==0)  ||
           (strcmp(status, "KeepAlive")==0)  ||         
           (strcmp(status, "keepalive")==0);       

}

static bool supported_method(char *method){
    return (strcmp(method, "GET")==0) || 
           (strcmp(method, "POST")==0);
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
