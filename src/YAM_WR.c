/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 by Marcel Beck <mbeck@yam.ch>
 Copyright (C) 2000-2008 by YAM Open Source Team

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 YAM Official Support Site :  http://www.yam.ch/
 YAM Open Source project   :  http://sourceforge.net/projects/yamos/

 $Id$

***************************************************************************/

/***************************************************************************
 Module: Write
***************************************************************************/

#include <ctype.h>
#include <stdlib.h>

#include <clib/alib_protos.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/muimaster.h>

#include "extrasrc.h"

#include "YAM.h"
#include "YAM_addressbook.h"
#include "YAM_addressbookEntry.h"
#include "YAM_utilities.h"
#include "YAM_config.h"
#include "YAM_error.h"
#include "YAM_stringsizes.h"
#include "YAM_mainFolder.h"
#include "YAM_global.h"
#include "YAM_utilities.h"

#include "classes/Classes.h"
#include "mime/rfc2231.h"
#include "mime/rfc2047.h"
#include "mime/base64.h"
#include "mime/qprintable.h"
#include "mime/uucode.h"

#include "FileInfo.h"
#include "FolderList.h"
#include "Locale.h"
#include "MailList.h"
#include "MUIObjects.h"
#include "Requesters.h"

#include "Debug.h"

/* local structures */
struct ExpandTextData
{
  const char *      OS_Name;
  const char *      OS_Address;
  const char *      OM_Subject;
  struct DateStamp  OM_Date;
  int               OM_TimeZone;
  const char *      OM_MessageID;
  const char *      R_Name;
  const char *      R_Address;
  const char *      HeaderFile;
};

/**************************************************************************/

/*** Compose Message ***/
/// GetDateTime
//  Formats current date and time for Date header field
static char *GetDateTime(void)
{
  static char dt[SIZE_DEFAULT];

  ENTER();

  DateStamp2RFCString(dt, sizeof(dt), NULL, C->TimeZone + (C->DaylightSaving ? 60 : 0), FALSE);

  RETURN(dt);
  return dt;
}

///
/// NewMessageID
//  Creates a unique id, used for Message-ID header field
static char *NewMessageID(void)
{
  static char idbuf[SIZE_MSGID];
  ULONG seconds;
  struct DateStamp ds;

  ENTER();

  // lets calculate the seconds
  DateStamp(&ds);
  seconds = ds.ds_Days * 24 * 60 * 60 + ds.ds_Minute * 60; // seconds since 1-Jan-78

  // Here we try to generate a unique MessageID.
  // We try to be as much conform to the Recommandations for generating
  // unique Message IDs as we can: http://www.jwz.org/doc/mid.html
  snprintf(idbuf, sizeof(idbuf), "<%lx%lx.%lx@%s>", seconds, ds.ds_Tick, (ULONG)rand(), C->SMTP_Server);

  RETURN(idbuf);
  return idbuf;
}

///
/// NewBoundaryID
//  Creates a unique id, used for the MIME boundaries
static char *NewBoundaryID(void)
{
  static char idbuf[SIZE_MSGID];
  static int ctr = 0;

  ENTER();

  // Generate a unique Boundary ID which conforms to RFC 2045 and includes
  // a "=_" sequence to make it safe for quoted printable encoded parts
  snprintf(idbuf, sizeof(idbuf), "--=_BOUNDARY.%lx%lx.%02x", (ULONG)FindTask(NULL), (ULONG)rand(), ++ctr);

  RETURN(idbuf);
  return idbuf;
}

///
/// NewMIMEpart
//  Initializes a new message part
struct WritePart *NewMIMEpart(struct WriteMailData *wmData)
{
  struct WritePart *p;

  ENTER();

  if((p = calloc(1, sizeof(struct WritePart))) != NULL)
  {
    p->ContentType = "text/plain";
    p->EncType = ENC_7BIT;

    if(wmData != NULL)
    {
      p->Filename = wmData->filename;
      p->charset = wmData->charset;
    }
    else
      p->charset = G->localCharset;
  }
  else
    E(DBF_MAIL, "couldn't create new MIME part");

  RETURN(p);
  return p;
}

///
/// FreePartsList
//  Clears message parts and deletes temporary files
void FreePartsList(struct WritePart *p)
{
  struct WritePart *np;

  ENTER();

  for(; p; p = np)
  {
    np = p->Next;
    if(p->IsTemp)
      DeleteFile(p->Filename);

    free(p);
  }

  LEAVE();
}

///
/// HeaderFputs
// Outputs the value of a header line directly to the FILE pointer and handle
// RFC2231 complicant MIME parameter encoding, but also RFC2047 compliant
// MIME value encoding as well. As soon as "param" is set to NULL, RFC2047
// encoding will be used, otherwise RFC2231-based one.
static void HeaderFputs(FILE *fh, const char *s, const char *param, const int offset)
{
  BOOL doEncoding = FALSE;
  char *c = (char *)s;
  int paramLen = 0;

  ENTER();

  if(param != NULL)
    paramLen = strlen(param);

  // let us now search for any non-ascii compliant character aswell
  // as converting each character with the translation table
  while(*c != '\0')
  {
    // check for any non-ascii character or
    // if the string would be
    if(!isascii(*c) || iscntrl(*c))
    {
      doEncoding = TRUE;
      break;
    }

    // increment the pointer
    c++;
  }

  // in case we want to process a MIME parameter we have to
  // check other things as well.
  if(doEncoding == FALSE)
  {
    if(param != NULL)
    {
      if((c-s+1+paramLen+6) > 78)
        doEncoding = TRUE;
    }
    else
    {
      // we have to check for stray =? strings as we are
      // going to consider a rfc2047 encoding
      if((c = strstr(s, "=?")) != NULL && isascii(*(c+1)) &&
         (c == s || isspace(*(c-1))))
      {
        doEncoding = TRUE;
      }
    }

  }

  // if an encoding is required, we go and process it accordingly but
  // have to check whether we do rfc2047 or rfc2231 based encoding
  if(doEncoding == TRUE)
  {
    if(param != NULL)
    {
      // do the actual rfc2231 based MIME paramater encoding
      D(DBF_MAIL, "writing RFC2231 content '%s'='%s'", param, s);
      rfc2231_encode_file(fh, param, s);
    }
    else
    {
      // do the actual rfc2047 based encoding
      D(DBF_MAIL, "writing RFC2047 content '%s' with offset %ld", s, offset);
      rfc2047_encode_file(fh, s, offset);
    }
  }
  else if(param != NULL)
  {
    D(DBF_MAIL, "writing quoted content '%s'='%s'", param, s);
    // output the parameter name right before
    // the resulting parameter value
    fprintf(fh, "\n\t%s=\"%s\"", param, s);
  }
  else
  {
    size_t len = strlen(s);

    // there seems to be non "violating" characters in the string and
    // the resulting string will also be not > 78 chars in case we
    // have to encode a MIME parameter, so we go and output the source
    // string immediately
    D(DBF_MAIL, "writing plain content '%s'", s);

    // all we have to make sure now is that we don't write longer lines
    // than 78 chars or we fold them
    if(len >= (size_t)75-offset)
    {
      char *p = (char *)s;
      char *e = (char *)s;
      char *last_space = NULL;
      size_t c = offset;

      // start our search
      while(len > 0)
      {
        if(*e == ' ')
          last_space = e;

        // check if we need a newline and
        // if so we go and write out the last
        // stuff including a newline.
        if(c >= 75 && last_space != NULL)
        {
          fwrite(p, last_space-p, 1, fh);

          if(len > 1)
            fwrite("\n ", 2, 1, fh);

          p = last_space+1;
          c = e-p;
          last_space = NULL;
        }

        c++;
        e++;
        len--;
      }

      if(c > 0)
        fwrite(p, e-p, 1, fh);
    }
    else
      fwrite(s, len, 1, fh);
  }

  LEAVE();
}

///
/// EmitHeader
//  Outputs a complete header line
void EmitHeader(FILE *fh, const char *hdr, const char *body)
{
  int offset;

  ENTER();

  D(DBF_MAIL, "writing header '%s' with content '%s'", hdr, body);

  offset = fprintf(fh, "%s: ", hdr);
  HeaderFputs(fh, body, NULL, offset);
  fputc('\n', fh);

  LEAVE();
}

///
/// EmitRcptField
//  Outputs the value of a recipient header line, one entry per line
static void EmitRcptField(FILE *fh, const char *body)
{
  char *bodycpy;

  ENTER();

  if((bodycpy = strdup(body)) != NULL)
  {
    char *part = bodycpy;

    while(part != NULL)
    {
      char *next;

      if(*part == '\0')
        break;

      if((next = MyStrChr(part, ',')))
        *next++ = '\0';

      HeaderFputs(fh, Trim(part), NULL, 0);

      if((part = next))
        fputs(",\n\t", fh);
    }

    free(bodycpy);
  }

  LEAVE();
}

///
/// EmitRcptHeader
//  Outputs a complete recipient header line
static void EmitRcptHeader(FILE *fh, const char *hdr, const char *body)
{
  ENTER();

  fprintf(fh, "%s: ", hdr);
  EmitRcptField(fh, body ? body : "");
  fputc('\n', fh);

  LEAVE();
}

///
/// WriteContentTypeAndEncoding
//  Outputs content type header including parameters
static void WriteContentTypeAndEncoding(FILE *fh, struct WritePart *part)
{
  char *p;

  ENTER();

  // output the "Content-Type:
  fprintf(fh, "Content-Type: %s", part->ContentType);
  if(part->EncType != ENC_7BIT && strncmp(part->ContentType, "text/", 5) == 0)
    fprintf(fh, "; charset=%s", strippedCharsetName(part->charset));

  // output the "name" and Content-Disposition as well
  // as the "filename" parameter to the mail
  if((p = part->Name) && *p)
  {
    fputc(';', fh);
    HeaderFputs(fh, p, "name", 0); // output and do rfc2231 encoding
    fputs("\nContent-Disposition: attachment;", fh);
    HeaderFputs(fh, p, "filename", 0); // output and do rfc2231 encoding
  }
  fputc('\n', fh);

  // output the Content-Transfer-Encoding:
  if(part->EncType != ENC_7BIT)
  {
    const char *enc = "7bit";

    switch(part->EncType)
    {
      case ENC_B64:  enc = "base64"; break;
      case ENC_QP:   enc = "quoted-printable"; break;
      case ENC_UUE:  enc = "x-uue"; break;
      case ENC_8BIT: enc = "8bit"; break;
      case ENC_BIN:  enc = "binary"; break;
      case ENC_7BIT:
        // nothing
      break;
    }

    fprintf(fh, "Content-Transfer-Encoding: %s\n", enc);
  }

  // output the Content-Description if appropriate
  if((p = part->Description) != NULL && p[0] != '\0')
    EmitHeader(fh, "Content-Description", p);

  LEAVE();
}

