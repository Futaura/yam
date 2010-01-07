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
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.   See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 YAM Official Support Site : http://www.yam.ch
 YAM OpenSource project     : http://sourceforge.net/projects/yamos/

 $Id$

 Superclass:  MUIC_NList
 Description: List that manages the different pages of the configuration

***************************************************************************/

#include "ConfigPageList_cl.h"

#include "YAM_config.h"

#include "Debug.h"

/* CLASSDATA
struct Data
{
  struct Hook displayHook;
  Object       *configIcon[cp_Max];
};
*/

/* EXPORT
struct PageList
{
  int  Offset;
  const void *PageLabel;
};
*/

/* Hooks */
/// DisplayHook
//  Section listview displayhook
HOOKPROTONH(DisplayFunc, long, char **array, struct PageList *entry)
{
  static char page[SIZE_DEFAULT];
  snprintf(array[0] = page, sizeof(page), "\033o[%d] %s", entry->Offset, tr(entry->PageLabel));
  return 0;
}
MakeStaticHook(DisplayHook, DisplayFunc);
///

/* Private Functions */

/* Overloaded Methods */
/// OVERLOAD(OM_NEW)
OVERLOAD(OM_NEW)
{
  ENTER();

  if((obj = DoSuperNew(cl, obj,
    TAG_MORE, inittags(msg))))
  {
    GETDATA;
    enum ConfigPage i;

    InitHook(&data->displayHook, DisplayHook, data);
    set(obj, MUIA_NList_DisplayHook, &data->displayHook);

    // create/load all bodychunkimages of our config icons
    data->configIcon[cp_FirstSteps  ] = MakeImageObject("config_firststep", G->theme.configImages[ci_FirstStep]);
    data->configIcon[cp_TCPIP       ] = MakeImageObject("config_network",   G->theme.configImages[ci_Network]);
    data->configIcon[cp_NewMail     ] = MakeImageObject("config_newmail",   G->theme.configImages[ci_NewMail]);
    data->configIcon[cp_Filters     ] = MakeImageObject("config_filters",   G->theme.configImages[ci_Filters]);
    data->configIcon[cp_Spam        ] = MakeImageObject("config_spam",      G->theme.configImages[ci_Spam]);
    data->configIcon[cp_Read        ] = MakeImageObject("config_read",      G->theme.configImages[ci_Read]);
    data->configIcon[cp_Write       ] = MakeImageObject("config_write",     G->theme.configImages[ci_Write]);
    data->configIcon[cp_ReplyForward] = MakeImageObject("config_answer",    G->theme.configImages[ci_Answer]);
    data->configIcon[cp_Signature   ] = MakeImageObject("config_signature", G->theme.configImages[ci_Signature]);
    data->configIcon[cp_Lists       ] = MakeImageObject("config_lists",     G->theme.configImages[ci_Lists]);
    data->configIcon[cp_Security    ] = MakeImageObject("config_security",  G->theme.configImages[ci_Security]);
    data->configIcon[cp_StartupQuit ] = MakeImageObject("config_start",     G->theme.configImages[ci_Start]);
    data->configIcon[cp_MIME        ] = MakeImageObject("config_mime",      G->theme.configImages[ci_MIME]);
    data->configIcon[cp_AddressBook ] = MakeImageObject("config_abook",     G->theme.configImages[ci_ABook]);
    data->configIcon[cp_Scripts     ] = MakeImageObject("config_scripts",   G->theme.configImages[ci_Scripts]);
    data->configIcon[cp_Mixed       ] = MakeImageObject("config_misc",      G->theme.configImages[ci_Misc]);
    data->configIcon[cp_LookFeel    ] = MakeImageObject("config_lookfeel",  G->theme.configImages[ci_LookFeel]);
    data->configIcon[cp_Update      ] = MakeImageObject("config_update",    G->theme.configImages[ci_Update]);

    // now we can add the config icon objects and use UseImage
    // to prepare it for displaying it in the NList
    for(i = cp_FirstSteps; i < cp_Max; i++)
      DoMethod(obj, MUIM_NList_UseImage, data->configIcon[i], i, MUIF_NONE);
  }

  RETURN((IPTR)obj);
  return (IPTR)obj;
}

///
/// OVERLOAD(OM_DISPOSE)
OVERLOAD(OM_DISPOSE)
{
  GETDATA;
  ULONG result;
  enum ConfigPage i;

  ENTER();

  for(i = cp_FirstSteps; i < cp_Max; i++)
  {
    if(data->configIcon[i])
      MUI_DisposeObject(data->configIcon[i]);

    data->configIcon[i] = NULL;
  }

  result = DoSuperMethodA(cl, obj, msg);

  RETURN(result);
  return result;
}

///

/* Public Methods */

