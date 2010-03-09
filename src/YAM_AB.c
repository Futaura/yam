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

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <clib/alib_protos.h>
#include <dos/datetime.h>
#include <libraries/asl.h>
#include <libraries/iffparse.h>
#include <libraries/gadtools.h>
#include <libraries/locale.h>
#include <mui/BetterString_mcc.h>
#include <mui/NList_mcc.h>
#include <mui/NListview_mcc.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <proto/codesets.h>
#include <proto/expat.h>

#include "extrasrc.h"

#include "SDI_hook.h"

#include "YAM.h"
#include "YAM_addressbook.h"
#include "YAM_addressbookEntry.h"
#include "YAM_config.h"
#include "YAM_error.h"
#include "YAM_global.h"
#include "YAM_main.h"
#include "YAM_utilities.h"
#include "YAM_write.h"

#include "mui/Classes.h"
#include "mime/base64.h"

#include "Locale.h"
#include "MUIObjects.h"
#include "Requesters.h"
#include "FileInfo.h"

#include "Debug.h"

enum XMLSection
{
  xs_Unknown = 0,
  xs_Group,
  xs_Contact,
};

enum XMLData
{
  xd_Unknown = 0,
  xd_GroupName,
  xd_ContactName,
  xd_Description,
  xd_Address,
  xd_Alias,
  xd_PGPId,
  xd_Homepage,
  xd_Portrait,
  xd_BirthDay,
  xd_Street,
  xd_City,
  xd_ZIPCode,
  xd_State,
  xd_Country,
  xd_Phone,
};

struct XMLUserData
{
  enum XMLSection section;
  enum XMLData dataType;
  struct ABEntry entry;
  XML_Char xmlData[SIZE_LARGE];
  size_t xmlDataSize;
  BOOL sorted;
};

/***************************************************************************
 Module: Address book
***************************************************************************/

/// AB_GotoEntry
//  Searches an entry by alias and activates it
APTR AB_GotoEntry(char *alias)
{
  struct MUI_NListtree_TreeNode *tn = NULL;

  ENTER();

  if(alias != NULL)
  {
    if((tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_FindName, MUIV_NListtree_FindName_ListNode_Root, alias, MUIF_NONE)) != NULL)
    {
      DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Open, MUIV_NListtree_Open_ListNode_Parent, tn, MUIF_NONE);
      set(G->AB->GUI.LV_ADDRESSES, MUIA_NListtree_Active, tn);
    }
  }

  RETURN(tn);
  return tn;
}

///
/// ParseDateString
// parse a date string produces by strftime() and put the result in a struct tm
enum ScanDateState
{
  SDS_DEFAULT = 0,
  SDS_SPECIFIER,
  SDS_DONE,
  SDS_SECOND,
  SDS_MINUTE,
  SDS_HOUR,
  SDS_DAY_OF_MONTH,
  SDS_MONTH,
  SDS_YEAR,
  SDS_DAY_OF_WEEK,
  SDS_DAY_YEAR,
  SDS_IS_DST,
};

#define FLG_SEC         (1<<0)
#define FLG_MIN         (1<<1)
#define FLG_HOUR        (1<<2)
#define FLG_MDAY        (1<<3)
#define FLG_MON         (1<<4)
#define FLG_YEAR        (1<<5)
#define FLG_WDAY        (1<<6)
#define FLG_YDAY        (1<<7)
#define FLG_ISDST       (1<<8)
#define FLG_4DIGIT_YEAR (1<<9)

static BOOL ScanDateString(const char *string, const char *fmt, struct tm *res)
{
  BOOL result = TRUE;
  char fc;
  char sc;
  enum ScanDateState state = SDS_DEFAULT;
  int flags = 0;

  ENTER();

  D(DBF_UTIL, "scan date string '%s' with format '%s'", string, fmt);

  memset(res, 0, sizeof(*res));

  // start with the first character in both strings
  fc = *fmt++;
  sc = *string++;

  while(state != SDS_DONE)
  {
    if(fc == '\0' && sc == '\0')
      state = SDS_DONE;

    switch(state)
    {
      case SDS_DEFAULT:
      {
        if(fc == '%')
        {
          state = SDS_SPECIFIER;
          fc = *fmt++;
        }
        else
        {
          // the format string seems to be malformed, bail out
          state = SDS_DONE;
        }
      }
      break;

      case SDS_SPECIFIER:
      {
        switch(fc)
        {
          case 'd':
          {
            SET_FLAG(flags, FLG_MDAY);
            state = SDS_DAY_OF_MONTH;
            fc = *fmt++;
          }
          break;

          case 'm':
          {
            SET_FLAG(flags, FLG_MON);
            state = SDS_MONTH;
            fc = *fmt++;
          }
          break;

          case 'Y':
          {
            SET_FLAG(flags, FLG_4DIGIT_YEAR);
          }
          // we fall through here

          case 'y':
          {
            SET_FLAG(flags, FLG_YEAR);
            state = SDS_YEAR;
            fc = *fmt++;
          }
          break;

          default:
          {
            // unknown specifier, bail out
            state = SDS_DONE;
          }
          break;
        }
      }
      break;

      case SDS_DAY_OF_MONTH:
      {
        if(sc == fc)
        {
          // next separator in format string found
          state = SDS_DEFAULT;
          fc = *fmt++;
          sc = *string++;
        }
        else if(sc >= '0' && sc <= '9')
        {
          // valid number found, add it to the day of month
          res->tm_mday = res->tm_mday * 10 + sc - '0';
          sc = *string++;
        }
        else
        {
          // unexpected character, bail out
          state = SDS_DONE;
        }
      }
      break;

      case SDS_MONTH:
      {
        if(sc == fc)
        {
          // next separator in format string found
          state = SDS_DEFAULT;
          fc = *fmt++;
          sc = *string++;
        }
        else if(sc >= '0' && sc <= '9')
        {
          // valid number found, add it to the month
          res->tm_mon = res->tm_mon * 10 + sc - '0';
          sc = *string++;
        }
        else
        {
          // unexpected character, bail out
          state = SDS_DONE;
        }
      }
      break;

      case SDS_YEAR:
      {
        if(sc == fc)
        {
          // next separator in format string found
          state = SDS_DEFAULT;
          fc = *fmt++;
          sc = *string++;
        }
        else if(sc >= '0' && sc <= '9')
        {
          // valid number found, add it to the year
          res->tm_year = res->tm_year * 10 + sc - '0';
          sc = *string++;
        }
        else
        {
          // unexpected character, bail out
          state = SDS_DONE;
        }
      }
      break;

      default:
        // nothing to do
      break;
    }
  }

  // finally check if the calculated values are correct, but only those which
  // were specified in the format string
  if(isFlagSet(flags, FLG_MDAY) || strstr(fmt, "%d") != NULL)
  {
    if(res->tm_mday >= 1 && res->tm_mday <= 31)
    {
      // nothing to adjust
    }
    else
    {
      W(DBF_UTIL, "bad day number %ld", res->tm_mday);
      result = FALSE;
    }
  }
  if(isFlagSet(flags, FLG_MON) || strstr(fmt, "%m") != NULL)
  {
    if(res->tm_mon >= 1 && res->tm_mon <= 12)
    {
      // tm_mon counts from 0 to 11
      res->tm_mon--;
    }
    else
    {
      W(DBF_UTIL, "bad month number %ld", res->tm_mon);
      result = FALSE;
    }
  }
  if(isFlagSet(flags, FLG_YEAR) || strstr(fmt, "%y") != NULL || strstr(fmt, "%Y") != NULL)
  {
    if(isFlagSet(flags, FLG_4DIGIT_YEAR) || strstr(fmt, "%Y") != NULL)
    {
      if(res->tm_year >= 1900)
      {
        // tm_year counts the years from 1900
        res->tm_year -= 1900;
      }
      else
      {
        // year numbers less than 1900 are not supported
        W(DBF_UTIL, "bad year number %ld", res->tm_year);
        result = FALSE;
      }
    }
    else
    {
      // 2 digit year number, must be less than 100
      if(res->tm_year < 100)
      {
        if(res->tm_year < 40)
        {
          // tm_year counts the years from 1900
          // if the year number is less than 40 we assume a year between
          // 2000 and 2039 instead of between 1900 and 1939 to allow a user
          // age of at least ~70 years.
          res->tm_year += 100;
        }
      }
      // Although we expect a two digit year number for %y we got one with more digits.
      // Better not fail at this even if the entered string is wrong. People tend to
      // forget the correct formatting.
      else if(res->tm_year >= 1900)
      {
        // tm_year counts the years from 1900
        res->tm_year -= 1900;
      }
      else
      {
        // numbers between 100 and 1899 are definitely not allowed
        W(DBF_UTIL, "bad year number %ld", res->tm_year);
        result = FALSE;
      }
    }
  }

  // finally check if the day value is correct
  if(result == TRUE && isFlagSet(flags, FLG_MDAY))
  {
    if(res->tm_mon == 1)
    {
      // February has 29 days at most, but we don't check for leap years here
      if(res->tm_mday > 29)
      {
        W(DBF_UTIL, "wrong number of days (%ld) for February", res->tm_mday);
        result = FALSE;
      }
    }
    else if(res->tm_mon ==  3 ||
            res->tm_mon ==  5 ||
            res->tm_mon ==  8 ||
            res->tm_mon == 10)
    {
      // April, June, September and November have 30 days
      if(res->tm_mday > 30)
      {
        W(DBF_UTIL, "wrong number of days (%ld) for April, June, September or November", res->tm_mday);
        result = FALSE;
      }
    }
  }

  D(DBF_UTIL, "scaned date day=%ld month=%ld year=%ld", res->tm_mday, res->tm_mon, res->tm_year);

  RETURN(result);
  return result;
}

///
/// AB_ExpandBD
//  Converts date from numeric into textual format
char *AB_ExpandBD(long date)
{
  static char datestr[SIZE_SMALL];
  ldiv_t d;
  LONG day;
  LONG month;
  LONG year;

  ENTER();

  d = ldiv(date, 10000);
  year = d.rem;
  d = ldiv(d.quot, 100);
  month = d.rem;
  day = d.quot;

  // check first if it could be a valid date!
  // I think we can assume that nobody used EMail before WW1 :)
  if(date == 0 || day < 1 || day > 31 || month < 1 || month > 12 || year < 1900)
    datestr[0] = '\0';
  else
  {
    struct tm tm;
    STRPTR dateFormat;

    tm.tm_mday = day;
    tm.tm_mon = month - 1;
    tm.tm_year = year - 1900;

    dateFormat = G->Locale != NULL ? G->Locale->loc_ShortDateFormat : (STRPTR)"%d.%m.%Y";
    D(DBF_UTIL, "formatting date %ld as %ld/%ld/%ld -> '%s'", date, day, month, year, dateFormat);
    strftime(datestr, sizeof(datestr), dateFormat, &tm);
    D(DBF_UTIL, "formatted date string is '%s'", datestr);
  }

  RETURN(datestr);
  return datestr;
}

///
/// AB_CompressBD
//  Connverts date from textual into numeric format
long AB_CompressBD(char *datestr)
{
  long result = 0;
  struct tm tm;

  ENTER();

  if(ScanDateString(datestr, G->Locale != NULL ? G->Locale->loc_ShortDateFormat : (STRPTR)"%d.%m.%Y", &tm) == TRUE)
  {
    result = tm.tm_mday * 1000000 + (tm.tm_mon + 1) * 10000 + (tm.tm_year + 1900);
  }

  RETURN(result);
  return result;
}

///
/// AB_CheckBirthdates
// searches the address book for todays birth days
void AB_CheckBirthdates(void)
{
  ldiv_t today;
  struct MUI_NListtree_TreeNode *tn;
  int i;

  ENTER();

  today = ldiv(DateStamp2Long(NULL), 10000);
  i = 0;
  while((tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_GetEntry, MUIV_NListtree_GetEntry_ListNode_Root, i, MUIF_NONE)) != NULL)
  {
    struct ABEntry *ab = tn->tn_User;

    if(ab->Type == AET_USER)
    {
      ldiv_t birthday = ldiv(ab->BirthDay, 10000);

      if(birthday.quot == today.quot)
      {
        char question[SIZE_LARGE];
        char *name = *ab->RealName ? ab->RealName : ab->Alias;

        snprintf(question, sizeof(question), tr(MSG_AB_BirthdayReq), name, today.rem - birthday.rem);

        if(MUI_Request(G->App, G->MA->GUI.WI, 0, tr(MSG_AB_BirthdayReminder), tr(MSG_YesNoReq), question))
        {
          struct WriteMailData *wmData;

          if((wmData = NewWriteMailWindow(NULL, 0)) != NULL)
          {
            xset(wmData->window, MUIA_WriteWindow_To, ab->Alias,
                                 MUIA_WriteWindow_Subject, tr(MSG_AB_HappyBirthday));
          }
        }
      }
    }

    i++;
  }

  LEAVE();
}

