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
  int clientsNum;
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

double msec() {
  struct timeval tv;
  double time;
  
  gettimeofday(&tv, NULL);  // get current time
  time = (tv.tv_sec) * 1000 + (tv.tv_usec) / 1000;
  return time;
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
    printf("  server on port %d: running = %d, clientsNum = %d, stop = %d\n", server->port, server->running, server->clientsNum, server->stop);
  }
  if(!count)
    printf("  no AS_Server running in this process\n");
  return 0;
}

void* AS_ServerThread(void *arg) {
  AS_Server_t* server = arg;
  fprintf(stderr, "AS_ServerThread(%d)\n", server->port);
  
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
  void *buffer;
  void *payload;
  int *tmpPI;
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
    fprintf(stderr, "server %d: error: getaddrinfo: %s\n", server->port, gai_strerror(rv));
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
    fprintf(stderr, "server %d: running at [%s]:%s\n", server->port, ipstr, portstr);
    break;
  }
  if(ai_p == NULL) {  // iterated through complete list without binding
    fprintf(stderr, "server %d: error: failed to bind server to port %s\n", server->port, portstr);
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
  server->clientsNum = 0;
  
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
            // newClient->sockaddr = sockaddr_remote;
            // newClient->name is set to '\0\0\0\0...' due to calloc()
            newClient->socket = sock_remote;
            newClient->next = NULL;
            
            // now add this new socket to the select()-list for socket reading
            FD_SET(sock_remote, &fds_master);
            if(sock_remote > sockmax)
              sockmax = sock_remote;
            
            // send clientID to new client
            header = calloc(1, sizeof(AS_MessageHeader_t));
            header->as_identifier = 144; // mandatory (for checking at receiver)
            header->clientSource = -1; // server
            header->clientDestination = newClient->socket; // this indicates the new clients id
            header->payloadType = AS_TypeClientID; // inform client that it will receive it's own id
            header->payloadLength = 0; // no payload needed
            AS_sendAll(newClient->socket, header, sizeof(AS_MessageHeader_t)); // send info only to new client
            free(header); header = NULL;
            
            // send "new client" to all clients
            client = AS_ConnectedClientList;  // let pointer point to root of list
            while(client->next != NULL) {
              client = client->next;
              
              header = calloc(1, sizeof(AS_MessageHeader_t));
              header->as_identifier = 144; // mandatory (for checking at receiver)
              header->clientSource = -1; // server
              header->clientDestination = newClient->socket;
                // destination not needed since server is source
                // -> use field for transmitting new client ID
              header->payloadType = AS_TypeClientConnect; // new client
              header->payloadLength = 0; // no payload needed (id is stored in "destination")
              AS_sendAll(client->socket, header, sizeof(AS_MessageHeader_t)); // send info
              free(header); header = NULL;
            }
            // append client object to list
            client->next = newClient; // add pointer to new client to list of clients!
            server->clientsNum ++; // increase client counter
            
            fprintf(stderr, "server %d: new client %d\n", server->port, sock_remote);
          } else  {
            // some client sends data
            sock_remote = i;
            header = calloc(1, sizeof(AS_MessageHeader_t));
            rv = recv(sock_remote, header, sizeof(AS_MessageHeader_t), 0);
            if(rv == -1)  { // error
              perror("receive");
            } else if(rv == 0) {  // client closes connection
              fprintf(stderr, "server %d: client %d closed connection\n", server->port, sock_remote);
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
                } else  {
                  // inform other clients that this client left
                  header = calloc(1, sizeof(AS_MessageHeader_t));
                  header->as_identifier = 144; // mandatory (for checking at receiver)
                  header->clientSource = -1; // server
                  header->clientDestination = sock_remote;  // triggering socket is the client which disconnected
                  header->payloadType = AS_TypeClientDisconnect; // 
                  header->payloadLength = 0;
                  AS_sendAll(client->socket, header, sizeof(AS_MessageHeader_t)); // send info
                  free(header); header = NULL;
                }
              }
              server->clientsNum --; // decrease client counter
            } else  { // clients sends actual data
              // analyze header
              if(rv == sizeof(AS_MessageHeader_t) && header->as_identifier == 144)  {
                // header received
                // header has orrect length and test variable is also correct
                // receive the rest of the packet (payload length)
                payload = NULL;
                if(header->payloadLength) {
                  payload = calloc(1, header->payloadLength + sizeof(char));  // add an additional '\0' to the end of the payload (safe version, not needed)
                  AS_receiveAll(sock_remote, payload, header->payloadLength); // receive the exact amount of data
                }
                
                switch(header->payloadType) {
                  // all typed that are forwarded to other clients and handled the same way:
                  case AS_TypeMessage:
                  case AS_TypeFileRequest:
                  case AS_TypeFileAnswer:
                  case AS_TypeFileData:
                    if(header->clientDestination == -1) {
                      fprintf(stderr, "error: server %d: client %d sends unexpected data\n", server->port, sock_remote);
                    } else  {
                      // forward message to user
                      // first: generate new message out of header + payload
                      // header already present, just add sourceID (if not present already)
                      header->clientSource = sock_remote;
                      len = sizeof(AS_MessageHeader_t) + header->payloadLength;
                      buffer = calloc(1, len);
                      memcpy(buffer, header, sizeof(AS_MessageHeader_t)); // copy header + payload to buffer
                      memcpy(buffer + sizeof(AS_MessageHeader_t), payload, header->payloadLength);
                      // packet ready
                      if(header->clientDestination == -2) { // broadcasting -> send to all clients
                        client = AS_ConnectedClientList;
                        while(client->next != NULL) {
                          client = client->next;
                          AS_sendAll(client->socket, buffer, len);
                        }
                        fprintf(stderr, "server %d: data: client %d -> broadcast\n", server->port, sock_remote);
                      } else  { // destination specified ->  send only to destination client (no checking if connected)
                        AS_sendAll(header->clientDestination, buffer, len);
                        fprintf(stderr, "server %d: data: client %d -> client %d\n", server->port, sock_remote, header->clientDestination);
                      }
                      free(buffer); buffer = NULL;
                      // done forwarding the message
                    }
                    break;
                  case AS_TypeAskForClients:
                    // client wants to know who is connected to this server
                    header->clientSource = -1;  // change source to server
                    header->clientDestination = sock_remote; // change dest to client asking
                    header->payloadType = AS_TypeListOfClients; // return list of clients
                    header->payloadLength = server->clientsNum * sizeof(int); // all client IDs in payload
                    // don't need old payload
                    if(payload)
                      free(payload);
                    payload = calloc(server->clientsNum, sizeof(int)); // allocate memory for payload
                    tmpPI = payload; // copy pointer, now tmpP points to payload start
                    client = AS_ConnectedClientList;
                    while(client->next != NULL) {
                      client = client->next;
                      *tmpPI = client->socket; // write int to location of tmpP pointer
                      tmpPI += 1;  // more tmpP to next entry
                    }
                    len = sizeof(AS_MessageHeader_t) + server->clientsNum * sizeof(int);
                    buffer = calloc(1, len);
                    memcpy(buffer, header, sizeof(AS_MessageHeader_t)); // copy header + payload to buffer
                    memcpy(buffer + sizeof(AS_MessageHeader_t), payload, server->clientsNum * sizeof(int));
                    // send header + list of clients to requesting client
                    AS_sendAll(header->clientDestination, buffer, len);
                    fprintf(stderr, "server %d: sent list of clients to client %d\n", server->port, sock_remote);
                    free(buffer); buffer = NULL;
                    break;
                }
                if(payload)
                  free(payload); payload = NULL;
              } else  {
                // received too less in order for a correct header
                // do not try to handle error, just leave
                fprintf(stderr, "server %d: error: received incorrect header from %d!\n", server->port, sock_remote);
              }
            }
            free(header); header = NULL;
          }
        } // FD_ISSET
      }
    }
  }
  
  // some thread has called this server to stop
  //fprintf(stderr, "AS_ServerThread: server on port %d is shutting down NOW\n", server->port);
  // disconnect users...
  header = calloc(1, sizeof(AS_MessageHeader_t));
  header->as_identifier = 144; // mandatory (for checking at receiver)
  header->clientSource = -1;              // Server
  header->clientDestination = -2;         // input clientID here, -2 = broadcast
  header->payloadType = AS_TypeShutdown;  // Type of Packet
  header->payloadLength = 0;              // len of payload in byte
  client = AS_ConnectedClientList;
  while(client->next != NULL) {
    client = client->next;
    AS_sendAll(client->socket, header, sizeof(AS_MessageHeader_t));
  }
  free(header); header = NULL;
  
  // close socket
  close(sock_server);
  // finish up
  server->running = 0;
  server->port = 0;
  return;
}

