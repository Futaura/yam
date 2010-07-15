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
 YAM OpenSource project    : http://sourceforge.net/projects/yamos/

 $Id$

 Superclass:  MUIC_Application
 Description: Application subclass handles all "global" stuff.

***************************************************************************/

#include "YAM_cl.h"

#include <proto/icon.h>
#if defined(__amigaos4__)
#include <proto/application.h>
#endif

#include "YAM_addressbook.h"
#include "YAM_addressbookEntry.h"

#include "Debug.h"

#define EMAILCACHENAME "PROGDIR:.emailcache"

/* CLASSDATA
struct Data
{
  struct List EMailCache;
  STRPTR EMailCacheName;
  char compileInfo[SIZE_DEFAULT];
};
*/

struct EMailCacheNode
{
  struct Node ecn_Node;
  struct ABEntry ecn_Person;
};

/* Private functions */
/// LoadEMailCache()
VOID LoadEMailCache(STRPTR name, struct List *list)
{
  BPTR fh;

  ENTER();

  NewList(list);

  if((fh = Open(name, MODE_OLDFILE)))
  {
    int i = 0;
    char line[SIZE_REALNAME + SIZE_ADDRESS + 5]; /* should hold "name <addr>\n\0" */

    while(FGets(fh, line, sizeof(line)) && i++ < 100) // we limit the reading to a maximum of 100 so that this code can`t read endlessly
    {
      char *addr;
      char *end;
      struct EMailCacheNode *node;

      if((addr = strchr(line, '<')) && (end = strchr(addr, '>')) && (node = calloc(1, sizeof(struct EMailCacheNode))))
      {
        if(addr != line)
        {
          addr[-1] = '\0';
          // now check if the cached entry contains a comma in the real name and does not start with a quote
          if(strchr(line, ',') != NULL && line[0] != '"')
          {
            // add the quotes around the name
            snprintf(node->ecn_Person.RealName, sizeof(node->ecn_Person.RealName), "\"%s\"", line);
          }
          else
          {
            // just copy the real name
            strlcpy(node->ecn_Person.RealName, line, sizeof(node->ecn_Person.RealName));
          }
        }
        end[0] = '\0';
        strlcpy(node->ecn_Person.Address, addr+1, sizeof(node->ecn_Person.Address));

        AddTail(list, &node->ecn_Node);
      }
      else
      {
        D(DBF_STARTUP, "Error with '%s', parsing line: '%s'", name, line);
      }
    }
    Close(fh);
  }
  else
  {
    E(DBF_STARTUP, "Error opening file '%s' for reading", name);
  }

  LEAVE();
}

///
/// SaveEMailCache()
VOID SaveEMailCache(STRPTR name, struct List *list)
{
  BPTR fh;

  ENTER();

  if((fh = Open(name, MODE_NEWFILE)))
  {
    int i;
    char line[SIZE_REALNAME + SIZE_ADDRESS + 5]; /* should hold "name <addr>\n\0" */
    struct Node *curNode;

    i = 0;
    IterateList(list, curNode)
    {
      struct EMailCacheNode *node = (struct EMailCacheNode *)curNode;
      struct ABEntry *entry = &node->ecn_Person;

      if(i >= C->EmailCache)
        break;

      if(entry->RealName[0])
      {
        // check wether the real name contains a comma and does not yet start with quote
        if(strchr(entry->RealName, ',') != NULL && entry->RealName[0] != '"')
        {
          // add the necessary quotes
          snprintf(line, sizeof(line), "\"%s\" <%s>\n", entry->RealName, entry->Address);
        }
        else
        {
          // no quotes needed
          snprintf(line, sizeof(line), "%s <%s>\n", entry->RealName, entry->Address);
        }
      }
      else
      {
        snprintf(line, sizeof(line), "<%s>\n", entry->Address);
      }

      FPuts(fh, line);

      i++;
    }

    Close(fh);
  }
  else
  {
    E(DBF_STARTUP, "Error opening file '%s' for writing", name);
  }

  LEAVE();
}

