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

 Superclass:  MUIC_Window
 Description:

***************************************************************************/

#include "GenericRequestWindow_cl.h"

#include "Locale.h"
#include "MUIObjects.h"

#include "Debug.h"

/* CLASSDATA
struct Data
{
  LONG result;
};
*/

/* Overloaded Methods */
/// OVERLOAD(OM_NEW)
OVERLOAD(OM_NEW)
{
  struct TagItem *tags = inittags(msg), *tag;
  char *titleText = (char *)"YAM";
  char *bodyText = NULL;
  char *buttons = NULL;
  Object *buttonGroup = NULL;

  ENTER();

  while((tag = NextTagItem(&tags)) != NULL)
  {
    switch(tag->ti_Tag)
    {
      case MUIA_Window_Title:
      {
        titleText = (char *)tag->ti_Data;
        tag->ti_Tag = TAG_IGNORE;
      }
      break;

      ATTR(Body):
      {
        bodyText = (char *)tag->ti_Data;
        tag->ti_Tag = TAG_IGNORE;
      }
      break;

      ATTR(Buttons):
      {
        buttons = (char *)tag->ti_Data;
        tag->ti_Tag = TAG_IGNORE;
      }
      break;
    }
  }

  if((obj = DoSuperNew(cl, obj,

    MUIA_Window_Title,        titleText,
    MUIA_Window_LeftEdge,     MUIV_Window_LeftEdge_Centered,
    MUIA_Window_TopEdge,      MUIV_Window_TopEdge_Centered,
    MUIA_Window_Width,        MUIV_Window_Width_MinMax(0),
    MUIA_Window_Height,       MUIV_Window_Height_MinMax(0),
    MUIA_Window_CloseGadget,  FALSE,
    MUIA_Window_SizeGadget,   FALSE,
    MUIA_Window_Activate,     TRUE,
    MUIA_Window_NoMenus,      TRUE,
    WindowContents, VGroup,
       MUIA_Background, MUII_RequesterBack,
       InnerSpacing(4, 4),
       Child, HGroup,
         GroupFrame,
         MUIA_Background, MUII_GroupBack,
         Child, HSpace(0),
         Child, TextObject,
           InnerSpacing(4, 4),
           MUIA_Text_Contents, bodyText,
           MUIA_Text_SetMax,   TRUE,
         End,
         Child, HSpace(0),
       End,
       Child, buttonGroup = HGroup,
         GroupSpacing(0),
       End,
    End,

    TAG_MORE, (ULONG)inittags(msg))) != NULL)
  {
    // prepare the group for the change.
    if(buttons != NULL && DoMethod(buttonGroup, MUIM_Group_InitChange))
    {
      int numButtons;
      int i;
      char *token;
      char *next;
      BOOL active = FALSE;
      BOOL inputEvent = TRUE;

      // first we count how many buttons we have to create
      for(numButtons = 1, token = buttons; *token != '\0'; token++)
      {
        if(*token == '|')
          numButtons++;
      }

      // now we create the buttons for the requester
      for(token = buttons, i = 0; i < numButtons; i++, token = next)
      {
        char *ul;
        Object *button;

        if((next = strchr(token, '|')) != NULL)
          *next++ = '\0';

        if(*token == '*')
        {
          active=TRUE;
          token++;
        }

        if((ul = strchr(token, '_')) != NULL)
          inputEvent = FALSE;

        // create the button object now.
        button = TextObject,
                   ButtonFrame,
                   MUIA_CycleChain,    1,
                   MUIA_Text_Contents, token,
                   MUIA_Text_PreParse, "\33c",
                   MUIA_InputMode,     MUIV_InputMode_RelVerify,
                   MUIA_Background,    MUII_ButtonBack,
                   ul ? MUIA_Text_HiIndex : TAG_IGNORE, '_',
                   ul ? MUIA_ControlChar  : TAG_IGNORE, ul ? tolower(*(ul+1)) : 0,
                 End;

        if(button != NULL)
        {
          int buttonResult;

          if(numButtons == 1)
          {
            DoMethod(buttonGroup, OM_ADDMEMBER, HSpace(0));
            DoMethod(buttonGroup, OM_ADDMEMBER, HSpace(0));
            DoMethod(buttonGroup, OM_ADDMEMBER, button);
            DoMethod(buttonGroup, OM_ADDMEMBER, HSpace(0));
            DoMethod(buttonGroup, OM_ADDMEMBER, HSpace(0));
            set(obj, MUIA_Window_DefaultObject, button);
          }
          else if(i < numButtons - 1)
          {
            DoMethod(buttonGroup, OM_ADDMEMBER, button);
            DoMethod(buttonGroup, OM_ADDMEMBER, HSpace(4));
            DoMethod(buttonGroup, OM_ADDMEMBER, HSpace(0));
          }
          else
          {
            DoMethod(buttonGroup, OM_ADDMEMBER, button);
          }

          // which result should this button produce?
          if(i < numButtons - 1)
            buttonResult = i + 1;
          else
            buttonResult = 0;

          if(inputEvent == TRUE && numButtons == 2)
          {
            if(i == 0)
            {
              DoMethod(obj, MUIM_Notify, MUIA_Window_InputEvent, "y", obj, 2, MUIM_GenericRequestWindow_FinishInput, buttonResult);
            }
            else if(i == numButtons - 1)
            {
              DoMethod(obj, MUIM_Notify, MUIA_Window_InputEvent, "n", obj, 2, MUIM_GenericRequestWindow_FinishInput, buttonResult);
            }
          }

          if(i <= 8)
          {
            // by default we set it to "-capslock f1" so that we can press f1
            // even if the capslock is on.
            char fstring[13];

            snprintf(fstring, sizeof(fstring), "-capslock f%d", i + 1);
            DoMethod(obj, MUIM_Notify, MUIA_Window_InputEvent, fstring, obj, 2, MUIM_GenericRequestWindow_FinishInput, buttonResult);
          }

          DoMethod(button, MUIM_Notify, MUIA_Pressed, FALSE, obj, 2, MUIM_GenericRequestWindow_FinishInput, buttonResult);

          if(active == TRUE)
          {
            set(obj, MUIA_Window_ActiveObject, button);
            active = FALSE;
          }
        }

        // write back what we took.
        if(next != NULL)
          *(next-1) = '|';
      }

      DoMethod(buttonGroup, MUIM_Group_ExitChange);
    }

    DoMethod(obj, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, obj, 2, MUIM_GenericRequestWindow_FinishInput, 0);
  }

  RETURN(obj);
  return (ULONG)obj;
}

///
/// OVERLOAD(OM_GET)
OVERLOAD(OM_GET)
{
  GETDATA;
  ULONG *store = ((struct opGet *)msg)->opg_Storage;

  switch(((struct opGet *)msg)->opg_AttrID)
  {
    ATTR(Result):
    {
      *store = data->result;
      return TRUE;
    }
  }

  return DoSuperMethodA(cl, obj, msg);
}

///

/* Private Functions */

/* Public Methods */
/// DECLARE(FinishInput)
//
DECLARE(FinishInput) // ULONG result
{
  GETDATA;

  ENTER();

  data->result = msg->result;

  // trigger possible notifications
  set(obj, MUIA_GenericRequestWindow_Result, msg->result);

  RETURN(0);
  return 0;
}

///