int AS_ServerStart(int port, int IPv)  {
  if(!AS_initialized) AS_init();
  
  fprintf(stderr, "AS_startServer(%d)\n", port);
    
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
  //fprintf(stderr, "AS_stopServer(%d)\n",port);
  
  if(port == 0) {
    while(AS_ServerList->next != NULL) {
      AS_ServerStop(AS_ServerList->next->port);
    }
  }
  
  AS_Server_t *server, *last;
  server = AS_ServerList;
  while(server->next != NULL) {
    last = server;
    server = server->next;
    if(server->port == port || port == 0)  {
      server->stop = 1; // call thread to stop and wait
      pthread_join(*(server->thread), NULL);
      fprintf(stderr, "server %d is now stopped\n", port);
      
      // delete element from linked list
      last->next = server->next;
      free(server);
      return 1; // only return if this single server should be stopped
    }
  }
  return 0;
}


//##########################################################################################################################


//////////////////////////////
//          CLIENT          //
//////////////////////////////

int AS_ClientConnect(char* host, char* port)	{ // connect to a server and return connection ID
  if(!AS_initialized) AS_init();
	int sockID, rv, time;
	struct addrinfo *ai_hints, *ai_res, *ai_p;
  AS_Connections_t *con;
  AS_MessageHeader_t *header;
  int bytes, established;
	
	ai_hints = calloc(1, sizeof(struct addrinfo));
	ai_hints->ai_family = AF_UNSPEC;
	ai_hints->ai_socktype = SOCK_STREAM;
	
	if((rv = getaddrinfo(host, port, ai_hints, &ai_res)) != 0) {
	  fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
	  return 0;
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
	  fprintf(stderr, "client: failed to connect to server\n");
	  return 0;
	}
	
	freeaddrinfo(ai_res);
	freeaddrinfo(ai_hints);
	
	// socket connection successfull
	// now wait for welcome message of AS Server!
	
  header = calloc(1, sizeof(AS_MessageHeader_t));
  rv = AS_receiveAll(sockID, header, sizeof(AS_MessageHeader_t)); // wait for header to arrive
  // known bug: this function will block program!
  if(rv == sizeof(AS_MessageHeader_t))  {
    // correct size received
	  // successfully connected to AS Server
	  // add this client to internal client list
	  if(header->payloadType == AS_TypeClientID)  {
	    // received welcome message from server!
	    fprintf(stderr, "welcome to this server, your ID is %d\n", header->clientDestination);
	    
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
  }
  // header not received completely!
  fprintf(stderr, "problems connecting to server [%s]:%s\n", host, port);
	return 0;
}

int AS_ClientCheckConID(int conID)  { // test if conID is in list of current conections monitored by AS
  if(!AS_initialized) AS_init();
  
  AS_Connections_t *connection;
  
  connection = AS_ConnectionList; // root of con list
  while(connection->next != NULL) { // iterate through whole list until end
    connection = connection->next;
    if(connection->conID == conID)  {
      return true; // found this conID in the list -> return true (1)
    }
  }
  return false;
}

AS_ClientEvent_t* AS_ClientEvent(int conID) { // check for incomming stuff, receive data to buffer and return header
  if(!AS_initialized) AS_init();
  
  if(!AS_ClientCheckConID(conID)) {
    fprintf(stderr, "AS_ClientEvent error: conID not valid\n");
    return NULL;  // conID not valid (server socket fd)
  }
  
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
  //if(select(conID + 1, &fds, NULL, NULL, &tv))  { known bug: segmentation fault! (no idea why) -> use non blocking socket instead
    event = calloc(1, sizeof(AS_ClientEvent_t));
    event->header = calloc(1, sizeof(AS_MessageHeader_t));
    
    rv = recv(conID, event->header, sizeof(AS_MessageHeader_t), 0);
    // known bug: if a correct header is send but only a part of it is received in one call of recv
    // this header will not be recognised and the packet is lost!
    
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
      if(rv == sizeof(AS_MessageHeader_t) && event->header->as_identifier == 144)  {
        // received header with correct size and correct identifier
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
        
        if(event->header->payloadType == AS_TypeListOfClients)  {
          int num, i;
          int *cid;
          num = event->header->payloadLength / sizeof(int);
          cid = event->payload;
          fprintf(stderr, "%d clients are connected to the server:", num);
          for(i = 0; i < num; i++) {
            if(i == (num-1))
              fprintf(stderr, " #%d\n", *cid);
            else
              fprintf(stderr, " #%d,", *cid);
            cid ++; // point to next int
          }
        }
          
      } else  { // header size
        fprintf(stderr, "error: received incorrect header! (rv=%d)\n", rv);
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
  
  if(!AS_ClientCheckConID(conID)) {
    fprintf(stderr, "AS_ClientSendMessage error: conID not valid\n");
    return 0;  // conID not valid (server socket fd)
  }
  
  int rv;
  AS_MessageHeader_t *header;
  char *buffer;
  size_t size;
  int len;
  
  len = sizeof(char)*(strlen(message)+1); // +1 for '\0'
  header = calloc(1, sizeof(AS_MessageHeader_t));
  header->as_identifier = 144; // mandatory (for checking at receiver)
  header->clientSource = 0;             // server will fill this
  header->clientDestination = -2;       // input clientID here, -2 = broadcast
  header->payloadType = AS_TypeMessage; // Type of Packet
  header->payloadLength = len;          // len of payload in byte
  
  size = sizeof(AS_MessageHeader_t) + len;  // len does no need sizeof(char) (already included)
  buffer = calloc(1, size);  // complete size of header+string
  memcpy(buffer, header, sizeof(AS_MessageHeader_t)); // copy header to beginning of buffer
  memcpy(buffer + sizeof(AS_MessageHeader_t), message, len);  // copy messsage to buffer behing header
  
  // buffer is not complete, send everything to server
  rv =  AS_sendAll(conID, buffer, size);
  // error check?
  free(header);
  free(buffer);
  return rv;
}

int AS_ClientListClients(int conID) {
  if(!AS_initialized) AS_init();
  if(!AS_ClientCheckConID(conID)) {
    return 0;  // conID not valid (server socket fd)
  }
  
  int rv;
  AS_MessageHeader_t *header;
  
  header = calloc(1, sizeof(AS_MessageHeader_t));
  header->as_identifier = 144; // mandatory (for checking at receiver)
  header->clientSource = 0;
  header->clientDestination = -1;
  header->payloadType = AS_TypeAskForClients;
  header->payloadLength = 0;
  
  rv = AS_sendAll(conID, header, sizeof(AS_MessageHeader_t));
  free(header);
  return rv;
}

int AS_ClientDisconnect(int conID)	{
  if(!AS_initialized) AS_init();
  
  if(!AS_ClientCheckConID(conID)) {
    return 0;  // conID not valid (server socket fd)
  }
  
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
