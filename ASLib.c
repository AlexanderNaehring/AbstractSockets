/************************************
 *            ASLib.c               *
 *         AbstractSocket           *
 *  provides an abstract interface  *
 *      for socket communiction     *
 *      created: May 27, 2014       *
 *        Alexander Nähring         *
 ************************************/

#include "ASLib.h"

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#include <fcntl.h>  // non blocking

//////////////////////////////
//        STRUCTURES        //
//////////////////////////////

typedef struct AS_Server_s {  // server side: running servers
  int port;
  int running;
  int stop;
  int error;
  int IPv;
  pthread_t* thread;
  
  struct AS_Server_s *next;
} AS_Server_t;

typedef struct AS_ConnectedClients_s  { // server side: connected clients
  int socket;
  //struct sockaddr_storage sockaddr;
  char name[AS_NAMELEN];
  
  struct AS_ConnectedClients_s *next;
} AS_ConnectedClients_t;

typedef struct AS_Connections_s  {  // client side: outgoing connections
  int conID;
  
  struct AS_Connections_s *next;
} AS_Connections_t;

//////////////////////////////
//        VARIABLES         //
//////////////////////////////

int AS_initialized = 0;               // test if initialization is done
AS_Server_t* AS_ServerList;           // server side: global server list (linked list)
AS_Connections_t* AS_ConnectionList;  // client side: global connection list (linked list)

//////////////////////////////
//    SUPPORT FUNCTIONS     //
//////////////////////////////

void msecsleep(int msec) {
  struct timespec time;
  time.tv_sec = msec/1000;
  time.tv_nsec = (msec % 1000)*1000*1000;
  nanosleep(&time , &time);
}

//////////////////////////////
//        FUNCTIONS         //
//////////////////////////////

int AS_init() {
  if(!AS_initialized) {
    AS_ServerList = calloc(1, sizeof(AS_Server_t));
    AS_ConnectionList = calloc(1, sizeof(AS_Connections_t));
    AS_initialized = 1;
  }
}

int AS_version() {
  return AS_VERSION;
}

int AS_sendAll(int sock, void *buf, int len)  { // replaces send(), sends in multiple steps if necessary
  //fprintf(stderr, "send %d bytes to %d\n", len, sock);
  int total = 0;        // bytes sent
  int bytesleft = len;  // bytes left
  int n;                // bytes send per call
  while(total < len) {
    n = send(sock, buf+total, bytesleft, 0);
    if (n == -1) { break; } // error
    total += n;
    bytesleft -= n;
  }
  return total; // return -1 on failure, 0 on success
} 

int AS_receiveAll(int sock, void *buf, int len)  {
  int total = 0;        // bytes received
  int bytesleft = len;  // bytes left
  int n;                // bytes received per call
  while(total < len) {
    n = recv(sock, buf+total, bytesleft, 0);
    if (n == -1) { break; } // error
    total += n;
    bytesleft -= n;
  }
  return total; // return -1 on failure, 0 on success
}

//////////////////////////////
//          SERVER          //
//////////////////////////////

int AS_ServerIsRunning(int port)  {
  if(!AS_initialized) AS_init();
  
  AS_Server_t* server = AS_ServerList;
  while(server->next != NULL) {
    server = server->next;
    if(server->port == port)  
      return 1;
  }
  return 0;
}

int AS_ServerPrintRunning() {
  if(!AS_initialized) AS_init();
  
  int count = 0;
  printf("All running AS_Server in this process:\n");
  AS_Server_t* server = AS_ServerList;
  while(server->next != NULL) {
    count++;
    server = server->next;
    printf("  server on port %d: running = %d, stop = %d\n", server->port, server->running, server->stop);
  }
  if(!count)
    printf("  no AS_Server running in this process\n");
  return 0;
}