///
/// WR_WriteUserInfo
//  Outputs X-SenderInfo header line
static void WR_WriteUserInfo(FILE *fh, char *from)
{
  struct ABEntry *ab = NULL;
  struct Person pers = { "", "" };

  ENTER();

  // Now we extract the real email from the address string
  if(*from)
    ExtractAddress(from, &pers);

  if(*(pers.Address) && AB_SearchEntry(pers.Address, ASM_ADDRESS|ASM_USER, &ab))
  {
    if(ab->Type != AET_USER)
      ab = NULL;
    else if(!*ab->Homepage && !*ab->Phone && !*ab->Street && !*ab->City && !*ab->Country && !ab->BirthDay)
      ab = NULL;
  }

  if(ab != NULL || C->MyPictureURL[0] != '\0')
  {
    fputs("X-SenderInfo: 1", fh);
    if(C->MyPictureURL[0] != '\0')
    {
      fputc(';', fh);
      HeaderFputs(fh, C->MyPictureURL, "picture", 0);
    }

    if(ab != NULL)
    {
      if(*ab->Homepage)
      {
        fputc(';', fh);
        HeaderFputs(fh, ab->Homepage, "homepage", 0);
      }
      if(*ab->Street)
      {
        fputc(';', fh);
        HeaderFputs(fh, ab->Street, "street", 0);
      }
      if(*ab->City)
      {
        fputc(';', fh);
        HeaderFputs(fh, ab->City, "city", 0);
      }
      if(*ab->Country)
      {
        fputc(';', fh);
        HeaderFputs(fh, ab->Country, "country", 0);
      }
      if(*ab->Phone)
      {
        fputc(';', fh);
        HeaderFputs(fh, ab->Phone, "phone", 0);
      }
      if(ab->BirthDay)
        fprintf(fh, ";\n\tdob=%ld", ab->BirthDay);
    }
    fputc('\n', fh);
  }

  LEAVE();
}
///
/// EncodePart
//  Encodes a message part
static BOOL EncodePart(FILE *ofh, const struct WritePart *part)
{
  BOOL result = FALSE;
  FILE *ifh;

  ENTER();

  if((ifh = fopen(part->Filename, "r")))
  {
    setvbuf(ifh, NULL, _IOFBF, SIZE_FILEBUF);

    switch(part->EncType)
    {
      case ENC_B64:
      {
        BOOL convLF = FALSE;

        // let us first check if we need to convert single LF to
        // CRLF to be somewhat portable.
        if(!strnicmp(part->ContentType, "text", 4)    ||
           !strnicmp(part->ContentType, "message", 7) ||
           !strnicmp(part->ContentType, "multipart", 9))
        {
          convLF = TRUE;
        }

        // then start base64 encoding the whole file.
        if(base64encode_file(ifh, ofh, convLF) > 0)
          result = TRUE;
        else
          ER_NewError(tr(MSG_ER_B64FILEENCODE), part->Filename);
      }
      break;

      case ENC_QP:
      {
        if(qpencode_file(ifh, ofh) >= 0)
          result = TRUE;
        else
          ER_NewError(tr(MSG_ER_QPFILEENCODE), part->Filename);
      }
      break;

      case ENC_UUE:
      {
        LONG size;

        ObtainFileInfo(part->Filename, FI_SIZE, &size);

        fprintf(ofh, "begin 644 %s\n", *part->Name ? part->Name : (char *)FilePart(part->Filename));

        if(uuencode_file(ifh, ofh) >= 0)
          result = TRUE;
        else
          ER_NewError(tr(MSG_ER_UUFILEENCODE), part->Filename);

        fprintf(ofh, "``\nend\nsize %ld\n", size);
      }
      break;

      default:
      {
        if(CopyFile(NULL, ofh, NULL, ifh) == TRUE)
          result = TRUE;
        else
          ER_NewError(tr(MSG_ER_FILEENCODE), part->Filename);
      }
      break;
    }

    fclose(ifh);
  }
  else
    ER_NewError(tr(MSG_ER_FILEENCODE), part->Filename);

  RETURN(result);
  return result;
}

///
/// WR_Anonymize
//  Inserts recipient header field for remailer service
static void WR_Anonymize(FILE *fh, char *body)
{
  char *ptr;

  ENTER();

  for(ptr = C->RMCommands; *ptr; ptr++)
  {
    if(ptr[0] == '\\' && ptr[1] == 'n')
    {
      ptr++;
      fputs("\n", fh);
    }
    else if(ptr[0] == '%' && ptr[1] == 's')
    {
      ptr++;
      EmitRcptField(fh, body);
    }
    else
      fputc(*ptr, fh);
  }
  fputs("\n", fh);

  LEAVE();
}

///
/// WR_GetPGPId
//  Gets PGP key id for a person
static char *WR_GetPGPId(struct Person *pe)
{
  char *pgpid = NULL;
  struct ABEntry *ab = NULL;

  ENTER();

  if(AB_SearchEntry(pe->RealName, ASM_REALNAME|ASM_USER, &ab) == 0)
  {
    AB_SearchEntry(pe->Address, ASM_ADDRESS|ASM_USER, &ab);
  }

  if(ab != NULL)
  {
    if(ab->PGPId[0] != '\0')
      pgpid = ab->PGPId;
  }

  RETURN(pgpid);
  return pgpid;
}
///
/// WR_GetPGPIds
//  Collects PGP key ids for all persons in a recipient field
static char *WR_GetPGPIds(char *source, char *ids)
{
  char *next;

  ENTER();

  for(; source; source = next)
  {
    char *pid;
    struct Person pe;

    if(source[0] == '\0')
      break;

    if((next = MyStrChr(source, ',')) != NULL)
      *next++ = 0;

    ExtractAddress(source, &pe);
    if((pid = WR_GetPGPId(&pe)) == NULL)
    {
      pid = pe.RealName[0] ? pe.RealName : pe.Address;
      ER_NewError(tr(MSG_ER_ErrorNoPGPId), source, pid);
    }
    ids = StrBufCat(ids, (G->PGPVersion == 5) ? "-r \"" : "\"");
    ids = StrBufCat(ids, pid);
    ids = StrBufCat(ids, "\" ");
  }

  RETURN(ids);
  return ids;
}
///
/// WR_Bounce
//  Bounce message: inserts resent-headers while copying the message
static BOOL WR_Bounce(FILE *fh, struct Compose *comp)
{
  BOOL result = FALSE;
  FILE *oldfh;

  ENTER();

  if(comp->refMail != NULL &&
     (oldfh = fopen(GetMailFile(NULL, NULL, comp->refMail), "r")))
  {
    char address[SIZE_LARGE];

    setvbuf(oldfh, NULL, _IOFBF, SIZE_FILEBUF);

    // now we add the "Resent-#?" type headers which are defined
    // by RFC2822 section 3.6.6. The RFC defined that these headers
    // should be added to the top of a message
    EmitHeader(fh, "Resent-From", BuildAddress(address, sizeof(address), C->EmailAddress, C->RealName));
    EmitHeader(fh, "Resent-Date", GetDateTime());
    EmitRcptHeader(fh, "Resent-To", comp->MailTo);
    EmitHeader(fh, "Resent-Message-ID", NewMessageID());

    // now we copy the rest of the message
    // directly from the file handlers
    result = CopyFile(NULL, fh, NULL, oldfh);

    fclose(oldfh);
  }

  RETURN(result);
  return result;
}

///
/// WR_SaveDec
//  Creates decrypted copy of a PGP encrypted message
static BOOL WR_SaveDec(FILE *fh, struct Compose *comp)
{
  char *mailfile;
  BOOL result = FALSE;

  ENTER();

  if(comp->refMail != NULL && (mailfile = GetMailFile(NULL, NULL, comp->refMail)))
  {
    char unpFile[SIZE_PATHFILE];
    BOOL xpkPacked = FALSE;
    FILE *oldfh;

    // we need to analyze if the folder we are reading this mail from
    // is encrypted or compressed and then first unpacking it to a temporary file
    if(isXPKFolder(comp->refMail->Folder))
    {
      // so, this mail seems to be packed, so we need to unpack it to a temporary file
      if(StartUnpack(mailfile, unpFile, comp->refMail->Folder) &&
         stricmp(mailfile, unpFile) != 0)
      {
        xpkPacked = TRUE;
      }
      else
      {
        RETURN(FALSE);
        return FALSE;
      }
    }

    if((oldfh = fopen(xpkPacked ? unpFile : mailfile, "r")))
    {
      BOOL infield = FALSE;
      char buf[SIZE_LINE];

      setvbuf(oldfh, NULL, _IOFBF, SIZE_FILEBUF);

      while(fgets(buf, SIZE_LINE, oldfh))
      {
        if(*buf == '\n')
        {
          fprintf(fh, "X-YAM-Decrypted: PGP; %s\n", GetDateTime());
          break;
        }

        if(!isspace(*buf))
        {
          infield = !strnicmp(buf, "content-type:", 13) ||
                    !strnicmp(buf, "content-transfer-encoding", 25) ||
                    !strnicmp(buf, "mime-version:", 13);
        }

        if(!infield)
          fputs(buf, fh);
      }

      fclose(oldfh);
      result = TRUE;
    }

    // if we temporary unpacked the file we delete it now
    if(xpkPacked)
      DeleteFile(unpFile);
  }

  RETURN(result);
  return result;
}

///
/// WR_EmitExtHeader
//  Outputs special X-YAM-Header lines to remember user-defined headers
static void WR_EmitExtHeader(FILE *fh, struct Compose *comp)
{
  ENTER();

  if(comp->ExtHeader[0] != '\0')
  {
    char *p;
    char ch = '\n';

    for(p = comp->ExtHeader; *p; ++p)
    {
      if(ch == '\n')
        fputs("X-YAM-Header-", fh);
      if(*p != '\\')
        ch = *p;
      else if(*++p == '\\')
        ch = '\\';
      else if(*p == 'n')
        ch = '\n';
      fputc(ch, fh);
    }
    if(ch != '\n')
      fputc('\n', fh);
  }

  LEAVE();
}

///
/// WR_ComposeReport
//  Assembles the parts of a message disposition notification
static const char *const MIMEwarn =
  "Warning: This is a message in MIME format. Your mail reader does\n"
  "not support MIME. Some parts of this message will be readable as\n"
  "plain text. To see the rest, you will need to upgrade your mail\n"
  "reader. Following are some URLs where you can find MIME-capable\n"
  "mail programs for common platforms:\n"
  "\n"
  "  AmigaOS...........: http://www.yam.ch/\n"
  "  Unix/MacOS/Windows: http://www.mozilla.com/thunderbird/\n"
  "\n"
  "General information about MIME can be found at:\n"
  "http://en.wikipedia.org/wiki/MIME\n";
static const char *const PGPwarn  =
  "The following body part contains a PGP encrypted message. Either\n"
  "your mail reader doesn't support MIME/PGP as specified in RFC 2015,\n"
  "or the message was encrypted for someone else. To read the encrypted\n"
  "message, run the next body part through Pretty Good Privacy (PGP).\n\n";

static BOOL WR_ComposeReport(FILE *fh, const struct Compose *comp, const char *boundary)
{
  struct WritePart *p;

  ENTER();

  fprintf(fh, "Content-type: multipart/report; report-type=disposition-notification; boundary=\"%s\"\n\n", boundary);

  for(p = comp->FirstPart; p; p = p->Next)
  {
    fprintf(fh, "\n--%s\n", boundary);
    WriteContentTypeAndEncoding(fh, p);
    fputs("\n", fh);

    if(EncodePart(fh, p) == FALSE)
    {
      RETURN(FALSE);
      return FALSE;
    }
  }
  fprintf(fh, "\n--%s--\n\n", boundary);

  RETURN(TRUE);
  return TRUE;
}

