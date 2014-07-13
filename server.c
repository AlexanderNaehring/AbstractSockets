/************************************
 *           server.c               *
 *        Server using ASLib        *
 *      created: May 27, 2014       *
 *        Alexander Nähring         *
 ************************************/
 


#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/select.h>
#include <string.h>
#include "ASLib.h"

int main(void)  {
  char *buffer;
  size_t size;
  
  AS_version(); // not used here
  
  AS_ServerStart(20144, AS_IPv6);
  AS_ServerPrintRunning();
  
  while(1)  {
    buffer = NULL;
    getline(&buffer, &size, stdin); // wait for input and read line
    
    if(strstr(buffer, "help") == buffer || strstr(buffer, "h") == buffer) {
      printf("AS example server - help:\n");
      printf("  help, h:    show this help\n");
      printf("  quit, q:    quit server\n");
      printf("  server, s:  print list of running server in this process\n");
      //printf("  clients, c: print list of all connected clients for all running server in this process\n");
    }
    
    if(strstr(buffer, "server") == buffer || strstr(buffer, "s") == buffer) {
      AS_ServerPrintRunning();
    }
    
    if(strstr(buffer, "quit") == buffer || strstr(buffer, "q") == buffer) { 
      free(buffer);
      break;  // break while loop
    }
    free(buffer);
  }
  AS_ServerStop(20144);
}