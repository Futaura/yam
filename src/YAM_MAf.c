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

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <clib/alib_protos.h>
#include <clib/macros.h>
#include <libraries/gadtools.h>
#include <libraries/iffparse.h>
#include <mui/NList_mcc.h>
#include <mui/NListtree_mcc.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>

#include "extrasrc.h"

#include "SDI_hook.h"

#include "YAM.h"
#include "YAM_config.h"
#include "YAM_error.h"
#include "YAM_folderconfig.h"
#include "YAM_global.h"
#include "YAM_main.h"
#include "YAM_mainFolder.h"
#include "YAM_read.h"
#include "YAM_rexx.h"
#include "YAM_userlist.h"
#include "YAM_utilities.h"
#include "classes/Classes.h"

#include "FileInfo.h"
#include "FolderList.h"
#include "Locale.h"
#include "MailList.h"
#include "Mime.h"

#include "Debug.h"

/*
** The following structures are used to build the
** .index files of a folder.
**
** Alignment is very important here, because the
** structures are saved RAW to a file.
** Therefore we have to ensure that the alignment
** will be the same for 68k and PPC.
** That`s why we deal with the following #pragma
** calls
*/

#include "amiga-align.h"

/*
** structure of the compressed mail
**
** DO NOT CHANGE ALIGNMENT here or the .index
** files of a folder will be corrupt !
**
*/
struct ComprMail
{
   char             mailFile[SIZE_MFILE]; // mail filename without path
   struct DateStamp date;                 // the creation date of the mail (UTC)
   struct TimeVal   transDate;            // the received/sent date with ms (UTC)
   unsigned int     sflags;               // mail status flags
   unsigned int     mflags;               // general mail flags
   unsigned long    cMsgID;               // compressed MessageID
   unsigned long    cIRTMsgID;            // compressed InReturnTo MessageID
   long             size;                 // the total size of the message
   unsigned int     moreBytes;            // more bytes to follow as the subject
};

/*
** structure of the Folder Index
**
** DO NOT CHANGE ALIGNMENT here or the .index
** files of a folder will be corrupt !
**
*/
struct FIndex
{
   ULONG ID;
   int   Total;
   int   New;
   int   Unread;
   int   Size;
   long  reserved[2];
};

// whenever you change something up there (in FIndex or ComprMail) you
// need to increase this version ID!
#define FINDEX_VER  (MAKE_ID('Y','I','N','7'))

#include "default-align.h"

/* local protos */
static BOOL MA_ScanMailBox(struct Folder *folder);

/***************************************************************************
 Module: Main - Folder handling
***************************************************************************/

/*** Index Maintenance ***/
/// MA_PromptFolderPassword
//  Asks user for folder password
BOOL MA_PromptFolderPassword(struct Folder *fo, APTR win)
{
  BOOL success = FALSE;

  ENTER();

  if(isFreeAccess(fo))
  {
    D(DBF_FOLDER, "folder '%s' has no access restrictions", fo->Name);
    success = TRUE;
  }
  else
  {
    struct User *user;

    user = US_GetCurrentUser();

    if(Stricmp(fo->Password, user->Password) == 0)
    {
      D(DBF_FOLDER, "folder '%s' and user '%s' share the same password", fo->Name, user->Name);
      success = TRUE;
    }
    else
    {
      BOOL retry = TRUE;
      char prompt[SIZE_LARGE];

      snprintf(prompt, sizeof(prompt), tr(MSG_MA_GetFolderPass), fo->Name);

      do
      {
        char passwd[SIZE_PASSWORD];

        passwd[0] = '\0';
        if(StringRequest(passwd, SIZE_PASSWORD, tr(MSG_Folder), prompt, tr(MSG_Okay), NULL, tr(MSG_Cancel), TRUE, win) > 0)
          // try again if the password doesn't match
          success = (Stricmp(passwd, fo->Password) == 0);
        else
          // the user cancelled the requester, no more tries
          retry = FALSE;
      }
      while(success == FALSE && retry);
    }
  }

  RETURN(success);
  return success;
}

///
/// MA_ValidateStatus
//  Avoids invalid status values
static void MA_ValidateStatus(struct Folder *folder)
{
  ENTER();

  if(C->UpdateNewMail == TRUE  ||
     isOutgoingFolder(folder)  ||
     isSentFolder(folder))
  {
    D(DBF_FOLDER, "Validating status of new msgs in folder %s", folder->Name);

    LockMailListShared(folder->messages);

    if(IsMailListEmpty(folder->messages) == FALSE)
    {
      struct MailNode *mnode;

      ForEachMailNode(folder->messages, mnode)
      {
        struct Mail *mail = mnode->mail;

        if(hasStatusNew(mail))
        {
          if(isOutgoingFolder(folder))
            setStatusToQueued(mail);
          else if(isSentFolder(folder))
            setStatusToSent(mail);
          else
            setStatusToUnread(mail);
        }
      }
    }

    UnlockMailList(folder->messages);
  }

  LEAVE();
}

///
/// MA_IndexFileName
//  Returns file name of folder index
static char *MA_IndexFileName(struct Folder *folder)
{
  static char buffer[SIZE_PATHFILE];

  ENTER();

  AddPath(buffer, GetFolderDir(folder), ".index", sizeof(buffer));

  RETURN(buffer);
  return buffer;
}

///
/// MA_LoadIndex
//  Loads a folder index from disk
enum LoadedMode MA_LoadIndex(struct Folder *folder, BOOL full)
{
  FILE *fh;
  enum LoadedMode indexloaded = LM_UNLOAD;
  BOOL corrupt = FALSE;
  BOOL error = FALSE;

  ENTER();

  D(DBF_FOLDER, "Loading index for folder '%s'", folder->Name);

  if((fh = fopen(MA_IndexFileName(folder), "r")) != NULL)
  {
    struct FIndex fi;

    setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);

    BusyText(tr(MSG_BusyLoadingIndex), folder->Name);
    if(fread(&fi, sizeof(struct FIndex), 1, fh) != 1)
    {
      E(DBF_FOLDER, "error while loading struct FIndex from .index file");
      error = TRUE;
    }
    else if(fi.ID == FINDEX_VER)
    {
      folder->Total  = fi.Total;
      folder->New    = fi.New;
      folder->Unread = fi.Unread;
      folder->Size   = fi.Size;
      indexloaded = LM_FLUSHED;

      if(full == TRUE)
      {
        ClearMailList(folder, TRUE);
        for(;;)
        {
          struct Mail mail;
          struct ComprMail cmail;
          char buf[SIZE_LARGE];

          memset(&mail, 0, sizeof(struct Mail));
          if(fread(&cmail, sizeof(struct ComprMail), 1, fh) != 1)
          {
            // check if we are here because of an error or EOF
            if(ferror(fh) != 0 || feof(fh) == 0)
            {
              E(DBF_FOLDER, "error while loading ComprMail struct from .index file");
              error = TRUE;
            }

            // if we end up here it is just a EOF and no error.
            break;
          }

          if(cmail.moreBytes > SIZE_LARGE)
          {
            ER_NewError(tr(MSG_ER_INDEX_CORRUPTED), MA_IndexFileName(folder), folder->Name, ftell(fh), cmail.mailFile, cmail.moreBytes);
            corrupt = TRUE;
            break;
          }

          if(fread(buf, cmail.moreBytes, 1, fh) != 1)
          {
            E(DBF_FOLDER, "fread error while reading index file");
            error = TRUE;
            break;
          }

          strlcpy(mail.Subject, GetNextLine(buf), sizeof(mail.Subject));
          strlcpy(mail.From.Address, GetNextLine(NULL), sizeof(mail.From.Address));
          strlcpy(mail.From.RealName, GetNextLine(NULL), sizeof(mail.From.RealName));
          strlcpy(mail.To.Address, GetNextLine(NULL), sizeof(mail.To.Address));
          strlcpy(mail.To.RealName, GetNextLine(NULL), sizeof(mail.To.RealName));
          strlcpy(mail.ReplyTo.Address, GetNextLine(NULL), sizeof(mail.ReplyTo.Address));
          strlcpy(mail.ReplyTo.RealName, GetNextLine(NULL), sizeof(mail.ReplyTo.RealName));
          mail.Folder = folder;
          mail.mflags = cmail.mflags;
          mail.sflags = cmail.sflags;
          setVOLValue(&mail, 0);  // we have to make sure that the volatile flag field isn`t loaded
          strlcpy(mail.MailFile, cmail.mailFile, sizeof(mail.MailFile));
          mail.Date = cmail.date;
          mail.transDate = cmail.transDate;
          mail.cMsgID = cmail.cMsgID;
          mail.cIRTMsgID = cmail.cIRTMsgID;
          mail.Size = cmail.size;

          // finally add the new mail structure to our mail list
          if(AddMailToList(&mail, folder) == NULL)
          {
            E(DBF_FOLDER, "AddMailToList returned NULL!");
            error = TRUE;
            break;
          }
        }
      }
    }

    if(ferror(fh) != 0)
    {
      E(DBF_FOLDER, "ferror() returned != 0");
      error = TRUE;
    }