///
/// WR_ComposePGP
//  Creates a signed and/or encrypted PGP/MIME message
static BOOL WR_ComposePGP(FILE *fh, struct Compose *comp, char *boundary)
{
  enum Security sec = comp->Security;
  BOOL success = FALSE;
  struct WritePart pgppart;
  char *ids = AllocStrBuf(SIZE_DEFAULT);
  char pgpfile[SIZE_PATHFILE];
  struct TempFile *tf2;

  ENTER();

  pgpfile[0] = '\0';

  pgppart.Filename = pgpfile;
  pgppart.EncType = ENC_7BIT;
  if(sec == SEC_ENCRYPT || sec == SEC_BOTH)
  {
    if(comp->MailTo)
      ids = WR_GetPGPIds(comp->MailTo, ids);
    if(comp->MailCC)
      ids = WR_GetPGPIds(comp->MailCC, ids);
    if(comp->MailBCC)
      ids = WR_GetPGPIds(comp->MailBCC, ids);

    if(C->EncryptToSelf == TRUE && C->MyPGPID[0] != '\0')
    {
      if(G->PGPVersion == 5)
        ids = StrBufCat(ids, "-r ");
      ids = StrBufCat(ids, C->MyPGPID);
    }
  }

  if((tf2 = OpenTempFile(NULL)) != NULL)
  {
    struct TempFile *tf;

    if((tf = OpenTempFile("w")) != NULL)
    {
       struct WritePart *firstpart = comp->FirstPart;

       WriteContentTypeAndEncoding(tf->FP, firstpart);
       fputc('\n', tf->FP);

       if(EncodePart(tf->FP, firstpart) == FALSE)
       {
         CloseTempFile(tf);
         CloseTempFile(tf2);

         RETURN(FALSE);
         return FALSE;
       }

       fclose(tf->FP);
       tf->FP = NULL;
       ConvertCRLF(tf->Filename, tf2->Filename, TRUE);
       CloseTempFile(tf);

       snprintf(pgpfile, sizeof(pgpfile), "%s.asc", tf2->Filename);

       if(sec == SEC_SIGN || sec == SEC_BOTH)
         PGPGetPassPhrase();

       switch(sec)
       {
         case SEC_SIGN:
         {
           char options[SIZE_LARGE];

           fprintf(fh, "Content-type: multipart/signed; boundary=\"%s\"; micalg=pgp-md5; protocol=\"application/pgp-signature\"\n\n%s\n--%s\n", boundary, MIMEwarn, boundary);
           WriteContentTypeAndEncoding(fh, firstpart);
           fputc('\n', fh);

           if(EncodePart(fh, firstpart) == FALSE)
           {
             CloseTempFile(tf2);

             RETURN(FALSE);
             return FALSE;
           }

           fprintf(fh, "\n--%s\nContent-Type: application/pgp-signature\n\n", boundary);

           snprintf(options, sizeof(options), (G->PGPVersion == 5) ? "-ab %s +batchmode=1 +force" : "-sab %s +bat +f", tf2->Filename);
           if(C->MyPGPID[0] != '\0')
           {
             strlcat(options, " -u ", sizeof(options));
             strlcat(options, C->MyPGPID, sizeof(options));
           }

           if(PGPCommand((G->PGPVersion == 5) ? "pgps" : "pgp", options, 0) == 0)
             success = TRUE;
         }
         break;

         case SEC_ENCRYPT:
         {
           char options[SIZE_LARGE];

           fprintf(fh, "Content-type: multipart/encrypted; boundary=\"%s\"; protocol=\"application/pgp-encrypted\"\n\n%s\n--%s\n", boundary, MIMEwarn, boundary);
           fprintf(fh, "Content-Type: application/pgp-encrypted\n\nVersion: 1\n\n%s\n--%s\nContent-Type: application/octet-stream\n\n", PGPwarn, boundary);

           snprintf(options, sizeof(options), (G->PGPVersion == 5) ? "-a %s %s +batchmode=1 +force" : "-ea %s %s +bat +f", tf2->Filename, ids);
           if(PGPCommand((G->PGPVersion == 5) ? "pgpe" : "pgp", options, 0) == 0)
             success = TRUE;
         }
         break;

         case SEC_BOTH:
         {
           char options[SIZE_LARGE];

           fprintf(fh, "Content-type: multipart/encrypted; boundary=\"%s\"; protocol=\"application/pgp-encrypted\"\n\n%s\n--%s\n", boundary, MIMEwarn, boundary);
           fprintf(fh, "Content-Type: application/pgp-encrypted\n\nVersion: 1\n\n%s\n--%s\nContent-Type: application/octet-stream\n\n", PGPwarn, boundary);

           snprintf(options, sizeof(options), (G->PGPVersion == 5) ? "-a %s %s +batchmode=1 +force -s" : "-sea %s %s +bat +f", tf2->Filename, ids);

           if(C->MyPGPID[0] != '\0')
           {
             strlcat(options, " -u ", sizeof(options));
             strlcat(options, C->MyPGPID, sizeof(options));
           }

           if(PGPCommand((G->PGPVersion == 5) ? "pgpe" : "pgp", options, 0) == 0)
             success = TRUE;
         }
         break;

         default:
           // nothing
         break;
      }

      if(success == TRUE)
      {
        if(EncodePart(fh, &pgppart) == FALSE)
        {
          CloseTempFile(tf2);

          RETURN(FALSE);
          return FALSE;
        }
      }
    }
    CloseTempFile(tf2);
  }

  if(pgpfile[0] != '\0')
    DeleteFile(pgpfile);

  fprintf(fh, "\n--%s--\n\n", boundary);
  FreeStrBuf(ids);
  PGPClearPassPhrase(!success);

  RETURN(success);
  return success;
}

///
/// WR_ComposeMulti
//  Assembles a multipart message
static BOOL WR_ComposeMulti(FILE *fh, const struct Compose *comp, const char *boundary)
{
  struct WritePart *p;

  ENTER();

  fprintf(fh, "Content-type: multipart/mixed; boundary=\"%s\"\n\n", boundary);
  fputs(MIMEwarn, fh);

  for(p = comp->FirstPart; p; p = p->Next)
  {
    fprintf(fh, "\n--%s\n", boundary);

    WriteContentTypeAndEncoding(fh, p);

    if(comp->Security == SEC_SENDANON)
      WR_Anonymize(fh, comp->MailTo);

    fputs("\n", fh);

    if(EncodePart(fh, p) == FALSE)
    {
      RETURN(FALSE);
      return FALSE;
    }
  }

  fprintf(fh, "\n--%s--\n\n", boundary);

  RETURN(TRUE);
  return TRUE;
}

///
/// WriteOutMessage (rec)
//  Outputs header and body of a new message
BOOL WriteOutMessage(struct Compose *comp)
{
  BOOL success;
  struct TempFile *tf=NULL;
  FILE *fh = comp->FH;
  struct WritePart *firstpart = comp->FirstPart;
  char boundary[SIZE_DEFAULT];
  char options[SIZE_DEFAULT];
  char *rcptto;

  ENTER();

  if(comp->Mode == NMM_BOUNCE)
  {
    if(comp->DelSend == TRUE)
      EmitHeader(fh, "X-YAM-Options", "delsent");

    success = WR_Bounce(fh, comp);

    RETURN(success);
    return success;
  }
  else if(comp->Mode == NMM_SAVEDEC)
  {
    if(WR_SaveDec(fh, comp) == FALSE)
    {
      RETURN(FALSE);
      return FALSE;
    }
    else
      goto mimebody;
  }

  if(firstpart == NULL)
  {
    RETURN(FALSE);
    return FALSE;
  }

  /* encrypted multipart message requested? */
  if(firstpart->Next != NULL && comp->Security > SEC_NONE  && comp->Security <= SEC_BOTH)
  {
    struct Compose tcomp;
    FILE *tfh;

    if((tf = OpenTempFile(NULL)) != NULL && (tfh = fopen(tf->Filename, "w")) != NULL)
    {
      setvbuf(tfh, NULL, _IOFBF, SIZE_FILEBUF);

      memcpy(&tcomp,comp,sizeof(tcomp));   // clone struct Compose
      tcomp.FH = tfh;                      // set new filehandle
      tcomp.Security = SEC_NONE;           // temp msg gets attachments and no security

      /* clear a few other fields to avoid redundancies */
      tcomp.MailCC = NULL;
      tcomp.MailBCC = NULL;
      tcomp.ExtHeader = NULL;
      tcomp.Importance = 0;
      tcomp.DelSend = FALSE;
      tcomp.RequestMDN = FALSE;
      tcomp.UserInfo = FALSE;

      if(WriteOutMessage(&tcomp) == TRUE)    /* recurse! */
      {
        struct WritePart *tpart = comp->FirstPart; /* save parts list so we're able to recover from a calloc() error */

        /* replace with single new part */
        if((comp->FirstPart = (struct WritePart *)calloc(1,sizeof(struct WritePart))) != NULL)
        {
          comp->FirstPart->EncType = tpart->EncType;          /* reuse encoding */
          FreePartsList(tpart);                               /* free old parts list */
          comp->FirstPart->ContentType = "message/rfc822";    /* the only part is an email message */
          comp->FirstPart->Filename = tf->Filename;           /* set filename to tempfile */
          comp->Signature = 0;                                /* only use sig in enclosed mail */
        }
        else
        {
          /* no errormsg here - the window probably won't open anyway... */
          DisplayBeep(NULL);
          comp->FirstPart = tpart;     /* just restore old parts list */
          comp->Security = 0;          /* switch off security */
          /* we'll most likely get more errors further down :( */
        }
      }
      else
      {
        ER_NewError(tr(MSG_ER_PGPMultipart));
        comp->Security = 0;
      }

      fclose(tfh);
    }
    else
    {
      ER_NewError(tr(MSG_ER_PGPMultipart));
      comp->Security = 0;
    }
  }

  *options = '\0';
  if(comp->DelSend) strlcat(options, ",delsent", sizeof(options));
  if(comp->Security) snprintf(&options[strlen(options)], sizeof(options)-strlen(options), ",%s", SecCodes[comp->Security]);
  if(comp->Signature) snprintf(&options[strlen(options)], sizeof(options)-strlen(options), ",sigfile%d", comp->Signature-1);
  if(*options) EmitHeader(fh, "X-YAM-Options", &options[1]);

  if(comp->From != NULL)
    EmitRcptHeader(fh, "From", comp->From);
  else
  {
    char address[SIZE_LARGE];
    EmitRcptHeader(fh, "From", BuildAddress(address, sizeof(address), C->EmailAddress, C->RealName));
  }

  if(comp->ReplyTo) EmitRcptHeader(fh, "Reply-To", comp->ReplyTo);
  if(comp->MailTo) EmitRcptHeader(fh, "To", comp->Security == 4 ? C->ReMailer : comp->MailTo);
  if(comp->MailCC) EmitRcptHeader(fh, "CC", comp->MailCC);
  if(comp->MailBCC) EmitRcptHeader(fh, "BCC", comp->MailBCC);
  EmitHeader(fh, "Date", GetDateTime());

  // output the Message-ID, In-Reply-To and References message headers
  EmitHeader(fh, "Message-ID", NewMessageID());
  if(comp->inReplyToMsgID != NULL)
    EmitHeader(fh, "In-Reply-To", comp->inReplyToMsgID);
  if(comp->references != NULL)
    EmitHeader(fh, "References", comp->references);

  rcptto = comp->ReplyTo ? comp->ReplyTo : (comp->From ? comp->From : C->EmailAddress);
  if(comp->RequestMDN) EmitRcptHeader(fh, "Disposition-Notification-To", rcptto);
  if(comp->Importance) EmitHeader(fh, "Importance", comp->Importance == 1 ? "High" : "Low");
  fprintf(fh, "User-Agent: %s\n", yamuseragent);
  if(comp->UserInfo) WR_WriteUserInfo(fh, comp->From);
  if(*C->Organization) EmitHeader(fh, "Organization", C->Organization);
  if(*comp->Subject) EmitHeader(fh, "Subject", comp->Subject);
  if(comp->ExtHeader) WR_EmitExtHeader(fh, comp);

mimebody:

  fputs("MIME-Version: 1.0\n", fh); // RFC 2049 requires that

  strlcpy(boundary, NewBoundaryID(), sizeof(boundary));

  if(comp->GenerateMDN == TRUE)
  {
    success = WR_ComposeReport(fh, comp, boundary);
  }
  else if(comp->Security > SEC_NONE && comp->Security <= SEC_BOTH)
  {
    success = WR_ComposePGP(fh, comp, boundary);
  }
  else if(firstpart->Next != NULL)
  {
    success = WR_ComposeMulti(fh, comp, boundary);
  }
  else
  {
    WriteContentTypeAndEncoding(fh, firstpart);
    if(comp->Security == SEC_SENDANON && comp->OldSecurity != SEC_SENDANON)
      WR_Anonymize(fh, comp->MailTo);

    fputs("\n", fh);

    success = EncodePart(fh, firstpart);
  }

  CloseTempFile(tf);

  RETURN(success);
  return success;
}

