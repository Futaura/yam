#ifndef TCP_H
#define TCP_H 1

/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 by Marcel Beck <mbeck@yam.ch>
 Copyright (C) 2000-2010 by YAM Open Source Team

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 YAM Official Support Site :  http://www.yam.ch
 YAM OpenSource project    :  http://sourceforge.net/projects/yamos/

 $Id$

***************************************************************************/

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <openssl/ssl.h>
#include <time.h>

// general connection/transfer error enumation values
enum ConnectError
{
  CONNECTERR_SUCCESS       = 0,
  CONNECTERR_NO_ERROR      = -1,
  CONNECTERR_UNKNOWN_ERROR = -2,
  CONNECTERR_SOCKET_IN_USE = -3,
  CONNECTERR_UNKNOWN_HOST  = -4,
  CONNECTERR_NO_SOCKET     = -5,
  CONNECTERR_NO_NONBLOCKIO = -6,
  CONNECTERR_TIMEDOUT      = -7,
  CONNECTERR_ABORTED       = -8,
  CONNECTERR_SSLFAILED     = -9,
  CONNECTERR_INVALID8BIT   = -10,
  CONNECTERR_NO_CONNECTION = -11,
  CONNECTERR_NOT_CONNECTED = -12
};

struct Connection
{
  LONG socket;                   // the socket ID returned by socket()
  struct sockaddr_in socketAddr; // the host this connection was established to

  SSL_CTX *sslCtx;               // SSL context stuff
  SSL *ssl;

  char *receiveBuffer;           // receive buffer
  char *receivePtr;              // current pointer into the receive buffer
  int receiveCount;              // number of received bytes for buffered I/O
  int receiveBufferSize;         // receive buffer size

  char *sendBuffer;              // send buffer
  char *sendPtr;                 // current pointer into the send buffer
  int sendCount;                 // numer of bytes to by sent for buffered I/O
  int sendBufferSize;            // send buffer size

  struct fd_set fdset;           // file descriptors for WaitSelect()
  struct timeval timeout;        // timeout for WaitSelect()

  enum ConnectError error;       // error value of the last action

  struct Library *socketBase;    // local instance of SocketBase
  #if defined(__amigaos4__)
  struct SocketIFace *socketIFace;
  #endif

  BOOL closeSocketBase;          // do we have to close SocketBase ourself?
  BOOL connectedFromMainThread;  // who created this connection?
  BOOL isConnected;              // has ConnectToHost() been called before?
};

struct Connection *CreateConnection(void);
void DeleteConnection(struct Connection *conn);
BOOL ConnectionIsOnline(struct Connection *conn);
enum ConnectError ConnectToHost(struct Connection *conn, const char *host, const int port);
void DisconnectFromHost(struct Connection *conn);
BOOL MakeSecureConnection(struct Connection *conn);
int ReceiveLineFromHost(struct Connection *conn, char *vptr, const int maxlen);
int ReceiveFromHost(struct Connection *conn, char *recvdata, const int maxlen);
int SendLineToHost(struct Connection *conn, const char *vptr);
int SendToHost(struct Connection *conn, const char *ptr, const int len, const int flags);

#endif /* TCP_H */
