#define __PCSA_NET_
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define SUCCESS 0

int open_listenfd(char *port);
ssize_t read_line(int connFd, char *usrbuf, size_t maxlen);
void write_all(int connFd, char *buf, size_t len);
void write_logic(int data, int outputFd); 

//Header field
typedef struct
{
	char header_name[4096];
	char header_value[4096];
} Request_header;

//HTTP Request Header
typedef struct
{
	char http_version[50];
	char http_method[50];
	char http_uri[4096];
	Request_header *headers;
	int header_count;
} Request;

typedef struct poll_timeout{
	int timeout; 
} poll_t; 

Request* parse(char *buffer, int size,int socketFd);

// functions decalred in parser.y
int yyparse();
void set_parsing_options(char *buf, size_t i, Request *request);
