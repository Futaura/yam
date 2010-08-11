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

 Superclass:  MUIC_Group
 Description: Provides some GUI elements for filter/search controls

***************************************************************************/

#if defined(__AROS__)
#define MUI_OBSOLETE 1
#endif

#include "SearchControlGroup_cl.h"

#include <proto/asl.h>

#include "YAM_find.h"
#include "MUIObjects.h"

#include "Debug.h"

/* CLASSDATA
struct Data
{
  Object *PG_SRCHOPT;
  Object *PG_MODE;
  Object *CY_MODE[2];
  Object *ST_FIELD;
  Object *CY_COMP[5];
  Object *ST_MATCH[5];
  Object *BT_FILE[5];
  Object *BT_EDIT[5];
  Object *RA_ADRMODE;
  Object *CY_STATUS;
  Object *CH_CASESENS[5];
  Object *CH_SUBSTR[5];
  Object *CH_DOSPATTERN[5];
  Object *CY_COMBINE;

  Object *activeObject;

  BOOL remoteFilterMode;
  BOOL showCombineCycle;
};
*/

const int Mode2Group[12] = { 0,0,0,0,1,2,1,2,4,4,4,3 };

/* Hooks */

/* Private Functions */
/// DECLARE(EditFile)
//  Edits pattern list in text editor
DECLARE(EditFile) // int n
{
  GETDATA;

  if(*C->Editor)
  {
    char buffer[SIZE_COMMAND+SIZE_PATHFILE];

    snprintf(buffer, sizeof(buffer), "%s \"%s\"", C->Editor, GetRealPath((char *)xget(data->ST_MATCH[msg->n], MUIA_String_Contents)));
    ExecuteCommand(buffer, TRUE, OUT_NIL);
  }

  return 0;
}

///

