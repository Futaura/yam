/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 by Marcel Beck <mbeck@yam.ch>
 Copyright (C) 2000-2009 by YAM Open Source Team

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
#include <stdlib.h>
#include <string.h>

#include <clib/alib_protos.h>
#include <clib/macros.h>
#include <libraries/asl.h>
#include <libraries/iffparse.h>
#include <libraries/gadtools.h>
#include <mui/NList_mcc.h>
#include <mui/NListview_mcc.h>
#include <mui/TextEditor_mcc.h>
#include <proto/codesets.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>

#include "extrasrc.h"

#include "SDI_hook.h"

#include "YAM.h"
#include "YAM_addressbook.h"
#include "YAM_addressbookEntry.h"
#include "YAM_config.h"
#include "YAM_error.h"
#include "YAM_folderconfig.h"
#include "YAM_global.h"
#include "YAM_main.h"
#include "YAM_mainFolder.h"
#include "YAM_read.h"
#include "YAM_write.h"
#include "YAM_utilities.h"

#include "mui/Classes.h"
#include "mime/rfc2231.h"
#include "mime/base64.h"
#include "mime/qprintable.h"
#include "mime/uucode.h"

#include "HTML2Mail.h"
#include "FileInfo.h"
#include "FolderList.h"
#include "Locale.h"
#include "MailList.h"
#include "MimeTypes.h"
#include "MUIObjects.h"
#include "ParseEmail.h"
#include "Requesters.h"

#include "Debug.h"

/**************************************************************************/

/* local defines */
enum SMsgType { SMT_NORMAL=0, SMT_MDN, SMT_SIGNED, SMT_ENCRYPTED };

/* local protos */
static BOOL RE_HandleMDNReport(const struct Part *frp);

/***************************************************************************
 Module: Read
***************************************************************************/

/// RE_SuggestName
// Suggests a file name based on the message subject and strips characters
// from it that are not valid for filenames.
static void RE_SuggestName(const struct Mail *mail, char *name, const size_t length)
{
  const char *ptr = mail->Subject;
  size_t i = 0;

  ENTER();

  // we copy until we reach length-1 as we have
  // to reserve one space for the NUL char
  while(*ptr != '\0' && i < length-1)
  {
    unsigned char c;
    static const char invalidChars[] = ":/#?*()[]|%'\"";

    // see if we have to replace certain unallowed characters
    // by a '_'
    c = *ptr;
    if((c <= 0x20) || (c > 0x80 && c < 0xA0) || strchr(invalidChars, c) != NULL)
      c = '_';

    // put that character into our
    // destination string
    name[i] = c;

    // continue
    ptr++;
    i++;
  }

  // make sure name is NUL terminated
  name[i] = '\0';

  LEAVE();
}

///
/// RE_Export
//  Saves message or attachments to disk
BOOL RE_Export(struct ReadMailData *rmData, const char *source,
               const char *dest, const char *name, int nr, BOOL force, BOOL overwrite, const char *ctype)
{
  BOOL success = FALSE;
  Object *win;
  struct Mail *mail;
  char path[SIZE_PATHFILE];

  ENTER();

  win = rmData->readWindow ? rmData->readWindow : G->MA->GUI.WI;
  mail = rmData->mail;

  if(dest[0] == '\0')
  {
    char filename[SIZE_FILE];
    struct FileReqCache *frc;

    if(name[0] != '\0')
    {
      strlcpy(filename, name, sizeof(filename));
    }
    else if(nr != 0)
    {
      char ext[SIZE_DEFAULT];
      char suggestedName[SIZE_FILE];
      int extlen;

      // we have to get the file extension of our source file and use it
      // in our destination file as well
      stcgfe(ext, source);

      if(ext[0] != '\0')
        extlen = strlen(ext);
      else
        extlen = 3;

      RE_SuggestName(mail, suggestedName, sizeof(suggestedName)-extlen-3);
      snprintf(filename, sizeof(filename), "%s-%d.%s", suggestedName[0] != '\0' ? suggestedName : mail->MailFile,
                                                       nr,
                                                       ext[0] != '\0' ? ext : "tmp");
    }
    else
    {
      char suggestedName[SIZE_FILE];

      RE_SuggestName(mail, suggestedName, sizeof(suggestedName)-4);
      snprintf(filename, sizeof(filename), "%s.msg", suggestedName[0] != '\0' ? suggestedName : mail->MailFile);
    }

    if(force)
      dest = AddPath(path, C->DetachDir, filename, sizeof(path));
    else if((frc = ReqFile(ASL_DETACH, win, tr(MSG_RE_SaveMessage), REQF_SAVEMODE, C->DetachDir, filename)))
      dest = AddPath(path, frc->drawer, frc->file, sizeof(path));
    else
      dest = NULL;
  }

  if(dest != NULL)
  {
    if(FileExists(dest) && !overwrite)
    {
      if(MUI_Request(G->App, win, 0, tr(MSG_MA_ConfirmReq), tr(MSG_YesNoReq), tr(MSG_FILE_OVERWRITE), FilePart(dest)) == 0)
        dest = NULL;
    }
  }

  if(dest != NULL)
  {
    if(CopyFile(dest, 0, source, 0) == FALSE)
    {
      ER_NewError(tr(MSG_ER_CantCreateFile), dest);
      dest = NULL;
    }
  }

  if(dest != NULL)
  {
    char address[SIZE_LARGE];

    // let us set a default file comment
    SetComment(dest, BuildAddress(address, sizeof(address), mail->From.Address, mail->From.RealName));

    // set the protection bits correctly. Here we check if this file
    // is an identified amiga executable (MT_AP_AEXE) or if it of mime
    // type (application/octet-stream). Because for an octet-stream file
    // we really can't tell if it is a real executable or just a plain
    // binary file, hence we force a ----rwed protection for it.
    if(stricmp(ctype, IntMimeTypeArray[MT_AP_OCTET].ContentType) == 0 ||
       stricmp(ctype, IntMimeTypeArray[MT_AP_AEXE].ContentType) == 0)
    {
      // set protection of file to ----rwed
      SetProtection(dest, 0);
    }
    else if(stricmp(ctype, IntMimeTypeArray[MT_AP_SCRIPT].ContentType) == 0)
    {
      // set protection of file to -s--rwed
      SetProtection(dest, FIBF_SCRIPT);
    }

    AppendToLogfile(LF_VERBOSE, 80, tr(MSG_LOG_SavingAtt), dest, mail->MailFile, FolderName(mail->Folder));

    success = TRUE;
  }

  RETURN(success);
  return success;
}
///
/// RE_PrintFile
//  Prints a file. Currently it is just dumped to PRT:
BOOL RE_PrintFile(const char *filename)
{
  BOOL success = FALSE;

  ENTER();

  if(CheckPrinter() == TRUE)
  {
    switch(C->PrintMethod)
    {
      case PRINTMETHOD_RAW :
        // continue

      default:
        success = CopyFile("PRT:", 0, filename, 0);
      break;
    }

    // signal the failure to the user
    // in case we were not able to print something
    if(success == FALSE)
      MUI_Request(G->App, NULL, 0, tr(MSG_ErrorReq), tr(MSG_OkayReq), tr(MSG_ER_PRINTER_FAILED));
  }

  RETURN(success);
  return success;
}