///
/// MatchRealName
// check whether a given string matches any part of a real name
static BOOL MatchRealName(const char *realName, const char *text, LONG textLen, LONG *matchPart)
{
  BOOL match = FALSE;
  char *name;

  ENTER();

  // check if we have a realname at all
  if(realName[0] != '\0')
  {
    LONG part = 0;

    // first try to match the whole realname string
    // completly
    if(Strnicmp(realName, text, textLen) == 0)
      match = TRUE;
    else if((name = strdup(realName)) != NULL)
    {
      char *n = name;
      char *p;

      // if this didn't work out we see if we can seperate the realname
      // into a first/lastname and see if only parts of it matches
      do
      {
        // break up the name in single parts delimited by spaces, quotes and commas
        if((p = strpbrk(n, " \",'")) != NULL)
          *p++ = '\0';

        if(n[0] != '\0')
        {
          // now check if this part of the name matches
          if(Strnicmp(n, text, textLen) == 0)
          {
            // yes!!
            match = TRUE;
            break;
          }

          part++;
        }

        // advance to the next name part
        n = p;
      }
      while(p != NULL);

      free(name);
    }

    // remember which part of the name this is if there is any interest in it
    if(matchPart != NULL)
      *matchPart = part;
  }

  RETURN(match);
  return match;
}

///
/// FindAllABMatches()
// tries to find all matching addressbook entries and add them to the list
VOID FindAllABMatches(const char *text, Object *list, struct MUI_NListtree_TreeNode *root)
{
  LONG tl;
  struct MUI_NListtree_TreeNode *tn;

  ENTER();

  tl = strlen(text);

  // Now we try to find matches in the Addressbook Listtree
  tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_GetEntry, root, MUIV_NListtree_GetEntry_Position_Head, MUIF_NONE);

  for(;tn;)
  {
    if(isFlagSet(tn->tn_Flags, TNF_LIST)) /* it's a sublist */
    {
      FindAllABMatches(text, list, tn);
    }
    else
    {
      struct ABEntry *entry = (struct ABEntry *)tn->tn_User;
      struct CustomABEntry e = { -1, -1, NULL, NULL };

      if(Strnicmp(entry->Alias, text, tl) == 0)
      {
        e.MatchField = 0;
        e.MatchString = entry->Alias;
      }
      else if(MatchRealName(entry->RealName, text, tl, &e.RealNameMatchPart) == TRUE)
      {
        e.MatchField = 1;
        e.MatchString = entry->RealName;
      }
      else if(Strnicmp(entry->Address, text, tl) == 0)
      {
        e.MatchField = 2;
        e.MatchString = entry->Address;
      }

      if(e.MatchField != -1) /* one of the fields matches, so let's insert it in the MUI list */
      {
        e.MatchEntry = entry;
        DoMethod(list, MUIM_NList_InsertSingle, &e, MUIV_NList_Insert_Sorted);
      }
    }

    tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_GetEntry, tn, MUIV_NListtree_GetEntry_Position_Next, MUIF_NONE);
  }

  LEAVE();
}

///
/// FindABPerson()
// tries to find a Person in a addressbook
BOOL FindABPerson(struct Person *person, struct MUI_NListtree_TreeNode *root)
{
  struct MUI_NListtree_TreeNode *tn;
  BOOL result = FALSE;

  ENTER();

  // Now we try to find matches in the Addressbook Listtree
  tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_GetEntry, root, MUIV_NListtree_GetEntry_Position_Head, MUIF_NONE);

  for(;tn;)
  {
    if(isFlagSet(tn->tn_Flags, TNF_LIST)) /* it's a sublist */
    {
      if(FindABPerson(person, tn))
      {
        result = TRUE;
        break;
      }
    }
    else
    {
      struct ABEntry *entry = (struct ABEntry *)tn->tn_User;

      // If the email matches a entry in the AB we already can return here with TRUE
      if(Stricmp(entry->Address, person->Address) == 0)
      {
        result = TRUE;
        break;
      }
    }

    tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_GetEntry, tn, MUIV_NListtree_GetEntry_Position_Next, MUIF_NONE);
  }

  RETURN(result);
  return result;
}

