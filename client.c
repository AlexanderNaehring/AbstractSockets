/************************************
 *            client.c              *
 *        Client using ASLib        *
 *      created: May 27, 2014       *
 *        Alexander Nähring         *
 ************************************/
 


#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/select.h>
#include <string.h>
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
  int conID, len;
  char *buffer;
  char c;
  size_t size;
  AS_ClientEvent_t *event;
  
  AS_version(); // unimportant
  printf("connecting to localhost... ");
  if(conID = AS_ClientConnect("localhost", "20144"))  {
    printf("connected\n");
  } else  {
    printf("error\n");
    return -1;
  }
  
  while(1) {
    msecsleep(1); // sleep 1 millisecond in order to release CPU
    
    // main program loop
    // check with AS_ClientEvent(conID) for incomming messages
    // in case of message -> handle it
    // check for user input 
    // e.g. sending messages, asking for connected users, etc
    
    event = AS_ClientEvent(conID); // query client events
    // Events have to be handled before calling this function again
    // Everytime this function is called, the last event will be discarded
    
    if(event != NULL)
      switch(event->header->payloadType)  {
        case AS_TypeMessage:
          printf("Received message from client %d: %s", event->header->clientSource, (char *)(event->payload));
          break;
        case AS_TypeShutdown:
          printf("server shutdown - close socket now\n");
          return;
      }
    
    if(isSTDIN()) { // some input on stdin
      //printf("stdin\n");
      buffer = NULL;
      //printf("getline: ");
      getline(&buffer, &size, stdin);
      //printf("buffer = %s\n", buffer);
      if(strstr(buffer, "q") == buffer) { // q -> exit program
        printf("client will close now!\n");
        free(buffer);
        break;
      }
      if(strstr(buffer, "send ") == buffer) {  // send message
        // strstr returns NULL pointer if string is NOT found
        // if "send" is found, strstr returns pointer to occurance
        // therefore -> check if send is found at location of buffer pointer
        
        // message should be located behind "send"
        // +5 for shifting pointer behind "send "
        // now, in buffer2 themessage is located
        // recepient = -2 for broadcast
        AS_ClientSendMessage(conID, -2, buffer + 5);
      }
      free(buffer);
    } // endif isSTDIN()
  } // while
  
  AS_ClientDisconnect(conID);
  return 0;
}
