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

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if !defined(__amigaos4__)
#include <clib/alib_protos.h>
#endif

#include <exec/execbase.h>
#include <libraries/amisslmaster.h>
#include <libraries/asl.h>
#include <mui/BetterString_mcc.h>
#include <mui/NListview_mcc.h>
#include <mui/NFloattext_mcc.h>
#include <mui/TextEditor_mcc.h>
#include <mui/TheBar_mcc.h>
#include <mui/NBalance_mcc.h>
#include <proto/amissl.h>
#include <proto/amisslmaster.h>
#include <proto/codesets.h>
#include <proto/datatypes.h>
#include <proto/diskfont.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/icon.h>
#include <proto/iffparse.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <proto/layers.h>
#include <proto/locale.h>
#include <proto/muimaster.h>
#include <proto/openurl.h>
#include <proto/rexxsyslib.h>
#include <proto/timer.h>
#include <proto/utility.h>
#include <proto/wb.h>
#include <proto/xadmaster.h>
#include <proto/xpkmaster.h>
#include <proto/expat.h>

#if defined(__amigaos4__)
#include <proto/application.h>
#include <proto/timezone.h>
#endif

#if !defined(__amigaos4__)
#include <proto/cybergraphics.h>
#endif

#include "extrasrc.h"

#include "NewReadArgs.h"
#include "SDI_hook.h"

#include "YAM.h"
#include "YAM_addressbook.h"
#include "YAM_config.h"
#include "YAM_configFile.h"
#include "YAM_folderconfig.h"
#include "YAM_global.h"
#include "YAM_main.h"
#include "YAM_mainFolder.h"
#include "YAM_read.h"
#include "YAM_write.h"
#include "YAM_utilities.h"

#include "AppIcon.h"
#include "BayesFilter.h"
#include "DockyIcon.h"
#include "FileInfo.h"
#include "MUIObjects.h"
#include "FolderList.h"
#include "ImageCache.h"
#include "Locale.h"
#include "MailList.h"
#include "Requesters.h"
#include "Rexx.h"
#include "Threads.h"
#include "Timer.h"
#include "UpdateCheck.h"

#include "mui/Classes.h"

#include "Debug.h"

/***************************************************************************
 Module: Root
***************************************************************************/

struct Global *G = NULL;

struct Args
{
  char  *user;
  char  *password;
  char  *maildir;
  char  *prefsfile;
  LONG   nocheck;
  LONG   hide;
  LONG   debug;
  char  *mailto;
  char  *subject;
  char  *letter;
  char **attach;
  LONG   noImgWarning;
  LONG   noCatalog;
};

static struct NewRDArgs nrda;
static struct Args args;
static BPTR olddirlock = -1; /* -1 is an unset indicator */

/**************************************************************************/

static void Abort(const char *message, ...);

/**************************************************************************/

// AutoDST related variables
enum ADSTmethod { ADST_NONE=0, ADST_TZLIB, ADST_SETDST, ADST_FACTS, ADST_SGUARD, ADST_IXGMT };
static const char *const ADSTfile[] = { "", "ENV:TZONE", "ENV:TZONE", "ENV:FACTS/DST", "ENV:SUMMERTIME", "ENV:IXGMTOFFSET" };
static struct ADST_Data
{
  struct NotifyRequest nRequest;
  enum ADSTmethod method;
} ADSTdata;

// Semaphore related suff
static struct StartupSemaphore
{
  struct SignalSemaphore semaphore; // a standard semaphore structure
  ULONG UseCount;                   // how many other participants know this semaphore
} *startupSemaphore = NULL;

#define STARTUP_SEMAPHORE_NAME      "YAM_Startup"

/*** Library/MCC check routines ***/
/// InitLib
//  Opens a library & on OS4 also the interface
#if defined(__amigaos4__)
static BOOL InitLib(const char *libname,
                    ULONG version,
                    ULONG revision,
                    struct Library **libbase,
                    const char *iname,
                    struct Interface **iface,
                    BOOL required,
                    const char *homepage)
#else
static BOOL InitLib(const char *libname,
                    ULONG version,
                    ULONG revision,
                    struct Library **libbase,
                    BOOL required,
                    const char *homepage)
#endif
{
  struct Library *base = NULL;

  ENTER();

  #if defined(__amigaos4__)
  if(libbase != NULL && iface != NULL)
  #else
  if(libbase != NULL)
  #endif
  {
    // open the library base
    base = OpenLibrary(libname, version);

    if(base != NULL && revision != 0)
    {
      if(base->lib_Version == version && base->lib_Revision < revision)
      {
        CloseLibrary(base);
        base = NULL;
      }
    }

    // if we end up here, we can open the OS4 base library interface
    if(base != NULL)
    {
      #if defined(__amigaos4__)
      struct Interface *i;

      // if we weren't able to obtain the interface, lets close the library also
      if(GETINTERFACE(iname, i, base) == NULL)
      {
        D(DBF_STARTUP, "InitLib: can't get '%s' interface of library %s", iname, libname);

        CloseLibrary(base);
        *libbase = NULL;
        base = NULL;
      }
      else
        D(DBF_STARTUP, "InitLib: library %s v%ld.%ld with iface '%s' successfully opened.", libname, base->lib_Version, base->lib_Revision, iname);

      // store interface pointer
      *iface = i;
      #else
      D(DBF_STARTUP, "InitLib: library %s v%ld.%ld successfully opened.", libname, base->lib_Version, base->lib_Revision);
      #endif
    }
    else
      D(DBF_STARTUP, "InitLib: can't open library %s with minimum version v%ld.%ld", libname, version, revision);

    if(base == NULL && required == TRUE)
    {
      if(homepage != NULL)
      {
        char error[SIZE_LINE];
        LONG answer;

        snprintf(error, sizeof(error), tr(MSG_ER_LIB_URL), libname, version, revision, homepage);

        if(MUIMasterBase != NULL && G != NULL && G->App != NULL)
        {
          answer = MUI_Request(NULL, NULL, 0L, tr(MSG_ErrorStartup), OpenURLBase != NULL ? tr(MSG_HOMEPAGE_QUIT_GAD) : tr(MSG_Quit), error);
        }
        else if(IntuitionBase != NULL)
        {
          struct EasyStruct ErrReq;

          ErrReq.es_StructSize   = sizeof(struct EasyStruct);
          ErrReq.es_Flags        = 0;
          ErrReq.es_Title        = (STRPTR)tr(MSG_ErrorStartup);
          ErrReq.es_TextFormat   = error;
          ErrReq.es_GadgetFormat = OpenURLBase != NULL ? (STRPTR)tr(MSG_HOMEPAGE_QUIT_GAD) : (STRPTR)tr(MSG_Quit);

          answer = EasyRequestArgs(NULL, &ErrReq, NULL, NULL);
        }
        else
        {
          puts(error);
          answer = 0;
        }

        // visit the home page if the user requested that
        if(answer == 1)
          GotoURL(homepage, FALSE);

        Abort(NULL);
      }
      else
        Abort(tr(MSG_ER_LIB), libname, version, revision);
    }

    // store base
    *libbase = base;
  }

  RETURN((BOOL)(base != NULL));
  return (BOOL)(base != NULL);
}
///
/// CheckMCC
//  Checks if a certain version of a MCC is available
static BOOL CheckMCC(const char *name, ULONG minver, ULONG minrev, BOOL req, const char *url)
{
  BOOL success = FALSE;
  BOOL flush = TRUE;

  ENTER();

  D(DBF_STARTUP, "checking for v%ld.%ld+ of '%s'", minver, minrev, name);

  for(;;)
  {
    // First we attempt to acquire the version and revision through MUI
    Object *obj;

    if((obj = MUI_NewObject(name, TAG_DONE)) != NULL)
    {
      ULONG ver;
      ULONG rev;
      struct Library *base;
      char libname[256];

      ver = xget(obj, MUIA_Version);
      rev = xget(obj, MUIA_Revision);

      MUI_DisposeObject(obj);

      if(ver > minver || (ver == minver && rev >= minrev))
      {
        D(DBF_STARTUP, "%s v%ld.%ld found through MUIA_Version/Revision", name, ver, rev);

        success = TRUE;
        break;
      }

      // If we did't get the version we wanted, let's try to open the
      // libraries ourselves and see what happens...
      snprintf(libname, sizeof(libname), "PROGDIR:mui/%s", name);

      if((base = OpenLibrary(&libname[8], 0)) != NULL || (base = OpenLibrary(&libname[0], 0)) != NULL)
      {
        UWORD openCnt = base->lib_OpenCnt;

        ver = base->lib_Version;
        rev = base->lib_Revision;

        CloseLibrary(base);

        // we add some additional check here so that eventual broken .mcc also have
        // a chance to pass this test (e.g. _very_ old versions of Toolbar.mcc are broken)
        if(ver > minver || (ver == minver && rev >= minrev))
        {
          D(DBF_STARTUP, "%s v%ld.%ld found through OpenLibrary()", name, ver, rev);

          success = TRUE;
          break;
        }

        if(openCnt > 1)
        {
          if(req == TRUE)
          {
            LONG answer;

            answer = MUI_Request(NULL, NULL, 0L, tr(MSG_ErrorStartup), OpenURLBase != NULL ? tr(MSG_RETRY_HOMEPAGE_QUIT_GAD) : tr(MSG_RETRY_QUIT_GAD), tr(MSG_ER_MCC_IN_USE), name, minver, minrev, ver, rev, url);
            if(answer == 0)
            {
              // cancel
              break;
            }
            else if(answer == 1)
            {
              // flush and retry
              flush = TRUE;
            }
            else
            {
              // visit the home page if it is known but bail out nevertheless
              GotoURL(url, FALSE);
              break;
            }
          }
          else
            break;
        }

        // Attempt to flush the library if open count is 0 or because the
        // user wants to retry (meaning there's a chance that it's 0 now)
        if(flush == TRUE)
        {
          struct Library *result;

          Forbid();
          if((result = (struct Library *)FindName(&((struct ExecBase *)SysBase)->LibList, name)) != NULL)
            RemLibrary(result);
          Permit();
          flush = FALSE;
        }
        else
        {
          E(DBF_STARTUP, "%s: couldn't find minimum required version.", name);

          // We're out of luck - open count is 0, we've tried to flush
          // and still haven't got the version we want
          if(req == TRUE)
          {
            LONG answer;

            answer = MUI_Request(NULL, NULL, 0L, tr(MSG_ErrorStartup), OpenURLBase != NULL ? tr(MSG_RETRY_HOMEPAGE_QUIT_GAD) : tr(MSG_RETRY_QUIT_GAD), tr(MSG_ER_MCC_OLD), name, minver, minrev, ver, rev, url);
            if(answer == 0)
            {
              // cancel
              break;
            }
            else if(answer == 1)
            {
              // flush and retry
              flush = TRUE;
            }
            else
            {
              // visit the home page if it is known but bail out nevertheless
              GotoURL(url, FALSE);
              break;
            }
          }
          else
            break;
        }
      }
    }
    else
    {
      LONG answer;

      // No MCC at all - no need to attempt flush
      flush = FALSE;
      answer = MUI_Request(NULL, NULL, 0L, tr(MSG_ErrorStartup), OpenURLBase != NULL ? tr(MSG_RETRY_HOMEPAGE_QUIT_GAD) : tr(MSG_RETRY_QUIT_GAD), tr(MSG_ER_NO_MCC), name, minver, minrev, url);

      if(answer == 0)
      {
        // cancel
        break;
      }
      else if(answer == 2)
      {
        // visit the home page if it is known but bail out nevertheless
        GotoURL(url, FALSE);
        break;
      }
    }

  }

  if(success == FALSE && req == TRUE)
    exit(RETURN_ERROR); // Ugly

  RETURN(success);
  return success;
}
///