///
/// AB_SearchEntry
//  Searches the address book by alias, name or address
//  it will break if there is more then one entry
int AB_SearchEntry(char *text, int mode, struct ABEntry **ab)
{
  struct MUI_NListtree_TreeNode *tn;
  struct ABEntry *ab_found;
  int i;
  int hits = 0;
  BOOL found = FALSE;
  int mode_type = mode&ASM_TYPEMASK;
  LONG tl = strlen(text);

  ENTER();

  // we scan until we are at the end of the list or
  // if we found more then one matching entry
  for(i = 0; hits <= 2; i++, found = FALSE)
  {
    tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_GetEntry, MUIV_NListtree_GetEntry_ListNode_Root, i, MUIF_NONE);
    if(tn == NULL)
      break;

    // now we set the AB_Entry
    ab_found = tn->tn_User;
    if(ab_found == NULL)
      break;

    // now we check if this entry is one of the not wished entry types
    // and then we skip it.
    if(ab_found->Type == AET_USER  && !isUserSearch(mode))
      continue;
    if(ab_found->Type == AET_LIST  && !isListSearch(mode))
      continue;
    if(ab_found->Type == AET_GROUP && !isGroupSearch(mode))
      continue;

    if(isCompleteSearch(mode))
    {
      // Now we check for the ALIAS->REALNAME->ADDRESS, so only ONE mode is allowed at a time
      if(isAliasSearch(mode_type))
        found = !Strnicmp(ab_found->Alias,    text, tl);
      else if(isRealNameSearch(mode_type))
        found = !Strnicmp(ab_found->RealName, text, tl);
      else if(isAddressSearch(mode_type))
        found = !Strnicmp(ab_found->Address,  text, tl);
    }
    else
    {
      // Now we check for the ALIAS->REALNAME->ADDRESS, so only ONE mode is allowed at a time
      if(isAliasSearch(mode_type))
        found = !Stricmp(ab_found->Alias,    text);
      else if(isRealNameSearch(mode_type))
        found = !Stricmp(ab_found->RealName, text);
      else if(isAddressSearch(mode_type))
        found = !Stricmp(ab_found->Address,  text);
    }

    if(found == TRUE)
    {
      *ab = ab_found;
      hits++;
    }
  }

  RETURN(hits);
  return hits;
}

///
/// AB_CompleteAlias
//  Auto-completes alias or name in recipient field
char *AB_CompleteAlias(char *text)
{
  char *compl = NULL;
  struct ABEntry *ab = NULL;

  ENTER();

  if(AB_SearchEntry(text, ASM_ALIAS|ASM_USER|ASM_LIST|ASM_COMPLETE, &ab) == 1)
  {
    compl = ab->Alias;
  }
  else if(AB_SearchEntry(text, ASM_REALNAME|ASM_USER|ASM_LIST|ASM_COMPLETE, &ab) == 1)
  {
    compl = ab->RealName;
  }
  else if(AB_SearchEntry(text, ASM_ADDRESS|ASM_USER|ASM_LIST|ASM_COMPLETE, &ab) == 1)
  {
    compl = ab->Address;
  }

  if(compl != NULL)
    compl = &compl[strlen(text)];

  RETURN(compl);
  return compl;
}

///
/// AB_InsertAddress
//  Adds a new recipient to a recipient field
void AB_InsertAddress(APTR string, const char *alias, const char *name, const char *address)
{
  char *p;

  ENTER();

  p = (char *)xget(string, MUIA_UserData);
  if(p != NULL)
  {
    p = (char *)xget(string, MUIA_String_Contents);
    if(*p != '\0')
      DoMethod(string, MUIM_BetterString_Insert, ", ", MUIV_BetterString_Insert_EndOfString);
  }
  else
    setstring(string, "");

  if(*alias != '\0')
    DoMethod(string, MUIM_BetterString_Insert, alias, MUIV_BetterString_Insert_EndOfString);
  else
  {
    if(*name != '\0')
    {
      if(strchr(name, ','))
        DoMethod(string, MUIM_BetterString_Insert, "\"", MUIV_BetterString_Insert_EndOfString);
      DoMethod(string, MUIM_BetterString_Insert, name, MUIV_BetterString_Insert_EndOfString);
      if(strchr(name, ','))
        DoMethod(string, MUIM_BetterString_Insert, "\"", MUIV_BetterString_Insert_EndOfString);
    }
    if(*address != '\0')
    {
      if(*name != '\0')
        DoMethod(string, MUIM_BetterString_Insert, " <", MUIV_BetterString_Insert_EndOfString);
      DoMethod(string, MUIM_BetterString_Insert, address, MUIV_BetterString_Insert_EndOfString);
      if(*name != '\0')
        DoMethod(string, MUIM_BetterString_Insert, ">", MUIV_BetterString_Insert_EndOfString);
    }
  }
}

///
/// AB_FromAddrBook
/*** AB_FromAddrBook - Inserts an address book entry into a recipient string ***/
HOOKPROTONHNO(AB_FromAddrBook, void, ULONG *arg)
{
  struct MUI_NListtree_TreeNode *active;

  ENTER();

  if(arg[0] != ABM_NONE &&
     (active = (struct MUI_NListtree_TreeNode *)xget(G->AB->GUI.LV_ADDRESSES, MUIA_NListtree_Active)) != NULL)
  {
    Object *writeWindow = NULL;
    struct ABEntry *addr = (struct ABEntry *)(active->tn_User);

    if(G->AB->winNumber == -1)
    {
      struct WriteMailData *wmData = NewWriteMailWindow(NULL, 0);
      if(wmData != NULL)
        writeWindow = wmData->window;
    }
    else
    {
      struct Node *curNode;

      // find the write window object by iterating through the
      // global write window list and identify it via its window number
      IterateList(&G->writeMailDataList, curNode)
      {
        struct WriteMailData *wmData = (struct WriteMailData *)curNode;

        if(wmData->window != NULL &&
           (int)xget(wmData->window, MUIA_WriteWindow_Num) == G->AB->winNumber)
        {
          writeWindow = wmData->window;
          break;
        }
      }
    }

    if(writeWindow != NULL)
    {
      enum RcptType type = MUIV_WriteWindow_RcptType_To;

      switch(arg[0])
      {
        case ABM_TO:      type = MUIV_WriteWindow_RcptType_To; break;
        case ABM_CC:      type = MUIV_WriteWindow_RcptType_Cc; break;
        case ABM_BCC:     type = MUIV_WriteWindow_RcptType_BCC; break;
        case ABM_REPLYTO: type = MUIV_WriteWindow_RcptType_ReplyTo; break;
        case ABM_FROM:    type = MUIV_WriteWindow_RcptType_From; break;
      }

      DoMethod(writeWindow, MUIM_WriteWindow_AddRecipient, type, addr->Alias ? addr->Alias : addr->RealName);
    }
  }

  LEAVE();
}
MakeStaticHook(AB_FromAddrBookHook, AB_FromAddrBook);

///
/// AB_LoadTree
//  Loads the address book from a file
BOOL AB_LoadTree(char *fname, BOOL append, BOOL sorted)
{
  FILE *fh;
  BOOL result = FALSE;

  ENTER();

  if((fh = fopen(fname, "r")) != NULL)
  {
    char *buffer = NULL;
    size_t size = 0;

    setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);

    if(GetLine(&buffer, &size, fh) >= 0)
    {
      int nested = 0;
      struct MUI_NListtree_TreeNode *parent[8];

      parent[0] = MUIV_NListtree_Insert_ListNode_Root;

      if(strncmp(buffer,"YAB",3) == 0)
      {
        int version = buffer[3] - '0';

        G->AB->Modified = append;
        if(append == FALSE)
          DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Clear, NULL, 0);

        set(G->AB->GUI.LV_ADDRESSES, MUIA_NListtree_Quiet, TRUE);

        while(GetLine(&buffer, &size, fh) >= 0)
        {
          struct ABEntry addr;

          memset(&addr, 0, sizeof(struct ABEntry));

          if(strncmp(buffer, "@USER", 5) == 0)
          {
            addr.Type = AET_USER;
            strlcpy(addr.Alias, Trim(&buffer[6]), sizeof(addr.Alias));
            GetLine(&buffer, &size, fh);
            strlcpy(addr.Address, Trim(buffer), sizeof(addr.Address));
            GetLine(&buffer, &size, fh);
            strlcpy(addr.RealName, Trim(buffer), sizeof(addr.RealName));
            GetLine(&buffer, &size, fh);
            strlcpy(addr.Comment, Trim(buffer), sizeof(addr.Comment));
            if(version > 2)
            {
              GetLine(&buffer, &size, fh);
              strlcpy(addr.Phone, Trim(buffer), sizeof(addr.Phone));
              GetLine(&buffer, &size, fh);
              strlcpy(addr.Street, Trim(buffer), sizeof(addr.Street));
              GetLine(&buffer, &size, fh);
              strlcpy(addr.City, Trim(buffer), sizeof(addr.City));
              GetLine(&buffer, &size, fh);
              strlcpy(addr.Country, Trim(buffer), sizeof(addr.Country));
              GetLine(&buffer, &size, fh);
              strlcpy(addr.PGPId, Trim(buffer), sizeof(addr.PGPId));
              GetLine(&buffer, &size, fh);
              addr.BirthDay = atol(Trim(buffer));
              GetLine(&buffer, &size, fh);
              strlcpy(addr.Photo, Trim(buffer), sizeof(addr.Photo));
              GetLine(&buffer, &size, fh);
              if(strcmp(buffer, "@ENDUSER") == 0)
                strlcpy(addr.Homepage, Trim(buffer), sizeof(addr.Homepage));
            }
            if(version > 3)
            {
              GetLine(&buffer, &size, fh);
              addr.DefSecurity = atoi(Trim(buffer));
            }
            do
            {
              if(strcmp(buffer, "@ENDUSER") == 0)
                break;
            }
            while(GetLine(&buffer, &size, fh) >= 0);

            DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Insert, addr.Alias[0] ? addr.Alias : addr.RealName, &addr, parent[nested], sorted ?  MUIV_NListtree_Insert_PrevNode_Sorted : MUIV_NListtree_Insert_PrevNode_Tail, MUIF_NONE);
          }
          else if(strncmp(buffer, "@LIST", 5) == 0)
          {
            char *members;

            addr.Type = AET_LIST;

            strlcpy(addr.Alias, Trim(&buffer[6]), sizeof(addr.Alias));
            if(version > 2)
            {
              GetLine(&buffer, &size, fh);
              strlcpy(addr.Address , Trim(buffer), sizeof(addr.Address));
              GetLine(&buffer, &size, fh);
              strlcpy(addr.RealName, Trim(buffer), sizeof(addr.RealName));
            }
            GetLine(&buffer, &size, fh);
            strlcpy(addr.Comment , Trim(buffer), sizeof(addr.Comment));
            members = AllocStrBuf(SIZE_DEFAULT);
            while(GetLine(&buffer, &size, fh) >= 0)
            {
              if(strcmp(buffer, "@ENDLIST") == 0)
                break;

              if(*buffer == '\0')
                continue;

              members = StrBufCat(members, buffer);
              members = StrBufCat(members, "\n");
            }
            if((addr.Members = strdup(members)) != NULL)
            {
              FreeStrBuf(members);
              DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Insert, addr.Alias, &addr, parent[nested], sorted ?  MUIV_NListtree_Insert_PrevNode_Sorted : MUIV_NListtree_Insert_PrevNode_Tail, MUIF_NONE);
              free(addr.Members);
            }
          }
          else if(strncmp(buffer, "@GROUP", 6) == 0)
          {
            addr.Type = AET_GROUP;
            strlcpy(addr.Alias  , Trim(&buffer[7]), sizeof(addr.Alias));
            GetLine(&buffer, &size, fh);
            strlcpy(addr.Comment, Trim(buffer), sizeof(addr.Comment));
            nested++;
            parent[nested] = (struct MUI_NListtree_TreeNode *)DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Insert, addr.Alias, &addr, parent[nested-1], MUIV_NListtree_Insert_PrevNode_Tail, TNF_LIST);
          }
          else if(strcmp(buffer,"@ENDGROUP") == 0)
          {
            nested--;
          }
        }

        set(G->AB->GUI.LV_ADDRESSES, MUIA_NListtree_Quiet, FALSE);

        // no errors happened
        result = TRUE;
      }
      else
      {
        // ask the user if he really wants to read out a non YAM
        // Addressbook file.
        if(MUI_Request(G->App, G->AB->GUI.WI, MUIF_NONE, NULL, tr(MSG_AB_NOYAMADDRBOOK_GADS), tr(MSG_AB_NOYAMADDRBOOK), fname))
        {
          G->AB->Modified = append;
          if(append == FALSE)
            DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Clear, NULL, 0);

          fseek(fh, 0, SEEK_SET);
          while(GetLine(&buffer, &size, fh) >= 0)
          {
            struct ABEntry addr;
            char *p, *p2;

            memset(&addr, 0, sizeof(struct ABEntry));
            if((p = strchr(buffer, ' ')) != NULL)
              *p = '\0';
            strlcpy(addr.Address, buffer, sizeof(addr.Address));
            if(p != NULL)
            {
              strlcpy(addr.RealName, ++p, sizeof(addr.RealName));
              if((p2 = strchr(p, ' ')) != NULL)
                 *p2 = '\0';
            }
            else
            {
              p = buffer;
              if((p2 = strchr(p, '@')) != NULL)
                 *p2 = '\0';
            }
            strlcpy(addr.Alias, p, sizeof(addr.Alias));
            DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Insert, addr.Alias, &addr, parent[nested], sorted ?  MUIV_NListtree_Insert_PrevNode_Sorted : MUIV_NListtree_Insert_PrevNode_Tail, MUIF_NONE);
          }
        }

        // no errors happened
        result = TRUE;
      }
    }
    else
      ER_NewError(tr(MSG_ER_ADDRBOOKLOAD), fname);

    fclose(fh);

    if(buffer != NULL)
      free(buffer);
  }
  else
    ER_NewError(tr(MSG_ER_ADDRBOOKLOAD), fname);

  RETURN(result);
  return result;
}

