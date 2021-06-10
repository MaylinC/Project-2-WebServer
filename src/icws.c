#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <getopt.h>
#include <time.h>
#include "parse.h"
#define MAXBUF 8192

typedef struct sockaddr SA;

struct survival_bag {
    struct sockaddr_storage clientAddr;
    int connFd;
    char *rootFolder;
};

static struct option long_options[] =   {

    {"port", required_argument, NULL, 'p'},
    {"root", required_argument, NULL, 'r'},
    {NULL, 0, NULL, 0}
};


char *local_time() {

    char *date_time; 
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    date_time = asctime(tm); 
    return date_time;

}

void write_logic(int data, int outputFd)
{
    ssize_t bytesRead;
    char buf[MAXBUF];

    while ((bytesRead = read(data, buf, MAXBUF)) > 0)
    {
        if (bytesRead < 0) {
            
            fprintf(stderr, "ERROR writing, meh\n");
            break;
        }
        ssize_t numToWrite = bytesRead;
        char *writeBuf = buf;
        while (numToWrite > 0)
        {
            ssize_t numWritten = write(outputFd, writeBuf, numToWrite);
            if (numWritten < 0)
            {
                fprintf(stderr, "ERROR writing, meh\n");
                break;
            }
            numToWrite -= numWritten;
            writeBuf += numWritten;
        }
    }
    printf("DEBUG: Connection closed\n");
}

void respond_head(int connFd, char *uri, char *mime) { 

    char buf[MAXBUF];
    int uriFd = open(uri, O_RDONLY);

    if (uriFd < 0 || mime == NULL)
    {
        char *msg = "404 Not Found";
        sprintf(buf,
                "HTTP/1.1 404 Not Found\r\n"
                "Date: %s\r\n"
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
            "Date: %s\r\n"
            "Server: icws\r\n"
            "Connection: close\r\n"
            "Content-length: %lu\r\n"
            "Content-type: %s\r\n\r\n",
            local_time(),fstatbuf.st_size, mime);
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
                "Date: %s\r\n"
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
            "Date: %s\r\n"
            "Server: icws\r\n"
            "Connection: close\r\n"
            "Content-length: %lu\r\n"
            "Content-type: %s\r\n\r\n",
            local_time(),fstatbuf.st_size, mime);
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

void serve_http(int connFd,char *rootFolder)
{
    
    char buf[MAXBUF];
    char buffer[MAXBUF]; 
    char head[MAXBUF];
    char newPath[80];
    int readRet; 
    int sizeRat = 0; 
    while((readRet = read_line(connFd, buf, MAXBUF))>0) {
        sizeRat+=readRet; 
        strcat(buffer, buf); 
        if (!strcmp(buf,"\r\n")) {
            break; 
        }
    }
    if (readRet < 0) {
	    printf("Failed read\n");
	    return;
	}

    Request *request = parse(buffer,sizeRat,connFd);
    
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
                "Connection: close\r\n\r\n",local_time());
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

void* conn_handler(void *args) {

    struct survival_bag *context = (struct survival_bag *) args;
    //pthread_detach(pthread_self());
    serve_http(context->connFd,context->rootFolder);
    close(context->connFd);
    free(context); /* Done, get rid of our survival bag */
    return NULL; /* Nothing meaningful to return */
}

void print_usage() {
    printf("Usage: ./icws --port <listenPort> --root <wwwRoot> \n"); 
    exit(1); 
}

int main(int argc, char *argv[])
{
    int option; 
    int listenFd;
    char *rootFolder;

    if (argc < 5) {
        print_usage(); 
    } 

    while ((option = getopt_long(argc,argv,"p:r:", long_options, NULL)) != -1) {   
        
        switch (option) {

            case 'p':
            printf("port: %s \n", optarg);
            listenFd = open_listenfd(optarg);
            break;

            case 'r':
            //rootFolder = (char*)malloc(sizeof(char) * sizeof(optarg));
            //memcpy(rootFolder, optarg, strlen(optarg));
            rootFolder = optarg; 
            printf("root: %s \n", rootFolder); 
            break;

            default: 
            exit(1); 
        } 
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

        context->connFd = connFd;
        context->rootFolder = rootFolder;
        memcpy(&context->clientAddr, &clientAddr, sizeof(struct sockaddr_storage));

        conn_handler((void *) context); 

        char hostBuf[MAXBUF], svcBuf[MAXBUF];
        if (getnameinfo((SA *)&clientAddr, clientLen,
                        hostBuf, MAXBUF, svcBuf, MAXBUF, 0) == 0)
            printf("Connection from %s:%s\n", hostBuf, svcBuf);
        else
            printf("Connection from ?UNKNOWN?\n");
 
    }

    return 0;
}