    BusyEnd();
    fclose(fh);
  }
  else if(errno != ENOENT)
  {
    E(DBF_FOLDER, "fopen() on '%s' failed.", MA_IndexFileName(folder));
    error = TRUE;
  }

  // in case an error occurred we report it
  // to the user
  if(error == TRUE)
  {
    E(DBF_FOLDER, "error %ld occurred while trying to load the index file '%s'", errno, MA_IndexFileName(folder));
    ClearMailList(folder, TRUE);
    indexloaded = LM_UNLOAD;

    // report failure
    ER_NewError(tr(MSG_ER_CANNOT_READ_INDEX), MA_IndexFileName(folder), folder->Name);
  }
  else if(corrupt == TRUE || indexloaded == LM_UNLOAD)
  {
    W(DBF_FOLDER, "%s .index file '%s' detected, %s", corrupt ? "corrupt" : "missing",
                                                      MA_IndexFileName(folder),
                                                      full ? "rebuilding..." : "skipping...");

    // clear the mail list of the folder
    ClearMailList(folder, TRUE);

    // if the "full" mode was requested we make sure we
    // rescan the index accordingly
    if(full == TRUE)
    {
      // rebuild the index (rescanning the mailbox directory)
      if(MA_ScanMailBox(folder) == TRUE && MA_SaveIndex(folder) == TRUE)
        indexloaded = LM_VALID;
    }
  }
  else if(full == TRUE)
  {
    indexloaded = LM_VALID;
    CLEAR_FLAG(folder->Flags, FOFL_MODIFY);
  }

  RETURN(indexloaded);
  return indexloaded;
}

///
/// MA_SaveIndex
//  Saves a folder index to disk
BOOL MA_SaveIndex(struct Folder *folder)
{
  BOOL success = FALSE;
  FILE *fh;

  ENTER();

  if((fh = fopen(MA_IndexFileName(folder), "w")) != NULL)
  {
    struct FIndex fi;

    setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);

    BusyText(tr(MSG_BusySavingIndex), folder->Name);

    // lets prepare the Folder Index struct and write it out
    // we clear it first, so that the reserved field is also 0
    memset(&fi, 0, sizeof(struct FIndex));
    fi.ID = FINDEX_VER;
    fi.Total = folder->Total;
    fi.New = folder->New;
    fi.Unread = folder->Unread;
    fi.Size = folder->Size;
    fwrite(&fi, sizeof(struct FIndex), 1, fh);

    LockMailListShared(folder->messages);

    if(IsMailListEmpty(folder->messages) == FALSE)
    {
      struct MailNode *mnode;

      ForEachMailNode(folder->messages, mnode)
      {
        struct Mail *mail = mnode->mail;
        struct ComprMail cmail;
        char buf[SIZE_LARGE];

        memset(&cmail, 0, sizeof(struct ComprMail));
        snprintf(buf, sizeof(buf), "%s\n%s\n%s\n%s\n%s\n%s\n%s\n",
           mail->Subject,
           mail->From.Address, mail->From.RealName,
           mail->To.Address, mail->To.RealName,
           mail->ReplyTo.Address, mail->ReplyTo.RealName);

        strlcpy(cmail.mailFile, mail->MailFile, sizeof(cmail.mailFile));
        cmail.date = mail->Date;
        cmail.transDate = mail->transDate;
        cmail.sflags = mail->sflags;
        cmail.mflags = mail->mflags;
        setVOLValue(&cmail, 0);  // we have to make sure that the volatile flag field isn`t saved
        cmail.cMsgID = mail->cMsgID;
        cmail.cIRTMsgID = mail->cIRTMsgID;
        cmail.size = mail->Size;
        cmail.moreBytes = strlen(buf);

        fwrite(&cmail, sizeof(struct ComprMail), 1, fh);
        fwrite(buf, 1, cmail.moreBytes, fh);
      }
    }

    UnlockMailList(folder->messages);

    fclose(fh);

    CLEAR_FLAG(folder->Flags, FOFL_MODIFY);
    BusyEnd();

    success = TRUE;
  }
  else
  {
    W(DBF_FOLDER, "saving index file '%s' of folder '%s' failed", MA_IndexFileName(folder), folder->Name);
    ER_NewError(tr(MSG_ER_CANNOT_WRITE_INDEX), MA_IndexFileName(folder), folder->Name);
  }

  RETURN(success);
  return success;
}

///
/// MA_GetIndex
//  Opens/unlocks a folder
BOOL MA_GetIndex(struct Folder *folder)
{
  BOOL result = FALSE;

  ENTER();

  if(folder != NULL && isGroupFolder(folder) == FALSE)
  {
    D(DBF_FOLDER, "folder: '%s' path: '%s' type: %ld mode: %ld pw '%s'", folder->Name, folder->Path, folder->Type, folder->LoadedMode, folder->Password);

    // check that the folder is in a valid state for
    // getting the index
    if(folder->LoadedMode != LM_VALID &&
       folder->LoadedMode != LM_REBUILD)
    {
      BOOL canLoadIndex;

      // check the protected status of the folder and prompt
      // the user for the password in case it is required.
      if(isProtectedFolder(folder) == FALSE || folder->Password[0] == '\0')
        canLoadIndex = TRUE;
      else
        canLoadIndex = MA_PromptFolderPassword(folder, G->MA->GUI.WI);

      if(canLoadIndex == TRUE)
      {
        // load the index file (and eventually rebuild it)
        folder->LoadedMode = MA_LoadIndex(folder, TRUE);

        // we need to refresh the entry in the folder listtree this folder belongs to
        // but only if there is a GUI already!
        if(folder->LoadedMode == LM_VALID)
        {
          MA_ValidateStatus(folder);
        }
        else
          W(DBF_MAIL, "status of loaded folder '%s' != LM_VALID (%ld)", folder->Name, folder->LoadedMode);

        if(G->MA != NULL)
          DisplayStatistics(folder, FALSE);
      }
      else
        W(DBF_FOLDER, "password of protected folder '%s' couldn't be verified!", folder->Name);
    }
    else
      W(DBF_FOLDER, "skipping index loading due to LoadedMode %ld for folder '%s'", folder->LoadedMode, folder->Name);

    // check if the load status is valid or not
    result = (BOOL)(folder->LoadedMode == LM_VALID);
  }

  RETURN(result);
  return result;
}

///
/// MA_ExpireIndex
//  Invalidates a folder index
void MA_ExpireIndex(struct Folder *folder)
{
  ENTER();

  if(!isModified(folder))
    DeleteFile(MA_IndexFileName(folder));

  SET_FLAG(folder->Flags, FOFL_MODIFY);

  LEAVE();
}

///
/// MA_UpdateIndexes
//  Updates indices of all folders
void MA_UpdateIndexes(BOOL initial)
{
  ENTER();

  LockFolderListShared(G->folders);

  if(IsFolderListEmpty(G->folders) == FALSE)
  {
    struct FolderNode *fnode;

    ForEachFolderNode(G->folders, fnode)
    {
      struct Folder *folder = fnode->folder;

      if(folder != NULL && !isGroupFolder(folder))
      {
        if(initial == TRUE)
        {
          const char *folderDir = GetFolderDir(folder);
          char *indexFile = MA_IndexFileName(folder);
          ULONG dirDate;
          ULONG indexDate;

          // get date of the folder directory and the .index file
          // itself
          if(ObtainFileInfo(folderDir, FI_TIME, &dirDate) == TRUE &&
             ObtainFileInfo(indexFile, FI_TIME, &indexDate) == TRUE)
          {
            // only consider starting to rebuilding the .index if
            // either the date of the directory is greater than the
            // date of the .index file itself, or if there is no index
            // file date at all (no file present)
            if(dirDate > indexDate + 30)
            {
              ULONG dirProtection;
              ULONG indexProtection;

              // get the protection bits of the folder index file
              // and the folder directory, and if both have the A
              // bit set we skip the index rescanning process because
              // the A bits might have been set by a backup program
              if(ObtainFileInfo(folderDir, FI_PROTECTION, &dirProtection) == TRUE &&
                 ObtainFileInfo(indexFile, FI_PROTECTION, &indexProtection) == TRUE)
              {
                if(isFlagClear(indexProtection, FIBF_ARCHIVE) ||
                   isFlagClear(dirProtection, FIBF_ARCHIVE))
                {
                  // lets first delete the .index file to
                  // make sure MA_GetIndex() is going to
                  // rebuild it.
                  if(indexDate > 0)
                    DeleteFile(indexFile);

                  // then lets call GetIndex() to start rebuilding
                  // the .index - but only if this folder is one of the folders
                  // that should update it indexes during startup
                  if((isIncomingFolder(folder) || isOutgoingFolder(folder) ||
                      isTrashFolder(folder) || C->LoadAllFolders) &&
                     !isProtectedFolder(folder))
                  {
                    if(MA_GetIndex(folder) == TRUE)
                    {
                      // if we finally rebuilt the .index we
                      // immediatly flush it here so that another
                      // following index rebuild doesn't take
                      // all remaining memory.
                      if((isSentFolder(folder) || !isDefaultFolder(folder)) &&
                          folder->LoadedMode == LM_VALID &&
                          isFreeAccess(folder))
                      {
                        if(isModified(folder))
                          MA_SaveIndex(folder);

                        ClearMailList(folder, FALSE);
                        folder->LoadedMode = LM_FLUSHED;
                        CLEAR_FLAG(folder->Flags, FOFL_FREEXS);
                      }
                    }
                  }
                  else
                  {
                    // otherwise we make sure everything is cleared
                    ClearMailList(folder, FALSE);
                    folder->LoadedMode = LM_FLUSHED;
                    CLEAR_FLAG(folder->Flags, FOFL_FREEXS);
                  }
                }
              }
            }
          }
        }
        else
        {
          if(folder->LoadedMode == LM_VALID && isModified(folder))
            MA_SaveIndex(folder);
        }
      }
    }
  }

  UnlockFolderList(G->folders);

  LEAVE();
}