///
/// AB_SaveTreeNode (rec)
//  Recursively saves an address book node
static STACKEXT void AB_SaveTreeNode(FILE *fh, struct MUI_NListtree_TreeNode *list)
{
  struct MUI_NListtree_TreeNode *tn;
  int i;

  ENTER();

  for(i = 0; ; i++)
  {
    if((tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_GetEntry, list, i, MUIV_NListtree_GetEntry_Flag_SameLevel)) != NULL)
    {
      struct ABEntry *ab = tn->tn_User;

      switch(ab->Type)
      {
        case AET_USER:
          fprintf(fh, "@USER %s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%08ld\n%s\n%s\n%d\n@ENDUSER\n", ab->Alias, ab->Address, ab->RealName, ab->Comment,
                      ab->Phone, ab->Street, ab->City, ab->Country, ab->PGPId, ab->BirthDay, ab->Photo, ab->Homepage, ab->DefSecurity);
          break;
        case AET_LIST:
          fprintf(fh, "@LIST %s\n%s\n%s\n%s\n%s\n@ENDLIST\n", ab->Alias, ab->Address, ab->RealName, ab->Comment, ab->Members);
          break;
        case AET_GROUP:
          fprintf(fh, "@GROUP %s\n%s\n", ab->Alias, ab->Comment);
          AB_SaveTreeNode(fh, tn);
          fputs("@ENDGROUP\n", fh);
          break;
      }
    }
    else
      break;
  }

  LEAVE();
}

///
/// AB_SaveTree
//  Saves the address book to a file
BOOL AB_SaveTree(char *fname)
{
  FILE *fh;
  BOOL result = FALSE;

  ENTER();

  if((fh = fopen(fname, "w")) != NULL)
  {
    setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);

    fputs("YAB4 - YAM Addressbook\n", fh);
    AB_SaveTreeNode(fh, MUIV_NListtree_GetEntry_ListNode_Root);
    fclose(fh);
    AppendToLogfile(LF_VERBOSE, 70, tr(MSG_LOG_SavingABook), fname);

    result = TRUE;
  }
  else
    ER_NewError(tr(MSG_ER_CantCreateFile), fname);

  RETURN(result);
  return result;
}

///
/// AB_ImportTreeLDIF
//  Imports an address book in LDIF format
BOOL AB_ImportTreeLDIF(char *fname, BOOL append, BOOL sorted)
{
  FILE *fh;
  BOOL result = FALSE;

  ENTER();

  if((fh = fopen(fname, "r")) != NULL)
  {
    char *buffer = NULL;
    size_t size = 0;
    struct ABEntry addr;

    setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);

    G->AB->Modified = append;
    if(append == FALSE)
      DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Clear, NULL, 0);

    set(G->AB->GUI.LV_ADDRESSES, MUIA_NListtree_Quiet, TRUE);

    while(GetLine(&buffer, &size, fh) >= 0)
    {
      // an empty line separates two user entries
      if(buffer[0] == '\0')
      {
        // we need at least an EMail address
        if(addr.Address[0] != '\0')
        {
          // set up an alias only if none is given
          if(addr.Alias[0] == '\0')
            EA_SetDefaultAlias(&addr);

          // put it into the tree
          DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Insert, addr.Alias[0] ? addr.Alias : addr.RealName, &addr, MUIV_NListtree_Insert_ListNode_Root, sorted ?  MUIV_NListtree_Insert_PrevNode_Sorted : MUIV_NListtree_Insert_PrevNode_Tail, MUIF_NONE);
          result = TRUE;
        }
      }
      else
      {
        char *key, *value;

        // every line has the pattern "key: value"
        // now separate these two
        key = buffer;
        if((value = strpbrk(key, ":")) != NULL)
        {
          char b64buffer[SIZE_LARGE];
          BOOL utf8;

          *value++ = '\0';

          // a leading colon in the value marks a base64 encoded string
          if(value[0] == ':')
          {
            // first decode it
            base64decode(b64buffer, (const unsigned char *)&value[2], strlen(value) - 2);

            // now convert this prossible UTF8 string to a normal string
            value = CodesetsUTF8ToStr(CSA_Source,          Trim(b64buffer),
                                      CSA_DestCodeset,     G->readCharset,
                                      CSA_MapForeignChars, C->MapForeignChars,
                                      TAG_DONE);
            utf8 = TRUE;
          }
          else
          {
            // take the value as it is
            value = Trim(value);
            utf8 = FALSE;
          }

          if(value != NULL)
          {
            // this is the very first line a new entry,
            // so clear the structure for further actions now
            if(stricmp(key, "dn") == 0)
            {
              memset(&addr, 0, sizeof(struct ABEntry));
              addr.Type = AET_USER;
            }
            else if(stricmp(key, "cn") == 0)                         // complete name
              strlcpy(addr.RealName, value, sizeof(addr.RealName));
            else if(stricmp(key, "mail") == 0)                       // mail address
              strlcpy(addr.Address, value, sizeof(addr.Address));
            else if(stricmp(key, "mozillaNickname") == 0)            // alias
              strlcpy(addr.Alias, value, sizeof(addr.Alias));
            else if(stricmp(key, "telephoneNumber") == 0)            // phone number
            {
              if(addr.Phone[0] != '\0')
                strlcat(addr.Phone, ", ", sizeof(addr.Phone));
              strlcat(addr.Phone, value, sizeof(addr.Phone));
            }
            else if(stricmp(key, "homePhone") == 0)                  // phone number
            {
              if(addr.Phone[0] != '\0')
                strlcat(addr.Phone, ", ", sizeof(addr.Phone));
              strlcat(addr.Phone, value, sizeof(addr.Phone));
            }
            else if(stricmp(key, "fax") == 0)                        // fax number
            {
              if(addr.Phone[0] != '\0')
              strlcat(addr.Phone, ", ", sizeof(addr.Phone));
              strlcat(addr.Phone, value, sizeof(addr.Phone));
            }
            else if(stricmp(key, "pager") == 0)                      // pager number
            {
              if(addr.Phone[0] != '\0')
                strlcat(addr.Phone, ", ", sizeof(addr.Phone));
              strlcat(addr.Phone, value, sizeof(addr.Phone));
            }
            else if(stricmp(key, "mobile") == 0)                     // mobile number
            {
              if(addr.Phone[0] != '\0')
                strlcat(addr.Phone, ", ", sizeof(addr.Phone));
              strlcat(addr.Phone, value, sizeof(addr.Phone));
            }
            else if(stricmp(key, "homeStreet") == 0)                 // office street
            {
              if(addr.Street[0] != '\0')
                strlcat(addr.Street, ", ", sizeof(addr.Street));
              strlcat(addr.Street, value, sizeof(addr.Street));
            }
            else if(stricmp(key, "mozillaHomeStreet2") == 0)         // home street
            {
              if(addr.Street[0] != '\0')
                strlcat(addr.Street, ", ", sizeof(addr.Street));
              strlcat(addr.Street, value, sizeof(addr.Street));
            }
            else if(stricmp(key, "l") == 0)                          // office locality
            {
              if(addr.City[0] != '\0')
                strlcat(addr.City, ", ", sizeof(addr.City));
              strlcat(addr.City, value, sizeof(addr.City));
            }
            else if(stricmp(key, "mozillaHomeLocalityName") == 0)    // home locality
            {
              if(addr.City[0] != '\0')
                strlcat(addr.City, ", ", sizeof(addr.City));
              strlcat(addr.City, value, sizeof(addr.City));
            }
            else if(stricmp(key, "postalCode") == 0)                 // office postal code
            {
              if(addr.City[0] != '\0')
                strlcat(addr.City, ", ", sizeof(addr.City));
              strlcat(addr.City, value, sizeof(addr.City));
            }
            else if(stricmp(key, "mozillaHomePostalCode") == 0)      // home postal code
            {
              if(addr.City[0] != '\0')
                strlcat(addr.City, ", ", sizeof(addr.City));
              strlcat(addr.City, value, sizeof(addr.City));
            }
            else if(stricmp(key, "c") == 0)                          // office country
            {
              if(addr.Country[0] != '\0')
                strlcat(addr.Country, ", ", sizeof(addr.Country));
              strlcat(addr.Country, value, sizeof(addr.Country));
            }
            else if(stricmp(key, "mozillaHomeCountryName") == 0)     // home country
            {
              if(addr.Country[0] != '\0')
                strlcat(addr.Country, ", ", sizeof(addr.Country));
              strlcat(addr.Country, value, sizeof(addr.Country));
            }
            else if(stricmp(key, "mozillaWorkUrl") == 0)             // working home page
            {
              if(addr.Homepage[0] != '\0')
                strlcat(addr.Homepage, ", ", sizeof(addr.Homepage));
              strlcat(addr.Homepage, value, sizeof(addr.Homepage));
            }
            else if(stricmp(key, "mozillaHomeUrl") == 0)             // private homepage
            {
              if(addr.Homepage[0] != '\0')
                strlcat(addr.Homepage, ", ", sizeof(addr.Homepage));
              strlcat(addr.Homepage, value, sizeof(addr.Homepage));
            }
          }

          // if the value string has been converted from UTF8 before we need to free it now
          if(utf8 == TRUE)
            CodesetsFreeA(value, NULL);
        }
      }
    }

    set(G->AB->GUI.LV_ADDRESSES, MUIA_NListtree_Quiet, FALSE);

    fclose(fh);

    if(buffer != NULL)
      free(buffer);
  }
  else
     ER_NewError(tr(MSG_ER_ADDRBOOKIMPORT), fname);

  RETURN(result);
  return result;
}

///
/// WriteLDIFLine
// writes a line to an LDIF address book file according to RFC 2849
static void WriteLDIFLine(FILE *fh, const char *key, const char *valueFmt, ...)
{
  ENTER();

  if(key[0] != '\0')
  {
    char buffer[SIZE_LARGE];
    va_list args;
    char *p;
    unsigned char c;
    BOOL initChar;
    BOOL mustBeEncoded;

    // put the arguments into the value string
    va_start(args, valueFmt);
    vsnprintf(buffer, sizeof(buffer), valueFmt, args);
    va_end(args);

    // now check if the value string must be UTF8/base64 encoded
    p = buffer;
    initChar = TRUE;
    mustBeEncoded = FALSE;

    while((c = *p++) != '\0' && mustBeEncoded == FALSE)
    {
      BOOL safeChar;

      // these characters are safe, everything else must be encoded
      // see RFC 2849
      if(initChar)
      {
        // safe init character
        safeChar = ((c >= 0x01 && c <= 0x09) ||
                    (c >= 0x0b && c <= 0x0c) ||
                    (c >= 0x0e && c <= 0x1f) ||
                    (c >= 0x21 && c <= 0x39) ||
                    (c == 0x3b)              ||
                    (c >= 0x3d && c <= 0x7f));

        initChar = FALSE;
      }
      else
      {
        // safe characters
        safeChar = ((c >= 0x01 && c <= 0x09) ||
                    (c >= 0x0b && c <= 0x0c) ||
                    (c >= 0x0e && c <= 0x7f));
      }

      // yes, we have to encode this string
      if(safeChar == FALSE)
        mustBeEncoded = TRUE;
    }

    if(mustBeEncoded == TRUE)
    {
      UTF8 *utf8;

      // convert the value string to UTF8
      if((utf8 = CodesetsUTF8Create(CSA_Source, buffer, TAG_DONE)) != NULL)
      {
        // we can reuse the former buffer here again, because we have a copy of the string
        // in utf8
        if(base64encode(buffer, utf8, strlen((char *)utf8)) > 0)
        {
          // write the key and encoded value strings
          // these are separated by a double colon
          fprintf(fh, "%s:: %s\n", key, buffer);
        }

        CodesetsFreeA(utf8, NULL);
      }
    }
    else
    {
      // write the unencoded key and value strings
      // these are separated by a single colon
      fprintf(fh, "%s: %s\n", key, buffer);
    }
  }
  else
  {
    // just write the end marker (a blank line)
    fprintf(fh, "\n");
  }

  LEAVE();
}

///
/// XMLStartHandler
// handle an XML start tag
static void XMLStartHandler(void *userData, const XML_Char *name, UNUSED const XML_Char **atts)
{
  struct XMLUserData *xmlUserData = userData;

  ENTER();

  if(strcmp(name, "newgroup") == 0)
  {
    xmlUserData->section = xs_Group;
    memset(&xmlUserData->entry, 0, sizeof(xmlUserData->entry));
  }
  else if(strcmp(name, "newcontact") == 0)
  {
    xmlUserData->section = xs_Contact;
    memset(&xmlUserData->entry, 0, sizeof(xmlUserData->entry));
  }
  else if(strcmp(name, "name") == 0)
  {
    switch(xmlUserData->section)
    {
      default:
      case xs_Group:
        // not yet supported
        xmlUserData->dataType = xd_Unknown;
      break;

      case xs_Contact:
        xmlUserData->dataType = xd_ContactName;
      break;
    }
  }
  else if(strcmp(name, "description") == 0)
  {
    xmlUserData->dataType = xd_Description;
  }
  else if(strcmp(name, "email") == 0)
  {
    xmlUserData->dataType = xd_Address;
  }
  else if(strcmp(name, "alias") == 0)
  {
    xmlUserData->dataType = xd_Alias;
  }
  else if(strcmp(name, "pgpid") == 0)
  {
    xmlUserData->dataType = xd_PGPId;
  }
  else if(strcmp(name, "homepage") == 0)
  {
    xmlUserData->dataType = xd_Homepage;
  }
  else if(strcmp(name, "portrait") == 0)
  {
    xmlUserData->dataType = xd_Portrait;
  }
  else if(strcmp(name, "birthday") == 0)
  {
    xmlUserData->dataType = xd_BirthDay;
  }
  else if(strcmp(name, "street") == 0)
  {
    xmlUserData->dataType = xd_Street;
  }
  else if(strcmp(name, "city") == 0)
  {
    xmlUserData->dataType = xd_City;
  }
  else if(strcmp(name, "zip") == 0)
  {
    xmlUserData->dataType = xd_ZIPCode;
  }
  else if(strcmp(name, "state") == 0)
  {
    xmlUserData->dataType = xd_State;
  }
  else if(strcmp(name, "country") == 0)
  {
    xmlUserData->dataType = xd_Country;
  }
  else if(strcmp(name, "phone") == 0 || strcmp(name, "mobil") == 0 || strcmp(name, "fax") == 0)
  {
    xmlUserData->dataType = xd_Phone;
  }
  else
  {
    xmlUserData->dataType = xd_Unknown;
  }

  xmlUserData->xmlData[0] = '\0';
  xmlUserData->xmlDataSize = 0;

  LEAVE();
}

