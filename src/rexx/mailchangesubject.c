/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 Marcel Beck
 Copyright (C) 2000-2025 YAM Open Source Team

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
 YAM OpenSource project    :  https://github.com/jens-maus/yam/

***************************************************************************/

#include <clib/alib_protos.h>
#include <mui/NList_mcc.h>
#include <proto/exec.h>

#include "extrasrc.h"

#include "YAM.h"

#include "mui/ClassesExtra.h"
#include "mui/MainMailListGroup.h"

#include "MailList.h"
#include "MUIObjects.h"
#include "Rexx.h"

#include "Debug.h"

struct args
{
  char *subject;
};

void rx_mailchangesubject(UNUSED struct RexxHost *host, struct RexxParams *params, enum RexxAction action, UNUSED struct RexxMsg *rexxmsg)
{
  struct args *args = params->args;

  ENTER();

  switch(action)
  {
    case RXIF_INIT:
    {
      params->args = AllocVecPooled(G->SharedMemPool, sizeof(*args));
    }
    break;

    case RXIF_ACTION:
    {
      struct MailList *mlist;
      Object *lv = (Object *)xget(G->MA->GUI.PG_MAILLIST, MUIA_MainMailListGroup_MainList);

      if((mlist = MA_CreateMarkedList(lv, FALSE)) != NULL)
      {
        struct MailNode *mnode;

        ForEachMailNode(mlist, mnode)
        {
          struct Mail *mail = mnode->mail;

          if(mail != NULL)
            MA_ChangeSubject(mail, args->subject);
        }

        DeleteMailList(mlist);

        DoMethod(lv, MUIM_NList_Redraw, MUIV_NList_Redraw_All);
      }
      else
        params->rc = RETURN_ERROR;
    }
    break;

    case RXIF_FREE:
    {
      if(args != NULL)
        FreeVecPooled(G->SharedMemPool, args);
    }
    break;
  }

  LEAVE();
}