///
/// WR_AutoSaveFile
//  Returns filename of the auto-save file
char *WR_AutoSaveFile(const int winnr, char *dest, const size_t length)
{
  ENTER();

  if(dest != NULL)
  {
    AddPath(dest, G->MA_MailDir, ".autosave", length);
    snprintf(dest, length, "%s%02d.txt", dest, winnr);
  }

  RETURN(dest);
  return dest;
}
///
/// AppendRcpt()
//  Appends a recipient address to a string
static char *AppendRcpt(char *sbuf, struct Person *pe, BOOL excludeme)
{
  ENTER();

  if(pe != NULL)
  {
    char address[SIZE_LARGE];
    char *ins;
    BOOL skip = FALSE;

    if(strchr(pe->Address,'@'))
      ins = BuildAddress(address, sizeof(address), pe->Address, pe->RealName);
    else
    {
      char addr[SIZE_ADDRESS];
      char *p = strchr(C->EmailAddress, '@');

      snprintf(addr, sizeof(addr), "%s%s", pe->Address, p ? p : "");
      ins = BuildAddress(address, sizeof(address), addr, pe->RealName);
    }

    if(ins != NULL)
    {
      // exclude the given person if it is ourself
      if(excludeme && stricmp(pe->Address, C->EmailAddress) == 0)
        skip = TRUE;

      // if the string already contains this person then skip it
      if(stristr(sbuf, ins))
        skip = TRUE;

      if(skip == FALSE)
      {
        // lets prepend a ", " sequence in case sbuf
        // is not empty
        if(*sbuf)
          sbuf = StrBufCat(sbuf, ", ");

        sbuf = StrBufCat(sbuf, ins);
      }
    }
  }

  RETURN(sbuf);
  return sbuf;
}

///

/*** ExpandText ***/
/// ExpandText()
//  Replaces variables with values
static char *ExpandText(char *src, struct ExpandTextData *etd)
{
  char buf[SIZE_ADDRESS];
  char *p;
  char *p2;
  char *dst = AllocStrBuf(SIZE_DEFAULT);

  ENTER();

  for(; *src; src++)
  {
    if(*src == '\\')
    {
      src++;
      switch (*src)
      {
        case '\\':
          dst = StrBufCat(dst, "\\");
        break;

        case 'n':
          dst = StrBufCat(dst, "\n");
        break;
      }
    }
    else if(*src == '%' && etd)
    {
      src++;
      switch(*src)
      {
        case 'n':
          dst = StrBufCat(dst, etd->OS_Name);
        break;

        case 'f':
        {
          strlcpy(buf, etd->OS_Name, sizeof(buf));

          if((p = strchr(buf, ',')))
            p = Trim(++p);
          else
          {
            for(p = buf; *p && *p != ' '; p++);

            *p = 0;
            p = buf;
          }
          dst = StrBufCat(dst, p);
        }
        break;

        case 's':
          dst = StrBufCat(dst, etd->OM_Subject);
        break;

        case 'e':
          dst = StrBufCat(dst, etd->OS_Address);
        break;

        case 'd':
        {
          char datstr[64];
          DateStamp2String(datstr, sizeof(datstr), &etd->OM_Date, DSS_DATE, TZC_NONE);
          dst = StrBufCat(dst, datstr);
        }
        break;

        case 't':
        {
          char datstr[64];
          DateStamp2String(datstr, sizeof(datstr), &etd->OM_Date, DSS_TIME, TZC_NONE);
          dst = StrBufCat(dst, datstr);
        }
        break;

        case 'z':
        {
          char tzone[6];
          int convertedTimeZone = (etd->OM_TimeZone/60)*100 + (etd->OM_TimeZone%60);
          snprintf(tzone, sizeof(tzone), "%+05d", convertedTimeZone);
          dst = StrBufCat(dst, tzone);
        }
        break;

        case 'w':
        {
          char datstr[64];
          DateStamp2String(datstr, sizeof(datstr), &etd->OM_Date, DSS_WEEKDAY, TZC_NONE);
          dst = StrBufCat(dst, datstr);
        }
        break;

        case 'c':
        {
          char datstr[64];
          DateStamp2RFCString(datstr, sizeof(datstr), &etd->OM_Date, etd->OM_TimeZone, FALSE);
          dst = StrBufCat(dst, datstr);
        }
        break;

        case 'm':
          dst = StrBufCat(dst, etd->OM_MessageID);
        break;

        case 'r':
          dst = StrBufCat(dst, etd->R_Name);
        break;

        case 'v':
        {
          strlcpy(buf, etd->R_Name, sizeof(buf));
          if((p = strchr(buf, ',')))
            p = Trim(++p);
          else
          {
            for(p = buf; *p && *p != ' '; p++);

            *p = '\0';
            p = buf;
          }
          dst = StrBufCat(dst, p);
        }
        break;

        case 'a':
          dst = StrBufCat(dst, etd->R_Address);
        break;

        case 'i':
        {
          strlcpy(buf, etd->OS_Name, sizeof(buf));

          for(p = p2 = &buf[1]; *p; p++)
          {
            if(*p == ' ' && p[1] && p[1] != ' ')
              *p2++ = *++p;
          }
          *p2 = '\0';
          dst = StrBufCat(dst, buf);
        }
        break;

        case 'j':
        {
          strlcpy(buf, etd->OS_Name, sizeof(buf));

          for(p2 = &buf[1], p = &buf[strlen(buf)-1]; p > p2; p--)
          {
            if(p[-1] == ' ')
            {
              *p2++ = *p;
              break;
            }
          }
          *p2 = '\0';
          dst = StrBufCat(dst, buf);
        }
        break;

        case 'h':
        {
          if((p = FileToBuffer(etd->HeaderFile)))
          {
            dst = StrBufCat(dst, p);
            free(p);
          }
        }
        break;
      }
    }
    else
    {
       char chr[2];

       chr[0] = *src;
       chr[1] = '\0';
       dst = StrBufCat(dst, chr);
    }
  }

  RETURN(dst);
  return dst;
}
///
/// InsertIntroText()
//  Inserts a phrase into the message text
static void InsertIntroText(FILE *fh, char *text, struct ExpandTextData *etd)
{
  ENTER();

  if(*text)
  {
    char *sbuf = ExpandText(text, etd);

    fprintf(fh, "%s\n", sbuf);
    FreeStrBuf(sbuf);
  }

  LEAVE();
}

///
/// SetupExpandTextData()
//  Creates quote string by replacing variables with values
static void SetupExpandTextData(struct ExpandTextData *etd, struct Mail *mail)
{
  ENTER();

  etd->OS_Name     = mail ? (*(mail->From.RealName) ? mail->From.RealName : mail->From.Address) : "";
  etd->OS_Address  = mail ? mail->From.Address : "";
  etd->OM_Subject  = mail ? mail->Subject : "";
  etd->OM_TimeZone = mail ? mail->tzone : C->TimeZone;
  etd->R_Name      = "";
  etd->R_Address   = "";

  // we have to copy the datestamp and eventually convert it
  // according to the timezone
  if(mail)
  {
    // the mail time is in UTC, so we have to convert it to the
    // actual time of the mail as we don't do any conversion
    // later on
    memcpy(&etd->OM_Date, &mail->Date, sizeof(struct DateStamp));

    if(mail->tzone != 0)
    {
      struct DateStamp *date = &etd->OM_Date;

      date->ds_Minute += mail->tzone;

      // we need to check the datestamp variable that it is still in it`s borders
      // after adjustment
      while(date->ds_Minute < 0)     { date->ds_Minute += 1440; date->ds_Days--; }
      while(date->ds_Minute >= 1440) { date->ds_Minute -= 1440; date->ds_Days++; }
    }
  }
  else
    memcpy(&etd->OM_Date, &G->StartDate, sizeof(struct DateStamp));

  LEAVE();
}

///

/*** GUI ***/
/// CreateWriteWindow()
// Function that creates a new WriteWindow object and returns
// the referencing WriteMailData structure which was created
// during that process - or NULL if an error occurred.
struct WriteMailData *CreateWriteWindow(const enum NewMailMode mailMode, const BOOL quietMode)
{
  Object *newWriteWindow;

  ENTER();

  D(DBF_GUI, "Creating new Write Window.");

  // if we end up here we create a new WriteWindowObject
  newWriteWindow = WriteWindowObject,
                     MUIA_WriteWindow_Mode,  mailMode,
                     MUIA_WriteWindow_Quiet, quietMode,
                   End;

  if(newWriteWindow)
  {
    // get the WriteMailData and check that it is the same like created
    struct WriteMailData *wmData = (struct WriteMailData *)xget(newWriteWindow, MUIA_WriteWindow_WriteMailData);

    if(wmData != NULL && wmData->window == newWriteWindow)
    {
      D(DBF_GUI, "Write window created: 0x%08lx", wmData);

      RETURN(wmData);
      return wmData;
    }

    DoMethod(G->App, OM_REMMEMBER, newWriteWindow);
    MUI_DisposeObject(newWriteWindow);
  }

  E(DBF_GUI, "ERROR occurred during write window creation!");

  RETURN(NULL);
  return NULL;
}

///
/// NewWriteMailWindow()
//  Creates a new, empty write message
struct WriteMailData *NewWriteMailWindow(struct Mail *mail, const int flags)
{
  BOOL quiet = hasQuietFlag(flags);
  struct Folder *folder = FO_GetCurrentFolder();
  struct WriteMailData *wmData = NULL;

  ENTER();