/*** Auto-DST management routines ***/
/// ADSTnotify_start
//  AutoDST Notify start function
static BOOL ADSTnotify_start(void)
{
  if(ADSTdata.method != ADST_NONE)
  {
    // prepare the NotifyRequest structure
    BYTE signalAlloc;

    if((signalAlloc = AllocSignal(-1)) >= 0)
    {
      struct NotifyRequest *nr = &ADSTdata.nRequest;

      nr->nr_Name  = (STRPTR)ADSTfile[ADSTdata.method];
      nr->nr_Flags = NRF_SEND_SIGNAL;

      // prepare the nr_Signal now
      nr->nr_stuff.nr_Signal.nr_Task      = FindTask(NULL);
      nr->nr_stuff.nr_Signal.nr_SignalNum = signalAlloc;

      return StartNotify(nr);
    }
    else
    {
      memset(&ADSTdata, 0, sizeof(struct ADST_Data));
    }
  }

  return FALSE;
}

///
/// ADSTnotify_stop
//  AutoDST Notify stop function
static void ADSTnotify_stop(void)
{
  if(ADSTdata.method != ADST_NONE)
  {
    // stop the NotifyRequest
    struct NotifyRequest *nr = &ADSTdata.nRequest;

    if(nr->nr_Name != NULL)
    {
      EndNotify(nr);
      FreeSignal((LONG)nr->nr_stuff.nr_Signal.nr_SignalNum);
    }
  }
}

///
/// GetDST
//  Checks if daylight saving time is active
//  return 0 if no DST system was found
//         1 if no DST is set
//         2 if DST is set (summertime)
static int GetDST(BOOL update)
{
  char buffer[50];
  char *tmp;
  int result = 0;

  ENTER();

  // prepare the NotifyRequest structure
  if(update == FALSE)
    memset(&ADSTdata, 0, sizeof(struct ADST_Data));

  // lets check the DaylightSaving stuff now
  // we check in the following order:
  //
  // 1. timezone.library (AmigaOS4 only)
  // 2. SetDST (ENV:TZONE)
  // 3. FACTS (ENV:FACTS/DST)
  // 4. SummertimeGuard (ENV:SUMMERTIME)
  // 5. ixemul (ENV:IXGMTOFFSET)

  #if defined(__amigaos4__)
  // check via timezone.library in case we are compiled for AmigaOS4
  if((update == FALSE || ADSTdata.method == ADST_TZLIB))
  {
    if(INITLIB("timezone.library", 52, 1, &TimezoneBase, "main", &ITimezone, TRUE, NULL))
    {
      BYTE dstSetting = TFLG_UNKNOWN;

      // retrieve the current DST setting
      if(GetTimezoneAttrs(NULL, TZA_TimeFlag, &dstSetting, TAG_DONE) && dstSetting != TFLG_UNKNOWN)
      {
        if(dstSetting == TFLG_ISDST)
          result = 2;
        else
          result = 1;

        D(DBF_STARTUP, "Found timezone.library with DST flag: %ld", result);

        ADSTdata.method = ADST_TZLIB;
      }

      CLOSELIB(TimezoneBase, ITimezone);
    }
  }
  #endif

  // SetDST saves the DST settings in the TZONE env-variable which
  // is a bit more complex than the others, so we need to do some advance parsing
  if((update == FALSE || ADSTdata.method == ADST_SETDST) && result == 0
     && GetVar((STRPTR)&ADSTfile[ADST_SETDST][4], buffer, sizeof(buffer), 0) >= 3)
  {
    int i;

    for(i=0; buffer[i]; i++)
    {
      if(result == 0)
      {
        // if we found the time difference in the TZONE variable we at least found a correct TZONE file
        if(buffer[i] >= '0' && buffer[i] <= '9')
          result = 1;
      }
      else if(isalpha(buffer[i]))
        result = 2; // if it is followed by a alphabetic sign we are in DST mode
    }

    D(DBF_STARTUP, "Found '%s' (SetDST) with DST flag: %ld", ADSTfile[ADST_SETDST], result);

    ADSTdata.method = ADST_SETDST;
  }

  // FACTS saves the DST information in a ENV:FACTS/DST env variable which will be
  // Hex 00 or 01 to indicate the DST value.
  if((update == FALSE || ADSTdata.method == ADST_FACTS) && result == 0
     && GetVar((STRPTR)&ADSTfile[ADST_FACTS][4], buffer, sizeof(buffer), GVF_BINARY_VAR) > 0)
  {
    ADSTdata.method = ADST_FACTS;

    if(buffer[0] == 0x01)
      result = 2;
    else if(buffer[0] == 0x00)
      result = 1;

    D(DBF_STARTUP, "Found '%s' (FACTS) with DST flag: %ld", ADSTfile[ADST_FACTS], result);
  }

  // SummerTimeGuard sets the last string to "YES" if DST is actually active
  if((update == FALSE || ADSTdata.method == ADST_SGUARD) && result == 0
     && GetVar((STRPTR)&ADSTfile[ADST_SGUARD][4], buffer, sizeof(buffer), 0) > 3 && (tmp = strrchr(buffer, ':')))
  {
    ADSTdata.method = ADST_SGUARD;

    if(tmp[1] == 'Y')
      result = 2;
    else if(tmp[1] == 'N')
      result = 1;

    D(DBF_STARTUP, "Found '%s' (SGUARD) with DST flag: %ld", ADSTfile[ADST_SGUARD], result);
  }

  // ixtimezone sets the fifth byte in the IXGMTOFFSET variable to 01 if
  // DST is actually active.
  if((update == FALSE || ADSTdata.method == ADST_IXGMT) && result == 0
     && GetVar((STRPTR)&ADSTfile[ADST_IXGMT][4], buffer, sizeof(buffer), GVF_BINARY_VAR) >= 4)
  {
    ADSTdata.method = ADST_IXGMT;

    if(buffer[4] == 0x01)
      result = 2;
    else if(buffer[4] == 0x00)
      result = 1;

    D(DBF_STARTUP, "Found '%s' (IXGMT) with DST flag: %ld", ADSTfile[ADST_IXGMT], result);
  }

  if(update == FALSE && result == 0)
  {
    ADSTdata.method = ADST_NONE;

    W(DBF_STARTUP, "Didn't find any AutoDST facility active!");
  }

  // No correctly installed AutoDST tool was found
  // so lets return zero.
  RETURN(result);
  return result;
}
///

/*** XPK Packer initialization routines ***/
/// InitXPKPackerList()
// initializes the internal XPK PackerList
static BOOL InitXPKPackerList(void)
{
  BOOL result = FALSE;
  LONG error = 0;

  ENTER();

  if(XpkBase != NULL)
  {
    struct XpkPackerList xpl;

    if((error = XpkQueryTags(XPK_PackersQuery, &xpl, TAG_DONE)) == 0)
    {
      struct XpkPackerInfo xpi;
      unsigned int i;

      D(DBF_XPK, "Loaded XPK Packerlist: %ld packers found", xpl.xpl_NumPackers);

      for(i=0; i < xpl.xpl_NumPackers; i++)
      {
        if((error = XpkQueryTags(XPK_PackMethod, xpl.xpl_Packer[i], XPK_PackerQuery, &xpi, TAG_DONE)) == 0)
        {
          struct xpkPackerNode *newPacker;

          if((newPacker = memdup(&xpi, sizeof(struct xpkPackerNode))) != NULL)
          {
            // because the short name isn't always equal to the packer short name
            // we work around that problem and make sure they are equal.
            strlcpy((char *)newPacker->info.xpi_Name, (char *)xpl.xpl_Packer[i], sizeof(newPacker->info.xpi_Name));

            D(DBF_XPK, "Found XPKPacker: %ld: [%s] = '%s' flags = %08lx", i, xpl.xpl_Packer[i], newPacker->info.xpi_Name, newPacker->info.xpi_Flags);

            // add the new packer to our internal list.
            AddTail((struct List *)&G->xpkPackerList, (struct Node *)newPacker);

            result = TRUE;
          }
        }
        else
        {
          // something failed, so lets query the error!
          #if defined(DEBUG)
          char buf[1024];

          XpkFault(error, NULL, buf, sizeof(buf));

          E(DBF_XPK, "Error on XpkQuery() of packer '%s': '%s'", xpl.xpl_Packer[i], buf);
          #endif

          result = FALSE;
        }
      }
    }
    else
    {
      // something failed, so lets query the error!
      #if defined(DEBUG)
      char buf[1024];

      XpkFault(error, NULL, buf, sizeof(buf));

      E(DBF_XPK, "Error on general XpkQuery(): '%s'", buf);
      #endif
    }
  }

  RETURN((BOOL)(result == TRUE && error == 0));
  return (BOOL)(result == TRUE && error == 0);
}

///
/// FreeXPKPackerList()
// free all content of our previously loaded XPK packer list
static void FreeXPKPackerList(void)
{
  ENTER();

  if(IsListEmpty((struct List *)&G->xpkPackerList) == FALSE)
  {
    struct MinNode *curNode;

    // Now we process the read header to set all flags accordingly
    while((curNode = (struct MinNode *)RemHead((struct List *)&G->xpkPackerList)) != NULL)
    {
      // free everything of the node
      free(curNode);
    }
  }

  LEAVE();
}

///

/*** Synchronization routines ***/
/// CreateStartupSemaphore
//  create a new startup semaphore or find an old instance
static struct StartupSemaphore *CreateStartupSemaphore(void)
{
  struct StartupSemaphore *semaphore;

  ENTER();

  D(DBF_STARTUP, "creating startup semaphore...");

  // we have to disable multitasking before looking for an old instance with the same name
  Forbid();
  if((semaphore = (struct StartupSemaphore *)FindSemaphore((STRPTR)STARTUP_SEMAPHORE_NAME)) != NULL)
  {
    // the semaphore already exists, so just bump the counter
    semaphore->UseCount++;
  }
  Permit();

  // if we didn't find any semaphore with that name we generate a new one
  if(semaphore == NULL)
  {
    // allocate the memory for the semaphore system structure itself
    if((semaphore = AllocSysObjectTags(ASOT_SEMAPHORE,
                                       ASOSEM_Size,     sizeof(struct StartupSemaphore),
                                       ASOSEM_Name,     (ULONG)STARTUP_SEMAPHORE_NAME,
                                       ASOSEM_CopyName, TRUE,
                                       ASOSEM_Public,   TRUE,
                                       TAG_DONE)) != NULL)
    {
      // initialize the semaphore structure and start with a use counter of 1
      semaphore->UseCount = 1;
    }
  }