///
/// BuildCommandString
// sets up a complete MIME command string, substituting place holders if necessary
static void BuildCommandString(char *command, const size_t commandLen, const char *format, const char *file)
{
  const char *p;

  ENTER();

  command[0] = '\0';

  if((p = format) != NULL)
  {
    char c;
    BOOL hasQuotes = FALSE;

    D(DBF_MIME, "building command string of '%s'", format);

    while((c = *p++) != '\0')
    {
      switch(c)
      {
        case '%':
        {
          // check if the next character does exist at all
          if((c = *p) != '\0')
          {
            // now we can advance our pointer
            p++;

            // handle the possible options
            switch(c)
            {
              case 'p':
              {
                // insert the public screen name
                char pubScreenName[MAXPUBSCREENNAME + 1];

                // obtain the public screen name
                GetPubScreenName((struct Screen *)xget(G->MA->GUI.WI, MUIA_Window_Screen), pubScreenName, sizeof(pubScreenName));

                // insert the public screen name
                if(!hasQuotes)
                  strlcat(command, "\"", commandLen);
                strlcat(command, pubScreenName, commandLen);
                if(!hasQuotes)
                  strlcat(command, "\"", commandLen);
              }
              break;

              case 's':
              {
                // insert the filename
                if(!hasQuotes)
                  strlcat(command, "\"", commandLen);
                strlcat(command, file, commandLen);
                if(!hasQuotes)
                  strlcat(command, "\"", commandLen);
              }
              break;

              // if another '%' is found we just put one
              // into our command string
              case '%':
              {
                char tmp[2];

                tmp[0] = '%';
                tmp[1] = '\0';
                strlcat(command, tmp, commandLen);
              }
              break;

              // unknown '%X' sequences will be ignored and
              // not put in our destination command string
              default:
                // nothing
              break;
            }
          }
        }
        break;

        case '"':
        {
          // remember that the user put some quotes in the command string himself
          hasQuotes = !hasQuotes;
        }
        // continue

        default:
        {
          char tmp[2];

          tmp[0] = c;
          tmp[1] = '\0';
          strlcat(command, tmp, commandLen);
        }
        break;
      }
    }
  }

  D(DBF_MIME, "built command string '%s'", command);

  LEAVE();
}
///
/// RE_DisplayMIME
//  Displays a message part (attachment) using a MIME viewer
void RE_DisplayMIME(char *fname, const char *ctype)
{
  struct MimeTypeNode *mt = NULL;
  BOOL triedToIdentify = FALSE;

  ENTER();

  // in case no content-type was specified, or it is empty we try to be
  // somewhat intelligent, by deeper analyzing the file and its content
  // first.
  if(ctype == NULL || ctype[0] == '\0')
  {
    D(DBF_MIME, "no content-type specified, analyzing '%s'", fname);

    if((ctype = IdentifyFile(fname)) != NULL)
      D(DBF_MIME, "identified file as '%s'", ctype);

    triedToIdentify = TRUE;
  }

  D(DBF_MIME, "trying to display file '%s' of content-type '%s'", fname, ctype);

  // we first browse through the whole mimeTypeList and try to find
  // out if the content-type spec in one of the user-defined
  // MIME types matches or not.
  if(ctype != NULL)
  {
    struct MinNode *curNode;

    for(curNode = C->mimeTypeList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
    {
      struct MimeTypeNode *curType = (struct MimeTypeNode *)curNode;

      if(MatchNoCase(ctype, curType->ContentType))
      {
        mt = curType;
        break;
      }
    }
  }

  // if the MIME part is an rfc822 conform email attachment we
  // try to open it as a virtual mail in another read window.
  if(mt == NULL && ctype != NULL && stricmp(ctype, "message/rfc822") == 0)
  {
    struct TempFile *tf;

    D(DBF_MIME, "identified content-type as 'message/rfc822'");

    if((tf = OpenTempFile(NULL)) != NULL)
    {
      struct ExtendedMail *email;

      // copy the contents of our message file into the
      // temporary file.
      if(CopyFile(tf->Filename, NULL, fname, NULL) == TRUE &&
         (email = MA_ExamineMail(NULL, (char *)FilePart(tf->Filename), TRUE)) != NULL)
      {
        struct Mail *mail;
        struct ReadMailData *rmData;

        if((mail = calloc(1, sizeof(struct Mail))) == NULL)
        {
          CloseTempFile(tf);
          LEAVE();
          return;
        }

        memcpy(mail, &email->Mail, sizeof(struct Mail));
        mail->Reference = NULL;
        mail->Folder    = NULL;
        mail->sflags    = SFLAG_READ; // this sets the mail as OLD
        SET_FLAG(mail->mflags, MFLAG_NOFOLDER);

        MA_FreeEMailStruct(email);

        // create the read read window now
        if((rmData = CreateReadWindow(TRUE)) != NULL)
        {
          rmData->tempFile = tf;

          // make sure it is opened correctly and then read in a mail
          if(SafeOpenWindow(rmData->readWindow) == FALSE ||
             DoMethod(rmData->readWindow, MUIM_ReadWindow_ReadMail, mail) == FALSE)
          {
            // on any error we make sure to delete the read window
            // immediatly again.
            CleanupReadMailData(rmData, TRUE);
          }
        }
        else
        {
          CloseTempFile(tf);
          free(mail);
        }
      }
      else
        CloseTempFile(tf);
    }
  }
  else
  {
    static char command[SIZE_COMMAND+SIZE_PATHFILE];
    char *cmdPtr;

    // if we still didn't found the correct mime type or the command line of the
    // current mime type is empty we use the default mime viewer specified in
    // the YAM configuration.
    if(mt == NULL || mt->Command[0] == '\0')
    {
      cmdPtr = C->DefaultMimeViewer;

      // if we haven't tried to identify the file
      // via IdentifyFile() yet, we try it here
      if(triedToIdentify == FALSE)
      {
        D(DBF_MIME, "haven't found a user action, trying to identifying via IdentifyFile()");

        if((ctype = IdentifyFile(fname)))
        {
          struct MinNode *curNode;

          D(DBF_MIME, "identified file as '%s'", ctype);

          for(curNode = C->mimeTypeList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
          {
            struct MimeTypeNode *curType = (struct MimeTypeNode *)curNode;

            if(MatchNoCase(ctype, curType->ContentType))
            {
              if(curType->Command[0] != '\0')
                cmdPtr = curType->Command;

              break;
            }
          }
        }
      }
    }
    else
      cmdPtr = mt->Command;

    // set up the command string
    BuildCommandString(command, sizeof(command), cmdPtr, GetRealPath(fname));

    // execute the command
    ExecuteCommand(command, TRUE, OUT_NIL);
  }

  LEAVE();
}
///
/// RE_SaveAll
//  Saves all attachments to disk
void RE_SaveAll(struct ReadMailData *rmData, const char *path)
{
  char *dest;
  size_t size = strlen(path)+SIZE_DEFAULT+1;

  ENTER();

  if((dest = calloc(1, size)))
  {
    struct Part *part;
    char fname[SIZE_DEFAULT];

    for(part = rmData->firstPart->Next; part; part = part->Next)
    {
      // we skip the part which is considered the letterPart
      if(part->Nr != rmData->letterPartNum)
      {
        if(*part->Name)
          strlcpy(fname, part->Name, sizeof(fname));
        else
          snprintf(fname, sizeof(fname), "%s-%d", rmData->mail->MailFile, part->Nr);

        AddPath(dest, path, fname, size);

        RE_DecodePart(part);
        RE_Export(rmData, part->Filename, dest, part->Name, part->Nr, FALSE, FALSE, part->ContentType);
      }
    }

    free(dest);
  }

  LEAVE();
}
///
/// RE_GetAddressFromLog
//  Finds e-mail address in PGP output
static BOOL RE_GetAddressFromLog(char *buf, char *address)
{
  BOOL success = FALSE;

  ENTER();

  if((buf = strchr(buf, '"')) != NULL)
  {
    strlcpy(address, ++buf, SIZE_ADDRESS);
    if((buf = strchr(address, '"')))
      *buf = '\0';

    success = TRUE;
  }

  RETURN(success);
  return success;
}
///
/// RE_GetSigFromLog
//  Interprets logfile created from the PGP signature check
void RE_GetSigFromLog(struct ReadMailData *rmData, char *decrFor)
{
  BOOL sigDone = FALSE;
  BOOL decrFail = FALSE;
  FILE *fh;

  ENTER();

  if((fh = fopen(PGPLOGFILE, "r")) != NULL)
  {
    char *buf = NULL;
    size_t buflen = 0;

    setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);

    while(getline(&buf, &buflen, fh) > 0)
    {
      if(decrFail == FALSE && decrFor != NULL && G->PGPVersion == 5)
      {
        if(strnicmp(buf, "cannot decrypt", 14) == 0)
        {
          *decrFor = '\0';

          // skip one line
          getline(&buf, &buflen, fh);
          getline(&buf, &buflen, fh);

          RE_GetAddressFromLog(buf, decrFor);
          decrFail = TRUE;
        }
      }

      if(sigDone == FALSE)
      {
        if(strnicmp(buf, "good signature", 14) == 0)
          sigDone = TRUE;
        else if(strnicmp(buf, "bad signature", 13) == 0 || stristr(buf, "unknown keyid") != NULL)
        {
          SET_FLAG(rmData->signedFlags, PGPS_BADSIG);
          sigDone = TRUE;
        }

        if(sigDone == TRUE)
        {
          if(G->PGPVersion == 5)
          {
            // skip one line
            getline(&buf, &buflen, fh);
            getline(&buf, &buflen, fh);
          }

          if(RE_GetAddressFromLog(buf, rmData->sigAuthor) == TRUE)
            SET_FLAG(rmData->signedFlags, PGPS_ADDRESS);

          break;
        }
      }
    }

    fclose(fh);

    if(buf != NULL)
      free(buf);

    if(sigDone == TRUE || (decrFor != NULL && decrFail == FALSE))
      DeleteFile(PGPLOGFILE);
  }

  LEAVE();
}
///

/*** MIME ***/
/// ExtractNextParam()
// extracts the name and value of the next upcoming content paramater in string s
// returns the end of string s in case we are finished
static char *ExtractNextParam(char *s, char **name, char **value)
{
  char *p;
  char *u;

  ENTER();

  // skip all leading spaces and return pointer
  // to first real char.
  p = TrimStart(s);

  // extract the paramater name first
  if((s = u = strchr(p, '=')) != NULL && u > p)
  {
    char *t;
    int nameLen;

    // skip trailing spaces as well
    while(u > p && *--u && isspace(*u));

    // get the length of the parameter name
    nameLen = u-p+1;

    // allocate enough memory to put in the name
    if((*name = t = malloc(nameLen+1)) != NULL)
    {
      // copy the parameter name char by char
      // while converting it to lowercase
      while(p <= u)
      {
        int c = *p++;

        // for SAS/C this is a macro and no function.
        // Hence we better pass an int than something like "*p++" which
        // could have unpredictable results.
        *t++ = tolower(c);
      }

      *t = '\0'; // NUL termination

      D(DBF_MAIL, "name='%s' %ld", *name, nameLen);

      // now we go and extract the parameter value, actually
      // taking respect of quoted string and such
      s = TrimStart(++s);
      if(*s != '\0')
      {
        BOOL quoted = (*s == '"');
        int valueLen;

        if(quoted == TRUE)
          s++;

        // we first calculate the value length for
        // allocating the buffer later on
        if((p = strchr(s, ';')))
        {
          valueLen = p-s;
          p++;
        }
        else
        {
          valueLen = strlen(s);
          p = s+valueLen;
        }

        // allocate enough memory to put in the value
        if((*value = t = malloc(valueLen+1)) != NULL)
        {
          int i = valueLen;

          while(i--)
          {
            if(quoted)
            {
              if(*s == '\\')
                s++;
              else if(*s == '"')
                break;
            }

            *t++ = *s++;
          }

          *t = '\0';

          // skip trailing spaces as well if the value
          // wasn't quoted
          if(quoted == FALSE)
          {
            while(t > *value && *--t && isspace(*t))
              *t = '\0';
          }

          D(DBF_MAIL, "value='%s' %ld", *value, valueLen);

          RETURN(p);
          return p;
        }
      }

      free(*name);
    }
  }

  *name = NULL;
  *value = NULL;

  RETURN(NULL);
  return NULL;
}

///
/// RE_ParseContentParameters
//  Parses parameters of Content-Type header field
enum parameterType { PT_CONTENTTYPE, PT_CONTENTDISPOSITION };

static void RE_ParseContentParameters(char *str, struct Part *rp, enum parameterType pType)
{
  char *p = str;
  char *s;
  int size = 0;

  ENTER();

  // scan for the real size of the content-type: value without the
  // corresponding parameters.
  while(*p != '\0')
  {
    if(isspace(*p) || *p == ';')
      break;
    else
      size++;

    p++;
  }

  // now we scan through the contentType spec and strip all spaces
  // in between until we reach a ";" which signals a next parameter.
  // we also make sure we just allocate memory for the content-type
  // as our ParseContentParameters() function will allocate the rest.
  if((s = malloc(size+1)) != NULL)
  {
    char *q = s;

    p=str;
    while(*p)
    {
      if(isspace(*p) || *p == ';')
        break;
      else
        *q++ = *p;

      p++;
    }

    *q = '\0';
  }

  // depending on the parameterType we
  // have to use different source string
  switch(pType)
  {
    case PT_CONTENTTYPE:
    {
      if(rp->ContentType != NULL)
        free(rp->ContentType);

      rp->ContentType = s;
    }
    break;

    case PT_CONTENTDISPOSITION:
    {
      if(rp->ContentDisposition != NULL)
        free(rp->ContentDisposition);

      rp->ContentDisposition = s;
    }
    break;
  }

  // if we have additional content parameters we go and
  // try to separate them accordingly.
  if(*p == ';' || isspace(*p))
  {
    char *next = ++p;
    char *attribute;
    char *value;

    // now we walk through our string by extracting each
    // content parameter/value combo in a while loop.
    while(*next != '\0' && (next = ExtractNextParam(next, &attribute, &value)) != NULL)
    {
      if(attribute != NULL && value != NULL)
      {
        // depending on the parameterType we
        // have to parse different parameters
        switch(pType)
        {
          case PT_CONTENTTYPE:
          {
            if(strncmp(attribute, "name", 4) == 0)
            {
              // we try to find out which encoding the
              // parameter is actually using by checking for
              // an asterisk sign (see: RFC 2231)
              if(attribute[4] == '*')
              {
                static struct codeset *nameCodeset = NULL;

                rfc2231_decode(&attribute[5], value, &rp->CParName, &nameCodeset);
              }
              else
              {
                // otherwise we keep the string as is, as the
                // MA_ReadHeader() function might already converted
                // an eventually existing RFC2047 encoding even if
                // that is normally just reserved for special header lines.
                // However, even modern mail clients seem to still encode
                // even MIME parameters with rfc2047.
                rp->CParName = value;
              }
            }
            else if(strncmp(attribute, "description", 11) == 0)
            {
              // we try to find out which encoding the
              // parameter is actually using by checking for
              // an asterisk sign (see: RFC 2231)
              if(attribute[11] == '*')
              {
                static struct codeset *descCodeset = NULL;

                rfc2231_decode(&attribute[12], value, &rp->CParDesc, &descCodeset);
              }
              else
              {
                // otherwise we keep the string as is, as the
                // MA_ReadHeader() function might already converted
                // an eventually existing RFC2047 encoding even if
                // that is normally just reserved for special header lines.
                // However, even modern mail clients seem to still encode
                // even MIME parameters with rfc2047.
                rp->CParDesc = value;
              }
            }
            else if(strcmp(attribute, "boundary") == 0)
            {
              if(rp->CParBndr != NULL)
                free(rp->CParBndr);
              rp->CParBndr = value;
            }
            else if(strcmp(attribute, "protocol") == 0)
            {
              if(rp->CParProt != NULL)
                free(rp->CParProt);
              rp->CParProt = value;
            }
            else if(strcmp(attribute, "report-type") == 0)
            {
              if(rp->CParRType != NULL)
                free(rp->CParRType);
              rp->CParRType = value;
            }
            else if(strcmp(attribute, "charset") == 0)
            {
              if(rp->CParCSet != NULL)
                free(rp->CParCSet);
              rp->CParCSet = value;
            }
            else
              free(value);
          }
          break;

          case PT_CONTENTDISPOSITION:
          {
            if(strncmp(attribute, "filename", 8) == 0)
            {
              // we try to find out which encoding the
              // parameter is actually using by checking for
              // an asterisk sign (see: RFC 2231)
              if(attribute[8] == '*')
              {
                static struct codeset *fileNameCodeset = NULL;
                rfc2231_decode(&attribute[9], value, &rp->CParFileName, &fileNameCodeset);
              }
              else
              {
                // otherwise we keep the string as is, as the
                // MA_ReadHeader() function might already converted
                // an eventually existing RFC2047 encoding even if
                // that is normally just reserved for special header lines.
                // However, even modern mail clients seem to still encode
                // even MIME parameters with rfc2047.
                rp->CParFileName = value;
              }
            }
            else
              free(value);
          }
          break;
        }

        free(attribute);
      }
      else
      {
        E(DBF_MIME, "couldn't extract a full parameter (%lx/%lx)", attribute, value);

        if(attribute)
          free(attribute);

        if(value)
          free(value);
      }
    }
  }

  LEAVE();
}
///
/// RE_ScanHeader
//  Parses the header of the message or of a message part
static BOOL RE_ScanHeader(struct Part *rp, FILE *in, FILE *out, enum ReadHeaderMode mode)
{
  struct MinNode *curNode;

  ENTER();

  // check if we already have a headerList and if so we clean it first
  if(rp->headerList != NULL)
    FreeHeaderList(rp->headerList);
  else
  {
    // we do not have any headerList yet so lets allocate a new one
    if((rp->headerList = calloc(1, sizeof(struct MinList))) == NULL)
    {
      RETURN(FALSE);
      return FALSE;
    }
  }

  // we read in the headers from our mail file
  if(MA_ReadHeader(rp->rmData->readFile, in, rp->headerList, mode) == FALSE)
  {
    if(out != NULL && hasFlag(rp->rmData->parseFlags, PM_QUIET) == FALSE)
    {
      if(mode == RHM_MAINHEADER)
        ER_NewError(tr(MSG_ER_MIME_ERROR), rp->rmData->readFile);
      else if(mode == RHM_SUBHEADER)
        ER_NewError(tr(MSG_ER_UNEXPECTED_MULTIPART_EOF), rp->rmData->readFile);
    }

    // clear the subheaders flag
    CLEAR_FLAG(rp->Flags, PFLAG_SUBHEADERS);

    RETURN(FALSE);
    return FALSE;
  }
  else
    SET_FLAG(rp->Flags, PFLAG_SUBHEADERS);

  // Now we process the read header to set all flags accordingly
  for(curNode = rp->headerList->mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
  {
    struct HeaderNode *hdrNode = (struct HeaderNode *)curNode;
    char *field = hdrNode->name;
    char *value = hdrNode->content;

    // if we have a fileoutput pointer lets write out the header immediatly
    if(out != NULL)
      fprintf(out, "%s: %s\n", field, value);

    if(stricmp(field, "content-type") == 0)
    {
      // we check whether we have a content-type value or not, because otherwise
      // we have to keep the default "text/plain" content-type value the
      // OpenNewPart() function sets
      if(value[0] != '\0')
        RE_ParseContentParameters(value, rp, PT_CONTENTTYPE);
      else
        W(DBF_MAIL, "Empty 'Content-Type' headerline found.. using default '%s'.", rp->ContentType);

      // check the alternative part status
      // and try to find out if this is the main alternative part
      // which we might show later
      if(isAlternativePart(rp) == TRUE && rp->Parent != NULL)
      {
        if(stricmp(rp->ContentType, "text/plain") == 0 ||
           rp->Parent->MainAltPart == NULL)
        {
          D(DBF_MAIL, "setting new main alternative part in parent #%ld [%lx] to #%ld [%lx]", rp->Parent->Nr, rp->Parent, rp->Nr, rp);

          rp->Parent->MainAltPart = rp;
        }
      }
    }
    else if(stricmp(field, "content-transfer-encoding") == 0)
    {
      char *p;
      char buf[SIZE_DEFAULT];

      strlcpy(p = buf, value, sizeof(buf));
      TrimEnd(p);

      // As the content-transfer-encoding field is mostly used in
      // attachment MIME fields, we first check for common attachement encodings
      if(strnicmp(p, "base64", 6) == 0)
        rp->EncodingCode = ENC_B64;
      else if(strnicmp(p, "quoted-printable", 16) == 0)
        rp->EncodingCode = ENC_QP;
      else if(strnicmp(p, "8bit", 4) == 0 || strnicmp(p, "8-bit", 5) == 0)
        rp->EncodingCode = ENC_8BIT;
      else if(strnicmp(p, "7bit", 4) == 0 || strnicmp(p, "7-bit", 5) == 0 ||
              strnicmp(p, "plain", 5) == 0 || strnicmp(p, "none", 4) == 0)
      {
        rp->EncodingCode = ENC_7BIT;
      }
      else if(strnicmp(p, "x-uue", 5) == 0)
        rp->EncodingCode = ENC_UUE;
      else if(strnicmp(p, "binary", 6) == 0)
        rp->EncodingCode = ENC_BIN;
      else
      {
        ER_NewError(tr(MSG_ER_UNKNOWN_MIME_ENCODING), p, rp->rmData->readFile);

        // set the default to ENC_7BIT
        rp->EncodingCode = ENC_7BIT;
      }
    }
    else if(stricmp(field, "content-description") == 0)
    {
      // we just copy the value here as the initial rfc2047 decoding
      // was done in MA_ReadHeader() already.
      strlcpy(rp->Description, value, sizeof(rp->Description));
    }
    else if(stricmp(field, "content-disposition") == 0)
    {
      RE_ParseContentParameters(value, rp, PT_CONTENTDISPOSITION);
    }
    else if(mode == RHM_MAINHEADER && stricmp(field, "mime-version") == 0)
    {
      // RFC 2049 requires a MIME coformance mail client
      // to have a "MIME-Version" header field within the main header
      // or otherwise the mail in not to be treated in anyway related
      // to the MIME standards.
      SET_FLAG(rp->Flags, PFLAG_MIME);
    }
  }

  // if this is a main header scan and if this main part
  // is non MIME conform, we have to force the encoding mode to
  // 7bit US-ASCII due to RFC2049 rules
  if(mode == RHM_MAINHEADER && isMIMEconform(rp) == FALSE)
  {
    W(DBF_MAIL, "found non-MIME conform mail, forcing encoding to 7bit");
    rp->EncodingCode = ENC_7BIT;
  }

  RETURN(TRUE);
  return TRUE;
}
///
/// RE_ConsumeRestOfPart
//  Processes body of a message part
static BOOL RE_ConsumeRestOfPart(FILE *in, FILE *out, const struct codeset *srcCodeset,
                                 const struct Part *rp, const BOOL allowAutoDetect)
{
  BOOL result = FALSE;

  ENTER();

  // check if an input file stream was given
  if(in != NULL)
  {
    char *buf = NULL;
    size_t buflen = 0;
    ssize_t curlen = 0;
    int boundaryLen = 0;
    int numLines = 0;
    BOOL skipCodesets = FALSE;

    // if a part was specified we go and extract some information from
    // it
    if(rp != NULL)
    {
      boundaryLen = strlen(rp->CParBndr);

      // if the encoding of this part is not 7bit or 8bit ascii we go
      // and set the srcCodeset = NULL so that no codesets.library stuff
      // gets activated.
      if(isDecoded(rp) == FALSE &&
         rp->EncodingCode != ENC_7BIT && rp->EncodingCode != ENC_8BIT)
      {
        skipCodesets = TRUE;
      }
    }

    // we process the file line-by-line, analyze it if it is between the boundary
    // do an eventually existing charset translation and write it out again.
    while((curlen = GetLine(&buf, &buflen, in)) >= 0)
    {
      // count number of lines
      numLines++;

      #if defined(DEBUG)
      if(curlen > 998) // CRLF has been stripped!
        W(DBF_MIME, "RFC2822 violation: line length %ld in MIME part found to be > 998 @ line %ld", curlen, numLines);
      #endif

      // first we check if we reached a MIME boundary yet.
      if(boundaryLen > 0 && curlen >= boundaryLen+2 &&
         buf[0] == '-' && buf[1] == '-' && strncmp(buf+2, rp->CParBndr, boundaryLen) == 0)
      {
        if(buf[boundaryLen+2] == '-' && buf[boundaryLen+3] == '-' &&
           buf[boundaryLen+4] == '\0')
        {
          D(DBF_MAIL, "found end boundary of MIME part");

          // we had success, so lets break out
          result = TRUE;
          break;
        }
        else
        {
          D(DBF_MAIL, "found start boundary, but didn't find end boundary");

          // no success, return FALSE
          break;
        }
      }

      // check if the data should be written out
      // immediately or not
      if(out != NULL)
      {
        // as we use GetLine() above (with no LF) we have to output a LF before we
        // go on. This will in fact strip the last newline right where the mime boundary
        // comes.
        if(numLines > 1 && fputc('\n', out) == EOF)
        {
          E(DBF_MAIL, "error during '\n' write operation! buf: (%ld) '%s'", curlen, buf);

          // no success, return false
          break;
        }

        // see if there is anything left to write
        if(curlen > 0)
        {
          // in case the user wants us to detect the correct cyrillic codeset
          // we do it now
          if(skipCodesets == FALSE &&
             C->DetectCyrillic == TRUE && allowAutoDetect == TRUE &&
             (srcCodeset == NULL || stricmp(srcCodeset->name, "utf-8") != 0))
          {
            struct codeset *cs = CodesetsFindBest(CSA_Source,         buf,
                                                  CSA_SourceLen,      curlen,
                                                  CSA_CodesetFamily,  CSV_CodesetFamily_Cyrillic,
                                                  TAG_DONE);

            if(cs != NULL && cs != srcCodeset)
              srcCodeset = cs;
          }

          // if this function was invoked with a source Codeset we have to make sure
          // we convert from the supplied source Codeset to our current local codeset with
          // help of the functions codesets.library provides.
          if(srcCodeset != NULL && skipCodesets == FALSE)
          {
            ULONG dstlen = 0;

            // convert from the srcCodeset to the destination one.
            char *str = CodesetsConvertStr(CSA_SourceCodeset,   srcCodeset,
                                           CSA_DestCodeset,     G->readCharset,
                                           CSA_Source,          buf,
                                           CSA_SourceLen,       curlen,
                                           CSA_DestLenPtr,      &dstlen,
                                           CSA_MapForeignChars, C->MapForeignChars,
                                           TAG_DONE);

            // check if operations succeeded
            if(str != NULL && dstlen > 0)
            {
              // now write back exactly the amount of bytes the CodesetsConvertStr()
              // function tells us.
              if(fwrite(str, dstlen, 1, out) <= 0)
              {
                E(DBF_MAIL, "error during write operation!");

                // no success, return false
                break;
              }

              CodesetsFreeA(str, NULL);

              // continue with next iteration
              continue;
            }
            else
              W(DBF_MAIL, "couldn't convert buf with CodesetsConvertStr(), %lx %ld %ld", str, dstlen, curlen);
          }

          // now write back exactly the same amount of bytes we read previously
          if(fwrite(buf, curlen, 1, out) <= 0)
          {
            E(DBF_MAIL, "error during write operation! buf: (%ld) '%s'", curlen, buf);

            // no success, return false
            break;
          }
        }
      }
    }

    // if we end up here because of a EOF we have to check
    // if there is still something in c and then write it into the out fh.
    if(result == FALSE &&
       curlen == -1 && feof(in) != 0)
    {
      result = TRUE;
    }

    // free the buffer allocated by getline()
    if(buf != NULL)
      free(buf);
  }

  RETURN(result);
  return result;
}
///
/// RE_DecodeStream
//  Decodes contents of a part
//  return 0 on error
//  return 1 on success (was decoded)
//  return 2 on success (no decode required, no data written to out)
//  return 3 on success (no decode required, data without headers written to out)
static int RE_DecodeStream(struct Part *rp, FILE *in, FILE *out)
{
  int decodeResult = 0;
  struct codeset *sourceCodeset = NULL;
  struct ReadMailData *rmData = rp->rmData;
  BOOL quietParsing = hasFlag(rmData->parseFlags, PM_QUIET);

  ENTER();

  // now we find out if we should decode charset aware. This means
  // that we make sure that we convert the text in "in" into our
  // local charset or not.
  if(rp->Nr != PART_RAW && isPrintable(rp) == TRUE && rp->CParCSet != NULL)
  {
    // now we check that the codeset of the mail part really
    // differs from the local one we are currently using
    if(stricmp(rp->CParCSet, strippedCharsetName(G->readCharset)) != 0)
    {
      D(DBF_MAIL, "found Part #%ld encoded in charset '%s' which is different than local one.", rp->Nr, rp->CParCSet);

      // try to obtain the source codeset from codesets.library
      // such that we can convert to our local charset accordingly.
      if((sourceCodeset = CodesetsFind(rp->CParCSet,
                                       CSA_CodesetList,       G->codesetsList,
                                       CSA_FallbackToDefault, FALSE,
                                       TAG_DONE)) == NULL)
      {
        W(DBF_MAIL, "the specified codeset '%s' wasn't found in codesets.library", rp->CParCSet);
      }
    }
  }

  // lets check if we got some encoding here and
  // if so we have to decode it immediatly
  switch(rp->EncodingCode)
  {
    // process a base64 decoding.
    case ENC_B64:
    {
      long decoded = base64decode_file(in, out, sourceCodeset, isPrintable(rp));
      D(DBF_MAIL, "base64 decoded %ld bytes of part %ld.", decoded, rp->Nr);

      if(decoded > 0)
        decodeResult = 1;
      else
      {
        switch(decoded)
        {
          case -1:
          {
            if(quietParsing == FALSE)
              ER_NewError(tr(MSG_ER_B64DEC_FILEIO), rp->Nr, rmData->readFile);
          }
          break;

          case -2:
          {
            if(quietParsing == FALSE)
              ER_NewError(tr(MSG_ER_B64DEC_WARN), rp->Nr, rmData->readFile);

            decodeResult = 1;
          }
          break;

          default:
          {
            if(quietParsing == FALSE)
              ER_NewError(tr(MSG_ER_B64DEC_UNEXP), rp->Nr, rmData->readFile);
          }
          break;
        }
      }
    }
    break;

    // process a Quoted-Printable decoding
    case ENC_QP:
    {
      long decoded = qpdecode_file(in, out, sourceCodeset);
      D(DBF_MAIL, "quoted-printable decoded %ld chars of part %ld.", decoded, rp->Nr);

      if(decoded >= 0)
        decodeResult = 1;
      else
      {
        switch(decoded)
        {
          case -1:
          {
            if(quietParsing == FALSE)
              ER_NewError(tr(MSG_ER_QPDEC_FILEIO), rp->Filename);
          }
          break;

          case -2:
          {
            if(quietParsing == FALSE)
              ER_NewError(tr(MSG_ER_QPDEC_UNEXP), rp->Filename);
          }
          break;

          case -3:
          {
            W(DBF_MAIL, "found an undecodeable qp char sequence. Warning the user.");

            if(quietParsing == FALSE)
              ER_NewError(tr(MSG_ER_QPDEC_WARN), rp->Filename);

            decodeResult = 1; // allow to save the resulting file
          }
          break;

          case -4:
          {
            W(DBF_MAIL, "found an invalid character during decoding. Warning the user.");

            if(quietParsing == FALSE)
              ER_NewError(tr(MSG_ER_QPDEC_CHAR), rp->Filename);

            decodeResult = 1; // allow to save the resulting file
          }
          break;

          default:
          {
            if(quietParsing == FALSE)
              ER_NewError(tr(MSG_ER_QPDEC_UNEXP), rp->Filename);
          }
          break;
        }
      }
    }
    break;

    // process UU-Encoded decoding
    case ENC_UUE:
    {
      long decoded = uudecode_file(in, out, sourceCodeset);
      D(DBF_MAIL, "UU decoded %ld chars of part %ld.", decoded, rp->Nr);

      if(decoded >= 0 &&
        RE_ConsumeRestOfPart(in, NULL, NULL, NULL, FALSE))
      {
        decodeResult = 1;
      }
      else
      {
        switch(decoded)
        {
          case -1:
          {
            if(quietParsing == FALSE)
              ER_NewError(tr(MSG_ER_UNEXPECTED_UUE_EOF), rp->Filename);
          }
          break;

          case -2:
          {
            if(quietParsing == FALSE)
              ER_NewError(tr(MSG_ER_UUDEC_TAGMISS), rp->Filename, "begin");
          }
          break;

          case -3:
          {
            if(quietParsing == FALSE)
              ER_NewError(tr(MSG_ER_INVALID_UUE_LENGTH), 0, rp->Filename);
          }
          break;

          case -4:
          {
            if(quietParsing == FALSE)
              ER_NewError(tr(MSG_ER_UUDEC_CHECKSUM), rp->Filename);

            decodeResult = 1; // allow to save the resulting file
          }
          break;

          case -5:
          {
            if(quietParsing == FALSE)
              ER_NewError(tr(MSG_ER_UUDEC_CORRUPT), rp->Filename);
          }
          break;

          case -6:
          {
            if(quietParsing == FALSE)
              ER_NewError(tr(MSG_ER_UUDEC_TAGMISS), rp->Filename, "end");

            decodeResult = 1; // allow to save the resulting file
          }
          break;

          default:
          {
            if(quietParsing == FALSE)
              ER_NewError(tr(MSG_ER_UNEXPECTED_UUE_EOF), rp->Filename);
          }
        }
      }
    }
    break;

    default:
    {
      if(sourceCodeset != NULL || C->DetectCyrillic)
      {
        if(RE_ConsumeRestOfPart(in, out, sourceCodeset, NULL, TRUE))
          decodeResult = 1;
      }
      else if(hasSubHeaders(rp))
      {
        if(CopyFile(NULL, out, NULL, in))
          decodeResult = 3;
      }
      else
        decodeResult = 2;
    }
  }

  RETURN(decodeResult);
  return decodeResult;
}
///
/// RE_OpenNewPart
//  Adds a new entry to the message part list
static FILE *RE_OpenNewPart(struct ReadMailData *rmData,
                            struct Part **new,
                            struct Part *prev,
                            struct Part *first)
{
  FILE *fp = NULL;
  struct Part *newPart;

  ENTER();

  if((newPart = calloc(1, sizeof(*newPart))) != NULL)
  {
    char file[SIZE_FILE];

    if(prev != NULL)
    {
      // link in the new Part
      newPart->Prev = prev;
      prev->Next = newPart;
      newPart->Nr = prev->Nr+1;
    }

    if(first != NULL && strnicmp(first->ContentType, "multipart", 9) != 0)
    {
      newPart->ContentType = strdup(first->ContentType);
      newPart->CParCSet = first->CParCSet ? strdup(first->CParCSet) : NULL;
      newPart->EncodingCode = first->EncodingCode;
    }
    else
    {
      newPart->ContentType = strdup("text/plain");
      newPart->EncodingCode = ENC_7BIT;

      if(first != NULL && (isAlternativePart(first) ||
         (first->ContentType[9] != '\0' && strnicmp(&first->ContentType[10], "alternative", 11) == 0)))
      {
        SET_FLAG(newPart->Flags, PFLAG_ALTPART);
      }
    }

    // make sure to set the MIME conforming flag in
    // case the first one got that flag as well
    if(first != NULL && isMIMEconform(first))
      SET_FLAG(newPart->Flags, PFLAG_MIME);

    // make sure we make the hierarchy clear
    newPart->Parent = first;

    // copy the boundary specification
    newPart->CParBndr = strdup(first ? first->CParBndr : (prev ? prev->CParBndr : ""));

    newPart->rmData = rmData;
    snprintf(file, sizeof(file), "YAMr%08x-p%d.txt", (unsigned int)rmData->uniqueID, newPart->Nr);
    AddPath(newPart->Filename, C->TempDir, file, sizeof(newPart->Filename));

    D(DBF_MAIL, "New Part #%ld [%lx]", newPart->Nr, newPart);
    D(DBF_MAIL, "  IsAltPart..: %ld",  isAlternativePart(newPart));
    D(DBF_MAIL, "  Filename...: [%s]", newPart->Filename);
    D(DBF_MAIL, "  Nextptr....: %lx",  newPart->Next);
    D(DBF_MAIL, "  Prevptr....: %lx",  newPart->Prev);
    D(DBF_MAIL, "  Parentptr..: %lx",  newPart->Parent);
    D(DBF_MAIL, "  MainAltPart: %lx",  newPart->MainAltPart);

    if((fp = fopen(newPart->Filename, "w")) != NULL)
    {
      setvbuf(fp, NULL, _IOFBF, SIZE_FILEBUF);
    }
    else
    {
      // opening the file failed, so we return failure
      free(newPart);
      newPart = NULL;
    }
  }

  *new = newPart;
  if(newPart == NULL)
    E(DBF_MAIL, "Error: Couldn't create a new Part!");

  RETURN(fp);
  return fp;
}
///
/// RE_UndoPart
//  Removes an entry from the message part list
static void RE_UndoPart(struct Part *rp)
{
  struct Part *trp = rp;

  ENTER();

  D(DBF_MAIL, "Undoing part #%ld [%lx]", rp->Nr, rp);

  // lets delete the file first so that we can cleanly "undo" the part
  DeleteFile(rp->Filename);

  // we only iterate through our partlist if there is
  // a next item, if not we can simply relink it
  if(trp->Next != NULL)
  {
    // if we remove a part from the part list we have to take
    // care of the part index number aswell. So all following
    // parts have to be descreased somehow by one.
    //
    // p2->p3->p4->p5
    do
    {
      // use the next element as the current trp
      trp = trp->Next;

      // decrease the part number aswell
      trp->Nr--;

      // Now we also have to rename the temporary filename also
      Rename(trp->Filename, trp->Prev->Filename);

    }
    while(trp->Next != NULL);

    // now go from the end to the start again and copy
    // the filenames strings as we couldn`t do that in the previous
    // loop also
    //
    // p5->p4->p3->p2
    do
    {
      // iterate backwards
      trp = trp->Prev;

      // now copy the filename string
      strlcpy(trp->Next->Filename, trp->Filename, sizeof(trp->Next->Filename));

    }
    while(trp->Prev != NULL && trp != rp);
  }

  // relink the partlist
  if(rp->Prev != NULL)
    rp->Prev->Next = rp->Next;
  if(rp->Next != NULL)
    rp->Next->Prev = rp->Prev;

  // free an eventually existing headerList
  if(rp->headerList != NULL)
  {
    FreeHeaderList(rp->headerList);
    free(rp->headerList);
  }

  // free some string buffers
  free(rp->ContentType);
  free(rp->ContentDisposition);

  // free all the CPar structue members
  if(rp->CParName != NULL)
    free(rp->CParName);
  if(rp->CParFileName != NULL)
    free(rp->CParFileName);
  if(rp->CParBndr != NULL)
    free(rp->CParBndr);
  if(rp->CParProt != NULL)
    free(rp->CParProt);
  if(rp->CParDesc != NULL)
    free(rp->CParDesc);
  if(rp->CParRType != NULL)
    free(rp->CParRType);
  if(rp->CParCSet != NULL)
    free(rp->CParCSet);

  // now we check whether the readMailData letterPartNum has to be decreased to
  // point to the correct letterPart number again
  if(rp->rmData != NULL)
  {
    struct ReadMailData *rmData = rp->rmData;

    // if the letterPart is that what is was removed from our part list
    // we have to find out which one is the new letterPart indeed
    if(rmData->letterPartNum == rp->Nr)
    {
      rmData->letterPartNum = 0;

      for(trp = rmData->firstPart; trp; trp = trp->Next)
      {
        if(trp->Nr > PART_RAW && trp->Nr >= PART_LETTER &&
           isPrintable(trp))
        {
          rmData->letterPartNum = trp->Nr;
          break;
        }
      }
    }
    else if(rmData->letterPartNum > rp->Nr)
      rmData->letterPartNum--;

    D(DBF_MAIL, "new letterpart is #%ld", rmData->letterPartNum);
  }

  // now we also have to relink the Parent pointer
  for(trp=rp; trp; trp = trp->Next)
  {
    if(trp->Parent == rp)
      trp->Parent = rp->Parent;
  }

  // relink the MainAltPart pointer as well
  if(rp->Parent != NULL && rp->Parent->MainAltPart == NULL)
  {
    D(DBF_MAIL, "setting parent #%ld [%lx] MainAltPart to %lx", rp->Parent->Nr, rp->Parent, rp->MainAltPart);

    rp->Parent->MainAltPart = rp->MainAltPart;
  }

  // and last, but not least we free the part
  free(rp);

  LEAVE();
}
///
/// RE_RequiresSpecialHandling
//  Checks if part is PGP signed/encrypted or a MDN
static enum SMsgType RE_RequiresSpecialHandling(const struct Part *hrp)
{
  enum SMsgType res = SMT_NORMAL;

  ENTER();

  if(hrp->CParRType != NULL && stricmp(hrp->ContentType, "multipart/report") == 0 &&
                               stricmp(hrp->CParRType, "disposition-notification") == 0)
  {
    res = SMT_MDN;
  }
  else if(hrp->CParProt != NULL)
  {
    if(stricmp(hrp->ContentType, "multipart/signed") == 0 &&
       stricmp(hrp->CParProt, "application/pgp-signature") == 0)
    {
      res = SMT_SIGNED;
    }
    else if(stricmp(hrp->ContentType, "multipart/encrypted") == 0 &&
            stricmp(hrp->CParProt, "application/pgp-encrypted") == 0)
    {
      res = SMT_ENCRYPTED;
    }
  }

  RETURN(res);
  return res;
}
///
/// RE_SaveThisPart
//  Decides if the part should be kept in memory
static BOOL RE_SaveThisPart(struct Part *rp)
{
  BOOL result = FALSE;
  short parseFlags = rp->rmData->parseFlags;

  ENTER();

  if(hasFlag(parseFlags, PM_ALL))
    result = TRUE;
  else if(hasFlag(parseFlags, PM_TEXTS) && strnicmp(rp->ContentType, "text", 4) == 0)
    result = TRUE;

  RETURN(result);
  return result;
}
///
/// RE_SetPartInfo
//  Determines size and other information of a message part
static void RE_SetPartInfo(struct Part *rp)
{
  LONG size;

  ENTER();

  // get the part's filesize
  ObtainFileInfo(rp->Filename, FI_SIZE, &size);

  // let's calculate the partsize of an undecoded part, if this
  // part isn't the RAW part and we found a positive size.
  if(isDecoded(rp) == FALSE && rp->Nr > PART_RAW && size > 0)
  {
    // The following calculations are a very loosy estimation of the
    // real unencoded size of a MIME encoded part. Depending on how
    // a MIME part is encoded (uucode/base64/quoted-printable),
    // we can more or less estimate the size by the following
    // empiric formulars:
    //
    // BASE64/UUCODE: unencoded_size = encoded_size / 1.36
    // QP...........: unencoded_size = encoded_size / 1.06
    //
    // However, please note that we perform these calculations using
    // integer calculations (due to speed considerations) and we
    // are therefore making sure we first divide and then multiplicate
    // the value. Otherwise the calculation will overflow with sizes
    // >= 20MB. Of course, this leads to minor incorrect calculations
    // (precision loss) - but as we are estimating anyway this shouldn't
    // be a big deal. All in all we can only loose a max. of 99 bytes
    // precision which is really neglectable.
    switch(rp->EncodingCode)
    {
      case ENC_UUE:
      case ENC_B64:
      {
        size = (size / 136) * 100;
      }
      break;

      case ENC_QP:
      {
        size = (size / 106) * 100;
      }
      break;

      default:
        // nothing
      break;
    }
  }
  rp->Size = size;

  // if this part hasn`t got any name, we place the CParName as the normal name
  if(rp->Name[0] == '\0' && (rp->CParName != NULL || rp->CParFileName != NULL))
    strlcpy(rp->Name, (rp->CParName != NULL) ? rp->CParName : rp->CParFileName, sizeof(rp->Name));

  // let`s set if this is a printable (readable part)
  if(rp->Nr == PART_RAW ||
     strnicmp(rp->ContentType, "text", 4) == 0 ||
     strnicmp(rp->ContentType, "message", 7) == 0)
  {
    SET_FLAG(rp->Flags, PFLAG_PRINTABLE);
  }
  else
    CLEAR_FLAG(rp->Flags, PFLAG_PRINTABLE);

  // Now that we have defined that this part is printable we have
  // to check whether our readMailData structure already contains a reference
  // to the actual readable letterPart or not and if not we do make this
  // part the actual letterPart
  if((rp->rmData->letterPartNum < PART_LETTER ||
      (isAlternativePart(rp) == TRUE && rp->Parent != NULL && rp->Parent->MainAltPart == rp)) &&
      isPrintable(rp) &&
      rp->Nr >= PART_LETTER)
  {
    D(DBF_MAIL, "setting part #%ld as LETTERPART", rp->Nr);

    rp->rmData->letterPartNum = rp->Nr;

    SetComment(rp->Filename, tr(MSG_RE_Letter));
  }
  else if(rp->Nr == PART_RAW)
    SetComment(rp->Filename, tr(MSG_RE_Header));
  else
  {
    // if this is not a printable LETTER part or a RAW part we
    // write another comment
    SetComment(rp->Filename, *rp->Description ? rp->Description : (*rp->Name ? rp->Name : rp->ContentType));
  }

  LEAVE();
}
///
/// RE_ParseMessage (rec)
//  Parses a complete message
static struct Part *RE_ParseMessage(struct ReadMailData *rmData,
                                    FILE *in,
                                    char *fname,
                                    struct Part *hrp)
{
  ENTER();

  D(DBF_MAIL, "ParseMessage(): %08lx, %08lx, %08lx, %08lx", rmData, in, fname, hrp);

  if(in == NULL && fname != NULL)
  {
    if((in = fopen(fname, "r")) != NULL)
      setvbuf(in, NULL, _IOFBF, SIZE_FILEBUF);
  }

  if(in != NULL)
  {
    FILE *out = NULL;
    struct Part *rp;

    if(hrp == NULL)
    {
      if((out = RE_OpenNewPart(rmData, &hrp, NULL, NULL)) != NULL)
      {
        BOOL parse_ok = RE_ScanHeader(hrp, in, out, RHM_MAINHEADER);

        fclose(out);
        out = NULL;

        if(parse_ok == TRUE)
          RE_SetPartInfo(hrp);
      }
      else
        ER_NewError(tr(MSG_ER_CantCreateTempfile));
    }

    // check if we have a header part at all
    if(hrp != NULL)
    {
      // now we check if the header part tells us that
      // this mail is MIME conform or not and that it has a
      // boundary identifier
      if(isMIMEconform(hrp) == TRUE &&
         hrp->CParBndr != NULL && strnicmp(hrp->ContentType, "multipart", 9) == 0)
      {
        BOOL done = RE_ConsumeRestOfPart(in, NULL, NULL, hrp, FALSE);

        rp = hrp;

        while(done == FALSE)
        {
          struct Part *prev = rp;

          out = RE_OpenNewPart(rmData, &rp, prev, hrp);
          if(out == NULL)
            break;

          if(RE_ScanHeader(rp, in, out, RHM_SUBHEADER) == FALSE)
          {
            fclose(out);
            out = NULL;

            RE_UndoPart(rp);
            break;
          }

          if(strnicmp(rp->ContentType, "multipart", 9) == 0)
          {
            fclose(out);
            out = NULL;

            if(RE_ParseMessage(rmData, in, NULL, rp) != NULL)
            {
              // undo the dummy part
              RE_UndoPart(rp);

              // but consume all rest of the part
              done = RE_ConsumeRestOfPart(in, NULL, NULL, prev, FALSE);
              for(rp = prev; rp->Next; rp = rp->Next)
                ;
            }
          }
          else if(RE_SaveThisPart(rp) == TRUE || RE_RequiresSpecialHandling(hrp) == SMT_ENCRYPTED)
          {
            fputc('\n', out);
            done = RE_ConsumeRestOfPart(in, out, NULL, rp, FALSE);
            fclose(out);
            out = NULL;
            RE_SetPartInfo(rp);
          }
          else
          {
            fclose(out);
            out = NULL;
            done = RE_ConsumeRestOfPart(in, NULL, NULL, rp, FALSE);
            RE_UndoPart(rp);
            rp = prev;
          }
        }
      }
      else if((out = RE_OpenNewPart(rmData, &rp, hrp, hrp)) != NULL)
      {
        if(RE_SaveThisPart(rp) == TRUE || RE_RequiresSpecialHandling(hrp) == SMT_ENCRYPTED)
        {
          RE_ConsumeRestOfPart(in, out, NULL, NULL, FALSE);
          fclose(out);
          out = NULL;
          RE_SetPartInfo(rp);
        }
        else
        {
          fclose(out);
          out = NULL;
          RE_UndoPart(rp);
          RE_ConsumeRestOfPart(in, NULL, NULL, NULL, FALSE);
        }
      }
    }

    if(fname != NULL && in != NULL)
      fclose(in);

    if(out != NULL)
      fclose(out);
  }

  #if defined(DEBUG)
  if(fname != NULL)
  {
    struct Part *rp;

    D(DBF_MAIL, "HeaderPart: [%lx]", hrp);

    for(rp = hrp; rp; rp = rp->Next)
    {
      D(DBF_MAIL, "Part[%lx]:#%ld%s", rp, rp->Nr, rp->Nr == rp->rmData->letterPartNum ? ":LETTERPART" : "");
      D(DBF_MAIL, "  Name.......: [%s]", rp->Name);
      D(DBF_MAIL, "  ContentType: [%s]", rp->ContentType);
      D(DBF_MAIL, "  Boundary...: [%s]", rp->CParBndr ? rp->CParBndr : "NULL");
      D(DBF_MAIL, "  Charset....: [%s]", rp->CParCSet ? rp->CParCSet : "NULL");
      D(DBF_MAIL, "  IsAltPart..: %ld",  isAlternativePart(rp));
      D(DBF_MAIL, "  Printable..: %ld",  isPrintable(rp));
      D(DBF_MAIL, "  Encoding...: %ld",  rp->EncodingCode);
      D(DBF_MAIL, "  Filename...: [%s]", rp->Filename);
      D(DBF_MAIL, "  Size.......: %ld",  rp->Size);
      D(DBF_MAIL, "  Nextptr....: %lx",  rp->Next);
      D(DBF_MAIL, "  Prevptr....: %lx",  rp->Prev);
      D(DBF_MAIL, "  Parentptr..: %lx",  rp->Parent);
      D(DBF_MAIL, "  MainAltPart: %lx",  rp->MainAltPart);
      D(DBF_MAIL, "  headerList.: %lx",  rp->headerList);
    }
  }
  #endif

  RETURN(hrp);
  return hrp;
}
///
/// RE_DecodePart
//  Decodes a single message part
BOOL RE_DecodePart(struct Part *rp)
{
  ENTER();

  // it only makes sense to go on here if
  // the data wasn't decoded before.
  if(isDecoded(rp) == FALSE)
  {
    FILE *in;
    FILE *out;
    char filepath[SIZE_PATHFILE];
    char file[SIZE_FILE];
    char ext[SIZE_FILE];

    // start with an empty extension string
    ext[0] = '\0';

    if((in = fopen(rp->Filename, "r")) != NULL)
    {
      setvbuf(in, NULL, _IOFBF, SIZE_FILEBUF);

      // if this part has some headers, let`s skip them so that
      // we just decode the raw data.
      if(hasSubHeaders(rp) == TRUE)
      {
        char *buf = NULL;
        size_t buflen = 0;

        // we walk through the mail file until we reach an empty
        // line (with just a newline). This signals that the actual
        // message body starts there.
        while(getline(&buf, &buflen, in) > 0)
        {
          if(buf[0] == '\n' || (buf[0] == '\r' && buf[1] == '\n'))
            break;
        }

        if(buf != NULL)
          free(buf);

        // we only go on if we are not in an ferror() condition
        // as we shouldn`t have a EOF or real error here.
        if(ferror(in) || feof(in) || buflen == 0)
        {
          E(DBF_MAIL, "ferror()=%ld,feof()=%ld,buflen=%ld while parsing through PartHeader.", ferror(in), feof(in), buflen);

          fclose(in);

          RETURN(FALSE);
          return FALSE;
        }
      }

      // we try to get a proper file extension for our decoded part which we
      // in fact first try to get out of the user's MIME configuration.
      if(rp->Nr != PART_RAW)
      {
        // we first try to identify the file extension via the user
        // definable MIME type list configuration.
        if(rp->ContentType != NULL && rp->ContentType[0] != '\0')
        {
          struct MinNode *curNode;

          for(curNode = C->mimeTypeList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
          {
            struct MimeTypeNode *curType = (struct MimeTypeNode *)curNode;

            if(MatchNoCase(rp->ContentType, curType->ContentType))
            {
              char *s = TrimStart(curType->Extension);
              char *e;

              if((e = strpbrk(s, " |;,")) == NULL)
                e = s + strlen(s);

              // break out of the loop only if we found a non-empty extension string
              if(s[0] != '\0')
              {
                strlcpy(ext, s, MIN(sizeof(ext), (size_t)(e - s + 1)));

                D(DBF_MIME, "identified file extension '%s' via user MIME list entry '%s'", ext, curType->ContentType);

                break;
              }
            }
          }
        }

        // if we still don't have a valid extension, we try to take it from
        // and eventually existing part name
        if(ext[0] == '\0' && rp->Name[0] != '\0')
        {
          // get the file extension name
          stcgfe(ext, rp->Name);

          // if the file extension is longer than 5 chars lets use "tmp"
          if(strlen(ext) > 5)
            ext[0] = '\0';
          else
            D(DBF_MIME, "identified file extension '%s' via part name", ext);
        }

        // and last, but not least we try to identify the proper file extension
        // via our internal fallback mime type list
        if(ext[0] == '\0' &&
           rp->ContentType != NULL && rp->ContentType[0] != '\0')
        {
          int i;

          for(i=0; IntMimeTypeArray[i].ContentType != NULL; i++)
          {
            if(stricmp(rp->ContentType, IntMimeTypeArray[i].ContentType) == 0)
            {
              char *extension = (char *)IntMimeTypeArray[i].Extension;

              if(extension != NULL)
              {
                char *e;

                // search for a space
                if((e = strchr(extension, ' ')))
                {
                  strlcpy(ext, extension, (size_t)(e-extension+1));
                }
                else
                  strlcpy(ext, extension, sizeof(ext));

                D(DBF_MIME, "identified file extension '%s' via internal MIME list", ext);
              }
              else
                ext[0] = '\0';

              break;
            }
          }
        }
      }

      // lets generate the destination file name for the decoded part
      snprintf(file, sizeof(file), "YAMm%08x-p%d.%s", (unsigned int)rp->rmData->uniqueID, rp->Nr, ext[0] != '\0' ? ext : "tmp");
      AddPath(filepath, C->TempDir, file, sizeof(filepath));

      D(DBF_MAIL, "decoding '%s' to '%s'", rp->Filename, filepath);

      // now open the stream and decode it afterwards.
      if((out = fopen(filepath, "w")) != NULL)
      {
        int decodeResult;

        setvbuf(out, NULL, _IOFBF, SIZE_FILEBUF);

        // decode the stream
        decodeResult = RE_DecodeStream(rp, in, out);

        // close the streams
        fclose(out);
        fclose(in);

        // check if we were successfull in decoding the data.
        if(decodeResult > 0)
        {
          // if decodeResult == 2 then no decode was required and we just have to rename
          // the file
          if(decodeResult == 2)
          {
            D(DBF_MAIL, "no decode required, renaming file '%s' to '%s'", rp->Filename, filepath);

            DeleteFile(filepath); // delete the temporary file again.

            if(Rename(rp->Filename, filepath) == 0)
              CLEAR_FLAG(rp->Flags, PFLAG_DECODED);
            else
              SET_FLAG(rp->Flags, PFLAG_DECODED);
          }
          else
          {
            D(DBF_MAIL, "%s", decodeResult == 1 ? "successfully decoded" : "no decode required, did a raw copy");

            DeleteFile(rp->Filename);
            SET_FLAG(rp->Flags, PFLAG_DECODED);
          }

          strlcpy(rp->Filename, filepath, sizeof(rp->Filename));
          RE_SetPartInfo(rp);
        }
        else
        {
          E(DBF_MAIL, "error during RE_DecodeStream()");
          DeleteFile(filepath); // delete the temporary file again.
        }
      }
      else if((out = fopen(filepath, "r")) != NULL)
      {
        // if we couldn`t open that file for writing we check if it exists
        // and if so we use it because it is locked actually and already decoded
        fclose(out);
        fclose(in);
        DeleteFile(rp->Filename);
        strlcpy(rp->Filename, filepath, sizeof(rp->Filename));
        SET_FLAG(rp->Flags, PFLAG_DECODED);
        RE_SetPartInfo(rp);
      }
      else
        fclose(in);
    }
  }

  RETURN(isDecoded(rp));
  return isDecoded(rp);
}
///
/// RE_HandleSignedMessage
//  Handles a PGP signed message, checks validity of signature
static void RE_HandleSignedMessage(struct Part *frp)
{
  struct Part *rp[2];

  ENTER();

  if((rp[0] = frp->Next) != NULL)
  {
    if(G->PGPVersion != 0 && (rp[1] = rp[0]->Next) != NULL)
    {
      struct TempFile *tf;

      if((tf = OpenTempFile(NULL)) != NULL)
      {
        int error;
        char options[SIZE_LARGE];

        // flag the mail as having a PGP signature within the MIME encoding
        SET_FLAG(frp->rmData->signedFlags, PGPS_MIME);

        ConvertCRLF(rp[0]->Filename, tf->Filename, TRUE);
        snprintf(options, sizeof(options), (G->PGPVersion == 5) ? "%s -o %s +batchmode=1 +force +language=us" : "%s %s +bat +f +lang=en", rp[1]->Filename, tf->Filename);
        error = PGPCommand((G->PGPVersion == 5) ? "pgpv": "pgp", options, NOERRORS|KEEPLOG);
        if(error > 0)
          SET_FLAG(frp->rmData->signedFlags, PGPS_BADSIG);

        if(error >= 0)
          RE_GetSigFromLog(frp->rmData, NULL);

        CloseTempFile(tf);
      }
    }
    RE_DecodePart(rp[0]);
  }

  LEAVE();
}
///
/// RE_DecryptPGP
//  Decrypts a PGP encrypted file
static int RE_DecryptPGP(struct ReadMailData *rmData, char *src)
{
  int error;
  char orcpt[SIZE_ADDRESS];

  ENTER();

  orcpt[0] = '\0';
  PGPGetPassPhrase();

  if(G->PGPVersion == 5)
  {
    char fname[SIZE_PATHFILE];
    char options[SIZE_LARGE];

    snprintf(fname, sizeof(fname), "%s.asc", src);
    Rename(src, fname);
    snprintf(options, sizeof(options), "%s +batchmode=1 +force +language=us", fname);
    error = PGPCommand("pgpv", options, KEEPLOG|NOERRORS);
    RE_GetSigFromLog(rmData, orcpt);
    if(orcpt[0] != '\0')
      error = 2;

    DeleteFile(fname);
  }
  else
  {
    char options[SIZE_LARGE];

    snprintf(options, sizeof(options), "%s +bat +f +lang=en", src);
    error = PGPCommand("pgp", options, KEEPLOG|NOERRORS);
    RE_GetSigFromLog(rmData, NULL);
  }

  PGPClearPassPhrase(error < 0 || error > 1);
  if(error < 0 || error > 1)
  {
    FILE *fh;

    if((fh = fopen(src, "w")) != NULL)
    {
      fputs(tr(MSG_RE_PGPNotAllowed), fh);

      if(G->PGPVersion == 5 && orcpt[0] != '\0')
        fprintf(fh, tr(MSG_RE_MsgReadOnly), orcpt);

      fclose(fh);
    }
  }

  RETURN(error);
  return error;
}
///
/// RE_HandleEncryptedMessage
//  Handles a PGP encryped message
static void RE_HandleEncryptedMessage(struct Part *frp)
{
  struct Part *warnPart;
  struct Part *encrPart;

  ENTER();

  // if we find a warning and a encryption part we start decrypting
  if((warnPart = frp->Next) != NULL && (encrPart = warnPart->Next) != NULL)
  {
    struct TempFile *tf;

    if((tf = OpenTempFile("w")) != NULL)
    {
      // first we copy our encrypted part because the DecryptPGP()
      // function will overwrite it
      if(CopyFile(NULL, tf->FP, encrPart->Filename, NULL))
      {
        int decryptResult;

        fclose(tf->FP);
        tf->FP = NULL;

        decryptResult = RE_DecryptPGP(frp->rmData, tf->Filename);

        if(decryptResult == 1 || decryptResult == 0)
        {
          FILE *in;

          if(decryptResult == 0)
            SET_FLAG(frp->rmData->signedFlags, PGPS_MIME);

          SET_FLAG(frp->rmData->encryptionFlags, PGPE_MIME);

          // if DecryptPGP() returns with 0 everything worked perfectly and we can
          // convert & copy our decrypted file over the encrypted part
          if(ConvertCRLF(tf->Filename, warnPart->Filename, FALSE))
          {
            if((in = fopen(warnPart->Filename, "r")) != NULL)
            {
              setvbuf(in, NULL, _IOFBF, SIZE_FILEBUF);

              if(warnPart->ContentType)
                free(warnPart->ContentType);

              warnPart->ContentType = strdup("text/plain");
              SET_FLAG(warnPart->Flags, PFLAG_PRINTABLE);
              warnPart->EncodingCode = ENC_7BIT;
              *warnPart->Description = '\0';
              RE_ScanHeader(warnPart, in, NULL, RHM_MAINHEADER);
              fclose(in);

              CLEAR_FLAG(warnPart->Flags, PFLAG_DECODED);
              RE_DecodePart(warnPart);
              RE_UndoPart(encrPart); // undo the encrypted part because we have a decrypted now.
            }
          }
        }
        else
        {
          // if we end up here the DecryptPGP returned an error an
          // we have to put this error in place were the nonlocalized version is right now.
          if(CopyFile(warnPart->Filename, NULL, tf->Filename, NULL))
          {
            if(warnPart->ContentType)
              free(warnPart->ContentType);

            warnPart->ContentType = strdup("text/plain");
            SET_FLAG(warnPart->Flags, PFLAG_PRINTABLE);
            warnPart->EncodingCode = ENC_7BIT;
            *warnPart->Description = '\0';
            SET_FLAG(warnPart->Flags, PFLAG_DECODED);
            CLEAR_FLAG(warnPart->Flags, PFLAG_SUBHEADERS);
          }
        }
      }

      CloseTempFile(tf);
    }
  }

  LEAVE();
}
///
/// RE_LoadMessagePart
//  Decodes a single message part
static void RE_LoadMessagePart(struct ReadMailData *rmData, struct Part *part)
{
  ENTER();

  switch(RE_RequiresSpecialHandling(part))
  {

    case SMT_SIGNED:
      RE_HandleSignedMessage(part);
    break;

    case SMT_ENCRYPTED:
      RE_HandleEncryptedMessage(part);
    break;

    case SMT_MDN:
    {
      if(RE_HandleMDNReport(part))
        break;
    }
    // continue

    case SMT_NORMAL:
    {
      struct Part *rp;

      for(rp = part->Next; rp; rp = rp->Next)
      {
        if(stricmp(rp->ContentType, "application/pgp-keys") == 0)
          rmData->hasPGPKey = TRUE;
        else if(rp->Nr == PART_RAW || rp->Nr == rmData->letterPartNum ||
                (isPrintable(rp) && C->DisplayAllTexts))
        {
          RE_DecodePart(rp);
        }
      }
    }
  }

  LEAVE();
}
///
/// RE_LoadMessage
// Function that preparses a mail for either direct display in a read mail group
// or for background parsing (for Arexx and stuff)
BOOL RE_LoadMessage(struct ReadMailData *rmData)
{
  struct Mail *mail = rmData->mail;
  struct Folder *folder = mail->Folder;
  struct Part *part;
  BOOL result = FALSE;


  ENTER();

  if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
    BusyText(tr(MSG_BusyReading), "");

  // with each new LoadMessage() we set a new uniqueID
  // to our ReadMailData structure (this is required for
  // the tempfilename generation)
  rmData->uniqueID = GetUniqueID();

  // here we read in the mail in our read mail group
  GetMailFile(rmData->readFile, folder, mail);

  // check whether the folder of the mail is using XPK and if so we
  // unpack it to a temporarly file
  if(isVirtualMail(mail) == FALSE &&
     isXPKFolder(folder))
  {
    char tmpFile[SIZE_PATHFILE];

    if(StartUnpack(rmData->readFile, tmpFile, folder) == FALSE)
    {
      if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
        BusyEnd();

      RETURN(FALSE);
      return FALSE;
    }

    strlcpy(rmData->readFile, tmpFile, sizeof(rmData->readFile));
  }

  if((part = rmData->firstPart = RE_ParseMessage(rmData, NULL, rmData->readFile, NULL)) != NULL)
  {
    int i;

    RE_LoadMessagePart(rmData, part);

    for(i = 0; part; i++, part = part->Next)
    {
      if(part->Nr != i)
      {
        char tmpFile[SIZE_PATHFILE];
        char file[SIZE_FILE];

        part->Nr = i;
        snprintf(file, sizeof(file), "YAMm%08x-p%d%s", (unsigned int)rmData->uniqueID, i, strchr(part->Filename, '.'));
        AddPath(tmpFile, C->TempDir, file, sizeof(tmpFile));

        D(DBF_MAIL, "renaming '%s' to '%s'", part->Filename, tmpFile);

        RenameFile(part->Filename, tmpFile);
        strlcpy(part->Filename, tmpFile, sizeof(part->Filename));
      }
    }

    if(i > 0)
      result = TRUE;

    // now we do a check because there might be mails with no actual
    // readable letterPart - however, they just have one part and it
    // is just a binary attachment (perhaps a jpg). For these mails we do
    // have to do things a bit different and still flag them as being a
    // multipart mail so that an attachment icon is shown.
    if(rmData->letterPartNum == 0 && i == 2)
    {
      // we copy some meta information from the firstPart to
      // the actual part containing the attachment.
      struct Part *firstPart = rmData->firstPart;
      struct Part *attachPart = firstPart->Next;

      strlcpy(attachPart->Name, firstPart->Name, sizeof(attachPart->Name));
      strlcpy(attachPart->Description, firstPart->Description, sizeof(attachPart->Description));

      if(attachPart->CParFileName)
        free(attachPart->CParFileName);

      attachPart->CParFileName = firstPart->CParFileName ? strdup(firstPart->CParFileName) : NULL;

      // in case the mail is already flagged as a
      // multipart mail we don't have to go on...
      if(isMultiPartMail(mail) == FALSE)
      {
        // set the MultiPart-Mixed flag
        SET_FLAG(mail->mflags, MFLAG_MP_MIXED);

        // if the mail is no virtual mail we can also
        // refresh the maillist depending information
        if(!isVirtualMail(mail))
        {
          SET_FLAG(mail->Folder->Flags, FOFL_MODIFY);  // flag folder as modified
          DoMethod(G->MA->GUI.PG_MAILLIST, MUIM_MainMailListGroup_RedrawMail, mail);
        }
      }
    }
  }

  if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
    BusyEnd();

  RETURN(result);
  return result;
}

///
/// RE_ReadInMessage
//  Reads a message into a dynamic buffer and returns this buffer.
//  The text returned should *NOT* contain any MUI specific escape sequences, as
//  we will later parse the buffer again before we put it into the texteditor. So no deeep lexical analysis
//  are necessary here.
char *RE_ReadInMessage(struct ReadMailData *rmData, enum ReadInMode mode)
{
  struct Part *first;
  struct Part *last;
  struct Part *part;
  struct Part *uup = NULL;
  char *cmsg;
  int totsize, len;

  ENTER();

  D(DBF_MAIL, "rmData: 0x%08lx, mode: %ld", rmData, mode);

  // save exit conditions
  if(rmData == NULL || (first = rmData->firstPart) == NULL)
  {
    RETURN(NULL);
    return NULL;
  }

  SHOWVALUE(DBF_MAIL, rmData->letterPartNum);

  // first we precalculate the size of the final buffer where the message text will be put in
  for(totsize = 1000, part = first; part; part = part->Next)
  {
    // in non-READ mode (Reply, Forward etc) we do have to count only the sizes of
    // the RAW and LetterPart
    if(mode != RIM_READ && part->Nr != PART_RAW && part->Nr != rmData->letterPartNum)
      continue;

    if(isDecoded(part) || part->Nr == PART_RAW)
      totsize += part->Size;
    else
      totsize += 200;
  }

  // then we generate our final buffer for the message
  if((cmsg = calloc(len=(totsize*3)/2, sizeof(char))) != NULL)
  {
    int wptr=0, prewptr;

    // if this function wasn`t called with QUIET we place a BusyText into the Main Window
    if(mode != RIM_QUIET)
      BusyText(tr(MSG_BusyDisplaying), "");

    // then we copy the first part (which is the header of the mail
    // into our final buffer because we don`t need to preparse it. However, we just
    // have to do it in RIM_PRINT mode because all other modes do take
    // respect of the headerList
    if(mode == RIM_PRINT)
    {
      FILE *fh;

      if((fh = fopen(first->Filename, "r")) != NULL)
      {
        size_t buflen = 0;
        char *buf = NULL;

        setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);

        while(getline(&buf, &buflen, fh) > 0)
          cmsg = AppendToBuffer(cmsg, &wptr, &len, buf);

        fclose(fh);

        if(buf != NULL)
          free(buf);

        cmsg = AppendToBuffer(cmsg, &wptr, &len, "\n");
      }
    }

    // Now we check every part of the message if it will be displayed in the
    // texteditor or not and if so we run the part through the lexer
    for(part = first->Next; part; part = part->Next)
    {
      BOOL dodisp = (part->Nr == PART_RAW || part->Nr == rmData->letterPartNum) ||
                    (isPrintable(part) && C->DisplayAllTexts == TRUE && isDecoded(part));

      // before we go on we check whether this is an alternative multipart
      // and if so we check that we only display the plain text one
      if(dodisp == TRUE)
      {
        if(C->DisplayAllAltPart == FALSE &&
           (isAlternativePart(part) == TRUE && part->Parent != NULL && part->Parent->MainAltPart != part))
        {
          D(DBF_MAIL, "flagging part #%ld as hidden.", part->Nr);

          dodisp = FALSE;
        }
      }

      prewptr = wptr;

      // if we are in READ mode and other parts than the LETTER part
      // should be displayed in the texteditor as well, we drop a simple separator bar with info.
      // This is used for attachments and here escape sequences are allowed as we don`t want them
      // to get stripped if the user selects "NoTextStyles"
      if(part->Nr != PART_RAW && part->Nr != rmData->letterPartNum)
      {
        if(mode != RIM_READ)
          continue;
        else if(dodisp == TRUE)
        {
          char buffer[SIZE_LARGE];

          // lets generate the separator bar.
          snprintf(buffer, sizeof(buffer), "\n\033c\033[s:18]%s%s:%s%s%s\n"
                                           "\033l\033b%s:\033n %s <%s>\n", rmData->useTextcolors ? "\033p[7]" : "",
                                                                           tr(MSG_MA_ATTACHMENT),
                                                                           *part->Name ? " " : "",
                                                                           part->Name,
                                                                           rmData->useTextcolors ? "\033p[0]" : "",
                                                                           tr(MSG_RE_ContentType),
                                                                           DescribeCT(part->ContentType),
                                                                           part->ContentType);

          cmsg = AppendToBuffer(cmsg, &wptr, &len, buffer);

          buffer[0] = '\0';
          if(part->Description[0] != '\0')
            snprintf(buffer, sizeof(buffer), "\033b%s:\033n %s\n", tr(MSG_RE_Description), part->Description);

          strlcat(buffer, "\033[s:2]\n", sizeof(buffer));
          cmsg = AppendToBuffer(cmsg, &wptr, &len, buffer);
        }
      }

      D(DBF_MAIL, "Checking if part #%ld [%ld] (%ld) should be displayed", part->Nr, part->Size, dodisp);

      // only continue of this part should be displayed
      // and is greater than zero, or else we don`t have
      // to parse anything at all.
      if(dodisp == TRUE && part->Size > 0)
      {
        FILE *fh;

        D(DBF_MAIL, "  adding text of [%s] to display", part->Filename);

        if((fh = fopen(part->Filename, "r")) != NULL)
        {
          char *msg;

          setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);

          if((msg = calloc((size_t)(part->Size+3), sizeof(char))) != NULL)
          {
            char *ptr;
            char *rptr;
            char *eolptr;
            int nread;
            BOOL signatureFound = FALSE;

            *msg = '\n';
            nread = fread(msg+1, 1, (size_t)(part->Size), fh);

            // lets check if an error or short item count occurred
            if(nread == 0 || nread != part->Size)
            {
              W(DBF_MAIL, "Warning: EOF or short item count detected: feof()=%ld ferror()=%ld", feof(fh), ferror(fh));

              // distinguish between EOF and error
              if(feof(fh) == 0 && ferror(fh) != 0)
              {
                // an error occurred, lets signal it by returning NULL
                E(DBF_MAIL, "ERROR occurred while reading at pos %ld of '%s'", ftell(fh), part->Filename);

                // cleanup and return NULL
                free(msg);
                fclose(fh);

                if(mode != RIM_QUIET)
                  BusyEnd();

                RETURN(NULL);
                return NULL;
              }

              // if we end up here it is "just" an EOF so lets put out
              // a warning and continue.
              W(DBF_MAIL, "Warning: EOF detected at pos %ld of '%s'", ftell(fh), part->Filename);
            }

            // now we analyze if that part of the mail text contains HTML
            // tags or not so that our HTML converter routines can be fired
            // up accordingly.
            if(C->ConvertHTML == TRUE && (mode == RIM_EDIT || mode == RIM_QUOTE || mode == RIM_READ || mode == RIM_FORWARD) &&
               part->ContentType != NULL && stricmp(part->ContentType, "text/html") == 0)
            {
              char *converted;

              D(DBF_MAIL, "converting HTMLized part #%ld to plain-text", part->Nr);

              // convert all HTML stuff to plain text
              converted = html2mail(msg+1);

              // free the old HTMLized message
              free(msg);

              // overwrite the old values
              nread = strlen(converted);
              msg = converted;

              rptr = msg;
            }
            else
              rptr = msg+1; // nothing serious happened so lets continue...

            // parse the message string
            while(*rptr != '\0')
            {
              BOOL newlineAtEnd;

              // lets get the first real line of the data and make sure to strip all
              // NUL bytes because otherwise we are not able to show the text.
              for(eolptr = rptr; *eolptr != '\n' && eolptr < msg+nread+1; eolptr++)
              {
                // strip null bytes that are in between the start and end of stream
                // here we simply exchange it by a space
                if(*eolptr == '\0')
                  *eolptr = ' ';
              }

              // check if we have a newline at the end or not
              newlineAtEnd = (*eolptr == '\n');

              // terminate the string right where we are (also eventually stripping
              // a newline
              *eolptr = '\0';

              // now that we have a full line we can check for inline stuff
              // like inline uuencode/pgp sections

/* UUenc */   if(mode != RIM_EDIT && mode != RIM_FORWARD && // in Edit&Forward mode we don't handle inlined UUencoded attachments.
                 strncmp(rptr, "begin ", 6) == 0 &&
                 rptr[6] >= '0' && rptr[6] <= '7' &&
                 rptr[7] >= '0' && rptr[7] <= '7' &&
                 rptr[8] >= '0' && rptr[8] <= '7')
              {
                FILE *outfh;
                char *nameptr = NULL;

                D(DBF_MAIL, "inline UUencoded passage found!");

                // now we have to get the filename off the 'begin' line
                // so that we can put our new part together
                if(rptr[9] == ' ' && rptr[10] != '\0')
                  nameptr = &rptr[10];

                // find the currently last part of the message to where we attach
                // the new part now
                for(last = first; last->Next; last = last->Next);

                // then create the new part to which we will put our uudecoded
                // data
                if((outfh = RE_OpenNewPart(rmData, &uup, last, first)) != NULL)
                {
                  char *endptr = rptr+strlen(rptr)+1;
                  long old_pos;

                  // prepare our part META data and fake the new part as being
                  // a application/octet-stream part as we don't know if it
                  // is some text or something else.
                  if(uup->ContentType)
                    free(uup->ContentType);

                  uup->ContentType = strdup("application/octet-stream");
                  strlcpy(uup->Description, tr(MSG_RE_UUencodedFile), sizeof(uup->Description));

                  if(nameptr)
                    strlcpy(uup->Name, nameptr, sizeof(uup->Name));

                  // save the old position of our input file position so that
                  // we can set it back later on
                  old_pos = ftell(fh);

                  // then let us seek to the position where we found the starting
                  // "begin" indicator
                  if(old_pos >= 0 &&
                     fseek(fh, rptr-msg-1, SEEK_SET) == 0)
                  {
                    // now that we are on the correct position, we
                    // call the uudecoding function accordingly.
                    long decoded = uudecode_file(fh, outfh, NULL); // no translation table
                    D(DBF_MAIL, "UU decoded %ld chars of part %ld.", decoded, uup->Nr);

                    if(decoded >= 0)
                      SET_FLAG(uup->Flags, PFLAG_DECODED);
                    else
                    {
                      switch(decoded)
                      {
                        case -1:
                        {
                          if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
                            ER_NewError(tr(MSG_ER_UNEXPECTED_UUE_EOF), uup->Filename);
                        }
                        break;

                        case -2:
                        {
                          if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
                            ER_NewError(tr(MSG_ER_UUDEC_TAGMISS), uup->Filename, "begin");
                        }
                        break;

                        case -3:
                        {
                          if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
                            ER_NewError(tr(MSG_ER_INVALID_UUE_LENGTH), 0, uup->Filename);
                        }
                        break;

                        case -4:
                        {
                          if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
                            ER_NewError(tr(MSG_ER_UUDEC_CHECKSUM), uup->Filename);

                          SET_FLAG(uup->Flags, PFLAG_DECODED); // allow to save the resulting file
                        }
                        break;

                        case -5:
                        {
                          if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
                            ER_NewError(tr(MSG_ER_UUDEC_CORRUPT), uup->Filename);
                        }
                        break;

                        case -6:
                        {
                          if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
                            ER_NewError(tr(MSG_ER_UUDEC_TAGMISS), uup->Filename, "end");

                          SET_FLAG(uup->Flags, PFLAG_DECODED); // allow to save the resulting file
                        }
                        break;

                        default:
                        {
                          if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
                            ER_NewError(tr(MSG_ER_UNEXPECTED_UUE_EOF), uup->Filename);
                        }
                        break;
                      }
                    }
                  }

                  // set back the old position to the filehandle
                  fseek(fh, old_pos, SEEK_SET);

                  // close our part filehandle
                  fclose(outfh);

                  // refresh the partinfo
                  RE_SetPartInfo(uup);

                  // if everything was fine we try to find the end marker
                  if(isDecoded(uup) == TRUE)
                  {
                    // unfortunatly we have to find our ending "end" line now
                    // with an expensive string function. But this shouldn't be
                    // a problem as inline uuencoded parts are very rare today.
                    while((endptr = strstr(endptr, "\nend")) != '\0')
                    {
                      endptr += 4; // point to the char after end

                      // skip eventually existing whitespaces
                      while(*endptr == ' ')
                        endptr++;

                      // now check if the terminating char is a newline or not
                      if(*endptr == '\n')
                        break;
                    }

                    // check if we found the terminating "end" line or not
                    if(endptr != NULL)
                    {
                      // then starting from the next line there should be the "size" line
                      if(*(++endptr) != '\0' && strncmp(endptr, "size ", 5) == 0)
                      {
                        int expsize = atoi(&endptr[5]);

                        if(uup->Size != expsize)
                        {
                          if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
                            ER_NewError(tr(MSG_ER_UUSize), uup->Size, expsize);
                        }
                      }
                    }
                    else
                    {
                      if(hasFlag(rmData->parseFlags, PM_QUIET) == FALSE)
                        ER_NewError(tr(MSG_ER_UUDEC_TAGMISS), uup->Filename, "end");

                      endptr = rptr;
                    }
                  }
                  else
                    endptr = rptr;

                  // find the end of the line
                  for(eolptr = endptr; *eolptr && *eolptr != '\n'; eolptr++);

                  // terminate the end
                  *eolptr = '\0';
                }
                else
                  ER_NewError(tr(MSG_ER_CantCreateTempfile));
              }
/* PGP msg */ else if(strncmp(rptr, "-----BEGIN PGP MESSAGE", 21) == 0)
              {
                struct TempFile *tf;
                D(DBF_MAIL, "inline PGP encrypted message found");

                if((tf = OpenTempFile("w")) != NULL)
                {
                  *eolptr = '\n';
                  for(ptr=eolptr+1; *ptr; ptr++)
                  {
                    if(!strncmp(ptr, "-----END PGP MESSAGE", 19)) break;

                    while(*ptr && *ptr != '\n') ptr++;
                  }

                  while(*ptr && *ptr != '\n') ptr++;

                  eolptr = ptr++;
                  fwrite(rptr, 1, (size_t)(ptr-rptr), tf->FP);
                  fclose(tf->FP);
                  tf->FP = NULL;

                  D(DBF_MAIL, "decrypting");

                  if(RE_DecryptPGP(rmData, tf->Filename) == 0)
                  {
                    // flag the mail as having a inline PGP signature
                    SET_FLAG(rmData->signedFlags, PGPS_OLD);

                    // make sure that the mail is flaged as signed
                    if(!isMP_SignedMail(rmData->mail))
                    {
                      SET_FLAG(rmData->mail->mflags, MFLAG_MP_SIGNED);

                      // flag folder as modified
                      if(rmData->mail->Folder)
                        SET_FLAG(rmData->mail->Folder->Flags, FOFL_MODIFY);
                    }
                  }

                  if((tf->FP = fopen(tf->Filename, "r")))
                  {
                    char *buf = NULL;
                    size_t buflen = 0;

                    setvbuf(tf->FP, NULL, _IOFBF, SIZE_FILEBUF);

                    D(DBF_MAIL, "decrypted message follows:");

                    while(getline(&buf, &buflen, tf->FP) > 0)
                    {
                      rptr = buf;
                      D(DBF_MAIL, "%s", buf);
                      cmsg = AppendToBuffer(cmsg, &wptr, &len, buf);
                    }

                    if(buf != NULL)
                      free(buf);
                  }

                  CloseTempFile(tf);
                }

                // flag the mail as being inline PGP encrypted
                SET_FLAG(rmData->encryptionFlags, PGPE_OLD);

                // make sure that mail is flagged as crypted
                if(!isMP_CryptedMail(rmData->mail))
                {
                  SET_FLAG(rmData->mail->mflags, MFLAG_MP_CRYPT);

                  // flag folder as modified
                  if(rmData->mail->Folder)
                    SET_FLAG(rmData->mail->Folder->Flags, FOFL_MODIFY);
                }

                D(DBF_MAIL, "done with decryption");
              }
/* Signat. */ else if(signatureFound == FALSE && strcmp(rptr, "-- ") == 0)
              {
                if(mode == RIM_READ)
                {
                  if(C->SigSepLine == SST_BAR) // show seperator bar
                  {
                    if(rmData->useTextcolors == FALSE)
                      cmsg = AppendToBuffer(cmsg, &wptr, &len, "\033[s:2]");
                    else
                      cmsg = AppendToBuffer(cmsg, &wptr, &len, rptr);
                  }
                  else if(C->SigSepLine == SST_SKIP) // skip signature
                    break;
                  else if(C->SigSepLine == SST_DASH || // show "-- "
                          (C->SigSepLine == SST_BLANK && rmData->useTextcolors))
                  {
                    cmsg = AppendToBuffer(cmsg, &wptr, &len, rptr);
                  }

                  if(newlineAtEnd)
                    cmsg = AppendToBuffer(cmsg, &wptr, &len, "\n");
                }
                else if(mode == RIM_QUOTE && C->StripSignature)
                {
                  // if the user wants to strip the signature
                  // of a mail we go and strip all further text from
                  // here by breaking out of our current loop
                  break;
                }
                else if(mode == RIM_FORWARD)
                {
                  // in forward mode we make the signature seperator
                  // invalid by stripping the trailing space from it,
                  // just leaving the plain "--". This should prevent
                  // our signature stripping in forwarded mails from
                  // stripping at the wrong position.
                  if(newlineAtEnd)
                    cmsg = AppendToBuffer(cmsg, &wptr, &len, "--\n");
                  else
                    cmsg = AppendToBuffer(cmsg, &wptr, &len, "--");
                }
                else
                {
                  if(newlineAtEnd)
                    cmsg = AppendToBuffer(cmsg, &wptr, &len, "-- \n");
                  else
                    cmsg = AppendToBuffer(cmsg, &wptr, &len, "-- ");
                }

                signatureFound = TRUE;
              }
/* PGP sig */ else if(strncmp(rptr, "-----BEGIN PGP PUBLIC KEY BLOCK", 31) == 0)
              {
                rmData->hasPGPKey = TRUE;

                cmsg = AppendToBuffer(cmsg, &wptr, &len, rptr);

                if(newlineAtEnd)
                  cmsg = AppendToBuffer(cmsg, &wptr, &len, "\n");
              }
              else if(strncmp(rptr, "-----BEGIN PGP SIGNED MESSAGE", 29) == 0)
              {
                // flag the mail as having a inline PGP signature
                SET_FLAG(rmData->signedFlags, PGPS_OLD);

                if(!isMP_SignedMail(rmData->mail))
                {
                  SET_FLAG(rmData->mail->mflags, MFLAG_MP_SIGNED);

                  // flag folder as modified
                  if(rmData->mail->Folder)
                    SET_FLAG(rmData->mail->Folder->Flags, FOFL_MODIFY);
                }
              }
/* other */   else
              {
                cmsg = AppendToBuffer(cmsg, &wptr, &len, rptr);

                if(newlineAtEnd)
                  cmsg = AppendToBuffer(cmsg, &wptr, &len, "\n");
              }

              rptr = eolptr+1;
            }

            free(msg);
          }

          fclose(fh);
        }
      }
    }

    if(mode != RIM_QUIET)
      BusyEnd();
  }

  RETURN(cmsg);
  return cmsg;
}
///
/// RE_UpdateSenderInfo
//  Updates address book entry of sender
void RE_UpdateSenderInfo(struct ABEntry *old, struct ABEntry *new)
{
  BOOL changed = FALSE;

  ENTER();

  if(!*old->RealName && *new->RealName) { strlcpy(old->RealName, new->RealName, sizeof(old->RealName)); changed = TRUE; }
  if(!*old->Address  && *new->Address ) { strlcpy(old->Address,  new->Address,  sizeof(old->Address));  changed = TRUE; }
  if(!*old->Street   && *new->Street  ) { strlcpy(old->Street,   new->Street,   sizeof(old->Street));   changed = TRUE; }
  if(!*old->Country  && *new->Country ) { strlcpy(old->Country,  new->Country,  sizeof(old->Country));  changed = TRUE; }
  if(!*old->City     && *new->City    ) { strlcpy(old->City,     new->City,     sizeof(old->City));     changed = TRUE; }
  if(!*old->Phone    && *new->Phone   ) { strlcpy(old->Phone,    new->Phone,    sizeof(old->Phone));    changed = TRUE; }
  if(!*old->Homepage && *new->Homepage) { strlcpy(old->Homepage, new->Homepage, sizeof(old->Homepage)); changed = TRUE; }
  if(!old->BirthDay  && new->BirthDay ) { old->BirthDay = new->BirthDay; changed = TRUE; }

  if(changed)
    CallHookPkt(&AB_SaveABookHook, 0, 0);

  LEAVE();
}
///
/// RE_AddToAddrbook
//  Adds sender to the address book
struct ABEntry *RE_AddToAddrbook(Object *win, struct ABEntry *templ)
{
  struct ABEntry *result = NULL;
  BOOL doit = FALSE;

  ENTER();

  switch(C->AddToAddrbook)
  {
    case 1:
    {
      if(!templ->Type)
        break;
    }
    // continue

    case 2:
    {
      char buf[SIZE_LARGE];
      char address[SIZE_LARGE];
      snprintf(buf, sizeof(buf), tr(MSG_RE_AddSender), BuildAddress(address, sizeof(address), templ->Address, templ->RealName));
      doit = MUI_Request(G->App, win, 0, NULL, tr(MSG_YesNoReq), buf);
    }
    break;

    case 3:
    {
      if(!templ->Type)
        break;
    }
    // continue

    case 4:
      doit = TRUE;
    break;
  }

  if(doit)
  {
    struct ABEntry ab_new;
    struct MUI_NListtree_TreeNode *tn = NULL;

    // first we check if the group for new entries already exists and if so
    // we add this address to this special group.
    if(C->NewAddrGroup[0])
    {
      tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_FindName, MUIV_NListtree_FindName_ListNode_Root, C->NewAddrGroup, MUIF_NONE);

      // only if the group doesn`t exist yet
      if(tn == NULL || ((struct ABEntry *)tn->tn_User)->Type != AET_GROUP)
      {
        memset(&ab_new, 0, sizeof(struct ABEntry));
        strlcpy(ab_new.Alias, C->NewAddrGroup, sizeof(ab_new.Alias));
        strlcpy(ab_new.Comment, tr(MSG_RE_NewGroupTitle), sizeof(ab_new.Comment));
        ab_new.Type = AET_GROUP;
        tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Insert, ab_new.Alias, &ab_new, MUIV_NListtree_Insert_ListNode_Root, MUIV_NListtree_Insert_PrevNode_Sorted, TNF_LIST);
      }
    }

    // then lets add the entry to the group that was perhaps
    // created previously.
    memset(&ab_new, 0, sizeof(struct ABEntry));
    ab_new.Type = AET_USER;
    RE_UpdateSenderInfo(&ab_new, templ);
    EA_SetDefaultAlias(&ab_new);

    tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_Insert, ab_new.Alias, &ab_new, tn ? tn : MUIV_NListtree_Insert_ListNode_Root, MUIV_NListtree_Insert_PrevNode_Sorted, MUIF_NONE);
    if(tn != NULL)
    {
      CallHookPkt(&AB_SaveABookHook, 0, 0);

      result = tn->tn_User;
    }
  }

  RETURN(result);
  return result;
}
///
/// RE_ClickedOnMessage
//  User clicked on a e-mail address
void RE_ClickedOnMessage(char *address)
{
  int l;

  ENTER();

  SHOWSTRING(DBF_MAIL, address);

  // just prevent something bad from happening.
  if(address != NULL && (l = strlen(address)) > 0)
  {
    char *p;
    char *body = NULL;
    char *subject = NULL;
    char *cc = NULL;
    char *bcc = NULL;
    char buf[SIZE_LARGE];
    struct ABEntry *ab = NULL;
    int hits;

    // now we check for additional options to the mailto: string (if it is one)
    if((p = strchr(address, '?')) != NULL)
      *p++ = '\0';

    while(p != NULL)
    {
      if(strnicmp(p, "subject=", 8) == 0)
        subject = &p[8];
      else if(strnicmp(p, "body=", 5) == 0)
        body = &p[5];
      else if(strnicmp(p, "cc=", 3) == 0)
        cc = &p[3];
      else if(strnicmp(p, "bcc=", 4) == 0)
        bcc = &p[4];

      if((p = strchr(p, '&')) != NULL)
        *p++ = '\0';

      // now we check if this "&" is because of a "&amp;" which is the HTML code
      // for a "&" - we only handle this code because handling ALL HTML code
      // would be too complicated right now. we will support that later anyway.
      if(p != NULL && strnicmp(p, "amp;", 4) == 0)
        p += 4;
    }

    // please note that afterwards we should normally transfer HTML specific codes
    // like &amp; %20 aso. because otherwise links like mailto:Bilbo%20Baggins%20&lt;bilbo@baggins.de&gt;
    // will not work... but this is stuff we can do in one of the next versions.

    // lets see if we have an entry for that in the Addressbook
    // and if so, we reuse it
    hits = AB_SearchEntry(address, ASM_ADDRESS|ASM_USER|ASM_LIST, &ab);

    snprintf(buf, sizeof(buf), tr(MSG_RE_SelectAddressReq), address);

    switch(MUI_Request(G->App, G->MA->GUI.WI, 0, NULL, tr(hits ? MSG_RE_SelectAddressEdit : MSG_RE_SelectAddressAdd), buf))
    {
      case 1:
      {
        struct WriteMailData *wmData;

        if((wmData = NewWriteMailWindow(NULL, 0)) != NULL)
        {
          if(hits > 0)
          {
            char addrStr[SIZE_LARGE];
            set(wmData->window, MUIA_WriteWindow_To, BuildAddress(addrStr, sizeof(addrStr), address, ab->RealName));
          }
          else
            set(wmData->window, MUIA_WriteWindow_To, address);

          if(subject != NULL)
            set(wmData->window, MUIA_WriteWindow_Subject, subject);

          if(body != NULL)
            set(wmData->window, MUIA_WriteWindow_MailBody, body);

          if(cc != NULL)
            set(wmData->window, MUIA_WriteWindow_Cc, cc);

          if(bcc != NULL)
            set(wmData->window, MUIA_WriteWindow_BCC, bcc);

          // set the active object of the window
          set(wmData->window, MUIA_WriteWindow_ActiveObject, MUIV_WriteWindow_ActiveObject_Subject);
        }
      }
      break;

      case 2:
      {
        int win;

        DoMethod(G->App, MUIM_CallHook, &AB_OpenHook, ABM_EDIT);

        if(hits != 0)
        {
          if((win = EA_Init(ab->Type, ab)) >= 0)
            EA_Setup(win, ab);
        }
        else
        {
          if((win = EA_Init(AET_USER, NULL)) >= 0)
            setstring(G->EA[win]->GUI.ST_ADDRESS, address);
        }
      }
      break;
    }
  }

  LEAVE();
}
///

