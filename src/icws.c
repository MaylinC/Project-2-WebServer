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


void respond_all(int connFd, char *uri, char *mime)
{
    char buf[MAXBUF];
    int uriFd = open(uri, O_RDONLY);
    
    char *msg = "404 Not Found";
    if (uriFd < 0)
    {
        sprintf(buf,
                "HTTP/1.1 404 Not Found\r\n"
                "Server: Micro\r\n"
                "Connection: close\r\n\r\n");
        write_all(connFd, buf, strlen(buf));
        write_all(connFd, msg, strlen(msg));
        return;
    }
    struct stat fstatbuf;
    fstat(uriFd, &fstatbuf);
    sprintf(buf,
            "HTTP/1.1 200 OK\r\n"
            "Server: Micro\r\n"
            "Connection: close\r\n"
            "Content-length: %lu\r\n"
            "Content-type: %s\r\n\r\n",
            fstatbuf.st_size, mime);
    write_all(connFd, buf, strlen(buf));  // write header into  connFd
    write_logic(uriFd, connFd); // send the content data into connFd
}


void serve_http(int connFd,char *rootFolder)
{
    
    char buf[MAXBUF];


    // char newPath[80];
    // if (strcasecmp(request->http_method, "GET") == 0)
    // {
    //     if (request->http_uri[0] == '/')
    //     {
    //         sprintf(newPath, "%s%s", rootFolder, request->http_uri);
    //         if (strstr(request->http_uri, "html") != NULL)
    //         {
    //             respond_all(connFd, newPath, "text/html");
    //         }
    //         else if (strstr(request->http_uri, "jpg") != NULL || strstr(request->http_uri, "jpeg") != NULL)
    //         {
    //             respond_all(connFd, newPath, "image/jpeg");
    //         }
    //         else
    //         {
    //             respond_all(connFd, newPath, NULL);
    //         }
    //     }
    // }
    // else
    // {
    //     // respond_all(connFd, newPath, NULL);
    //     printf("LOG: Unknown request\n");
    // }
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

        // int fd_in = open(rootFolder, O_RDONLY);
        // int index;

        char buf[MAXBUF];

	    // if (fd_in < 0) {
		//     printf("Failed to open the file\n");
		//     return 0;
	    // }

        int readRet = read(connFd,buf,MAXBUF); 

        for(int i = 0; i < readRet; i++) {
            printf("%c", buf[i]); 
        }

        Request *request = parse(buf,readRet,connFd);

        printf("Http Method %s\n",request->http_method);
        printf("Http Version %s\n",request->http_version);
        printf("Http Uri %s\n",request->http_uri);

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