///
/// MA_FlushIndexes
//  Removes loaded folder indices from memory and closes folders
void MA_FlushIndexes(BOOL all)
{
  ENTER();

  LockFolderListShared(G->folders);

  if(IsFolderListEmpty(G->folders) == FALSE)
  {
    struct FolderNode *fnode;
    struct Folder *actfolder = FO_GetCurrentFolder();

    ForEachFolderNode(G->folders, fnode)
    {
      struct Folder *folder = fnode->folder;

      // make sure the folder index is saved
      if(isModified(folder))
        MA_SaveIndex(folder);

      // then we make sure we only clear the folder index of
      // a folder where this should really be done and also not
      // on the actual folder or otherwise we risk to run into
      // problems.
      if((isSentFolder(folder) || !isDefaultFolder(folder)) &&
          folder->LoadedMode == LM_VALID &&
          (all == TRUE || isFreeAccess(folder)) &&
          folder != actfolder)
      {
        D(DBF_FOLDER, "Flush index of folder '%s'", folder->Name);

        ClearMailList(folder, FALSE);
        folder->LoadedMode = LM_FLUSHED;
        CLEAR_FLAG(folder->Flags, FOFL_FREEXS);
      }
    }

    // make sure to redraw the whole folder list
    DoMethod(G->MA->GUI.NL_FOLDERS, MUIM_NListtree_Redraw, MUIV_NListtree_Redraw_All, MUIF_NONE);
  }

  UnlockFolderList(G->folders);

  LEAVE();
}

HOOKPROTONHNONP(MA_FlushIndexFunc, void)
{
   MA_FlushIndexes(TRUE);
}
MakeHook(MA_FlushIndexHook, MA_FlushIndexFunc);
///

/*** Private functions ***/
/// MA_ChangeFolder
//  Changes to another folder
void MA_ChangeFolder(struct Folder *folder, BOOL set_active)
{
  struct Folder *actfo;

  ENTER();

  actfo = FO_GetCurrentFolder();

  if(actfo != NULL)
  {
    BOOL folderChanged = TRUE;

    if(folder == NULL)
      folder = actfo;
    else if(actfo == folder)
      folderChanged = FALSE;
    else if(set_active == TRUE)
      FO_SetCurrentFolder(folder);

    if(folderChanged == TRUE)
    {
      struct MA_GUIData *gui = &G->MA->GUI;

      // in case the main window has an embedded read pane, we have to
      // clear it before changing the actual folder
      if(C->EmbeddedReadPane == TRUE)
        DoMethod(gui->MN_EMBEDDEDREADPANE, MUIM_ReadMailGroup_Clear, MUIF_NONE);

      // if this folder should be disabled, lets do it now
      if(isGroupFolder(folder) || MA_GetIndex(folder) == FALSE)
      {
        xset(gui->PG_MAILLIST, MUIA_Disabled,     TRUE,
                               MUIA_ShortHelp,    NULL,
                               MUIA_NList_Active, MUIV_NList_Active_Off);

        // set the quickbar as disabled as well and abort a search still in progress
        if(C->QuickSearchBar == TRUE)
        {
          set(gui->GR_QUICKSEARCHBAR, MUIA_Disabled, TRUE);
          DoMethod(gui->GR_QUICKSEARCHBAR, MUIM_QuickSearchBar_AbortSearch);
        }

        // also set an embedded read pane as disabled.
        if(C->EmbeddedReadPane == TRUE)
          set(gui->MN_EMBEDDEDREADPANE, MUIA_Disabled, TRUE);

        DoMethod(gui->IB_INFOBAR, MUIM_InfoBar_SetFolder, folder);

        // make sure the main mail list noticies that
        // the selection has changed so that if a user reactivates a valid
        // folder the main mail list will get updated accordingly.
        MA_ChangeSelected(TRUE);
      }
      else if(FO_GetCurrentFolder() == folder) // check again for the current folder
      {
        // set the SortFlag in the NList accordingly
        MA_SetSortFlag();

        // Now we update the InfoBar accordingly
        DoMethod(gui->IB_INFOBAR, MUIM_InfoBar_SetFolder, folder);

        // enable an embedded read pane again
        if(C->EmbeddedReadPane == TRUE)
          set(gui->MN_EMBEDDEDREADPANE, MUIA_Disabled, FALSE);

        // In case the main window has an quicksearchbar, we have to
        // clear it as well before changing the folder. We also abort
        // a search still being in progress.
        if(C->QuickSearchBar == TRUE)
        {
          DoMethod(gui->GR_QUICKSEARCHBAR, MUIM_QuickSearchBar_AbortSearch);
          DoMethod(gui->GR_QUICKSEARCHBAR, MUIM_QuickSearchBar_Clear);
        }

        // Create the Mail List and display it
        DisplayMailList(folder, gui->PG_MAILLIST);

        // now we have to assure that the folder is enabled
        set(gui->PG_MAILLIST, MUIA_Disabled, FALSE);

        // Now we jump to messages that are NEW
        if(C->JumpToNewMsg == TRUE && (folder->New != 0 || folder->Unread != 0))
          MA_JumpToNewMsg();
        else if(C->JumpToRecentMsg == TRUE)
          MA_JumpToRecentMsg();
        else if(folder->LastActive >= 0)
          set(gui->PG_MAILLIST, MUIA_NList_Active, folder->LastActive);

        // if there is still no entry active in the NList we make the first one active
        if(xget(gui->PG_MAILLIST, MUIA_NList_Active) == (ULONG)MUIV_NList_Active_Off)
          set(gui->PG_MAILLIST, MUIA_NList_Active, MUIV_NList_Active_Top);

        // if there are no messages in the folder the GUI needs to be updated nevertheless
        if(folder->Total == 0)
          MA_ChangeSelected(TRUE);
      }
    }
  }

  LEAVE();
}

