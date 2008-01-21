#ifndef YAM_TRANSFER_H
#define YAM_TRANSFER_H

/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 by Marcel Beck <mbeck@yam.ch>
 Copyright (C) 2000-2008 by YAM Open Source Team

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

#include <stdio.h>

#include "YAM_mainFolder.h"

#include "HashTable.h"
#include "MailList.h"

enum TransferType
{
  TR_IMPORT = 0,   // import mails
  TR_EXPORT,       // export mails
  TR_GET_USER,     // get mails, user triggered
  TR_GET_AUTO,     // get mails, timer/ARexx triggered
  TR_SEND_USER,    // send mails, user triggered
  TR_SEND_AUTO     // send mails, timer/ARexx triggered
};

enum SMTPSecMethod  { SMTPSEC_NONE=0, SMTPSEC_TLS, SMTPSEC_SSL };
enum SMTPAuthMethod { SMTPAUTH_AUTO=0, SMTPAUTH_DIGEST, SMTPAUTH_CRAM, SMTPAUTH_LOGIN, SMTPAUTH_PLAIN };
enum TransWinMode   { TWM_HIDE=0, TWM_AUTO, TWM_SHOW };
enum PreSelMode     { PSM_NEVER=0, PSM_LARGE, PSM_ALWAYS, PSM_ALWAYSLARGE };

#define TCP_NO_SOCKET -1

// Socket Options a user can set in .config
// if a value was not specified by the user it is either -1 or
// FALSE for a boolean.
struct TRSocketOpt
{
  LONG SendBuffer;   // SO_SNDBUF
  LONG RecvBuffer;   // SO_RCVBUF
  LONG SendLowAt;    // SO_SNDLOWAT
  LONG RecvLowAt;    // SO_RCVLOWAT
  LONG SendTimeOut;  // SO_SNDTIMEO
  LONG RecvTimeOut;  // SO_RCVTIMEO
  BOOL KeepAlive;    // SO_KEEPALIVE
  BOOL NoDelay;      // TCP_NODELAY
  BOOL LowDelay;     // IPTOS_LOWDELAY
};

struct DownloadResult
{
  long Downloaded;
  long OnServer;
  long DupSkipped;
  long Deleted;
  BOOL Error;
};

struct TR_GUIData
{
  Object *WI;
  Object *GR_LIST;
  Object *GR_PAGE;
  Object *LV_MAILS;
  Object *BT_PAUSE;
  Object *BT_RESUME;
  Object *BT_QUIT;
  Object *BT_START;
  Object *TX_STATS;
  Object *TX_STATUS;
  Object *GA_COUNT;
  Object *GA_BYTES;
  Object *BT_ABORT;
  char *ST_STATUS;
};

enum GUILevel
{
  POP_USER = 0,
  POP_START,
  POP_TIMED,
  POP_REXX
};

enum ImportFormat
{
  IMF_UNKNOWN = 0,
  IMF_MBOX,
  IMF_DBX,
  IMF_PLAIN
};

enum SendMode
{
  SEND_ALL_USER = 0,
  SEND_ALL_AUTO,
  SEND_ACTIVE_USER,
  SEND_ACTIVE_AUTO
};

// transfer window class data
struct TR_ClassData
{
  struct TR_GUIData     GUI;          // the actual GUI relevant data
  struct MinList        transferList; // list for managing the downloads
  struct MinNode       *GMD_Mail;
  struct Folder *       ImportFolder;
  struct HashTable *    UIDLhashTable; // for maintaining all UIDLs

  long                  Abort;
  long                  Pause;
  long                  Start;
  int                   SearchCount;
  int                   GMD_Line;
  enum GUILevel         GUIlevel;
  enum ImportFormat     ImportFormat;
  int                   POP_Nr;
  BOOL                  SinglePOP;
  BOOL                  Checking;
  BOOL                  DuplicatesChecking;
  BOOL                  UIDLhashIsDirty;
  struct DownloadResult Stats;

  char                  WTitle[SIZE_DEFAULT];
  char                  ImportFile[SIZE_PATHFILE];
  char                  CountLabel[SIZE_DEFAULT];
  char                  BytesLabel[SIZE_DEFAULT];
  char                  StatsLabel[SIZE_DEFAULT];
};

extern struct Hook TR_ProcessGETHook;
extern struct Hook TR_ProcessIMPORTHook;

void  TR_Cleanup(void);
void  TR_CloseTCPIP(void);
BOOL  TR_DownloadURL(const char *server, const char *request, const char *filename);
void  TR_GetMailFromNextPOP(BOOL isfirst, int singlepop, enum GUILevel guilevel);
BOOL  TR_GetMessageList_IMPORT(void);
BOOL  TR_IsOnline(void);
struct TR_ClassData *TR_New(enum TransferType TRmode);
BOOL  TR_OpenTCPIP(void);
BOOL  TR_ProcessEXPORT(char *fname, struct MailList *mlist, BOOL append);
BOOL  TR_ProcessSEND(struct MailList *mlist, enum SendMode mode);
void  TR_SetWinTitle(BOOL from, const char *text);
BOOL  TR_SendPOP3KeepAlive(void);

#endif /* YAM_TRANSFER_H */
