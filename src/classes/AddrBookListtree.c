/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 by Marcel Beck <mbeck@yam.ch>
 Copyright (C) 2000-2007 by YAM Open Source Team

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

 Superclass:  MUIC_NListtree
 Description: NListtree class of the addressbook

***************************************************************************/

#include "AddrBookListtree_cl.h"

#include "Debug.h"

/* CLASSDATA
struct Data
{
  Object *listImage;
};
*/

/* Overloaded Methods */
/// OVERLOAD(OM_NEW)
OVERLOAD(OM_NEW)
{
  struct Data *data;

  ENTER();

  if(!(obj = DoSuperNew(cl, obj,
    TAG_MORE, inittags(msg))))
  {
    RETURN(0);
    return 0;
  }

  data = (struct Data *)INST_DATA(cl,obj);

  // prepare the group image
  data->listImage = MakeImageObject("status_group");
  DoMethod(obj, MUIM_NList_UseImage, data->listImage, 0, MUIF_NONE);

  RETURN((ULONG)obj);
  return (ULONG)obj;
}

///
/// OVERLOAD(OM_DISPOSE)
OVERLOAD(OM_DISPOSE)
{
  GETDATA;

  // make sure that we free our group image
  DoMethod(obj, MUIM_NList_UseImage, NULL, 0, MUIF_NONE);
  MUI_DisposeObject(data->listImage);
  data->listImage = NULL;

  return DoSuperMethodA(cl,obj,msg);
}

///
/// OVERLOAD(MUIM_DragQuery)
OVERLOAD(MUIM_DragQuery)
{
  struct MUIP_DragQuery *d = (struct MUIP_DragQuery *)msg;

  if(DoMethod(G->MA->GUI.PG_MAILLIST, MUIM_MainMailListGroup_IsMailList, d->obj) == TRUE)
    return MUIV_DragQuery_Accept;

  return DoSuperMethodA(cl,obj,msg);
}

///
/// OVERLOAD(MUIM_DragDrop)
OVERLOAD(MUIM_DragDrop)
{
  struct MUIP_DragQuery *d = (struct MUIP_DragQuery *)msg;

  if(DoMethod(G->MA->GUI.PG_MAILLIST, MUIM_MainMailListGroup_IsMailList, d->obj) == TRUE)
  {
    struct Mail **mlist = MA_CreateMarkedList(d->obj, FALSE);
    if(mlist)
    {
      MA_GetAddress(mlist);
      free(mlist);
    }
  }

  return DoSuperMethodA(cl,obj,msg);
}

///
/// OVERLOAD(MUIM_NListtree_DropType)
OVERLOAD(MUIM_NListtree_DropType)
{
  struct MUIP_NListtree_DropType *dt = (struct MUIP_NListtree_DropType *)msg;
  struct MUI_NListtree_TreeNode *tn;

  ENTER();

  // determine the entry under the mouse pointer
  if((tn = (struct MUI_NListtree_TreeNode *)DoMethod(obj, MUIM_NListtree_GetEntry, MUIV_NListtree_GetEntry_ListNode_Root, *dt->Pos, MUIV_NListtree_GetEntry_Flag_Visible)) != NULL)
  {
    struct ABEntry *entry;

    if((entry = (struct ABEntry *)tn->tn_User) != NULL)
    {
      // If we drag an ABEntry on another ABEntry we abort the
      // DragReport immediately because we want to support drag operations
      // between ABEntry elements and groups is allowed
      if(*dt->Type == MUIV_NListtree_DropType_Onto && entry->Type != AET_GROUP)
         *dt->Type = MUIV_NListtree_DropType_None;
    }
    else
      *dt->Type = MUIV_NListtree_DropType_None;
  }
  else
    *dt->Type = MUIV_NListtree_DropType_None;

  RETURN(0);
  return 0;
}

///

/* Private Functions */

/* Public Methods */