///

/* Public Methods */
/// DECLARE(FindEmailMatches)
DECLARE(FindEmailMatches) // STRPTR matchText, Object *list
{
  GETDATA;

  ENTER();

  if(msg->matchText != NULL && msg->matchText[0] != '\0')
  {
    // We first try to find matches in the Addressbook
    // and add them to the MUI list
    FindAllABMatches(msg->matchText, msg->list, MUIV_NListtree_GetEntry_ListNode_Root);

    // If the user has selected the EmailCache feature we also have to check this
    // list and add matches to the MUI List too
    if(C->EmailCache > 0)
    {
      int i;
      LONG tl = strlen(msg->matchText);
      struct Node *curNode;

      i = 0;
      IterateList(&data->EMailCache, curNode)
      {
        struct EMailCacheNode *node = (struct EMailCacheNode *)curNode;
        struct ABEntry *entry = &node->ecn_Person;
        struct CustomABEntry e = { -1, -1, NULL, NULL };

        if(i >= C->EmailCache)
          break;

        if(MatchRealName(entry->RealName, msg->matchText, tl, &e.RealNameMatchPart) == TRUE)
        {
          e.MatchField = 1;
          e.MatchString = entry->RealName;
        }
        else if(Strnicmp(entry->Address, msg->matchText, tl) == 0)
        {
          e.MatchField = 2;
          e.MatchString = entry->Address;
        }

        if(e.MatchField != -1)
        {
          // at least one of the fields matches, so let's insert it in the MUI list
          e.MatchEntry = entry;
          DoMethod(msg->list, MUIM_NList_InsertSingle, &e, MUIV_NList_Insert_Bottom);
        }

        i++;
      }
    }
  }

  RETURN(0);
  return 0;
}

///
/// DECLARE(FindEmailCacheMatch)
// Method that search in the email cache and return the found entry if not more than one
DECLARE(FindEmailCacheMatch) // STRPTR matchText
{
  GETDATA;
  struct ABEntry *foundentry = NULL;

  ENTER();

  if(C->EmailCache > 0 && msg->matchText && msg->matchText[0] != '\0')
  {
    int i, matches = 0;
    LONG tl = strlen(msg->matchText);
    struct Node *curNode;

    i = 0;
    IterateList(&data->EMailCache, curNode)
    {
      struct EMailCacheNode *node = (struct EMailCacheNode *)curNode;
      struct ABEntry *entry = &node->ecn_Person;

      if(i >= C->EmailCache)
        break;

      if(MatchRealName(entry->RealName, msg->matchText, tl, NULL) == TRUE ||
         Strnicmp(entry->Address,  msg->matchText, tl) == 0)
      {
        if(++matches > 1)
        {
          foundentry = NULL;
          break;
        }
        foundentry = entry;
      }

      i++;
    }
  }

  RETURN(foundentry);
  return (IPTR)foundentry;
}