  // First check if the basic configuration is okay, then open write window */
  if(folder != NULL && CO_IsValid() == TRUE &&
     (wmData = CreateWriteWindow(NMM_NEW, quiet)) != NULL)
  {
    FILE *out;

    if((out = fopen(wmData->filename, "w")) != NULL)
    {
      setvbuf(out, NULL, _IOFBF, SIZE_FILEBUF);

      wmData->refMail = mail;

      if(wmData->refMail != NULL)
      {
        char address[SIZE_LARGE];
        struct ExtendedMail *email;

        // check whether the old mail contains a ReplyTo: address
        // or not. And if so we prefer that one instead of using the
        // To: adresses
        if(mail->ReplyTo.Address[0] != '\0')
        {
          char *addr = BuildAddress(address, sizeof(address), mail->ReplyTo.Address, mail->ReplyTo.RealName);

          if(addr != NULL)
          {
            if(isMultiReplyToMail(mail) &&
               (email = MA_ExamineMail(mail->Folder, mail->MailFile, TRUE)) != NULL)
            {
              int i;
              char *sbuf;

              // add all "ReplyTo:" recipients of the mail
              sbuf = StrBufCpy(NULL, addr);
              for(i=0; i < email->NoSReplyTo; i++)
                sbuf = AppendRcpt(sbuf, &email->SReplyTo[i], FALSE);

              set(wmData->window, MUIA_WriteWindow_To, sbuf);

              FreeStrBuf(sbuf);
            }
            else
              set(wmData->window, MUIA_WriteWindow_To, addr);
          }
        }
        else
        {
          char *addr = BuildAddress(address, sizeof(address), mail->From.Address, mail->From.RealName);

          if(addr != NULL)
          {
            if(isMultiSenderMail(mail) &&
              (email = MA_ExamineMail(mail->Folder, mail->MailFile, TRUE)) != NULL)
            {
              char *sbuf;
              int i;

              // add all "From:" recipients of the mail
              sbuf = StrBufCpy(NULL, addr);
              for(i=0; i < email->NoSFrom; i++)
                sbuf = AppendRcpt(sbuf, &email->SFrom[i], FALSE);

              set(wmData->window, MUIA_WriteWindow_To, sbuf);

              FreeStrBuf(sbuf);
            }
            else
              set(wmData->window, MUIA_WriteWindow_To, addr);
          }
        }
      }
      else if(folder->MLSupport == TRUE)
      {
        if(folder->MLAddress[0] != '\0')
          set(wmData->window, MUIA_WriteWindow_To, folder->MLAddress);

        if(folder->MLFromAddress[0] != '\0')
          set(wmData->window, MUIA_WriteWindow_From, folder->MLFromAddress);

        if(folder->MLReplyToAddress[0] != '\0')
          set(wmData->window, MUIA_WriteWindow_ReplyTo, folder->MLReplyToAddress);
      }

      if(folder->WriteIntro[0] != '\0')
        InsertIntroText(out, folder->WriteIntro, NULL);
      else
        InsertIntroText(out, C->NewIntro, NULL);

      if(folder->WriteGreetings[0] != '\0')
        InsertIntroText(out, folder->WriteGreetings, NULL);
      else
        InsertIntroText(out, C->Greetings, NULL);

      // close the output file handle
      fclose(out);

      // add a signature to the mail depending on the selected signature for this list
      DoMethod(wmData->window, MUIM_WriteWindow_AddSignature, folder->MLSupport ? folder->MLSignature: -1);

      // update the message text
      DoMethod(wmData->window, MUIM_WriteWindow_ReloadText, FALSE);

      // make sure the window is opened
      if(quiet == FALSE)
        SafeOpenWindow(wmData->window);

      if(C->LaunchAlways == TRUE && quiet == FALSE)
        DoMethod(wmData->window, MUIM_WriteWindow_LaunchEditor);
    }
    else
    {
      CleanupWriteMailData(wmData);
      wmData = NULL;
    }
  }

  RETURN(wmData);
  return wmData;
}

///
/// NewEditMailWindow()
//  Edits a message in a new write window
struct WriteMailData *NewEditMailWindow(struct Mail *mail, const int flags)
{
  BOOL quiet = hasQuietFlag(flags);
  struct Folder *folder;
  struct WriteMailData *wmData = NULL;

  ENTER();

  // check the parameters
  if(mail == NULL || (folder = mail->Folder) == NULL)
  {
    RETURN(wmData);
    return wmData;
  }

  // check if the mail in question resists in the outgoing
  // folder
  if(isOutgoingFolder(folder) &&
     IsListEmpty((struct List *)&G->writeMailDataList) == FALSE)
  {
    // search through our WriteMailDataList
    struct MinNode *curNode;

    for(curNode = G->writeMailDataList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
    {
      struct WriteMailData *wmData = (struct WriteMailData *)curNode;

      if(wmData->window != NULL && wmData->refMail == mail)
      {
        DoMethod(wmData->window, MUIM_Window_ToFront);

        RETURN(wmData);
        return wmData;
      }
    }
  }

  // check if necessary settings fror writing are OK and open new window
  if(CO_IsValid() == TRUE &&
     (wmData = CreateWriteWindow(isOutgoingFolder(folder) ? NMM_EDIT : NMM_EDITASNEW, quiet)) != NULL)
  {
    FILE *out;

    if((out = fopen(wmData->filename, "w")) != NULL)
    {
      char *sbuf = NULL;
      struct ReadMailData *rmData;
      struct ExtendedMail *email;

      setvbuf(out, NULL, _IOFBF, SIZE_FILEBUF);

      wmData->refMail = mail;

      if((email = MA_ExamineMail(folder, mail->MailFile, TRUE)) == NULL)
      {
        ER_NewError(tr(MSG_ER_CantOpenFile), GetMailFile(NULL, folder, mail));
        fclose(out);
        CleanupWriteMailData(wmData);

        RETURN(NULL);
        return NULL;
      }

      if((rmData = AllocPrivateRMData(mail, PM_ALL)) != NULL)
      {
        char *cmsg;

        if((cmsg = RE_ReadInMessage(rmData, RIM_EDIT)) != NULL)
        {
          int msglen = strlen(cmsg);

          // we check whether cmsg contains any text and if so we
          // write out the whole text to our temporary file.
          if(msglen == 0 || fwrite(cmsg, msglen, 1, out) == 1)
          {
            int i;
            char address[SIZE_LARGE];

            // free our temp text now
            free(cmsg);

            // set the In-Reply-To / References message header references, if they exist
            if(email->inReplyToMsgID != NULL)
              wmData->inReplyToMsgID = StrBufCpy(NULL, email->inReplyToMsgID);

            if(email->references != NULL)
              wmData->references = StrBufCpy(NULL, email->references);

            // set the subject gadget
            set(wmData->window, MUIA_WriteWindow_Subject, mail->Subject);

            // in case this is a EDITASNEW action we have to make sure
            // to add the From: and ReplyTo: address of the user of
            // YAM instead of filling in the data of the mail we
            // are trying to edit.
            if(wmData->mode == NMM_EDITASNEW)
            {
              if(folder->MLSupport == TRUE)
              {
                if(folder->MLFromAddress[0] != '\0')
                  set(wmData->window, MUIA_WriteWindow_From, folder->MLFromAddress);

                if(folder->MLReplyToAddress[0] != '\0')
                  set(wmData->window, MUIA_WriteWindow_ReplyTo, folder->MLReplyToAddress);
              }
            }
            else
            {
              // use all From:/ReplyTo: from the original mail
              // instead.

              // add all From: senders
              sbuf = StrBufCpy(sbuf, BuildAddress(address, sizeof(address), mail->From.Address, mail->From.RealName));
              for(i=0; i < email->NoSFrom; i++)
                sbuf = AppendRcpt(sbuf, &email->SFrom[i], FALSE);

              set(wmData->window, MUIA_WriteWindow_From, sbuf);

              // add all ReplyTo: recipients
              sbuf = StrBufCpy(sbuf, BuildAddress(address, sizeof(address), mail->ReplyTo.Address, mail->ReplyTo.RealName));
              for(i=0; i < email->NoSReplyTo; i++)
                sbuf = AppendRcpt(sbuf, &email->SReplyTo[i], FALSE);

              set(wmData->window, MUIA_WriteWindow_ReplyTo, sbuf);
            }

            // add all "To:" recipients of the mail
            sbuf = StrBufCpy(sbuf, BuildAddress(address, sizeof(address), mail->To.Address, mail->To.RealName));
            for(i=0; i < email->NoSTo; i++)
              sbuf = AppendRcpt(sbuf, &email->STo[i], FALSE);

            set(wmData->window, MUIA_WriteWindow_To, sbuf);

            // add all "CC:" recipients of the mail
            sbuf[0] = '\0';
            for(i=0; i < email->NoCC; i++)
            {
              sbuf = AppendRcpt(sbuf, &email->CC[i], FALSE);
            }
            set(wmData->window, MUIA_WriteWindow_Cc, sbuf);

            // add all "BCC:" recipients of the mail
            sbuf[0] = '\0';
            for(i=0; i < email->NoBCC; i++)
            {
              sbuf = AppendRcpt(sbuf, &email->BCC[i], FALSE);
            }
            set(wmData->window, MUIA_WriteWindow_BCC, sbuf);

            // free our temporary buffer
            FreeStrBuf(sbuf);

            if(email->extraHeaders != NULL)
              set(wmData->window, MUIA_WriteWindow_ExtHeaders, email->extraHeaders);

            xset(wmData->window, MUIA_WriteWindow_DelSend,    email->DelSend,
                                 MUIA_WriteWindow_MDN,        isSendMDNMail(mail),
                                 MUIA_WriteWindow_AddInfo,    isSenderInfoMail(mail),
                                 MUIA_WriteWindow_Importance, getImportanceLevel(mail) == IMP_HIGH ? 0 : getImportanceLevel(mail)+1,
                                 MUIA_WriteWindow_Signature,  email->Signature,
                                 MUIA_WriteWindow_Security,   email->Security);

            // let us safe the security state
            wmData->oldSecurity = email->Security;

            // setup the write window from an existing readmailData structure
            DoMethod(wmData->window, MUIM_WriteWindow_SetupFromOldMail, rmData);
          }
          else
          {
            E(DBF_MAIL, "Error while writing cmsg to out FH");

            // an error occurred while trying to write the text to out
            free(cmsg);
            FreePrivateRMData(rmData);
            fclose(out);
            MA_FreeEMailStruct(email);

            CleanupWriteMailData(wmData);

            RETURN(NULL);
            return NULL;
          }
        }

        FreePrivateRMData(rmData);
      }

      fclose(out);
      MA_FreeEMailStruct(email);

      // update the message text
      DoMethod(wmData->window, MUIM_WriteWindow_ReloadText, FALSE);

      // make sure the window is opened
      if(quiet == FALSE)
        SafeOpenWindow(wmData->window);

      sbuf = (STRPTR)xget(wmData->window, MUIA_WriteWindow_To);
      set(wmData->window, MUIA_WriteWindow_ActiveObject, *sbuf ? MUIV_WriteWindow_ActiveObject_TextEditor :
                                                                 MUIV_WriteWindow_ActiveObject_To);

      if(C->LaunchAlways == TRUE && quiet == FALSE)
        DoMethod(wmData->window, MUIM_WriteWindow_LaunchEditor);
    }
    else
    {
      CleanupWriteMailData(wmData);
      wmData = NULL;
    }
  }

  RETURN(wmData);
  return wmData;
}

///
/// NewForwardMailWindow()
//  Forwards a list of messages
struct WriteMailData *NewForwardMailWindow(struct MailList *mlist, const int flags)
{
  BOOL quiet = hasQuietFlag(flags);
  struct WriteMailData *wmData = NULL;