/* Overloaded Methods */
/// OVERLOAD(OM_NEW)
OVERLOAD(OM_NEW)
{
  static const char *fldopt[2][13];
  static const char *compopt[14];
  static const char *statopt[11];
  static const char *amode[3];
  static const char *bcrit[5];
  struct Data *data;
  struct Data *tmpData;
  struct TagItem *tags = inittags(msg), *tag;

  amode[0] = tr(MSG_Address);
  amode[1] = tr(MSG_Name);
  amode[2] = NULL;

  // make sure the following array has the same
  // order than the mailStatusMap in YAM_global.c
  statopt[0] = tr(MSG_FI_StatNew);
  statopt[1] = tr(MSG_FI_StatRead);
  statopt[2] = tr(MSG_FI_StatForwarded);
  statopt[3] = tr(MSG_FI_StatReplied);
  statopt[4] = tr(MSG_FI_StatQueued);
  statopt[5] = tr(MSG_FI_StatFailed);
  statopt[6] = tr(MSG_FI_StatHold);
  statopt[7] = tr(MSG_FI_StatSent);
  statopt[8] = tr(MSG_FI_StatMarked);
  statopt[9] = tr(MSG_FI_STATSPAM);
  statopt[10] = NULL;

  compopt[0] = compopt[5] = compopt[ 8] = " = ";
  compopt[1] = compopt[6] = compopt[ 9] = " <> ";
  compopt[2] =              compopt[10] = " < ";
  compopt[3] =              compopt[11] = " > ";
                            compopt[12] = " IN ";
  compopt[4] = compopt[7] = compopt[13] = NULL;

  fldopt[0][0] = fldopt[1][0] = tr(MSG_FI_FROM_FIELD);
  fldopt[0][1] = fldopt[1][1] = tr(MSG_FI_TO_FIELD);
  fldopt[0][2] = fldopt[1][2] = tr(MSG_FI_CC_FIELD);
  fldopt[0][3] = fldopt[1][3] = tr(MSG_FI_REPLYTO_FIELD);
  fldopt[0][4] = fldopt[1][4] = tr(MSG_FI_SUBJECT_FIELD);
  fldopt[0][5] = fldopt[1][5] = tr(MSG_FI_DATE_FIELD);
  fldopt[0][6] = fldopt[1][6] = tr(MSG_FI_OtherField);
  fldopt[0][7] = fldopt[1][7] = tr(MSG_FI_MessageSize);
  fldopt[0][8] = fldopt[1][8] = tr(MSG_FI_MessageHeader);
  fldopt[0][9] =                 tr(MSG_FI_MessageBody);
  fldopt[0][10]=                 tr(MSG_FI_WholeMessage);
  fldopt[0][11]=                 tr(MSG_Status);
  fldopt[0][12]= fldopt[1][9] = fldopt[1][10] = fldopt[1][11] = fldopt[1][12] = NULL;

  bcrit[0] = tr(MSG_CO_CTOr);
  bcrit[1] = tr(MSG_CO_CTAnd);
  bcrit[2] = tr(MSG_CO_CTXor);
  bcrit[3] = NULL;

  // generate a temporary struct Data to which we store our data and
  // copy it later on
  if(!(data = tmpData = calloc(1, sizeof(struct Data))))
    return 0;

  // get eventually set attributes first
  while((tag = NextTagItem((APTR)&tags)) != NULL)
  {
    switch(tag->ti_Tag)
    {
      case ATTR(RemoteFilterMode) : data->remoteFilterMode = tag->ti_Data; break;
      case ATTR(ShowCombineCycle) : data->showCombineCycle = tag->ti_Data; break;
    }
  }

  if((obj = DoSuperNew(cl, obj,

    MUIA_Group_Horiz, FALSE,
    MUIA_HelpNode,     "FI_K",
      Child, data->CY_COMBINE = MakeCycle(bcrit, ""),
      Child, HGroup,
        Child, Label1(tr(MSG_FI_SearchIn)),
        Child, data->PG_MODE = PageGroup,
          MUIA_Group_ActivePage, data->remoteFilterMode,
          Child, data->CY_MODE[0] = MakeCycle(fldopt[0], tr(MSG_FI_SearchIn)),
          Child, data->CY_MODE[1] = MakeCycle(fldopt[1], tr(MSG_FI_SearchIn)),
        End,
        Child, data->ST_FIELD = BetterStringObject,
          StringFrame,
          MUIA_String_Accept,      "!#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_abcdefghijklmnopqrstuvwxyz{|}~",
          MUIA_String_MaxLen,      SIZE_DEFAULT,
          MUIA_String_AdvanceOnCR, TRUE,
          MUIA_CycleChain,         TRUE,
        End,
      End,
      Child, data->PG_SRCHOPT = PageGroup,
        Child, VGroup, // 0  from, to, cc, reply-to
          Child, HGroup,
            MUIA_Group_HorizSpacing, 0,
            Child, data->CY_COMP[0] = MakeCycle(&compopt[8], ""),
            Child, HSpace(4),
            Child, PopaslObject,
              MUIA_Popasl_Type,      ASL_FileRequest,
              MUIA_Popstring_String, data->ST_MATCH[0] = MakeString(SIZE_PATTERN, ""),
              MUIA_Popstring_Button, data->BT_FILE[0] = PopButton(MUII_PopFile),
            End,
            Child, data->BT_EDIT[0] = PopButton(MUII_PopUp),
          End,
          Child, HGroup,
            Child, VGroup,
              Child, MakeCheckGroup((Object **)&data->CH_CASESENS[0], tr(MSG_FI_CaseSensitive)),
              Child, MakeCheckGroup((Object **)&data->CH_SUBSTR[0], tr(MSG_FI_SubString)),
              Child, MakeCheckGroup((Object **)&data->CH_DOSPATTERN[0], tr(MSG_FI_DOS_PATTERN)),
            End,
            Child, data->RA_ADRMODE = Radio(NULL, amode),
          End,
        End,
        Child, VGroup, // 1  subject, other field
          Child, HGroup,
            MUIA_Group_HorizSpacing, 0,
            Child, data->CY_COMP[1] = MakeCycle(&compopt[8], ""),
            Child, HSpace(4),
            Child, PopaslObject,
              MUIA_Popasl_Type,      ASL_FileRequest,
              MUIA_Popstring_String, data->ST_MATCH[1] = MakeString(SIZE_PATTERN,""),
              MUIA_Popstring_Button, data->BT_FILE[1] = PopButton(MUII_PopFile),
            End,
            Child, data->BT_EDIT[1] = PopButton(MUII_PopUp),
          End,
          Child, VGroup,
            Child, MakeCheckGroup((Object **)&data->CH_CASESENS[1], tr(MSG_FI_CaseSensitive)),
            Child, MakeCheckGroup((Object **)&data->CH_SUBSTR[1], tr(MSG_FI_SubString)),
            Child, MakeCheckGroup((Object **)&data->CH_DOSPATTERN[1], tr(MSG_FI_DOS_PATTERN)),
          End,
        End,
        Child, VGroup, // 2  date, size
          Child, HGroup,
            Child, data->CY_COMP[2] = MakeCycle(compopt, ""),
            Child, data->ST_MATCH[2] = MakeString(SIZE_PATTERN, ""),
          End,
          Child, HVSpace,
        End,
        Child, VGroup, // 3  status
           Child, HGroup,
             Child, data->CY_COMP[3] = MakeCycle(&compopt[5], ""),
             Child, data->CY_STATUS = MakeCycle(statopt, ""),
             Child, HSpace(0),
           End,
          Child, HVSpace,
        End,
        Child, VGroup, // 4  message header/body
          Child, HGroup,
            Child, data->CY_COMP[4] = MakeCycle(&compopt[5], ""),
            Child, data->ST_MATCH[4] = MakeString(SIZE_PATTERN, ""),
          End,
          Child, MakeCheckGroup((Object **)&data->CH_CASESENS[4], tr(MSG_FI_CaseSensitive)),
          Child, MakeCheckGroup((Object **)&data->CH_DOSPATTERN[4], tr(MSG_FI_DOS_PATTERN)),
          Child, HVSpace,
        End,
      End,

    TAG_MORE, inittags(msg))) != NULL)
  {
    int i;

    if((data = (struct Data *)INST_DATA(cl, obj)) == NULL)
      return 0;

    // copy back the data stored in our temporarly struct Data
    memcpy(data, tmpData, sizeof(struct Data));

    // depending on the combinCycle mode we either show or hide it
    set(data->CY_COMBINE, MUIA_ShowMe, data->showCombineCycle);

    // set the cyclechain
    set(data->RA_ADRMODE, MUIA_CycleChain, TRUE);

    // set ST_MATCH[0] as the current active object
    data->activeObject = data->ST_MATCH[0];

    // set help text
    SetHelp(data->CY_MODE[0], MSG_HELP_FI_CY_MODE);
    SetHelp(data->CY_MODE[1], MSG_HELP_FI_CY_MODE);
    SetHelp(data->CY_COMBINE, MSG_HELP_CO_CY_COMBINE);
    SetHelp(data->ST_FIELD,   MSG_HELP_FI_ST_FIELD);
    SetHelp(data->RA_ADRMODE, MSG_HELP_FI_RA_ADRMODE);
    SetHelp(data->CY_STATUS,  MSG_HELP_FI_CY_STATUS);

    DoMethod(data->CY_MODE[0], MUIM_Notify, MUIA_Cycle_Active,    MUIV_EveryTime, obj, 1, MUIM_SearchControlGroup_Update);
    DoMethod(data->CY_MODE[1], MUIM_Notify, MUIA_Cycle_Active,    MUIV_EveryTime, obj, 1, MUIM_SearchControlGroup_Update);
    DoMethod(data->CY_COMBINE, MUIM_Notify, MUIA_Cycle_Active,    MUIV_EveryTime, obj, 3, MUIM_Set, MUIA_SearchControlGroup_Modified, TRUE);
    DoMethod(data->RA_ADRMODE, MUIM_Notify, MUIA_Radio_Active,    MUIV_EveryTime, obj, 3, MUIM_Set, MUIA_SearchControlGroup_Modified, TRUE);
    DoMethod(data->ST_FIELD,   MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, obj, 3, MUIM_Set, MUIA_SearchControlGroup_Modified, TRUE);
    DoMethod(data->CY_STATUS,  MUIM_Notify, MUIA_Cycle_Active,    MUIV_EveryTime, obj, 3, MUIM_Set, MUIA_SearchControlGroup_Modified, TRUE);

    for(i = 0; i < 5; i++)
    {
      if(data->CY_COMP[i] != NULL)
      {
        DoMethod(data->CY_COMP[i], MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, obj, 1, MUIM_SearchControlGroup_Update);
        DoMethod(data->CY_COMP[i], MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, obj, 3, MUIM_Set, MUIA_SearchControlGroup_Modified, TRUE);
        SetHelp(data->CY_COMP[i], MSG_HELP_FI_CY_COMP);
        set(data->CY_COMP[i], MUIA_HorizWeight, 0);
      }

      if(data->ST_MATCH[i] != NULL)
      {
        DoMethod(data->ST_MATCH[i], MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, obj, 3, MUIM_Set, MUIA_SearchControlGroup_Modified, TRUE);
        SetHelp(data->ST_MATCH[i], MSG_HELP_FI_ST_MATCH);
      }

      if(data->CH_CASESENS[i] != NULL)
      {
        DoMethod(data->CH_CASESENS[i], MUIM_Notify, MUIA_Selected, MUIV_EveryTime, obj, 3, MUIM_Set, MUIA_SearchControlGroup_Modified, TRUE);
        SetHelp(data->CH_CASESENS[i], MSG_HELP_FI_CH_CASESENS);
      }

      if(data->CH_SUBSTR[i] != NULL)
      {
        DoMethod(data->CH_SUBSTR[i], MUIM_Notify, MUIA_Selected, MUIV_EveryTime, obj, 3, MUIM_Set, MUIA_SearchControlGroup_Modified, TRUE);
        SetHelp(data->CH_SUBSTR[i], MSG_HELP_FI_CH_SUBSTR);
        nnset(data->CH_SUBSTR[i], MUIA_Selected, TRUE);
      }

      if(data->CH_DOSPATTERN[i] != NULL)
      {
        DoMethod(data->CH_DOSPATTERN[i], MUIM_Notify, MUIA_Selected, MUIV_EveryTime, obj, 3, MUIM_Set, MUIA_SearchControlGroup_Modified, TRUE);
        SetHelp(data->CH_DOSPATTERN[i], MSG_HELP_FI_CH_DOS_PATTERN);
      }

      if(data->BT_EDIT[i] != NULL)
        DoMethod(data->BT_EDIT[i], MUIM_Notify, MUIA_Pressed, FALSE, obj, 2, MUIM_SearchControlGroup_EditFile, i);
    }

    // make sure all elements are enabled.
    set(obj, MUIA_Disabled, FALSE);
  }

  // free the temporary mem we allocated before
  free(tmpData);

  return (IPTR)obj;
}
///
/// OVERLOAD(OM_SET)
OVERLOAD(OM_SET)
{
  GETDATA;

  struct TagItem *tags = inittags(msg), *tag;
  while((tag = NextTagItem((APTR)&tags)) != NULL)
  {
    switch(tag->ti_Tag)
    {
      case ATTR(RemoteFilterMode):
      {
        // we check if we switch the FilterMode
        if(tag->ti_Data != (ULONG)data->remoteFilterMode)
        {
          int oldActive = xget(data->CY_MODE[data->remoteFilterMode], MUIA_Cycle_Active);

          // lets first copy the cycle status from the old
          // to the new status
          if(data->remoteFilterMode == FALSE)
            set(data->CY_MODE[tag->ti_Data], MUIA_Cycle_Active, oldActive > 8 ? 8 : oldActive);
          else
            set(data->CY_MODE[tag->ti_Data], MUIA_Cycle_Active, oldActive);

          data->remoteFilterMode = tag->ti_Data;
          set(data->PG_MODE, MUIA_Group_ActivePage, data->remoteFilterMode);
        }
      }
      break;

      // Overload some global attributes as well
      case MUIA_Disabled:
      {
        BOOL disabled = (BOOL)tag->ti_Data;
        int i;
        int mode = GetMUICycle(data->CY_MODE[data->remoteFilterMode]);
        int oper = GetMUICycle(data->CY_COMP[Mode2Group[mode]]);

        set(data->CY_MODE[0], MUIA_Disabled, disabled);
        set(data->CY_MODE[1], MUIA_Disabled, disabled);
        set(data->ST_FIELD, MUIA_Disabled, disabled);
        set(data->ST_FIELD, MUIA_ShowMe, mode == 6);
        set(data->RA_ADRMODE, MUIA_Disabled, disabled);
        set(data->CY_STATUS, MUIA_Disabled, disabled);

        for(i = 0; i < 5; i++)
        {
          set(data->CY_COMP[i], MUIA_Disabled, disabled);

          if(data->ST_MATCH[i] != NULL)
            set(data->ST_MATCH[i], MUIA_Disabled, disabled);
          if(data->CH_CASESENS[i] != NULL)
            set(data->CH_CASESENS[i], MUIA_Disabled, disabled);
          if(data->CH_SUBSTR[i] != NULL)
            set(data->CH_SUBSTR[i], MUIA_Disabled, disabled || oper == 4 || (i < 2 && oper > 1));
          if(data->CH_DOSPATTERN[i] != NULL)
            set(data->CH_DOSPATTERN[i], MUIA_Disabled, disabled);
          if(data->BT_FILE[i] != NULL)
            set(data->BT_FILE[i], MUIA_Disabled, disabled || oper != 4);
          if(data->BT_EDIT[i] != NULL)
            set(data->BT_EDIT[i], MUIA_Disabled, disabled || oper != 4);
        }

        return 0;
      }
      break;
    }
  }

  return DoSuperMethodA(cl, obj, msg);
}
///
/// OVERLOAD(OM_GET)
OVERLOAD(OM_GET)
{
  GETDATA;
  IPTR *store = ((struct opGet *)msg)->opg_Storage;

  switch(((struct opGet *)msg)->opg_AttrID)
  {
    case ATTR(Modified):     *store = 1; return TRUE;
    case ATTR(ActiveObject): *store = (ULONG)data->activeObject; return TRUE;
  }

  return DoSuperMethodA(cl, obj, msg);
}
///

