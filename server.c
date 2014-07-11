/************************************
 *           server.c               *
 *        Server using ASLib        *
 *      created: May 27, 2014       *
 *        Alexander Nähring         *
 ************************************/
 


#include <stdio.h>
#include "ASLib.h"


int main(void)  {
  printf("Server: AS_version() = %d\n",AS_version());
  
  AS_ServerStart(AS_PORT, AS_IPv6);
  AS_ServerPrintRunning();
  
  
  printf("press enter to close server\n");
  
  getchar();  
  
  AS_ServerStop(AS_PORT);
}