/*** MDN management (RFC 3798) ***/
/// RE_SendMDN()
//  Creates and sends the message disposition notification
static void RE_SendMDN(const enum MDNMode mode,
                       const struct Mail *mail,
                       const struct Person *recipient,
                       const BOOL sendnow,
                       const BOOL autoAction,
                       const BOOL autoSend)
{
  static const char *const MDNMessage[2] =
  {
     "The message sent on %s to %s with subject \"%s\" has been displayed. This is no guarantee that the content has been read or understood.\n",
     "The message sent on %s to %s with subject \"%s\" has been deleted %s. The recipient may or may not have seen the message. The recipient may \"undelete\" the message at a later time and read the message.\n",
  };
  struct WritePart *p1;
  struct TempFile *tf1;

  ENTER();

  SHOWVALUE(DBF_MAIL, sendnow);

  p1 = NewMIMEpart(NULL);

  if(p1 && (tf1 = OpenTempFile("w")) != NULL)
  {
    char buf[SIZE_LINE];
    char disp[SIZE_DEFAULT];
    char date[64];
    char address[SIZE_LARGE];
    struct WritePart *p2;
    struct TempFile *tf2;

    // link the filename of our temp file to
    // the first part
    p1->Filename = tf1->Filename;

    // generate the first lines of our MDN
    DateStamp2RFCString(date, sizeof(date), &mail->Date, mail->tzone, TRUE);
    snprintf(disp, sizeof(disp), "%s%s", autoAction ? "automatic-action/" : "manual-action/",
                                         autoSend ? "MDN-sent-automatically; " : "MDN-sent-manually; ");

    switch(mode)
    {
      case MDN_MODE_DISPLAY:
      {
        strlcat(disp, "displayed", sizeof(disp));
        fprintf(tf1->FP, MDNMessage[0], date, BuildAddress(address, sizeof(address), mail->To.Address, mail->To.RealName), mail->Subject);
      }
      break;

      case MDN_MODE_DELETE:
      {
        strlcat(disp, "deleted", sizeof(disp));
        fprintf(tf1->FP, MDNMessage[1], date, BuildAddress(address, sizeof(address), mail->To.Address, mail->To.RealName), mail->Subject, autoAction ? "automatically" : "in response to a user command");
      }
      break;
    }

    // close the filehandle
    fclose(tf1->FP);
    tf1->FP = NULL;

    // make sure to word wrap at 72 chars
    SimpleWordWrap(tf1->Filename, 72);

    // open a new part and another temporary file
    p2 = p1->Next = NewMIMEpart(NULL);
    if(p2 && (tf2 = OpenTempFile("w")) != NULL)
    {
      struct ExtendedMail *email;

      if((email = MA_ExamineMail(mail->Folder, mail->MailFile, TRUE)) != NULL)
      {
        struct WritePart *p3;
        struct TempFile *tf3;

        p2->ContentType = "message/disposition-notification";
        p2->Filename = tf2->Filename;
        snprintf(buf, sizeof(buf), "%s; %s", C->SMTP_Domain, yamversion);
        EmitHeader(tf2->FP, "Reporting-UA", buf);
        if(email->OriginalRcpt.Address[0] != '\0')
        {
          snprintf(buf, sizeof(buf), "rfc822;%s", BuildAddress(address, sizeof(address), email->OriginalRcpt.Address, email->OriginalRcpt.RealName));
          EmitHeader(tf2->FP, "Original-Recipient", buf);
        }

        snprintf(buf, sizeof(buf), "rfc822;%s", BuildAddress(address, sizeof(address), C->EmailAddress, C->RealName));
        EmitHeader(tf2->FP, "Final-Recipient", buf);
        EmitHeader(tf2->FP, "Original-Message-ID", email->messageID);
        EmitHeader(tf2->FP, "Disposition", disp);

        // close the file handle
        fclose(tf2->FP);
        tf2->FP = NULL;

        // create another MIME part
        p3 = p2->Next = NewMIMEpart(NULL);
        MA_FreeEMailStruct(email);

        if(p3 && (tf3 = OpenTempFile("w")) != NULL)
        {
          char fullfile[SIZE_PATHFILE];
          char mfile[SIZE_MFILE];
          struct Compose comp;
          struct Folder *outfolder;

          p3->ContentType = "text/rfc822-headers";
          p3->Filename = tf3->Filename;
          if(StartUnpack(GetMailFile(NULL, mail->Folder, mail), fullfile, mail->Folder))
          {
            FILE *fh;

            // put all headers from the original mail into
            // our third MIME part
            if((fh = fopen(fullfile, "r")) != NULL)
            {
              char *buf = NULL;
              size_t buflen = 0;
              ssize_t curlen = 0;

              setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);

              while((curlen = getline(&buf, &buflen, fh)) > 0)
              {
                if(*buf == '\n' || *buf == '\r')
                  break;
                else
                  fwrite(buf, curlen, 1, tf3->FP);
              }

              fclose(fh);

              if(buf != NULL)
                free(buf);
            }

            FinishUnpack(fullfile);
          }

          // close the file handle
          fclose(tf3->FP);
          tf3->FP = NULL;

          // finally, we compose the MDN mail
          memset(&comp, 0, sizeof(struct Compose));
          comp.MailTo = StrBufCpy(comp.MailTo, BuildAddress(address, sizeof(address), recipient->Address, recipient->RealName));
          comp.Subject = buf;
          comp.GenerateMDN = TRUE;
          comp.FirstPart = p1;

          // create the subject
          switch(mode)
          {
            case MDN_MODE_DISPLAY:
              snprintf(buf, sizeof(buf), "Return Receipt (displayed) - %s", mail->Subject);
            break;

            case MDN_MODE_DELETE:
              snprintf(buf, sizeof(buf), "Return Receipt (deleted) - %s", mail->Subject);
            break;
          }

          outfolder = FO_GetFolderByType(FT_OUTGOING, NULL);
          if(outfolder != NULL && (comp.FH = fopen(MA_NewMailFile(outfolder, mfile), "w")) != NULL)
          {
            struct MailList *mlist;

            if((mlist = CreateMailList()) != NULL)
            {
              setvbuf(comp.FH, NULL, _IOFBF, SIZE_FILEBUF);

              WriteOutMessage(&comp);
              fclose(comp.FH);

              if((email = MA_ExamineMail(outfolder, mfile, TRUE)) != NULL)
              {
                struct Mail *mail;

                if((mail = AddMailToList(&email->Mail, outfolder)) != NULL)
                {
                  setStatusToQueued(mail);

                  // refresh the folder statistics before the transfer
                  DisplayStatistics(outfolder, TRUE);

                  AddNewMailNode(mlist, mail);
                }
                MA_FreeEMailStruct(email);
              }

              // in case the user wants to send the message
              // immediately we go and send it out
              if(sendnow == TRUE && mlist->count != 0 && G->TR == NULL)
                TR_ProcessSEND(mlist, autoSend ? SEND_ACTIVE_AUTO : SEND_ACTIVE_USER);

              // refresh the folder statistics after the transfer
              DisplayStatistics(outfolder, TRUE);

              DeleteMailList(mlist);
            }
          }
          else
            ER_NewError(tr(MSG_ER_CreateMailError));

          FreeStrBuf(comp.MailTo);
          CloseTempFile(tf3);
        }
      }

      CloseTempFile(tf2);
    }

    CloseTempFile(tf1);
  }

  if(p1)
    FreePartsList(p1);

  LEAVE();
}
///
/// RE_ProcessMDN()
//  Handles/Processes message disposition requests
//  returns TRUE if all further MDN requests should be ignored, else FALSE
BOOL RE_ProcessMDN(const enum MDNMode mode,
                   struct Mail *mail,
                   const BOOL multi,
                   const BOOL autoAction)
{
  BOOL ignoreall = FALSE;

  ENTER();

  // check if MDN replies are enabled at all and if
  // the mail isn't in the outgoing folder
  if(C->MDNEnabled == TRUE &&
     isOutgoingFolder(mail->Folder) == FALSE)
  {
    struct ExtendedMail *email;

    // now we examine the original mail and see
    // if we should process the MDN or not according to the user
    // preferences
    if((email = MA_ExamineMail(mail->Folder, mail->MailFile, TRUE)) != NULL)
    {
      // see if we found a Disposition-Notification-To address
      if(email->ReceiptTo.Address[0] != '\0')
      {
        BOOL retPathWarning = FALSE;
        enum MDNAction action = MDN_ACTION_ASK; // per default we ask

        // according to RFC 3798 section 2.1 an MDN should only be
        // automatically send in case the "Return-Path" address
        // matches the "Disposition-Notification-To" address. Also
        // a mail MUST contain a "Return-Path" specification or the
        // user must be asked.
        if(email->ReturnPath.Address[0] != '\0')
        {
          char *p;
          BOOL cont = TRUE;

          // before we check all MDN cases we go and see if they
          // aren't just all set to the same value
          if(C->MDN_NoRecipient == C->MDN_NoDomain &&
             C->MDN_NoRecipient == C->MDN_OnDelete &&
             C->MDN_NoRecipient == C->MDN_Other)
          {
            action = C->MDN_NoRecipient;
            cont = FALSE;

            D(DBF_MAIL, "All MDN actions have the same value, skip analysis");
          }

          // check that our address is either in the To:
          // or Cc of the MDN requesting mail
          if(cont)
          {
            BOOL found;

            // find out if our address is in the To
            found = (stricmp(mail->To.Address, C->EmailAddress) == 0);
            if(found == FALSE)
            {
              int i;

              for(i=0; i < email->NoSTo; i++)
              {
                struct Person *pe = &email->STo[i];

                if(stricmp(pe->Address, C->EmailAddress) == 0)
                {
                  found = TRUE;
                  break;
                }
              }
            }

            // find out if our address is in Cc
            if(found == FALSE)
            {
              int i;

              for(i=0; i < email->NoCC; i++)
              {
                struct Person *pe = &email->CC[i];

                if(stricmp(pe->Address, C->EmailAddress) == 0)
                {
                  found = TRUE;
                  break;
                }
              }
            }

            // in case we found that our address is NOT in the
            // To: or Cc: of the mail, we go and execute the user
            // defined action for the MDN processing
            if(found == FALSE)
            {
              action = C->MDN_NoRecipient;

              cont = FALSE;

              D(DBF_MAIL, "triggered MDN action %ld due to missing address in To/Cc", action);
            }
          }

          // try to find out if the sender is outside of the
          // domain of the current user
          if(cont && (p = strchr(C->EmailAddress, '@')))
          {
            BOOL outsideDomain = FALSE;
            int domainLen = strlen(p);
            int peLen = strlen(mail->From.Address);

            if(domainLen > peLen ||
               stricmp(&mail->From.Address[peLen-domainLen], p) != 0)
            {
              outsideDomain = TRUE;
            }
            else
            {
              int i;

              for(i=0; i < email->NoSFrom; i++)
              {
                struct Person *pe = &email->SFrom[i];
                int peLen = strlen(pe->Address);

                if(domainLen > peLen ||
                   stricmp(&pe->Address[peLen-domainLen], p) != 0)
                {
                  outsideDomain = TRUE;
                  break;
                }
              }
            }

            // in case we found that there is a sender in
            // the 'From:' which is outside our domain, we
            // process the action accordingly
            if(outsideDomain)
            {
              action = C->MDN_NoDomain;

              cont = FALSE;

              D(DBF_MAIL, "triggered MDN action %ld due to outsideDomain", action);
            }
          }

          // in case this is a delete operation we go and
          // use the defined action immediately
          if(cont && mode == MDN_MODE_DELETE)
          {
            action = C->MDN_OnDelete;

            cont = FALSE;

            D(DBF_MAIL, "triggered MDN action %ld for MODE_DELETE", action);
          }

          // if this no action was found we use the one the user
          // configured for "Other"
          if(cont)
          {
            action = C->MDN_Other;

            D(DBF_MAIL, "no other MDN action fired, firing default: %ld", action);
          }

          // now we check if the Return-Path matches the Disposition-Notification-To
          // address. According to RFC 3798 section 2.1 we have to do this comparison
          // partly case sensitive for the local part of the address
          p = strchr(email->ReturnPath.Address, '@');

          // compare the local part first
          if(p != NULL)
          {
            int plen = (p - email->ReturnPath.Address);

            // compare case sensitive
            retPathWarning = (strncmp(email->ReturnPath.Address, email->ReceiptTo.Address, plen) != 0);

            // compare the domain part case insensitive
            if(retPathWarning == FALSE)
              retPathWarning = (stricmp(++p, &email->ReceiptTo.Address[plen+1]) != 0);
          }
          else
          {
            // we only have a local part? compare case sensitive only
            retPathWarning = (strcmp(email->ReturnPath.Address, email->ReceiptTo.Address) != 0);
          }

          // make sure we ask the user in case the Return-Path
          // doesn't match the ReceiptTo address
          if(retPathWarning)
          {
            action = MDN_ACTION_ASK;

            W(DBF_MAIL, "ReturnPath (%s) doesn't match ReceiptTo (%s) address", email->ReturnPath.Address, email->ReceiptTo.Address);
          }
        }
        else
        {
          retPathWarning = TRUE;

          W(DBF_MAIL, "no 'Return-Path' found, asking user for MDN permission");
        }

        // now we process the MDN action and react accordingl
        switch(action)
        {
          case MDN_ACTION_IGNORE:
            D(DBF_MAIL, "no MDN wanted, skipping");
          break;

          case MDN_ACTION_SEND:
            RE_SendMDN(mode, mail, &email->ReceiptTo, TR_IsOnline(), autoAction, TRUE);
          break;

          case MDN_ACTION_QUEUED:
            RE_SendMDN(mode, mail, &email->ReceiptTo, FALSE, autoAction, TRUE);
          break;

          case MDN_ACTION_ASK:
          {
            char buttons[SIZE_DEFAULT*2];
            int answer;
            BOOL isonline = TR_IsOnline();

            D(DBF_MAIL, "asking user for MDN confirmation");

            // set up the possible answers for the MDN requester
            strlcpy(buttons, tr(MSG_RE_MDN_ACCEPT_LATER), sizeof(buttons));

            // in case the user is only we can ask him to send the MDN
            // immediately if wanted.
            if(isonline)
              snprintf(buttons, sizeof(buttons), "%s|%s", buttons, tr(MSG_RE_MDN_ACCEPT_NOW));

            // he can also ignore the MDN, if required
            snprintf(buttons, sizeof(buttons), "%s|%s", buttons, tr(MSG_RE_MDN_IGNORE));

            // in case we have multiple MDNs waiting we go and provide
            // an 'ignore all' answer as well
            if(multi)
              snprintf(buttons, sizeof(buttons), "%s|%s", buttons, tr(MSG_RE_MDN_IGNORE_ALL));

            // now ask the user
            answer = MUI_Request(G->App, G->MA->GUI.WI, 0, tr(MSG_MA_ConfirmReq), buttons, tr(MSG_RE_MDNReq));
            switch(answer)
            {
              // accept and send later
              case 1:
                RE_SendMDN(mode, mail, &email->ReceiptTo, FALSE, autoAction, FALSE);
              break;

              // accept and send now or ignore
              case 2:
              {
                if(isonline)
                  RE_SendMDN(mode, mail, &email->ReceiptTo, TRUE, autoAction, FALSE);
                else
                  D(DBF_MAIL, "user wants to ignore the MDN request");
              }
              break;

              case 0: // ESC and last button (ignore or ignoreall)
              case 3: // ignore or ignoreall
              case 4: // ignoreall
              {
                D(DBF_MAIL, "user wants to ignore the MDN request");
                ignoreall = (multi == TRUE);
              }
              break;
            }
          }
          break;
        }

        // we processed this mail so we can go and clear the
        // SENDMDN flag
        CLEAR_FLAG(mail->mflags, MFLAG_SENDMDN);
      }
      else
        W(DBF_MAIL, "no 'Disposition-Notification-To' found!");

      MA_FreeEMailStruct(email);
    }
  }
  else
    D(DBF_MAIL, "MDN answeres disabled");

  RETURN(ignoreall);
  return ignoreall;
}
///
/// RE_HandleMDNReport()
//  Translates a message disposition notification to readable text
static BOOL RE_HandleMDNReport(const struct Part *frp)
{
  BOOL result = FALSE;
  struct Part *rp[3];

  ENTER();

  // check if the message has at least 2 parts
  if((rp[0] = frp->Next) && (rp[1] = rp[0]->Next))
  {
    FILE *fh;
    const char *mode;
    char *type;
    char *msgdesc = AllocStrBuf(80);
    char disposition[SIZE_DEFAULT];
    char file[SIZE_FILE];
    char buf[SIZE_PATHFILE];
    int i;

    // pointer two third part
    rp[2] = rp[1]->Next;

    // clear the disposition
    disposition[0] = '\0';

    // we either iterate through 2 or 3 message
    // parts to collect all necessary information
    for(i=1; i < (rp[2] ? 3 : 2); i++)
    {
      // decode the part
      RE_DecodePart(rp[i]);

      // open the decoded part output
      if((fh = fopen(rp[i]->Filename, "r")) != NULL)
      {
        struct MinList *headerList;

        if((headerList = calloc(1, sizeof(struct MinList))) != NULL)
        {
          setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);

          // read in the header into the headerList
          MA_ReadHeader(frp->rmData->readFile, fh, headerList, RHM_SUBHEADER);
          fclose(fh);

          if(IsListEmpty((struct List *)headerList) == FALSE)
          {
            struct MinNode *curNode;

            for(curNode = headerList->mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
            {
              char buf[SIZE_LINE];
              struct HeaderNode *hdrNode = (struct HeaderNode *)curNode;
              char *field = hdrNode->name;
              char *value = hdrNode->content;
              const char *msg = NULL;

              if(!stricmp(field, "from"))
                msg = tr(MSG_RE_MDNFrom);
              else if(!stricmp(field, "to"))
                msg = tr(MSG_RE_MDNTo);
              else if(!stricmp(field, "subject"))
                msg = tr(MSG_RE_MDNSubject);
              else if(!stricmp(field, "original-message-id"))
                msg = tr(MSG_RE_MDNMessageID);
              else if(!stricmp(field, "date"))
                msg = tr(MSG_RE_MDNDate);
              else if(!stricmp(field, "original-recipient"))
                msg = tr(MSG_RE_MDNOrigRecpt);
              else if(!stricmp(field, "final-recipient"))
                msg = tr(MSG_RE_MDNFinalRecpt);
              else if(!stricmp(field, "disposition"))
                strlcpy(disposition, Trim(value), sizeof(disposition));

              if(msg)
              {
                snprintf(buf, sizeof(buf), "%s %s", msg, value);
                msgdesc = StrBufCat(msgdesc, buf);
              }
            }
          }

          FreeHeaderList(headerList);
          free(headerList);
        }
        else
          fclose(fh);
      }
    }

    // add a newline
    msgdesc = StrBufCat(msgdesc, "\n");

    // find out if the disposition was automatically
    // generated or via manual interaction
    if(!strnicmp(disposition, "manual-action", 13))
      mode = tr(MSG_RE_MDNmanual);
    else if(!strnicmp(disposition, "automatic-action", 16))
      mode = tr(MSG_RE_MDNauto);
    else
      mode = "";

    // get the pointer to MDN type
    if((type = strchr(disposition, ';')))
      type = Trim(++type);
    else
      type = disposition;

    // now we generate the translated MDN report
    snprintf(file, sizeof(file), "YAMm%08x-p%d.txt", (unsigned int)rp[0]->rmData->uniqueID, rp[0]->Nr);
    AddPath(buf, C->TempDir, file, sizeof(buf));

    D(DBF_MAIL, "creating MDN report in '%s'", buf);

    if((fh = fopen(buf, "w")))
    {
      if(stristr(type, "displayed"))
        fprintf(fh, tr(MSG_RE_MDNdisplay), msgdesc);
      else if(stristr(type, "deleted"))
        fprintf(fh, tr(MSG_RE_MDNdeleted), msgdesc, mode);
      else
        fprintf(fh, tr(MSG_RE_MDNunknown), msgdesc, type, mode);

      fclose(fh);

      // replace the original decoded part
      // message
      DeleteFile(rp[0]->Filename);
      strlcpy(rp[0]->Filename, buf, sizeof(rp[0]->Filename));
      SET_FLAG(rp[0]->Flags, PFLAG_DECODED);
      RE_SetPartInfo(rp[0]);

      result = TRUE;
    }

    FreeStrBuf(msgdesc);
  }

  RETURN(result);
  return result;
}
///