///
/// DECLARE(AddToEmailCache)
// method that parses a string for addresses and add them to the emailcache if enabled
DECLARE(AddToEmailCache) // struct Person *person
{
  GETDATA;

  ENTER();

  // if the emailcache feature is turned off or
  // the supplied person doesn`t have a address, lets exit immediatly
  if(C->EmailCache == 0 || !msg->person->Address[0])
  {
    RETURN(-1);
    return -1;
  }

  // We first check the Addressbook if this Person already exists in the AB and if
  // so we cancel this whole operation.
  if(!FindABPerson(msg->person, MUIV_NListtree_GetEntry_ListNode_Root))
  {
    int i;
    BOOL found = FALSE;
    struct Node *curNode;

    // Ok, it doesn`t exists in the AB, now lets check the cache list
    // itself
    i = 0;
    IterateList(&data->EMailCache, curNode)
    {
      struct EMailCacheNode *node = (struct EMailCacheNode *)curNode;
      struct ABEntry *entry = &node->ecn_Person;

      if(i >= C->EmailCache)
        break;

      // If we find the same entry already in the list we just move it
      // up to the top
      if((msg->person->RealName[0] ? !Stricmp(entry->RealName, msg->person->RealName) : TRUE) &&
         !Stricmp(entry->Address, msg->person->Address))
      {
        Remove((struct Node *)node);
        AddHead(&data->EMailCache, (struct Node *)node);
        found = TRUE;
        break;
      }

      i++;
    }

    // if we didn`t find the person already in the list
    // we have to add it after the last node
    if(found == FALSE)
    {
      struct EMailCacheNode *newnode;

      // we alloc mem for this new node and add it behind the last node
      if((newnode = calloc(1, sizeof(struct EMailCacheNode))))
      {
        struct ABEntry *entry = &newnode->ecn_Person;

        // Lets copy the data in the new Person struct
        // for the real name we have to check for possible commas without quotes yet
        if(strchr(msg->person->RealName, ',') != NULL && msg->person->RealName[0] != '"')
        {
          // add the necessary quotes around the real name
          snprintf(entry->RealName, sizeof(entry->RealName), "\"%s\"", msg->person->RealName);
        }
        else
        {
          // simply copy the real name
          strlcpy(entry->RealName, msg->person->RealName, sizeof(entry->RealName));
        }
        strlcpy(entry->Address, msg->person->Address, sizeof(entry->Address));

        // we always add new items to the top because this is a FILO
        AddHead(&data->EMailCache, (struct Node *)newnode);
      }
    }

    // Now lets save the emailcache file again
    SaveEMailCache(data->EMailCacheName, &data->EMailCache);
  }

  RETURN(0);
  return 0;
}

///

/* Overloaded Methods */
/// OVERLOAD(OM_NEW)
OVERLOAD(OM_NEW)
{
  char filebuf[SIZE_PATHFILE];
  char verbuf[CBD_TITLELEN];
  BOOL singleTaskOnly = TRUE;
  char var;

  // prepare a string pointer array with all the
  // names of the used classes within. This array is only usefull if MUI v20
  // is used and the user wants to alter the MUI settings of the application
  static const char *const Classes[] = { "BetterString.mcc",
                                         "NBalance.mcc",
                                         "NList.mcc",
                                         "NListtree.mcc",
                                         "NListviews.mcc",
                                         "TextEditor.mcc",
                                         "TheBar.mcc",
                                         NULL
                                       };

  // let us check if there is a "MultipleYAM" env variable and if
  // so we set SingleTask to true
  if(GetVar("MultipleYAM", &var, sizeof(var), 0) > -1)
    singleTaskOnly = FALSE;

  // now we load the standard icons like (check.info, new.info etc)
  // but we also try to take care of different icon.library versions.
  AddPath(filebuf, G->ProgDir, G->ProgName, sizeof(filebuf));

  if(LIB_VERSION_IS_AT_LEAST(IconBase, 44, 0) == TRUE)
   G->HideIcon = (struct DiskObject *)GetIconTags(filebuf, TAG_DONE);
  else
   G->HideIcon = GetDiskObject(filebuf);

  // set up the version string for the Commodity title
  // the string MUST include the "$VER:" cookie, because this one will be stripped
  // by MUI. However, to avoid any problems with two version cookies in the final
  // executable we set up this one here in a bit more obfuscated fashion.
  snprintf(verbuf, sizeof(verbuf), "%lcVER: YAM %s (%s)", '$', yamver, yamversiondate);

  if((obj = (Object *)DoSuperNew(cl, obj,

    MUIA_Application_Author,         "YAM Open Source Team",
    MUIA_Application_Base,           "YAM",
    MUIA_Application_Title,          "YAM",
    MUIA_Application_Version,        verbuf,
    MUIA_Application_Copyright,      yamcopyright,
    MUIA_Application_Description,    tr(MSG_APP_DESCRIPTION),
    MUIA_Application_UseRexx,        FALSE,
    MUIA_Application_SingleTask,     singleTaskOnly,
    MUIA_Application_UsedClasses,    Classes,
    MUIA_Application_HelpFile,       "YAM.guide",
    MUIA_Application_DiskObject,     G->HideIcon,

    TAG_MORE, inittags(msg))))
  {
    GETDATA;
    struct DateTime dt;
    struct TagItem *tags = inittags(msg), *tag;

    data->EMailCacheName = (STRPTR)EMAILCACHENAME;

    // now we generate some static default for our whole application
    dt.dat_Stamp.ds_Days   = yamversiondays;
    dt.dat_Stamp.ds_Minute = 0;
    dt.dat_Stamp.ds_Tick   = 0;
    dt.dat_Format          = FORMAT_DEF;
    dt.dat_Flags           = 0L;
    dt.dat_StrDay          = NULL;
    dt.dat_StrDate         = data->compileInfo;
    dt.dat_StrTime         = NULL;
    DateToStr(&dt);
    data->compileInfo[31] = '\0';  // make sure that the string is really terminated at LEN_DATSTRING.

    // now we add the compiler information as YAM can be
    // compiled with different versions and types of compilers
    strlcat(&data->compileInfo[strlen(data->compileInfo)], yamcompiler, sizeof(data->compileInfo)-strlen(data->compileInfo));

    while((tag = NextTagItem((APTR)&tags)))
    {
      switch(tag->ti_Tag)
      {
        ATTR(EMailCacheName) : data->EMailCacheName = (STRPTR)tag->ti_Data ; break;
      }
    }

    LoadEMailCache(data->EMailCacheName, &data->EMailCache);
  }

  return (IPTR)obj;
}

