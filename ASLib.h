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

#define AS_PORT 20144
#define AS_PORT_STR "20144"
/*
  AF_INET
  AF_INET6
  AF_UNSPEC
*/

void msecsleep(int msec); // waits for msec milliseconds
int AS_version();         // return AS version

int AS_ServerIsRunning(int port);       // returns 1 if an AS_Server is running in this process on this port, otherwise 0
int AS_ServerPrintRunning();            // prints a list of all running AS_Server in this process to stdout
int AS_ServerStart(int port, int IPv);  // start ASServer at specific port
                                        // IPv can be AS_IPv4, AS_IPv6 or AS_IPunspec
int AS_ServerStop(int port);            // stop ASServer if running


int AS_ClientConnect(char* host, char *port); // establish a connection to an AS_Server at [host]:port, returns connection id: cid
int AS_ClientDisconect(int cid);              // disconnects from an AS_Server previously connected with AS_ClientConnect

#endif