void* AS_ServerThread(void *arg) {
  AS_Server_t* server = arg;
  printf("AS_ServerThread(%d)\n", server->port);
  
  // start server now
  struct addrinfo *ai_hints, *ai_res, *ai_p;
  int rv, sock_server, sockmax, i, sock_remote;
  char portstr[6];
  char ipstr[INET6_ADDRSTRLEN]; 
  fd_set fds_master, fds_read;
  struct timeval timeout;
  struct sockaddr_storage sockaddr_remote; // IP agnostic instead of using sockaddr_in
  socklen_t sockaddr_size;
  AS_MessageHeader_t *header; // message header pointer
  AS_ConnectedClients_t *newClient;  // adding new client
  AS_ConnectedClients_t *AS_ConnectedClientList;  // root element
  AS_ConnectedClients_t *client, *lastClient; // iteration elements
  char *buffer;
  char *payload;
  int len;
  
  ai_hints = calloc(1, sizeof(struct addrinfo));
  
  ai_hints->ai_socktype = SOCK_STREAM;  // TCP
  ai_hints->ai_flags    = AI_PASSIVE;   // fill in the IP for me please
  // AddressFamily:
  switch(server->IPv) {
    case AS_IPv4:
      ai_hints->ai_family = AF_INET;
      break;
    case AS_IPv6:
      ai_hints->ai_family = AF_INET6;
      break;
    case AS_IPunspec:
    default:
      ai_hints->ai_family = AF_UNSPEC;
      break;
  }
  snprintf(portstr, 6, "%d", server->port);  // int to string
  if((rv = getaddrinfo(NULL, portstr, ai_hints, &ai_res)) != 0) {
    fprintf(stderr, "error: getaddrinfo: %s\n", gai_strerror(rv));
    server->error = 1;
    return;
  }
  // loop through all the results and bind to the first working socket
  for(ai_p = ai_res; ai_p != NULL; ai_p = ai_p->ai_next) {  // struct addrinfo: *serverinfo, *p!!!!
    // try to open socket
    if((sock_server = socket(ai_p->ai_family, ai_p->ai_socktype, ai_p->ai_protocol)) < 0)  {
      perror("error: socket:");
      continue; // if fails -> try next;
    }
    // try to bind socket to port
    if(bind(sock_server, ai_p->ai_addr, ai_p->ai_addrlen) < 0) {
      close(sock_server);  // if fails: close socket again
      perror("error: bind:");
      continue; // and try next;
    }
    // if this point is reached, bind worked!
    // socket is now operational!
    // socket connected!
    // print socket information (port and IP version)
    inet_ntop(ai_p->ai_family, &(ai_p->ai_addr), ipstr, sizeof(ipstr));
    printf("server running at [%s]:%s\n",ipstr,portstr);
    break;
  }
  if(ai_p == NULL) {  // iterated through complete list without binding
    fprintf(stderr, "error: failed to bind server to port %s\n", portstr);
    server->error = 1;
    return;
  }
  // no more need for servinfo, listening socket is already open :)
  freeaddrinfo(ai_res);
  freeaddrinfo(ai_hints);
  ai_p = NULL; ai_res = NULL; ai_hints = NULL;
  // listen to socket
  if(listen(sock_server, AS_BACKLOG) < 0) {
    perror("listen");
    server->error = 1;
    return;
  }
  
  // init client list
  // AS_ClientList is root element, first real client will be 'AS_ClientList->next'
  AS_ConnectedClientList = calloc(1,sizeof(AS_ConnectedClients_t));
  int AS_ConnectedClients = 0;
  
  server->running = 1;
  // server is now running, calling process can read this variable and return
  
  // init select()
  FD_ZERO(&fds_master);
  FD_SET(sock_server,&fds_master); // add listening socket to list
  sockmax = sock_server;           // maximum socket number = only socket
  
  ///////////////////////////////////////////////////////////////////////////////////////
  // main server loop
  while(!server->stop) {
    // copy master list to "fds_read", since select() will manipulate the list
    fds_read = fds_master;
    // Use select() to wait for the next incomming message OR connection!
    // in order to react to the main thread, implement timeout
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000;  // 10 * 1000 microsec = 10 millisec
    rv = select(sockmax+1, &fds_read, NULL, NULL, &timeout);
    if(rv == 0) // timeout!
      // time out used in order to react to shutdown event
      // shutdown is checked at each loop iteartion
      // repeat loop
      continue;
    if(rv == -1)  //error!
      // some select() error...
      // no error catching for select() implemented
      // ignore for now and repeat loop
      continue;
    if(rv > 0)  {
      // a socket has triggered
      for(i = 0; i <= sockmax; i++) { // loop through all possible socket numbers
        if(FD_ISSET(i, &fds_read)) { // i = found triggering socket
          if(i == sock_server) {
            // this socket is the server listening socket!
            // -> accept new connections here!
            
            sockaddr_size = sizeof(sockaddr_remote);
            // typecast sockaddr_storage to sockaddr
            sock_remote = accept(sock_server, (struct sockaddr *) &sockaddr_remote, &sockaddr_size);
            if(sock_remote == -1)  {
              perror("accept"); 
              continue;
            }
            
            // create new Client
            newClient = calloc(1, sizeof(AS_ConnectedClients_t));
            // copy sockaddr_storage to client 'object'
            //newClient->sockaddr = sockaddr_remote;
            // save socket id to client 'object'
            // newClient->name is set to '\0\0\0\0...' due to calloc()
            newClient->socket = sock_remote;
            newClient->next = NULL;
            
            // now add this new socket to the select()-list in order to read it
            FD_SET(sock_remote, &fds_master);
            if(sock_remote > sockmax)
              sockmax = sock_remote;
            
            // append client object to list
            // iterate to end of client list
            AS_ConnectedClients_t* client;
            client = AS_ConnectedClientList;  // let pointer point to root of list
            while(client->next != NULL)
              client = client->next;  // iterate to end of list
            client->next = newClient; // last entry: add pointer to new Client!
            AS_ConnectedClients ++; // increase client counter
            
            // client is now in select() and also in AS_ConnectedClientlist
            printf("new client! (socket_id = %d)\n", sock_remote);
            
            // send "new user message" to all clients
            
          } else  {
            // some client sends data
            sock_remote = i;
            header = calloc(1, sizeof(AS_MessageHeader_t));
            rv = recv(sock_remote, header, sizeof(AS_MessageHeader_t), 0);
            if(rv == -1)  { // error
              perror("receive");
            } else if(rv == 0) {  // client closes connection
              printf("connection closed by remote client (socket_id = %d)\n", sock_remote);
              close(sock_remote); // clear the client (socket) from the select () list
              FD_CLR(sock_remote, &fds_master); // delete the client from AS_ConnectedClientList
              client = AS_ConnectedClientList;
              while(client->next != NULL) {
                lastClient = client;
                client = client->next;
                if(client->socket == sock_remote)  {
                  // delete element from linked list
                  lastClient->next = client->next;
                  free(client); // free memory
                  break;  // leave while loop
                }
              }
              AS_ConnectedClients --; // decrease client counter
            } else  { // clients sends actual data
              printf("receive data from client %d\n", sock_remote);
              // analyze header
              if(rv == sizeof(AS_MessageHeader_t))  {
                // received header!
                //printf("received header: destination: %d, type: %d, length: %d\n", header->clientDestination, header->payloadType, header->payloadLength);
                // receive the rest of the packet (payload length)
                payload = NULL;
                if(header->payloadLength) {
                  payload = calloc(1, header->payloadLength + sizeof(char));  // add an additional '\0' to the end of the payload (safe version, not needed)
                  AS_receiveAll(sock_remote, payload, header->payloadLength); // receive the exact amount of data
                }
                //printf("received payload: %s\n", payload);
                
                switch(header->payloadType) {
                  case AS_TypeMessage:
                  case AS_TypeFile:
                  default: // unknown payload (just forward)
                    if(header->clientDestination == -1) {
                      // message for server ? 
                      // server is not supposed to receive messages
                    } else  {
                      // forward message to user
                      // first: generate new message out of header + payload
                      header->clientSource = sock_remote;
                      len = sizeof(AS_MessageHeader_t) + header->payloadLength;
                      fprintf(stderr, "buffer size: %d\n", len);
                      buffer = calloc(1, len);
                      memcpy(buffer, header, sizeof(AS_MessageHeader_t));
                      memcpy(buffer + sizeof(AS_MessageHeader_t), payload, header->payloadLength);
                      // packet ready
                      if(header->clientDestination == -2) { // broadcasting -> send to all clients
                        printf("broadcasting message\n");
                        client = AS_ConnectedClientList;
                        while(client->next != NULL) {
                          client = client->next;
                          AS_sendAll(client->socket, buffer, len);
                        }
                      } else  { // destination specified ->  send only to destination client (no checking if connected)
                        AS_sendAll(header->clientDestination, buffer, len);
                      }
                      // done forwarding the message
                    }
                    break;
                }
                if(payload)
                  free(payload);
              } else  {
                // received to less in order for a correct header
                // do not try to handle error, just leave
                fprintf(stderr, "error: received to less data to read a correct header - discarding message\n");
              }
            }
            free(header);
          }
        } // FD_ISSET
      }
    }
  }
  
  // some thread has called this server to stop
  printf("AS_ServerThread: server on port %d is shutting down NOW\n", server->port);
  // disconnect users...
  header = calloc(1, sizeof(AS_MessageHeader_t));
  header->clientSource = -1;              // Server
  header->clientDestination = -2;         // input clientID here, -2 = broadcast
  header->payloadType = AS_TypeShutdown;  // Type of Packet
  header->payloadLength = 0;              // len of payload in byte
  client = AS_ConnectedClientList;
  while(client->next != NULL) {
    client = client->next;
    AS_sendAll(client->socket, header, sizeof(AS_MessageHeader_t));
  }
  
  // close socket
  close(sock_server);
  // finish up
  server->running = 0;
  server->port = 0;
  return;
}

