/************************************
 *            client.c              *
 *        Client using ASLib        *
 *      created: May 27, 2014       *
 *        Alexander Nähring         *
 ************************************/
 


#include <stdio.h>
#include "ASLib.h"

int isSTDIN() { // 0: timeout (no input), >0: stdin has input, -1: error
  struct timeval tv;
  tv.tv_sec = 0;  // zero timeeout (just check if input and return if not)
  tv.tv_usec = 0;
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(0, &fds);  // only stdin as input in select()
  return select(1, &fds, NULL, NULL, &tv);
  // directly return value from select()
}

int main(void)  {
  int conID;
  
  AS_version(); // unimportant
  printf("connecting to localhost... ");
  if(conID = AS_ClientConnect("localhost", AS_PORT_STR))  {
    printf("connected\n");
  } else  {
    printf("error\n");
    return -1;
  }
  
  while(true) {
    // main program loop
    // check with AS_ClientEvent(conID) for incomming messages
    // in case of message -> handle it
    // check for user input 
    // e.g. sending messages, asking for connected users, etc
    
    AS_ClientEvent = AS_ClientEvent(conID); // query client events
    // Events have to be handled before calling this function again
    // Everytime this function is called, the last event will be discarded
    // AS_ClientMessageLength will contain number of bytes to be read
    
    switch(AS_ClientEvent)  {
      case -1:  // error
        fprintf(stderr, "AS_ClientEvent error");
        break;
      case AS_ClientEventReceiveMessage:
        AS_Client
      default 0: // no event or not implemented
        break;
    }
    
  }
  
  AS_ClientDisconnect(conID);
  return 0;
}