HOOKPROTONHNONP(MA_ChangeFolderFunc, void)
{
  ENTER();

  MA_ChangeFolder(NULL, FALSE);

  LEAVE();
}
MakeHook(MA_ChangeFolderHook, MA_ChangeFolderFunc);
///
/// MA_JumpToNewMsg
// Function that jumps to the first or last unread mail in a folder,
// depending on sort order of the folder
void MA_JumpToNewMsg(void)
{
  struct Folder *folder;
  Object *lv;
  int i, incr, pos = -1;

  ENTER();

  folder = FO_GetCurrentFolder();
  lv = G->MA->GUI.PG_MAILLIST;

  if(folder->Sort[0] < 0 || folder->Sort[1] < 0)
  {
    i = xget(lv, MUIA_NList_Entries) - 1;
    incr = -1;
  }
  else
  {
    i = 0;
    incr = 1;
  }

  while(TRUE)
  {
    struct Mail *mail;

    DoMethod(lv, MUIM_NList_GetEntry, i, &mail);
    if(mail == NULL)
      break;

    if(hasStatusNew(mail) || !hasStatusRead(mail))
    {
      pos = i;
      break;
    }

    i += incr;
  }

  set(lv, MUIA_NList_Active, pos >= 0 ? pos : folder->LastActive);

  LEAVE();
}
///
/// MA_JumpToRecentMsg
// Function that jumps to the most recent mail in a folder
void MA_JumpToRecentMsg(void)
{
  struct Folder *folder;
  Object *lv;
  struct Mail *recent = NULL;
  int recentIdx = -1, i;

  ENTER();

  folder = FO_GetCurrentFolder();
  lv = G->MA->GUI.PG_MAILLIST;

  i = 0;
  while(TRUE)
  {
    struct Mail *mail;

    DoMethod(lv, MUIM_NList_GetEntry, i, &mail);
    if(mail == NULL)
      break;

    if(recent == NULL || MA_CompareByDate(mail, recent) > 0)
    {
      // this mail is more recent than the yet most recent known
      recentIdx = i;
      recent = mail;
    }

    i++;
  }

  set(lv, MUIA_NList_Active, recentIdx >= 0 ? recentIdx : folder->LastActive);

  LEAVE();
}
///
/// MA_ConvertOldMailFile
// This function takes a fileinfoblock and a folder and converts the
// oldstyle mailfile referred by fib to a new YAM 2.5+ conform mail file.
static char *MA_ConvertOldMailFile(char *filename, struct Folder *folder)
{
  char dateFilePart[13];  // the base64 encoded date part occupies 12+1 bytes
  char statusFilePart[4]; // we only need 3+1 bytes for the status flags here
  char oldFilePath[SIZE_PATHFILE+1];
  char *statusPartPtr = statusFilePart;
  char *comment;
  char *ptr;
  char *result = filename;

  ENTER();

  // clear
  dateFilePart[0] = '\0';
  statusFilePart[0] = '\0';

  // construct the full path of the old filename
  // and get the file comment
  if(AddPath(oldFilePath, GetFolderDir(folder), filename, sizeof(oldFilePath)) == NULL ||
     ObtainFileInfo(oldFilePath, FI_COMMENT, &comment) == FALSE ||
     comment == NULL)
  {
    RETURN(NULL);
    return NULL;
  }

  // for a proper conversion we have to take mainly the file comment into
  // account as in there all status information aswell as the transfer
  // data was stored in YAM versions prior to 2.5
  if(comment[0] != '\0')
  {
    // read out the mailstatus - which was normally the first character
    // of the comment
    switch(comment[0])
    {
      case 'U':
        // nothing
      break;

      case 'R':
        *statusPartPtr++ = SCHAR_READ;
        *statusPartPtr++ = SCHAR_REPLIED;
      break;

      case 'F':
        *statusPartPtr++ = SCHAR_READ;
        *statusPartPtr++ = SCHAR_FORWARDED;
      break;

      case 'W':
        *statusPartPtr++ = SCHAR_READ;
        *statusPartPtr++ = SCHAR_QUEUED;
      break;

      case 'H':
        *statusPartPtr++ = SCHAR_READ;
        *statusPartPtr++ = SCHAR_HOLD;
      break;

      case 'O':
        *statusPartPtr++ = SCHAR_READ;
      break;

      case 'E':
        *statusPartPtr++ = SCHAR_ERROR;
      break;

      case 'S':
        *statusPartPtr++ = SCHAR_READ;
        *statusPartPtr++ = SCHAR_SENT;
      break;

      case 'N':
      default:
        *statusPartPtr++ = SCHAR_NEW;
      break;
    }

    // now we check if second char is present and if we set the mailfag
    // as "marked" and the arexx permanent flag
    if(comment[1] != '\0' && comment[1] != ' ')
    {
      int pval = 0;

      // we have to check for the permanent flag also.
      if(comment[1] >= 'M' && comment[1] <= 'M'+7)
      {
        *statusPartPtr++ = SCHAR_MARKED;

        pval = comment[1]-'M';
      }
      else if(comment[1] >= '1' && comment[1] <= '7')
      {
        pval = comment[1]-'1'+1;
      }

      if(pval > 0)
        *statusPartPtr++ = '0'+pval;
    }

    *statusPartPtr = '\0'; // NUL terminate the status part

    // we check if this comment also has the transfer Date included
    // we only take the string if it is exactly 12bytes long or
    // otherwise it could be some weird data in the Comment string
    if(comment[1] != '\0' && comment[2] != '\0' && comment[14] == '\0')
    {
      // ok we have the transfer Date, so lets put it at the beginning of
      // the new filename
      strlcpy(dateFilePart, &comment[2], sizeof(dateFilePart));
    }
  }

  // free the duplicated comment again
  free(comment);

  if(dateFilePart[0] == '\0')
  {
    struct TimeVal newDate;

    // so we don't seem to have a transfer Date in the file comment.
    // What we do now is that we take the date from the original file
    // (the first 5 numerical numbers are the days)
    newDate.Seconds = atol(filename) * 24 * 60 * 60;
    newDate.Microseconds = 0;

    // encode this date as a base64 encoded string
    base64encode(dateFilePart, (unsigned char *)&newDate, sizeof(struct TimeVal));
  }

  // as the dateFilePart may contain slashes "/" we have to replace them
  // with "-" chars to don't drive the filesystem crazy :)
  ptr = dateFilePart;
  while((ptr = strchr(ptr, '/')) != NULL)
    *ptr = '-';

  do
  {
    static char newFileName[SIZE_MFILE];
    char newFilePath[SIZE_PATHFILE+1];

    // ok, now we should have all main parts of the new filename, so we
    // can concatenate it to one new filename and try to rename the old
    // style mailfile to the newstyle equivalent.
    snprintf(newFileName, sizeof(newFileName), "%s.001,%s", dateFilePart, statusFilePart);

    // so, now we should be finished with finding the new filename of the mail file.
    // lets try to rename it with the dos.library's Rename() function
    if(AddPath(newFilePath, GetFolderDir(folder), newFileName, sizeof(newFilePath)) == NULL)
    {
      result = NULL;
      break;
    }

    // try to rename it and if it fails go and find out if we have to increase
    // the mail counter or not.
    if(Rename(oldFilePath, newFilePath) == 0)
    {
      char pattern[15+1];
      LONG mailCounter;

      // search for files matching the dateFilePart
      snprintf(pattern, sizeof(pattern), "%s.#?", dateFilePart);
      mailCounter = FileCount(GetFolderDir(folder), pattern);

      // if we didn't find any matching file then this signals
      // another error than an already existing file!
      if(mailCounter <= 0)
      {
        E(DBF_FOLDER, "  error on renaming '%s' to '%s'", oldFilePath, newFilePath);
        result = NULL;
        break;
      }

      // let us now try it again to rename the file
      snprintf(newFileName, sizeof(newFileName), "%s.%03ld,%s", dateFilePart, ++mailCounter, statusFilePart);

      if(AddPath(newFilePath, GetFolderDir(folder), newFileName, sizeof(newFilePath)) == NULL)
      {
        result = NULL;
        break;
      }

      // try to rename it and if it fails finally return an error
      if(Rename(oldFilePath, newFilePath) == 0)
      {
        E(DBF_FOLDER, "  error on renaming '%s' to '%s'", oldFilePath, newFilePath);
        result = NULL;
        break;
      }
    }

    result = newFileName;

    // and to make everything as clean as possible, lets erase
    // the filecomment as YAM 2.5+ doesn't require file comments anymore
    SetComment(newFilePath, "");
  }
  while(FALSE);

  // return failure if we didn't change anything
  if(result == filename)
    result = NULL;

  RETURN(result);
  return result;
}
///

/*** Mail header scanning ***/
/// MA_NewMailFile
//  Function that creates a new plain mail filename or by taking provided
//  data into account. It returns the full path to the new mail file in the
//  folder and also writes it into the mailfile parameter
char *MA_NewMailFile(const struct Folder *folder, char *mailfile)
{
  static char fullpath[SIZE_PATHFILE+1];
  char dateFilePart[12+1];
  char newFileName[SIZE_MFILE];
  const char *folderDir;
  char *ptr;
  struct TimeVal curDate;
  int mCounter = 0;
  char *result = NULL;

  ENTER();

  folderDir = GetFolderDir(folder);

  // take the current time and use it as the datePart of the
  // new mailfile name
  GetSysTimeUTC(&curDate);

  // encode this date as a base64 encoded string
  base64encode(dateFilePart, (unsigned char *)&curDate, sizeof(struct TimeVal));

  // as the dateFilePart may contain slashes "/" we have to replace them
  // with "-" chars to don't drive the filesystem crazy :)
  ptr = dateFilePart;
  while((ptr = strchr(ptr, '/')))
    *ptr = '-';

  do
  {
    snprintf(newFileName, sizeof(newFileName), "%s.%03d,N", dateFilePart, ++mCounter);

    AddPath(fullpath, folderDir, newFileName, sizeof(fullpath));
  }
  while(mCounter < 999 && FileExists(fullpath));

  if(mCounter < 999)
  {
    // copy the newFileName to our mailfile buffer
    if(mailfile)
      strcpy(mailfile, newFileName);
    // get the real path to the file
    result = GetRealPath(fullpath);
  }
  else
    // we ran out of free numbers
    result = NULL;

  RETURN(result);
  return result;
}

///
/// MA_DetectUUE
//  Checks if message contains an uuencoded file
static BOOL MA_DetectUUE(FILE *fh)
{
  char *buffer;
  BOOL found = FALSE;

  ENTER();

  if((buffer = malloc(SIZE_LINE)) != NULL)
  {
    // Now we process the whole mailfile and check if there is any line that
    // starts with "begin xxx"
    while(GetLine(fh, buffer, SIZE_LINE) != NULL)
    {
      // lets check for digit first because this will throw out many others first
      if(isdigit((int)buffer[6]) && strncmp(buffer, "begin ", 6) == 0)
      {
        found = TRUE;
        break;
      }
    }

    free(buffer);
  }

  RETURN(found);
  return found;
}

