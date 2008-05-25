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
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.   See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 YAM Official Support Site : http://www.yam.ch
 YAM OpenSource project     : http://sourceforge.net/projects/yamos/

 $Id$

 Superclass:  MUIC_Group
 Description: Displays additional information in the Main window

***************************************************************************/

#include "InfoBar_cl.h"

#include "MUIObjects.h"

#include "Debug.h"

/* CLASSDATA
struct Data
{
  Object *TX_FOLDER;
  Object *TX_FINFO;
  Object *TX_INFO;
  Object *GA_GROUP;
  Object *GA_INFO;
  Object *GA_LABEL;
  Object *BT_STOP;
  Object *actualImage;
  BOOL stopButtonPressed;
  struct TimeVal last_gaugemove;
};
*/

/* Private Functions */
/// GetFolderInfo()
// this function creates a folder string and returns it
static char *GetFolderInfo(struct Folder *folder)
{
  static char barText[SIZE_DEFAULT / 2];
  char *src;

  ENTER();

  // clear the bar text first
  barText[0] = '\0';

  // Lets create the label of the AppIcon now
  for(src = C->InfoBarText; *src; src++)
  {
    char dst[10];

    if(*src == '%')
    {
      switch(*++src)
      {
        case '%': strlcpy(dst, "%", sizeof(dst)); break;
        case 'n': snprintf(dst, sizeof(dst), "%d", folder->New);     break;
        case 'u': snprintf(dst, sizeof(dst), "%d", folder->Unread);  break;
        case 't': snprintf(dst, sizeof(dst), "%d", folder->Total);   break;
        case 's': snprintf(dst, sizeof(dst), "%d", folder->Sent);    break;
        case 'd': snprintf(dst, sizeof(dst), "%d", folder->Deleted); break;
      }
    }
    else
      snprintf(dst, sizeof(dst), "%c", *src);

    strlcat(barText, dst, sizeof(barText));
  }

  RETURN(barText);
  return barText;
}

///

/* Overloaded Methods */
/// OVERLOAD(OM_NEW)
OVERLOAD(OM_NEW)
{
  struct Data *data;
  Object *folderString;
  Object *folderInfoStr;
  Object *statusGroup;
  Object *gauge;
  Object *gaugeLabel;
  Object *infoText;
  Object *stopButton;

  ENTER();

  if((obj = DoSuperNew(cl, obj,

    // Some objects are allowed to disappear if the available space in the window is too narrow.
    // This is a workaround for a bug in MUI.
    TextFrame,
    MUIA_Background,    MUII_TextBack,
    MUIA_Group_Horiz,   TRUE,
    Child, HGroup,
      InnerSpacing(0,0),
      Child, folderString = TextObject,
        MUIA_HorizWeight,    0,
        MUIA_HorizDisappear, 2,
        MUIA_Font,           MUIV_Font_Big,
        MUIA_Text_SetMax,    FALSE,
        MUIA_Text_PreParse,  "\033b",
      End,
      Child, folderInfoStr = TextObject,
        MUIA_HorizWeight,    100,
        MUIA_HorizDisappear, 1,
        MUIA_Font,           MUIV_Font_Tiny,
        MUIA_Text_SetMax,    FALSE,
        MUIA_Text_PreParse,  "\033l",
      End,
    End,

    Child, HGroup,

      Child, gaugeLabel = TextObject,
        MUIA_HorizDisappear, 1,
        MUIA_Text_SetMax,   FALSE,
        MUIA_Text_PreParse, "\033r",
      End,

      Child, statusGroup = PageGroup,
        MUIA_HorizDisappear, 3,
        Child, HSpace(0),
        Child, HGroup,
          InnerSpacing(0,0),
          GroupSpacing(1),
          Child, gauge = GaugeObject,
            GaugeFrame,
            MUIA_Gauge_Horiz,    TRUE,
            MUIA_Gauge_InfoText, "",
          End,
          Child, stopButton = TextObject,
            ButtonFrame,
            MUIA_CycleChain,     TRUE,
            MUIA_Font,           MUIV_Font_Tiny,
            MUIA_Text_Contents,  "\033bX",
            MUIA_InputMode,      MUIV_InputMode_RelVerify,
            MUIA_Background,     MUII_ButtonBack,
            MUIA_Text_SetMax,    TRUE,
          End,
        End,
        Child, infoText = TextObject,
          MUIA_Text_SetMax,   FALSE,
          MUIA_Text_PreParse, "\033r",
        End,
      End,
    End,

    TAG_MORE, inittags(msg))))
  {
    data = (struct Data *)INST_DATA(cl,obj);

    // per default we set the stop button as hidden
    set(stopButton, MUIA_ShowMe, FALSE);

    data->TX_FOLDER = folderString;
    data->TX_FINFO  = folderInfoStr;
    data->GA_GROUP  = statusGroup;
    data->GA_LABEL  = gaugeLabel;
    data->GA_INFO   = gauge;
    data->TX_INFO   = infoText;
    data->BT_STOP   = stopButton;
    data->stopButtonPressed = FALSE;

    // on a button press on the stop button, lets set the
    // correct variable to TRUE as well.
    DoMethod(data->BT_STOP, MUIM_Notify, MUIA_Pressed, FALSE, obj, 3, MUIM_WriteLong, MUIV_NotTriggerValue, &(data->stopButtonPressed));
  }

  RETURN((ULONG)obj);
  return (ULONG)obj;
}
///

