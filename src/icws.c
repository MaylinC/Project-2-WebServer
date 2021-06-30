#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <getopt.h>
#include <time.h>
#include <poll.h>
// extern "C" 
// {
//     #include "parse.h"
//     #include "work_q.h"
// }
#include "parse.h"
#include "work_q.h"
#include <sys/wait.h>
#define BUFSIZE 2048
#define MAXBUF 8192

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;  
pthread_mutex_t parse_mutex = PTHREAD_MUTEX_INITIALIZER; 
pthread_cond_t condition_variable = PTHREAD_COND_INITIALIZER; 

struct pollfd fds[1]; 
int timeOut;
int ret; 
int *cf_client; 
char *program; 
int persistance = 1; 

char *rootFolder_glob;

typedef struct sockaddr SA;

struct survival_bag {
    struct sockaddr_storage clientAddr;
    int connFd;
    char *rootFolder;
};

static struct option long_options[] =   {

    {"port", required_argument, NULL, 'p'},
    {"root", required_argument, NULL, 'r'},
    {"numThreads", required_argument, NULL, 't'},
    {"timeout", required_argument, NULL, 'o'},
    {"cgiHandler", required_argument, NULL, 'c'},
    {NULL, 0, NULL, 0}
};

typedef enum {
  RUNNING, EXITING
} p_state;

typedef struct p_threadpool{
    p_state state; 
    pthread_t *thread_array;  
    int thread_count;
} threadpool; 

char *local_time() {

    char *date_time; 
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    date_time = asctime(tm); 
    return date_time;

}

void respond_head(int connFd, char *uri, char *mime, int persis) { 

    char buf[MAXBUF];
    int uriFd = open(uri, O_RDONLY);
    char *connection; 

    if (persis == 0) {
        connection = "close"; 
    }
    else {
        connection = "keep-alive"; 
    }

    if (uriFd < 0 || mime == NULL)
    {
        char *msg = "404 Not Found\n";
        sprintf(buf,
                "HTTP/1.1 404 Not Found\r\n"
                "Date: %s"
                "Server: icws\r\n"
                "Connection: close\r\n\r\n"
                ,local_time());
        write_all(connFd, buf, strlen(buf));
        write_all(connFd, msg, strlen(msg));
        persistance = 0;
        return;
    }
    struct stat fstatbuf;
    fstat(uriFd, &fstatbuf);
    sprintf(buf,
            "HTTP/1.1 200 OK\r\n"
            "Date: %s"
            "Server: icws\r\n"
            "Connection: %s\r\n"
            "Content-length: %lu\r\n"
            "Content-type: %s\r\n"
            "Last-Modified: %s\r\n",
            local_time(),connection,fstatbuf.st_size,mime,ctime(&fstatbuf.st_mtim));
    write_all(connFd, buf, strlen(buf));  // write header into  connFd
}


void respond_all(int connFd, char *uri, char *mime, int persis)
{
    char buf[MAXBUF];
    int uriFd = open(uri, O_RDONLY);
    char *connection; 

    if (persis == 0) {
        connection = "close"; 
    }
    else {
        connection = "keep-alive"; 
    }
    
    char *msg = "404 Not Found";
    if (uriFd < 0 || mime == NULL)
    {
        sprintf(buf,
                "HTTP/1.1 404 Not Found\r\n"
                "Date: %s"
                "Server: icws\r\n"
                "Connection: close\r\n\r\n",local_time());
        write_all(connFd, buf, strlen(buf));
        write_all(connFd, msg, strlen(msg));
        persistance = 0;
        return;
    }

    struct stat fstatbuf;
    fstat(uriFd, &fstatbuf);
    sprintf(buf,
            "HTTP/1.1 200 OK\r\n"
            "Date: %s"
            "Server: icws\r\n"
            "Connection: %s\r\n"
            "Content-length: %lu\r\n"
            "Content-type: %s\r\n"
            "Last-Modified: %s\r\n",
            local_time(),connection,fstatbuf.st_size, mime,ctime(&fstatbuf.st_mtim));
    write_all(connFd, buf, strlen(buf));  // write header into  connFd
    write_logic(uriFd, connFd); // send the content data into connFd
}

const char *get_filename_ext(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if(!dot || dot == filename) return "";
    return dot + 1;
}