  ENTER();

  // check if necessary settings fror writing are OK and open new window
  if(CO_IsValid() == TRUE &&
     (wmData = CreateWriteWindow(NMM_FORWARD, quiet)) != NULL)
  {
    FILE *out;

    if((out = fopen(wmData->filename, "w")) != NULL)
    {
      int signature = -1;
      enum ForwardMode fwdMode = C->ForwardMode;
      char *rsub = AllocStrBuf(SIZE_SUBJECT);
      struct MailNode *mnode;

      // if the user wants to have the alternative
      // forward mode we go and select it here
      if(hasAltFwdModeFlag(flags))
      {
        switch(fwdMode)
        {
          case FWM_ATTACH:
            fwdMode = FWM_INLINE;
          break;

          case FWM_INLINE:
            fwdMode = FWM_ATTACH;
          break;
        }
      }

      // set the output filestream buffer size
      setvbuf(out, NULL, _IOFBF, SIZE_FILEBUF);

      // set the write mode
      wmData->refMailList = CloneMailList(mlist);

      // sort the mail list by date
      SortMailList(mlist, MA_CompareByDate);

      // insert the intro text
      InsertIntroText(out, C->NewIntro, NULL);

      // iterate through all the mail in the
      // mail list and build up the forward text
      ForEachMailNode(mlist, mnode)
      {
        struct ExtendedMail *email;
        struct ExpandTextData etd;
        struct Mail *mail = mnode->mail;

        if(signature == -1 && mail->Folder != NULL)
        {
          if(mail->Folder->MLSupport == TRUE)
            signature = mail->Folder->MLSignature;
        }

        if((email = MA_ExamineMail(mail->Folder, mail->MailFile, TRUE)) == NULL)
        {
          ER_NewError(tr(MSG_ER_CantOpenFile), GetMailFile(NULL, mail->Folder, mail));
          fclose(out);
          FreeStrBuf(rsub);

          CleanupWriteMailData(wmData);

          RETURN(NULL);
          return NULL;
        }

        SetupExpandTextData(&etd, &email->Mail);
        etd.OM_MessageID = email->messageID;
        etd.R_Name = mail->To.RealName[0] != '\0' ? mail->To.RealName : mail->To.Address;
        etd.R_Address = mail->To.Address;

        // we create a generic subject line for the forward
        // action so that a forwarded mail will have a [Fwd: XXX] kinda
        // subject line instead of the original.
        if(mail->Subject != '\0')
        {
          char buffer[SIZE_LARGE];

          snprintf(buffer, sizeof(buffer), "[Fwd: %s]", mail->Subject);
          if(strstr(rsub, buffer) == NULL)
          {
            if(rsub[0] != '\0')
              rsub = StrBufCat(rsub, "; ");

            rsub = StrBufCat(rsub, buffer);
          }
        }

        // depending on the selected forward mode we either
        // forward the email as inlined text or by simply putting
        // the original message as an attachment to the new one.
        switch(fwdMode)
        {
          // let simply add the original mail as an attachment
          case FWM_ATTACH:
          {
            char filename[SIZE_PATHFILE];
            struct Attach attach;

            memset(&attach, 0, sizeof(struct Attach));

            if(StartUnpack(GetMailFile(filename, NULL, mail), attach.FilePath, mail->Folder) != NULL)
            {
              strlcpy(attach.Description, mail->Subject, sizeof(attach.Description));
              strlcpy(attach.ContentType, "message/rfc822", sizeof(attach.ContentType));
              attach.Size = mail->Size;
              attach.IsMIME = TRUE;

              // add the attachment to our attachment listview
              DoMethod(wmData->window, MUIM_WriteWindow_InsertAttachment, &attach);
            }
            else
              E(DBF_MAIL, "unpacking of file '%s' failed!", filename);
          }
          break;

          // inline the message text of our original mail to
          // our forward message
          case FWM_INLINE:
          {
            struct ReadMailData *rmData;

            // we allocate some private readmaildata object so that
            // we can silently parse the mail which we want to
            // forward.
            if((rmData = AllocPrivateRMData(mail, PM_ALL)) != NULL)
            {
              char *cmsg;

              etd.HeaderFile = rmData->firstPart->Filename;
              InsertIntroText(out, C->ForwardIntro, &etd);
              MA_FreeEMailStruct(email);

              // read in the message text to cmsg.
              if((cmsg = RE_ReadInMessage(rmData, RIM_FORWARD)) != NULL)
              {
                // output the readin message text immediately to
                // our out filehandle
                fputs(cmsg, out);
                free(cmsg);

                InsertIntroText(out, C->ForwardFinish, &etd);

                // if the mail we are forwarding has an attachment
                // we go and attach them to our forwarded mail as well.
                if(hasNoAttachFlag(flags) == FALSE)
                  DoMethod(wmData->window, MUIM_WriteWindow_SetupFromOldMail, rmData);
              }

              FreePrivateRMData(rmData);
            }
          }
          break;
        }
      }

      // add some footer with greatings.
      InsertIntroText(out, C->Greetings, NULL);
      fclose(out);

      // add a signature to the mail depending on the selected signature for this list
      DoMethod(wmData->window, MUIM_WriteWindow_AddSignature, signature);

      // set the composed subject text
      set(wmData->window, MUIA_WriteWindow_Subject, rsub);
      FreeStrBuf(rsub);

      // update the message text
      DoMethod(wmData->window, MUIM_WriteWindow_ReloadText, FALSE);

      // make sure the window is opened
      if(quiet == FALSE)
        SafeOpenWindow(wmData->window);

      // set the active object of the window
      set(wmData->window, MUIA_WriteWindow_ActiveObject, MUIV_WriteWindow_ActiveObject_To);

      if(C->LaunchAlways == TRUE && quiet == FALSE)
        DoMethod(wmData->window, MUIM_WriteWindow_LaunchEditor);
    }
    else
    {
      CleanupWriteMailData(wmData);
      wmData = NULL;
    }
  }

  RETURN(wmData);
  return wmData;
}

///
/// NewReplyMailWindow()
//  Creates a reply to a list of messages
struct WriteMailData *NewReplyMailWindow(struct MailList *mlist, const int flags)
{
  BOOL quiet = hasQuietFlag(flags);
  struct WriteMailData *wmData = NULL;

  ENTER();