/*** GUI ***/
/// CreateReadWindow()
// Function that creates a new ReadWindow object and returns
// the referencing ReadMailData structure which was created
// during that process - or NULL if an error occurred.
struct ReadMailData *CreateReadWindow(BOOL forceNewWindow)
{
  Object *newReadWindow;

  ENTER();

  // if MultipleWindows support if off we try to reuse an already existing
  // readWindow
  if(forceNewWindow == FALSE &&
     C->MultipleReadWindows == FALSE &&
     IsListEmpty((struct List *)&G->readMailDataList) == FALSE)
  {
    struct MinNode *curNode = G->readMailDataList.mlh_Head;

    D(DBF_GUI, "No MultipleReadWindows support, trying to reuse a window.");

    // search through our ReadDataList
    for(; curNode->mln_Succ; curNode = curNode->mln_Succ)
    {
      struct ReadMailData *rmData = (struct ReadMailData *)curNode;

      if(rmData->readWindow != NULL)
      {
        RETURN(rmData);
        return rmData;
      }
    }
  }

  D(DBF_GUI, "Creating new Read Window.");

  // if we end up here we create a new ReadWindowObject
  newReadWindow = ReadWindowObject, End;
  if(newReadWindow)
  {
    // get the ReadMailData and check that it is the same like created
    struct ReadMailData *rmData = (struct ReadMailData *)xget(newReadWindow, MUIA_ReadWindow_ReadMailData);

    if(rmData && rmData->readWindow == newReadWindow)
    {
      D(DBF_GUI, "Read window created: 0x%08lx", rmData);

      RETURN(rmData);
      return rmData;
    }

    DoMethod(G->App, OM_REMMEMBER, newReadWindow);
    MUI_DisposeObject(newReadWindow);
  }