///
/// XMLEndHandler
// handle an XML end tag
static void XMLEndHandler(void *userData, const XML_Char *name)
{
  struct XMLUserData *xmlUserData = userData;
  struct ABEntry *entry = &xmlUserData->entry;
  char *isoStr;

  ENTER();

  // add the terminating NUL character
  xmlUserData->xmlData[xmlUserData->xmlDataSize] = '\0';

  // now convert this prossible UTF8 string to a normal string
  if((isoStr = CodesetsUTF8ToStr(CSA_Source,          Trim(xmlUserData->xmlData),
                                 CSA_DestCodeset,     G->readCharset,
                                 CSA_MapForeignChars, C->MapForeignChars,
                                 TAG_DONE)) != NULL)
  {
    if(xmlUserData->section == xs_Group)
    {
    }
    else if(xmlUserData->section == xs_Contact)
    {
      // now fill the converted string into the entry
      // possible double entries (private, work, etc) will be merged
      switch(xmlUserData->dataType)
      {
        case xd_ContactName:
        {
          strlcpy(entry->RealName, isoStr, sizeof(entry->RealName));
        }
        break;

        case xd_Description:
        {
          strlcpy(entry->Comment, isoStr, sizeof(entry->Comment));
        }
        break;

        case xd_Address:
        {
          strlcpy(entry->Address, isoStr, sizeof(entry->Address));
        }
        break;

        case xd_Alias:
        {
          strlcpy(entry->Alias, isoStr, sizeof(entry->Alias));
        }
        break;

        case xd_PGPId:
        {
          strlcpy(entry->PGPId, isoStr, sizeof(entry->PGPId));
        }
        break;

        case xd_Homepage:
        {
          strlcpy(entry->Homepage, isoStr, sizeof(entry->Homepage));
        }
        break;

        case xd_Portrait:
        {
          strlcpy(entry->Photo, isoStr, sizeof(entry->Photo));
        }
        break;

        case xd_BirthDay:
        {
          LONG day = 0;
          LONG month = 0;
          LONG year = 0;
          char *p = isoStr;
          char *q;

          if((q = strchr(p, '/')) != NULL)
          {
            *q++ = '\0';
            month = atol(p);
            p = q;
          }
          if((q = strchr(p, '/')) != NULL)
          {
            *q++ = '\0';
            day = atol(p);
            p = q;
          }
          year = atol(p);

          entry->BirthDay = day * 1000000 + month * 10000 + year;
        }
        break;

        case xd_Street:
        {
          if(entry->Street[0] == '\0')
          {
            strlcpy(entry->Street, isoStr, sizeof(entry->Street));
          }
          else
          {
            snprintf(entry->Street, sizeof(entry->Street), "%s, %s", entry->Street, isoStr);
          }
        }
        break;

        case xd_City:
        {
          if(entry->City[0] == '\0')
          {
            strlcpy(entry->City, isoStr, sizeof(entry->City));
          }
          else
          {
            snprintf(entry->City, sizeof(entry->City), "%s, %s", entry->City, isoStr);
          }
        }
        break;

        case xd_ZIPCode:
        {
          if(entry->City[0] == '\0')
          {
            strlcpy(entry->City, isoStr, sizeof(entry->City));
          }
          else
          {
            char tmp[SIZE_DEFAULT];

            strlcpy(tmp, entry->City, sizeof(tmp));
            snprintf(entry->City, sizeof(entry->City), "%s %s", isoStr, tmp);
          }
        }
        break;

        case xd_State:
        case xd_Country:
        {
          if(entry->Country[0] == '\0')
          {
            strlcpy(entry->Country, isoStr, sizeof(entry->Country));
          }
          else
          {
            snprintf(entry->Country, sizeof(entry->Country), "%s, %s", entry->Country, isoStr);
          }
        }
        break;

        case xd_Phone:
        {
          if(entry->Phone[0] == '\0')
          {
            strlcpy(entry->Phone, isoStr, sizeof(entry->Phone));
          }
          else
          {
            snprintf(entry->Phone, sizeof(entry->Phone), "%s, %s", entry->Phone, isoStr);
          }
        }
        break;

        default:
        {
          // ignore this item
        }
        break;
      }
    }

    CodesetsFreeA(isoStr, NULL);
  }

  // add the entry to our address book if it was a normal contact information
  if(xmlUserData->section == xs_Contact && strcmp(name, "newcontact") == 0)
  {
    // we need at least an EMail address
    if(entry->Address[0] != '\0')
    {
      // set up an alias only if none is given
      if(entry->Alias[0] == '\0')
        EA_SetDefaultAlias(entry);

      // put it into the tree
      DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Insert, entry->Alias[0] ? entry->Alias : entry->RealName, entry, MUIV_NListtree_Insert_ListNode_Root, xmlUserData->sorted ?  MUIV_NListtree_Insert_PrevNode_Sorted : MUIV_NListtree_Insert_PrevNode_Tail, MUIF_NONE);
    }
  }

  LEAVE();
}

///
/// XMLCharacterDataHandler
// handle the XML character data
static void XMLCharacterDataHandler(void *userData, const XML_Char *s, int len)
{
  struct XMLUserData *xmlUserData = userData;

  ENTER();

  // does the string still fit in our buffer?
  if(xmlUserData->xmlDataSize < sizeof(xmlUserData->xmlData))
  {
    strncpy(&xmlUserData->xmlData[xmlUserData->xmlDataSize], s, MIN(sizeof(xmlUserData->xmlData) - xmlUserData->xmlDataSize, (size_t)len));
  }
  // add the size nevertheless
  xmlUserData->xmlDataSize += len;

  LEAVE();
}

///
/// AB_ImportTreeXML
// imports an address book in XML format (i.e. from SimpleMail)
BOOL AB_ImportTreeXML(char *fname, BOOL append, BOOL sorted)
{
  FILE *fh;
  BOOL result = FALSE;

  ENTER();

  if((fh = fopen(fname, "r")) != NULL)
  {
    XML_Parser parser;

    setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);

    G->AB->Modified = append;
    if(append == FALSE)
      DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Clear, NULL, 0);

    set(G->AB->GUI.LV_ADDRESSES, MUIA_NListtree_Quiet, TRUE);

    // create the XML parser
    if((parser = XML_ParserCreate(NULL)) != NULL)
    {
      struct XMLUserData xmlUserData;
      char *buffer = NULL;
      size_t size = 0;

      result = TRUE;

      xmlUserData.section = xs_Unknown;
      xmlUserData.dataType = xd_Unknown;
      xmlUserData.sorted = sorted;

      XML_SetElementHandler(parser, XMLStartHandler, XMLEndHandler);
      XML_SetCharacterDataHandler(parser, XMLCharacterDataHandler);
      XML_SetUserData(parser, &xmlUserData);

      // now parse the file line by line
      while(GetLine(&buffer, &size, fh) >= 0)
      {
        if(XML_Parse(parser, buffer, strlen(buffer), FALSE) == XML_STATUS_ERROR)
        {
          result = FALSE;
          break;
        }
      }
      if(result == TRUE)
      {
        // if everything went fine we need one final parsing step in case
        // there are some characters left in the parsing pipeline.
        if(XML_Parse(parser, "", 0, TRUE) == XML_STATUS_ERROR)
        {
          result = FALSE;
        }
      }

      if(buffer != NULL)
        free(buffer);

      // free the parser again
      XML_ParserFree(parser);
    }

    set(G->AB->GUI.LV_ADDRESSES, MUIA_NListtree_Quiet, FALSE);

    fclose(fh);
  }
  else
     ER_NewError(tr(MSG_ER_ADDRBOOKIMPORT), fname);

  RETURN(result);
  return result;
}

///
/// AB_ExportTreeNodeLDIF
//  Exports an address book as LDIF file
static STACKEXT void AB_ExportTreeNodeLDIF(FILE *fh, struct MUI_NListtree_TreeNode *list)
{
  int i;

  ENTER();

  for(i = 0; ; i++)
  {
    struct MUI_NListtree_TreeNode *tn;

    if((tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_GetEntry, list, i, MUIV_NListtree_GetEntry_Flag_SameLevel)) != NULL)
    {
      struct ABEntry *ab;

      ab = tn->tn_User;
      switch(ab->Type)
      {
        case AET_USER:
        {
          WriteLDIFLine(fh, "dn", "cn=%s,mail=%s", ab->RealName, ab->Address);
          WriteLDIFLine(fh, "objectClass", "top");
          WriteLDIFLine(fh, "objectClass", "person");
          WriteLDIFLine(fh, "objectClass", "organizationalPerson");
          WriteLDIFLine(fh, "objectClass", "inetOrdPerson");
          WriteLDIFLine(fh, "objectClass", "mozillaAbPersonAlpha");
          WriteLDIFLine(fh, "cn", "%s", ab->RealName);
          WriteLDIFLine(fh, "mail", "%s", ab->Address);
          if(ab->Alias[0] != '\0')
            WriteLDIFLine(fh, "mozillaNickname", "%s", ab->Alias);
          if(ab->Phone[0] != '\0')
            WriteLDIFLine(fh, "telephoneNumber", "%s", ab->Phone);
          if(ab->Street[0] != '\0')
            WriteLDIFLine(fh, "street", "%s", ab->Street);
          if(ab->City[0] != '\0')
            WriteLDIFLine(fh, "l", "%s", ab->City);
          if(ab->Country[0] != '\0')
            WriteLDIFLine(fh, "c", "%s", ab->Country);
          if(ab->Homepage[0] != '\0')
            WriteLDIFLine(fh, "mozillaHomeUrl", "%s", ab->Homepage);
          WriteLDIFLine(fh, "", "");
        }
        break;

        case AET_GROUP:
        {
          AB_ExportTreeNodeLDIF(fh, tn);
        }
        break;

        default:
          // lists cannot be exported
          break;
      }
    }
    else
      break;
  }

  LEAVE();
}

///
/// AB_ExportTreeLDIF
//  Exports an address book as LDIF file
BOOL AB_ExportTreeLDIF(char *fname)
{
  FILE *fh;
  BOOL result = FALSE;

  ENTER();

  if((fh = fopen(fname, "w")) != NULL)
  {
    setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);

    AB_ExportTreeNodeLDIF(fh, MUIV_NListtree_GetEntry_ListNode_Root);

    fclose(fh);
    result = TRUE;
  }
  else
    ER_NewError(tr(MSG_ER_ADDRBOOKEXPORT), fname);

  RETURN(result);
  return result;
}