/* Public Methods */
/// DECLARE(Clear)
DECLARE(Clear)
{
  GETDATA;
  int m;

  for(m = 0; m < 5; m++)
  {
    // reset all GUI elements due to the new active filter
    nnset(data->CY_COMP[m], MUIA_Cycle_Active, 0);

    if(data->ST_MATCH[m] != NULL)
      nnset(data->ST_MATCH[m], MUIA_String_Contents, "");
    else
      nnset(data->CY_STATUS, MUIA_Cycle_Active, 0);

    if(data->CH_CASESENS[m] != NULL)
      nnset(data->CH_CASESENS[m], MUIA_Selected, FALSE);

    if(data->CH_SUBSTR[m] != NULL)
      nnset(data->CH_SUBSTR[m], MUIA_Selected, FALSE);

    if(data->CH_DOSPATTERN[m] != NULL)
      nnset(data->CH_DOSPATTERN[m], MUIA_Selected, FALSE);
  }

  return 0;
}

///
/// DECLARE(PrepareSearch)
DECLARE(PrepareSearch) // struct Search *search
{
  GETDATA;
  int pg = xget(data->PG_SRCHOPT, MUIA_Group_ActivePage);
  const char *match;
  char *field;

  if(pg != 3) // Page 3 (Status) has no ST_MATCH
    match = (const char *)xget(data->ST_MATCH[pg], MUIA_String_Contents);
  else
    match = "";

  field = (char *)xget(data->ST_FIELD, MUIA_String_Contents);

  FI_PrepareSearch(msg->search,
                   GetMUICycle(data->CY_MODE[data->remoteFilterMode]),
                   GetMUICheck(data->CH_CASESENS[pg]),
                   GetMUIRadio(data->RA_ADRMODE),
                   GetMUICycle(data->CY_COMP[pg]),
                   mailStatusCycleMap[GetMUICycle(data->CY_STATUS)],
                   pg < 2 ? GetMUICheck(data->CH_SUBSTR[pg]) : (pg == 4 ? TRUE : FALSE),
                   GetMUICheck(data->CH_DOSPATTERN[pg]),
                   match,
                   field);

  return 0;
}