char *parse_file_type(int connFd,char *rootFolder, Request *request) {

    char *m_type; 

    if (strcmp(get_filename_ext(request->http_uri),"html") == 0) {
        
        m_type = "text/html"; 
        return m_type; 
    }
    else if (strcmp(get_filename_ext(request->http_uri),"jpg") == 0 || strcmp(get_filename_ext(request->http_uri),"jpeg") == 0) {
        
        m_type = "image/jpeg"; 
        return m_type; 
    }
    else if (strcmp(get_filename_ext(request->http_uri),"css") == 0) {

        m_type = "text/css"; 
        return m_type; 
    }
    else if (strcmp(get_filename_ext(request->http_uri),"txt") == 0) {

        m_type = "text/plain"; 
        return m_type; 
    }
    else if (strcmp(get_filename_ext(request->http_uri),"js") == 0) {

        m_type = "text/javascript"; 
        return m_type; 
    }
    else if (strcmp(get_filename_ext(request->http_uri),"png") == 0) {
                
        m_type = "image/png"; 
        return m_type; 
        
    }
    else if (strcmp(get_filename_ext(request->http_uri),"gif") == 0) {
        
        m_type = "image/gif"; 
        return m_type;  
    }
    else {
        return m_type;
    }
}

void fail_exit(char *msg) { fprintf(stderr, "%s\n", msg); exit(-1); }

void cgi(char *inferiorCmd ,Request *request) {
    int c2pFds[2]; /* Child to parent pipe */
    int p2cFds[2]; /* Parent to child pipe */

    if (pipe(c2pFds) < 0) fail_exit("c2p pipe failed.");
    if (pipe(p2cFds) < 0) fail_exit("p2c pipe failed.");

    int pid = fork();

    if (pid < 0) fail_exit("Fork failed.");
    if (pid == 0) { /* Child - set up the conduit & run inferior cmd */

        /* Wire pipe's incoming to child's stdin */
        /* First, close the unused direction. */
        if (close(p2cFds[1]) < 0) fail_exit("failed to close p2c[1]");
        if (p2cFds[0] != STDIN_FILENO) {
            if (dup2(p2cFds[0], STDIN_FILENO) < 0)
                fail_exit("dup2 stdin failed.");
            if (close(p2cFds[0]) < 0)
                fail_exit("close p2c[0] failed.");
        }

        /* Wire child's stdout to pipe's outgoing */
        /* But first, close the unused direction */
        if (close(c2pFds[0]) < 0) fail_exit("failed to close c2p[0]");
        if (c2pFds[1] != STDOUT_FILENO) {
            if (dup2(c2pFds[1], STDOUT_FILENO) < 0)
                fail_exit("dup2 stdin failed.");
            if (close(c2pFds[1]) < 0)
                fail_exit("close pipeFd[0] failed.");
        }

        char* inferiorArgv[] = {inferiorCmd, NULL};
        if (execvpe(inferiorArgv[0], inferiorArgv, environ) < 0)
            fail_exit("exec failed.");
    }
    else { /* Parent - send a random message */
        /* Close the write direction in parent's incoming */
        if (close(c2pFds[1]) < 0) fail_exit("failed to close c2p[1]");

        /* Close the read direction in parent's outgoing */
        if (close(p2cFds[0]) < 0) fail_exit("failed to close p2c[0]");

        char *message = "OMGWTFBBQ\n";
        /* Write a message to the child - replace with write_all as necessary */
        write(p2cFds[1], message, strlen(message));
        /* Close this end, done writing. */
        if (close(p2cFds[1]) < 0) fail_exit("close p2c[01] failed.");

        char buf[BUFSIZE+1];
        ssize_t numRead;
        /* Begin reading from the child */
        while ((numRead = read(c2pFds[0], buf, BUFSIZE))>0) {
            printf("Parent saw %ld bytes from child...\n", numRead);
            buf[numRead] = '\x0'; /* Printing hack; won't work with binary data */
            printf("-------\n");
            printf("%s", buf);
            printf("-------\n");
        }
        /* Close this end, done reading. */
        if (close(c2pFds[0]) < 0) fail_exit("close c2p[01] failed.");

        /* Wait for child termination & reap */
        int status;

        if (waitpid(pid, &status, 0) < 0) fail_exit("waitpid failed.");
        printf("Child exited... parent's terminating as well.\n");
    }
}

