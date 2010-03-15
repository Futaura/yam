#ifndef MAILSERVERS_H
#define MAILSERVERS_H

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

#include "YAM_stringsizes.h"

// forward declarations
struct Config;

// an enumeration type to flag a mail server
// structure to a specific type
enum MailServerType { MST_UNKNOWN=0, MST_SMTP, MST_POP3, MST_IMAP };
#define isSMTPServer(v) ((v)->type == MST_SMTP)
#define isPOP3Server(v) ((v)->type == MST_POP3)
#define isIMAPServer(v) ((v)->type == MST_IMAP)

// for managing the mail server flags. These flags are
// dependant on the actual type of the mail server
#define MSF_ACTIVE              (1<<0)  // [POP3/SMTP] : user enabled this server
#define MSF_APOP                (1<<1)  // [POP3]      : use APOP authentication
#define MSF_PURGEMESSGAES       (1<<2)  // [POP3]      : delete messages after transfer
#define MSF_UIDLCHECKED         (1<<3)  // [POP3]      : TRUE if the UIDLs were checked during the last transfer
#define MSF_SEC_SSL             (1<<4)  // [POP3/SMTP] : SSLv3 protocol secure mode
#define MSF_SEC_TLS             (1<<5)  // [POP3/SMTP] : TLSv1 protocol secure mode
#define MSF_AUTH                (1<<6)  // [SMTP]      : SMTP AUTH enabled/disabled
#define MSF_AUTH_AUTO           (1<<7)  // [SMTP]      : SMTP AUTH method = AUTO
#define MSF_AUTH_DIGEST         (1<<8)  // [SMTP]      : SMTP AUTH method = DIGEST-MD5
#define MSF_AUTH_CRAM           (1<<9)  // [SMTP]      : SMTP AUTH method = CRAM-MD5
#define MSF_AUTH_LOGIN          (1<<10) // [SMTP]      : SMTP AUTH method = LOGIN
#define MSF_AUTH_PLAIN          (1<<11) // [SMTP]      : SMTP AUTH method = PLAIN
#define MSF_ALLOW_8BIT          (1<<12) // [SMTP]      : Server allows 8bit characters

#define isServerActive(v)       (isFlagSet((v)->flags, MSF_ACTIVE))
#define hasServerAPOP(v)        (isFlagSet((v)->flags, MSF_APOP))
#define hasServerPurge(v)       (isFlagSet((v)->flags, MSF_PURGEMESSGAES))
#define hasServerCheckedUIDL(v) (isFlagSet((v)->flags, MSF_UIDLCHECKED))
#define hasServerSSL(v)         (isFlagSet((v)->flags, MSF_SEC_SSL))
#define hasServerTLS(v)         (isFlagSet((v)->flags, MSF_SEC_TLS))
#define hasServerAuth(v)        (isFlagSet((v)->flags, MSF_AUTH))
#define hasServerAuth_AUTO(v)   (isFlagSet((v)->flags, MSF_AUTH_AUTO))
#define hasServerAuth_DIGEST(v) (isFlagSet((v)->flags, MSF_AUTH_DIGEST))
#define hasServerAuth_CRAM(v)   (isFlagSet((v)->flags, MSF_AUTH_CRAM))
#define hasServerAuth_LOGIN(v)  (isFlagSet((v)->flags, MSF_AUTH_LOGIN))
#define hasServerAuth_PLAIN(v)  (isFlagSet((v)->flags, MSF_AUTH_PLAIN))
#define hasServer8bit(v)        (isFlagSet((v)->flags, MSF_ALLOW_8BIT))

#define MSF2SMTPSecMethod(v)    (hasServerTLS(v) ? 1 : (hasServerSSL(v) ? 2 : 0))
#define MSF2POP3SecMethod(v)    (hasServerSSL(v) ? 1 : (hasServerTLS(v) ? 2 : 0))
#define MSF2SMTPAuthMethod(v)   (hasServerAuth_AUTO(v)   ? 0 : \
                                (hasServerAuth_DIGEST(v) ? 1 : \
                                (hasServerAuth_CRAM(v)   ? 2 : \
                                (hasServerAuth_LOGIN(v)  ? 3 : \
                                (hasServerAuth_PLAIN(v)  ? 4 : 0)))))
#define SMTPSecMethod2MSF(v)    ((v) == 1 ? MSF_SEC_TLS : ((v) == 2 ? MSF_SEC_SSL : 0))
#define POP3SecMethod2MSF(v)    ((v) == 1 ? MSF_SEC_SSL : ((v) == 2 ? MSF_SEC_TLS : 0))
#define SMTPAuthMethod2MSF(v)   ((v) == 0 ? MSF_AUTH_AUTO   : \
                                ((v) == 1 ? MSF_AUTH_DIGEST : \
                                ((v) == 2 ? MSF_AUTH_CRAM   : \
                                ((v) == 3 ? MSF_AUTH_LOGIN  : \
                                ((v) == 4 ? MSF_AUTH_PLAIN  : 0)))))

// mail server data structure
struct MailServerNode
{
  struct MinNode node;                   // required for placing it into struct Config

  enum MailServerType type;              // which type is this server? POP3 or SMTP?

  char account[SIZE_USERID+SIZE_HOST+1]; // user definable account name
  char hostname[SIZE_HOST];              // servername/IP
  char domain[SIZE_HOST];                // [SMTP] : the mail domain
  int  port;                             // the port
  char username[SIZE_USERID];            // the account ID/name
  char password[SIZE_USERID];            // the password for this account

  unsigned int flags;                    // for mail server flags (MSF_#?)
};

// public functions
struct MailServerNode *CreateNewMailServer(enum MailServerType type, struct Config *co, BOOL first);
void FreeMailServerList(struct MinList *mailServerList);
BOOL CompareMailServerLists(const struct MinList *msl1, const struct MinList *msl2);
struct MailServerNode *GetMailServer(struct MinList *mailServerList, enum MailServerType type, unsigned int num);

#endif // MAILSERVERS_H