int AS_ServerStart(int port, int IPv)  {
  if(!AS_initialized) AS_init();
  
  printf("AS_startServer(%d)\n", port);
    
  // check if port in range
  if(port > AS_MAXPORT || port <= 0) {
    fprintf(stderr, "error: AS_startServer(%d): port number out of range\n", port);
    return 0;
  }
  
  // check if there is already an AS server with this port number
  if(AS_ServerIsRunning(port))  {
    fprintf(stderr, "error: AS_startServer(%d): there is already an AS_Server on this port\n", port);
    return 0;
  }
  
  // create new element
  AS_Server_t* newServer = calloc(1, sizeof(AS_Server_t));
  // init element
  newServer->IPv = IPv;
  newServer->port = port;
  newServer->thread = calloc(1, sizeof(pthread_t));
  newServer->next = NULL;
  
  // start server thread
  pthread_create(newServer->thread, NULL, &AS_ServerThread, newServer);
  
  // thread created, wait for thread to finish start-up
  while(!newServer->running && !newServer->error)
    msecsleep(10);
    
  // either server started successfully, or an error occured
  if(newServer->error)  { // error occured, wait for thread to finish
    pthread_join(*(newServer->thread), NULL);
    free(newServer);  // free allocated memory and return
    return 0;
  }
  
  // server has announced that it is now running (without error)
  // add element to list:
  AS_Server_t* server;
  server = AS_ServerList;  // let pointer point to root of list
  while(server->next != NULL)
    server = server->next;
  server->next = newServer;
  return 1;
}


