#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include "parse.h"
#define MAXBUF 8192

typedef struct sockaddr SA;

struct survival_bag {
    struct sockaddr_storage clientAddr;
    int connFd;
    char *rootFolder;
};

// void write_logic(int data, int outputFd)
// {
//     ssize_t bytesRead;
//     char buf[MAXBUF];

//     while ((bytesRead = read(data, buf, MAXBUF)) > 0)
//     {
//         if (bytesRead < 0) {
            
//             fprintf(stderr, "ERROR writing, meh\n");
//             break;
//         }
//         ssize_t numToWrite = bytesRead;
//         char *writeBuf = buf;
//         while (numToWrite > 0)
//         {
//             ssize_t numWritten = write(outputFd, writeBuf, numToWrite);
//             if (numWritten < 0)
//             {
//                 fprintf(stderr, "ERROR writing, meh\n");
//                 break;
//             }
//             numToWrite -= numWritten;
//             writeBuf += numWritten;
//         }
//     }
//     printf("DEBUG: Connection closed\n");
// }


// void respond_all(int connFd, char *uri, char *mime)
// {
//     char buf[MAXBUF];
//     int uriFd = open(uri, O_RDONLY);
    
//     char *msg = "404 Not Found";
//     if (uriFd < 0)
//     {
//         sprintf(buf,
//                 "HTTP/1.1 404 Not Found\r\n"
//                 "Server: Micro\r\n"
//                 "Connection: close\r\n\r\n");
//         write_all(connFd, buf, strlen(buf));
//         write_all(connFd, msg, strlen(msg));
//         return;
//     }
//     struct stat fstatbuf;
//     fstat(uriFd, &fstatbuf);
//     sprintf(buf,
//             "HTTP/1.1 200 OK\r\n"
//             "Server: Micro\r\n"
//             "Connection: close\r\n"
//             "Content-length: %lu\r\n"
//             "Content-type: %s\r\n\r\n",
//             fstatbuf.st_size, mime);
//     write_all(connFd, buf, strlen(buf));  // write header into  connFd
//     write_logic(uriFd, connFd); // send the content data into connFd
// }


void serve_http(int connFd,char *rootFolder)
{
    
    char buf[MAXBUF];
    int readRet = read(connFd,buf,MAXBUF);
    Request *request = parse(buf,readRet,connFd);
    printf("Http Method %s\n",request->http_method);
    printf("Http Version %s\n",request->http_version);
    printf("Http Uri %s\n",request->http_uri);

    // if (!read_line(connFd, buf, MAXBUF))
    //     return; /* Quit if we can't read the first line */
    // /* [METHOD] [URI] [HTTPVER] */
    // char method[MAXBUF], uri[MAXBUF], httpVer[MAXBUF];
    // sscanf(buf, "%s %s %s", method, uri, httpVer);
    // char newPath[80];
    // if (strcasecmp(method, "GET") == 0)
    // {
    //     if (uri[0] == '/')
    //     {
    //         sprintf(newPath, "%s%s", rootFolder, uri);
    //         if (strstr(uri, "html") != NULL)
    //         {
    //             respond_all(connFd, newPath, "text/html");
    //         }
    //         else if (strstr(uri, "jpg") != NULL || strstr(uri, "jpeg") != NULL)
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

int main(int argc, char *argv[])
{
    int listenFd = open_listenfd(argv[1]);
    char *rootFolder = argv[2];
    for (;;)
    {
        struct sockaddr_storage clientAddr;
        socklen_t clientLen = sizeof(struct sockaddr_storage);
        //pthread_t threadInfo;

        int connFd = accept(listenFd, (SA *)&clientAddr, &clientLen);
        printf("connFd: %d", connFd); 
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

        //pthread_create(&threadInfo, NULL, conn_handler, (void *) context);
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