///
/// AB_ImportTreeTabCSV
//  Imports an address book with comma or tab separated entries
BOOL AB_ImportTreeTabCSV(char *fname, BOOL append, BOOL sorted, char delim)
{
  FILE *fh;
  BOOL result = FALSE;

  ENTER();

  if((fh = fopen(fname, "r")) != NULL)
  {
    char *buffer = NULL;
    size_t size = 0;
    char delimStr[2];

    setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);

    G->AB->Modified = append;
    if(append == FALSE)
      DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Clear, NULL, 0);

    set(G->AB->GUI.LV_ADDRESSES, MUIA_NListtree_Quiet, TRUE);

    delimStr[0] = delim;
    delimStr[1] = '\0';

    while(GetLine(&buffer, &size, fh) >= 0)
    {
      struct ABEntry addr;
      char *item = buffer;
      int itemNumber = 0;

      memset(&addr, 0, sizeof(struct ABEntry));
      addr.Type = AET_USER;

      do
      {
        char *next;

        // first check if the current item begins with a quote
        if(item[0] == '"')
        {
          // now we have to search for the next quote and treat all
          // characters inbetween as one item
          if((next = strpbrk(&item[1], "\"")) != NULL)
          {
            // skip the leading quote
            item++;
            // erase the trailing quote
            *next++ = '\0';
            // and look for the next delimiter starting right after the just erased quote
            if((next = strpbrk(next, delimStr)) != NULL)
              *next++ = '\0';
          }
          else
          {
            // no closing quote found, abort this line
            item[0] = '\0';
            // to make sure this item doesn't make it into YAM's address book clear all values again
            memset(&addr, 0, sizeof(struct ABEntry));
          }
        }
        else
        {
          // do a normal search for the separating character
          if((next = strpbrk(item, delimStr)) != NULL)
            *next++ = '\0';
        }

        itemNumber++;

        // remove any nonsense like leading or trailing spaces
        item = Trim(item);

        if(item[0] != '\0')
        {
          // Thunderbird 1.5 exports 36 items, let's look which
          switch(itemNumber)
          {
            // first name
            case 1:
            {
              strlcat(addr.RealName, item, sizeof(addr.RealName));
            }
            break;

            // last name
            case 2:
            {
              if(addr.RealName[0] != '\0')
                strlcat(addr.RealName, " ", sizeof(addr.RealName));
              strlcat(addr.RealName, item, sizeof(addr.RealName));
            }
            break;

            // complete name, preferred, if available
            case 3:
            {
              strlcpy(addr.RealName, item, sizeof(addr.RealName));
            }
            break;

            // nickname
            case 4:
            {
              strlcpy(addr.Alias, item, sizeof(addr.Alias));
            }
            break;

            // EMail address
            case 5:
            {
              strlcpy(addr.Address, item, sizeof(addr.Address));
            }
            break;

            // second EMail address, ignored
            case 6:
              // nothing
            break;

            case 7:   // office phone number
            case 8:   // private phone number
            case 9:   // fax number
            case 10:  // pager number
            case 11:  // mobile phone
            {
              if(addr.Phone[0] != '\0')
                strlcat(addr.Phone, ", ", sizeof(addr.Phone));
              strlcat(addr.Phone, item, sizeof(addr.Phone));
            }
            break;

            case 12: // address, part 1
            case 13: // address, part 2
            {
              if(addr.Street[0] != '\0')
                strlcat(addr.Street, " ", sizeof(addr.Street));
              strlcat(addr.Street, item, sizeof(addr.Street));
            }
            break;

            // city
            case 14:
            {
              strlcpy(addr.City, item, sizeof(addr.City));
            }
            break;

            // province, ignored
            case 15:
              // nothing
            break;

            // ZIP code, append it to the city name
            case 16:
            {
              if(addr.City[0] != '\0')
                strlcat(addr.City, ", ", sizeof(addr.City));
              strlcat(addr.City, item, sizeof(addr.City));
            }
            break;

            // country
            case 17:
            {
              strlcpy(addr.Country, item, sizeof(addr.Country));
            }
            break;

            case 27: // office web address
            case 28: // private web address
            {
              strlcpy(addr.Homepage, item, sizeof(addr.Homepage));
            }
            break;

            default: // everything else is ignored
              break;
          }
        }

        item = next;
      }
      while(item != NULL);

      // we need at least an EMail address
      if(addr.Address[0] != '\0')
      {
        // set up an alias only if none is given
        if(addr.Alias[0] == '\0')
          EA_SetDefaultAlias(&addr);

        DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Insert, addr.Alias[0] ? addr.Alias : addr.RealName, &addr, MUIV_NListtree_Insert_ListNode_Root, sorted ?  MUIV_NListtree_Insert_PrevNode_Sorted : MUIV_NListtree_Insert_PrevNode_Tail, MUIF_NONE);
      }

      result = TRUE;
    }

    set(G->AB->GUI.LV_ADDRESSES, MUIA_NListtree_Quiet, FALSE);

    if(buffer != NULL)
      free(buffer);

    fclose(fh);
  }
  else
    ER_NewError(tr(MSG_ER_ADDRBOOKIMPORT), fname);

  RETURN(result);
  return result;
}

///
/// WriteTabCSVItem
// writes TAB or comma separated item to an address book file
static void WriteTabCSVItem(FILE *fh, const char *value, const char delim)
{
  ENTER();

  if(value != NULL)
  {
    char c;
    char *p = (char *)value;
    BOOL addQuotes = FALSE;

    // check if we need to add quotes to the item because it contains the separating character
    while((c = *p++) != '\0' && addQuotes == FALSE)
    {
      if(c == ',' || c == delim)
        addQuotes = TRUE;
    }

    if(addQuotes)
    {
      // surround the value by quotes and add the delimiter
      fprintf(fh, "\"%s\"%c", value, delim);
    }
    else
    {
      // just write the value and the delimiter
      fprintf(fh, "%s%c", value, delim);
    }
  }
  else
    // no value given, that means this was the final item in the line
    fprintf(fh, "\n");

  LEAVE();
}

///
/// AB_ExportTreeNodeTabCSV
//  Exports an address book with comma or tab separated entries
static STACKEXT void AB_ExportTreeNodeTabCSV(FILE *fh, struct MUI_NListtree_TreeNode *list, const char delim)
{
  int i;

  ENTER();

  for(i = 0; ; i++)
  {
    struct MUI_NListtree_TreeNode *tn;

    if((tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_GetEntry, list, i, MUIV_NListtree_GetEntry_Flag_SameLevel)) != NULL)
    {
      struct ABEntry *ab;

      ab = tn->tn_User;
      switch(ab->Type)
      {
        case AET_USER:
        {
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, ab->RealName, delim);
          WriteTabCSVItem(fh, ab->Alias, delim);
          WriteTabCSVItem(fh, ab->Address, delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, ab->Phone, delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, ab->Street, delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, ab->City, delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, "", delim); // postal code
          WriteTabCSVItem(fh, ab->Country, delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, ab->Homepage, delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, "", delim);
          WriteTabCSVItem(fh, NULL, delim);
        }
        break;

        case AET_GROUP:
        {
          AB_ExportTreeNodeTabCSV(fh, tn, delim);
        }
        break;

        default:
          // lists cannot be exported
          break;
      }
    }
    else
      break;
  }

  LEAVE();
}

///
/// AB_ExportTreeTabCSV
//  Exports an address book with comma or tab separated entries
BOOL AB_ExportTreeTabCSV(char *fname, char delim)
{
  FILE *fh;
  BOOL result = FALSE;

  ENTER();

  if((fh = fopen(fname, "w")) != NULL)
  {
    setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);

    AB_ExportTreeNodeTabCSV(fh, MUIV_NListtree_GetEntry_ListNode_Root, delim);

    fclose(fh);
    result = TRUE;
  }
  else
    ER_NewError(tr(MSG_ER_ADDRBOOKEXPORT), fname);

  RETURN(result);
  return result;
}

///
/// AB_EditFunc
/*** AB_EditFunc - Modifies selected address book entry ***/
HOOKPROTONHNONP(AB_EditFunc, void)
{
  struct MUI_NListtree_TreeNode *tn;

  ENTER();

  if((tn = (struct MUI_NListtree_TreeNode *)xget(G->AB->GUI.LV_ADDRESSES, MUIA_NListtree_Active)) != NULL)
  {
    struct ABEntry *ab = (struct ABEntry *)(tn->tn_User);
    int winnum = EA_Init(ab->Type, ab);

    if(winnum >= 0)
      EA_Setup(winnum, ab);
  }

  LEAVE();
}
MakeHook(AB_EditHook, AB_EditFunc);

///
/// AB_ActiveChange
// a hook called whenever the active entry in the address book listtree is changed
HOOKPROTONHNONP(AB_ActiveChange, void)
{
  struct AB_GUIData *gui = &G->AB->GUI;
  struct MUI_NListtree_TreeNode *active;
  BOOL disabled;

  ENTER();

  if((active = (struct MUI_NListtree_TreeNode *)xget(gui->LV_ADDRESSES, MUIA_NListtree_Active)) != NULL)
  {
    // disable the buttons for groups only
    struct ABEntry *addr = (struct ABEntry *)active->tn_User;

    disabled = (addr->Type == AET_GROUP);
  }
  else
  {
    // no active entry
    disabled = TRUE;
  }

  DoMethod(G->App, MUIM_MultiSet, MUIA_Disabled, disabled, gui->BT_TO,
                                                           gui->BT_CC,
                                                           gui->BT_BCC,
                                                           NULL);
  DoMethod(gui->TB_TOOLBAR, MUIM_AddrBookToolbar_UpdateControls);

  LEAVE();
}
MakeStaticHook(AB_ActiveChangeHook, AB_ActiveChange);

///
/// AB_DoubleClick
/*** AB_DoubleClick - User double-clicked in the address book ***/
HOOKPROTONHNONP(AB_DoubleClick, void)
{
  ENTER();

  if(G->AB->winNumber != -1)
  {
    DoMethod(G->App, MUIM_CallHook, &AB_FromAddrBookHook, G->AB->Mode);
    set(G->AB->GUI.WI, MUIA_Window_CloseRequest, TRUE);
  }
  else
  {
    if(G->AB->Mode == ABM_CONFIG &&
       G->AB->parentStringGadget != NULL)
    {
      struct MUI_NListtree_TreeNode *active;

      if((active = (struct MUI_NListtree_TreeNode *)xget(G->AB->GUI.LV_ADDRESSES, MUIA_NListtree_Active)) != NULL)
      {
        struct ABEntry *addr = (struct ABEntry *)(active->tn_User);

        DoMethod(G->AB->parentStringGadget, MUIM_Recipientstring_AddRecipient, addr->Alias ? addr->Alias : addr->RealName);
      }

      set(G->AB->GUI.WI, MUIA_Window_CloseRequest, TRUE);
    }
    else
      AB_EditFunc();
  }

  LEAVE();
}
MakeStaticHook(AB_DoubleClickHook, AB_DoubleClick);

///
/// AB_Sort
/*** AB_Sort - Sorts the address book ***/
HOOKPROTONHNO(AB_Sort, void, int *arg)
{
  char fname[SIZE_PATHFILE];

  ENTER();

  AddPath(fname, C->TempDir, ".addressbook.tmp", sizeof(fname));

  if(AB_SaveTree(fname) == TRUE)
  {
    G->AB->SortBy = *arg;
    AB_LoadTree(fname, FALSE, TRUE);
    DeleteFile(fname);
    G->AB->Modified = TRUE;
  }

  LEAVE();
}
MakeStaticHook(AB_SortHook, AB_Sort);

///
/// AB_NewABookFunc
/*** AB_NewABookFunc - Clears entire address book ***/
HOOKPROTONHNONP(AB_NewABookFunc, void)
{
  ENTER();

  DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Remove, MUIV_NListtree_Remove_ListNode_Root, MUIV_NListtree_Remove_TreeNode_All, MUIF_NONE);
  G->AB->Modified = FALSE;

  LEAVE();
}
MakeStaticHook(AB_NewABookHook, AB_NewABookFunc);

///
/// AB_OpenABookFunc
/*** AB_OpenABookFunc - Loads selected address book ***/
HOOKPROTONHNONP(AB_OpenABookFunc, void)
{
  struct FileReqCache *frc;

  ENTER();

  if((frc = ReqFile(ASL_ABOOK,G->AB->GUI.WI, tr(MSG_Open), REQF_NONE, G->MA_MailDir, "")) != NULL)
  {
    AddPath(G->AB_Filename, frc->drawer, frc->file, sizeof(G->AB_Filename));
    AB_LoadTree(G->AB_Filename, FALSE, FALSE);
  }

  LEAVE();
}
MakeStaticHook(AB_OpenABookHook, AB_OpenABookFunc);

///
/// AB_AppendABookFunc
/*** AB_AppendABookFunc - Appends selected address book ***/
HOOKPROTONHNONP(AB_AppendABookFunc, void)
{
  struct FileReqCache *frc;

  ENTER();

  if((frc = ReqFile(ASL_ABOOK,G->AB->GUI.WI, tr(MSG_Append), REQF_NONE, G->MA_MailDir, "")) != NULL)
  {
    char aname[SIZE_PATHFILE];

    AddPath(aname, frc->drawer, frc->file, sizeof(aname));
    AB_LoadTree(aname, TRUE, FALSE);
  }

  LEAVE();
}
MakeStaticHook(AB_AppendABookHook, AB_AppendABookFunc);

///
/// AB_ImportLDIFABookFunc
// imports an LDIF address book
HOOKPROTONHNONP(AB_ImportLDIFABookFunc, void)
{
  struct FileReqCache *frc;

  ENTER();

  if((frc = ReqFile(ASL_ABOOK_LDIF, G->AB->GUI.WI, tr(MSG_AB_IMPORT), REQF_NONE, G->MA_MailDir, "")) != NULL)
  {
    char ldifname[SIZE_PATHFILE];

    AddPath(ldifname, frc->drawer, frc->file, sizeof(ldifname));
    AB_ImportTreeLDIF(ldifname, TRUE, FALSE);
  }

  LEAVE();
}
MakeStaticHook(AB_ImportLDIFABookHook, AB_ImportLDIFABookFunc);

///
/// AB_ExportLDIFABookFunc
// exports an LDIF address book
HOOKPROTONHNONP(AB_ExportLDIFABookFunc, void)
{
  struct FileReqCache *frc;

  ENTER();

  if((frc = ReqFile(ASL_ABOOK_LDIF, G->AB->GUI.WI, tr(MSG_AB_EXPORT), REQF_SAVEMODE, G->MA_MailDir, "")) != NULL)
  {
    char ldifname[SIZE_PATHFILE];

    AddPath(ldifname, frc->drawer, frc->file, sizeof(ldifname));

    if(FileExists(ldifname) == FALSE ||
       MUI_Request(G->App, G->AB->GUI.WI, 0, tr(MSG_MA_ConfirmReq), tr(MSG_YesNoReq), tr(MSG_FILE_OVERWRITE), frc->file) != 0)
    {
      AB_ExportTreeLDIF(ldifname);
    }
  }

  LEAVE();
}
MakeStaticHook(AB_ExportLDIFABookHook, AB_ExportLDIFABookFunc);

///
/// AB_ImportTabCSVABookFunc
// imports a comma or TAB separated address book
HOOKPROTONHNO(AB_ImportTabCSVABookFunc, void, int *arg)
{
  char delim = (char)arg[0];
  int type;
  struct FileReqCache *frc;

  ENTER();

  switch(delim)
  {
    case '\t': type = ASL_ABOOK_TAB; break;
    case ',':  type = ASL_ABOOK_CSV; break;
    default:   type = ASL_ABOOK;     break;
  }

  if((frc = ReqFile(type, G->AB->GUI.WI, tr(MSG_AB_IMPORT), REQF_NONE, G->MA_MailDir, "")) != NULL)
  {
    char aname[SIZE_PATHFILE];

    AddPath(aname, frc->drawer, frc->file, sizeof(aname));
    AB_ImportTreeTabCSV(aname, TRUE, FALSE, delim);
  }

  LEAVE();
}
MakeStaticHook(AB_ImportTabCSVABookHook, AB_ImportTabCSVABookFunc);

