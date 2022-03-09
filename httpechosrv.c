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
#define TIMEOUT 10

#define CHECK(X) ({int __val = (X); (__val == (-1) ? ({fprintf(stderr, "ERROR ("__FILE__":%d) -- %s\n", __LINE__, strerror(errno)); exit(-1); -1;}) : __val);})

typedef struct clnt_rq_s {
    char Method[MIDBUF];
    char URL[MIDBUF];
    char Version[MIDBUF];
} clnt_rq_t;


enum connectivity{live, dead};
enum method{error = -1, get = 0,post,head};

bool caught_alrm = false;
static int open_listenfd(int port);
static void signal_init();
static void open_pipe(int *pipe_fd);
static void handle_request(int connfd,int *pipefd);
static bool iskeepalive(char *status);
static bool supported_method(char *method);
static void get_file_transfer(clnt_rq_t request, int connfd);
static void error_handling(int connfd);
static void fork_handle_connection(int connfd, int *pipefd,pid_t *pid);
static void parse_client_request(char *in, clnt_rq_t *rq);
static void connection_check(char *in,int *pipefd);
static void child_state(int *pipefd);
static void child_exiting(int fd);
static void parent_exiting(pid_t pid, int pipe_fdw);
pid_t ppid;


int main(int argc, char **argv){
    int listenfd, connfd, port, clientlen = sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    pid_t pid;
    if (argc != 2){
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    int pipe_fd[2];
    port = atoi(argv[1]);
    signal_init();
    open_pipe(pipe_fd);
    ppid = getpid();
    listenfd = open_listenfd(port);
    while (!caught_alrm){
        child_state(pipe_fd);
        connfd = accept(listenfd, (struct sockaddr *)&clientaddr, (socklen_t * )&clientlen);
        fork_handle_connection(connfd, pipe_fd, &pid);
    }
    parent_exiting(pid, pipe_fd[1]);
}

/*
 * parent_exiting - exiting from parent
 */
static void parent_exiting(pid_t pid, int pipe_fdw){
    if(pid!=0){
        wait(NULL);
        if(pipe_fdw!=-1)
            close(pipe_fdw);
        printf("Connection:Close\n");
    }
}
/*
 * fork_handle_connection - fork connection when the 
 * tcp connection estabilished
 */
static void fork_handle_connection(int connfd, int *pipefd, pid_t *pid){
    if((connfd==-1))
        return;
    if(((*pid)=fork())==0){
        handle_request(connfd,pipefd);
    }else if((*pid)==-1){
        error_handling(connfd);
    }
}

/*
 * child_exiting - exiting from child 
 */
static void child_exiting(int fd){
    CHECK(shutdown(fd, SHUT_RDWR));
    close(fd);
    exit(0);
}

/*
 * handle_request - handling request like 
 * request parse, request connection state
 * transfer file and clean up
 */
static void handle_request(int connfd,int *pipefd){
    char buf[MAXLINE];
    clnt_rq_t client_request;
    memset(&client_request, 0, sizeof(client_request));
    CHECK(read(connfd, buf, MAXLINE));
    parse_client_request(buf, &client_request);
    connection_check(buf,pipefd);
    get_file_transfer(client_request,connfd);
    child_exiting(connfd);
}


/*
 * connection_keyword - check client connection 
 * request and set connection state
 */
static void connection_keyword(char *connection, char *status, enum connectivity *state){
    if((strcmp(connection, "Connection:")==0)){
        if(!iskeepalive(status) || !(strcmp(status, "close"))){
            *state = dead;
        }
    }
}
/*
 * connection_check - break down http request line
 * and check the connection keyword
 * write to parent about the connection state 
 */
static void connection_check(char *in, int *pipefd){
    char connection[MAXBUF];
    char status[MAXBUF];
    char in_cp[MAXBUF];
    char *p= NULL;
    enum connectivity state = live;
    strcpy(in_cp, in);
    //check everyline with dimiliter "\r\n"
    p = strtok(in_cp, "\r\n");
    while (p!= NULL){
        sscanf(p, "%s %s", connection,status);
        connection_keyword(connection,status, &state);
        p = strtok(NULL, "\r\n");
    }
    //close read pipe and write to parents
    close(pipefd[0]);
    write(pipefd[1],&state,sizeof(int));
    close(pipefd[1]);
}

/*
 * child_state - calling from parent
 * to get child connectivity report
 */
static void child_state(int *pipefd){
    int pid;
    int state;
    if((pid = getpid())==0)
        return;
    //read from parents
    read(pipefd[0], &state, sizeof(int));
    //reset timer
    if(state == live){
        alarm(TIMEOUT);
    //kill the parent loop
    }else if (state == dead){
        kill(pid, SIGALRM);
    }
}

/*
 * signal_handler - handle only SIGALRM
 * to end the parent process
 */
void signal_handler(int signal, siginfo_t *info, void *context){
    int errno_cp = errno;
    if(signal == SIGALRM){
        caught_alrm = true;
    }
    errno = errno_cp;
}


/*
 * signal_init - init signal
 */
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


/*
 * parse_client_request - get client connection request
 */
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


/*
 * get_file_transfer - transfer file base on request
 */
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
        /*check if exist a path other than ROOT*/
        if(request.URL+1!=NULL){
            strcat(dir, request.URL);
        }

        fd = fopen(dir, "r");
        //handle error of wrong directory 
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
        //get file size
        fseek(fd, 0, SEEK_END);
        fsize = ftell(fd);
        rewind(fd);

        //header construction
        header_len = snprintf(header, MAXBUF, "%s ", request.Version);
        header_len+= snprintf(header+header_len, MAXBUF-header_len, "200 OK\r\n");
        header_len+= snprintf(header+header_len, MAXBUF-header_len, "Content-Type: %s\r\n", ftype);
        header_len+= snprintf(header+header_len, MAXBUF-header_len, "Content-Length: %d\r\n", fsize);
        header_len+= snprintf(header+header_len, MAXBUF-header_len, "Connection:Keep-alive\r\n\r\n");
        CHECK(send(connfd, header, header_len, 0)<0);
        
        if(!strcmp(ftype, "html") && !strcmp(request.Method, "POST")){
            byte_read = strlen(post_data);
            send(connfd, post_data, byte_read, 0);
        }

        while((byte_read = fread(fbuffer, 1, MAXBUF, fd))> 0 || !feof(fd)) {
            CHECK(send(connfd, fbuffer, byte_read, 0)<0);
        }
       
    }else{
        error_handling(connfd);        
    }
}