int AS_ServerStop(int port)  {
  if(!AS_initialized) AS_init();
  printf("AS_stopServer(%d)\n",port);
  
  AS_Server_t *server, *last;
  server = AS_ServerList;
  while(server->next != NULL) {
    last = server;
    server = server->next;
    if(server->port == port)  {
      // call thread to stop
      server->stop = 1;
      // wait until thread stops
      pthread_join(*(server->thread), NULL);
      printf("Server on port %d is down\n", port);
      
      // delete element from linked list
      last->next = server->next;
      free(server);
      
      return 1;
    }
  }
  return 0;
  fprintf(stderr, "no AS_Server running on port %d in this process\n", port);
}


//##########################################################################################################################


//////////////////////////////
//          CLIENT          //
//////////////////////////////

int AS_ClientConnect(char* host, char* port)	{
  if(!AS_initialized) AS_init();
	int sockID, rv;
	struct addrinfo *ai_hints, *ai_res, *ai_p;
  AS_Connections_t *con;
	
	ai_hints = calloc(1, sizeof(struct addrinfo));
	ai_hints->ai_family = AF_UNSPEC;
	ai_hints->ai_socktype = SOCK_STREAM;
	
	if((rv = getaddrinfo(host, port, ai_hints, &ai_res)) != 0) {
	  fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
	  return -1;
	}
	
	for(ai_p = ai_res; ai_p != NULL; ai_p = ai_p->ai_next)  {
	  if((sockID = socket(ai_res->ai_family, ai_res->ai_socktype, ai_res->ai_protocol)) == -1)  {
	    perror("client: socket");
	    continue;
	  }
	  if(connect(sockID, ai_res->ai_addr, ai_res->ai_addrlen) == -1)  {
	    close(sockID);
	    sockID = -1;
	    perror("client: connect");
	    continue;
	  }
	  // if this point is reached, connect was successfull
	  break;
	}
	
  if(ai_p == NULL)  { // for loop iteared until the end -> no connect !
	  fprintf(stderr, "client: failed to connect\n");
	  return -1;
	}
	
	freeaddrinfo(ai_res);
	freeaddrinfo(ai_hints);
	
	// successfully connected to server
	// add this client to internal client list
  con = calloc(1, sizeof(AS_Connections_t));
  con->conID = sockID;
  con->next = NULL;
  
  AS_Connections_t *connection;
  connection = AS_ConnectionList; // root of con list
  while(connection->next != NULL) // iterate through whole list until end
    connection = connection->next;
  connection->next = con; // add new connection to the end of the list
  
	return sockID;    // return socket fd (conID)
}

