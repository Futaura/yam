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

#include "SDI_compiler.h"
#include "SDI_stdarg.h"

#include <proto/dos.h>
#include <proto/utility.h>

#include "YAM_stringsizes.h"
#include "YAM_utilities.h"

#include "Debug.h"

struct DirContext
{
  BPTR lock;
  BPTR oldCD;
  struct ExAllControl *eaControl;
  struct ExAllData *eaBuffer;
  struct ExAllData *eaData;
  ULONG dataFields;
  LONG more;
  LONG exAllError;
  BOOL restoreOldCD;
  struct ExamineData exData;
};

/// ObtainDirContext
// obtain a directory scanning context for a given directory, including pattern matching
APTR ObtainDirContext(struct TagItem *tags)
{
  struct DirContext *ctx;
  BOOL success = FALSE;

  ENTER();

  SHOWTAGS(tags);

  if((ctx = (struct DirContext *)calloc(1, sizeof(*ctx))) != NULL)
  {
    char *dir;

    if((dir = (char *)GetTagData(EX_StringName, (ULONG)NULL, tags)) != NULL)
    {
      SHOWSTRING(DBF_FOLDER, dir);

      if((ctx->lock = Lock(dir, SHARED_LOCK)))
      {
        if((ctx->eaControl = AllocDosObject(DOS_EXALLCONTROL, NULL)) != NULL)
        {
          ctx->eaControl->eac_LastKey = 0;
          ctx->eaControl->eac_MatchString = (STRPTR)GetTagData(EX_MatchString, (ULONG)NULL, tags);
          ctx->eaControl->eac_MatchFunc = NULL;

          SHOWSTRING(DBF_FOLDER, ctx->eaControl->eac_MatchString);

          if((ctx->eaBuffer = malloc(SIZE_EXALLBUF)) != NULL)
          {
            if(GetTagData(EX_DoCurrentDir, FALSE, tags) == TRUE)
            {
              // change the current directory to the one given
              ctx->oldCD = CurrentDir(ctx->lock);
              ctx->restoreOldCD = TRUE;
            }
            else
              ctx->restoreOldCD = FALSE;

            ctx->dataFields = GetTagData(EX_DataFields, 0xffffffff, tags);
            SHOWVALUE(DBF_FOLDER, ctx->dataFields);

            // we have no information obtained yet
            ctx->eaData = NULL;

            // start with a faked yet unterminated but still successful call to ExAll()
            ctx->more = 1;
            ctx->exAllError = ERROR_NO_MORE_ENTRIES;

            success = TRUE;
          }
        }
      }
    }
  }

  // if anything failed, then free all the stuff allocated so far
  if(success == FALSE)
  {
    ReleaseDirContext(ctx);
    ctx = NULL;
  }

  RETURN(ctx);
  return ctx;
}

///
/// ObtainDirContextTags
// varargs implementation of ObtainDirContext()
#if !defined(PPC)
APTR VARARGS68K ObtainDirContextTags(ULONG tag1, ...)
{
  return ObtainDirContext((APTR)&tag1);
}
#endif

///
/// ReleaseDirContext
// free all resources allocated by ObtainDirContext()
void ReleaseDirContext(APTR context)
{
  ENTER();

  if(context != NULL)
  {
    struct DirContext *ctx = (struct DirContext *)context;

    if(ctx->restoreOldCD == TRUE)
      CurrentDir(ctx->oldCD);

    if(ctx->eaBuffer != NULL)
      free(ctx->eaBuffer);

    if(ctx->eaControl != NULL)
      FreeDosObject(DOS_EXALLCONTROL, ctx->eaControl);

    if(ctx->lock)
      UnLock(ctx->lock);

    free(context);
  }

  LEAVE();
}

///
/// ExamineDir
// iterate one step through the directory scanning context and
// return the next entry, or NULL if there are no more
struct ExamineData *ExamineDir(APTR context)
{
  struct DirContext *ctx = (struct DirContext *)context;
  struct ExamineData *ed = NULL;

  ENTER();

  // did we reach the end of the buffer in the previous call?
  if(ctx->eaData == NULL)
  {
    if(ctx->more == 0)
    {
      // restore the error number of the last ExAll() call
      SetIoErr(ctx->exAllError);
    }
    else
    {
      // then do another ExAll() call to get more data
      ctx->more = ExAll(ctx->lock, ctx->eaBuffer, SIZE_EXALLBUF, ED_COMMENT, ctx->eaControl);

      if(ctx->more == 0)
      {
        // preserve the error number of this last call, this will be restored later
        // make sure to return "no more entries" in case the filesystem reported no error
        if((ctx->exAllError = IoErr()) == 0)
          ctx->exAllError = ERROR_NO_MORE_ENTRIES;
      }
      if(ctx->more != 0 || ctx->eaControl->eac_Entries > 0)
      {
        // either there is more outstanding data not yet fitting in our buffer (more != 0)
        // or we got at least one entry to handle
        ctx->eaData = ctx->eaBuffer;
      }

      SHOWVALUE(DBF_FOLDER, ctx->more);
      SHOWVALUE(DBF_FOLDER, ctx->exAllError);
      SHOWVALUE(DBF_FOLDER, ctx->eaControl->eac_Entries);
    }
  }

  if(ctx->eaData != NULL && ctx->eaControl->eac_Entries > 0)
  {
    // return the prepared data structure
    ed = &ctx->exData;

    // copy over the data we might be interested in
    ed->Name = isFlagSet(ctx->dataFields, EXF_NAME) ? ctx->eaData->ed_Name : NULL;
    ed->FileSize = ctx->eaData->ed_Size;

    // convert the ExAll() type to ExamineDir() style
    if(EAD_IS_FILE(ctx->eaData))
      ed->Type = FSO_TYPE_FILE;
    else if(EAD_IS_DRAWER(ctx->eaData))
      ed->Type = FSO_TYPE_DIRECTORY;
    else if(EAD_IS_SOFTLINK(ctx->eaData))
      ed->Type = FSO_TYPE_SOFTLINK;

    SHOWVALUE(DBF_FOLDER, ed->Type);
    SHOWVALUE(DBF_FOLDER, ed->FileSize);
    SHOWSTRING(DBF_FOLDER, ed->Name);

    // and advance to the next item for the next call
    ctx->eaData = ctx->eaData->ed_Next;
  }

  RETURN(ed);
  return ed;
}

///