/*
 * error_handling - transfer 500 on error
 */
static void error_handling(int connfd){
    char header[MAXBUF];
    int content_len = 0;
    char content[] = "<html><head></head><body><h1>500 Internal Server Error<h1></body></html>";
    uint32_t header_len=0;
    content_len = strlen(content);
    header_len = snprintf(header, MAXBUF, "HTTP/1.1 500 Internal Server Error\r\n");
    header_len+= snprintf(header+header_len, MAXBUF-header_len, "Content-Type: text/html\r\n");
    header_len+= snprintf(header+header_len, MAXBUF-header_len, "Content-Length: %u\r\n\r\n", content_len);
    //send header packet
    CHECK(send(connfd, header, strlen(header), 0));
    //send 500 error
    CHECK(send(connfd, content, content_len,0));
}


/*
 *iskeepalive - is it keepalive keyword
 */
static bool iskeepalive(char *status){
    return (strcmp(status, "keep-alive")==0) || 
           (strcmp(status, "Keep-alive")==0) || 
           (strcmp(status, "Keep-Alive")==0) || 
           (strcmp(status, "Keepalive")==0)  ||
           (strcmp(status, "KeepAlive")==0)  ||         
           (strcmp(status, "keepalive")==0);       

}
/*
 *supported_method - is the method supported
 */
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

/**
 * @brief open a non blocking read pipe
 * @param pipe_fd 
 */
void open_pipe(int *pipe_fd){
    int flags;
    CHECK(pipe(pipe_fd));
    flags = CHECK(fcntl(*pipe_fd,F_GETFL));
    CHECK(fcntl(*pipe_fd,F_SETFL, flags | O_NONBLOCK));
}