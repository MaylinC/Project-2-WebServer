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
#define MAXBUF 8192

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;  
pthread_mutex_t parse_mutex = PTHREAD_MUTEX_INITIALIZER; 
pthread_cond_t condition_variable = PTHREAD_COND_INITIALIZER; 

struct pollfd fds[1]; 
int timeOut;
int ret; 
int *cf_client; 

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

void respond_head(int connFd, char *uri, char *mime) { 

    char buf[MAXBUF];
    int uriFd = open(uri, O_RDONLY);

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
        return;
    }
    struct stat fstatbuf;
    fstat(uriFd, &fstatbuf);
    sprintf(buf,
            "HTTP/1.1 200 OK\r\n"
            "Date: %s"
            "Server: icws\r\n"
            "Connection: close\r\n"
            "Content-length: %lu\r\n"
            "Content-type: %s\r\n"
            "Last-Modified: %s\r\n",
            local_time(),fstatbuf.st_size,mime,ctime(&fstatbuf.st_mtim));
    write_all(connFd, buf, strlen(buf));  // write header into  connFd
}


void respond_all(int connFd, char *uri, char *mime)
{
    char buf[MAXBUF];
    int uriFd = open(uri, O_RDONLY);
    
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
        return;
    }

    struct stat fstatbuf;
    fstat(uriFd, &fstatbuf);
    sprintf(buf,
            "HTTP/1.1 200 OK\r\n"
            "Date: %s"
            "Server: icws\r\n"
            "Connection: close\r\n"
            "Content-length: %lu\r\n"
            "Content-type: %s\r\n"
            "Last-Modified: %s\r\n",
            local_time(),fstatbuf.st_size, mime,ctime(&fstatbuf.st_mtim));
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

void serve_http(int* connfd,char *rootFolder)
{
    
    char buf[MAXBUF];
    char buffer[MAXBUF]; 
    char head[MAXBUF];
    char newPath[80];
    int readRet; 
    int sizeRat = 0; 

    int connFd = *((int*)connfd); 
    
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
    }
  
    if (fds[0].revents & POLLIN) {

        while((readRet = read_line(connFd, buf, MAXBUF))>0) {
            sizeRat+=readRet; 
            strcat(buffer, buf); 
            if (!strcmp(buf,"\r\n")) {
                break; 
            }
        }
    }

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
        return;
    }

    else if (strcasecmp(request->http_method, "GET") == 0)
    {
        if (request->http_uri[0] == '/') {

            sprintf(newPath, "%s%s", rootFolder, request->http_uri);

            printf("newPath: %s \n", newPath); 
            printf("exten: %s \n", get_filename_ext(request->http_uri)); 

            respond_all(connFd, newPath, parse_file_type(connFd,rootFolder,request)); 
        }
    }
    else if (strcasecmp(request->http_method, "Head") == 0) {
         
        if (request->http_uri[0] == '/') {

            sprintf(newPath, "%s%s", rootFolder, request->http_uri);

            printf("newPath: %s \n", newPath); 
            printf("exten: %s \n", get_filename_ext(request->http_uri)); 

            respond_head(connFd, newPath, parse_file_type(connFd,rootFolder,request)); 
        }
    }

    else if (strcasecmp(request->http_version, "HTTP/1.1") != 0) {
        
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
        return;
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
        return;
    }
}

void * do_work(void *pool) {

    for (;;) {

        threadpool *t_pool = (threadpool *) pool;
        //printf("numthread: %d \n", t_pool->thread_count);
        pthread_mutex_lock(&mutex); 
        int *ct_client; 
        ct_client = pop(); 
    
        if (ct_client == NULL) {

            pthread_cond_wait(&condition_variable, &mutex); 
            ct_client = pop();
            //printf("ct_client: %d \n", ct_client);   
        }
        pthread_mutex_unlock(&mutex); 

        if (ct_client != NULL) {
            
            serve_http(ct_client,rootFolder_glob); 
            int connFd = *((int*)ct_client);  
            close(connFd);

        }  
    }  
}

// void* conn_handler(void *args) {

//     struct survival_bag *context = (struct survival_bag *) args;
//     //pthread_detach(pthread_self());
//     serve_http(context->connFd,context->rootFolder);
//     return NULL; /* Nothing meaningful to return */
// }

void print_usage() {
    printf("Usage: ./icws --port <listenPort> --root <wwwRoot> --numThreads <numThreads> --timeout <timeout>  \n"); 
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

    //pthread_t thread_array[numThread]; 

    if (argc < 9) {
        print_usage(); 
    } 

    while ((option = getopt_long(argc,argv,"p:r:t:o", long_options, NULL)) != -1) {   
        
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
    //thread_array = (pthread_t*)malloc(sizeof(pthread_t)*numThread);
    for(int i = 0; i < pool->thread_count; i++){  // create thread accroding to numThread
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

        cf_client = (int *)malloc(sizeof(int)); 

        if (cf_client == NULL) { 
            fprintf(stderr, "fail to malloc!\n");
		    return NULL;
        } 

        *cf_client = context->connFd; 
        pthread_mutex_lock(&mutex); 
        push(cf_client); 
        pthread_cond_signal(&condition_variable); 
        pthread_mutex_unlock(&mutex);  

        char hostBuf[MAXBUF], svcBuf[MAXBUF];
        if (getnameinfo((SA *)&clientAddr, clientLen,
                        hostBuf, MAXBUF, svcBuf, MAXBUF, 0) == 0)
            printf("Connection from %s:%s\n", hostBuf, svcBuf);
        else
            printf("Connection from ?UNKNOWN?\n");
        
        free(context);
    }

    free(pool->thread_array);
    free(pool); 
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&condition_variable); 
    free(cf_client); 

    return 0;
}