///
/// AB_ExportTabCSVABookFunc
// exports a comma or TAB separated address book
HOOKPROTONHNO(AB_ExportTabCSVABookFunc, void, int *arg)
{
  char delim = (char)arg[0];
  int type;
  struct FileReqCache *frc;

  ENTER();

  switch(delim)
  {
    case '\t': type = ASL_ABOOK_TAB; break;
    case ',':  type = ASL_ABOOK_CSV; break;
    default:   type = ASL_ABOOK;     break;
  }

  if((frc = ReqFile(type, G->AB->GUI.WI, tr(MSG_AB_EXPORT), REQF_SAVEMODE, G->MA_MailDir, "")) != NULL)
  {
    char aname[SIZE_PATHFILE];

    AddPath(aname, frc->drawer, frc->file, sizeof(aname));

    if(FileExists(aname) == FALSE ||
       MUI_Request(G->App, G->AB->GUI.WI, 0, tr(MSG_MA_ConfirmReq), tr(MSG_YesNoReq), tr(MSG_FILE_OVERWRITE), frc->file) != 0)
    {
      AB_ExportTreeTabCSV(aname, delim);
    }
  }

  LEAVE();
}

MakeStaticHook(AB_ExportTabCSVABookHook, AB_ExportTabCSVABookFunc);

///
/// AB_ImportXMLABookFunc
// imports an XML address book
HOOKPROTONHNONP(AB_ImportXMLABookFunc, void)
{
  struct FileReqCache *frc;

  ENTER();

  if((frc = ReqFile(ASL_ABOOK_XML, G->AB->GUI.WI, tr(MSG_AB_IMPORT), REQF_NONE, G->MA_MailDir, "")) != NULL)
  {
    char xmlname[SIZE_PATHFILE];

    AddPath(xmlname, frc->drawer, frc->file, sizeof(xmlname));
    AB_ImportTreeXML(xmlname, TRUE, FALSE);
  }

  LEAVE();
}
MakeStaticHook(AB_ImportXMLABookHook, AB_ImportXMLABookFunc);

///
/// AB_SaveABookFunc
/*** AB_SaveABookFunc - Saves address book using the default name ***/
HOOKPROTONHNONP(AB_SaveABookFunc, void)
{
  ENTER();

  Busy(tr(MSG_BusySavingAB), G->AB_Filename, 0, 0);
  AB_SaveTree(G->AB_Filename);
  G->AB->Modified = FALSE;
  BusyEnd();

  LEAVE();
}
MakeHook(AB_SaveABookHook, AB_SaveABookFunc);

///
/// AB_SaveABookAsFunc
/*** AB_SaveABookAsFunc - Saves address book under a different name ***/
HOOKPROTONHNONP(AB_SaveABookAsFunc, void)
{
  struct FileReqCache *frc;

  ENTER();

  if((frc = ReqFile(ASL_ABOOK, G->AB->GUI.WI, tr(MSG_SaveAs), REQF_SAVEMODE, G->MA_MailDir, "")) != NULL)
  {
    AddPath(G->AB_Filename, frc->drawer, frc->file, sizeof(G->AB_Filename));

    if(FileExists(G->AB_Filename) == FALSE ||
       MUI_Request(G->App, G->AB->GUI.WI, 0, tr(MSG_MA_ConfirmReq), tr(MSG_YesNoReq), tr(MSG_FILE_OVERWRITE), frc->file) != 0)
    {
      AB_SaveABookFunc();
    }
  }

  LEAVE();
}
MakeStaticHook(AB_SaveABookAsHook, AB_SaveABookAsFunc);

///
/// AB_PrintField
//  Formats and prints a single field
static void AB_PrintField(FILE *prt, const char *fieldname, const char *field)
{
  ENTER();

  if(*field != '\0')
    fprintf(prt, "%-20.20s: %-50.50s\n", StripUnderscore(fieldname), field);

  LEAVE();
}

///
/// AB_PrintShortEntry
//  Prints an address book entry in compact format
static void AB_PrintShortEntry(FILE *prt, struct ABEntry *ab)
{
  static const char types[3] = { 'P','L','G' };

  ENTER();

  fprintf(prt, "%c %-12.12s %-20.20s %-36.36s\n", types[ab->Type-AET_USER],
     ab->Alias, ab->RealName, ab->Type == AET_USER ? ab->Address : ab->Comment);

  LEAVE();
}

///
/// AB_PrintLongEntry
//  Prints an address book entry in detailed format
static void AB_PrintLongEntry(FILE *prt, struct ABEntry *ab)
{
  ENTER();

  fputs("------------------------------------------------------------------------\n", prt);
  switch(ab->Type)
  {
    case AET_USER:
      AB_PrintField(prt, tr(MSG_AB_PersonAlias), ab->Alias);
      AB_PrintField(prt, tr(MSG_EA_RealName), ab->RealName);
      AB_PrintField(prt, tr(MSG_EA_EmailAddress), ab->Address);
      AB_PrintField(prt, tr(MSG_EA_PGPId), ab->PGPId);
      AB_PrintField(prt, tr(MSG_EA_Homepage), ab->Homepage);
      AB_PrintField(prt, tr(MSG_EA_Street), ab->Street);
      AB_PrintField(prt, tr(MSG_EA_City), ab->City);
      AB_PrintField(prt, tr(MSG_EA_Country), ab->Country);
      AB_PrintField(prt, tr(MSG_EA_Phone), ab->Phone);
      AB_PrintField(prt, tr(MSG_EA_DOB), AB_ExpandBD(ab->BirthDay));
      break;
    case AET_LIST:
      AB_PrintField(prt, tr(MSG_AB_ListAlias), ab->Alias);
      AB_PrintField(prt, tr(MSG_EA_MLName), ab->RealName);
      AB_PrintField(prt, tr(MSG_EA_ReturnAddress), ab->Address);
      if(ab->Members != NULL)
      {
        BOOL header = FALSE;
        char *ptr;

        for(ptr = ab->Members; *ptr; ptr++)
        {
          char *nptr = strchr(ptr, '\n');

          if(nptr != NULL)
            *nptr = 0;
          else
            break;
          if(!header)
          {
            AB_PrintField(prt, tr(MSG_EA_Members), ptr);
            header = TRUE;
          }
          else
            fprintf(prt, "                      %s\n", ptr);
          *nptr = '\n';
          ptr = nptr;
        }
      }
      break;
    case AET_GROUP:
      AB_PrintField(prt, tr(MSG_AB_GroupAlias), ab->Alias);
  }
  AB_PrintField(prt, tr(MSG_EA_Description), ab->Comment);

  LEAVE();
}

///
/// AB_PrintLevel (rec)
//  Recursively prints an address book node
static STACKEXT void AB_PrintLevel(struct MUI_NListtree_TreeNode *list, FILE *prt, int mode)
{
  struct MUI_NListtree_TreeNode *tn;
  int i;

  ENTER();

  for(i = 0; ; i++)
  {
    if((tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_GetEntry, list, i, MUIV_NListtree_GetEntry_Flag_SameLevel)) != NULL)
    {
      struct ABEntry *ab = tn->tn_User;

      if(mode == 1)
        AB_PrintLongEntry(prt, ab); else AB_PrintShortEntry(prt, ab);
      if(ab->Type == AET_GROUP)
         AB_PrintLevel(tn, prt, mode);
    }
    else
      break;
  }

  LEAVE();
}

///
/// AB_PrintABookFunc
/*** AB_PrintABookFunc - Prints the entire address book in compact or detailed format ***/
HOOKPROTONHNONP(AB_PrintABookFunc, void)
{
  int mode;

  ENTER();

  mode = MUI_Request(G->App, G->AB->GUI.WI, 0, tr(MSG_Print), tr(MSG_AB_PrintReqGads), tr(MSG_AB_PrintReq));
  if(mode != 0)
  {
    if(CheckPrinter())
    {
      BOOL success = FALSE;
      FILE *prt;

      if((prt = fopen("PRT:", "w")) != NULL)
      {
        setvbuf(prt, NULL, _IOFBF, SIZE_FILEBUF);

        Busy(tr(MSG_BusyPrintingAB), "", 0, 0);
        fprintf(prt, "%s\n", G->AB_Filename);

        if(mode == 2)
        {
          fprintf(prt, "\n  %-12.12s %-20.20s %s/%s\n", tr(MSG_AB_AliasFld), tr(MSG_EA_RealName), tr(MSG_EA_EmailAddress), tr(MSG_EA_Description));
          fputs("------------------------------------------------------------------------\n", prt);
        }
        AB_PrintLevel(MUIV_NListtree_GetEntry_ListNode_Root, prt, mode);
        BusyEnd();

        // before we close the file
        // handle we check the error state
        if(ferror(prt) == 0)
          success = TRUE;

        fclose(prt);
      }

      // signal the failure to the user
      // in case we were not able to print something
      if(success == FALSE)
        MUI_Request(G->App, NULL, 0, tr(MSG_ErrorReq), tr(MSG_OkayReq), tr(MSG_ER_PRINTER_FAILED));
    }
  }

  LEAVE();
}
MakeStaticHook(AB_PrintABookHook, AB_PrintABookFunc);

///
/// AB_PrintFunc
/*** AB_PrintFunc - Prints selected address book entry in detailed format ***/
HOOKPROTONHNONP(AB_PrintFunc, void)
{
  struct MUI_NListtree_TreeNode *tn;

  ENTER();

  if((tn = (struct MUI_NListtree_TreeNode *)xget(G->AB->GUI.LV_ADDRESSES, MUIA_NListtree_Active)) != NULL)
  {
    if(CheckPrinter())
    {
      BOOL success = FALSE;
      FILE *prt;

      if((prt = fopen("PRT:", "w")) != NULL)
      {
         struct ABEntry *ab = (struct ABEntry *)(tn->tn_User);

         setvbuf(prt, NULL, _IOFBF, SIZE_FILEBUF);

         set(G->App, MUIA_Application_Sleep, TRUE);

         AB_PrintLongEntry(prt, ab);
         if(ab->Type == AET_GROUP)
           AB_PrintLevel(tn, prt, 1);

        // before we close the file
        // handle we check the error state
        if(ferror(prt) == 0)
          success = TRUE;

         fclose(prt);
         set(G->App, MUIA_Application_Sleep, FALSE);
      }

      // signal the failure to the user
      // in case we were not able to print something
      if(success == FALSE)
        MUI_Request(G->App, NULL, 0, tr(MSG_ErrorReq), tr(MSG_OkayReq), tr(MSG_ER_PRINTER_FAILED));
    }
  }

  LEAVE();
}
MakeHook(AB_PrintHook, AB_PrintFunc);

///
/// AB_AddEntryFunc
/*** AB_AddEntryFunc - Add a new entry to the address book ***/
HOOKPROTONHNO(AB_AddEntryFunc, void, int *arg)
{
  ENTER();

  EA_Init(*arg, NULL);

  LEAVE();
}
MakeHook(AB_AddEntryHook, AB_AddEntryFunc);

///
/// AB_DeleteFunc
/*** AB_DeleteFunc - Deletes selected address book entry ***/
HOOKPROTONHNONP(AB_DeleteFunc, void)
{
  ENTER();

  DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Remove, NULL, MUIV_NListtree_Remove_TreeNode_Active, MUIF_NONE);
  G->AB->Modified = TRUE;

  LEAVE();
}
MakeHook(AB_DeleteHook, AB_DeleteFunc);

///
/// AB_DuplicateFunc
/*** AB_DuplicateFunc - Duplicates selected address book entry ***/
HOOKPROTONHNONP(AB_DuplicateFunc, void)
{
  struct MUI_NListtree_TreeNode *tn;

  ENTER();

  if((tn = (struct MUI_NListtree_TreeNode *)xget(G->AB->GUI.LV_ADDRESSES, MUIA_NListtree_Active)) != NULL)
  {
    struct ABEntry *ab = (struct ABEntry *)(tn->tn_User);
    int winnum = EA_Init(ab->Type, NULL);

    if(winnum >= 0)
    {
      char buf[SIZE_NAME];
      int len;

      EA_Setup(winnum, ab);
      strlcpy(buf, ab->Alias, sizeof(buf));
      if((len = strlen(buf)) > 0)
      {
        if(isdigit(buf[len - 1]))
          buf[len - 1]++;
        else if(len < SIZE_NAME - 1)
          strlcat(buf, "2", sizeof(buf));
        else
          buf[len - 1] = '2';

        setstring(G->EA[winnum]->GUI.ST_ALIAS, buf);
      }
    }
  }

  LEAVE();
}
MakeStaticHook(AB_DuplicateHook, AB_DuplicateFunc);