/* Public Methods */
/// DECLARE(SetFolder)
// set a new folder and update its name and image in the infobar
DECLARE(SetFolder) // struct Folder *newFolder
{
  GETDATA;
  struct Folder *folder = msg->newFolder;
  ULONG result = (ULONG)-1;

  ENTER();

  if(folder != NULL)
  {
    // set the name of the folder as the info text
    nnset(data->TX_FOLDER, MUIA_Text_Contents, folder->Name);

    // now we are going to set some status field at the right side of the folder name
    nnset(data->TX_FINFO, MUIA_Text_Contents, GetFolderInfo(folder));

    // prepare the object for a change
    if(DoMethod(obj, MUIM_Group_InitChange))
    {
      // only if the image should be changed we proceed or otherwise
      // MUI will refresh too often
      if(data->actualImage != NULL && (folder->imageObject == NULL ||
         stricmp((char *)xget(data->actualImage, MUIA_ImageArea_ID),
                 (char *)xget(folder->imageObject, MUIA_ImageArea_ID)) != 0))
      {
        DoMethod(obj, OM_REMMEMBER, data->actualImage);

        D(DBF_GUI, "disposing folder image: id '%s' file '%s'", xget(data->actualImage, MUIA_ImageArea_ID), xget(data->actualImage, MUIA_ImageArea_Filename));
        MUI_DisposeObject(data->actualImage);
        data->actualImage = NULL;
      }

      // and if we have a new one we generate the object an add it
      // to the grouplist of this infobar
      if(data->actualImage == NULL)
      {
        if(folder->imageObject != NULL)
        {
          char *imageID = (char *)xget(folder->imageObject, MUIA_ImageArea_ID);
          char *imageName = (char *)xget(folder->imageObject, MUIA_ImageArea_Filename);

          data->actualImage = MakeImageObject(imageID, imageName);

          D(DBF_GUI, "init imagearea: id '%s', file '%s'", imageID, imageName);
        }
        else if(folder->ImageIndex >= 0 && folder->ImageIndex <= MAX_FOLDERIMG)
        {
          Object **imageArray = (Object **)xget(G->MA->GUI.NL_FOLDERS, MUIA_MainFolderListtree_ImageArray);

          D(DBF_GUI, "init imagearea: 0x%08lx[%ld]", imageArray, folder->ImageIndex);

          if(imageArray != NULL && imageArray[folder->ImageIndex] != NULL)
          {
            char *imageID = (char *)xget(imageArray[folder->ImageIndex], MUIA_ImageArea_ID);
            char *imageName = (char *)xget(imageArray[folder->ImageIndex], MUIA_ImageArea_Filename);

            data->actualImage = MakeImageObject(imageID, imageName);
          }
        }

        D(DBF_GUI, "init finished..: 0x%08lx %ld", data->actualImage, folder->ImageIndex);

        if(data->actualImage != NULL)
          DoMethod(obj, OM_ADDMEMBER, data->actualImage);
      }

      // now that we are finished we can call ExitChange to refresh the infobar
      DoMethod(obj, MUIM_Group_ExitChange);
    }

    result = 0;
  }

  RETURN(result);
  return result;
}
///
/// DECLARE(ShowGauge)
// activates the gauge in the InfoBar with the passed text and percentage
// and also returns the current stop 'X' button status so that the calling
// function may aborts its operation.
DECLARE(ShowGauge) // STRPTR gaugeText, LONG perc, LONG max
{
  GETDATA;
  BOOL result = TRUE;

  ENTER();

  if(msg->gaugeText != NULL)
  {
    static char infoText[256];

    nnset(data->GA_LABEL, MUIA_Text_Contents, msg->gaugeText);

    snprintf(infoText, sizeof(infoText), "%%ld/%ld", msg->max);

    xset(data->GA_INFO, MUIA_Gauge_InfoText,  infoText,
                        MUIA_Gauge_Current,   msg->perc > 0 ? msg->perc : 0,
                        MUIA_Gauge_Max,       msg->max);

    // make sure the stop button is shown or hiden, dependent
    // on msg->perc
    set(data->BT_STOP, MUIA_ShowMe, msg->perc == -1);
    data->stopButtonPressed = FALSE;

    set(data->GA_GROUP, MUIA_Group_ActivePage, 1);
  }
  else
  {
    struct TimeVal now;

    // then we update the gauge, but we take also care of not refreshing
    // it too often or otherwise it slows down the whole search process.
    GetSysTime(TIMEVAL(&now));
    if(-CmpTime(TIMEVAL(&now), TIMEVAL(&data->last_gaugemove)) > 0)
    {
      struct TimeVal delta;

      // how much time has passed exactly?
      memcpy(&delta, &now, sizeof(struct TimeVal));
      SubTime(TIMEVAL(&delta), TIMEVAL(&data->last_gaugemove));

      // update the display at least twice a second
      if(delta.Seconds > 0 || delta.Microseconds > 250000)
      {
        set(data->GA_INFO, MUIA_Gauge_Current, msg->perc);

        memcpy(&data->last_gaugemove, &now, sizeof(struct TimeVal));
      }
    }

    // give the application the chance to clear its event loop
    DoMethod(G->App, MUIM_Application_InputBuffered);

    // in case the stopButton was pressed we return FALSE
    result = (data->stopButtonPressed == FALSE);

    set(data->GA_GROUP, MUIA_Group_ActivePage, 1);
  }

  RETURN(result);
  return result;
}
///
/// DECLARE(ShowInfoText)
// activates the gauge in the InfoBar with the passed text and percentage
DECLARE(ShowInfoText) // STRPTR infoText
{
  GETDATA;

  ENTER();

  nnset(data->GA_GROUP, MUIA_Group_ActivePage, 2);
  // setting a NULL text is ok, according to the AutoDocs
  nnset(data->TX_INFO, MUIA_Text_Contents, msg->infoText);

/*
  if(msg->infoText != NULL)
  {
    nnset(data->TX_INFO, MUIA_Text_Contents, msg->infoText);
  }
  else
  {
    nnset(data->TX_INFO, MUIA_Text_Contents, "");
  }
*/

  RETURN(TRUE);
  return TRUE;
}
///
/// DECLARE(HideBars)
// activates the gauge in the InfoBar with the passed text and percentage
DECLARE(HideBars)
{
  GETDATA;

  ENTER();

  set(data->GA_GROUP, MUIA_Group_ActivePage, 0);
  set(data->GA_LABEL, MUIA_Text_Contents, " ");

  RETURN(TRUE);
  return TRUE;
}
///

