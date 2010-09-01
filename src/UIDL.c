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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extrasrc.h"

#include "YAM_stringsizes.h"
#include "YAM_utilities.h"

#include "FileInfo.h"
#include "MailServers.h"
#include "UIDL.h"

#include "Debug.h"

/// BuildUIDLFilename
// set up a name for a UIDL file to be accessed
static char *BuildUIDLFilename(const struct MailServerNode *msn)
{
  char *filename;

  ENTER();

  if(msn != NULL)
  {
    char uidlName[SIZE_FILE];
    char uidlPath[SIZE_PATHFILE];
    char *p;

    // create a file name consisting of the user and host name of the given server entry
    snprintf(uidlName, sizeof(uidlName), ".uidl_%s_%s", msn->username, msn->hostname);

    // filter out possible invalid characters for filenames
    p = uidlName;
    while(*p != '\0')
    {
      switch(*p)
      {
        case ':':
        case '/':
        case '<':
        case '>':
        case '[':
        case ']':
        {
          *p = '_';
        }
        break;
      }

      p++;
    }
    filename = CreateFilename(uidlName, uidlPath, sizeof(uidlPath));
  }
  else
  {
    char uidlPath[SIZE_PATHFILE];

    // use the old style .uidl name
    filename = CreateFilename(".uidl", uidlPath, sizeof(uidlPath));
  }

  RETURN(filename);
  return filename;
}

///
/// InitUIDLhash
// Initialize the UIDL list and load it from the .uidl file
struct UIDLhash *InitUIDLhash(const struct MailServerNode *msn)
{
  struct UIDLhash *uidlHash;

  ENTER();

  if((uidlHash = malloc(sizeof(*uidlHash))) != NULL)
  {
    // allocate a new hashtable for managing the UIDL data
    if((uidlHash->hash = HashTableNew(HashTableGetDefaultStringOps(), NULL, sizeof(struct UIDLtoken), 512)) != NULL)
    {
      char *filename;
      LONG size;
      FILE *fh = NULL;

      // try to access the account specific .uidl file first
      filename = BuildUIDLFilename(msn);
      if(ObtainFileInfo(filename, FI_SIZE, &size) == TRUE && size > 0)
      {
        fh = fopen(filename, "r");
      }

      if(fh == NULL)
      {
        // an account specific UIDL does not seem to exist, try the old .uidl file instead
        filename = BuildUIDLFilename(NULL);
        if(ObtainFileInfo(filename, FI_SIZE, &size) == TRUE && size > 0)
        {
          fh = fopen(filename, "r");
        }
      }

      if(fh != NULL)
      {
        // now read in the UIDL/MsgIDs line-by-line
        char *uidl = NULL;
        size_t size = 0;

        D(DBF_UIDL, "opened UIDL database file '%s'", filename);

        setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);

        // add all read UIDL to the hash marking them as OLD
        while(GetLine(&uidl, &size, fh) >= 0)
          AddUIDLtoHash(uidlHash, uidl, UIDLF_OLD);

        fclose(fh);

        free(uidl);
      }
      else
        W(DBF_UIDL, "no or empty .uidl file found");

      // remember the mail server to be able to regenerate the file name upon cleanup
      uidlHash->mailServer = (struct MailServerNode *)msn;
      // we start with an unmodified hash table
      uidlHash->isDirty = FALSE;

      SHOWVALUE(DBF_UIDL, uidlHash->hash->entryCount);
    }
    else
    {
      E(DBF_UIDL, "couldn't create new Hashtable for UIDL management");
      free(uidlHash);
      uidlHash = NULL;
    }
  }
  else
    E(DBF_UIDL, "couldn't create new Hashtable for UIDL management");

  RETURN(uidlHash);
  return uidlHash;
}
///
/// SaveUIDLtoken
// HashTable callback function to save an UIDLtoken
static enum HashTableOperator SaveUIDLtoken(UNUSED struct HashTable *table,
                                            struct HashEntryHeader *entry,
                                            UNUSED ULONG number,
                                            void *arg)
{
  struct UIDLtoken *token = (struct UIDLtoken *)entry;

  ENTER();

  // Check whether the UIDL is a new one (received from the server), then we keep it.
  // Otherwise (OLD set, but not NEW) we skip it, because the mail belonging to this
  // UIDL does no longer exist on the server and we can forget about it.
  if(isFlagSet(token->flags, UIDLF_NEW))
  {
    FILE *fh = (FILE *)arg;

    fprintf(fh, "%s\n", token->uidl);
    D(DBF_UIDL, "saved UIDL '%s' to .uidl file", token->uidl);
  }
  else
    D(DBF_UIDL, "outdated UIDL '%s' found and deleted", token->uidl);

  RETURN(htoNext);
  return htoNext;
}
///
/// CleanupUIDLhash
// Cleanup the whole UIDL hash
void CleanupUIDLhash(struct UIDLhash *uidlHash)
{
  ENTER();

  if(uidlHash != NULL)
  {
    if(uidlHash->hash != NULL)
    {
      // save the UIDLs only if something has been changed
      if(uidlHash->isDirty == TRUE)
      {
        char *filename;
        FILE *fh;

        // we are saving account specific .uidl files only, the old one will be kept
        // in case it still contains UIDLs of multiple accounts
        filename = BuildUIDLFilename(uidlHash->mailServer);

        // before we go and destroy the UIDL hash we have to
        // write it to the .uidl file back again.
        if((fh = fopen(filename, "w")) != NULL)
        {
          setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);

          // call HashTableEnumerate with the SaveUIDLtoken callback function
          HashTableEnumerate(uidlHash->hash, SaveUIDLtoken, fh);

          fclose(fh);
        }
        else
          E(DBF_UIDL, "couldn't open .uidl file for writing");
      }

      // now we can destroy the uidl hash
      HashTableDestroy(uidlHash->hash);
      uidlHash->hash = NULL;
    }

    free(uidlHash);

    D(DBF_UIDL, "successfully cleaned up UIDLhash");
  }

  LEAVE();
}
///
/// AddUIDLtoHash
// adds the UIDL of a mail transfer node to the hash
struct UIDLtoken *AddUIDLtoHash(struct UIDLhash *uidlHash, const char *uidl, const ULONG flags)
{
  struct UIDLtoken *token = NULL;
  struct HashEntryHeader *entry;

  ENTER();

  if((entry = HashTableOperate(uidlHash->hash, uidl, htoLookup)) != NULL && HASH_ENTRY_IS_LIVE(entry))
  {
    token = (struct UIDLtoken *)entry;

    token->flags |= flags;

    D(DBF_UIDL, "updated flags for UIDL '%s' (%08lx)", uidl, token);
    uidlHash->isDirty = TRUE;
  }
  else if((entry = HashTableOperate(uidlHash->hash, uidl, htoAdd)) != NULL)
  {
    token = (struct UIDLtoken *)entry;

    token->uidl = strdup(uidl);
    token->flags = flags;

    D(DBF_UIDL, "added UIDL '%s' (%08lx) to hash", uidl, token);
    uidlHash->isDirty = TRUE;
  }
  else
    E(DBF_UIDL, "couldn't add UIDL '%s' to hash", uidl);

  RETURN(token);
  return token;
}

///