  // check if necessary settings fror writing are OK and open new window
  if(CO_IsValid() == TRUE &&
     (wmData = CreateWriteWindow(NMM_REPLY, quiet)) != NULL)
  {
    FILE *out;

    if((out = fopen(wmData->filename, "w")) != NULL)
    {
      int j;
      int repmode = 1;
      int signature = -1;
      BOOL altpat = FALSE;
      char *domain = NULL;
      char *mlistad = NULL;
      char *rfrom = NULL;
      char *rrepto = NULL;
      char *rto = AllocStrBuf(SIZE_ADDRESS);
      char *rcc = AllocStrBuf(SIZE_ADDRESS);
      char *rsub = AllocStrBuf(SIZE_SUBJECT);
      char buffer[SIZE_LARGE];
      struct ExpandTextData etd;
      BOOL mlIntro = FALSE;
      struct MailNode *mnode;

      setvbuf(out, NULL, _IOFBF, SIZE_FILEBUF);

      // make sure the write window know of the
      // operation and knows which mails to process
      wmData->refMailList = CloneMailList(mlist);

      // make sure we sort the mlist according to
      // the mail date
      SortMailList(mlist, MA_CompareByDate);

      // Now we iterate through all selected mails
      j = 0;
      ForEachMailNode(mlist, mnode)
      {
        int k;
        struct Mail *mail = mnode->mail;
        struct Folder *folder = mail->Folder;
        struct ExtendedMail *email;
        struct Person pe;
        BOOL foundMLFolder = FALSE;

        if((email = MA_ExamineMail(folder, mail->MailFile, TRUE)) == NULL)
        {
          ER_NewError(tr(MSG_ER_CantOpenFile), GetMailFile(NULL, folder, mail));
          fclose(out);
          CleanupWriteMailData(wmData);
          FreeStrBuf(rto);
          FreeStrBuf(rcc);
          FreeStrBuf(rsub);

          RETURN(NULL);
          return NULL;
        }

        // make sure we setup the quote string
        // correctly.
        SetupExpandTextData(&etd, &email->Mail);
        etd.OM_MessageID = email->messageID;

        // If the mail which we are going to reply to already has a subject,
        // we are going to add a "Re:" to it.
        if(mail->Subject[0] != '\0')
        {
          if(j > 0)
          {
            // if the subject contains brackets then these need to be removed first,
            // else the strstr() call below will not find the "reduced" subject
            if(mail->Subject[0] == '[' && strchr(mail->Subject, ']') != NULL)
            {
              // copy the stripped subject
              strlcpy(buffer, MA_GetRealSubject(mail->Subject), sizeof(buffer));
            }
            else
            {
              // copy the subject as-is
              strlcpy(buffer, mail->Subject, sizeof(buffer));
            }
          }
          else
          {
            // copy the first subject stripped, but prepend the usual "Re:"
            snprintf(buffer, sizeof(buffer), "Re: %s", MA_GetRealSubject(mail->Subject));
          }

          // try to find following subjects in the yet created reply subject
          if(strstr(rsub, buffer) == NULL)
          {
            if(rsub[0] != '\0')
              rsub = StrBufCat(rsub, "; ");

            rsub = StrBufCat(rsub, buffer);
          }
        }

        // in case we are replying to a single message we also have to
        // save the messageID of the email we are replying to
        if(wmData->inReplyToMsgID != NULL)
          wmData->inReplyToMsgID = StrBufCat(wmData->inReplyToMsgID, " ");

        wmData->inReplyToMsgID = StrBufCat(wmData->inReplyToMsgID, email->messageID);

        // in addition, we check for "References:" message header stuff
        if(wmData->references != NULL)
          wmData->references = StrBufCat(wmData->references, " ");

        if(email->references != NULL)
          wmData->references = StrBufCat(wmData->references, email->references);
        else
        {
          // check if this email contains inReplyToMsgID data and if so we
          // create a new references header entry
          if(email->inReplyToMsgID != NULL)
            wmData->references = StrBufCat(wmData->references, email->inReplyToMsgID);
        }

        // Now we analyse the folder of the selected mail and if it
        // is a mailing list we have to do some special operation
        if(folder != NULL)
        {
          // if the mail we are going to reply resists in the incoming folder
          // we have to check all other folders first.
          if(isIncomingFolder(folder))
          {
            LockFolderListShared(G->folders);

            // walk through all our folders
            // and check if it matches a pattern
            if(IsFolderListEmpty(G->folders) == FALSE)
            {
              struct FolderNode *fnode;

              ForEachFolderNode(G->folders, fnode)
              {
                if(fnode->folder != NULL && fnode->folder->MLSupport == TRUE && fnode->folder->MLPattern[0] != '\0')
                {
                  char *pattern = fnode->folder->MLPattern;

                  if(MatchNoCase(mail->To.Address, pattern) == FALSE &&
                     MatchNoCase(mail->To.RealName, pattern) == FALSE)
                  {
                    for(k=0; k < email->NoSTo; k++)
                    {
                      if(MatchNoCase(email->STo[k].Address, pattern) ||
                         MatchNoCase(email->STo[k].RealName, pattern))
                      {
                        foundMLFolder = TRUE;
                        break;
                      }
                    }
                  }
                  else
                    foundMLFolder = TRUE;

                  if(foundMLFolder == TRUE)
                  {
                    mlistad = fnode->folder->MLAddress[0] != '\0' ? fnode->folder->MLAddress : NULL;
                    folder = fnode->folder;

                    if(folder->MLFromAddress[0] != '\0')
                      rfrom  = folder->MLFromAddress;

                    if(folder->MLReplyToAddress[0] != '\0')
                      rrepto = folder->MLReplyToAddress;

                    break;
                  }
                }
              }
            }

            UnlockFolderList(G->folders);
          }
          else if(folder->MLSupport == TRUE && folder->MLPattern[0] != '\0')
          {
            if(MatchNoCase(mail->To.Address, folder->MLPattern) == FALSE &&
               MatchNoCase(mail->To.RealName, folder->MLPattern) == FALSE)
            {
              for(k=0; k < email->NoSTo; k++)
              {
                if(MatchNoCase(email->STo[k].Address, folder->MLPattern) ||
                   MatchNoCase(email->STo[k].RealName, folder->MLPattern))
                {
                  foundMLFolder = TRUE;
                  break;
                }
              }
            }
            else
              foundMLFolder = TRUE;

            if(foundMLFolder == TRUE)
            {
              mlistad = folder->MLAddress[0] != '\0' ? folder->MLAddress : NULL;

              if(folder->MLFromAddress[0] != '\0')
                rfrom  = folder->MLFromAddress;

              if(folder->MLReplyToAddress[0] != '\0')
                rrepto = folder->MLReplyToAddress;
            }
          }
        }

        // If this mail is a standard multi-recipient mail and the user hasn't pressed SHIFT
        // or ALT we going to ask him to which recipient he want to send the mail to.
        if(isMultiRCPTMail(mail) && !hasPrivateFlag(flags) && !hasMListFlag(flags))
        {
          // ask the user and in case he want to abort, quit this
          // function immediately.
          if((repmode = MUI_Request(G->App, G->MA->GUI.WI, 0, NULL, tr(MSG_MA_ReplyReqOpt), tr(MSG_MA_ReplyReq))) == 0)
          {
            MA_FreeEMailStruct(email);
            fclose(out);
            CleanupWriteMailData(wmData);
            FreeStrBuf(rto);
            FreeStrBuf(rcc);
            FreeStrBuf(rsub);

            RETURN(NULL);
            return NULL;
          }
        }

        // now we should know how the user wants to
        // reply to the mail. The possible reply modes are:
        //
        // repmode == 1 : To Sender (From:/ReplyTo:)
        // repmode == 2 : To Sender and all recipients (From:/ReplyTo:, To:, CC:)
        // repmode == 3 : To Recipients (To:, CC:)
        if(repmode == 1)
        {
          BOOL addDefault = FALSE;

          // the user wants to reply to the Sender (From:), however we
          // need to check whether he want to get asked or directly reply to
          // the wanted address.
          if(hasPrivateFlag(flags))
          {
            // the user seem to have pressed the SHIFT key, so
            // we are going to "just"reply to the "From:" addresses of
            // the original mail. so we add them accordingly.
            rto = AppendRcpt(rto, &mail->From, FALSE);
            for(k=0; k < email->NoSFrom; k++)
              rto = AppendRcpt(rto, &email->SFrom[k], FALSE);
          }
          else if(foundMLFolder && mlistad != NULL)
          {
            char *p;

            if((p = strdup(mlistad)) != NULL)
            {
              struct Person pe;

              // we found a matching folder for the mail we are going to
              // reply to, so we go and add the 'mlistad' to our To: addresses
              while(p != NULL && *p != '\0')
              {
                char *next;

                if((next = MyStrChr(p, ',')) != NULL)
                  *next++ = '\0';

                ExtractAddress(p, &pe);
                rto = AppendRcpt(rto, &pe, FALSE);

                p = next;
              }

              free(p);
            }
          }
          else if(C->CompareAddress == TRUE && !hasMListFlag(flags) &&
                  mail->ReplyTo.Address[0] != '\0')
          {
            BOOL askUser = FALSE;

            // now we have to check whether the ReplyTo: and From: of the original are the
            // very same or not.
            if(stricmp(mail->From.Address, mail->ReplyTo.Address) == 0)
            {
              if(email->NoSFrom == email->NoSReplyTo)
              {
                for(k=0; k < email->NoSFrom; k++)
                {
                  if(stricmp(email->SFrom[k].Address, email->SReplyTo[k].Address) != 0)
                  {
                    askUser = TRUE;
                    break;
                  }
                }
              }
              else
                askUser = TRUE;
            }
            else
              askUser = TRUE;

            // if askUser == TRUE, we go and
            // ask the user which address he wants to reply to.
            if(askUser == TRUE)
            {
              snprintf(buffer, sizeof(buffer), tr(MSG_MA_CompareReq), mail->From.Address, mail->ReplyTo.Address);

              switch(MUI_Request(G->App, G->MA->GUI.WI, 0, NULL, tr(MSG_MA_Compare3ReqOpt), buffer))
              {
                // Both (From:/ReplyTo:) address
                case 3:
                {
                  // add all From: addresses to the CC: list
                  rcc = AppendRcpt(rcc, &mail->From, FALSE);
                  for(k=0; k < email->NoSFrom; k++)
                    rcc = AppendRcpt(rcc, &email->SFrom[k], FALSE);
                }
                // continue

                // Reply-To: addresses
                case 2:
                {
                  rto = AppendRcpt(rto, &mail->ReplyTo, FALSE);
                  for(k=0; k < email->NoSReplyTo; k++)
                    rto = AppendRcpt(rto, &email->SReplyTo[k], FALSE);
                }
                break;

                // only From: addresses
                case 1:
                {
                  rto = AppendRcpt(rto, &mail->From, FALSE);
                  for(k=0; k < email->NoSFrom; k++)
                    rto = AppendRcpt(rto, &email->SFrom[k], FALSE);
                }
                break;

                // cancel operation
                case 0:
                {
                  MA_FreeEMailStruct(email);
                  fclose(out);
                  CleanupWriteMailData(wmData);
                  FreeStrBuf(rto);
                  FreeStrBuf(rcc);
                  FreeStrBuf(rsub);

                  RETURN(NULL);
                  return NULL;
                }
              }
            }
            else
              addDefault = TRUE;
          }
          else
            addDefault = TRUE;

          if(addDefault == TRUE)
          {
            // otherwise we check whether to use the ReplyTo: or From: addresses as the
            // To: adress of our reply. If a ReplyTo: exists we use that one instead
            if(mail->ReplyTo.Address[0] != '\0')
            {
              rto = AppendRcpt(rto, &mail->ReplyTo, FALSE);
              for(k=0; k < email->NoSReplyTo; k++)
                rto = AppendRcpt(rto, &email->SReplyTo[k], FALSE);
            }
            else
            {
              rto = AppendRcpt(rto, &mail->From, FALSE);
              for(k=0; k < email->NoSFrom; k++)
                rto = AppendRcpt(rto, &email->SFrom[k], FALSE);
            }
          }
        }
        else
        {
          // user wants to replyd to all senders and recipients
          // so lets add
          if(repmode == 2)
          {
            if(mail->ReplyTo.Address[0] != '\0')
            {
              rto = AppendRcpt(rto, &mail->ReplyTo, FALSE);
              for(k=0; k < email->NoSReplyTo; k++)
                rto = AppendRcpt(rto, &email->SReplyTo[k], FALSE);
            }
            else
            {
              rto = AppendRcpt(rto, &mail->From, FALSE);
              for(k=0; k < email->NoSFrom; k++)
                rto = AppendRcpt(rto, &email->SFrom[k], FALSE);
            }
          }

          // now add all original To: addresses
          rto = AppendRcpt(rto, &mail->To, TRUE);
          for(k=0; k < email->NoSTo; k++)
            rto = AppendRcpt(rto, &email->STo[k], TRUE);

          // add the CC: addresses as well
          for(k=0; k < email->NoCC; k++)
            rcc = AppendRcpt(rcc, &email->CC[k], TRUE);
        }

        // extract the first address/name from our generated
        // To: address string
        ExtractAddress(rto, &pe);
        etd.R_Name = pe.RealName;
        etd.R_Address = pe.Address;

        // extract the domain name from the To address or respective
        // the default To: mail address
        if((domain = strchr(pe.Address, '@')) == NULL)
          domain = strchr(C->EmailAddress, '@');

        if(C->AltReplyPattern[0] != '\0' && domain && MatchNoCase(domain, C->AltReplyPattern))
          altpat = TRUE;
        else
          altpat = FALSE;

        // insert a "Hello" text as the first intro text in case
        // this is our first iteration
        if(j == 0)
        {
          if(foundMLFolder)
          {
            signature = folder->MLSignature;
            mlIntro = TRUE;
          }

          InsertIntroText(out, foundMLFolder ? C->MLReplyHello : (altpat ? C->AltReplyHello : C->ReplyHello), &etd);
        }

        // if the user wants to quote the mail text of the original mail,
        // we process it right now.
        if(C->QuoteMessage == TRUE && !hasNoQuoteFlag(flags))
        {
          struct ReadMailData *rmData;

          if(j > 0)
            fputc('\n', out);

          if((rmData = AllocPrivateRMData(mail, PM_TEXTS)) != NULL)
          {
            char *cmsg;

            etd.HeaderFile = rmData->firstPart->Filename;

            // put some introduction right before the quoted text.
            InsertIntroText(out, foundMLFolder ? C->MLReplyIntro : (altpat ? C->AltReplyIntro : C->ReplyIntro), &etd);

            if((cmsg = RE_ReadInMessage(rmData, RIM_QUOTE)))
            {
              // make sure we quote the text in question.
              QuoteText(out, cmsg, strlen(cmsg), C->EdWrapMode != EWM_OFF ? C->EdWrapCol-2 : 1024);

              free(cmsg);
            }

            FreePrivateRMData(rmData);
          }
        }

        // free out temporary extended mail structure again.
        MA_FreeEMailStruct(email);

        j++;
      }

      // now we complement the "References:" header by adding our replyto header to it
      if(wmData->inReplyToMsgID != NULL)
      {
        if(wmData->references != NULL)
          wmData->references = StrBufCat(wmData->references, " ");

        wmData->references = StrBufCat(wmData->references, wmData->inReplyToMsgID);
      }

      // now that the mail is finished, we go and output some footer message to
      // the reply text.
      InsertIntroText(out, mlIntro ? C->MLReplyBye : (altpat ? C->AltReplyBye: C->ReplyBye), &etd);
      fclose(out);

      // add a signature to the mail depending on the selected signature for this list
      DoMethod(wmData->window, MUIM_WriteWindow_AddSignature, signature);

      // If this is a reply to a mail belonging to a mailing list,
      // set the "From:" and "Reply-To:" addresses accordingly */
      if(rfrom != NULL)
        set(wmData->window, MUIA_WriteWindow_From, rfrom);

      if(rrepto != NULL)
        set(wmData->window, MUIA_WriteWindow_ReplyTo, rrepto);

      set(wmData->window, MUIA_WriteWindow_To, rto);
      set(wmData->window, rto[0] != '\0' ? MUIA_WriteWindow_Cc : MUIA_WriteWindow_To, rcc);
      set(wmData->window, MUIA_WriteWindow_Subject, rsub);

      // update the message text
      DoMethod(wmData->window, MUIM_WriteWindow_ReloadText, FALSE);

      // make sure the window is opened
      if(quiet == FALSE)
        SafeOpenWindow(wmData->window);

      // set the active object of the window
      set(wmData->window, MUIA_WriteWindow_ActiveObject, MUIV_WriteWindow_ActiveObject_TextEditor);

      if(C->LaunchAlways == TRUE && quiet == FALSE)
        DoMethod(wmData->window, MUIM_WriteWindow_LaunchEditor);

      // free our temporary buffers
      FreeStrBuf(rto);
      FreeStrBuf(rcc);
      FreeStrBuf(rsub);
    }
    else
    {
      CleanupWriteMailData(wmData);
      wmData = NULL;
    }
  }