AS_ClientEvent_t* AS_ClientEvent(int conID) { // check for incomming stuff, receive data to buffer and return header
  if(!AS_initialized) AS_init();
  
  struct timeval tv;
  fd_set fds;
  int rv;
  static AS_ClientEvent_t *event = NULL; // including header
  static void *payload = NULL; // use same pointer to payload
  
  // each time this function is called, the old payload will be deleted
  if(payload != NULL) {
    free(payload);
    payload = NULL;
  }
  if(event != NULL) {
    free(event);
    event = NULL;
  }
  
  /*
  tv.tv_sec = 0;
  tv.tv_usec = 1000; // 1 ms
  FD_ZERO(&fds);
  FD_SET(conID, &fds);  // only this socket should be monitored
  */
  fcntl(conID, F_SETFL, O_NONBLOCK);  // non-blocking!
  //if(select(conID + 1, &fds, NULL, NULL, &tv))  { segmentation fault! -> use nonblocking socket instead
    event = calloc(1, sizeof(AS_ClientEvent_t));
    event->header = calloc(1, sizeof(AS_MessageHeader_t));
    rv = recv(conID, event->header, sizeof(AS_MessageHeader_t), 0);
    if(rv == -1)  { // error  
      if(errno != EWOULDBLOCK)  {  // ignore blocking "error"
        //perror("receive");
        return NULL;
      }
    } else if(rv == 0) {  // connection closed
      fprintf(stderr, "remote socket closed\n");
      AS_ClientDisconnect(conID);
      event->header->payloadType = AS_TypeShutdown;
    } else  { // receive data!
      if(rv == sizeof(AS_MessageHeader_t))  { // received size == header size
        // header is already in event variable
        // now receive payload
        if(event->header->payloadLength)  {
          payload = calloc(1, event->header->payloadLength);
          AS_receiveAll(conID, payload, event->header->payloadLength);
          // now, payload is downloaded to "payload"
          event->payload = payload; // copy pointer to event return value
        }
        
        if(event->header->payloadType == AS_TypeShutdown)
          AS_ClientDisconnect(conID);
      } else  { // header size
        fprintf(stderr, "error: received corrupt header - discarding message! (rv = %d)\n", rv);
        return NULL;
      }
    }
    return event;
  //}
  // timeout (no data waiting)
  return NULL;
}

int AS_ClientSendMessage(int conID, int recipient, char *message)  {
  if(!AS_initialized) AS_init();
  //fprintf(stderr, "AS_ClientSendMessage: '%s'\n", message);
  
  AS_MessageHeader_t *header;
  char *buffer;
  size_t size;
  int len;
  
  len = sizeof(char)*(strlen(message)+1); // +1 for '\0'
  header = calloc(1, sizeof(AS_MessageHeader_t));
  header->clientSource = 0;             // server will fill this
  header->clientDestination = -2;       // input clientID here, -2 = broadcast
  header->payloadType = AS_TypeMessage; // Type of Packet
  header->payloadLength = len;          // len of payload in byte
  
  size = sizeof(AS_MessageHeader_t) + len;  // len does no need sizeof(char) (already included)
  buffer = calloc(1, size);  // complete size of header+string
  memcpy(buffer, header, sizeof(AS_MessageHeader_t)); // copy header to beginning of buffer
  memcpy(buffer + sizeof(AS_MessageHeader_t), message, len);  // copy messsage to buffer behing header
  
  // buffer is not complete, send everything to server
  AS_sendAll(conID, buffer, size);
  // error check?
}

int AS_ClientDisconnect(int conID)	{
  if(!AS_initialized) AS_init();
  
  AS_Connections_t *connection, *last;
  
  connection = AS_ConnectionList; // root of con list
  while(connection->next != NULL) { // iterate through whole list until end
    last = connection;
    connection = connection->next;
    if(connection->conID == conID)  {
      last->next = connection->next;
      free(connection); // free memory
      break;
    }
  }
  
  close(conID);
  return 1;
}