#include <err.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "queue.h"
#include <getopt.h>

#include <stdio.h>
#define __USE_XOPEN
#include <time.h>
#define BUF_SIZE 4096
#define BUF_MAX 32000
struct tm time_data;
#define OPTIONS "c:m:u"

/**
   Creates a socket for connecting to a server running on the same
   computer, listening on the specified port number.  Returns the
   socket file descriptor on succes.  On failure, returns -1 and sets
   errno appropriately.
 */
int create_client_socket(uint16_t port)
{
  int clientfd = socket(AF_INET, SOCK_STREAM, 0);
  if (clientfd < 0)
  {
    return -1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (connect(clientfd, (struct sockaddr *)&addr, sizeof addr))
  {
    return -1;
  }
  return clientfd;
}

/**
   Converts a string to an 16 bits unsigned integer.
   Returns 0 if the string is malformed or out of the range.
 */
uint16_t strtouint16(char number[])
{
  char *last;
  long num = strtol(number, &last, 10);
  if (num <= 0 || num > UINT16_MAX || *last != '\0')
  {
    return 0;
  }
  return num;
}

/**
   Creates a socket for listening for connections.
   Closes the program and prints an error message on error.
 */
int create_listen_socket(uint16_t port)
{
  struct sockaddr_in addr;
  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0)
  {
    err(EXIT_FAILURE, "socket error");
  }

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (bind(listenfd, (struct sockaddr *)&addr, sizeof addr) < 0)
  {
    err(EXIT_FAILURE, "bind error");
  }

  if (listen(listenfd, 500) < 0)
  {
    err(EXIT_FAILURE, "listen error");
  }

  return listenfd;
}

void clearBuffer(char *buffer)
{
  for (size_t i = 0; i < BUF_SIZE; i++)
  {
    buffer[i] = 0;
  }
}