  RETURN(wmData);
  return wmData;
}

///
/// NewBounceMailWindow()
//  Bounces a message
struct WriteMailData *NewBounceMailWindow(struct Mail *mail, const int flags)
{
  BOOL quiet = hasQuietFlag(flags);
  struct WriteMailData *wmData = NULL;

  ENTER();

#warning "TODO: implement BounceWindow class"
  // check if necessary settings fror writing are OK and open new window
/*
  if(CO_IsValid() == TRUE && (winnum = WR_Open(quiet ? 2 : -1, TRUE)) >= 0)
  {
    struct WR_ClassData *wr = G->WR[winnum];

    wr->Mode = NEW_BOUNCE;
    wr->refMail = mail;

    if(quiet == FALSE)
      set(wr->GUI.WI, MUIA_Window_Open, TRUE);

    set(wr->GUI.WI, MUIA_Window_ActiveObject, wr->GUI.ST_TO);
  }

  if(winnum >= 0 && quiet == FALSE)
    winnum = MA_CheckWriteWindow(winnum);
*/
  RETURN(wmData);
  return wmData;
}

///

/*** WriteMailData ***/
/// CleanupWriteMailData()
// cleans/deletes all data of a WriteMailData structure
BOOL CleanupWriteMailData(struct WriteMailData *wmData)
{
  ENTER();

  SHOWVALUE(DBF_MAIL, wmData);
  ASSERT(wmData != NULL);

  // check if this wmData is the current active Rexx background
  // processing one and if so set the ptr to NULL to signal the rexx
  // commands that their active window was closed/disposed
  if(wmData == G->ActiveRexxWMData)
    G->ActiveRexxWMData = NULL;

  // stop any pending file notification.
  if(wmData->fileNotifyActive == TRUE)
  {
    if(wmData->notifyRequest != NULL)
      EndNotify(wmData->notifyRequest);

    wmData->fileNotifyActive = FALSE;
  }

  // free the notify resources
  if(wmData->notifyRequest != NULL)
  {
    if(wmData->notifyRequest->nr_stuff.nr_Msg.nr_Port != NULL)
      FreeSysObject(ASOT_PORT, wmData->notifyRequest->nr_stuff.nr_Msg.nr_Port);

    #if defined(__amigaos4__)
    FreeDosObject(DOS_NOTIFYREQUEST, wmData->notifyRequest);
    #else
    FreeVecPooled(G->SharedMemPool, wmData->notifyRequest);
    #endif

    wmData->notifyRequest = NULL;
  }

  // delete the temp file
  DeleteFile(wmData->filename);

  // cleanup the reference mail list
  if(wmData->refMailList != NULL)
  {
    DeleteMailList(wmData->refMailList);
    wmData->refMailList = NULL;
  }

  wmData->refMail = NULL;

  // cleanup the In-Reply-To / References stuff
  if(wmData->inReplyToMsgID != NULL)
  {
    FreeStrBuf(wmData->inReplyToMsgID);
    wmData->inReplyToMsgID = NULL;
  }

  if(wmData->references != NULL)
  {
    FreeStrBuf(wmData->references);
    wmData->references = NULL;
  }

  // clean up the write window now
  if(wmData->window != NULL)
  {
    // make sure the window is really closed
    D(DBF_GUI, "make sure the write window is closed");
    nnset(wmData->window, MUIA_Window_Open, FALSE);

    D(DBF_GUI, "cleaning up write window");
    DoMethod(G->App, OM_REMMEMBER, wmData->window);
    MUI_DisposeObject(wmData->window);

    wmData->window = NULL;
  }

  // Remove the writeWindowNode and free it afterwards
  Remove((struct Node *)wmData);
  free(wmData);

  RETURN(TRUE);
  return TRUE;
}

///
/// SetWriteMailDataMailRef()
// sets the refMail and refMailList mail references to
// the one specified
BOOL SetWriteMailDataMailRef(const struct Mail *search, const struct Mail *newRef)
{
  BOOL result = FALSE;

  ENTER();

  if(IsListEmpty((struct List *)&G->writeMailDataList) == FALSE)
  {
    // search through our WriteMailDataList
    struct MinNode *curNode;

    for(curNode = G->writeMailDataList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
    {
      struct WriteMailData *wmData = (struct WriteMailData *)curNode;

      if(wmData->refMail == search)
      {
        wmData->refMail = (struct Mail *)newRef;
        result = TRUE;
      }

      if(wmData->refMailList != NULL && IsMailListEmpty(wmData->refMailList) == FALSE)
      {
        struct MailNode *mnode;

        LockMailListShared(wmData->refMailList);

        ForEachMailNode(wmData->refMailList, mnode)
        {
          if(mnode->mail == search)
          {
            mnode->mail = (struct Mail *)newRef;
            result = TRUE;
          }
        }

        UnlockMailList(wmData->refMailList);
      }
    }
  }

  RETURN(result);
  return result;
}

///

/*** AutoSave files ***/
/// CheckForAutoSaveFiles()
// function that checks for .autosaveXX.txt files and
// warn the user accordingly of the existance of such a backup file
void CheckForAutoSaveFiles(void)
{
  static const char *pattern = ".autosave[0-9][0-9].txt";
  char *parsedPattern;
  LONG parsedPatternSize;
  APTR context;

  ENTER();

  // we go and check whether there is any .autosaveXX.txt file in the
  // maildir directory. And if so we ask the user what he would like to do with it
  parsedPatternSize = strlen(pattern) * 2 + 2;
  if((parsedPattern = malloc(parsedPatternSize)) != NULL)
  {
    ParsePatternNoCase(pattern, parsedPattern, parsedPatternSize);

    if((context = ObtainDirContextTags(EX_StringName,  (ULONG)G->MA_MailDir,
                                       EX_MatchString, (ULONG)parsedPattern,
                                       TAG_DONE)) != NULL)
    {
      struct ExamineData *ed;

      while((ed = ExamineDir(context)) != NULL)
      {
        // check that this entry is a file
        // because we don't accept any dir here
        if(EXD_IS_FILE(ed))
        {
          int answer;
          char fileName[SIZE_PATHFILE];

          D(DBF_MAIL, "found file '%s' matches autosave pattern '%s'", ed->Name, pattern);

          // pack the filename and path together so that we can reference to it
          AddPath(fileName, G->MA_MailDir, ed->Name, sizeof(fileName));

          // now that we have identified the existance of a .autosave file
          // we go and warn the user accordingly.
          answer = MUI_Request(G->App, G->MA->GUI.WI, 0, tr(MSG_MA_AUTOSAVEFOUND_TITLE),
                                                         tr(MSG_MA_AUTOSAVEFOUND_BUTTONS),
                                                         tr(MSG_MA_AUTOSAVEFOUND),
                                                         fileName);
          if(answer == 1)
          {
            // the user wants to put the autosave file on hold in the outgoing folder
            // so lets do it and delete the autosave file afterwards
            struct WriteMailData *wmData;

            if((wmData = NewWriteMailWindow(NULL, NEWF_QUIET)) != NULL)
            {
              // set some default receiver and subject, because the autosave file just contains
              // the message text
              set(wmData->window, MUIA_WriteWindow_To, "no@receiver");
              set(wmData->window, MUIA_WriteWindow_Subject, "(subject)");

              // load the file in the new editor gadget and flag it as changed
              DoMethod(wmData->window, MUIM_WriteWindow_LoadText, fileName, TRUE);

              // put the new mail on hold
              DoMethod(wmData->window, MUIM_WriteWindow_ComposeMail, WRITE_HOLD);

              // we need to explicitly delete the autosave file here because
              // the delete routine in WR_NewMail() doesn't catch the correct file
              // because it only cares about the autosave file for the newly created
              // write object
              if(DeleteFile(fileName) == 0)
                AddZombieFile(fileName);
            }
          }
          else if(answer == 2)
          {
            // the user wants to open the autosave file in an own new write window,
            // so lets do it and delete the autosave file afterwards
            struct WriteMailData *wmData;

            if((wmData = NewWriteMailWindow(NULL, 0)) != NULL)
            {
              // load the file in the new editor gadget and flag it as changed
              DoMethod(wmData->window, MUIM_WriteWindow_LoadText, fileName, TRUE);

              // we delete the autosave file now
              if(DeleteFile(fileName) == 0)
                AddZombieFile(fileName);

              // then we immediately create a new autosave file
              DoMethod(wmData->window, MUIM_WriteWindow_DoAutoSave);
            }
          }
          else if(answer == 3)
          {
            // just delete the autosave file
            if(DeleteFile(fileName) == 0)
              AddZombieFile(fileName);
          }
        }
      }

      if(IoErr() != ERROR_NO_MORE_ENTRIES)
        E(DBF_ALWAYS, "ExamineDir failed");

      ReleaseDirContext(context);
    }

    free(parsedPattern);
  }

  LEAVE();
}

///

/*** GUI ***/
/// WR_NewBounce
//  Creates a bounce window
/*
static struct WR_ClassData *WR_NewBounce(int winnum)
{
  struct WR_ClassData *data;

  ENTER();

  if((data = calloc(1, sizeof(struct WR_ClassData))) != NULL)
  {
    data->GUI.WI = WindowObject,
       MUIA_Window_Title, tr(MSG_WR_BounceWT),
       MUIA_HelpNode, "WR_W",
       MUIA_Window_ID, MAKE_ID('W','R','I','B'),
       WindowContents, VGroup,
          Child, ColGroup(2),
             Child, Label2(tr(MSG_WR_BounceTo)),
             Child, MakeAddressField(&data->GUI.ST_TO, tr(MSG_WR_BounceTo), MSG_HELP_WR_ST_TO, ABM_TO, winnum, AFF_ALLOW_MULTI|AFF_EXTERNAL_SHORTCUTS),
          End,
          Child, ColGroup(4),
             Child, data->GUI.BT_SEND   = MakeButton(tr(MSG_WR_SENDNOW)),
             Child, data->GUI.BT_QUEUE  = MakeButton(tr(MSG_WR_SENDLATER)),
             Child, data->GUI.BT_HOLD   = MakeButton(tr(MSG_WR_HOLD)),
             Child, data->GUI.BT_CANCEL = MakeButton(tr(MSG_WR_CANCEL)),
          End,
       End,
    End;
    if(data->GUI.WI != NULL)
    {
      DoMethod(G->App, OM_ADDMEMBER, data->GUI.WI);
      WR_SharedSetup(data, winnum);
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
*/
///