///
/// DECLARE(SetToRule)
// fills a rule structure from the settings of this search controls
DECLARE(SetToRule) // struct RuleNode *rule
{
  GETDATA;
  int g = xget(data->PG_SRCHOPT, MUIA_Group_ActivePage);
  struct RuleNode *rule = msg->rule;

  rule->searchMode = GetMUICycle(data->CY_MODE[data->remoteFilterMode]);
  rule->combine = GetMUICycle(data->CY_COMBINE)+1;
  rule->subSearchMode = GetMUIRadio(data->RA_ADRMODE);
  GetMUIString(rule->customField, data->ST_FIELD, sizeof(rule->customField));
  rule->comparison = GetMUICycle(data->CY_COMP[g]);

  if(g != 3) // Page 3 (Status) has no ST_MATCH
    GetMUIString(rule->matchPattern, data->ST_MATCH[g], sizeof(rule->matchPattern));
  else
  {
    rule->matchPattern[0] = mailStatusCycleMap[GetMUICycle(data->CY_STATUS)];
    rule->matchPattern[1] = '\0';
  }

  if(data->CH_CASESENS[g] != NULL)
    rule->caseSensitive = GetMUICheck(data->CH_CASESENS[g]);

  if(data->CH_SUBSTR[g] != NULL)
    rule->subString = GetMUICheck(data->CH_SUBSTR[g]);

  if(data->CH_DOSPATTERN[g] != NULL)
    rule->dosPattern = GetMUICheck(data->CH_DOSPATTERN[g]);

  return 0;
}