void serve_http(int* connfd,char *rootFolder)
{
    
    char buf[MAXBUF];
    char buffer[MAXBUF]; 
    char head[MAXBUF];
    char newPath[80];
    int readRet; 
    char *connetion; 
    int sizeRat = 0; 
    int connFd = *((int*)connfd); 
    memset(buffer,'\0',MAXBUF); 
    
    //printf("connFd in serv: %d \n", connFd); 

    fds[0].fd = connFd; 
    fds[0].events = POLLIN;

    ret = poll(fds,1,timeOut * 1000);

    if (ret == -1) {
		perror ("poll error");
		exit(1); 
	}

    if (!ret) {
        printf("ret = %d \n", ret); 
        printf("Timeout: %d second pass \n", timeOut);  
        sprintf(buf,"Timeout: %d second pass \r\n", timeOut);
        write_all(connFd, buf, strlen(buf));
        persistance = 0;
        return; 
    }
  
    if (fds[0].revents & POLLIN) {

        while((readRet = read_line(connFd, buf, MAXBUF))>0) {
            sizeRat+=readRet; 
            strcat(buffer, buf); 
            if (!strcmp(buf,"\r\n")){
                break; 
            }
        }
    }

    printf("buffer: %s", buffer); 

    //printf("readRet: %d", readRet); 
    if (readRet < 0) {
	    printf("Failed read\n");
	    return;
	}

    pthread_mutex_lock(&parse_mutex); 
    Request *request = parse(buffer,sizeRat,connFd);
    pthread_mutex_unlock(&parse_mutex); 
    
    if (request == NULL) // handled malformede request
    {
        printf("LOG: Bad Request \n");
        sprintf(head,
                "HTTP/1.1 400 Bad Request\r\n"
                "Date: %s\r\n"
                "Server: icws\r\n"
                "Connection: close\r\n\r\n",local_time());
        write_all(connFd, head, strlen(head));
        free(request);
        persistance = 0;
        return;
    }

    for(int index = 0; index < request->header_count; index++) {
        if (strcasecmp(request->headers[index].header_name, "Connection") == 0) {
            if (strcasecmp(request->headers[index].header_value, "keep-alive") != 0) {
                persistance = 0;
                break; 
            }
        }
    }

    if (strcasecmp(request->http_version, "HTTP/1.1") != 0 && (strcasecmp(request->http_version, "HTTP/1.0") != 0)) {
        
        printf("LOG: Not Supported the current HTTP Version \n");
        sprintf(head,
                "HTTP/1.1 505 HTTP Version Not Supported \r\n"
                "Date: %s\r\n"
                "Server: icws\r\n"
                "Connection: close\r\n\r\n"
                ,local_time());
        write_all(connFd, head, strlen(head));
        free(request->headers); 
        free(request);
        persistance = 0;
        return;
    }

    if (strstr(request->http_uri,"cgi") != NULL) {
        cgi(program,request); 
    }

    if (strcasecmp(request->http_method, "GET") == 0)
    {
        if (request->http_uri[0] == '/') {

            sprintf(newPath, "%s%s", rootFolder, request->http_uri);

            printf("newPath: %s \n", newPath); 
            printf("exten: %s \n", get_filename_ext(request->http_uri)); 

            respond_all(connFd, newPath, parse_file_type(connFd,rootFolder,request),persistance); 
        }
    }
    else if (strcasecmp(request->http_method, "Head") == 0) {
         
        if (request->http_uri[0] == '/') {

            sprintf(newPath, "%s%s", rootFolder, request->http_uri);

            printf("newPath: %s \n", newPath); 
            printf("exten: %s \n", get_filename_ext(request->http_uri)); 

            respond_head(connFd, newPath, parse_file_type(connFd,rootFolder,request),persistance); 
        }
    }

    else {
        printf("LOG: Unknown request\n");
        sprintf(head,
                "HTTP/1.1 501 Method Unimplemented\r\n"
                "Date: %s\r\n"
                "Server: icws\r\n"
                "Connection: close\r\n\r\n",local_time());
        write_all(connFd, head, strlen(head));
        free(request);
        persistance = 0;
        return;
    }
}