///
/// MA_ReadHeader
//  Reads header lines of a message into memory
BOOL MA_ReadHeader(const char *mailFile, FILE *fh, struct MinList *headerList, enum ReadHeaderMode mode)
{
  BOOL success = FALSE;

  ENTER();

  if(headerList != NULL)
  {
    unsigned int linesread = 0;
    char *buffer;

    // clear the headerList first
    NewList((struct List *)headerList);

    // allocate some memory for use as a read buffer
    if((buffer = calloc(SIZE_LINE, sizeof(char))) != NULL)
    {
      BOOL finished = FALSE;
      struct HeaderNode *hdrNode = NULL;

      // we read out the whole header line by line and
      // concatenate lines that are belonging together.
      while((GetLine(fh, buffer, SIZE_LINE) != NULL && (++linesread, buffer[0] != '\0')) ||
            (finished == FALSE && (finished = TRUE)))
      {
        // if the start of this line is a space or a tabulator sign
        // this line belongs to the last header also and we have to
        // add it to the last one.
        if((buffer[0] == ' ' || buffer[0] == '\t') && finished == FALSE)
        {
          if(hdrNode != NULL)
          {
            char *ptr;

            // move to the "real" start of the string so that we can copy
            // from there to our previous header.
            for(ptr = buffer; *ptr && isspace(*ptr); ptr++);

            // we want to preserve the last space so that this headerline
            // is correctly connected
            if(ptr != buffer)
              *(--ptr) = ' ';

            // now concatenate this new headerstring to our previous one
            hdrNode->content = StrBufCat(hdrNode->content, ptr);
          }
        }
        else
        {
          // it seems that we have found another header line because
          // it didn`t start with a linear-white-space, so lets
          // first validate the previous one, if it exists.
          if(hdrNode != NULL)
          {
            char *ptr;
            char *hdrContents = hdrNode->content;
            int len;

            // we first decode the header according to RFC 2047 which
            // should give us the full charset interpretation
            if((len = rfc2047_decode(hdrContents, hdrContents, strlen(hdrContents))) == -1)
            {
              E(DBF_FOLDER, "ERROR: malloc() error during rfc2047() decoding");
              break; // break-out
            }
            else if(len == -2)
            {
              W(DBF_FOLDER, "WARNING: unknown header encoding found");

              // signal an error but continue.
              ER_NewError(tr(MSG_ER_UNKNOWN_HEADER_ENCODING), hdrContents, mailFile);
            }
            else if(len == -3)
            {
              W(DBF_FOLDER, "WARNING: base64 header decoding failed");
            }

            // now that we have decoded the headerline accoring to rfc2047
            // we have to strip out eventually existing ESC sequences as
            // this can be dangerous with MUI.
            for(ptr=hdrContents; *ptr; ptr++)
            {
              // if we find an ESC sequence, strip it!
              if(*ptr == 0x1b)
                *ptr = ' ';
            }

            // the headerNode seems to be finished so we put it into our
            // headerList
            AddTail((struct List *)headerList, (struct Node *)hdrNode);
          }

          // if we are finished we break out here
          if(finished == TRUE)
          {
            success = TRUE;
            break;
          }

          // now that we have finished the last header line
          // we can finally start processing a new one.
          // Which means we allocate a new HeaderNode and try to get out the header
          // name
          if((hdrNode = calloc(1, sizeof(struct HeaderNode))) != NULL)
          {
            char *ptr;

            // now we try to find the name of the header (ends with a ':' and no white space
            // or control character in between
            for(ptr = buffer; *ptr; ptr++)
            {
              if(*ptr == ':')
                break;
              else if(*ptr < 33 || *ptr > 126)
              {
                ptr = NULL;
                break;
              }
            }

            if(ptr != NULL && *ptr != '\0')
            {
              *ptr = '\0';

              // use our StrBufCpy() function to copy the name of the header
              // into our ->name element
              if((hdrNode->name = StrBufCpy(NULL, buffer)) != NULL)
              {
                // now we copy also the rest of buffer into the contents
                // of the headerNode
                if((hdrNode->content = StrBufCpy(NULL, Trim(ptr+1))) != NULL)
                {
                  // everything seemed to work fine, so lets continue
                  continue;
                }
              }
            }

            // if we end up here then something went wrong and we have to clear
            // the header Node and stuff
            free(hdrNode);
            hdrNode = NULL;
          }
          else
            break;
        }
      }

      // if we haven't had success in reading the headers
      // we make sure we clean everything up. If we read no
      // headers at all we return a failure. But if we were able to
      // read a single empty line it is a signal that this part of
      // the mail doesn't have any header at all (which may be valid)
      if(success == FALSE)
        FreeHeaderList(headerList);
      else if(IsListEmpty((struct List *)headerList) == TRUE &&
              (mode == RHM_MAINHEADER || buffer[0] != '\0' || linesread != 1))
      {
        W(DBF_MAIL, "no required header data found while having scanned '%s'.", mailFile);
        success = FALSE;
      }

      free(buffer);
    }
  }

  RETURN(success);
  return success;
}

///
/// MA_FreeEMailStruct
//  Frees an extended email structure
void MA_FreeEMailStruct(struct ExtendedMail *email)
{
  ENTER();

  if(email)
  {
    FreeStrBuf(email->SenderInfo);
    email->SenderInfo = NULL;

    FreeStrBuf(email->extraHeaders);
    email->extraHeaders = NULL;

    if(email->SFrom != NULL)
    {
      ASSERT(email->NoSFrom > 0);

      free(email->SFrom);
      email->SFrom = NULL;
    }

    if(email->STo != NULL)
    {
      ASSERT(email->NoSTo > 0);

      free(email->STo);
      email->STo = NULL;
    }

    if(email->SReplyTo != NULL)
    {
      ASSERT(email->NoSReplyTo > 0);

      free(email->SReplyTo);
      email->SReplyTo = NULL;
    }

    if(email->CC != NULL)
    {
      ASSERT(email->NoCC > 0);

      free(email->CC);
      email->CC = NULL;
    }

    if(email->BCC != NULL)
    {
      ASSERT(email->NoBCC > 0);

      free(email->BCC);
      email->BCC = NULL;
    }

    free(email);
  }

  LEAVE();
}

///
/// MA_GetRecipients
//  Extracts recipients from a header field
static int MA_GetRecipients(char *h, struct Person **per)
{
  int cnt=0;
  char *p = h;

  ENTER();

  while(*p)
  {
    cnt++;
    if((p = MyStrChr(p, ',')))
      p++;
    else
      break;
  }

  if(cnt > 0)
  {
    struct Person *cur;

    // allocate enough memory to carry all
    // found recipients in an array of struct Person
    // structures.
    if((*per = cur = calloc(cnt, sizeof(struct Person))))
    {
      for(p=h; *p; cur++)
      {
        char *next;

        if((next = MyStrChr(p, ',')))
          *next++ = '\0';

        ExtractAddress(p, cur);

        D(DBF_MIME, "extracted rcpt: '%s' '%s'", cur->RealName, cur->Address);

        if(!(p = next))
          break;
      }
    }
    else
      cnt = 0;
  }
  else
    *per = NULL;

  RETURN(cnt);
  return cnt;
}

///
/// MA_ScanDate
//  Converts textual date header into datestamp format
static BOOL MA_ScanDate(struct Mail *mail, const char *date)
{
  BOOL success = FALSE;
  int count = 0;
  int day = 0;
  int mon = 0;
  int year = 0;
  int hour = 0;
  int min = 0;
  int sec = 0;
  char *s;
  char tdate[SIZE_SMALL];
  char ttime[SIZE_SMALL];
  char tzone[SIZE_SMALL];
  struct DateTime dt;

  ENTER();

  // make sure to skip the weekday definition if it exists
  if((s = strpbrk(date, " |;,")) != NULL)
  {
    // check if we did reach here because the whole
    // weekday definition was missing
    if(isspace(*s) && isdigit(*date))
      s = (char *)date;
    else
      s++;
  }
  else
  {
    W(DBF_MAIL, "no starting separator found!!");

    s = (char *)date;
  }

  // skip leading spaces
  while(*s && isspace(*s))
    s++;

  while(*s)
  {
    char *e;

    if((e = strpbrk(s, " |;,")) == NULL)
      e = s+strlen(s);

    switch(count)
    {
      // get the day
      case 0:
      {
        if(!isdigit(*s) || (day = atoi(s)) > 31)
        {
          W(DBF_MAIL, "couldn't parse day from '%s'", s);

          day = 1;
        }
      }
      break;

      // get the month
      case 1:
      {
        for(mon = 1; mon <= 12; mon++)
        {
          if(strnicmp(s, months[mon-1], 3) == 0)
            break;
        }

        if(mon > 12)
        {
          W(DBF_MAIL, "couldn't parse month from '%s'", s);

          mon = 1;
        }
      }
      break;

      // get the year
      case 2:
      {
        if(isdigit(*s))
          year = atoi(s);
        else
        {
          W(DBF_MAIL, "couldn't parse year from '%s'", s);

          year = 1978;
        }
      }
      break;

      // get the time values
      case 3:
      {
        if(sscanf(s, "%d:%d:%d", &hour, &min, &sec) != 3)
        {
          if(sscanf(s, "%d:%d", &hour, &min) == 2)
            sec = 0;
          else
          {
            W(DBF_MAIL, "couldn't parse time from '%s'", s);

            hour = 0;
            min = 0;
            sec = 0;
          }
        }
      }
      break;

      // get the time zone
      case 4:
      {
        while(*s && *s == '(')
          s++;

        strlcpy(tzone, s, MIN(sizeof(tzone), (unsigned int)(e-s+1)));
      }
      break;
    }

    // if we iterated until 4 we can break out
    if(count == 4)
      break;

    count++;

    // set the next start to our last search
    if(*e)
    {
      // skip leading spaces
      while(*e && isspace(*e))
        e++;

      s = e;
    }
    else
      break;
  }

  // then format a standard DateStamp string like string
  // so that we can use StrToDate()
  snprintf(tdate, sizeof(tdate), "%02d-%02d-%02d", mon, day, year % 100);
  snprintf(ttime, sizeof(ttime), "%02d:%02d:%02d", hour, min, sec);
  dt.dat_Format  = FORMAT_USA;
  dt.dat_Flags   = 0;
  dt.dat_StrDate = tdate;
  dt.dat_StrTime = ttime;
  if(StrToDate(&dt))
  {
    struct DateStamp *ds = &dt.dat_Stamp;

    // save the timezone
    mail->tzone = TZtoMinutes(tzone);

    // bring the date in relation to UTC
    ds->ds_Minute -= mail->tzone;

    // we need to check the datestamp variable that it is still in it`s borders
    // after the UTC correction
    while(ds->ds_Minute < 0)     { ds->ds_Minute += 1440; ds->ds_Days--; }
    while(ds->ds_Minute >= 1440) { ds->ds_Minute -= 1440; ds->ds_Days++; }

    // now we do copy the datestamp stuff over the one from our mail
    memcpy(&mail->Date, ds, sizeof(struct DateStamp));

    success = TRUE;
  }

  RETURN(success);
  return success;
}
///
/// MA_ExamineMail
//  Parses the header lines of a message and fills email structure
struct ExtendedMail *MA_ExamineMail(const struct Folder *folder, const char *file, const BOOL deep)
{
  struct ExtendedMail *email;
  static struct Person pe;
  struct MinList headerList;
  struct Mail *mail;
  char *p;
  char fullfile[SIZE_PATHFILE];
  int ok;
  BOOL dateFound = FALSE;
  FILE *fh;