///
/// OVERLOAD(OM_DISPOSE)
OVERLOAD(OM_DISPOSE)
{
  GETDATA;
  struct EMailCacheNode *node;

  // lets free the EMailCache List ourself in here, to make it a bit cleaner.
  while((node = (struct EMailCacheNode *)RemHead(&data->EMailCache)))
  {
    free(node);
  }

  // then we call the supermethod to let
  // MUI free the rest for us.
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
    ATTR(CompileInfo) : *store = (IPTR)data->compileInfo; return TRUE;
  }

  return DoSuperMethodA(cl, obj, msg);
}
///
/// OVERLOAD(OM_SET)
OVERLOAD(OM_SET)
{
  struct TagItem *tags = inittags(msg), *tag;

  while((tag = NextTagItem((APTR)&tags)))
  {
    switch(tag->ti_Tag)
    {
      // if the Application is going to be (un)iconified we
      // have to take care of certain tasks.
      case MUIA_Application_Iconified:
      {
        // in case we have an applicationID we make sure
        // we notify application.library that YAM was uniconified
        #if defined(__amigaos4__)
        if(G->applicationID != 0)
        {
          struct TagItem hiddenTags[] =
          {
            { APPATTR_Hidden, tag->ti_Data },
            { TAG_DONE, 0 }
          };

          // adapt the tag values for the different interface versions
          if(IApplication->Data.Version >= 2)
          {
            if(APPATTR_Hidden < TAG_USER)
              hiddenTags[0].ti_Tag += TAG_USER;
          }
          else
          {
            if(APPATTR_Hidden >= TAG_USER)
              hiddenTags[0].ti_Tag -= TAG_USER;
          }

          SetApplicationAttrsA(G->applicationID, hiddenTags);
        }
        #endif

        D(DBF_STARTUP, "Application was %s", tag->ti_Data ? "iconified" : "uniconified");
      }
      break;

      case MUIA_Application_Active:
      {
        D(DBF_STARTUP, "Application is %s", tag->ti_Data ? "active" : "inactive");

        set(obj, MUIA_Application_Sleep, !tag->ti_Data);
      }
      break;
    }
  }

  return DoSuperMethodA(cl, obj, msg);
}
///
