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
  * sending binary files
    * unlimited size
    * handle file transfer
    * file transfer should not block other communication
  * maybe: streaming functionality (binary data stream)
  * sending user defined data directly (leave room for the programmer)
The interface will offer the programmer the following functionality:
* Server functionality:
  * AS_ServerStart(port)
  * AS_ServerStop(port)
*Client functionality:
  * Calls:
    * AS_connect(host,port)		connect to server
    * AS_GetConnectedUsers()	returns list of all connected users
    * AS_send_message(user)	send message to a user (or broadcast)
    * AS_send_file(user)		send a file to a user
  * Call-backs: Callbacks have to be implemented by the programmer. If a callback is not implemented, this functionality will simply not be present in this client. E.g. if there is no callback defined for file_incomming, all file will be rejected automatically.
    * AS_message_received(*buffer)	receive ASCII message
    * AS_file_incomming()		choose whether to accept incoming file
    * AS_file_progress()		reports progress of incoming/outgoing file
    * AS_file_received(tmpLocation)	reports location of received file
    * AS_file_rejected()		receiver does not accept incoming file
    * AS_user_new()			reports new users
    * AS_user_left()			reports disconnecting users