///
/// DECLARE(GetFromRule)
// fills a rule structure from the settings of this search controls
DECLARE(GetFromRule) // struct RuleNode *rule
{
  GETDATA;
  struct RuleNode *rule = msg->rule;
  int g = Mode2Group[rule->searchMode];

  nnset(data->CY_MODE[data->remoteFilterMode],  MUIA_Cycle_Active,      rule->searchMode);
  nnset(data->CY_COMBINE,                       MUIA_Cycle_Active,      rule->combine>0 ? rule->combine-1 : 0);
  nnset(data->RA_ADRMODE,                       MUIA_Radio_Active,      rule->subSearchMode);
  nnset(data->ST_FIELD,                         MUIA_String_Contents,   rule->customField);
  nnset(data->PG_SRCHOPT,                       MUIA_Group_ActivePage,  g);
  nnset(data->CY_COMP[g],                       MUIA_Cycle_Active,      rule->comparison);

  if(g != 3) // Page 3 (Status) has no ST_MATCH
    nnset(data->ST_MATCH[g], MUIA_String_Contents, rule->matchPattern);
  else
  {
    int i;

    for(i=0; i <= 8; i++)
    {
      if(*rule->matchPattern == mailStatusCycleMap[i])
      {
        nnset(data->CY_STATUS, MUIA_Cycle_Active, i);
        break;
      }
    }
  }

  if(data->CH_CASESENS[g] != NULL)
    nnset(data->CH_CASESENS[g], MUIA_Selected, rule->caseSensitive);

  if(data->CH_SUBSTR[g] != NULL)
    nnset(data->CH_SUBSTR[g], MUIA_Selected, rule->subString);

  if(data->CH_DOSPATTERN[g] != NULL)
    nnset(data->CH_DOSPATTERN[g], MUIA_Selected, rule->dosPattern);

  return 0;
}

///
/// DECLARE(Update)
//  Selects correct form for search mode
DECLARE(Update)
{
  GETDATA;
  ULONG mode = GetMUICycle(data->CY_MODE[data->remoteFilterMode]);

  ENTER();

  if(mode < 12)
  {
    ULONG group = Mode2Group[mode];
    Object *newActiveObj = NULL;

    set(data->PG_SRCHOPT, MUIA_Group_ActivePage, group);

    switch(group)
    {
      case 0:
      case 1:
      case 2:
      case 4:
        newActiveObj = data->ST_MATCH[group];
      break;

      case 3:
        newActiveObj = data->CY_STATUS;
      break;
    }

    if(_win(obj) != NULL &&
       ((Object *)xget(_win(obj), MUIA_Window_ActiveObject) == NULL ||
        (Object *)xget(_win(obj), MUIA_Window_ActiveObject) == data->activeObject))
    {
      set(_win(obj), MUIA_Window_ActiveObject, newActiveObj);
    }

    data->activeObject = newActiveObj;

    set(obj, MUIA_Disabled, FALSE);
  }

  set(obj, MUIA_SearchControlGroup_Modified, TRUE);

  RETURN(0);
  return 0;
}

///