void * do_work(void *pool) {

    for (;;) {

        threadpool *t_pool = (threadpool *) pool;
        //printf("numthread: %d \n", t_pool->thread_count);
        pthread_mutex_lock(&mutex); 
        int *cf_client; 
        cf_client = pop(); 
    
        if (cf_client == NULL) {

            pthread_cond_wait(&condition_variable, &mutex); 
            cf_client = pop();
            //printf("ct_client: %d \n", ct_client);   
        }
        pthread_mutex_unlock(&mutex); 

        if (cf_client != NULL) {
            
            while (persistance == 1) { 
                serve_http(cf_client,rootFolder_glob); 
            }

            if (persistance == 0) {
                int connFd = *((int*)cf_client);  
                close(connFd);
            }
        }  
    }  
}

void print_usage() {
    printf("Usage: ./icws --port <listenPort> --root <wwwRoot> --numThreads <numThreads> --timeout <timeout> --cgiHandler <cgiProgram>  \n"); 
    exit(1); 
}

int main(int argc, char *argv[])
{
    int option; 
    int listenFd;
    int port;
    char *rootFolder;
    int numThread;
    threadpool *pool;

    pthread_mutex_init(&mutex,NULL);
    pthread_mutex_init(&parse_mutex,NULL);
    pthread_cond_init(&condition_variable,NULL);

    if (argc < 9) {
        print_usage(); 
    } 

    while ((option = getopt_long(argc,argv,"p:r:t:o:c", long_options, NULL)) != -1) {   
        
        switch (option) {

            case 'o': 
            timeOut = atoi(optarg);
            printf("timeOut: %d \n", timeOut); 
            break;

            case 'p':
            printf("port: %s \n", optarg);
            listenFd = open_listenfd(optarg);
            break;

            case 'r':
            rootFolder = optarg; 
            printf("root: %s \n", rootFolder); 
            break;

            case 't': 
            numThread = atoi(optarg);
            printf("numThread: %d \n", numThread); 
            break;

            case 'c': 
            program = optarg;
            printf("cgiHandler: %s \n", program); 
            break;
            
            default: 
            exit(1); 
        } 
    }

    pool = (threadpool *) malloc(sizeof(threadpool));
    if (pool == NULL) { 
        fprintf(stderr, "fail to malloc!\n");
		return NULL;
    }

    pool->thread_count = numThread;
    pool->state = RUNNING;
    pool->thread_array = (pthread_t *) malloc (pool->thread_count * sizeof(pthread_t));

    if (pool->thread_array == NULL) {
        fprintf(stderr, "fail to malloc!\n");
        free(pool);
		return NULL;
    }
    for(int i = 0; i < pool->thread_count; i++){ 
        pthread_create(& pool->thread_array[i],NULL,do_work,(void *)pool); 
    }
    
    for (;;)
    {
        struct sockaddr_storage clientAddr;
        socklen_t clientLen = sizeof(struct sockaddr_storage);

        int connFd = accept(listenFd, (SA *)&clientAddr, &clientLen);
      
        if (connFd < 0)
        {
            fprintf(stderr, "Failed to accept\n");
            continue;
        }

        struct survival_bag *context = 
            (struct survival_bag *) malloc(sizeof(struct survival_bag));

        if (context == NULL) { 
            fprintf(stderr, "fail to malloc!\n");
		    return NULL;
        }

        context->connFd = connFd;
        //printf("connFd: %d \n", connFd); 
        context->rootFolder = rootFolder;
        rootFolder_glob = context->rootFolder; 
        //printf("rootFolder_glob: %s \n", rootFolder_glob); 
        memcpy(&context->clientAddr, &clientAddr, sizeof(struct sockaddr_storage));
        //shared.work_q.add_job(context->connFd); 
        //conn_handler((void *) context); 

        char hostBuf[MAXBUF], svcBuf[MAXBUF];
        if (getnameinfo((SA *)&clientAddr, clientLen,
                        hostBuf, MAXBUF, svcBuf, MAXBUF, 0) == 0)
            printf("Connection from %s:%s\n", hostBuf, svcBuf);
        else
            printf("Connection from ?UNKNOWN?\n");
        

        pthread_mutex_lock(&mutex); 

        cf_client = (int *)malloc(sizeof(int)); 

        if (cf_client == NULL) { 
            fprintf(stderr, "fail to malloc!\n");
		    return NULL;
        } 
        *cf_client = context->connFd;
        push(cf_client); 
        pthread_cond_signal(&condition_variable); 
        pthread_mutex_unlock(&mutex); 

        free(context);

    }

    free(pool->thread_array);
    free(pool); 
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&condition_variable); 
    free(cf_client); 

    return 0;
}