  ENTER();

  // first we generate a new ExtendedMail buffer
  if((email = calloc(1, sizeof(struct ExtendedMail))) == NULL)
  {
    RETURN(NULL);
    return NULL;
  }

  mail = &email->Mail;
  strlcpy(mail->MailFile, file, sizeof(mail->MailFile));
  email->DelSend = !C->SaveSent;
  if((fh = fopen(GetMailFile(fullfile, folder, mail), "r")) != NULL)
  {
    setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);

    // if the first three bytes are 'X' 'P' 'K', then this is an XPK packed
    // file and we have to unpack it first.
    if(fgetc(fh) == 'X' && fgetc(fh) == 'P' && fgetc(fh) == 'K')
    {
      // temporary close the file
      fclose(fh);

      // then unpack the file with XPK routines.
      if(!StartUnpack(GetMailFile(NULL, folder, mail), fullfile, folder))
      {
        free(email);

        E(DBF_MAIL, "couldn't unpack mailfile");

        RETURN(NULL);
        return NULL;
      }

      // reopen it again.
      if((fh = fopen(fullfile, "r")) != NULL)
        setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);
    }
    else
      rewind(fh); // rewind the file handle to the start
  }
  else
    E(DBF_MAIL, "couldn't open mail file for reading main header");

  // check if the file handle is valid and then immediatly read in the
  // header lines
  if(fh != NULL && MA_ReadHeader(fullfile, fh, &headerList, RHM_MAINHEADER) == TRUE)
  {
     char *ptr;
     char dateFilePart[12+1];
     char timebuf[sizeof(struct TimeVal)+1]; // +1 because the b64decode does set a NUL byte
     struct MinNode *curNode = headerList.mlh_Head;
     LONG size;

     // Now we process the read header to set all flags accordingly
     for(ok=0; curNode->mln_Succ; curNode = curNode->mln_Succ)
     {
        struct HeaderNode *hdrNode = (struct HeaderNode *)curNode;
        char *field = hdrNode->name;
        char *value = hdrNode->content;

        if(stricmp(field, "from") == 0)
        {
          SET_FLAG(ok, 1);

          // find out if there are more than one From: address
          if((p = MyStrChr(value, ',')) != NULL)
           *p++ = '\0';

          // extract the main mail address
          ExtractAddress(value, &pe);
          mail->From = pe;

          // if we have more addresses waiting we
          // go and process them yet
          if(p)
          {
            if(deep)
            {
              if(email->NoSFrom == 0)
                email->NoSFrom = MA_GetRecipients(p, &(email->SFrom));

              if(email->NoSFrom > 0)
                SET_FLAG(mail->mflags, MFLAG_MULTISENDER);
            }
            else if(strlen(p) >= 7) // minimum rcpts size "a@bc.de"
              SET_FLAG(mail->mflags, MFLAG_MULTISENDER);
          }

          D(DBF_MIME, "'From' senders: %ld", email->NoSFrom+1);
        }
        else if(stricmp(field, "reply-to") == 0)
        {
          SET_FLAG(ok, 8);

          // find out if there are more than one ReplyTo: address
          if((p = MyStrChr(value, ',')))
           *p++ = '\0';

          ExtractAddress(value, &pe);
          mail->ReplyTo = pe;

          // if we have more addresses waiting we
          // go and process them yet
          if(p)
          {
            if(deep)
            {
              if(email->NoSReplyTo == 0)
                email->NoSReplyTo = MA_GetRecipients(p, &(email->SReplyTo));

              if(email->NoSReplyTo > 0)
                SET_FLAG(mail->mflags, MFLAG_MULTIREPLYTO);
            }
            else if(strlen(p) >= 7) // minimum rcpts size "a@bc.de"
              SET_FLAG(mail->mflags, MFLAG_MULTIREPLYTO);
          }

          D(DBF_MIME, "'ReplyTo' recipients: %ld", email->NoSReplyTo+1);
        }
        else if(stricmp(field, "original-recipient") == 0)
        {
          ExtractAddress(value, &pe);
          email->OriginalRcpt = pe;
        }
        else if(stricmp(field, "return-path") == 0)
        {
          ExtractAddress(value, &pe);
          email->ReturnPath = pe;
        }
        else if(stricmp(field, "disposition-notification-to") == 0 ||
                stricmp(field, "return-receipt-to") == 0)
        {
          ExtractAddress(value, &pe);
          email->ReceiptTo = pe;
          SET_FLAG(mail->mflags, MFLAG_SENDMDN);
        }
        else if(stricmp(field, "to") == 0)
        {
          if((ok & 2) == 0)
          {
            SET_FLAG(ok, 2);
            if((p = MyStrChr(value, ',')) != NULL)
              *p++ = '\0';

            ExtractAddress(value, &pe);
            mail->To = pe;
            if(p != NULL)
            {
              if(deep)
              {
                if(email->NoSTo == 0)
                  email->NoSTo = MA_GetRecipients(p, &(email->STo));

                if(email->NoSTo > 0)
                  SET_FLAG(mail->mflags, MFLAG_MULTIRCPT);
              }
              else if(strlen(p) >= 7) // minimum rcpts size "a@bc.de"
                SET_FLAG(mail->mflags, MFLAG_MULTIRCPT);
            }

            D(DBF_MIME, "'To:' recipients: %ld", email->NoSTo+1);
          }
        }
        else if(stricmp(field, "cc") == 0)
        {
          if(deep)
          {
            if(email->NoCC == 0)
              email->NoCC = MA_GetRecipients(value, &(email->CC));

            D(DBF_MIME, "'Cc:' recipients: %ld", email->NoCC);

            if(email->NoCC > 0)
              SET_FLAG(mail->mflags, MFLAG_MULTIRCPT);
          }
          else if(strlen(value) >= 7) // minimum rcpts size "a@bc.de"
            SET_FLAG(mail->mflags, MFLAG_MULTIRCPT);
        }
        else if(stricmp(field, "bcc") == 0)
        {
          if(deep)
          {
            if(email->NoBCC == 0)
              email->NoBCC = MA_GetRecipients(value, &(email->BCC));

            D(DBF_MIME, "'BCC:' recipients: %ld", email->NoBCC);

            if(email->NoBCC > 0)
              SET_FLAG(mail->mflags, MFLAG_MULTIRCPT);
          }
          else if(strlen(value) >= 7) // minimum rcpts size "a@bc.de"
            SET_FLAG(mail->mflags, MFLAG_MULTIRCPT);
        }
        else if(stricmp(field, "subject") == 0)
        {
          SET_FLAG(ok, 4);
          strlcpy(mail->Subject, Trim(value), sizeof(mail->Subject));
        }
        else if(stricmp(field, "message-id") == 0)
        {
          mail->cMsgID = CompressMsgID(p = Trim(value));
          strlcpy(email->MsgID, p, sizeof(email->MsgID));
        }
        else if(stricmp(field, "in-reply-to") == 0)
        {
          mail->cIRTMsgID = CompressMsgID(p = Trim(value));
          strlcpy(email->IRTMsgID, p, sizeof(email->IRTMsgID));
        }
        else if(stricmp(field, "date") == 0)
        {
          dateFound = MA_ScanDate(mail, value);
        }
        else if(stricmp(field, "importance") == 0)
        {
          if(getImportanceLevel(mail) == IMP_NORMAL)
          {
            p = Trim(value);
            if(!stricmp(p, "high"))
              setImportanceLevel(mail, IMP_HIGH);
            else if(!stricmp(p, "low"))
              setImportanceLevel(mail, IMP_LOW);
          }
        }
        else if(stricmp(field, "priority") == 0)
        {
          if(getImportanceLevel(mail) == IMP_NORMAL)
          {
            p = Trim(value);
            if(stricmp(p, "urgent") == 0)
              setImportanceLevel(mail, IMP_HIGH);
            else if(stricmp(p, "non-urgent") == 0)
              setImportanceLevel(mail, IMP_HIGH);
          }
        }
        else if(stricmp(field, "content-type") == 0)
        {
          p = Trim(value);
          if(strnicmp(p, "multipart", 9) == 0)
          {
            p += 10;

            // we do specify the multipart content-type in
            // accordance to RFC 2046/RFC2387
            if(strnicmp(p, "mixed", 5) == 0)             // RFC 2046 (5.1.3)
              SET_FLAG(mail->mflags, MFLAG_MP_MIXED);
            else if(strnicmp(p, "alternative", 11) == 0) // RFC 2046 (5.1.4)
              SET_FLAG(mail->mflags, MFLAG_MP_ALTERN);
            else if(strnicmp(p, "report", 6) == 0)       // RFC 3462
              SET_FLAG(mail->mflags, MFLAG_MP_REPORT);
            else if(strnicmp(p, "encrypted", 9) == 0)    // RFC 1847 (2.2)
              SET_FLAG(mail->mflags, MFLAG_MP_CRYPT);
            else if(strnicmp(p, "signed", 6) == 0)       // RFC 1847 (2.1)
              SET_FLAG(mail->mflags, MFLAG_MP_SIGNED);
            else
            {
              // "mixed" is the primary subtype and in fact RFC 2046 (5.1.7)
              // suggests to fall back to mixed if a MIME subtype is unknown
              // to a MIME parser, which we do here now.
              SET_FLAG(mail->mflags, MFLAG_MP_MIXED);
            }
          }
          else if(strnicmp(p, "message/partial", 15) == 0) // RFC 2046 (5.2.2)
          {
            SET_FLAG(mail->mflags, MFLAG_PARTIAL);
          }
        }
        else if(stricmp(field, "x-senderinfo") == 0)
        {
           SET_FLAG(mail->mflags, MFLAG_SENDERINFO);
           if(deep)
             email->SenderInfo = StrBufCpy(email->SenderInfo, value);
        }
        else if(deep) // and if we end up here we check if we really have to go further
        {
          if(stricmp(field, "x-yam-options") == 0)
          {
            enum Security sec;

            if(strstr(value, "delsent") != NULL)
              email->DelSend = TRUE;

            if((p = strstr(value, "sigfile")) != NULL)
              email->Signature = p[7]-'0'+1;

            for(sec = SEC_SIGN; sec <= SEC_SENDANON; sec++)
            {
              if(strstr(value, SecCodes[sec]))
                email->Security = sec;
            }
          }
          else if(strnicmp(field, "x-yam-header-", 13) == 0)
          {
            email->extraHeaders = StrBufCat(StrBufCat(email->extraHeaders, &field[13]), ":");
            email->extraHeaders = StrBufCat(StrBufCat(email->extraHeaders, value), "\\n");
          }
        }

     }

     // if now the mail is still not MULTIPART we have to check for uuencoded attachments
     if(!isMP_MixedMail(mail) && MA_DetectUUE(fh))
       SET_FLAG(mail->mflags, MFLAG_MP_MIXED);

     // And now we close the Mailfile and clear the temporary headerList again
     fclose(fh);
     FreeHeaderList(&headerList);

     // in case the replyTo recipient doesn't have a realname yet and it is
     // completly the same like the from address we go and copy the realname as both
     // are the same.
     if((ok & 8) && !mail->ReplyTo.RealName[0] && !stricmp(mail->ReplyTo.Address, mail->From.Address))
       strlcpy(mail->ReplyTo.RealName, mail->From.RealName, sizeof(mail->ReplyTo.RealName));

     // if this function call has a folder of NULL then we are examining a virtual mail
     // which means this mail doesn't have any folder and also no filename that may contain
     // any usable date or stuff
     if(folder != NULL)
     {
       // now we take the filename of our mailfile into account to check for
       // the transfer date at the start of the name and for the set status
       // flags at the end of it.
       strlcpy(dateFilePart, mail->MailFile, sizeof(dateFilePart));

       // make sure there is no "-" in the base64 encoded part as we just mapped
       // the not allowed "/" to "-" to make it possible to use base64 for
       // the timeval encoding
       ptr = dateFilePart;
       while((ptr = strchr(ptr, '-')))
         *ptr = '/';

       // lets decode the base64 encoded timestring in a temporary buffer
       if(base64decode(timebuf, (unsigned char *)dateFilePart, 12) <= 0)
       {
         W(DBF_FOLDER, "WARNING: failure in decoding the encoded date from mailfile: '%s'", mail->MailFile);

         // if we weren`t able to decode the base64 encoded string
         // we have to validate the transDate so that the calling function
         // recognizes to rewrite the comment with a valid string.
         mail->transDate.Seconds      = 0;
         mail->transDate.Microseconds = 0;
       }
       else
       {
         // everything seems to have worked so lets copy the binary data in our
         // transDate structure
         memcpy(&mail->transDate, timebuf, sizeof(struct TimeVal));
       }

       // now grab the status out of the end of the mailfilename
       ptr = &mail->MailFile[17];
       while(*ptr != '\0')
       {
         if(*ptr >= '1' && *ptr <= '7')
         {
           setPERValue(mail, *ptr-'1'+1);
         }
         else
         {
           switch(*ptr)
           {
             case SCHAR_READ:
               SET_FLAG(mail->sflags, SFLAG_READ);
             break;

             case SCHAR_REPLIED:
               SET_FLAG(mail->sflags, SFLAG_REPLIED);
             break;

             case SCHAR_FORWARDED:
               SET_FLAG(mail->sflags, SFLAG_FORWARDED);
             break;

             case SCHAR_NEW:
               SET_FLAG(mail->sflags, SFLAG_NEW);
             break;

             case SCHAR_QUEUED:
               SET_FLAG(mail->sflags, SFLAG_QUEUED);
             break;

             case SCHAR_HOLD:
               SET_FLAG(mail->sflags, SFLAG_HOLD);
             break;

             case SCHAR_SENT:
               SET_FLAG(mail->sflags, SFLAG_SENT);
             break;

             case SCHAR_DELETED:
               SET_FLAG(mail->sflags, SFLAG_DELETED);
             break;

             case SCHAR_MARKED:
               SET_FLAG(mail->sflags, SFLAG_MARKED);
             break;

             case SCHAR_ERROR:
               SET_FLAG(mail->sflags, SFLAG_ERROR);
             break;

             case SCHAR_USERSPAM:
               SET_FLAG(mail->sflags, SFLAG_USERSPAM);
             break;

             case SCHAR_AUTOSPAM:
               SET_FLAG(mail->sflags, SFLAG_AUTOSPAM);
             break;

             case SCHAR_HAM:
               SET_FLAG(mail->sflags, SFLAG_HAM);
             break;
           }
         }

         ptr++;
       }
     }

     // if we didn't find a Date: header we take the transfered date (if found)
     if(dateFound == FALSE)
     {
       if(mail->transDate.Seconds > 0)
       {
         // convert the UTC transDate to a UTC mail Date
         TimeVal2DateStamp(&mail->transDate, &mail->Date, TZC_NONE);
       }
       else
       {
         struct DateStamp *ds;

         // and as a fallback we take the date of the mail file
         if(ObtainFileInfo(mail->MailFile, FI_DATE, &ds) == TRUE)
         {
           memcpy(&mail->Date, ds, sizeof(struct DateStamp));
           DateStampTZConvert(&mail->Date, TZC_UTC);
           free(ds);
         }
       }

       // set the timeZone to our local one
       mail->tzone = C->TimeZone;
     }

     // lets calculate the mailSize out of the FileSize() function
     if(ObtainFileInfo(fullfile, FI_SIZE, &size) == TRUE)
       mail->Size = size;
     else
       mail->Size = -1;

     FinishUnpack(fullfile);

     RETURN(email);
     return email;
  }
  else
    E(DBF_MAIL, "couldn't read/parse mail header of mail file '%s'", fullfile);

  FinishUnpack(fullfile);

  // finish up everything before we exit with an error
  if(fh)
   fclose(fh);

  free(email);

  RETURN(NULL);
  return NULL;
}