  RETURN(semaphore);
  return semaphore;
}
///
/// DeleteStartupSemaphore
//  delete a public semaphore, removing it from the system if it is no longer in use
static void DeleteStartupSemaphore(void)
{
  ENTER();

  if(startupSemaphore != NULL)
  {
    // first obtain the semaphore so that nobody else can interfere
    ObtainSemaphore(&startupSemaphore->semaphore);

    // protect access to the semaphore
    Forbid();

    // now we can release the semaphore again, because nobody else can steal it
    ReleaseSemaphore(&startupSemaphore->semaphore);

    // one user less for this semaphore
    startupSemaphore->UseCount--;

    // if nobody else uses this semaphore it can be removed complete
    if(startupSemaphore->UseCount == 0)
    {
      // free the semaphore structure
      // for OS4 this will also remove our public semaphore from the list
      FreeSysObject(ASOT_SEMAPHORE, startupSemaphore);
      startupSemaphore = NULL;
    }

    // free access to the semaphore (FreeVecPooled may have
    // released it already anyway)
    Permit();
  }

  LEAVE();
}
///

/*** Application Abort/Termination routines ***/
/// Terminate
//  Deallocates used memory and MUI modules and terminates
static void Terminate(void)
{
  int i;

  ENTER();

  D(DBF_STARTUP, "freeing spam filter module...");
  BayesFilterCleanup();

  D(DBF_STARTUP, "freeing config module...");
  if(G->CO != NULL)
  {
    CO_ClearConfig(CE);
    free(CE);
    CE = NULL;

    DisposeModule(&G->CO);
  }

  D(DBF_STARTUP, "freeing addressbook entries...");
  for(i = 0; i < MAXEA; i++)
    DisposeModule(&G->EA[i]);

  D(DBF_STARTUP, "freeing readmailData...");
  // cleanup the still existing readmailData objects
  if(IsListEmpty((struct List *)&G->readMailDataList) == FALSE)
  {
    // search through our ReadDataList
    struct MinNode *curNode;

    for(curNode = G->readMailDataList.mlh_Head; curNode->mln_Succ;)
    {
      struct ReadMailData *rmData = (struct ReadMailData *)curNode;

      // already iterate to the next node as the cleanup
      // will free the memory area
      curNode = curNode->mln_Succ;

      CleanupReadMailData(rmData, TRUE);
    }
  }

  D(DBF_STARTUP, "freeing writemailData...");
  // cleanup the still existing writemailData objects
  if(IsListEmpty((struct List *)&G->writeMailDataList) == FALSE)
  {
    // search through our WriteMailDataList
    struct MinNode *curNode;

    for(curNode = G->writeMailDataList.mlh_Head; curNode->mln_Succ;)
    {
      struct WriteMailData *wmData = (struct WriteMailData *)curNode;

      // already iterate to the next node as the cleanup
      // will free the memory area
      curNode = curNode->mln_Succ;

      CleanupWriteMailData(wmData);
    }
  }

  D(DBF_STARTUP, "freeing tcp/ip stuff...");
  if(G->TR != NULL)
  {
    TR_Cleanup();
    TR_CloseTCPIP();
    DisposeModule(&G->TR);
  }

  if(G->FO != NULL)
    DisposeModule(&G->FO);

  if(G->FI != NULL)
    DisposeModule(&G->FI);

  if(G->ER != NULL)
    DisposeModule(&G->ER);

  if(G->US != NULL)
    DisposeModule(&G->US);

  D(DBF_STARTUP, "finalizing indexes and closing main window...");
  if(G->MA != NULL)
  {
    MA_UpdateIndexes(FALSE);
    set(G->MA->GUI.WI, MUIA_Window_Open, FALSE);

    // delete our list of folders
    D(DBF_STARTUP, "freeing folders...");
    LockFolderList(G->folders);
    if(IsFolderListEmpty(G->folders) == FALSE)
    {
      struct FolderNode *fnode;

      ForEachFolderNode(G->folders, fnode)
      {
        FO_FreeFolder(fnode->folder);
        fnode->folder = NULL;
      }
    }
    UnlockFolderList(G->folders);
    DeleteFolderList(G->folders);
  }

  D(DBF_STARTUP, "freeing addressbook module...");
  if(G->AB != NULL)
    DisposeModule(&G->AB);

  D(DBF_STARTUP, "freeing main window module...");
  if(G->MA != NULL)
    DisposeModule(&G->MA);

  D(DBF_STARTUP, "freeing FileReqCache structures...");
  for(i = 0; i < ASL_MAX; i++)
  {
    struct FileReqCache *frc;

    if((frc = G->FileReqCache[i]) != NULL)
    {
      FreeFileReqCache(frc);
      free(frc);
    }
  }

  FreeAppIcon();

  D(DBF_STARTUP, "freeing write window file notify port...");
  if(G->writeWinNotifyPort != NULL)
    FreeSysObject(ASOT_PORT, G->writeWinNotifyPort);

  D(DBF_STARTUP, "freeing Arexx port...");
  if(G->RexxHost != NULL)
    CloseDownARexxHost(G->RexxHost);

  D(DBF_STARTUP, "freeing timer resources...");
  CleanupTimers();

  // stop the AutoDST notify
  D(DBF_STARTUP, "stoping ADSTnotify...");
  ADSTnotify_stop();

  // check if we have an allocated NewMailSound_Obj and dispose it.
  D(DBF_STARTUP, "freeing newmailsound object...");
  if(G->NewMailSound_Obj != NULL)
    DisposeDTObject(G->NewMailSound_Obj);

  D(DBF_STARTUP, "freeing hideIcon...");
  if(G->HideIcon != NULL)
    FreeDiskObject(G->HideIcon);

  D(DBF_STARTUP, "deleting zombie files...");
  if(DeleteZombieFiles(FALSE) == FALSE)
  {
    BOOL ignore = FALSE;

    do
    {
      if(MUI_Request(G->App, NULL, MUIF_NONE, tr(MSG_ER_ZOMBIE_FILES_EXIST_TITLE),
                                              tr(MSG_ER_ZOMBIE_FILES_EXIST_BT),
                                              tr(MSG_ER_ZOMBIE_FILES_EXIST)) == 0)
      {
        ignore = TRUE;
      }
    }
    while(DeleteZombieFiles(ignore) == FALSE);
  }

  // we unregister the application from application.library
  FreeDockyIcon();

  D(DBF_STARTUP, "freeing toolbar cache...");
  ToolbarCacheCleanup();

  D(DBF_STARTUP, "freeing config...");
  CO_ClearConfig(C);
  free(C);
  C = NULL;

  // free our private codesets list
  D(DBF_STARTUP, "freeing private codesets list...");
  if(G->codesetsList != NULL)
  {
    CodesetsListDelete(CSA_CodesetList, G->codesetsList,
                       TAG_DONE);

    G->codesetsList = NULL;
  }

  // free our private internal XPK PackerList
  D(DBF_STARTUP, "cleaning up XPK stuff...");
  FreeXPKPackerList();
  CLOSELIB(XpkBase, IXpk);

  // free our xad stuff
  D(DBF_STARTUP, "cleaning up XAD stuff...");
  CLOSELIB(xadMasterBase, IxadMaster);

  D(DBF_STARTUP, "freeing main application object...");
  if(G->App != NULL)
    MUI_DisposeObject(G->App);

  D(DBF_STARTUP, "unloading/freeing theme images...");
  FreeTheme(&G->theme);

  D(DBF_STARTUP, "freeing image cache...");
  ImageCacheCleanup();

  D(DBF_STARTUP, "freeing internal MUI classes...");
  YAM_CleanupClasses();

  D(DBF_STARTUP, "deleting semaphore...");
  DeleteStartupSemaphore();

  D(DBF_STARTUP, "cleaning up thread system...");
  CleanupThreads();

  // cleaning up all AmiSSL stuff
  D(DBF_STARTUP, "cleaning up AmiSSL stuff...");
  if(AmiSSLBase != NULL)
  {
    CleanupAmiSSLA(NULL);

    DROPINTERFACE(IAmiSSL);
    CloseAmiSSL();
    AmiSSLBase = NULL;
  }
  CLOSELIB(AmiSSLMasterBase, IAmiSSLMaster);

  // close all libraries now.
  D(DBF_STARTUP, "closing all opened libraries...");
  #if defined(__amigaos4__)
  CLOSELIB(ApplicationBase, IApplication);
  #else
  CLOSELIB(CyberGfxBase,    ICyberGfx);
  #endif
  CLOSELIB(ExpatBase,       IExpat);
  CLOSELIB(CodesetsBase,    ICodesets);
  CLOSELIB(DataTypesBase,   IDataTypes);
  CLOSELIB(MUIMasterBase,   IMUIMaster);
  CLOSELIB(RexxSysBase,     IRexxSys);
  CLOSELIB(IFFParseBase,    IIFFParse);
  CLOSELIB(KeymapBase,      IKeymap);
  CLOSELIB(LayersBase,      ILayers);
  CLOSELIB(WorkbenchBase,   IWorkbench);
  CLOSELIB(GfxBase,         IGraphics);

  // close the catalog and locale now
  D(DBF_STARTUP, "closing catalog...");
  CloseYAMCatalog();
  if(G->Locale != NULL)
    CloseLocale(G->Locale);

  CLOSELIB(LocaleBase, ILocale);

  // make sure to free the shared memory pool before
  // freeing the rest
  FreeSysObject(ASOT_MEMPOOL, G->SharedMemPool);

  // last, but not clear free the global structure
  free(G);
  G = NULL;

  LEAVE();
}
///
/// Abort
//  Shows error requester, then terminates the program
static void Abort(const char *message, ...)
{
  ENTER();

  if(message != NULL)
  {
    va_list a;
    char error[SIZE_LINE];

    va_start(a, message);
    vsnprintf(error, sizeof(error), message, a);
    va_end(a);

    W(DBF_STARTUP, "aborting application due to reason '%s'", error);

    if(MUIMasterBase != NULL && G != NULL && G->App != NULL)
    {
      MUI_Request(G->App, NULL, MUIF_NONE, tr(MSG_ErrorStartup), tr(MSG_Quit), error);
    }
    else if(IntuitionBase != NULL)
    {
      struct EasyStruct ErrReq;

      ErrReq.es_StructSize   = sizeof(struct EasyStruct);
      ErrReq.es_Flags        = 0;
      ErrReq.es_Title        = (STRPTR)tr(MSG_ErrorStartup);
      ErrReq.es_TextFormat   = error;
      ErrReq.es_GadgetFormat = (STRPTR)tr(MSG_Quit);

      EasyRequestArgs(NULL, &ErrReq, NULL, NULL);
    }
    else
      puts(error);
  }
  else
    W(DBF_STARTUP, "aborting application");

  // do a hard exit.
  exit(RETURN_ERROR);

  LEAVE();
}
///
/// yam_exitfunc()
/* This makes it possible to leave YAM without explicitely calling cleanup procedure */
static void yam_exitfunc(void)
{
  ENTER();

  D(DBF_STARTUP, "cleaning up in 'yam_exitfunc'...");

  if(olddirlock != -1)
  {
    Terminate();
    CurrentDir(olddirlock);
  }

  if(nrda.Template != NULL)
    NewFreeArgs(&nrda);

  // close some libraries now
  CLOSELIB(DiskfontBase,   IDiskfont);
  CLOSELIB(UtilityBase,    IUtility);
  CLOSELIB(IconBase,       IIcon);
  CLOSELIB(IntuitionBase,  IIntuition);

  LEAVE();

  // cleanup our debugging system.
  #if defined(DEBUG)
  CleanupDebug();
  #endif
}