///
/// AB_FindEntry
// Searches an address book node for a given pattern
int AB_FindEntry(char *pattern, enum AddressbookFind mode, char **result)
{
  Object *lv = G->AB->GUI.LV_ADDRESSES;
  int res = 0;
  int i;

  ENTER();

  D(DBF_ALWAYS, "searching for pattern '%s' in abook, mode=%ld", pattern, mode);

  for(i = 0; ; i++)
  {
    struct MUI_NListtree_TreeNode *tn;

    if((tn = (struct MUI_NListtree_TreeNode *)DoMethod(lv, MUIM_NListtree_GetEntry, MUIV_NListtree_GetEntry_ListNode_Root, i, MUIF_NONE)) != NULL)
    {
      struct ABEntry *ab = tn->tn_User;

      if(ab->Type == AET_GROUP)
        continue;
      else
      {
        BOOL found = FALSE;
        int winnum;

        switch(mode)
        {
          case ABF_RX_NAME:
            found = MatchNoCase(ab->RealName, pattern);
          break;

          case ABF_RX_EMAIL:
            found = MatchNoCase(ab->Address, pattern);
          break;

          case ABF_RX_NAMEEMAIL:
            found = MatchNoCase(ab->RealName, pattern) || MatchNoCase(ab->Address, pattern);
          break;

          default:
          {
            if((found = MatchNoCase(ab->Alias, pattern) || MatchNoCase(ab->Comment, pattern)) == FALSE)
            {
              if((found = MatchNoCase(ab->RealName, pattern) || MatchNoCase(ab->Address, pattern)) == FALSE && ab->Type == AET_USER)
              {
                found = MatchNoCase(ab->Homepage, pattern) ||
                        MatchNoCase(ab->Street, pattern)   ||
                        MatchNoCase(ab->City, pattern)     ||
                        MatchNoCase(ab->Country, pattern)  ||
                        MatchNoCase(ab->Phone, pattern);
              }
            }
          }
        }

        if(found == TRUE)
        {
          D(DBF_ALWAYS, "found pattern '%s' in entry with address '%s'", pattern, ab->Address);

          res++;

          if(mode == ABF_USER)
          {
            char buf[SIZE_LARGE];

            DoMethod(lv, MUIM_NListtree_Open, MUIV_NListtree_Open_ListNode_Parent, tn, MUIF_NONE);
            set(lv, MUIA_NListtree_Active, tn);

            snprintf(buf, sizeof(buf), tr(MSG_AB_FoundEntry), ab->Alias, ab->RealName);

            switch(MUI_Request(G->App, G->AB->GUI.WI, 0, tr(MSG_AB_FindEntry), tr(MSG_AB_FoundEntryGads), buf))
            {
              case 1:
                // nothing
              break;

              case 2:
              {
                if((winnum = EA_Init(ab->Type, ab)) >= 0)
                  EA_Setup(winnum, ab);
              }
              // continue

              case 0:
              {
                RETURN(-1);
                return -1;
              }
            }
          }
          else if(result != NULL)
            *result++ = ab->Alias;
        }
      }
    }
    else
      break;
  }

  RETURN(res);
  return res;
}

///
/// AB_FindFunc
/*** AB_FindFunc - Searches address book ***/
HOOKPROTONHNONP(AB_FindFunc, void)
{
  static char pattern[SIZE_PATTERN+1] = "";

  ENTER();

  if(StringRequest(pattern, SIZE_PATTERN, tr(MSG_AB_FindEntry), tr(MSG_AB_FindEntryReq), tr(MSG_AB_StartSearch), NULL, tr(MSG_Cancel), FALSE, G->AB->GUI.WI))
  {
    char searchPattern[SIZE_PATTERN+5];

    snprintf(searchPattern, sizeof(searchPattern), "#?%s#?", pattern);

    if(AB_FindEntry(searchPattern, ABF_USER, NULL) == 0)
      MUI_Request(G->App, G->AB->GUI.WI, 0, tr(MSG_AB_FindEntry), tr(MSG_OkayReq), tr(MSG_AB_NoneFound));
  }

  LEAVE();
}
MakeHook(AB_FindHook, AB_FindFunc);

///
/// AB_FoldUnfoldFunc
HOOKPROTONHNO(AB_FoldUnfoldFunc, void, int *arg)
{
  ENTER();

  if(G->AB->GUI.LV_ADDRESSES)
  {
    if(*arg <= 0)
      DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Open, MUIV_NListtree_Open_ListNode_Root, MUIV_NListtree_Open_TreeNode_All, MUIF_NONE);
    else
      DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Close, MUIV_NListtree_Close_ListNode_Root, MUIV_NListtree_Close_TreeNode_All, MUIF_NONE);
  }

  LEAVE();
}
MakeHook(AB_FoldUnfoldHook, AB_FoldUnfoldFunc);

///
/// AB_OpenFunc
/*** AB_OpenFunc - Open address book window ***/
HOOKPROTONHNO(AB_OpenFunc, void, LONG *arg)
{
  struct AB_ClassData *ab = G->AB;
  const char *md = "";

  ENTER();

  switch((ab->Mode = arg[0]))
  {
    case ABM_TO:      md = "(To)";      break;
    case ABM_CC:      md = "(CC)";      break;
    case ABM_BCC:     md = "(BCC)";     break;
    case ABM_FROM:    md = "(From)";    break;
    case ABM_REPLYTO: md = "(Reply-To)";break;
    case ABM_CONFIG:  ab->parentStringGadget = (Object *)arg[1]; break;
    default:
      // nothing
    break;
  }

  ab->winNumber = (*md != '\0' ? arg[1] : -1);
  ab->Modified = FALSE;

  // disable the To/CC/BCC buttons regardless of the config mode, because we start
  // with no active tree entry
  DoMethod(G->App, MUIM_MultiSet, MUIA_Disabled, TRUE, ab->GUI.BT_TO,
                                                       ab->GUI.BT_CC,
                                                       ab->GUI.BT_BCC,
                                                       NULL);
  DoMethod(ab->GUI.TB_TOOLBAR, MUIM_AddrBookToolbar_UpdateControls);


  snprintf(ab->WTitle, sizeof(ab->WTitle), "%s %s", tr(MSG_MA_MAddrBook), md);
  set(ab->GUI.WI, MUIA_Window_Title, ab->WTitle);

  SafeOpenWindow(ab->GUI.WI);

  LEAVE();
}
MakeHook(AB_OpenHook, AB_OpenFunc);

///
/// AB_Close
/*** AB_Close - Closes address book window ***/
HOOKPROTONHNONP(AB_Close, void)
{
  BOOL closeWin = TRUE;

  ENTER();

  if(G->AB->Modified)
  {
    switch(MUI_Request(G->App, G->AB->GUI.WI, 0, NULL, tr(MSG_AB_ModifiedGads), tr(MSG_AB_Modified)))
    {
      case 0: closeWin = FALSE; break;
      case 1: AB_SaveABookFunc(); break;
      case 2: break;
      case 3: AB_LoadTree(G->AB_Filename, FALSE, FALSE); break;
    }
  }

  if(closeWin)
    set(G->AB->GUI.WI, MUIA_Window_Open, FALSE);

  LEAVE();
}
MakeStaticHook(AB_CloseHook, AB_Close);

///
/// AB_LV_ConFunc
/*** AB_LV_ConFunc - Address book listview construction hook ***/
HOOKPROTONHNO(AB_LV_ConFunc, struct ABEntry *, struct MUIP_NListtree_ConstructMessage *msg)
{
  struct ABEntry *addr = (struct ABEntry *)msg->UserData;
  struct ABEntry *entry;

  ENTER();

  if((entry = memdup(addr, sizeof(*addr))) != NULL)
  {
    // clone the member list aswell
    if(addr->Members != NULL)
    {
      if((entry->Members = strdup(addr->Members)) == NULL)
      {
        // if strdup() failed then we let the whole function fail
        free(entry);
        entry = NULL;
      }
    }
  }

  RETURN(entry);
  return entry;
}
MakeStaticHook(AB_LV_ConFuncHook, AB_LV_ConFunc);

///
/// AB_LV_DesFunc
/*** AB_LV_DesFunc - Address book listview destruction hook ***/
HOOKPROTONHNO(AB_LV_DesFunc, long, struct MUIP_NListtree_DestructMessage *msg)
{
  struct ABEntry *entry;

  ENTER();

  if(msg != NULL)
  {
    entry = (struct ABEntry *)msg->UserData;

    if(entry != NULL)
    {
      if(entry->Members)
        free(entry->Members);
      free(entry);
    }
  }

  RETURN(0);
  return 0;
}
MakeStaticHook(AB_LV_DesFuncHook, AB_LV_DesFunc);

///
/// AB_LV_DspFunc
/*** AB_LV_DspFunc - Address book listview display hook ***/
HOOKPROTONHNO(AB_LV_DspFunc, long, struct MUIP_NListtree_DisplayMessage *msg)
{
  ENTER();

  if(msg != NULL)
  {
    if(msg->TreeNode)
    {
      struct ABEntry *entry = msg->TreeNode->tn_User;

      if(entry != NULL)
      {
        msg->Array[0] = entry->Alias;
        msg->Array[1] = entry->RealName;
        msg->Array[2] = entry->Comment;
        msg->Array[3] = entry->Address;
        msg->Array[4] = entry->Street;
        msg->Array[5] = entry->City;
        msg->Array[6] = entry->Country;
        msg->Array[7] = entry->Phone;
        msg->Array[8] = AB_ExpandBD(entry->BirthDay);
        msg->Array[9] = entry->PGPId;
        msg->Array[10]= entry->Homepage;

        switch(entry->Type)
        {
           case AET_LIST:
           {
             static char dispal[SIZE_DEFAULT];
             snprintf(msg->Array[0] = dispal, sizeof(dispal), "\033o[0]%s", entry->Alias);
           }
           break;

           case AET_GROUP:
           {
             msg->Preparse[0] = (char *)MUIX_B;
             msg->Preparse[2] = (char *)MUIX_B;
           }
           break;

           default:
             // nothing
           break;
        }
      }
    }
    else
    {
      msg->Array[0] = (STRPTR)tr(MSG_AB_TitleAlias);
      msg->Array[1] = (STRPTR)tr(MSG_AB_TitleName);
      msg->Array[2] = (STRPTR)tr(MSG_AB_TitleDescription);
      msg->Array[3] = (STRPTR)tr(MSG_AB_TitleAddress);
      msg->Array[4] = (STRPTR)tr(MSG_AB_TitleStreet);
      msg->Array[5] = (STRPTR)tr(MSG_AB_TitleCity);
      msg->Array[6] = (STRPTR)tr(MSG_AB_TitleCountry);
      msg->Array[7] = (STRPTR)tr(MSG_AB_TitlePhone);
      msg->Array[8] = (STRPTR)tr(MSG_AB_TitleBirthDate);
      msg->Array[9] = (STRPTR)tr(MSG_AB_TitlePGPId);
      msg->Array[10]= (STRPTR)tr(MSG_AB_TitleHomepage);
    }
  }

  RETURN(0);
  return 0;
}
MakeHook(AB_LV_DspFuncHook, AB_LV_DspFunc);

///
/// AB_LV_CmpFunc
/*** AB_LV_CmpFunc - Address book listview compare hook ***/
HOOKPROTONHNO(AB_LV_CmpFunc, long, struct MUIP_NListtree_CompareMessage *msg)
{
  struct MUI_NListtree_TreeNode *entry1, *entry2;
  struct ABEntry *ab1, *ab2;
  char *n1, *n2;
  int cmp;

  if(msg == NULL)
    return 0;

  // now we get the entries
  entry1 = msg->TreeNode1;
  entry2 = msg->TreeNode2;
  if(!entry1 || !entry2)
    return 0;

  ab1 = (struct ABEntry *)entry1->tn_User;
  ab2 = (struct ABEntry *)entry2->tn_User;
  if(!ab1 || !ab2)
    return 0;

  switch(G->AB->SortBy)
  {
     case 1:
       if(!(n1 = strrchr(ab1->RealName,' '))) n1 = ab1->RealName;
       if(!(n2 = strrchr(ab2->RealName,' '))) n2 = ab2->RealName;

       if((cmp = Stricmp(n1, n2)))
         return cmp;
     break;

     case 2:
       if((cmp = Stricmp(ab1->RealName, ab2->RealName)))
         return cmp;
     break;

     case 3:
       if((cmp = Stricmp(ab1->Comment, ab2->Comment)))
         return cmp;
     break;

     case 4:
       if((cmp = Stricmp(ab1->Address, ab2->Address)))
         return cmp;
     break;
  }

  return Stricmp(ab1->Alias, ab2->Alias);
}
MakeStaticHook(AB_LV_CmpFuncHook, AB_LV_CmpFunc);

///
/// AB_MakeABFormat
//  Creates format definition for address book listview
void AB_MakeABFormat(APTR lv)
{
  int i;
  char format[SIZE_LARGE];
  BOOL first = TRUE;

  *format = '\0';

  for(i = 0; i < ABCOLNUM; i++)
  {
    if(C->AddrbookCols & (1<<i))
    {
      int p;

      if(first)
        first = FALSE;
      else
        strlcat(format, " BAR,", sizeof(format));

      p = strlen(format);

      snprintf(&format[p], sizeof(format)-p, "COL=%d W=-1", i);
    }
  }

  set(lv, MUIA_NListtree_Format, format);
}

///

/// AB_New
//  Creates address book window
struct AB_ClassData *AB_New(void)
{
  struct AB_ClassData *data;

  ENTER();