///
/// MA_ScanMailBox
//  Scans for message files in a folder directory
static BOOL MA_ScanMailBox(struct Folder *folder)
{
  long filecount;
  BOOL result = TRUE;
  static BOOL alreadyScanning = FALSE;

  ENTER();

  filecount = FileCount(GetFolderDir(folder), NULL);

  // check if there are files in this mailbox or not.
  if(filecount < 0)
  {
    // an error happened in FileCount()
    result = FALSE;
  }
  else if(filecount == 0)
  {
    // an empty directory is ok
    result = TRUE;
  }
  else
  {
    // check if we are already in this function or not
    // (due to a previously started scanning
    if(alreadyScanning == TRUE)
    {
      result = FALSE;
    }
    else
    {
      struct MA_GUIData *gui = &G->MA->GUI;
      APTR context;

      // make sure others notice that an index scanning already
      // runs
      alreadyScanning = TRUE;

      // now we make sure some GUI components will be disabled
      // or cleared if the rescanning folder is the current one
      if(FO_GetCurrentFolder() == folder)
      {
        // before we go and rebuild the index of the folder we make
        // sure all major GUI components of it are disabled for the
        // time being...
        xset(gui->PG_MAILLIST, MUIA_Disabled, TRUE,
                               MUIA_ShortHelp, NULL);
        set(gui->NL_FOLDERS, MUIA_ShortHelp, NULL);
        DoMethod(gui->PG_MAILLIST, MUIM_NList_Clear);

        // and now we also make sure an eventually enabled preview pane
        // is disabled as well.
        if(C->QuickSearchBar == TRUE)
          set(gui->GR_QUICKSEARCHBAR, MUIA_Disabled, TRUE);

        // also set an embedded read pane as disabled.
        if(C->EmbeddedReadPane == TRUE)
        {
          DoMethod(gui->MN_EMBEDDEDREADPANE, MUIM_ReadMailGroup_Clear, MUIF_NONE);
          set(gui->MN_EMBEDDEDREADPANE, MUIA_Disabled, TRUE);
        }
      }

      BusyGaugeInt(tr(MSG_BusyScanning), folder->Name, filecount-1);
      ClearMailList(folder, TRUE);

      D(DBF_FOLDER, "Scanning folder: '%s' (path '%s', %ld files)...", folder->Name, GetFolderDir(folder), filecount);

      if((context = ObtainDirContextTags(EX_StringName, (ULONG)GetFolderDir(folder), TAG_DONE)) != NULL)
      {
        struct ExamineData *ed;
        long processedFiles = 0;
        BOOL convertAllOld = FALSE;
        BOOL skipAllOld = FALSE;
        BOOL convertAllUnknown = FALSE;
        BOOL skipAllUnknown = FALSE;
        BOOL ignoreInvalids = FALSE;

        // Now that the folder is locked we go and define its loaded
        // mode to LM_REBUILD so that others don't try to access it
        // anymore.
        folder->LoadedMode = LM_REBUILD;
        // visually update this state change
        DoMethod(gui->NL_FOLDERS, MUIM_NListtree_Redraw, FO_GetFolderTreeNode(folder), MUIF_NONE);

        while((ed = ExamineDir(context)) != NULL)
        {
          // set the gauge and check the stopButton status as well.
          if(BusySet(processedFiles++) == FALSE)
          {
            D(DBF_FOLDER, "scan process aborted by user");
            result = FALSE;
            break;
          }

          // give the GUI the chance to refresh
          DoMethod(G->App,MUIM_Application_InputBuffered);

          // then check whether this is a file as we don't care for subdirectories
          if(EXD_IS_FILE(ed))
          {
            // check whether the filename is a valid mailfilename
            char fbuf[SIZE_PATHFILE+1];
            char *fname = (char *)ed->Name;
            BOOL validMailFile = isValidMailFile(fname);

            if(validMailFile == FALSE)
            {
              // ok, the file doesn't seem to have to be a valid mailfilename, so
              // lets see if it is an "old" file (<= YAM2.4) or just trash
              int i = 0;
              BOOL oldFound = TRUE;

              do
              {
                // on position 5 should be a colon
                if(i == 5)
                {
                  if(fname[i] != '.')
                  {
                    oldFound = FALSE;
                    break;
                  }
                }
                else if(!isdigit(fname[i]))
                {
                  oldFound = FALSE;
                  break;
                }
              }
              while(fname[++i] != '\0');

              // check if our test was successfully and we found and old style
              // filename
              if(oldFound == TRUE)
              {
                int res;
                BOOL convertOnce = FALSE;

                // ok we seem to have found an old-fashioned mailfile, so lets
                // convert it to the newstyle
                W(DBF_FOLDER, "found < v2.5 style mailfile: '%s'", fname);

                // lets ask if the user wants to convert the file or not
                if(convertAllOld == FALSE && skipAllOld == FALSE)
                {
                  res = MUI_Request(G->App, NULL, 0,
                                    tr(MSG_MA_CREQ_OLDFILE_TITLE),
                                    tr(MSG_MA_YESNOTOALL),
                                    tr(MSG_MA_CREQUEST_OLDFILE),
                                    fname, folder->Name);

                  // if the user has clicked on Yes or YesToAll then
                  // set the flags accordingly
                  if(res == 0)
                    skipAllOld = TRUE;
                  else if(res == 1)
                    convertOnce = TRUE;
                  else if(res == 2)
                    convertAllOld = TRUE;
                }

                if(convertAllOld == TRUE || convertOnce == TRUE)
                {
                  char *newfname;

                  // now we finally convert the file to a new style mail file
                  if((newfname = MA_ConvertOldMailFile(fname, folder)) == NULL)
                  {
                    // if there occurred any error we skip to the next file.
                    ER_NewError(tr(MSG_ER_CONVERTMFILE), fname, folder->Name);
                    continue;
                  }
                  else
                  {
                    // use the new name from now on
                    fname = newfname;
                  }
                }
                else
                  continue;
              }
              else if(fname[0] != '.')
              {
                // to make it as convienent as possible for a user we also allow
                // to copy mail files to a folder directory without having to take
                // care that they have the correct filename.
                int res;
                BOOL convertOnce = FALSE;

                W(DBF_FOLDER, "found unknown file: '%s'", fname);

                // lets ask if the user wants to convert the file or not
                if(convertAllUnknown == FALSE && skipAllUnknown == FALSE)
                {
                  res = MUI_Request(G->App, NULL, 0,
                                    tr(MSG_MA_CREQ_UNKNOWN_TITLE),
                                    tr(MSG_MA_YESNOTOALL),
                                    tr(MSG_MA_CREQUEST_UNKNOWN),
                                    fname, folder->Name);

                  // if the user has clicked on Yes or YesToAll then
                  // set the flags accordingly
                  if(res == 0)
                    skipAllUnknown = TRUE;
                  else if(res == 1)
                    convertOnce = TRUE;
                  else if(res == 2)
                    convertAllUnknown = TRUE;
                }

                if(convertAllUnknown == TRUE || convertOnce == TRUE)
                {
                  // now it is our job to get a new mailfile name and replace the old one
                  char oldfile[SIZE_PATHFILE+1];
                  char *newfile;

                  AddPath(oldfile, GetFolderDir(folder), fname, sizeof(oldfile));
                  if((newfile = MA_NewMailFile(folder, fbuf)) != NULL)
                  {
                    if(Rename(oldfile, newfile))
                    {
                      fname = fbuf;
                    }
                    else
                    {
                      // if there occurred any error we skip to the next file.
                      ER_NewError(tr(MSG_ER_CONVERTMFILE), fname, folder->Name);
                      continue;
                    }
                  }
                  else
                  {
                    // if there occurred any error we skip to the next file.
                    ER_NewError(tr(MSG_ER_CONVERTMFILE), fname, folder->Name);
                    continue;
                  }
                }
                else
                  continue;
              }
              else
                continue;
            }

            // check the filesize of the mail file
            if(ed->FileSize > 0)
            {
              struct ExtendedMail *email;

              D(DBF_FOLDER, "examining MailFile: %s", fname);

              while((email = MA_ExamineMail(folder, fname, FALSE)) == NULL &&
                    ignoreInvalids == FALSE)
              {
                // if the MA_ExamineMail() operation failed we
                // warn the user and ask him how to proceed with
                // the file

                int res = MUI_Request(G->App, NULL, 0,
                                     tr(MSG_MA_INVALIDMFILE_TITLE),
                                     tr(MSG_MA_INVALIDMFILE_BT),
                                     tr(MSG_MA_INVALIDMFILE),
                                     fname, folder->Name);

                if(res == 0) // cancel/ESC
                {
                  result = FALSE;
                  break;
                }
                else if(res == 1) // Retry
                  continue;
                else if(res == 2) // Ignore
                  break;
                else if(res == 3) // Ignore All
                {
                  ignoreInvalids = TRUE;
                  break;
                }
                else if(res == 4) // Delete
                {
                  char path[SIZE_PATHFILE+1];

                  AddPath(path, GetFolderDir(folder), fname, sizeof(path));
                  DeleteFile(path);

                  break;
                }
              }

              if(email != NULL)
              {
                struct Mail *newMail;

                if((newMail = AddMailToList(&email->Mail, folder)) != NULL)
                {
                  // if this new mail hasn`t got a valid transDate we have to check if we
                  // have to take the fileDate as a fallback value.
                  if(newMail->transDate.Seconds == 0)
                  {
                    // only if it is _not_ a "waitforsend" and "hold" message we can take the fib_Date
                    // as the fallback
                    if(!hasStatusQueued(newMail) && !hasStatusHold(newMail))
                    {
                      struct DateStamp *ds;

                      W(DBF_FOLDER, "no transfer Date information found in mail file, taking fileDate...");

                      // obtain the datestamp information from  and as a fallback we take the date of the mail file
                      if(ObtainFileInfo(GetMailFile(NULL, folder, newMail), FI_DATE, &ds) == TRUE)
                      {
                        // now convert the local TZ fib_Date to a UTC transDate
                        DateStamp2TimeVal(ds, &newMail->transDate, TZC_UTC);
                        free(ds);
                      }

                      // then we update the mailfilename
                      MA_UpdateMailFile(newMail);
                    }
                  }
                }

                MA_FreeEMailStruct(email);
              }
            }
            else
            {
              char path[SIZE_PATHFILE+1];

              AddPath(path, GetFolderDir(folder), fname, sizeof(path));
              DeleteFile(path);

              W(DBF_FOLDER, "found empty file '%s' in mail folder and deleted it", path);
            }
          }
        }

        ReleaseDirContext(context);
      }
      else
      {
        W(DBF_FOLDER, "couldn't allocate DirContext structure for directory '%s', IoErr()=%ld", GetFolderDir(folder), IoErr());
        result = FALSE;
      }

      D(DBF_FOLDER, "Scanning finished %s", result ? "successfully" : "unsuccessfully");

      BusyEnd();

      // make sure others can use this function again
      alreadyScanning = FALSE;
    }
  }

  RETURN(result);
  return result;
}
///

/*** Hooks ***/
/// MA_MakeFOFormat
//  Creates format definition for folder listview
void MA_MakeFOFormat(Object *lv)
{
  static const int defwidth[FOCOLNUM] = { -1,-1,-1,-1,-1 };
  char format[SIZE_LARGE];
  BOOL first = TRUE;
  int i;

  ENTER();

  format[0] = '\0';
  for(i = 0; i < FOCOLNUM; i++)
  {
    if(isFlagSet(C->FolderCols, (1<<i)))
    {
      int p;

      if(first)
        first = FALSE;
      else
        strlcat(format, " BAR,", sizeof(format));

      p = strlen(format);
      snprintf(&format[p], sizeof(format)-p,  "COL=%d W=%d", i, defwidth[i]);

      if(i > 0)
        strlcat(format, " P=\033r", sizeof(format));
    }
  }
  strlcat(format, " BAR", sizeof(format));

  set(lv, MUIA_NListtree_Format, format);

  LEAVE();
}
///

