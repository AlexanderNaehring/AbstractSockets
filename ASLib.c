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

//////////////////////////////
//        STRUCTURES        //
//////////////////////////////

typedef enum { false, true } bool;

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
  struct sockaddr_storage sockaddr;
  
  struct AS_ConnectedClients_s *next;
} AS_ConnectedClients_t;

typedef struct AS_Connections_s  {  // client side: outgoing connections
  int conID;
  
  struct AS_Connections_s *next;
} AS_Connections_t;

typedef struct AS_MessageHeader_s { // header of each packet! afterwards -> payload
  int clientSource;           // -1: server
  int clientDestination;      // -1: server // -2: broadcast to all clients
  unsigned int payloadType;   // AS_PAYLOAD_xxx
  unsigned int payloadLength; // bytes
} AS_MessageHeader_t;

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

int AS_sendAll(int sock, char *buf, int len)  { // replaces send(), sends in multiple steps if necessary
  int total = 0;        // bytes sent
  int bytesleft = len;  // bytes left
  int n;                // bytes send per call
  while(total < len) {
    n = send(s, buf+total, bytesleft, 0);
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
    printf("no AS_Server running in this process\n");
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
  struct sockaddr_storage sockaddr_remote; // IP agnostiv instead of using sockaddr_in
  socklen_t sockaddr_size;
  char buffer[AS_BUFFLEN+1];
  
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
  AS_ConnectedClients_t *AS_ConnectedClientList;
  AS_ConnectedClientList = calloc(1,sizeof(AS_ConnectedClients_t));
  
  server->running = 1;
  // server is now running, calling process can read this variable and return
  
  // init select()
  FD_ZERO(&fds_master);
  FD_SET(sock_server,&fds_master); // add listening socket to list
  sockmax = sock_server;           // maximum socket number = only socket
  // main server loop
  while(!server->stop) {
    // copy master list to "fds_read", since select() will manipulate the list
    fds_read = fds_master;
    // Use select() to wait for the next incomming message OR connection!
    // in order to react to the main thread, implement timeout
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000;  // 10 * 1000 = 10 ms
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
            AS_ConnectedClients_t* newClient = calloc(1, sizeof(AS_ConnectedClients_t));
            // copy sockaddr_storage to client 'object'
            newClient->sockaddr = sockaddr_remote;
            // save socket id to client 'object'
            newClient->socket = sock_remote;
            
            // now add this new socket to the select()-list in order to read it
            FD_SET(sock_remote, &fds_master);
            if(sock_remote > sockmax)
              sockmax = sock_remote;
              
            // append client object to list
            // iterate to end of client list
            AS_ConnectedClients_t* client;
            client = AS_ConnectedClientList;  // let pointer point to root of list
            while(client->next != NULL)
              client = client->next;
            client->next = newClient;
            
            // client is now in select() and also in AS_ConnectedClientlist
            printf("new client! (socket_id = %d)\n", sock_remote);
            
            
          } else  { // select()
            // some client sends data
            sock_remote = i;
            memset(&buffer, 0, sizeof(buffer));
            rv = recv(sock_remote, buffer, AS_BUFFLEN, 0);
            
            if(rv == -1)  { // error
              perror("receive");
            } else if(rv == 0) {  // client closes connection
              printf("connection closed by remote client (socket_id = %d)\n", sock_remote);
              close(sock_remote);
              // clear the client (socket) from the select () list
              FD_CLR(sock_remote, &fds_master);
              // delete the client from AS_ConnectedClientList
              AS_ConnectedClients_t *client, *lastClient;
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
            } else  { // clients sends actual data
            
            
            
            
            }
            
            //_______
          }
        } // FD_ISSET
      }
    }
  }
  
  // some thread has called this server to stop
  printf("AS_ServerThread: server on port %d is shutting down NOW\n", server->port);
  // disconnect users...
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

int AS_ClientEvent(int conID) {

}

int AS_ClientReceivedBytes(int conID) {
  
}

int AS_ClientSendMessage(int conID, int recipient, char *message, int len)  {
  AS_MessageHeader_t *header;
  char *buffer;
  size_t = size;

  header = calloc(1, sizeof(AS_MessageHeader_t));
  header->clientSource = 0;                 // server will fill this
  header->clientDestination = -2;           // input clientID here, -2 = broadcast
  header->payloadType = AS_TypeMessage;     // Type of Packet
  header->payloadLength = strlen(buffer2);  // len of payload (here: message string)
  
  size = sizeof(AS_MessageHeader_t) + sizeof(char)*(len+1)
  buffer = calloc(1, size);  // complete size of header+string
  memcpy(buffer, header, sizeof(AS_MessageHeader_t)); // copy header to beginning of buffer
  memcpy(buffer + sizeof(AS_MessageHeader_t), message, len);  // copy messsage to buffer behing header
  
  // buffer is not complete, send everything to server
  AS_sendAll(conID, buffer, size);
  // error check?
}

int AS_ClientRead(int conID, char *buffer, int len) { // called after a receive-event is triggered
  // 
}

int AS_ClientDisconnect(int sockID)	{
  
}