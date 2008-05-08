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

#include <clib/alib_protos.h>
#include <mui/NList_mcc.h>

#include "extrasrc.h"

#include "YAM.h"
#include "classes/Classes.h"

#include "Rexx.h"

#include "Debug.h"

void rx_listselect(UNUSED struct RexxHost *host, void **rxd, enum RexxAction action, UNUSED struct RexxMsg *rexxmsg)
{
  struct rxd_listselect *rd = *rxd;

  ENTER();

  switch(action)
  {
    case RXIF_INIT:
    {
      *rxd = AllocVecPooled(G->SharedMemPool, sizeof(*rd));
    }
    break;

    case RXIF_ACTION:
    {
      Object *nl = (Object *)xget(G->MA->GUI.PG_MAILLIST, MUIA_MainMailListGroup_MainList);

      switch(rd->arg.mode[0])
      {
        case 'a': case 'A': DoMethod(nl, MUIM_NList_Select, MUIV_NList_Select_All, MUIV_NList_Select_On, NULL); break;
        case 'n': case 'N': DoMethod(nl, MUIM_NList_Select, MUIV_NList_Select_All, MUIV_NList_Select_Off, NULL); break;
        case 't': case 'T': DoMethod(nl, MUIM_NList_Select, MUIV_NList_Select_All, MUIV_NList_Select_Toggle, NULL); break;
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
        case '8': case '9': DoMethod(nl, MUIM_NList_Select, atol(rd->arg.mode), MUIV_NList_Select_On, NULL); break;
      }
    }
    break;

    case RXIF_FREE:
    {
      FreeVecPooled(G->SharedMemPool, rd);
    }
    break;
  }

  LEAVE();
}