///

/// SplashProgress
//  Shows progress of program initialization in the splash window
static void SplashProgress(const char *txt, int percent)
{
  ENTER();

  DoMethod(G->SplashWinObject, MUIM_Splashwindow_StatusChange, txt, percent);

  LEAVE();
}
///
/// PopUp
//  Un-iconify YAM
void PopUp(void)
{
  Object *window = G->MA->GUI.WI;

  ENTER();

  nnset(G->App, MUIA_Application_Iconified, FALSE);

  // avoid MUIA_Window_Open's side effect of activating the window if it was already open
  if(xget(window, MUIA_Window_Open) == FALSE)
    set(window, MUIA_Window_Open, TRUE);

  DoMethod(window, MUIM_Window_ScreenToFront);
  DoMethod(window, MUIM_Window_ToFront);

  // Now we check if there is any read window open and bring it also
  // to the front
  if(IsListEmpty((struct List *)&G->readMailDataList) == FALSE)
  {
    // search through our ReadDataList
    struct MinNode *curNode;

    for(curNode = G->readMailDataList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
    {
      struct ReadMailData *rmData = (struct ReadMailData *)curNode;

      if(rmData->readWindow != NULL)
      {
        DoMethod(rmData->readWindow, MUIM_Window_ToFront);
        window = rmData->readWindow;
      }
    }
  }

  // Now we check if there is any write window open and bring it also
  // to the front
  if(IsListEmpty((struct List *)&G->writeMailDataList) == FALSE)
  {
    // search through our WriteDataList
    struct MinNode *curNode;

    for(curNode = G->writeMailDataList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
    {
      struct WriteMailData *wmData = (struct WriteMailData *)curNode;

      if(wmData->window != NULL)
      {
        DoMethod(wmData->window, MUIM_Window_ToFront);
        window = wmData->window;
      }
    }
  }

  // now we activate the window that is on the top
  set(window, MUIA_Window_Activate, TRUE);

  LEAVE();
}
///
/// DoublestartHook
//  A second copy of YAM was started
HOOKPROTONHNONP(DoublestartFunc, void)
{
  ENTER();

  if(G->App != NULL && G->MA != NULL && G->MA->GUI.WI != NULL)
    PopUp();

  LEAVE();
}
MakeStaticHook(DoublestartHook, DoublestartFunc);

///
/// StayInProg
//  Makes sure that the user really wants to quit the program
BOOL StayInProg(void)
{
  BOOL stayIn = FALSE;

  ENTER();

  if(stayIn == FALSE && G->AB->Modified == TRUE)
  {
    int result;

    result = MUI_Request(G->App, G->MA->GUI.WI, 0, NULL, tr(MSG_ABOOK_MODIFIED_GAD), tr(MSG_AB_Modified));
    switch(result)
    {
      default:
      case 0:
      {
        // dont' quit
        stayIn = TRUE;
      }
      break;

      case 1:
      {
        // save and quit
        CallHookPkt(&AB_SaveABookHook, 0, 0);
      }
      break;

      case 2:
      {
        // quit without save
      }
      break;
    }
  }

  if(stayIn == FALSE && C->ConfigIsSaved == FALSE)
  {
    int result;

    result = MUI_Request(G->App, G->MA->GUI.WI, 0, NULL, tr(MSG_CONFIG_MODIFIED_GAD), tr(MSG_CONFIG_MODIFIED));
    switch(result)
    {
      default:
      case 0:
      {
        // dont' quit
        stayIn = TRUE;
      }
      break;

      case 1:
      {
        // save and quit
        CO_SaveConfig(C, G->CO_PrefsFile);
      }
      break;

      case 2:
      {
        // quit without save
      }
      break;
    }
  }

  if(stayIn == FALSE)
  {
    int i;
    BOOL req = FALSE;

    for(i=0; i < MAXEA && req == FALSE; i++)
    {
      if(G->EA[i] != NULL)
        req = TRUE;
    }

    // check if there exists an active write window
    if(req == FALSE &&
       IsListEmpty((struct List *)&G->writeMailDataList) == FALSE)
    {
      // search through our WriteDataList
      struct MinNode *curNode;

      for(curNode = G->writeMailDataList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
      {
        struct WriteMailData *wmData = (struct WriteMailData *)curNode;

        if(wmData->window != NULL)
        {
          req = TRUE;
          break;
        }
      }
    }

    if(req == TRUE || G->CO != NULL || C->ConfirmOnQuit == TRUE)
    {
      if(MUI_Request(G->App, G->MA->GUI.WI, 0, tr(MSG_MA_ConfirmReq), tr(MSG_YesNoReq), tr(MSG_QuitYAMReq)) == 0)
        stayIn = TRUE;
    }
  }

  RETURN(stayIn);
  return stayIn;
}
///
/// Root_GlobalDispatcher
//  Processes return value of MUI_Application_NewInput
static int Root_GlobalDispatcher(ULONG app_input)
{
  int ret = 0;

  ENTER();

  switch(app_input)
  {
    // user initiated a normal QUIT command
    case MUIV_Application_ReturnID_Quit:
    {
      if(xget(G->App, MUIA_Application_ForceQuit) == FALSE)
        ret = StayInProg() ? 0 : 1;
      else
        ret = 1;
    }
    break;

    // user closed the main window
    case ID_CLOSEALL:
    {
      if(C->IconifyOnQuit == FALSE)
        ret = StayInProg() ? 0 : 1;
      else
        set(G->App, MUIA_Application_Iconified, TRUE);
    }
    break;

    // user initiated a 'restart' action
    case ID_RESTART:
    {
      ret = StayInProg() ? 0 : 2;
    }
    break;


    // the application window was iconfified (either
    // by a user or automatically)
    case ID_ICONIFY:
    {
      MA_UpdateIndexes(FALSE);
    }
    break;
  }

  RETURN(ret);
  return ret;
}
///
/// Root_New
//  Creates MUI application
static BOOL Root_New(BOOL hidden)
{
  BOOL result = FALSE;

  ENTER();

  // make the following operations single threaded
  // MUI chokes if a single task application is created a second time while the first instance is not yet fully created
  ObtainSemaphore(&startupSemaphore->semaphore);

  if((G->App = YAMObject, End) != NULL)
  {
    if(hidden == TRUE)
      set(G->App, MUIA_Application_Iconified, TRUE);

    DoMethod(G->App, MUIM_Notify, MUIA_Application_DoubleStart, TRUE, MUIV_Notify_Application, 2, MUIM_CallHook, &DoublestartHook);
    DoMethod(G->App, MUIM_Notify, MUIA_Application_Iconified, TRUE, MUIV_Notify_Application, 2, MUIM_Application_ReturnID, ID_ICONIFY);

    // create the splash window object and return true if
    // everything worked out fine.
    if((G->SplashWinObject = SplashwindowObject, End) != NULL)
    {
      G->InStartupPhase = TRUE;

      set(G->SplashWinObject, MUIA_Window_Open, !hidden);

      result = TRUE;
    }
    else
      E(DBF_STARTUP, "couldn't create splash window object!");
  }
  else
    E(DBF_STARTUP, "couldnn't create root object!");

  // now a second instance may continue
  ReleaseSemaphore(&startupSemaphore->semaphore);

  RETURN(result);
  return result;
}
///

/// InitAfterLogin
//  Phase 2 of program initialization (after user logs in)
static void InitAfterLogin(void)
{
  struct FolderList *oldfolders = NULL;
  BOOL newfolders;
  BOOL splashWasActive;
  int i;
  char pubScreenName[MAXPUBSCREENNAME + 1];
  struct Screen *pubScreen;

  ENTER();

  // clear the configuration (set defaults) and load it
  // from the user defined .config file
  D(DBF_STARTUP, "loading configuration...");
  SplashProgress(tr(MSG_LoadingConfig), 20);

  if(CO_LoadConfig(C, G->CO_PrefsFile, &oldfolders) == FALSE)
  {
    // clear the config with defaults if the config file couldn't be loaded
    CO_SetDefaults(C, cp_AllPages);
  }
  CO_Validate(C, FALSE);

  // load all necessary graphics/themes
  SplashProgress(tr(MSG_LoadingGFX), 30);

  // load the choosen theme of the user
  LoadTheme(&G->theme, C->ThemeName);

  // make sure we initialize the toolbar Cache which in turn will
  // cause YAM to cache all often used toolbars and their images
  if(ToolbarCacheInit() == FALSE)
    Abort(NULL); // exit the application

  // create all necessary GUI elements
  D(DBF_STARTUP, "creating GUI...");
  SplashProgress(tr(MSG_CreatingGUI), 40);

  // before we go and create the first MUI windows
  // we register the application to application.library
  InitDockyIcon();

  // Create a new Main & Addressbook Window
  if((G->MA = MA_New()) == NULL || (G->AB = AB_New()) == NULL)
     Abort(tr(MSG_ErrorMuiApp));

  // make sure the GUI objects for the embedded read pane are created
  MA_SetupEmbeddedReadPane();

  // Now we have to check on which position we should display the InfoBar and if it's not
  // center or off we have to resort the main group
  if(C->InfoBar != IB_POS_CENTER && C->InfoBar != IB_POS_OFF)
     MA_SortWindow();

  // load the main window GUI layout from the ENV: variable
  LoadLayout();

  SplashProgress(tr(MSG_LoadingFolders), 50);

  newfolders = FALSE;
  if(FO_LoadTree(CreateFilename(".folders")) == FALSE && oldfolders != NULL && IsFolderListEmpty(oldfolders) == FALSE)
  {
    struct FolderNode *fnode;

    // add all YAM 1.x style folders
    ForEachFolderNode(oldfolders, fnode)
    {
      struct Folder *folder = fnode->folder;

      DoMethod(G->MA->GUI.NL_FOLDERS, MUIM_NListtree_Insert, folder->Name, fnode, MUIV_NListtree_Insert_ListNode_Root);
    }

    newfolders = TRUE;
  }

  // free any YAM 1.x style folder
  if(oldfolders != NULL && IsFolderListEmpty(oldfolders) == FALSE)
  {
    struct FolderNode *fnode;

    ForEachFolderNode(oldfolders, fnode)
    {
      free(fnode->folder);
    }

    DeleteFolderList(oldfolders);
  }

  if(FO_GetFolderByType(FT_INCOMING, NULL) == NULL)
    newfolders |= FO_CreateFolder(FT_INCOMING, FolderName[FT_INCOMING], tr(MSG_MA_Incoming));

  if(FO_GetFolderByType(FT_OUTGOING, NULL) == NULL)
    newfolders |= FO_CreateFolder(FT_OUTGOING, FolderName[FT_OUTGOING], tr(MSG_MA_Outgoing));

  if(FO_GetFolderByType(FT_SENT, NULL) == NULL)
    newfolders |= FO_CreateFolder(FT_SENT, FolderName[FT_SENT], tr(MSG_MA_Sent));

  if(FO_GetFolderByType(FT_TRASH, NULL) == NULL)
    newfolders |= FO_CreateFolder(FT_TRASH, FolderName[FT_TRASH], tr(MSG_MA_TRASH));

  if(C->SpamFilterEnabled == TRUE)
  {
    // check if the spam folder has to be created
    if(FO_GetFolderByType(FT_SPAM, NULL) == NULL)
    {
      BOOL createSpamFolder;
      enum FType type;

      if(ObtainFileInfo(CreateFilename(FolderName[FT_SPAM]), FI_TYPE, &type) == TRUE && type == FIT_NONEXIST)
      {
        // no directory named "spam" exists, so let's create it
        createSpamFolder = TRUE;
      }
      else
      {
        // the directory "spam" already exists, but it is not the standard spam folder
        // let the user decide what to do
        ULONG result;

        result = MUI_Request(G->App, NULL, 0, NULL,
                                              tr(MSG_ER_SPAMDIR_EXISTS_ANSWERS),
                                              tr(MSG_ER_SPAMDIR_EXISTS));
        switch(result)
        {
          default:
          case 0:
            // the user has chosen to disable the spam filter, so we do it
            // or the requester was cancelled
            C->SpamFilterEnabled = FALSE;
            createSpamFolder = FALSE;
            break;

          case 1:
            // delete everything in the folder, the directory itself can be kept
            DeleteMailDir(CreateFilename(FolderName[FT_SPAM]), FALSE);
            createSpamFolder = TRUE;
            break;

          case 2:
            // keep the folder contents
            createSpamFolder = TRUE;
            break;
        }
      }

      if(createSpamFolder == TRUE)
      {
        struct Folder *spamFolder;

        // try to remove the existing folder named "spam"
        if((spamFolder = FO_GetFolderByPath(FolderName[FT_SPAM], NULL)) != NULL)
        {
          struct MUI_NListtree_TreeNode *tn;

          if(spamFolder->imageObject != NULL)
          {
            // we make sure that the NList also doesn't use the image in future anymore
            DoMethod(G->MA->GUI.NL_FOLDERS, MUIM_NList_UseImage, NULL, spamFolder->ImageIndex, MUIF_NONE);
            spamFolder->imageObject = NULL;
          }
          if((tn = FO_GetFolderTreeNode(spamFolder)) != NULL)
          {
            // remove the folder from the folder list
            DoMethod(G->MA->GUI.NL_FOLDERS, MUIM_NListtree_Remove, MUIV_NListtree_Insert_ListNode_Root, tn, MUIF_NONE);
          }
        }
        // finally, create the spam folder
        newfolders |= FO_CreateFolder(FT_SPAM, FolderName[FT_SPAM], tr(MSG_MA_SPAM));
      }
    }
  }

  if(newfolders == TRUE)
  {
    set(G->MA->GUI.NL_FOLDERS, MUIA_NListtree_Active, MUIV_NListtree_Active_FirstVisible);
    FO_SaveTree(CreateFilename(".folders"));
  }

  // setup some dynamic (changing) menus
  MA_SetupDynamicMenus();

  // do some initial call to ChangeSelected() for correctly setting up
  // some mail information
  MA_ChangeSelected(TRUE);

  SplashProgress(tr(MSG_RebuildIndices), 60);
  MA_UpdateIndexes(TRUE);

  SplashProgress(tr(MSG_LOADINGUPDATESTATE), 65);
  LoadUpdateState();

  SplashProgress(tr(MSG_LOADINGSPAMTRAININGDATA), 70);
  BayesFilterInit();

  SplashProgress(tr(MSG_LoadingFolders), 75);
  for(i = 0; ;i++)
  {
    struct MUI_NListtree_TreeNode *tn;
    struct MUI_NListtree_TreeNode *tn_parent;
    struct Folder *folder;

    tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->MA->GUI.NL_FOLDERS, MUIM_NListtree_GetEntry, MUIV_NListtree_GetEntry_ListNode_Root, i, MUIF_NONE);
    if(tn == NULL || tn->tn_User == NULL)
      break;

    folder = ((struct FolderNode *)tn->tn_User)->folder;

    // if this entry is a group lets skip here immediatly
    if(isGroupFolder(folder))
      continue;

    if((isIncomingFolder(folder) || isOutgoingFolder(folder) || isTrashFolder(folder) ||
        C->LoadAllFolders == TRUE) && !isProtectedFolder(folder))
    {
      // call the getIndex function which on one hand loads the full .index file
      // and makes sure that all "new" mail is marked to unread if the user
      // enabled the C->UpdateNewMail option in the configuration.
      MA_GetIndex(folder);
    }
    else if(folder->LoadedMode != LM_VALID)
    {
      // do not load the full index, do load only the header of the .index
      // which summarizes everything
      folder->LoadedMode = MA_LoadIndex(folder, FALSE);

      // if the user wishs to make sure all "new" mail is flagged as
      // read upon start we go through our folders and make sure they show
      // no "new" mail, even if their .index file is not fully loaded
      if(C->UpdateNewMail == TRUE && folder->LoadedMode == LM_FLUSHED)
        folder->New = 0;
    }

    // if this folder hasn't got any own folder image in the folder
    // directory and it is one of our standard folders we have to check which image we put in front of it
    if(folder->imageObject == NULL)
    {
      if(isIncomingFolder(folder))      folder->ImageIndex = (folder->Unread != 0) ? FICON_ID_INCOMING_NEW : FICON_ID_INCOMING;
      else if(isOutgoingFolder(folder)) folder->ImageIndex = (folder->Unread != 0) ? FICON_ID_OUTGOING_NEW : FICON_ID_OUTGOING;
      else if(isTrashFolder(folder))    folder->ImageIndex = (folder->Unread != 0) ? FICON_ID_TRASH_NEW : FICON_ID_TRASH;
      else if(isSentFolder(folder))     folder->ImageIndex = FICON_ID_SENT;
      else if(isSpamFolder(folder))     folder->ImageIndex = (folder->Unread != 0) ? FICON_ID_SPAM_NEW : FICON_ID_SPAM;
      else folder->ImageIndex = -1;
    }

    // now we have to add the amount of mails of this folder to the foldergroup
    // aswell and also the grandparents.
    while((tn_parent = (struct MUI_NListtree_TreeNode *)DoMethod(G->MA->GUI.NL_FOLDERS, MUIM_NListtree_GetEntry, tn, MUIV_NListtree_GetEntry_Position_Parent, MUIF_NONE)) != NULL)
    {
      // tn_parent->tn_User is NULL then it's ROOT and we have to skip here
      // because we cannot have a status of the ROOT tree.
      if(tn_parent->tn_User != NULL)
      {
        struct Folder *fo_parent = ((struct FolderNode *)tn_parent->tn_User)->folder;

        fo_parent->Unread    += folder->Unread;
        fo_parent->New       += folder->New;
        fo_parent->Total     += folder->Total;
        fo_parent->Sent      += folder->Sent;
        fo_parent->Deleted   += folder->Deleted;

        // for the next step we set tn to the current parent so that we get the
        // grandparents ;)
        tn = tn_parent;
      }
      else
        break;
    }

    DoMethod(G->App, MUIM_Application_InputBuffered);
  }

  // Now we have to make sure that the current folder is really in "active" state
  // or we risk to get a unsynced message listview.
  MA_ChangeFolder(NULL, TRUE);

  SplashProgress(tr(MSG_LoadingABook), 90);
  AB_LoadTree(G->AB_Filename, FALSE, FALSE);
  if((G->RexxHost = SetupARexxHost("YAM", NULL)) == NULL)
     Abort(tr(MSG_ErrorARexx));

  SplashProgress(tr(MSG_OPENGUI), 100);
  G->InStartupPhase = FALSE;

  // Lock the screen that YAM has opened its splash window on to prevent this screen
  // from closing after the splash window is closed and before the main window is opened.
  // This is necessary if YAM is running on its own screen instead of the Workbench.
  GetPubScreenName((struct Screen *)xget(G->SplashWinObject, MUIA_Window_Screen), pubScreenName, sizeof(pubScreenName));
  pubScreen = LockPubScreen(pubScreenName);

  // close the splash window right before we open our main YAM window
  // but ask it before closing if it was activated or not.
  splashWasActive = xget(G->SplashWinObject, MUIA_Window_Activate);
  set(G->SplashWinObject, MUIA_Window_Open, FALSE);

  // cleanup the splash window object immediately
  DoMethod(G->App, OM_REMMEMBER, G->SplashWinObject);
  MUI_DisposeObject(G->SplashWinObject);
  G->SplashWinObject = NULL;

  // Only activate the main window if the about window is active and open it immediatly.
  // We always start YAM with Window_Open=TRUE or else the hide functionality does not work as expected.
  xset(G->MA->GUI.WI,
       MUIA_Window_Activate, splashWasActive,
       MUIA_Window_Open,     TRUE);

  // unlock the public screen again now that the main window is open
  UnlockPubScreen(pubScreenName, pubScreen);

  LEAVE();
}
///
/// InitBeforeLogin
//  Phase 1 of program initialization (before user logs in)
static void InitBeforeLogin(BOOL hidden)
{
  int i;

  ENTER();

  // lets save the current date/time in our startDate value
  DateStamp(&G->StartDate);

  // initialize the random number seed.
  srand((unsigned int)GetDateStamp());

  // First open locale.library, so we can display a translated error requester
  // in case some of the other libraries can't be opened.
  if(INITLIB("locale.library", 38, 0, &LocaleBase, "main", &ILocale, TRUE, NULL))
    G->Locale = OpenLocale(NULL);

  // Now load the catalog of YAM
  if(G->NoCatalogTranslation == FALSE && OpenYAMCatalog() == FALSE)
    Abort(NULL);

  // load&initialize all required libraries
  // first all system relevant libraries
  INITLIB("graphics.library",      36, 0, &GfxBase,       "main", &IGraphics,  TRUE,  NULL);
  INITLIB("layers.library",        39, 0, &LayersBase,    "main", &ILayers,    TRUE,  NULL);
  INITLIB("workbench.library",     36, 0, &WorkbenchBase, "main", &IWorkbench, TRUE,  NULL);
  INITLIB("keymap.library",        36, 0, &KeymapBase,    "main", &IKeymap,    TRUE,  NULL);
  INITLIB("iffparse.library",      36, 0, &IFFParseBase,  "main", &IIFFParse,  TRUE,  NULL);
  INITLIB(RXSNAME,                 36, 0, &RexxSysBase,   "main", &IRexxSys,   TRUE,  NULL);
  INITLIB("datatypes.library",     39, 0, &DataTypesBase, "main", &IDataTypes, TRUE,  NULL);

  // try to open the cybergraphics.library on non-OS4 systems as on OS4 we use the
  // new graphics.library functions instead.
  #if !defined(__amigaos4__)
  INITLIB("cybergraphics.library", 40, 0, &CyberGfxBase,  "main", &ICyberGfx,  FALSE, NULL);
  #endif

  // try to open MUI 3.8+
  INITLIB("muimaster.library",     19, 0, &MUIMasterBase, "main", &IMUIMaster, TRUE, "http://www.sasg.com/");

  // openurl.library has a homepage, but providing that homepage without having OpenURL
  // installed would result in a paradoxon, because InitLib() would provide a button
  // to visit the URL which in turn requires OpenURL to be installed...
  // Hence we try to open openurl.library without
  INITLIB("openurl.library",        1, 0, &OpenURLBase,   "main", &IOpenURL,   FALSE, NULL);

  // try to open the mandatory codesets.library
  INITLIB("codesets.library",       6, 6, &CodesetsBase,  "main", &ICodesets,  TRUE, "http://www.sf.net/projects/codesetslib/");

  // try to open expat.library for our XML import stuff
  INITLIB("expat.library", XML_MAJOR_VERSION, 0, &ExpatBase, "main", &IExpat, FALSE, NULL);

  // we check for the amisslmaster.library v3 accordingly
  if(INITLIB("amisslmaster.library", AMISSLMASTER_MIN_VERSION, 5, &AmiSSLMasterBase, "main", &IAmiSSLMaster, FALSE, NULL))
  {
    if(InitAmiSSLMaster(AMISSL_CURRENT_VERSION, TRUE))
    {
      if((AmiSSLBase = OpenAmiSSL()) != NULL &&
         GETINTERFACE("main", IAmiSSL, AmiSSLBase))
      {
        G->TR_UseableTLS = TRUE;

        D(DBF_STARTUP, "successfully opened AmiSSL library.");
      }
    }
  }

  // now we try to open the application.library which is part of OS4
  // and will be used to notify YAM of certain events and also manage
  // the docky icon accordingly.
  #if defined(__amigaos4__)
  INITLIB("application.library", 50, 0, &ApplicationBase, "application", &IApplication, FALSE, NULL);
  #endif

  // Lets check for the correct TheBar.mcc version
  CheckMCC(MUIC_TheBar,     26, 2, TRUE, "http://www.sf.net/projects/thebar/");
  CheckMCC(MUIC_TheBarVirt, 26, 2, TRUE, "http://www.sf.net/projects/thebar/");
  CheckMCC(MUIC_TheButton,  26, 2, TRUE, "http://www.sf.net/projects/thebar/");

  // Lets check for the correct BetterString.mcc version
  CheckMCC(MUIC_BetterString, 11, 15, TRUE, "http://www.sf.net/projects/bstring-mcc/");

  // we also make sure the user uses the latest brand of all other NList classes, such as
  // NListview, NFloattext etc.
  CheckMCC(MUIC_NList,      20, 121, TRUE, "http://www.sf.net/projects/nlist-classes/");
  CheckMCC(MUIC_NListview,  19,  76, TRUE, "http://www.sf.net/projects/nlist-classes/");
  CheckMCC(MUIC_NFloattext, 19,  57, TRUE, "http://www.sf.net/projects/nlist-classes/");
  CheckMCC(MUIC_NListtree,  18,  28, TRUE, "http://www.sf.net/projects/nlist-classes/");
  CheckMCC(MUIC_NBalance,   15,  2,  TRUE, "http://www.sf.net/projects/nlist-classes/");

  // Lets check for the correct TextEditor.mcc version
  CheckMCC(MUIC_TextEditor, 15, 27, TRUE, "http://www.sf.net/projects/texteditor-mcc/");

  // initialize the thread system of YAM
  if(InitThreads() == FALSE)
    Abort(tr(MSG_ERROR_THREADS));

  // now we search through PROGDIR:Charsets and load all user defined
  // codesets via codesets.library
  G->codesetsList = CodesetsListCreateA(NULL);

  // create a public semaphore which can be used to single thread certain actions
  if((startupSemaphore = CreateStartupSemaphore()) == NULL)
    Abort(tr(MSG_ER_CANNOT_CREATE_SEMAPHORE));

  // try to find out if DefIcons is running or not by querying
  // the Port of DefIcons. Alternatively the Ambient desktop
  // should provide the same functionallity.
  Forbid();
  G->DefIconsAvailable = (FindPort("DEFICONS") != NULL || FindPort("AMBIENT") != NULL);
  Permit();

  // Initialise and Setup our own MUI custom classes before we go on
  D(DBF_STARTUP, "setup internal MUI classes...");
  if(YAM_SetupClasses() == FALSE)
    Abort(tr(MSG_ErrorClasses));

  // allocate the MUI root object and popup the progress/about window
  D(DBF_STARTUP, "creating root object...");
  if(Root_New(hidden) == FALSE)
  {
    BOOL activeYAM;

    Forbid();
    activeYAM = (FindPort("YAM") != NULL);
    Permit();

    Abort(activeYAM ? NULL : tr(MSG_ErrorMuiApp));
  }

  // signal that we are loading our libraries
  D(DBF_STARTUP, "init libraries...");
  SplashProgress(tr(MSG_InitLibs), 10);

  // try to open the xadmaster.library v12.1+ as this is the somewhat
  // most recent version publically available
  INITLIB(XADNAME, 12, 1, &xadMasterBase, "main", &IxadMaster, FALSE, NULL);

  // try to open xpkmaster.library v5.0+ as this is somewhat the most
  // stable version available. Previous version might have some issues
  // as documented in our FAQ.
  INITLIB(XPKNAME, 5, 0, &XpkBase, "main", &IXpk, FALSE, NULL);
  InitXPKPackerList();

  // initialize our timers
  if(InitTimers() == FALSE)
    Abort(tr(MSG_ErrorTimer));

  // initialize our ASL FileRequester cache stuff
  for(i = 0; i < ASL_MAX; i++)
  {
    if((G->FileReqCache[i] = calloc(sizeof(struct FileReqCache), 1)) == NULL)
      Abort(NULL);
  }

  // initialize the AppIcon related stuff
  if(InitAppIcon() == FALSE)
    Abort(NULL);

  // initialize the write window file nofifications
  if((G->writeWinNotifyPort = AllocSysObjectTags(ASOT_PORT, TAG_DONE)) == NULL)
    Abort(NULL);

  LEAVE();
}
///
/// SendWaitingMail
//  Sends pending mail on startup
static BOOL SendWaitingMail(BOOL hideDisplay, BOOL skipSend)
{
  struct Folder *fo;
  BOOL sendableMail = FALSE;

  ENTER();

  if((fo = FO_GetFolderByType(FT_OUTGOING, NULL)) != NULL)
  {
    LockMailListShared(fo->messages);

    if(IsMailListEmpty(fo->messages) == FALSE)
    {
      struct MailNode *mnode;

      ForEachMailNode(fo->messages, mnode)
      {
        if(!hasStatusHold(mnode->mail) && !hasStatusError(mnode->mail))
        {
          sendableMail = TRUE;
          break;
        }
      }
    }

    UnlockMailList(fo->messages);

    // in case the folder contains
    // mail which could be sent, we ask the
    // user what to do with it
    if(sendableMail == TRUE &&
       (hideDisplay == FALSE && xget(G->App, MUIA_Application_Iconified) == FALSE))
    {
      // change the folder first so that the user
      // might have a look at the mails
      MA_ChangeFolder(fo, TRUE);

      // now ask the user for permission to send the mail.
      sendableMail = MUI_Request(G->App, G->MA->GUI.WI, 0, NULL, tr(MSG_YesNoReq), tr(MSG_SendStartReq));
    }
  }

  if(skipSend == FALSE && sendableMail == TRUE)
    MA_Send(SEND_ALL_USER);

  RETURN(sendableMail);
  return(sendableMail);
}
///
/// DoStartup
//  Performs different checks/cleanup operations on startup
static void DoStartup(BOOL nocheck, BOOL hide)
{
  static char lastUserName[SIZE_NAME] = "";
  char *currentUserName = NULL;
  struct User *currentUser;

  ENTER();

  // display the AppIcon now because if non of the functions below
  // does it it could happen that no AppIcon will be displayed at all.
  UpdateAppIcon();

  // execute the startup stuff only if the user changed upon a restart or if
  // we start for the first time
  if((currentUser = US_GetCurrentUser()) != NULL)
    currentUserName = currentUser->Name;

  // we must compare the names here instead of the IDs, because the IDs will
  // change upon every restart
  if(currentUserName != NULL && strcmp(lastUserName, currentUserName) != 0)
  {
    // if the user wishs to delete all old mail during startup of YAM,
    // we do it now
    if(C->CleanupOnStartup == TRUE)
      DoMethod(G->App, MUIM_CallHook, &MA_DeleteOldHook);

    // if the user wants to clean the trash upon starting YAM, do it
    if(C->RemoveOnStartup == TRUE)
      DoMethod(G->App, MUIM_CallHook, &MA_DeleteDeletedHook, FALSE);

    // check for current birth days in our addressbook if the user
    // selected it
    if(C->CheckBirthdates == TRUE && nocheck == FALSE && hide == FALSE)
      AB_CheckBirthdates();

    // the rest of the startup jobs require a running TCP/IP stack,
    // so check if it is properly running.
    if(nocheck == FALSE && TR_IsOnline() == TRUE)
    {
      enum GUILevel mode;

      mode = (C->PreSelection == PSM_NEVER || hide == TRUE) ? POP_START : POP_USER;

      if(C->GetOnStartup == TRUE && C->SendOnStartup == TRUE)
      {
        // check whether there is mail to be sent and the user allows us to send it
        if(SendWaitingMail(hide, TRUE) == TRUE)
        {
          // do a complete mail exchange, the order depends on the user settings
          MA_ExchangeMail(mode);
          // the delayed closure of any transfer window is already handled in MA_ExchangeMail()
        }
        else
        {
          // just get new mail
          MA_PopNow(mode, -1);
          // let MUI execute the delayed disposure of the POP3 transfer window
          DoMethod(G->App, MUIM_Application_InputBuffered);
        }
      }
      else if(C->GetOnStartup == TRUE)
      {
        MA_PopNow(mode, -1);
        // let MUI execute the delayed disposure of the POP3 transfer window
        DoMethod(G->App, MUIM_Application_InputBuffered);
      }
      else if(C->SendOnStartup == TRUE)
      {
        SendWaitingMail(hide, FALSE);
        // let MUI execute the delayed disposure of the SMTP transfer window
        DoMethod(G->App, MUIM_Application_InputBuffered);
      }
    }
  }

  // remember the current user name for a possible restart
  strlcpy(lastUserName, currentUserName, sizeof(lastUserName));

  LEAVE();
}
///
/// Login
//  Log in a given user or prompt for user and password
static void Login(const char *user, const char *password,
                  const char *maildir, const char *prefsfile)
{
  ENTER();

  if(US_Login(user, password, maildir, prefsfile) == FALSE)
  {
    E(DBF_STARTUP, "terminating due to incorrect login information");
    exit(RETURN_WARN);
  }

  LEAVE();
}
///

/*** Command-Line Argument parsing routines ***/
/// ParseCommandArgs()
//
static LONG ParseCommandArgs(void)
{
  LONG result = 0;
  char *extHelp;

  ENTER();

  // clear the args structure
  memset(&args, 0, sizeof(struct Args));

  // allocate some memory for the extended help
  #define SIZE_EXTHELP  2048
  if((extHelp = malloc(SIZE_EXTHELP)) != NULL)
  {
    // set argument template
    nrda.Template = (STRPTR)"USER/K,"
                            "PASSWORD/K,"
                            "MAILDIR/K,"
                            "PREFSFILE/K,"
                            "NOCHECK/S,"
                            "HIDE/S,"
                            "DEBUG/S,"
                            "MAILTO/K,"
                            "SUBJECT/K,"
                            "LETTER/K,"
                            "ATTACH/M,"
                            "NOIMGWARNING/S,"
                            "NOCATALOG/S";

    // now we build an extended help page text
    snprintf(extHelp, SIZE_EXTHELP, "%s (%s)\n%s\n\nUsage: YAM <options>\nOptions/Tooltypes:\n"
                                    "  USER=<username>     : Selects the active YAM user and skips\n"
                                    "                        the login process.\n"
                                    "  PASSWORD=<password> : Password of selected user (if required).\n"
                                    "  MAILDIR=<path>      : Sets the home directory for the folders\n"
                                    "                        and configuration.\n"
                                    "  PREFSFILE=<filename>: Configuration file that should be used\n"
                                    "                        instead of the default.\n"
                                    "  NOCHECK             : Starts YAM without trying to receive/send\n"
                                    "                        any mail.\n"
                                    "  HIDE                : Starts YAM in iconify mode.\n"
                                    "  DEBUG               : Sends all conversations between YAM and a\n"
                                    "                        mail server to the console window.\n"
                                    "  MAILTO=<recipient>  : Creates a new mail for the specified\n"
                                    "                        recipients when YAM started.\n"
                                    "  SUBJECT=<subject>   : Sets the subject text for a new mail.\n"
                                    "  LETTER=<file>       : The text file containing the actual mail\n"
                                    "                        text of a new message.\n"
                                    "  ATTACH=<file>       : Attaches the specified file to the new\n"
                                    "                        mail created.\n"
                                    "  NOIMGWARNING        : Supresses all warnings regarding missing\n"
                                    "                        image files.\n"
                                    "  NOCATALOG           : Starts YAM without loading any catalog\n"
                                    "                        translation (english).\n"
                                    "\n%s: ", yamversion,
                                              yamversiondate,
                                              yamcopyright,
                                              nrda.Template);

    // set the extHelp pointer
    nrda.ExtHelp = (STRPTR)extHelp;

    // set rest of new read args structure elements
    nrda.Window = NULL;
    nrda.Parameters = (LONG *)&args;
    nrda.FileParameter = -1;
    nrda.PrgToolTypesOnly = FALSE;

    // now call NewReadArgs to parse all our commandline/tooltype arguments in accordance
    // to the above template
    result = NewReadArgs(WBmsg, &nrda);

    free(extHelp);
    nrda.ExtHelp = NULL;
  }

  RETURN(result);
  return result;
}

///

/*** main entry function ***/
/// main()
//  Program entry point, main loop
int main(int argc, char **argv)
{
  BOOL yamFirst;
  BPTR progdir;
  LONG err;

  // obtain the MainInterface of Exec before anything else.
  #ifdef __amigaos4__
  IExec = (struct ExecIFace *)((struct ExecBase *)SysBase)->MainInterface;

  // check the exec version first and force be at least an 52.2 version
  // from AmigaOS4 final. This should assure we are are using the very
  // latest stable version.
  if(SysBase->lib_Version < 52 ||
     (SysBase->lib_Version == 52 && SysBase->lib_Revision < 2))
  {
    if((IntuitionBase = (APTR)OpenLibrary("intuition.library", 36)) != NULL &&
       GETINTERFACE("main", IIntuition, IntuitionBase))
    {
      struct EasyStruct ErrReq;

      ErrReq.es_StructSize = sizeof(struct EasyStruct);
      ErrReq.es_Flags      = 0;
      ErrReq.es_Title        = (STRPTR)"YAM Startup Error";
      ErrReq.es_TextFormat   = (STRPTR)"This version of YAM requires at least\n"
                                       "an AmigaOS4 kernel version 52.2";
      ErrReq.es_GadgetFormat = (STRPTR)"Exit";

      EasyRequestArgs(NULL, &ErrReq, NULL, NULL);

      CLOSELIB(IntuitionBase, IIntuition);
    }

    exit(RETURN_WARN);
  }
  #endif

  // we make sure that if this is a build for 68k processors and for 68020+
  // that this is really a 68020+ machine
  #if _M68060 || _M68040 || _M68030 || _M68020 || __mc68020 || __mc68030 || __mc68040 || __mc68060
  if((SysBase->AttnFlags & AFF_68020) == 0)
  {
    if((IntuitionBase = (APTR)OpenLibrary("intuition.library", 36)) != NULL)
    {
      struct EasyStruct ErrReq;

      ErrReq.es_StructSize = sizeof(struct EasyStruct);
      ErrReq.es_Flags      = 0;
      ErrReq.es_Title        = (STRPTR)"YAM Startup Error";
      ErrReq.es_TextFormat   = (STRPTR)"This version of YAM requires at\n"
                                       "least an 68020 processor or higher.";
      ErrReq.es_GadgetFormat = (STRPTR)"Exit";

      EasyRequestArgs(NULL, &ErrReq, NULL, NULL);

      CloseLibrary((struct Library *)IntuitionBase);
   }

   exit(RETURN_WARN);
  }
  #endif


#if defined(DEVWARNING)
  {
    BOOL goon = TRUE;

    if((IntuitionBase = (APTR)OpenLibrary("intuition.library", 36)) != NULL &&
       GETINTERFACE("main", IIntuition, IntuitionBase))
    {
      if((UtilityBase = (APTR)OpenLibrary("utility.library", 36)) != NULL &&
         GETINTERFACE("main", IUtility, UtilityBase))
      {
        char var;
        struct EasyStruct ErrReq;
        struct DateStamp ds;
        DateStamp(&ds); // get actual time/date

        ErrReq.es_StructSize = sizeof(struct EasyStruct);
        ErrReq.es_Flags      = 0;

        #if defined(EXPDATE)
        if(EXPDATE <= ds.ds_Days)
        {
          ErrReq.es_Title        = (STRPTR)"YAM Developer Version Expired!";
          ErrReq.es_TextFormat   = (STRPTR)"This developer version of YAM has expired!\n\n"
                                   "Please note that you may download a new, updated\n"
                                   "version from the YAM nightly build page at:\n\n"
                                   "http://nightly.yam.ch/\n\n"
                                   "All developer versions will automatically expire\n"
                                   "after a certian time interval. This is to insure\n"
                                   "that no old versions are floating around causing\n"
                                   "users to report bugs on old versions.\n\n"
                                   "Thanks for your help in improving YAM!";
          if((OpenURLBase = (APTR)OpenLibrary("openurl.library", 1)) != NULL &&
             GETINTERFACE("main", IOpenURL, OpenURLBase))
          {
            ErrReq.es_GadgetFormat = (STRPTR)"Visit homepage|Exit";
          }
          else
            ErrReq.es_GadgetFormat = (STRPTR)"Exit";

          DisplayBeep(NULL);
          if(EasyRequestArgs(NULL, &ErrReq, NULL, NULL) == 1)
          {
            // visit YAM's nightly build page and exit
            GotoURL("http://nightly.yam.ch/", FALSE);
          }

          CLOSELIB(OpenURLBase, IOpenURL);
          goon = FALSE;
        }
        #endif

        if(goon == TRUE && GetVar("I_KNOW_YAM_IS_UNDER_DEVELOPMENT", &var, sizeof(var), 0) == -1)
        {
          LONG answer;

          ErrReq.es_Title        = (STRPTR)"YAM Developer Snapshot Warning!";
          ErrReq.es_TextFormat   = (STRPTR)"This is just an *internal* developer snapshot\n"
                                           "version of YAM. It is not recommended or intended\n"
                                           "for general use as it may contain bugs that can\n"
                                           "lead to any loss of data. No regular support\n"
                                           "for this version is provided.\n\n"
                                           #if defined(EXPDATE)
                                           "In addition, this version will automatically\n"
                                           "expire after a certain time interval.\n\n"
                                           #endif
                                           "So, if you're unsure and prefer to have a stable\n"
                                           "installation instead of a potentially dangerous\n"
                                           "version, please consider to use the current\n"
                                           "stable release version available from:\n\n"
                                           "http://www.yam.ch/\n\n"
                                           "Thanks for your help in improving YAM!";

          if((OpenURLBase = (APTR)OpenLibrary("openurl.library", 1)) != NULL &&
             GETINTERFACE("main", IOpenURL, OpenURLBase))
          {
            ErrReq.es_GadgetFormat = (STRPTR)"Go on|Visit homepage|Exit";
          }
          else
            ErrReq.es_GadgetFormat = (STRPTR)"Go on|Exit";

          DisplayBeep(NULL);
          answer = EasyRequestArgs(NULL, &ErrReq, NULL, NULL);
          if(answer == 0)
          {
            // exit YAM
            goon = FALSE;
          }
          else if(answer == 2)
          {
            // visit YAM's home page and continue normally
            GotoURL("http://www.yam.ch/", FALSE);
          }

          CLOSELIB(OpenURLBase, IOpenURL);
        }
      }

      CLOSELIB(UtilityBase, IUtility);
    }

    CLOSELIB(IntuitionBase, IIntuition);
    if(goon == FALSE)
      exit(RETURN_WARN);
  }
#endif

  // initialize our debugging system.
  #if defined(DEBUG)
  SetupDebug();
  #endif

  // signal that on a exit() the 'yam_exitfunc' function
  // should be called.
  atexit(yam_exitfunc);

  WBmsg = (struct WBStartup *)(0 == argc ? argv : NULL);

  INITLIB("intuition.library", 36, 0, &IntuitionBase, "main", &IIntuition, TRUE, NULL);
  INITLIB("icon.library",      36, 0, &IconBase,      "main", &IIcon,      TRUE, NULL);
  INITLIB("utility.library",   36, 0, &UtilityBase,   "main", &IUtility,   TRUE, NULL);
  INITLIB("diskfont.library",  37, 0, &DiskfontBase,  "main", &IDiskfont,  TRUE, NULL);

  // now we parse the command-line arguments
  if((err = ParseCommandArgs()) != 0)
  {
    PrintFault(err, "YAM");

    SetIoErr(err);
    exit(RETURN_ERROR);
  }

  // security only, can happen for residents only
  if((progdir = GetProgramDir()) == (BPTR)0)
    exit(RETURN_ERROR);

  olddirlock = CurrentDir(progdir);

  for(yamFirst=TRUE;;)
  {
    ULONG signals;
    ULONG timsig;
    ULONG adstsig;
    ULONG rexxsig;
    ULONG appsig;
    ULONG applibsig;
    ULONG writeWinNotifySig;
    struct User *user;
    int ret;

    // allocate our global G and C structures
    if((G = calloc(1, sizeof(struct Global))) == NULL ||
       (C = calloc(1, sizeof(struct Config))) == NULL)
    {
      // break out immediately to signal an error!
      break;
    }

    // create the MEMF_SHARED memory pool we use for our
    // own AllocVecPooled() allocations later on
    if((G->SharedMemPool = AllocSysObjectTags(ASOT_MEMPOOL,
                                              ASOPOOL_MFlags,    MEMF_SHARED|MEMF_CLEAR,
                                              ASOPOOL_Puddle,    2048,
                                              ASOPOOL_Threshold, 1024,
                                              ASOPOOL_Name,      (ULONG)"YAM Shared Pool",
                                              TAG_DONE)) == NULL)
    {
      // break out immediately to signal an error!
      break;
    }

    // create a list for all the folders
    if((G->folders = CreateFolderList()) == NULL)
      break;

    // prepare the exec lists in G and C
    NewList((struct List *)&(C->mimeTypeList));
    NewList((struct List *)&(C->filterList));
    NewList((struct List *)&(G->readMailDataList));
    NewList((struct List *)&(G->writeMailDataList));
    NewList((struct List *)&(G->xpkPackerList));
    NewList((struct List *)&(G->zombieFileList));

    // get the PROGDIR: and program name and put it into own variables
    NameFromLock(progdir, G->ProgDir, sizeof(G->ProgDir));
    if(WBmsg != NULL && WBmsg->sm_NumArgs > 0)
    {
      strlcpy(G->ProgName, (char *)WBmsg->sm_ArgList[0].wa_Name, sizeof(G->ProgName));
    }
    else
    {
      char buf[SIZE_PATHFILE];

      GetProgramName((STRPTR)&buf[0], sizeof(buf));
      strlcpy(G->ProgName, (char *)FilePart(buf), sizeof(G->ProgName));
    }

    D(DBF_STARTUP, "ProgDir.: '%s'", G->ProgDir);
    D(DBF_STARTUP, "ProgName: '%s'", G->ProgName);

    if(args.maildir == NULL)
      strlcpy(G->MA_MailDir, G->ProgDir, sizeof(G->MA_MailDir));

    G->TR_Debug = args.debug ? TRUE : FALSE;
    G->TR_Socket = TCP_NO_SOCKET;
    G->TR_Allow = TRUE;
    G->CO_DST = GetDST(FALSE);
    G->NoImageWarning = args.noImgWarning ? TRUE : FALSE;
    G->NoCatalogTranslation = args.noCatalog ? TRUE : FALSE;

    // setup our ImageCache
    ImageCacheSetup();

    if(yamFirst == TRUE)
    {
      InitBeforeLogin(args.hide ? TRUE : FALSE);
      Login(args.user, args.password, args.maildir, args.prefsfile);
      InitAfterLogin();
    }
    else
    {
      InitBeforeLogin(FALSE);
      Login(NULL, NULL, NULL, NULL);
      InitAfterLogin();
    }

    DoMethod(G->App, MUIM_Application_Load, MUIV_Application_Load_ENVARC);
    AppendToLogfile(LF_ALL, 0, tr(MSG_LOG_Started));
    MA_StartMacro(MACRO_STARTUP, NULL);

    // let us check for the existance of .autosaveXX.txt files
    // and war the user accordingly if there exists such an
    // autosave file.
    CheckForAutoSaveFiles();

    if(yamFirst == TRUE)
    {
      struct WriteMailData *wmData;

      DoStartup(args.nocheck ? TRUE : FALSE, args.hide ? TRUE : FALSE);

      if((args.mailto != NULL || args.letter != NULL || args.subject != NULL || args.attach != NULL) &&
         (wmData = NewWriteMailWindow(NULL, 0)) != NULL)
      {
        if(args.mailto != NULL)
          set(wmData->window, MUIA_WriteWindow_To, args.mailto);

        if(args.subject != NULL)
          set(wmData->window, MUIA_WriteWindow_Subject, args.subject);

        if(args.letter != NULL)
          DoMethod(wmData->window, MUIM_WriteWindow_LoadText, args.letter, FALSE);

        if(args.attach != NULL)
        {
          char **sptr;

          for(sptr = args.attach; *sptr; sptr++)
          {
            LONG size;

            if(ObtainFileInfo(*sptr, FI_SIZE, &size) == TRUE && size > 0)
              DoMethod(wmData->window, MUIM_WriteWindow_AddAttachment, *sptr, NULL, FALSE);
          }
        }
      }

      yamFirst = FALSE;
    }
    else
    {
      DoStartup(args.nocheck ? TRUE : FALSE, FALSE);
    }

    user = US_GetCurrentUser();
    AppendToLogfile(LF_NORMAL, 1, tr(MSG_LOG_LoggedIn), user->Name);
    AppendToLogfile(LF_VERBOSE, 2, tr(MSG_LOG_LoggedInVerbose), user->Name, G->CO_PrefsFile, G->MA_MailDir);

    // Now start the NotifyRequest for the AutoDST file
    if(ADSTnotify_start() == TRUE)
      adstsig = 1UL << ADSTdata.nRequest.nr_stuff.nr_Signal.nr_SignalNum;
    else
      adstsig = 0;

    // prepare all signal bits
    timsig            = (1UL << G->timerData.port->mp_SigBit);
    rexxsig           = (1UL << G->RexxHost->port->mp_SigBit);
    appsig            = (1UL << G->AppPort->mp_SigBit);
    applibsig         = DockyIconSignal();
    writeWinNotifySig = (1UL << G->writeWinNotifyPort->mp_SigBit);

    D(DBF_STARTUP, "YAM allocated signals:");
    D(DBF_STARTUP, " adstsig           = %08lx", adstsig);
    D(DBF_STARTUP, " timsig            = %08lx", timsig);
    D(DBF_STARTUP, " rexxsig           = %08lx", rexxsig);
    D(DBF_STARTUP, " appsig            = %08lx", appsig);
    D(DBF_STARTUP, " applibsig         = %08lx", applibsig);
    D(DBF_STARTUP, " writeWinNotifySig = %08lx", writeWinNotifySig);

    // start our maintanance Timer requests for
    // different purposes (writeindexes/mailcheck/autosave)
    PrepareTimer(TIMER_WRINDEX,   C->WriteIndexes, 0);
    PrepareTimer(TIMER_CHECKMAIL, C->CheckMailDelay*60, 0);
    PrepareTimer(TIMER_AUTOSAVE,  C->AutoSave, 0);
    PrepareTimer(TIMER_SPAMFLUSHTRAININGDATA, C->SpamFlushTrainingDataInterval, 0);
    StartTimer(TIMER_WRINDEX);
    StartTimer(TIMER_CHECKMAIL);
    StartTimer(TIMER_AUTOSAVE);
    StartTimer(TIMER_SPAMFLUSHTRAININGDATA);

    // initialize the automatic UpdateCheck facility and schedule an
    // automatic update check during startup if necessary
    InitUpdateCheck(TRUE);

    // start the event loop
    signals = 0;
    while((ret = Root_GlobalDispatcher(DoMethod(G->App, MUIM_Application_NewInput, &signals))) == 0)
    {
      if(signals != 0)
      {
        signals = Wait(signals | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_D | SIGBREAKF_CTRL_F | timsig | rexxsig | appsig | applibsig | adstsig | writeWinNotifySig);

        if(isFlagSet(signals, SIGBREAKF_CTRL_C))
        {
          ret = 1;
          break;
        }

        if(isFlagSet(signals, SIGBREAKF_CTRL_D))
        {
          ret = 0;
          break;
        }

        if(isFlagSet(signals, SIGBREAKF_CTRL_F))
          PopUp();

        // check for a Timer event
        if(isFlagSet(signals, timsig))
        {
          #if defined(DEBUG)
          char dateString[64];

          DateStamp2String(dateString, sizeof(dateString), NULL, DSS_DATETIME, TZC_NONE);
          D(DBF_TIMER, "timer signal received @ %s", dateString);
          #endif

          // call ProcessTimerEvent() to check all our
          // timers are process accordingly.
          ProcessTimerEvent();
        }

        // check for a Arexx signal
        if(isFlagSet(signals, rexxsig))
          ARexxDispatch(G->RexxHost);

        // check for a AppMessage signal
        if(isFlagSet(signals, appsig))
          HandleAppIcon();

        #if defined(__amigaos4__)
        if(isFlagSet(signals, applibsig))
        {
          // make sure to break out here in case
          // the Quit or ForceQuit succeeded.
          if(HandleDockyIcon() == TRUE)
            break;
        }
        #endif

        // check for a write window file notification signal
        if(isFlagSet(signals, writeWinNotifySig))
        {
          struct NotifyMessage *msg;

          while((msg = (struct NotifyMessage *)GetMsg(G->writeWinNotifyPort)) != NULL)
          {
            // the messages UserData field contains the WriteWindow object
            // which triggered the notification
            Object *writeWin;

            if((writeWin = (Object *)msg->nm_NReq->nr_UserData) != NULL)
            {
              DoMethod(writeWin, MUIM_WriteWindow_MailFileModified);
            }

            ReplyMsg((struct Message *)msg);
          }
        }

        // check for the AutoDST signal
        if(adstsig != 0 && isFlagSet(signals, adstsig))
        {
          // check the DST file and validate the configuration once more.
          G->CO_DST = GetDST(TRUE);
          CO_Validate(C, FALSE);
        }
      }
    }

    if(C->SendOnQuit == TRUE && args.nocheck == FALSE && TR_IsOnline() == TRUE)
      SendWaitingMail(FALSE, FALSE);

    if(C->CleanupOnQuit == TRUE)
      DoMethod(G->App, MUIM_CallHook, &MA_DeleteOldHook);

    if(C->RemoveOnQuit == TRUE)
      DoMethod(G->App, MUIM_CallHook, &MA_DeleteDeletedHook, TRUE);

    AppendToLogfile(LF_ALL, 99, tr(MSG_LOG_Terminated));
    MA_StartMacro(MACRO_QUIT, NULL);

    // if the user really wants to exit, do it now as Terminate() is broken !
    if(ret == 1)
    {
      // Create the shutdown window object, but only show it if the application is visible, too.
      // This window will be closed and disposed automatically as soon as the application itself
      // is disposed.
      if(G->App != NULL && xget(G->App, MUIA_Application_Iconified) == FALSE)
        ShutdownWindowObject, End;

      SetIoErr(RETURN_OK);
      exit(RETURN_OK);
    }

    D(DBF_STARTUP, "Restart issued");

    // prepare for restart
    Terminate();
  }

  /* not reached */
  SetIoErr(RETURN_OK);
  return RETURN_OK;
}
///

