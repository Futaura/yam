#ifndef MAIN_YAM_H
#define MAIN_YAM_H

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

#include <xpk/xpk.h>

#if defined(__AROS__)
#include <sys/types.h>
#endif

#include <netinet/in.h>

#include "YAM_stringsizes.h"
#include "YAM_userlist.h"    // struct Users
#include "YAM_utilities.h"   // ASL_MAX

#include "BayesFilter.h"     // struct TokenAnalyzer
#include "Rexx.h"            // struct RuleResult
#include "Themes.h"          // struct Theme
#include "Timer.h"           // struct Timers
#include "Threads.h"         // struct Thread
#include "Transfer.h"        // struct DownloadResult

// forward declarations
struct DiskObject;
struct AppIcon;
struct MsgPort;
struct RexxHost;
struct FileReqCache;
struct Locale;
struct Catalog;
struct MA_ClassData;
struct CO_ClassData;
struct AB_ClassData;
struct EA_ClassData;
struct WR_ClassData;
struct TR_ClassData;
struct ER_ClassData;
struct FI_ClassData;
struct FO_ClassData;
struct DI_ClassData;
struct ReadMailData;
struct codeset;
struct codesetList;
struct HashTable;
struct NotifyRequest;

/**************************************************************************/

enum GlobalDispatcherJob
{
  ID_CLOSEALL=1000,
  ID_RESTART,
  ID_RESTART_FORCE,
  ID_ICONIFY,
  ID_LOGIN
};

/*** Global Structure ***/
struct Global
{
  // pointers first
  APTR                     SharedMemPool; // MEMF_SHARED memory pool
  Object *                 App;
  Object *                 NewMailSound_Obj;
  Object *                 SplashWinObject;
  Object *                 AboutWinObject;
  Object *                 UpdateNotifyWinObject;
  Object *                 ReadToolbarCacheObject;
  Object *                 WriteToolbarCacheObject;
  Object *                 AbookToolbarCacheObject;
  Object *                 transferWindowObject;
  char *                   ER_Message[MAXERR];
  struct DiskObject *      HideIcon;
  struct AppIcon *         AppIcon;
  struct MsgPort *         AppPort;
  struct MsgPort *         writeWinNotifyPort;
  struct RexxHost *        RexxHost;
  struct FileReqCache *    FileReqCache[ASL_MAX];
  struct Locale *          Locale;
  struct Catalog *         Catalog;
  struct MA_ClassData *    MA;
  struct CO_ClassData *    CO;
  struct AB_ClassData *    AB;
  struct EA_ClassData *    EA[MAXEA];
//  struct TR_ClassData *    TR;
  struct ER_ClassData *    ER;
  struct FI_ClassData *    FI;
  struct FO_ClassData *    FO;
  struct DI_ClassData *    DI;
  struct US_ClassData *    US;
  struct ReadMailData *    ActiveRexxRMData;
  struct WriteMailData *   ActiveRexxWMData;
  struct codeset *         readCharset;
  struct codeset *         writeCharset;
  struct codesetList *     codesetsList;
  struct HashTable *       imageCacheHashTable;
  struct FolderList *      folders;

  #if defined(__amigaos4__)
  struct MsgPort *         AppLibPort;
  #endif

  LONG                     Weights[12];

  int                      PGPVersion;
  int                      CO_DST;
  int                      ER_NumErr;
  enum IconImages          currentAppIcon;
  #if defined(__amigaos4__)
  enum IconImages          LastIconID;
  #endif
  time_t                   LastPGPUsage;

  #if defined(__amigaos4__)
  uint32                   applicationID;
  #endif

  BOOL                     Error;
  BOOL                     PGP5;
  BOOL                     AppIconQuiet;
  BOOL                     PGPPassVolatile;
  BOOL                     CO_Valid;
  BOOL                     TR_Debug;
  BOOL                     TR_UseableTLS;
  BOOL                     InStartupPhase;
  BOOL                     NoImageWarning;
  BOOL                     NoCatalogTranslation;
  BOOL                     DefIconsAvailable;

  struct DateStamp         StartDate;
  struct Users             Users;
  struct RuleResult        RuleResults;
  struct DownloadResult    LastDL;
  struct MinList           readMailDataList;
  struct MinList           writeMailDataList;
  struct MinList           xpkPackerList;
  struct MinList           zombieFileList;
  struct Theme             theme;
  struct TokenAnalyzer     spamFilter;
  struct Timers            timerData;

  // for managing TCP/IP transfers we are
  // having a queue in which TransferNodes are being
  // placed and processed upon request.
  struct MinList           transferQueue;
  struct TransferNode *    activeTransfer;            // the currently active Transfer
  struct HashTable *       uidlHashTable;             // for maintaining all UIDLs
  BOOL                     uidlHashIsDirty;           // did the Hash change sind last reading it?
  enum ImportFormat        importFormat;              // for using a "TransferNode" for importing mails
  char                     importFile[SIZE_PATHFILE]; // file to import
  struct Folder *          importFolder;              // in which folder to import
  BOOL                     mailChecking;              // true while checking for new mails (POP3)


  // the data for our thread implementation
  struct Thread            mainThread;     // the main thread
  struct MinList           subThreadList;  // list of subthreads

  char                     ProgDir[SIZE_PATH];
  char                     ProgName[SIZE_FILE];
  char                     PGPPassPhrase[SIZE_DEFAULT];
  char                     MA_MailDir[SIZE_PATH];
  char                     AB_Filename[SIZE_PATHFILE];
  char                     CO_PrefsFile[SIZE_PATHFILE];
  char                     DI_Filename[SIZE_PATHFILE];
};

extern struct Global *G;

struct xpkPackerNode
{
  struct MinNode node;
  struct XpkPackerInfo info;
};

void PopUp(void);
BOOL StayInProg(void);

#endif /* MAIN_YAM_H */