  E(DBF_GUI, "ERROR occurred during read Window creation!");

  RETURN(NULL);
  return NULL;
}

///

/*** ReadMailData ***/
/// AllocPrivateRMData()
//  Allocates resources for background message parsing
struct ReadMailData *AllocPrivateRMData(struct Mail *mail, short parseFlags)
{
  struct ReadMailData *rmData;

  ENTER();

  if((rmData = calloc(1, sizeof(struct ReadMailData))) != NULL)
  {
    rmData->mail = mail;
    rmData->parseFlags = parseFlags;

    if(RE_LoadMessage(rmData) == FALSE)
    {
      free(rmData);
      rmData = NULL;
    }
  }

  RETURN(rmData);
  return rmData;
}
///
/// FreePrivateRMData()
//  Frees resources used by background message parsing
void FreePrivateRMData(struct ReadMailData *rmData)
{
  ENTER();

  if(CleanupReadMailData(rmData, FALSE))
    free(rmData);

  LEAVE();
}
///
/// CleanupReadMailData()
// cleans/deletes all data of a readmaildata structure
BOOL CleanupReadMailData(struct ReadMailData *rmData, BOOL fullCleanup)
{
  struct Part *part;
  struct Part *next;
  struct Mail *mail;

  ENTER();

  SHOWVALUE(DBF_MAIL, rmData);
  SHOWVALUE(DBF_MAIL, fullCleanup);
  ASSERT(rmData != NULL);

  // safe some pointer in advance
  mail = rmData->mail;

  // check if we also have to close an existing read window
  // or not.
  if(fullCleanup == TRUE && rmData->readWindow != NULL)
  {
    D(DBF_GUI, "make sure the read window is closed");

    // make sure the window is really closed
    nnset(rmData->readWindow, MUIA_Window_Open, FALSE);

    // for other windows we have to save the GUI object weights
    // aswell
    if(rmData->readMailGroup != NULL)
    {
      G->Weights[2] = xget(rmData->readMailGroup, MUIA_ReadMailGroup_HGVertWeight);
      G->Weights[3] = xget(rmData->readMailGroup, MUIA_ReadMailGroup_TGVertWeight);
    }
  }

  // cleanup the parts and their temporarly files/memory areas
  for(part = rmData->firstPart; part != NULL; part = next)
  {
    next = part->Next;

    D(DBF_MAIL, "freeing mail part %08lx, next %08lx", part, next);

    if(part->Filename[0] != '\0')
    {
      if(DeleteFile(part->Filename) == 0)
        AddZombieFile(part->Filename);
    }

    if(part->headerList != NULL)
    {
      FreeHeaderList(part->headerList);
      free(part->headerList);
    }

    if(part->ContentType != NULL)
      free(part->ContentType);

    if(part->ContentDisposition != NULL)
      free(part->ContentDisposition);

    // free all the CPar structue members
    if(part->CParName != NULL)
      free(part->CParName);
    if(part->CParFileName != NULL)
      free(part->CParFileName);
    if(part->CParBndr != NULL)
      free(part->CParBndr);
    if(part->CParProt != NULL)
      free(part->CParProt);
    if(part->CParDesc != NULL)
      free(part->CParDesc);
    if(part->CParRType != NULL)
      free(part->CParRType);
    if(part->CParCSet != NULL)
      free(part->CParCSet);

    // just a paranoia cleanup
    memset(part, 0, sizeof(*part));

    // finally free that part structure itself
    free(part);
  }
  rmData->firstPart = NULL;

  SHOWVALUE(DBF_MAIL, rmData);

  // now clear some flags and stuff so that others may have a clean readmaildata
  // structure
  rmData->signedFlags = 0;
  rmData->encryptionFlags = 0;
  rmData->hasPGPKey = 0;
  rmData->letterPartNum = 0;

  SHOWVALUE(DBF_MAIL, rmData);
  SHOWPOINTER(DBF_MAIL, mail);
  D(DBF_MAIL, "isVirtual: %ld", mail != NULL ? isVirtualMail(mail) : FALSE);
  D(DBF_MAIL, "isXPK: %ld", mail != NULL && mail->Folder != NULL ? isXPKFolder(mail->Folder) : FALSE);

  // now we have to check whether there is a .unp (unpack) file and delete
  // it acoordingly (we can`t use the FinishUnpack() function because the
  // window still refers to the file which will prevent the deletion.
  if(mail != NULL && isVirtualMail(mail) == FALSE &&
     mail->Folder != NULL && isXPKFolder(mail->Folder))
  {
    char ext[SIZE_FILE];

    D(DBF_MAIL, "cleaning up unpacked mail file");

    stcgfe(ext, rmData->readFile);
    if(strcmp(ext, "unp") == 0)
    {
      if(DeleteFile(rmData->readFile) == 0)
        AddZombieFile(rmData->readFile);
    }
  }

  // if the caller wants to cleanup everything tidy we do it here or exit immediatly
  if(fullCleanup == TRUE)
  {
    D(DBF_MAIL, "doing a full cleanup");

    // close any opened temporary file
    if(rmData->tempFile != NULL)
    {
      D(DBF_MAIL, "closing tempfile");
      CloseTempFile(rmData->tempFile);
      rmData->tempFile = NULL;
    }

    // if the rmData carries a virtual mail we have to clear it
    // aswell
    if(mail != NULL && isVirtualMail(mail))
    {
      D(DBF_MAIL, "freeing virtual mail pointer");
      free(mail);
    }

    // set the mail pointer to NULL
    rmData->mail = NULL;

    // clean up the read window now
    if(rmData->readWindow != NULL)
    {
      D(DBF_GUI, "cleaning up readwindow");
      DoMethod(G->App, OM_REMMEMBER, rmData->readWindow);
      MUI_DisposeObject(rmData->readWindow);
      rmData->readWindow = NULL;
    }
  }
  else
    rmData->mail = NULL;

  // check if this rmData is the current active Rexx background
  // processing one and if so set the ptr to NULL to signal the rexx
  // commands that their active window was closed/disposed
  if(rmData == G->ActiveRexxRMData)
    G->ActiveRexxRMData = NULL;

  RETURN(TRUE);
  return TRUE;
}

