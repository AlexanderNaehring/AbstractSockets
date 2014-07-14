/************************************
 *            ASLib.h               *
 *         AbstractSocket           *
 *  provides an abstract interface  *
 *      for socket communiction     *
 *      created: May 27, 2014       *
 *        Alexander Nähring         *
 ************************************/
 
#ifndef ASLIB_H_
#define ASLIB_H_

#define AS_VERSION 1
#define AS_LOG
#define AS_MAXPORT 65535
#define AS_BACKLOG 5
#define AS_BUFFLEN 1024
#define AS_IPv4 4
#define AS_IPv6 6
#define AS_IPunspec 0
#define AS_NAMELEN 128

#define AS_TypeShutdown 1
#define AS_TypeMessage 2
#define AS_TypeFile 3

/*
  AF_INET
  AF_INET6
  AF_UNSPEC
*/

//////////////////////////////
//        STRUCTURES        //
//////////////////////////////

typedef enum { false, true } bool;

typedef struct AS_MessageHeader_s { // header of each packet! afterwards -> payload
  int clientSource;           // -1: server
  int clientDestination;      // -1: server // -2: broadcast to all clients
  unsigned int payloadType;   // AS_PAYLOAD_xxx
  unsigned int payloadLength; // bytes
} AS_MessageHeader_t;

typedef struct AS_ClientEvent_s { // used for return from event function
  AS_MessageHeader_t *header;
  void* payload;
} AS_ClientEvent_t;

//////////////////////////////
//        FUNCTIONS         //
//////////////////////////////

void msecsleep(int msec); // waits for msec milliseconds
int AS_version();         // return AS version

int AS_ServerIsRunning(int port);       // returns 1 if an AS_Server is running in this process on this port, otherwise 0
int AS_ServerPrintRunning();            // prints a list of all running AS_Server in this process to stdout
int AS_ServerStart(int port, int IPv);  // start ASServer at specific port
                                        // IPv can be AS_IPv4, AS_IPv6 or AS_IPunspec
int AS_ServerStop(int port);            // stop ASServer if running

int AS_ClientConnect(char* host, char *port); // establish a connection to an AS_Server at [host]:port, returns connection id: cid
int AS_ClientDisconect(int cid);              // disconnects from an AS_Server previously connected with AS_ClientConnect
AS_ClientEvent_t* AS_ClientEvent(int conID);
int AS_ClientSendMessage(int conID, int recipient, char *message);

#endif