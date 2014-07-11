/************************************
 *            client.c              *
 *        Client using ASLib        *
 *      created: May 27, 2014       *
 *        Alexander Nähring         *
 ************************************/
 


#include <stdio.h>
#include "ASLib.h"



int main(void)  {
  printf("Client: AS_version() = %d\n",AS_version());
  
  printf("connecting to localhost\n");
  int conID;
  conID = AS_ClientConnect("localhost",AS_PORT_STR);
  
  printf("press enter to disconnect client\n");
  getchar();  
  
  AS_ClientDisconnect(conID);
}