void handle_connection(int connfd, int serverfd, struct entry *entries, int *entryCounter, int cacheSize, int mSize, int lruFlag)
{
  /* initialize variables */
  char *buffer = (char *)malloc(BUF_SIZE);
  if (!buffer)
  {
    dprintf(connfd, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal Server Error\n");
  }
  int savePoint = 0;
  int bytesRead = 0;
  int doneFlag = 0;
  int inRead = bytesRead + 1;

  /* Start receiving from connection into buffer and reading the buffer */
  clearBuffer(buffer);
  for (int i = 0; i < inRead; i++)
  {
    char parse[1];
    bytesRead = recv(connfd, parse, 1, 0);
    if (bytesRead == 0)
    {
      doneFlag = 1;
    }
    if (bytesRead == -1)
    {
      dprintf(connfd, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal Server Error\n");
    }
    strncat(buffer, parse, 1);
    inRead += bytesRead;
    if (buffer[i] == '\n')
    {
      savePoint = i;
      savePoint++;
      break;
    }
  }
  if (doneFlag == 1)
  { // check if any more request coming from connection
    close(connfd);
    free(buffer);
    return;
  }

  /* parsing for the command type and file name */
  char *tempstr = calloc(strlen(buffer) + 1, sizeof(char));
  if (!tempstr)
  {
    dprintf(connfd, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal Server Error\n");
  }
  strcpy(tempstr, buffer);
  char *token = strtok(tempstr, " ");
  char *command = token;
  token = strtok(NULL, " ");
  char *file = &token[1];
  token = strtok(NULL, "\n");
  char *http = token;

  /* check if command and file name is valid */
  if (strlen(command) == 0)
  {
    dprintf(connfd, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
    close(connfd);
    return;
  }
  for (unsigned long i = 0; i < strlen(command); i++)
  {
    if (isalpha(command[i]) == 0)
    {
      dprintf(connfd, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
      close(connfd);
      return;
    }
  }
  if (strlen(file) == 0)
  {
    dprintf(connfd, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
    close(connfd);
    return;
  }
  if (strlen(file) != 15)
  {
    dprintf(connfd, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
    close(connfd);
    return;
  }
  for (unsigned long i = 1; i < strlen(file); i++)
  {
    if (isalnum(file[i]) == 0)
    {
      dprintf(connfd, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
      close(connfd);
      return;
    }
  }
  char *httpCheck = strstr(http, "HTTP/1.1");
  if (!httpCheck)
  {
    dprintf(connfd, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
    close(connfd);
    return;
  }

  /* PUT REQUEST */
  if (strcmp(command, "PUT") == 0)
  {
    char *request = (char *)malloc(BUF_SIZE);
    char *serverBuffer = (char *)malloc(BUF_SIZE);
    char host[100];
    int len = -1;
    /* Continue receiving request until the \r\n\r\n before body */
    for (int i = (savePoint); i < inRead; i++)
    {
      char parse[1];
      bytesRead = recv(connfd, parse, 1, 0); //receiving 1 byte at a time
      strncat(buffer, parse, 1);
      inRead += bytesRead;
      if (buffer[i] == '\n')
      {
        char *hostCheck = NULL;
        char *pch = NULL;
        if (&buffer[savePoint] != NULL)
        {
          hostCheck = strstr(&buffer[savePoint], "Host:");
          pch = strstr(&buffer[savePoint], "Content-Length:"); //parsing for content length
        }
        if (pch)
        {
          sscanf(&buffer[savePoint], "Content-Length: %d", &len);
        }
        if (hostCheck)
        {
          sscanf(&buffer[savePoint], "Host: %s", host);
        }
        savePoint = i;
        savePoint++;
        if (buffer[i] == '\n' && buffer[i - 2] == '\n')
        {
          break;
        }
      }
    }
    sprintf(request, "%s /%s %s\r\nHost: %s\r\nContent-Length: %d\r\n\r\n", command, file, http, host, len);
    send(serverfd, request, strlen(request), 0);
    clearBuffer(buffer);
    if(len >= BUF_SIZE){
      buffer = (char*)realloc(buffer,len);
    }
    /* Read from body of request and write into file until the end */
    int numRead = 0;
    while(numRead != len){
      int val = recv(connfd, buffer+numRead, len-numRead,0);
      if(val == 0){
        break;
      }
      if(val == -1){
        dprintf(connfd, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal Server Error\n");
      }
      numRead += val;
    }
    /* Check if content length is valid */
    if (len == -1)
    {
      dprintf(connfd, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
      close(connfd);
      return;
    }
    /* Send request to Server */
    clearBuffer(serverBuffer);
    send(serverfd, buffer, len, 0);
    /* Get Response from Server */
    int size = 0;
    savePoint = 0;
    bytesRead = 0;
    inRead = bytesRead + 1;
    for (int i = 0; i < inRead; i++)
    {
      char parse[1];
      bytesRead = recv(serverfd, parse, 1, 0);
      strncat(serverBuffer, parse, 1);
      inRead += bytesRead;
      if (serverBuffer[i] == '\n')
      {
        char *responseContentSize = NULL;
        if (&serverBuffer[savePoint] != NULL)
        {
          responseContentSize = strstr(&serverBuffer[savePoint], "Content-Length:"); //parsing for content length
        }
        if (responseContentSize)
        {
          sscanf(&serverBuffer[savePoint], "Content-Length: %d", &size);
        }
        savePoint = i;
        savePoint++;
        if (serverBuffer[i] == '\n' && serverBuffer[i - 2] == '\n')
        {
          break;
        }
      }
    }
    bytesRead = recv(serverfd, &serverBuffer[savePoint], size, 0);
    /* Send buffer to client */
    dprintf(connfd, "%s", serverBuffer); // send file size to client
  }
  else if (strcmp(command, "HEAD") == 0)
  {
    /* Find the host */
    char host[100];
    for (int i = savePoint; i < inRead; i++)
    {
      char parse[1];
      bytesRead = recv(connfd, parse, 1, 0);
      strncat(buffer, parse, 1);
      inRead += bytesRead;
      if (buffer[i] == '\n')
      {
        char *hostCheck = NULL;
        if (&buffer[savePoint] != NULL)
        {
          hostCheck = strstr(&buffer[savePoint], "Host:");
        }
        if (hostCheck)
        {
          sscanf(&buffer[savePoint], "Host: %s", host);
          break;
        }
      }
    }
    /* Send request to Server */
    char *request = (char *)malloc(BUF_SIZE);
    char *serverBuffer = (char *)malloc(BUF_SIZE);
    clearBuffer(serverBuffer);
    sprintf(request, "%s /%s %s\r\nHost: %s\r\n\r\n", command, file, http, host);
    send(serverfd, request, strlen(request), 0);
    /* Get Response from Server */
    savePoint = 0;
    int doneServerFlag = 0;
    bytesRead = 0;
    inRead = bytesRead + 1;
    for (int i = 0; i < inRead; i++)
    {
      char parse[1];
      bytesRead = recv(serverfd, parse, 1, 0);
      if (bytesRead == 0)
      {
        doneServerFlag = 1;
      }
      strncat(serverBuffer, parse, 1);
      inRead += bytesRead;
      if (serverBuffer[i] == '\n')
      {
        savePoint = i;
        savePoint++;
        if (serverBuffer[i] == '\n' && serverBuffer[i - 2] == '\n')
        {
          break;
        }
      }
    }
    if (doneServerFlag == 1)
    { // check if any more request coming from connection
      close(serverfd);
      free(serverBuffer);
    }
    /* Send buffer to client */
    dprintf(connfd, "%s", serverBuffer); // send file size to client
    bytesRead = recv(connfd, buffer, BUF_SIZE, 0);
    // bytesRead = recv(serverfd,buffer,BUF_SIZE,0);
    free(tempstr);
    free(serverBuffer);
    free(request);
    /* GET REQUEST */
  }
  else if (strcmp(command, "GET") == 0)
  {
    /* Find the host */
    char host[100];
    for (int i = savePoint; i < inRead; i++)
    {
      char parse[1];
      bytesRead = recv(connfd, parse, 1, 0);
      strncat(buffer, parse, 1);
      inRead += bytesRead;
      if (buffer[i] == '\n')
      {
        char *hostCheck = NULL;
        if (&buffer[savePoint] != NULL)
        {
          hostCheck = strstr(&buffer[savePoint], "Host:");
        }
        if (hostCheck)
        {
          sscanf(&buffer[savePoint], "Host: %s", host);
          break;
        }
      }
    }
    /* Send request to Server */
    char *request = (char *)malloc(BUF_SIZE);
    char *serverBuffer = (char *)malloc(BUF_SIZE);
    clearBuffer(serverBuffer);
    time_t serverTimeModified;
    /* Check cache */
    int cacheFlag = -1;
    for (int i = 0; i < numberOfEntries(); i++)
    {
      if (strcmp(file, entries[i].filename) == 0)
      {
        cacheFlag = i;
        break;
      }
    }
    if (cacheFlag == -1)
    { //means we send get request to server and store in cache
      char timeModified[100];
      sprintf(request, "%s /%s %s\r\nHost: %s\r\n\r\n", command, file, http, host);
      send(serverfd, request, strlen(request), 0);
      /* Get Response from Server and Parse Time*/
      savePoint = 0;
      bytesRead = 0;
      inRead = bytesRead + 1;
      int size = -1;
      for (int i = 0; i < inRead; i++)
      {
        char parse[1];
        bytesRead = recv(serverfd, parse, 1, 0);
        strncat(serverBuffer, parse, 1);
        inRead += bytesRead;
        if (serverBuffer[i] == '\n')
        {
          char *timeCheck = NULL;
          char *responseContentSize = NULL;
          if (&serverBuffer[savePoint] != NULL)
          {
            timeCheck = strstr(&serverBuffer[savePoint], "Last-Modified:");
          }
          if (timeCheck)
          {
            strptime(&serverBuffer[savePoint], "Last-Modified: %a, %d %b %Y %T GMT", &time_data);
            strftime(timeModified, 100, "%D-%T\n", &time_data);
            serverTimeModified = mktime(&time_data);
          }
          if (&serverBuffer[savePoint] != NULL)
          {
            responseContentSize = strstr(&serverBuffer[savePoint], "Content-Length:"); //parsing for content length
          }
          if (responseContentSize)
          {
            sscanf(&serverBuffer[savePoint], "Content-Length: %d", &size);
          }
          savePoint = i;
          savePoint++;
        }
        if (serverBuffer[i] == '\n' && serverBuffer[i - 2] == '\n')
        {
          break;
        }
      }
      if (size == -1)
      {
        dprintf(connfd, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
        close(connfd);
        return;
      }
      int isWithinSize = 0;
      if (size > mSize)
      {
        isWithinSize = 1;
      }
      if(size >= BUF_SIZE){
        serverBuffer = (char*)realloc(serverBuffer,size+1);
      }
      dprintf(connfd, "%s", serverBuffer); // send client the content length first
      /* Error code check */
      int errorFlag = 0;
      char *error404Check = NULL;
      char *error403Check = NULL;
      error404Check = strstr(serverBuffer, "404");
      if (error404Check)
      {
        clearBuffer(serverBuffer); //clear buffer for reading
        bytesRead = recv(serverfd, serverBuffer, size, 0);
        send(connfd, serverBuffer, size, 0);
        errorFlag = 1;
      }
      error403Check = strstr(serverBuffer, "403");
      if (error403Check)
      {
        clearBuffer(serverBuffer); //clear buffer for reading
        bytesRead = recv(serverfd, serverBuffer, size, 0);
        send(connfd, serverBuffer, size, 0);
        errorFlag = 1;
      }
      if (errorFlag == 0)
      {
        clearBuffer(serverBuffer); //clear buffer for reading
        int numRead = 0;
        while(numRead != size){
          int val = recv(serverfd, serverBuffer+numRead, size-numRead,0);
          if(val == 0){
            break;
          }
          if(val == -1){
            dprintf(connfd, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal Server Error\n");
          }
          numRead += val;
        }
        send(connfd, serverBuffer, size, 0);
        // Insert LRU flag here
        if (isWithinSize == 0)
        {
          int n;
          struct entry addEntry;
          addEntry.id = *entryCounter;
          addEntry.filename = file;
          addEntry.size = size;
          addEntry.time = serverTimeModified;
          addEntry.contents = serverBuffer;
          n = insert(addEntry, entries);
          *entryCounter += 1;
          /* FIFO and cache is full */
          if (n == 0 && lruFlag == 0)
          {
            entries[0].id = *entryCounter;
            entries[0].filename = file;
            entries[0].size = size;
            entries[0].time = serverTimeModified;
            entries[0].contents = serverBuffer;
          }
          if (n == 0 && lruFlag == 1)
          {
            int lowest = entries[0].id;
            int entryIndex = 0;
            for (int i = 0; i < numberOfEntries(); i++)
            {
              if (lowest > entries[i].id)
              {
                lowest = entries[i].id;
                entryIndex = i;
              }
            }
            entries[entryIndex].id = *entryCounter;
            entries[entryIndex].filename = file;
            entries[entryIndex].size = size;
            entries[entryIndex].time = serverTimeModified;
            entries[entryIndex].contents = serverBuffer;
          }
        }
      }
    }
    if (cacheFlag >= 0)
    { // send a HEAD request to server
      /*If in cache*/
      char timeModified[100];
      int size = -1;
      sprintf(request, "HEAD /%s %s\r\nHost: %s\r\n\r\n", file, http, host);
      send(serverfd, request, strlen(request), 0);
      /* Get Response from Server and Parse Time*/
      savePoint = 0;
      bytesRead = 0;
      inRead = bytesRead + 1;
      for (int i = 0; i < inRead; i++)
      {
        char parse[1];
        bytesRead = recv(serverfd, parse, 1, 0);
        strncat(serverBuffer, parse, 1);
        inRead += bytesRead;
        if (serverBuffer[i] == '\n')
        {
          char *timeCheck = NULL;
          char *pch = NULL;
          if (&serverBuffer[savePoint] != NULL)
          {
            timeCheck = strstr(&serverBuffer[savePoint], "Last-Modified:");
            pch = strstr(&serverBuffer[savePoint], "Content-Length:");
          }
          if (pch)
          {
            sscanf(&serverBuffer[savePoint], "Content-Length: %d", &size);
          }
          if (timeCheck)
          {
            strptime(&serverBuffer[savePoint], "Last-Modified: %a, %d %b %Y %T GMT", &time_data);
            strftime(timeModified, 100, "%D-%T\n", &time_data);
            serverTimeModified = mktime(&time_data);
          }
          savePoint = i;
          savePoint++;
        }
        if (serverBuffer[i] == '\n' && serverBuffer[i - 2] == '\n')
        {
          break;
        }
      }
      if (size == -1)
      {
        dprintf(connfd, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
        close(connfd);
        return;
      }
      if(size >= BUF_SIZE)
      {
        serverBuffer = (char*)realloc(serverBuffer,size+1);
      }
      if (entries[cacheFlag].time >= serverTimeModified)
      {
        dprintf(connfd, "%s", serverBuffer); // send client the content length first
        send(connfd, entries[cacheFlag].contents, size, 0);
        *entryCounter += 1;
        entries[cacheFlag].id = *entryCounter;
      }
      else
      {
        sprintf(request, "%s /%s %s\r\nHost: %s\r\n\r\n", command, file, http, host);
        send(serverfd, request, strlen(request), 0);
        /* Get response from server */
        clearBuffer(serverBuffer);
        savePoint = 0;
        bytesRead = 0;
        inRead = bytesRead + 1;
        for (int i = 0; i < inRead; i++)
        {
          char parse[1];
          bytesRead = recv(serverfd, parse, 1, 0);
          strncat(serverBuffer, parse, 1);
          inRead += bytesRead;
          if (serverBuffer[i] == '\n')
          {
            char *timeCheck = NULL;
            if (&serverBuffer[savePoint] != NULL)
            {
              timeCheck = strstr(&serverBuffer[savePoint], "Last-Modified:");
            }
            if (timeCheck)
            {
              strptime(&serverBuffer[savePoint], "Last-Modified: %a, %d %b %Y %T GMT", &time_data);
              strftime(timeModified, 100, "%D-%T\n", &time_data);
              serverTimeModified = mktime(&time_data);
            }
            savePoint = i;
            savePoint++;
          }
          if (serverBuffer[i] == '\n' && serverBuffer[i - 2] == '\n')
          {
            break;
          }
        }
        dprintf(connfd, "%s", serverBuffer); // send client the content length first
        clearBuffer(serverBuffer);           //clear buffer for reading
        int numRead = 0;
        while(numRead != size){
          int val = recv(serverfd, serverBuffer+numRead, size-numRead,0);
          if(val == 0){
            break;
          }
          if(val == -1){
            dprintf(connfd, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal Server Error\n");
          }
          numRead += val;
        }
        send(connfd, serverBuffer, size, 0);
        *entryCounter += 1;
        entries[cacheFlag].id = *entryCounter;
        entries[cacheFlag].filename = file;
        entries[cacheFlag].size = size;
        entries[cacheFlag].time = serverTimeModified;
        entries[cacheFlag].contents = serverBuffer;
      }
    }
    /*
      If cache date >= response date, check content and send back to client
      else send a get a request to server and replace at index cacheFlag.
      */
    bytesRead = recv(connfd, buffer, BUF_SIZE, 0);
  }
  else
  { // if command and file name is formatted but not PUT, GET, HEAD
    dprintf(connfd, "HTTP/1.1 501 Not Implemented\r\nContent-Length: 16\r\n\r\nNot Implemented\n");
    free(tempstr);
  }
  handle_connection(connfd, serverfd, entries, entryCounter, cacheSize, mSize, lruFlag);
}

int main(int argc, char *argv[])
{
  int listenfd, serverfd, opt;
  int m = 65536;
  int c = 3;
  int lruFlag = 0;
  uint16_t client_port, server_port;

  // You will have to modify this and add your own argument parsing
  while ((opt = getopt(argc, argv, OPTIONS)) != -1)
  {
    switch (opt)
    {
    case 'c':
      for (unsigned long i = 0; i < strlen(optarg); i++)
      {
        if (isdigit(optarg[i]) == 0)
        {
          errx(EXIT_FAILURE, "Expected numeric digits after option\n");
          exit(EXIT_FAILURE);
        }
      }
      c = atoi(optarg);
      break;
    case 'm':
      for (unsigned long i = 0; i < strlen(optarg); i++)
      {
        if (isdigit(optarg[i]) == 0)
        {
          errx(EXIT_FAILURE, "Expected numeric digits after option\n");
          exit(EXIT_FAILURE);
        }
      }
      m = atoi(optarg);
      break;
    case 'u':
      lruFlag = 1;
      break;
    default: /* '?' */
      errx(EXIT_FAILURE, "Usage: client port number server port number -m integer -c integer -u\n");
      exit(EXIT_FAILURE);
    }
  }
  if (optind >= argc)
  {
    errx(EXIT_FAILURE, "Expected argument after options\n");
    exit(EXIT_FAILURE);
  }
  client_port = strtouint16(argv[optind]);
  if (client_port == 0)
  {
    errx(EXIT_FAILURE, "invalid client port number: %s", argv[optind]);
  }
  optind++;
  if (argv[optind] == (void *)'\0')
  {
    errx(EXIT_FAILURE, "Usage: client port number server port number -m integer -c integer -u\n");
    exit(EXIT_FAILURE);
  }
  server_port = strtouint16(argv[optind]);
  if (server_port == 0)
  {
    errx(EXIT_FAILURE, "invalid server port number: %s", argv[optind]);
  }
  listenfd = create_listen_socket(client_port);
  serverfd = create_client_socket(server_port);
  struct entry *entries = createList(c);
  int entryCounter = 0;
  setenv("TZ", "UTC", 1);
  tzset();

  while (1)
  {
    int connfd = accept(listenfd, NULL, NULL);
    if (connfd < 0)
    {
      warn("accept error");
      continue;
    }
    handle_connection(connfd, serverfd, entries, &entryCounter, c, m, lruFlag);
  }
  return EXIT_SUCCESS;
}