  if((data = calloc(1, sizeof(struct AB_ClassData))) != NULL)
  {
    enum {
      AMEN_NEW,AMEN_OPEN,AMEN_APPEND,AMEN_SAVE,AMEN_SAVEAS,
      AMEN_IMPORT_LDIF, AMEN_IMPORT_CSV, AMEN_IMPORT_TAB, AMEN_IMPORT_XML,
      AMEN_EXPORT_LDIF, AMEN_EXPORT_CSV, AMEN_EXPORT_TAB,
      AMEN_PRINTA,
      AMEN_FIND,AMEN_NEWUSER,AMEN_NEWLIST,AMEN_NEWGROUP,AMEN_EDIT,
      AMEN_DUPLICATE,AMEN_DELETE,AMEN_PRINTE,AMEN_SORTALIAS,
      AMEN_SORTLNAME,AMEN_SORTFNAME,AMEN_SORTDESC,AMEN_SORTADDR,
      AMEN_FOLD,AMEN_UNFOLD
    };

    data->GUI.WI = WindowObject,
       MUIA_HelpNode,"AB_W",
       MUIA_Window_Menustrip, MenustripObject,
          MenuChild, MenuObject,
             MUIA_Menu_Title, tr(MSG_CO_CrdABook),
             MenuChild, Menuitem(tr(MSG_New), "N", TRUE, FALSE, AMEN_NEW),
             MenuChild, Menuitem(tr(MSG_Open), "O", TRUE, FALSE, AMEN_OPEN),
             MenuChild, Menuitem(tr(MSG_Append), "I", TRUE, FALSE, AMEN_APPEND),
             MenuChild, MenuBarLabel,
             MenuChild, MenuitemObject,
                MUIA_Menuitem_Title, tr(MSG_AB_IMPORT),
                MenuChild, Menuitem(tr(MSG_AB_LDIF), NULL, TRUE, FALSE, AMEN_IMPORT_LDIF),
                MenuChild, Menuitem(tr(MSG_AB_CSV), NULL, TRUE, FALSE, AMEN_IMPORT_CSV),
                MenuChild, Menuitem(tr(MSG_AB_TAB), NULL, TRUE, FALSE, AMEN_IMPORT_TAB),
                MenuChild, Menuitem(tr(MSG_AB_XML), NULL, ExpatBase != NULL, FALSE, AMEN_IMPORT_XML),
             End,
             MenuChild, MenuitemObject,
                MUIA_Menuitem_Title, tr(MSG_AB_EXPORT),
                MenuChild, Menuitem(tr(MSG_AB_LDIF), NULL, TRUE, FALSE, AMEN_EXPORT_LDIF),
                MenuChild, Menuitem(tr(MSG_AB_CSV), NULL, TRUE, FALSE, AMEN_EXPORT_CSV),
                MenuChild, Menuitem(tr(MSG_AB_TAB), NULL, TRUE, FALSE, AMEN_EXPORT_TAB),
             End,
             MenuChild, MenuBarLabel,
             MenuChild, Menuitem(tr(MSG_Save), "S", TRUE, FALSE, AMEN_SAVE),
             MenuChild, Menuitem(tr(MSG_SaveAs), "A", TRUE, FALSE, AMEN_SAVEAS),
             MenuChild, MenuBarLabel,
             MenuChild, Menuitem(tr(MSG_AB_MIFind), "F", TRUE, FALSE, AMEN_FIND),
             MenuChild, Menuitem(tr(MSG_Print), NULL, TRUE, FALSE,AMEN_PRINTA),
          End,
          MenuChild, MenuObject,
             MUIA_Menu_Title, tr(MSG_AB_Entry),
             MenuChild, Menuitem(tr(MSG_AB_AddUser), "P", TRUE, FALSE, AMEN_NEWUSER),
             MenuChild, Menuitem(tr(MSG_AB_AddList), "L", TRUE, FALSE, AMEN_NEWLIST),
             MenuChild, Menuitem(tr(MSG_AB_AddGroup), "G", TRUE, FALSE, AMEN_NEWGROUP),
             MenuChild, MenuBarLabel,
             MenuChild, Menuitem(tr(MSG_Edit), "E", TRUE, FALSE, AMEN_EDIT),
             MenuChild, Menuitem(tr(MSG_AB_Duplicate), "D", TRUE, FALSE, AMEN_DUPLICATE),
             MenuChild, Menuitem(tr(MSG_AB_MIDelete), "Del", TRUE, TRUE, AMEN_DELETE),
             MenuChild, MenuBarLabel,
             MenuChild, Menuitem(tr(MSG_AB_MIPrint), NULL, TRUE, FALSE, AMEN_PRINTE),
          End,
          MenuChild, MenuObject,
             MUIA_Menu_Title, tr(MSG_AB_Sort),
             MenuChild, Menuitem(tr(MSG_AB_SortByAlias), "1", TRUE, FALSE, AMEN_SORTALIAS),
             MenuChild, Menuitem(tr(MSG_AB_SortByName), "2", TRUE, FALSE, AMEN_SORTLNAME),
             MenuChild, Menuitem(tr(MSG_AB_SortByFirstname), "3", TRUE, FALSE, AMEN_SORTFNAME),
             MenuChild, Menuitem(tr(MSG_AB_SortByDesc), "4", TRUE, FALSE, AMEN_SORTDESC),
             MenuChild, Menuitem(tr(MSG_AB_SortByAddress), "5", TRUE, FALSE, AMEN_SORTADDR),
          End,
          MenuChild, MenuObject,
             MUIA_Menu_Title, tr(MSG_AB_View),
             MenuChild, Menuitem(tr(MSG_AB_Unfold), "<", TRUE, FALSE, AMEN_UNFOLD),
             MenuChild, Menuitem(tr(MSG_AB_Fold), ">", TRUE, FALSE, AMEN_FOLD),
          End,
       End,
       MUIA_Window_ID,MAKE_ID('B','O','O','K'),
       WindowContents, VGroup,
          Child, hasHideToolBarFlag(C->HideGUIElements) ?
             (HGroup,
                MUIA_HelpNode, "AB_B",
                Child, data->GUI.BT_TO  = MakeButton("_To:"),
                Child, data->GUI.BT_CC  = MakeButton("_CC:"),
                Child, data->GUI.BT_BCC = MakeButton("_BCC:"),
             End) :
             (HGroup, GroupSpacing(0),
                MUIA_HelpNode, "AB_B",
                Child, VGroup,
                   MUIA_Weight, 10,
                   MUIA_Group_VertSpacing, 0,
                   Child, data->GUI.BT_TO  = MakeButton("_To:"),
                   Child, data->GUI.BT_CC  = MakeButton("_CC:"),
                   Child, data->GUI.BT_BCC = MakeButton("_BCC:"),
                   Child, HVSpace,
                End,
                Child, MUI_MakeObject(MUIO_VBar, 12),
                Child, HGroupV,
                  Child, data->GUI.TB_TOOLBAR = AddrBookToolbarObject,
                  End,
                End,
             End),
          Child, NListviewObject,
             MUIA_CycleChain,         TRUE,
             MUIA_Listview_DragType,  MUIV_Listview_DragType_Immediate,
             MUIA_NListview_NList,    data->GUI.LV_ADDRESSES = AddrBookListtreeObject,
                InputListFrame,
                MUIA_NListtree_CompareHook,     &AB_LV_CmpFuncHook,
                MUIA_NListtree_DragDropSort,    TRUE,
                MUIA_NListtree_Title,           TRUE,
                MUIA_NListtree_ConstructHook,   &AB_LV_ConFuncHook,
                MUIA_NListtree_DestructHook,    &AB_LV_DesFuncHook,
                MUIA_NListtree_DisplayHook,     &AB_LV_DspFuncHook,
                MUIA_NListtree_EmptyNodes,      TRUE,
                MUIA_NList_DefaultObjectOnClick,FALSE,
                MUIA_Font,                      C->FixedFontList ? MUIV_NList_Font_Fixed : MUIV_NList_Font,
             End,
          End,
       End,
    End;

    // If we successfully created the WindowObject
    if(data->GUI.WI != NULL)
    {
      AB_MakeABFormat(data->GUI.LV_ADDRESSES);
      DoMethod(G->App, OM_ADDMEMBER, data->GUI.WI);
      set(data->GUI.WI, MUIA_Window_DefaultObject, data->GUI.LV_ADDRESSES);
      DoMethod(G->App, MUIM_MultiSet, MUIA_Disabled, TRUE, data->GUI.BT_TO,
                                                           data->GUI.BT_CC,
                                                           data->GUI.BT_BCC,
                                                           NULL);
      SetHelp(data->GUI.BT_TO ,MSG_HELP_AB_BT_TO );
      SetHelp(data->GUI.BT_CC ,MSG_HELP_AB_BT_CC );
      SetHelp(data->GUI.BT_BCC,MSG_HELP_AB_BT_BCC);

      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_NEW        , MUIV_Notify_Application, 2, MUIM_CallHook, &AB_NewABookHook);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_OPEN       , MUIV_Notify_Application, 2, MUIM_CallHook, &AB_OpenABookHook);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_APPEND     , MUIV_Notify_Application, 2, MUIM_CallHook, &AB_AppendABookHook);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_IMPORT_LDIF, MUIV_Notify_Application, 2, MUIM_CallHook, &AB_ImportLDIFABookHook);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_IMPORT_TAB , MUIV_Notify_Application, 3, MUIM_CallHook, &AB_ImportTabCSVABookHook, '\t');
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_IMPORT_CSV , MUIV_Notify_Application, 3, MUIM_CallHook, &AB_ImportTabCSVABookHook, ',');
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_IMPORT_XML , MUIV_Notify_Application, 2, MUIM_CallHook, &AB_ImportXMLABookHook);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_EXPORT_LDIF, MUIV_Notify_Application, 2, MUIM_CallHook, &AB_ExportLDIFABookHook);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_EXPORT_TAB , MUIV_Notify_Application, 3, MUIM_CallHook, &AB_ExportTabCSVABookHook, '\t');
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_EXPORT_CSV , MUIV_Notify_Application, 3, MUIM_CallHook, &AB_ExportTabCSVABookHook, ',');
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_SAVE       , MUIV_Notify_Application, 2, MUIM_CallHook, &AB_SaveABookHook);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_SAVEAS     , MUIV_Notify_Application, 2, MUIM_CallHook, &AB_SaveABookAsHook);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_PRINTA     , MUIV_Notify_Application, 2, MUIM_CallHook, &AB_PrintABookHook);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_NEWUSER    , MUIV_Notify_Application, 3, MUIM_CallHook, &AB_AddEntryHook, AET_USER);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_NEWLIST    , MUIV_Notify_Application, 3, MUIM_CallHook, &AB_AddEntryHook, AET_LIST);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_NEWGROUP   , MUIV_Notify_Application, 3, MUIM_CallHook, &AB_AddEntryHook, AET_GROUP);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_EDIT       , MUIV_Notify_Application, 2, MUIM_CallHook, &AB_EditHook);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_DUPLICATE  , MUIV_Notify_Application, 2, MUIM_CallHook, &AB_DuplicateHook);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_DELETE     , MUIV_Notify_Application, 2, MUIM_CallHook, &AB_DeleteHook);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_PRINTE     , MUIV_Notify_Application, 2, MUIM_CallHook, &AB_PrintHook);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_FIND       , MUIV_Notify_Application, 2, MUIM_CallHook, &AB_FindHook);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_SORTALIAS  , MUIV_Notify_Application, 3, MUIM_CallHook, &AB_SortHook, 0);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_SORTLNAME  , MUIV_Notify_Application, 3, MUIM_CallHook, &AB_SortHook, 1);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_SORTFNAME  , MUIV_Notify_Application, 3, MUIM_CallHook, &AB_SortHook, 2);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_SORTDESC   , MUIV_Notify_Application, 3, MUIM_CallHook, &AB_SortHook, 3);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_SORTADDR   , MUIV_Notify_Application, 3, MUIM_CallHook, &AB_SortHook, 4);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_FOLD       , MUIV_Notify_Application, 3, MUIM_CallHook, &AB_FoldUnfoldHook, TRUE);
      DoMethod(data->GUI.WI            , MUIM_Notify, MUIA_Window_MenuAction    , AMEN_UNFOLD     , MUIV_Notify_Application, 3, MUIM_CallHook, &AB_FoldUnfoldHook, FALSE);
      DoMethod(data->GUI.LV_ADDRESSES  , MUIM_Notify, MUIA_NListtree_Active,      MUIV_EveryTime  , MUIV_Notify_Application, 2, MUIM_CallHook, &AB_ActiveChangeHook);
      DoMethod(data->GUI.LV_ADDRESSES  , MUIM_Notify, MUIA_NListtree_DoubleClick, MUIV_EveryTime  , MUIV_Notify_Application, 2, MUIM_CallHook, &AB_DoubleClickHook);
      DoMethod(data->GUI.BT_TO         , MUIM_Notify, MUIA_Pressed              , FALSE           , MUIV_Notify_Application, 3, MUIM_CallHook, &AB_FromAddrBookHook, ABM_TO);
      DoMethod(data->GUI.BT_CC         , MUIM_Notify, MUIA_Pressed              , FALSE           , MUIV_Notify_Application, 3, MUIM_CallHook, &AB_FromAddrBookHook, ABM_CC);
      DoMethod(data->GUI.BT_BCC        , MUIM_Notify, MUIA_Pressed              , FALSE           , MUIV_Notify_Application, 3, MUIM_CallHook, &AB_FromAddrBookHook, ABM_BCC);
      DoMethod(data->GUI.WI,MUIM_Notify, MUIA_Window_InputEvent   ,"-capslock del", MUIV_Notify_Application  ,2,MUIM_CallHook       ,&AB_DeleteHook);
      DoMethod(data->GUI.WI,MUIM_Notify, MUIA_Window_CloseRequest ,TRUE         , MUIV_Notify_Application           ,2,MUIM_CallHook       ,&AB_CloseHook);
    }
    else
    {
      free(data);
      data = NULL;
    }
  }

  RETURN(data);
  return data;
}
///

