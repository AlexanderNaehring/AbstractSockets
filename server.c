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
  int port;
  
  AS_version(); // not used here
  
  AS_ServerStart(20144, AS_IPv6);
  AS_ServerPrintRunning();
  
  while(1)  {
    buffer = NULL;
    getline(&buffer, &size, stdin); // wait for input and read line
    
    if(strstr(buffer, "help") == buffer || strstr(buffer, "h") == buffer) {
      printf("AS example server - help:\n");
      printf("  help,  h:  show this help\n");
      printf("  quit,  q:  quit program\n");
      printf("  print, p:  print list of running server in this process\n");
      printf("  start <port>, s <port>:  start a new server\n");
      printf("  stop <port>,  t <port>:  stop a running server\n");
      //printf("  clients, c: print list of all connected clients for all running server in this process\n");
    }
    
    if(strstr(buffer, "print") == buffer || strstr(buffer, "p") == buffer) {
      AS_ServerPrintRunning();
    }
    
    if(strstr(buffer, "quit") == buffer || strstr(buffer, "q") == buffer) { 
      free(buffer);
      break;  // break while loop
    }   
        
    port = -1; sscanf(buffer, "start %d\n", &port);
    if(port > -1) AS_ServerStart(port, AS_IPv6);
    port = -1; sscanf(buffer, "s %d\n", &port);
    if(port > -1) AS_ServerStart(port, AS_IPv6);
    port = -1; sscanf(buffer, "stop %d\n", &port);
    if(port > -1) AS_ServerStop(port);
    port = -1; sscanf(buffer, "t %d\n", &port);
    if(port > -1) AS_ServerStop(port);
    
    free(buffer);
  }
  AS_ServerStop(0); // 0 = all
}
