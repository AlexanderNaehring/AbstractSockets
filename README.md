AbstractSockets
===============

Abstraction Layer for Using Sockets in C-Programs
Plus Implementation of a Scalable Multi-Party Chat Program and File Transfer

My proposal is to program a library (.h / .c file) which will present the programmer with abstract functions for communication over sockets. The library will handle the connections, clients and server internally and provide the programmer with an easy-to-use interface for common types of communication tasks like sending messages, transferring files, etc.
The library will handle the following things internally:
* Starting and supervising running server
* Establish and administer connections to already running servers
* Maintain connections, handle loss of connection appropriate
* Handle incoming connections
* Use own application layer protocol for
  * administrative overhead
  * sending ASCII (human readable data, e.g. messages)
  * sending binary files (TODO)
    * unlimited size
    * handle file transfer
    * file transfer should not block other communication

__Server functionality:__
```c
int AS_ServerIsRunning(int port);       // returns 1 if an AS_Server is running in this process on this port, otherwise 0
int AS_ServerPrintRunning();            // prints a list of all running AS_Server in this process to stdout
int AS_ServerStart(int port, int IPv);  // start ASServer at specific port
                                        // IPv can be AS_IPv4, AS_IPv6 or AS_IPunspec
int AS_ServerStop(int port);            // stop ASServer if running
```
__Client functionality:__
```c
int AS_ClientConnect(char* host, char *port); // establish a connection to an AS_Server at [host]:port, returns connection id: cid
int AS_ClientDisconect(int cid);              // disconnects from an AS_Server previously connected with AS_ClientConnect
AS_ClientEvent_t* AS_ClientEvent(int conID);
int AS_ClientSendMessage(int conID, int recipient, char *message);
int AS_ClientConnect(char* host, char *port); // establish a connection to an AS_Server at [host]:port, returns connection id: cid
int AS_ClientDisconect(int cid);              // disconnects from an AS_Server previously connected with AS_ClientConnect
AS_ClientEvent_t* AS_ClientEvent(int conID);
int AS_ClientSendMessage(int conID, int recipient, char *message);
```