///
/// FreeHeaderNode()
// free a single header node
void FreeHeaderNode(struct HeaderNode *hdrNode)
{
  ENTER();

  FreeStrBuf(hdrNode->name);
  FreeStrBuf(hdrNode->content);

  free(hdrNode);

  LEAVE();
}

///
/// FreeHeaderList()
// Free all items of an existing header list
void FreeHeaderList(struct MinList *headerList)
{
  ENTER();

  if(headerList != NULL && IsListEmpty((struct List *)headerList) == FALSE)
  {
    struct MinNode *curNode;

    // Now we process the read header to set all flags accordingly
    while((curNode = (struct MinNode *)RemHead((struct List *)headerList)) != NULL)
    {
      struct HeaderNode *hdrNode = (struct HeaderNode *)curNode;

      FreeHeaderNode(hdrNode);
    }
  }

  LEAVE();
}

///
/// GetReadMailData()
//  returns the ReadMailData of a mail if it exists
struct ReadMailData *GetReadMailData(const struct Mail *mail)
{
  struct ReadMailData *result = NULL;

  ENTER();

  if(IsListEmpty((struct List *)&G->readMailDataList) == FALSE)
  {
    // search through our ReadDataList
    struct MinNode *curNode;

    for(curNode = G->readMailDataList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
    {
      struct ReadMailData *rmData = (struct ReadMailData *)curNode;

      if(rmData->mail == mail)
      {
        result = rmData;
        break;
      }
    }
  }

  RETURN(result);
  return result;
}
///
/// UpdateReadMailDataStatus()
// triggers an update of certain GUI and non-GUI relevant status information
// according to a provided mail pointer. returns TRUE if mail was found
BOOL UpdateReadMailDataStatus(const struct Mail *mail)
{
  BOOL result = FALSE;

  ENTER();

  if(IsListEmpty((struct List *)&G->readMailDataList) == FALSE)
  {
    // search through our ReadDataList
    struct MinNode *curNode;

    for(curNode = G->readMailDataList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
    {
      struct ReadMailData *rmData = (struct ReadMailData *)curNode;

      if(rmData->mail == mail)
      {
        // update the status bar information
        if(rmData->readWindow != NULL)
          DoMethod(rmData->readWindow, MUIM_ReadWindow_UpdateStatusBar);

        result = TRUE;
      }
    }
  }

  RETURN(result);
  return result;
}
///

