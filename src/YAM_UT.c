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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netinet/in.h>

#include <clib/alib_protos.h>
#include <clib/macros.h>
#include <datatypes/pictureclass.h>
#include <datatypes/soundclass.h>
#include <devices/printer.h>
#include <dos/doshunks.h>
#include <dos/dostags.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>
#include <libraries/openurl.h>
#include <mui/BetterString_mcc.h>
#include <mui/NList_mcc.h>
#include <mui/NListtree_mcc.h>
#include <mui/NListview_mcc.h>
#include <mui/TextEditor_mcc.h>
#include <workbench/startup.h>
#include <proto/codesets.h>
#include <proto/datatypes.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <proto/iffparse.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/keymap.h>
#include <proto/layers.h>
#include <proto/locale.h>
#include <proto/muimaster.h>
#include <proto/openurl.h>
#include <proto/timer.h>
#include <proto/utility.h>
#include <proto/wb.h>
#include <proto/xpkmaster.h>

#if defined(__amigaos4__)
#include <proto/application.h>
#endif

#include "extrasrc.h"

#include "SDI_hook.h"
#include "SDI_stdarg.h"

#include "YAM.h"
#include "YAM_addressbook.h"
#include "YAM_config.h"
#include "YAM_error.h"
#include "YAM_find.h"
#include "YAM_folderconfig.h"
#include "YAM_global.h"
#include "YAM_main.h"
#include "YAM_mainFolder.h"
#include "YAM_read.h"
#include "YAM_utilities.h"
#include "mui/Classes.h"

#include "AppIcon.h"
#include "FileInfo.h"
#include "FolderList.h"
#include "Locale.h"
#include "MailList.h"
#include "MimeTypes.h"
#include "MUIObjects.h"
#include "ParseEmail.h"
#include "Requesters.h"

#include "Debug.h"

#define CRYPTBYTE 164

#if !defined(SDTA_Cycles)
#define SDTA_Cycles           (SDTA_Dummy + 6)
#endif

struct UniversalClassData
{
   struct UniversalGUIData { APTR WI; } GUI;
};

int BusyLevel = 0;


/***************************************************************************
 Utilities
***************************************************************************/

#if !defined(__amigaos4__) || (INCLUDE_VERSION < 50)
struct PathNode
{
  BPTR pn_Next;
  BPTR pn_Lock;
};
#endif

struct ZombieFile
{
  struct MinNode node;
  char *fileName;
};

/// CloneSearchPath
// This returns a duplicated search path (preferable the workbench
// searchpath) usable for NP_Path of SystemTagList().
static BPTR CloneSearchPath(void)
{
  BPTR path = 0;

  ENTER();

  if(WorkbenchBase && WorkbenchBase->lib_Version >= 44)
    WorkbenchControl(NULL, WBCTRLA_DuplicateSearchPath, &path, TAG_DONE);

  #if !defined(__amigaos4__)
  // if we couldn't obtain a duplicate copy of the workbench search
  // path here it is very likely that we are running on a system with
  // workbench.library < 44 or on MorphOS with an old workbench.lib.
  if(path == 0)
  {
    struct Process *pr = (struct Process*)FindTask(NULL);

    if(pr->pr_Task.tc_Node.ln_Type == NT_PROCESS)
    {
      struct CommandLineInterface *cli = BADDR(pr->pr_CLI);

      if(cli)
      {
        BPTR *p = &path;
        BPTR dir = cli->cli_CommandDir;

        while (dir)
        {
          BPTR dir2;
          struct FileLock *lock = BADDR(dir);
          struct PathNode *node;

          dir = lock->fl_Link;
          dir2 = DupLock(lock->fl_Key);
          if(!dir2)
            break;

          // Use AllocVec(), because this memory is freed by FreeVec()
          // by the system later
          if(!(node = AllocVec(sizeof(struct PathNode), MEMF_PUBLIC)))
          {
            UnLock(dir2);
            break;
          }

          node->pn_Next = 0;
          node->pn_Lock = dir2;
          *p = MKBADDR(node);
          p = &node->pn_Next;
        }
      }
    }
  }
  #endif

  RETURN(path);
  return path;
}

///
/// FreeSearchPath
// Free the memory returned by CloneSearchPath
static void FreeSearchPath(BPTR path)
{
  ENTER();

  if(path != 0)
  {
    #ifndef __MORPHOS__
    if(WorkbenchBase && WorkbenchBase->lib_Version >= 44)
      WorkbenchControl(NULL, WBCTRLA_FreeSearchPath, path, TAG_DONE);
    else
    #endif
    {
      #ifndef __amigaos4__
      // This is also compatible with WorkenchControl(NULL, WBCTRLA_FreeSearchPath, ...)
      // in MorphOS/Ambient environments.
      while(path)
      {
        struct PathNode *node = BADDR(path);
        path = node->pn_Next;
        UnLock(node->pn_Lock);
        FreeVec(node);
      }
      #endif
    }
  }

  LEAVE();
}
///

/*** String related ***/
/// itoa
//  Converts an integer into a string
char *itoa(int val)
{
  static char str[SIZE_SMALL];

  ENTER();

  snprintf(str, sizeof(str), "%d", val);

  RETURN(str);
  return str;
}
///
/// MatchNoCase
//  Case insensitive pattern matching
BOOL MatchNoCase(const char *string, const char *match)
{
  BOOL result = FALSE;
  LONG patternlen = strlen(match)*2+2; // ParsePattern() needs at least 2*source+2 bytes buffer
  char *pattern;

  ENTER();

  if((pattern = malloc((size_t)patternlen)) != NULL)
  {
    if(ParsePatternNoCase((STRPTR)match, pattern, patternlen) != -1)
      result = MatchPatternNoCase((STRPTR)pattern, (STRPTR)string);

    free(pattern);
  }

  RETURN(result);
  return result;
}
///
/// StripUnderscore
//  Removes underscore from button labels
char *StripUnderscore(const char *label)
{
  static char newlabel[SIZE_DEFAULT];
  char *p;

  ENTER();

  for(p=newlabel; *label; label++)
  {
    if(*label != '_')
      *p++ = *label;
  }
  *p = '\0';

  RETURN(newlabel);
  return newlabel;
}
///
/// GetNextLine
//  Reads next line from a multi-line string
char *GetNextLine(char *p1)
{
  static char *begin;
  char *p2;

  ENTER();

  if(p1 != NULL)
    begin = p1;

  p2 = begin;
  if((p1 = strchr(p2, '\n')) != NULL)
  {
    *p1 = '\0';
    begin = ++p1;
  }

  RETURN(p2);
  return p2;
}
///
/// TrimStart
//  Strips leading spaces
char *TrimStart(char *s)
{
  ENTER();

  while(*s && isspace(*s))
    ++s;

  RETURN(s);
  return s;
}
///
/// TrimEnd
//  Removes trailing spaces
char *TrimEnd(char *s)
{
  char *e = s+strlen(s)-1;

  ENTER();

  while(e >= s && isspace(*e))
    *e-- = '\0';

  RETURN(s);
  return s;
}
///
/// Trim
// Removes leading and trailing spaces
char *Trim(char *s)
{
  ENTER();

  if(s != NULL)
  {
    s = TrimStart(s);
    s = TrimEnd(s);
  }

  RETURN(s);
  return s;
}
///
/// stristr
//  Case insensitive version of strstr()
char *stristr(const char *a, const char *b)
{
  char *s = NULL;
  int l = strlen(b);

  ENTER();

  for (; *a; a++)
  {
    if(strnicmp(a, b, l) == 0)
    {
      s = (char *)a;
      break;
    }
  }

  RETURN(s);
  return s;
}
///
/// MyStrChr
//  Searches for a character in string, ignoring text in quotes
char *MyStrChr(const char *s, const char c)
{
  char *result = NULL;
  BOOL nested = FALSE;

  ENTER();

  while(*s != '\0')
  {
    if(*s == '"')
      nested = !nested;
    else if(*s == c && !nested)
    {
      result = (char *)s;
      break;
    }

    s++;
  }

  RETURN(result);
  return result;
}
///
/// AllocStrBuf
//  Allocates a dynamic buffer
char *AllocStrBuf(size_t initlen)
{
  size_t *strbuf;

  ENTER();

  if((strbuf = calloc(initlen+sizeof(size_t), sizeof(char))) != NULL)
    *strbuf++ = initlen;

  RETURN(strbuf);
  return (char *)strbuf;
}
///
/// StrBufCpy
//  Fills a dynamic buffer
char *StrBufCpy(char *strbuf, const char *source)
{
  char *newstrbuf = NULL;
  size_t reqlen = (source != NULL) ? strlen(source) : 0;

  ENTER();

  // if our strbuf is NULL we have to allocate a new buffer
  if(strbuf == NULL)
    strbuf = AllocStrBuf(reqlen+1);

  if(strbuf != NULL)
  {
    size_t oldlen = ((size_t *)strbuf)[-1];
    size_t newlen;

    // make sure we allocate in SIZE_DEFAULT chunks
    for(newlen = oldlen; newlen <= reqlen; newlen += SIZE_DEFAULT);

    // if we have to change the size do it now
    if(newlen != oldlen)
    {
      FreeStrBuf(strbuf);                // free previous buffer
      newstrbuf = AllocStrBuf(newlen+1); // allocate a new one
    }
    else
      newstrbuf = strbuf;

    // do a string copy into the new buffer
    if(newstrbuf != NULL && source != NULL)
      strlcpy(newstrbuf, source, ((size_t *)newstrbuf)[-1]);
  }

  RETURN(newstrbuf);
  return newstrbuf;
}
///
/// StrBufCat
//  String concatenation using a dynamic buffer
char *StrBufCat(char *strbuf, const char *source)
{
  char *newstrbuf = NULL;
  size_t reqlen = (source != NULL) ? strlen(source) : 0;

  ENTER();

  // if our strbuf is NULL we have to allocate a new buffer
  if(strbuf == NULL)
    strbuf = AllocStrBuf(reqlen+1);

  if(strbuf != NULL)
  {
    size_t oldlen = ((size_t *)strbuf)[-1];
    size_t newlen;

    reqlen += strlen(strbuf);

    // make sure we allocate in SIZE_DEFAULT chunks
    for(newlen = oldlen; newlen <= reqlen; newlen += SIZE_DEFAULT);

    // if we have to change the size do it now
    if(newlen != oldlen)
    {
      if((newstrbuf = AllocStrBuf(newlen+1)) != NULL)
        strlcpy(newstrbuf, strbuf, newlen+1);

      FreeStrBuf(strbuf);
    }
    else
      newstrbuf = strbuf;

    // do a string copy into the new buffer
    if(newstrbuf != NULL && source != NULL)
      strlcat(newstrbuf, source, newlen+1);
  }

  RETURN(newstrbuf);
  return newstrbuf;
}
///
/// AppendToBuffer
//  Appends a string to a dynamic-length buffer
char *AppendToBuffer(char *buf, int *wptr, int *len, const char *add)
{
  int nlen;
  int npos;

  ENTER();

  nlen = *len;
  npos = (*wptr)+strlen(add);

  while(npos >= nlen-1)
    nlen = (nlen*3)/2;

  if(nlen != *len)
  {
    // allocate a new buffer and adapt the buffer size information
    buf = realloc(buf, nlen);
    *len = nlen;
  }

  if(buf != NULL)
  {
    // it is save to call strcpy() instead of strlcpy(), because we just
    // made sure to have space for at least the complete <add> string
    strcpy(&buf[*wptr], add);
    // finally update the "end of string" information
    *wptr = npos;
  }

  RETURN(buf);
  return buf;
}
///
/// Decrypt
//  Decrypts passwords
char *Decrypt(char *source)
{
  static char buffer[SIZE_PASSWORD+2];
  char *write = &buffer[SIZE_PASSWORD];

  ENTER();

  *write-- = '\0';
  while(*source != '\0')
  {
    *write-- = ((char)atoi(source)) ^ CRYPTBYTE;
    source += 4;
  }
  write++;

  RETURN(write);
  return write;
}
///
/// Encrypt
//  Encrypts passwords
char *Encrypt(const char *source)
{
  static char buffer[4*SIZE_PASSWORD+2];
  char *read = (char *)(source+strlen(source)-1);

  ENTER();

  *buffer = '\0';
  while(read >= source)
  {
    unsigned char c = (*read--) ^ CRYPTBYTE;
    int p = strlen(buffer);

    snprintf(&buffer[p], sizeof(buffer)-p, "%03d ", c);
  }

  RETURN(buffer);
  return buffer;
}
///
/// UnquoteString
//  Removes quotes from a string, skipping "escaped" quotes
char *UnquoteString(const char *s, BOOL new)
{
  char *ans;
  char *o = (char *)s;

  ENTER();

  // check if the string contains any quote chars
  // at all
  if(strchr(s, '"') == NULL)
  {
    if(new)
      o = strdup(s);

    RETURN(o);
    return(o);
  }

  // now start unquoting the string
  if((ans = malloc(strlen(s)+1)))
  {
    char *t = ans;

    while(*s)
    {
      if(*s == '\\')
        *t++ = *++s;
      else if(*s == '"')
        ; // nothing
      else
        *t++ = *s;

      ++s;
    }

    *t = '\0';

    // in case the user wants to have the copy lets do it
    if(new)
    {
      RETURN(ans);
      return ans;
    }

    // otherwise overwrite the original string array
    strcpy(o, ans);

    free(ans);
  }

  RETURN(o);
  return o;
}
///

/*** File related ***/
/// GetLine
// gets a NUL terminated line of a file handle and strips any
// trailing CR or LF
ssize_t GetLine(char **buffer, size_t *size, FILE *fh)
{
  ssize_t len;
  char *result = NULL;

  ENTER();

  if((len = getline(buffer, size, fh)) > 0)
  {
    char *buf = *buffer;

    // strip possible CR or LF characters at the end of the line
    if(buf[len-1] == '\n')
    {
      // search for possible CR or LF characters and adjust the length information accordingly
      if(len > 1 && buf[len-2] == '\r')
        len -= 2;
      else
        len -= 1;

      buf[len] = '\0';
    }

    result = *buffer;
  }
  #if defined(DEBUG)
  else
  {
    if(feof(fh) == 0 || ferror(fh) != 0)
    {
      // something bad happened, so we return NULL to signal abortion
      W(DBF_MAIL, "getline() in GetLine() returned NULL and feof()=%ld || ferror()=%ld", feof(fh), ferror(fh));
    }
  }
  #endif

  RETURN(len);
  return len;
}

///
/// RenameFile
//  Renames a file and restores the protection bits
BOOL RenameFile(const char *oldname, const char *newname)
{
  BOOL result = FALSE;

  ENTER();

  if(Rename(oldname, newname))
  {
    // the rename succeeded, now change the file permissions

    #if defined(__amigaos4__)
    struct ExamineData *ed;

    if((ed = ExamineObjectTags(EX_StringName, newname, TAG_DONE)) != NULL)
    {
      ULONG prots = ed->Protection;

      FreeDosObject(DOS_EXAMINEDATA, ed);
      if(SetProtection(newname, prots & ~EXDF_ARCHIVE))
        result = TRUE;
    }
    #else
    struct FileInfoBlock *fib;

    if((fib = AllocDosObject(DOS_FIB,NULL)) != NULL)
    {
      BPTR lock;

      if((lock = Lock(newname, ACCESS_READ)))
      {
        if(Examine(lock, fib))
        {
          UnLock(lock);
          if(SetProtection(newname, fib->fib_Protection & ~FIBF_ARCHIVE))
            result = TRUE;
        }
        else
          UnLock(lock);
      }
      FreeDosObject(DOS_FIB, fib);
    }
    #endif
  }

  RETURN(result);
  return result;
}
///
/// CopyFile
//  Copies a file
BOOL CopyFile(const char *dest, FILE *destfh, const char *sour, FILE *sourfh)
{
  BOOL success = FALSE;
  char *buf;

  ENTER();

  // allocate a dynamic buffer instead of placing it on the stack
  if((buf = malloc(SIZE_FILEBUF)) != NULL)
  {
    if(sour != NULL && (sourfh = fopen(sour, "r")) != NULL)
      setvbuf(sourfh, NULL, _IOFBF, SIZE_FILEBUF);

    if(sourfh != NULL && dest != NULL && (destfh = fopen(dest, "w")) != NULL)
      setvbuf(destfh, NULL, _IOFBF, SIZE_FILEBUF);

    if(sourfh != NULL && destfh != NULL)
    {
      int len;

      while((len = fread(buf, 1, SIZE_FILEBUF, sourfh)) > 0)
      {
        if(fwrite(buf, 1, len, destfh) != (size_t)len)
          break;
      }

      // if we arrived here because this was the eof of the sourcefile
      // and non of the two filehandles are in error state we can set
      // success to TRUE.
      if(feof(sourfh) && !ferror(sourfh) && !ferror(destfh))
        success = TRUE;
    }

    if(dest != NULL && destfh != NULL)
      fclose(destfh);

    if(sour != NULL && sourfh != NULL)
      fclose(sourfh);

    free(buf);
  }

  RETURN(success);
  return success;
}
///
/// MoveFile
//  Moves a file (also from one partition to another)
BOOL MoveFile(const char *oldfile, const char *newfile)
{
  BOOL success = TRUE;

  ENTER();

  // we first try to rename the file with a standard Rename()
  // and if it doesn't work we do a raw copy
  if(RenameFile(oldfile, newfile) == FALSE)
  {
    // a normal rename didn't work, so lets copy the file
    if(CopyFile(newfile, 0, oldfile, 0) == FALSE ||
       DeleteFile(oldfile) == 0)
    {
      // also a copy didn't work, so lets return an error
      success = FALSE;
    }
  }

  RETURN(success);
  return success;
}
///
/// ConvertCRLF
//  Converts line breaks from LF to CRLF or vice versa
BOOL ConvertCRLF(char *in, char *out, BOOL to)
{
  BOOL success = FALSE;
  FILE *infh;

  ENTER();

  if((infh = fopen(in, "r")))
  {
    FILE *outfh;

    setvbuf(infh, NULL, _IOFBF, SIZE_FILEBUF);

    if((outfh = fopen(out, "w")))
    {
      char *buf = NULL;
      size_t size = 0;

      setvbuf(outfh, NULL, _IOFBF, SIZE_FILEBUF);

      while(GetLine(&buf, &size, infh) >= 0)
        fprintf(outfh, "%s%s\n", buf, to?"\r":"");

      success = TRUE;
      fclose(outfh);

      if(buf != NULL)
        free(buf);
    }

    fclose(infh);
  }

  RETURN(success);
  return success;
}
///
/// Word_Length
//  returns the string length of the next word
static int Word_Length(const char *buf)
{
  unsigned char c;
  int len = 0;

  while((c = *buf))
  {
    if(isspace(c))
    {
      if(c == '\n' || c == '\r')
        return 0;

      len++;
    }
    else break;

    buf++;
  }


  while((c = *buf))
  {
    if(isspace(c) || c == '\0')
      break;

    len++;
    buf++;
  }

  return len;
}
///
/// Quoting_Chars
//  Determines and copies all quoting characters ">" to the buffer "buf"
//  out of the supplied text. It also returns the number of skipable
//  characters since the start of line like "JL>"
static int Quoting_Chars(char *buf, const int len, const char *text, int *post_spaces)
{
  unsigned char c;
  BOOL quote_found = FALSE;
  int i=0;
  int last_bracket = 0;
  int skip_chars = 0;
  int pre_spaces = 0;

  ENTER();

  (*post_spaces) = 0;

  while((c = *text++) && i < len-1)
  {
    if(c == '>')
    {
      last_bracket = i+1;

      quote_found = TRUE;
    }
    else
    {
      // if the current char is a newline or not between A-Z or a-z then we
      // can break out immediately as these chars are not allowed
      if(c == '\n' || (c != ' ' && (c < 'A' || c > 'z' || (c > 'Z' && c < 'a'))))
        break;

      if(c == ' ')
      {
        if(quote_found == TRUE)
        {
          // if we end up here we can count the number of spaces
          // after the quoting characters
          (*post_spaces)++;
        }
        else if(skip_chars == 0)
        {
          pre_spaces++;
        }
        else
          break;
      }
      else if(quote_found == TRUE || skip_chars > 2)
      {
        break;
      }
      else
        skip_chars++;
    }

    buf[i++] = c;
  }

  buf[last_bracket] = '\0';

  // if we got some spaces before anything else,
  // we put the amount of found pre_spaces in the post_spaces variable
  // instead
  if(pre_spaces > 0)
    (*post_spaces) = pre_spaces;

  // return the number of skipped chars before
  // any quote char was found.
  RETURN(last_bracket ? skip_chars+pre_spaces : 0);
  return last_bracket ? skip_chars+pre_spaces : 0;
}

///
/// QuoteText
//  Main mail text quotation function. It takes the source string "src" and
//  analyzes it concerning existing quoting characters. Depending on this
//  information it adds new quoting marks "prefix" to the start of each line
//  taking care of a correct word wrap if the line gets longs than "line_max".
//  All output is directly written to the already opened filehandle "out".
void QuoteText(FILE *out, const char *src, const int len, const int line_max)
{
  ENTER();

  // make sure the output file handle is valid
  if(out)
  {
    char temp_buf[128];
    int temp_len;
    BOOL newline = TRUE;
    BOOL wrapped = FALSE; // needed to implement automatic wordwrap while quoting
    BOOL lastwasspace = FALSE;
    int skip_on_next_newline = 0;
    int line_len = 0;
    int skip_chars;
    int post_spaces = 0;
    int srclen = len;

    // find out how many quoting chars the next line has
    skip_chars = Quoting_Chars(temp_buf, sizeof(temp_buf), src, &post_spaces);
    temp_len = strlen(temp_buf) - skip_chars;
    src += skip_chars;
    srclen -= skip_chars;

    while(srclen > 0)
    {
      char c = *src;

      // break out if we received a NUL byte, because this
      // should really never happen
      if(c == '\0')
        break;

      // skip any LF
      if(c == '\r')
      {
        src++;
        srclen--;
        continue;
      }

      // on a CR (newline)
      if(c == '\n')
      {
        src++;
        srclen--;

        // find out how many quoting chars the next line has
        skip_chars = Quoting_Chars(temp_buf, sizeof(temp_buf), src, &post_spaces);
        src += (skip_chars + skip_on_next_newline);
        srclen -= (skip_chars + skip_on_next_newline);
        skip_on_next_newline = 0;

        if(temp_len == ((int)strlen(temp_buf)-skip_chars) && wrapped)
        {
          // the text has been wrapped previously and the quoting chars
          // are the same like the previous line, so the following text
          // probably belongs to the same paragraph

          srclen -= temp_len; // skip the quoting chars
          src += temp_len;
          wrapped = FALSE;

          // check whether the next char will be a newline or not, because
          // a newline indicates a new empty line, so there is no need to
          // cat something together at all
          if(*src != '\n')
          {
            // add a space to if this was the first quoting
            if(lastwasspace == FALSE && (temp_len == 0 || *src != ' '))
            {
              fputc(' ', out);
              line_len++;
              lastwasspace = TRUE;
            }

            continue;
          }
        }

        temp_len = strlen(temp_buf)-skip_chars;
        wrapped = FALSE;

        // check whether this line would be zero or not and if so we
        // have to care about if the user wants to also quote empty lines
        if(line_len == 0 && C->QuoteEmptyLines)
          fputs(C->QuoteChar, out);

        // then put a newline in our file
        fputc('\n', out);
        newline = TRUE;
        lastwasspace = FALSE;

        line_len = 0;

        continue;
      }

      if(newline)
      {
        if(c == '>')
        {
          fputs(C->QuoteChar, out);
          line_len += strlen(C->QuoteChar);
        }
        else
        {
          fputs(C->QuoteChar, out);
          fputc(' ', out);
          line_len += strlen(C->QuoteChar)+1;
        }

        newline = FALSE;
      }

      // we check whether this char was a whitespace
      // or not and if so we set the lastwasspace flag and we also check if
      // we are near the end of the line so that we have to initiate a word wrap
      if((lastwasspace = isspace(c)) && line_len + Word_Length(src) >= line_max)
      {
        char *indent;

        src++;
        srclen--;

        // output a newline to start a new line
        fputc('\n', out);

        // reset line_len
        line_len = 0;

        fputs(C->QuoteChar, out);
        line_len += strlen(C->QuoteChar);

        if(strlen(temp_buf))
        {
          fputs(temp_buf+skip_chars, out);
          line_len += strlen(temp_buf)-skip_chars;
          lastwasspace = FALSE;
        }
        else
        {
          fputc(' ', out);
          line_len++;
          lastwasspace = TRUE;
        }

        // lets check the indention of the next line
        if((indent = strchr(src, '\n')) && ++indent != '\0')
        {
          int pre_spaces;

          Quoting_Chars(temp_buf, sizeof(temp_buf), indent, &pre_spaces);

          skip_on_next_newline = pre_spaces;

          if(pre_spaces == 0)
            pre_spaces += post_spaces;

          while(pre_spaces--)
          {
            fputc(' ', out);
            line_len++;
            lastwasspace = TRUE;
          }
        }

        wrapped = TRUE; // indicates that a word has been wrapped manually
        continue;
      }

      fputc(c, out);
      line_len++;

      src++;
      srclen--;
    }

    // check whether we finished the quoting with
    // a newline or otherwise the followed signature won't fit correctly
    if(newline == FALSE)
      fputc('\n', out);
  }

  LEAVE();
}
///
/// SimpleWordWrap
//  Reformats a file to a new line length
void SimpleWordWrap(char *filename, int wrapsize)
{
  BPTR fh;

  ENTER();

  if((fh = Open((STRPTR)filename, MODE_OLDFILE)))
  {
    char ch;
    int p = 0;
    int lsp = -1;
    int sol = 0;

    while(Read(fh, &ch, 1) == 1)
    {
      if(p - sol > wrapsize && lsp >= 0)
      {
        ch = '\n';
        ChangeFilePosition(fh, (LONG)lsp - p - 1, OFFSET_CURRENT);
        p = lsp;
        Write(fh, &ch, 1);
      }

      if(isspace(ch))
        lsp = p;
      if(ch == '\n')
      {
        sol = p + 1;
        lsp = -1;
      }
      p++;
    }
    Close(fh);
  }

  LEAVE();
}
///
/// ReqFile
//  Puts up a file requester
struct FileReqCache *ReqFile(enum ReqFileType num, Object *win,
                             const char *title, int mode,
                             const char *drawer, const char *file)
{
  // the following arrays depend on the ReqFileType enumeration
  static const char *const acceptPattern[ASL_MAX] =
  {
    "#?.addressbook#?",                    // ASL_ABOOK
    "#?.config#?",                         // ASL_CONFIG
    NULL,                                  // ASL_DETACH
    "~(#?.info)",                          // ASL_ATTACH
    "#?.(yam|rexx|rx)",                    // ASL_REXX
    "#?.(gif|jpg|jpeg|png|iff|ilbm)",      // ASL_PHOTO
    "#?.((mbx|mbox|eml|dbx|msg)|#?,#?)",   // ASL_IMPORT
    "#?.(mbx|mbox)",                       // ASL_EXPORT
    NULL,                                  // ASL_FOLDER
    "#?.(ldif|ldi)",                       // ASL_ABOOK_LIF
    "#?.csv",                              // ASL_ABOOK_CSV
    "#?.(tab|txt)",                        // ASL_ABOOK_TAB
    "#?.xml",                              // ASL_ABOOK_XML
  };

  struct FileRequester *fileReq;
  struct FileReqCache *result = NULL;

  ENTER();

  // allocate the required data for our file requester
  if((fileReq = MUI_AllocAslRequest(ASL_FileRequest, NULL)) != NULL)
  {
    const char *pattern = acceptPattern[num];
    struct FileReqCache *frc = G->FileReqCache[num];
    BOOL reqResult;
    BOOL usefrc = frc->used;

    // do the actual file request now
    reqResult = MUI_AslRequestTags(fileReq,
                                   ASLFR_Window,         xget(win, MUIA_Window_Window),
                                   ASLFR_TitleText,      title,
                                   ASLFR_PositiveText,   hasSaveModeFlag(mode) ? tr(MSG_UT_Save) : tr(MSG_UT_Load),
                                   ASLFR_DoSaveMode,     hasSaveModeFlag(mode),
                                   ASLFR_DoMultiSelect,  hasMultiSelectFlag(mode),
                                   ASLFR_DrawersOnly,    hasDrawersOnlyFlag(mode),
                                   ASLFR_RejectIcons,    FALSE,
                                   ASLFR_DoPatterns,     pattern != NULL,
                                   ASLFR_InitialFile,    file,
                                   ASLFR_InitialDrawer,  usefrc ? frc->drawer : drawer,
                                   ASLFR_InitialPattern, pattern ? pattern : "#?",
                                   usefrc ? ASLFR_InitialLeftEdge : TAG_IGNORE, frc->left_edge,
                                   usefrc ? ASLFR_InitialTopEdge  : TAG_IGNORE, frc->top_edge,
                                   usefrc ? ASLFR_InitialWidth    : TAG_IGNORE, frc->width,
                                   usefrc ? ASLFR_InitialHeight   : TAG_IGNORE, frc->height,
                                   TAG_DONE);

    // copy the data out of our fileRequester into our
    // own cached structure we return to the user
    if(reqResult)
    {
      // free previous resources
      FreeFileReqCache(frc);

      // copy all necessary data from the ASL filerequester structure
      // to our cache
      frc->file     = strdup(fileReq->fr_File);
      frc->drawer   = strdup(fileReq->fr_Drawer);
      frc->pattern  = strdup(fileReq->fr_Pattern);
      frc->numArgs  = fileReq->fr_NumArgs;
      frc->left_edge= fileReq->fr_LeftEdge;
      frc->top_edge = fileReq->fr_TopEdge;
      frc->width    = fileReq->fr_Width;
      frc->height   = fileReq->fr_Height;
      frc->used     = TRUE;

      // now we copy the optional arglist
      if(fileReq->fr_NumArgs > 0)
      {
        if((frc->argList = calloc(sizeof(char*), fileReq->fr_NumArgs)) != NULL)
        {
          int i;

          for(i=0; i < fileReq->fr_NumArgs; i++)
            frc->argList[i] = strdup(fileReq->fr_ArgList[i].wa_Name);
        }
      }
      else
        frc->argList = NULL;

      // everything worked out fine, so lets return
      // our globally cached filereq structure.
      result = frc;
    }
    else if(IoErr() != 0)
    {
      // and IoErr() != 0 signals that something
      // serious happend and that we have to inform the
      // user
      ER_NewError(tr(MSG_ER_CANTOPENASL));

      // beep the display as well
      DisplayBeep(NULL);
    }


    // free the ASL request structure again.
    MUI_FreeAslRequest(fileReq);
  }
  else
    ER_NewError(tr(MSG_ErrorAslStruct));

  RETURN(result);
  return result;
}
///
/// FreeFileReqCache
// free all structures inside a filerequest cache structure
void FreeFileReqCache(struct FileReqCache *frc)
{
  ENTER();

  if(frc->file != NULL)
    free(frc->file);

  if(frc->drawer != NULL)
    free(frc->drawer);

  if(frc->pattern != NULL)
    free(frc->pattern);

  if(frc->numArgs > 0)
  {
    int j;

    for(j=0; j < frc->numArgs; j++)
      free(frc->argList[j]);

    free(frc->argList);
  }

  LEAVE();
}
///
/// AddZombieFile
//  add an orphaned file to the zombie file list
void AddZombieFile(const char *fileName)
{
  struct ZombieFile *zombie;

  ENTER();

  if((zombie = malloc(sizeof(*zombie))) != NULL)
  {
    if((zombie->fileName = strdup(fileName)) != NULL)
    {
      AddTail((struct List *)&G->zombieFileList, (struct Node *)&zombie->node);

      D(DBF_UTIL, "added file '%s' to the zombie list", fileName);

      // trigger the retry mechanism in 5 minutes
      RestartTimer(TIMER_DELETEZOMBIEFILES, 5 * 60, 0);
    }
    else
      free(zombie);
  }

  LEAVE();
}
///
/// DeleteZombieFiles
//  try to delete all files in the list of zombie files
BOOL DeleteZombieFiles(BOOL force)
{
  BOOL listCleared = TRUE;

  ENTER();

  if(IsListEmpty((struct List *)&G->zombieFileList) == FALSE)
  {
    struct MinNode *curNode;

    for(curNode = G->zombieFileList.mlh_Head; curNode->mln_Succ; )
    {
      struct ZombieFile *zombie = (struct ZombieFile *)curNode;

      // save the pointer to the next zombie first, as we probably are going to Remove() this node later
      curNode = curNode->mln_Succ;

      D(DBF_UTIL, "trying to delete zombie file '%s'", zombie->fileName);

      // try again to delete the file, if it still exists
      if(force == FALSE && FileExists(zombie->fileName) == TRUE && DeleteFile(zombie->fileName) == 0)
      {
        // deleting failed again, but we are allowed to retry
        listCleared = FALSE;

        W(DBF_UTIL, "zombie file '%s' cannot be deleted, leaving in list", zombie->fileName);
      }
      else
      {
        // remove and free this node
        Remove((struct Node *)zombie);
        free(zombie->fileName);
        free(zombie);
      }
    }
  }

  RETURN(listCleared);
  return listCleared;
}
///
/// OpenTempFile
//  Creates or opens a temporary file
struct TempFile *OpenTempFile(const char *mode)
{
  struct TempFile *tf;

  ENTER();

  if((tf = calloc(1, sizeof(struct TempFile))) != NULL)
  {
    // the tempfile MUST be SIZE_MFILE long because we
    // also use this tempfile routine for showing temporary mails which
    // conform to SIZE_MFILE
    char buf[SIZE_MFILE];

    // now format our temporary filename according to our Application data
    // this format tries to make the temporary filename kinda unique.
    snprintf(buf, sizeof(buf), "YAMt%08lx.tmp", GetUniqueID());

    // now add the temporary path to the filename
    AddPath(tf->Filename, C->TempDir, buf, sizeof(tf->Filename));

    if(mode != NULL)
    {
      if((tf->FP = fopen(tf->Filename, mode)) == NULL)
      {
        E(DBF_UTIL, "couldn't create temporary file: '%s'", tf->Filename);

        // on error we free everything
        free(tf);
        tf = NULL;
      }
      else
        setvbuf(tf->FP, NULL, _IOFBF, SIZE_FILEBUF);
    }
  }

  RETURN(tf);
  return tf;
}
///
/// CloseTempFile
//  Closes a temporary file
void CloseTempFile(struct TempFile *tf)
{
  ENTER();

  if(tf != NULL)
  {
    if(tf->FP != NULL)
      fclose(tf->FP);

    D(DBF_UTIL, "DeleteTempFile: %s", tf->Filename);
    if(DeleteFile(tf->Filename) == 0)
      AddZombieFile(tf->Filename);

    free(tf);
  }

  LEAVE();
}
///
/// DumpClipboard
//  Exports contents of clipboard unit 0 to a file
#define ID_FTXT   MAKE_ID('F','T','X','T')
#define ID_CHRS   MAKE_ID('C','H','R','S')
BOOL DumpClipboard(FILE *out)
{
  BOOL success = FALSE;
  struct IFFHandle *iff;

  ENTER();

  if((iff = AllocIFF()) != NULL)
  {
    if((iff->iff_Stream = (ULONG)OpenClipboard(PRIMARY_CLIP)) != 0)
    {
      InitIFFasClip(iff);
      if(OpenIFF(iff, IFFF_READ) == 0)
      {
        if(StopChunk(iff, ID_FTXT, ID_CHRS) == 0)
        {
          while(TRUE)
          {
            struct ContextNode *cn;
            long error;
            long rlen;
            UBYTE readbuf[SIZE_DEFAULT];

            error = ParseIFF(iff, IFFPARSE_SCAN);
            if(error == IFFERR_EOC)
              continue;
            else if(error != 0)
              break;

            if((cn = CurrentChunk(iff)) != NULL)
            {
              if(cn->cn_Type == ID_FTXT && cn->cn_ID == ID_CHRS)
              {
                success = TRUE;
                while((rlen = ReadChunkBytes(iff, readbuf, SIZE_DEFAULT)) > 0)
                  fwrite(readbuf, 1, (size_t)rlen, out);
              }
            }
          }
        }
        CloseIFF(iff);
      }
      CloseClipboard((struct ClipboardHandle *)iff->iff_Stream);
    }
    FreeIFF(iff);
  }

  RETURN(success);
  return success;
}
///
/// IsFolderDir
//  Checks if a directory is used as a mail folder
static BOOL IsFolderDir(const char *dir)
{
  BOOL result = FALSE;
  char *filename;
  int i;

  ENTER();

  filename = (char *)FilePart(dir);

  for(i = 0; i < FT_NUM; i++)
  {
    if(FolderName[i] != NULL && stricmp(filename, FolderName[i]) == 0)
    {
      result = TRUE;
      break;
    }
  }

  if(result == FALSE)
  {
    char fname[SIZE_PATHFILE];

    result = (FileExists(AddPath(fname, dir, ".fconfig", sizeof(fname))) ||
              FileExists(AddPath(fname, dir, ".index", sizeof(fname))));
  }

  RETURN(result);
  return result;
}
///
/// AllFolderLoaded
//  Checks if all folder index are correctly loaded
BOOL AllFolderLoaded(void)
{
  BOOL allLoaded = TRUE;

  ENTER();

  LockFolderListShared(G->folders);

  if(IsFolderListEmpty(G->folders) == FALSE)
  {
    struct FolderNode *fnode;

    ForEachFolderNode(G->folders, fnode)
    {
      if(fnode->folder->LoadedMode != LM_VALID && !isGroupFolder(fnode->folder))
      {
        allLoaded = FALSE;
        break;
      }
    }
  }
  else
    allLoaded = FALSE;

  UnlockFolderList(G->folders);

  RETURN(allLoaded);
  return allLoaded;
}
///
/// DeleteMailDir (rec)
//  Recursively deletes a mail directory
BOOL DeleteMailDir(const char *dir, BOOL isroot)
{
  BOOL result = TRUE;
  APTR context;

  ENTER();

  if((context = ObtainDirContextTags(EX_StringName,   (ULONG)dir,
                                     EX_DoCurrentDir, TRUE,
                                     TAG_DONE)) != NULL)
  {
    struct ExamineData *ed;

    while((ed = ExamineDir(context)) != NULL && result == TRUE)
    {
      BOOL isdir = EXD_IS_DIRECTORY(ed);
      char *filename = (char *)ed->Name;
      char fname[SIZE_PATHFILE];

      AddPath(fname, dir, filename, sizeof(fname));

      if(isroot == TRUE)
      {
        if(isdir == TRUE)
        {
          if(IsFolderDir(fname) == TRUE)
            result = DeleteMailDir(fname, FALSE);
        }
        else
        {
          if(stricmp(filename, ".config")      == 0 ||
             stricmp(filename, ".glossary")    == 0 ||
             stricmp(filename, ".addressbook") == 0 ||
             stricmp(filename, ".emailcache")  == 0 ||
             stricmp(filename, ".folders")     == 0 ||
             stricmp(filename, ".spamdata")    == 0 ||
             stricmp(filename, ".signature")   == 0 ||
             stricmp(filename, ".uidl")        == 0)
          {
            if(DeleteFile(fname) == 0)
              result = FALSE;
          }
        }
      }
      else if(isdir == FALSE)
      {
        if(isValidMailFile(filename) == TRUE  ||
           stricmp(filename, ".fconfig") == 0 ||
           stricmp(filename, ".fimage") == 0  ||
           stricmp(filename, ".index") == 0)
        {
          if(DeleteFile(fname) == 0)
            result = FALSE;
        }
      }
    }

    ReleaseDirContext(context);

    if(result == TRUE && DeleteFile(dir) == 0)
      result = FALSE;
  }
  else
    result = FALSE;

  RETURN(result);
  return result;
}
///
/// FileToBuffer
//  Reads a complete file into memory
char *FileToBuffer(const char *file)
{
  char *text = NULL;
  LONG size;

  ENTER();

  if(ObtainFileInfo(file, FI_SIZE, &size) == TRUE &&
     size > 0 && (text = malloc((size+1)*sizeof(char))) != NULL)
  {
    FILE *fh;

    text[size] = '\0'; // NUL-terminate the string

    if((fh = fopen(file, "r")) != NULL)
    {
      if(fread(text, sizeof(char), size, fh) != (size_t)size)
      {
        free(text);
        text = NULL;
      }

      fclose(fh);
    }
    else
    {
      free(text);
      text = NULL;
    }
  }

  RETURN(text);
  return text;
}
///
/// FileCount
// returns the total number of files matching a pattern that are in a directory
// or -1 if an error occurred.
LONG FileCount(const char *directory, const char *pattern)
{
  APTR context;
  char *parsedPattern;
  LONG parsedPatternSize;
  LONG result = 0;

  ENTER();

  if(pattern == NULL)
    pattern = "#?";

  parsedPatternSize = strlen(pattern) * 2 + 2;
  if((parsedPattern = malloc(parsedPatternSize)) != NULL)
  {
    ParsePatternNoCase(pattern, parsedPattern, parsedPatternSize);

    #if defined(__amigaos4__)
    if((context = ObtainDirContextTags(EX_StringName,  (ULONG)directory,
                                       EX_MatchString, (ULONG)parsedPattern,
                                       EX_MatchFunc,   (DOSBase->lib_Version == 52 && DOSBase->lib_Revision < 17) ? &ExamineDirMatchHook : NULL,
                                       TAG_DONE)) != NULL)
    #else
    if((context = ObtainDirContextTags(EX_StringName,  (ULONG)directory,
                                       EX_MatchString, (ULONG)parsedPattern,
                                       TAG_DONE)) != NULL)
    #endif
    {
      struct ExamineData *ed;

      while((ed = ExamineDir(context)) != NULL)
      {
        // count the entries
        result++;
      }

      if(IoErr() != ERROR_NO_MORE_ENTRIES)
      {
        E(DBF_ALWAYS, "FileCount failed");
        result = -1;
      }

      ReleaseDirContext(context);
    }

    free(parsedPattern);
  }
  else
    result = -1;

  RETURN(result);
  return result;
}
///
/// AddPath
// Function that is a wrapper to AddPart so that we can add the
// specified path 'add' to an existing/non-existant 'src' which
// is then stored in dst of max size 'size'.
char *AddPath(char *dst, const char *src, const char *add, size_t size)
{
  ENTER();

  strlcpy(dst, src, size);
  if(AddPart(dst, add, size) == FALSE)
  {
    E(DBF_ALWAYS, "AddPath()/AddPart() buffer overflow detected!");
    dst = NULL;
  }

  RETURN(dst);
  return dst;
}
///

/*** Mail related ***/
/// CreateFilename
//  Prepends mail directory to a file name
char *CreateFilename(const char * const file)
{
  static char buffer[SIZE_PATHFILE];

  ENTER();

  AddPath(buffer, G->MA_MailDir, file, sizeof(buffer));

  RETURN(buffer);
  return buffer;
}
///
/// CreateDirectory
//  Makes a directory
BOOL CreateDirectory(const char *dir)
{
  BOOL success = FALSE;

  ENTER();

  // check if dir isn't empty
  if(dir[0] != '\0')
  {
    enum FType ft;

    if(ObtainFileInfo(dir, FI_TYPE, &ft) == TRUE)
    {
      if(ft == FIT_DRAWER)
        success = TRUE;
      else if(ft == FIT_NONEXIST)
      {
        char buf[SIZE_PATHFILE];
        BPTR fl;
        size_t len = strlen(dir)-1;

        // check for trailing slashes
        if(dir[len] == '/')
        {
          // we make a copy of dir first because
          // we are not allowed to modify it
          strlcpy(buf, dir, sizeof(buf));

          // remove all trailing slashes
          while(len > 0 && buf[len] == '/')
            buf[len--] = '\0';

          // set dir to our buffer
          dir = buf;
        }

        // use utility/CreateDir() to create the
        // directory
        if((fl = CreateDir(dir)))
        {
          UnLock(fl);
          success = TRUE;
        }
      }
    }

    if(G->MA != NULL && success == FALSE)
      ER_NewError(tr(MSG_ER_CantCreateDir), dir);
  }

  RETURN(success);
  return success;
}
///
/// GetFolderDir
//  Returns path of a folder directory
const char *GetFolderDir(const struct Folder *fo)
{
  static char buffer[SIZE_PATH];
  const char *dir;

  ENTER();

  if(strchr(fo->Path, ':') != NULL)
    dir = fo->Path;
  else
  {
    AddPath(buffer, G->MA_MailDir, fo->Path, sizeof(buffer));
    dir = buffer;
  }

  RETURN(dir);
  return dir;
}
///
/// GetMailFile
//  Returns path of a message file
char *GetMailFile(char *string, const struct Folder *folder, const struct Mail *mail)
{
  static char buffer[SIZE_PATHFILE];
  char *result;

  ENTER();

  if(folder == NULL && mail != NULL)
    folder = mail->Folder;

  if(string == NULL)
    string = buffer;

  AddPath(string, (folder == NULL || folder == (struct Folder *)-1) ? C->TempDir : GetFolderDir(folder), mail->MailFile, SIZE_PATHFILE);

  result = GetRealPath(string);

  RETURN(result);
  return result;
}
///
/// BuildAddress
// Creates "Real Name <E-mail>" string from a given address and name
// according to the rules defined in RFC2822, which in fact takes care
// of quoatation of the real name as well as escaping some special characters
char *BuildAddress(char *buffer, size_t buflen, const char *address, const char *name)
{
  ENTER();

  // check that buffer is != NULL
  if(buffer != NULL)
  {
    // check if a real name is given at all
    // or not
    if(name != NULL && name[0] != '\0')
    {
      // search for some chars which, when present,
      // require us to put the real name into quotations
      // see RFC2822 (section 3.2.1) - However, we don't
      // include "." here because of the comments in section 4.1
      // of RFC2822, which states that "." is a valid char in a
      // "phrase" token and don't need to be escaped.
      if(strpbrk(name, "()<>[]:;@\\,\"") != NULL) // check for 'specials' excluding '.'
      {
        char quotedstr[SIZE_REALNAME];
        const char *s = name;
        char *d;

        // we now have to search for a '"' quotation char or for
        // an escape '\' char which we need to escape via '\', if
        // it exists in the specified real name
        if((d = strpbrk(s, "\"\\")) != NULL)
        {
          quotedstr[0] = '\0';

          // now iterate through s and escape any char
          // we require to escape
          do
          {
            size_t qlen;

            // copy everything until the first escapable char
            if(d-s > 0)
            {
              qlen = strlen(quotedstr)+(d-s)+1;
              strlcat(quotedstr, s, (qlen <= sizeof(quotedstr)) ? qlen : sizeof(quotedstr));
            }

            // add the escape char + the char we want to escape
            qlen = strlen(quotedstr);
            if(qlen+2 < sizeof(quotedstr))
            {
              quotedstr[qlen]   = '\\';
              quotedstr[qlen+1] = *d;
              quotedstr[qlen+2] = '\0';
            }

            // prepare the next iteration
            s = d+1;
          }
          while((d = strpbrk(s, "\"\\")) != NULL);

          // check if there is anything left
          // to attach to quotedstr
          if(s < (name+strlen(name)))
            strlcat(quotedstr, s, sizeof(quotedstr));
        }
        else
        {
          // otherwise simply output the real name
          strlcpy(quotedstr, name, sizeof(quotedstr));
        }

        // add the addr-spec string
        snprintf(buffer, buflen, "\"%s\" <%s>", quotedstr, address);
      }
      else
        snprintf(buffer, buflen, "%s <%s>", name, address);
    }
    else
      strlcpy(buffer, address, buflen);
  }
  else
    E(DBF_UTIL, "BuildAddress buffer==NULL error!");

  RETURN(buffer);
  return buffer;
}

///
/// ExtractAddress
//  Extracts e-mail address and real name according to RFC2822 (section 3.4)
void ExtractAddress(const char *line, struct Person *pe)
{
  char *save;

  ENTER();

  pe->Address[0] = '\0';
  pe->RealName[0] = '\0';

  // create a temp copy of our source
  // string so that we don't have to alter it.
  if((save = strdup(line)) != NULL)
  {
    char *p = save;
    char *start;
    char *end;
    char *address = NULL;
    char *realname = NULL;

    // skip leading whitespaces
    while(isspace(*p))
      p++;

    // we first try to extract the email address part of the line in case
    // the email is in < > brackets (see RFC2822)
    //
    // something like: "Realname <mail@address.net>"
    if((start = MyStrChr(p, '<')) && (end = MyStrChr(start, '>')))
    {
      *start = '\0';
      *end = '\0';

      // now we have successfully extract the
      // email address between start and end
      address = ++start;

      // per definition of RFC 2822, the realname (display name)
      // should be in front of the email, but we will extract it later on
      realname = p;
    }

    // if we haven't found the email yet
    // we might have search for something like "mail@address.net (Realname)"
    if(address == NULL)
    {
      // extract the mail address first
      for(start=end=p; *end && !isspace(*end) && *end != ',' && *end != '('; end++);

      // now we should have the email address
      if(end > start)
      {
        char *s = NULL;

        if(*end != '\0')
        {
          *end = '\0';
          s = end+1;
        }

        // we have the mail address
        address = start;

        // we should have the email address now so we go and extract
        // the realname encapsulated in ( )
        if(s && (s = strchr(s, '(')))
        {
          start = ++s;

          // now we search for the last closing )
          end = strrchr(start, ')');
          if(end)
            *end = '\0';
          else
            end = start+strlen(start);

          realname = start;
        }
      }
    }

    // we successfully found an email adress, so we go
    // and copy it into our person's structure.
    if(address)
      strlcpy(pe->Address,  Trim(address), sizeof(pe->Address));

    // in case we found a descriptive realname we go and
    // parse it for quoted and escaped passages.
    if(realname)
    {
      unsigned int i;
      BOOL quoted = FALSE;

      // as a realname may be quoted '"' and also may contain escaped sequences
      // like '\"', we extract the realname more carefully here.
      p = Trim(realname);

      // check if the realname is quoted or not
      if(*p == '"')
      {
        quoted = TRUE;
        p++;
      }

      for(i=0; *p && i < sizeof(pe->RealName); i++, p++)
      {
        if(quoted)
        {
          if(*p == '\\')
            p++;
          else if(*p == '"' && strlen(p) == 1)
            break;
        }

        if(*p)
          pe->RealName[i] = *p;
        else
          break;
      }

      // make sure we properly NUL-terminate
      // the string
      if(i < sizeof(pe->RealName))
        pe->RealName[i] = '\0';
      else
        pe->RealName[sizeof(pe->RealName)-1] = '\0';
    }

    D(DBF_MIME, "addr: '%s'", pe->Address);
    D(DBF_MIME, "real: '%s'", pe->RealName);

    free(save);
  }

  LEAVE();
}
///
/// DescribeCT
//  Returns description of a content type
const char *DescribeCT(const char *ct)
{
  const char *ret = ct;

  ENTER();

  if(ct == NULL)
    ret = tr(MSG_CTunknown);
  else
  {
    struct MinNode *curNode;

    // first we search through the users' own MIME type list
    for(curNode = C->mimeTypeList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
    {
      struct MimeTypeNode *mt = (struct MimeTypeNode *)curNode;
      char *type;

      // find the type right after the '/' delimiter
      if((type = strchr(mt->ContentType, '/')) != NULL)
        type++;
      else
        type = (char *)"";

      // don't allow the catch-all and empty types
      if(type[0] != '*' && type[0] != '?' && type[0] != '#' && type[0] != '\0')
      {
        if(stricmp(ct, mt->ContentType) == 0 && mt->Description[0] != '\0')
        {
          ret = mt->Description;
          break;
        }
      }

    }

    // if we still haven't identified the description
    // we go and search through the internal list
    if(ret == ct)
    {
      unsigned int i;

      for(i=0; IntMimeTypeArray[i].ContentType != NULL; i++)
      {
        if(stricmp(ct, IntMimeTypeArray[i].ContentType) == 0)
        {
          ret = tr(IntMimeTypeArray[i].Description);
          break;
        }
      }
    }
  }

  RETURN(ret);
  return ret;
}
///
/// GetDateStamp
//  Get number of seconds since 1/1-1978
time_t GetDateStamp(void)
{
  struct DateStamp ds;
  time_t seconds;

  ENTER();

  // get the actual time
  DateStamp(&ds);
  seconds = ds.ds_Days * 24 * 60 * 60 +
            ds.ds_Minute * 60 +
            ds.ds_Tick / TICKS_PER_SECOND;

  RETURN(seconds);
  return seconds;
}
///
/// DateStampUTC
//  gets the current system time in UTC
void DateStampUTC(struct DateStamp *ds)
{
  ENTER();

  DateStamp(ds);
  DateStampTZConvert(ds, TZC_UTC);

  LEAVE();
}
///
/// GetSysTimeUTC
//  gets the actual system time in UTC
void GetSysTimeUTC(struct TimeVal *tv)
{
  ENTER();

  GetSysTime(TIMEVAL(tv));
  TimeValTZConvert(tv, TZC_UTC);

  LEAVE();
}
///
/// TimeValTZConvert
//  converts a supplied timeval depending on the TZConvert flag to be converted
//  to/from UTC
void TimeValTZConvert(struct TimeVal *tv, enum TZConvert tzc)
{
  ENTER();

  if(tzc == TZC_LOCAL)
    tv->Seconds += (C->TimeZone + C->DaylightSaving * 60) * 60;
  else if(tzc == TZC_UTC)
    tv->Seconds -= (C->TimeZone + C->DaylightSaving * 60) * 60;

  LEAVE();
}
///
/// DateStampTZConvert
//  converts a supplied DateStamp depending on the TZConvert flag to be converted
//  to/from UTC
void DateStampTZConvert(struct DateStamp *ds, enum TZConvert tzc)
{
  ENTER();

  // convert the DateStamp from local -> UTC or visa-versa
  if(tzc == TZC_LOCAL)
    ds->ds_Minute += (C->TimeZone + C->DaylightSaving * 60);
  else if(tzc == TZC_UTC)
    ds->ds_Minute -= (C->TimeZone + C->DaylightSaving * 60);

  // we need to check the datestamp variable that it is still in it's borders
  // after the UTC correction
  while(ds->ds_Minute < 0)
  {
    ds->ds_Minute += 1440;
    ds->ds_Days--;
  }
  while(ds->ds_Minute >= 1440)
  {
    ds->ds_Minute -= 1440;
    ds->ds_Days++;
  }

  LEAVE();
}
///
/// TimeVal2DateStamp
//  converts a struct TimeVal to a struct DateStamp
void TimeVal2DateStamp(const struct TimeVal *tv, struct DateStamp *ds, enum TZConvert tzc)
{
  LONG seconds;

  ENTER();

  seconds = tv->Seconds + (tv->Microseconds / 1000000);

  ds->ds_Days   = seconds / 86400;       // calculate the days since 1.1.1978
  ds->ds_Minute = (seconds % 86400) / 60;
  ds->ds_Tick   = (tv->Seconds % 60) * TICKS_PER_SECOND + (tv->Microseconds / 20000);

  // if we want to convert from/to UTC we need to do this now
  if(tzc != TZC_NONE)
    DateStampTZConvert(ds, tzc);

  LEAVE();
}
///
/// DateStamp2TimeVal
//  converts a struct DateStamp to a struct TimeVal
void DateStamp2TimeVal(const struct DateStamp *ds, struct TimeVal *tv, enum TZConvert tzc)
{
  ENTER();

  // check if the ptrs are set or not.
  if(ds != NULL && tv != NULL)
  {
    // creates wrong timevals from DateStamps with year >= 2114 ...
    tv->Seconds = (ds->ds_Days * 24 * 60 + ds->ds_Minute) * 60 + ds->ds_Tick / TICKS_PER_SECOND;
    tv->Microseconds = (ds->ds_Tick % TICKS_PER_SECOND) * 1000000 / TICKS_PER_SECOND;

    // if we want to convert from/to UTC we need to do this now
    if(tzc != TZC_NONE)
      TimeValTZConvert(tv, tzc);
  }

  LEAVE();
}
///
/// TimeVal2String
//  Converts a timeval structure to a string with using DateStamp2String after a convert
BOOL TimeVal2String(char *dst, int dstlen, const struct TimeVal *tv, enum DateStampType mode, enum TZConvert tzc)
{
  BOOL result;
  struct DateStamp ds;

  // convert the timeval into a datestamp
  ENTER();

  TimeVal2DateStamp(tv, &ds, TZC_NONE);

  // then call the DateStamp2String() function to get the real string
  result = DateStamp2String(dst, dstlen, &ds, mode, tzc);

  RETURN(result);
  return result;
}
///
/// DateStamp2String
//  Converts a datestamp to a string. The caller have to make sure that the destination has
//  at least 64 characters space.
BOOL DateStamp2String(char *dst, int dstlen, struct DateStamp *date, enum DateStampType mode, enum TZConvert tzc)
{
  char datestr[64], timestr[64], daystr[64]; // we don't use LEN_DATSTRING as OS3.1 anyway ignores it.
  struct DateTime dt;
  struct DateStamp dsnow;

  ENTER();

  // if this argument is not set we get the actual time
  if(!date)
    date = DateStamp(&dsnow);

  // now we fill the DateTime structure with the data for our request.
  dt.dat_Stamp   = *date;
  dt.dat_Format  = (mode == DSS_USDATETIME || mode == DSS_UNIXDATE) ? FORMAT_USA : FORMAT_DEF;
  dt.dat_Flags   = (mode == DSS_RELDATETIME || mode == DSS_RELDATEBEAT) ? DTF_SUBST : 0;
  dt.dat_StrDate = datestr;
  dt.dat_StrTime = timestr;
  dt.dat_StrDay  = daystr;

  // now we check whether we have to convert the datestamp to a specific TZ or not
  if(tzc != TZC_NONE)
    DateStampTZConvert(&dt.dat_Stamp, tzc);

  // lets terminate the strings as OS 3.1 is strange
  datestr[31] = '\0';
  timestr[31] = '\0';
  daystr[31]  = '\0';

  // lets convert the DateStamp now to a string
  if(DateToStr(&dt) == FALSE)
  {
    // clear the dststring as well
    dst[0] = '\0';

    RETURN(FALSE);
    return FALSE;
  }

  switch(mode)
  {
    case DSS_UNIXDATE:
    {
      int y = atoi(&datestr[6]);

      // this is a Y2K patch
      // if less then 8035 days has passed since 1.1.1978 then we are in the 20th century
      if (date->ds_Days < 8035) y += 1900;
      else y += 2000;

      snprintf(dst, dstlen, "%s %s %02d %s %d\n", wdays[dt.dat_Stamp.ds_Days%7], months[atoi(datestr)-1], atoi(&datestr[3]), timestr, y);
    }
    break;

    case DSS_DATETIME:
    case DSS_USDATETIME:
    case DSS_RELDATETIME:
    {
      snprintf(dst, dstlen, "%s %s", datestr, timestr);
    }
    break;

    case DSS_WEEKDAY:
    {
      strlcpy(dst, daystr, dstlen);
    }
    break;

    case DSS_DATE:
    {
      strlcpy(dst, datestr, dstlen);
    }
    break;

    case DSS_TIME:
    {
      strlcpy(dst, timestr, dstlen);
    }
    break;

    case DSS_BEAT:
    case DSS_DATEBEAT:
    case DSS_RELDATEBEAT:
    {
      // calculate the beat time
      LONG beat = (((date->ds_Minute-C->TimeZone+(C->DaylightSaving?0:60)+1440)%1440)*1000)/1440;

      if(mode == DSS_DATEBEAT || mode == DSS_RELDATEBEAT)
        snprintf(dst, dstlen, "%s @%03ld", datestr, beat);
      else
        snprintf(dst, dstlen, "@%03ld", beat);
    }
    break;
  }

  RETURN(TRUE);
  return TRUE;
}
///
/// DateStamp2RFCString
BOOL DateStamp2RFCString(char *dst, const int dstlen, const struct DateStamp *date, const int timeZone, const BOOL convert)
{
  struct DateStamp datestamp;
  struct ClockData cd;
  time_t seconds;
  int convertedTimeZone = (timeZone/60)*100 + (timeZone%60);

  ENTER();

  // if date == NULL we get the current time/date
  if(date == NULL)
    DateStamp(&datestamp);
  else
    memcpy(&datestamp, date, sizeof(struct DateStamp));

  // if the user wants to convert the datestamp we have to make sure we
  // substract/add the timeZone
  if(convert && timeZone != 0)
  {
    datestamp.ds_Minute += timeZone;

    // we need to check the datestamp variable that it is still in it's borders
    // after adjustment
    while(datestamp.ds_Minute < 0)     { datestamp.ds_Minute += 1440; datestamp.ds_Days--; }
    while(datestamp.ds_Minute >= 1440) { datestamp.ds_Minute -= 1440; datestamp.ds_Days++; }
  }

  // lets form the seconds now for the Amiga2Date function
  seconds = (datestamp.ds_Days*24*60*60 + datestamp.ds_Minute*60 + datestamp.ds_Tick/TICKS_PER_SECOND);

  // use utility's Amiga2Date for calculating the correct date/time
  Amiga2Date(seconds, &cd);

  // use snprintf to format the RFC2822 conforming datetime string.
  snprintf(dst, dstlen, "%s, %02d %s %d %02d:%02d:%02d %+05d", wdays[cd.wday],
                                                               cd.mday,
                                                               months[cd.month-1],
                                                               cd.year,
                                                               cd.hour,
                                                               cd.min,
                                                               cd.sec,
                                                               convertedTimeZone);

  RETURN(TRUE);
  return TRUE;
}
///
/// DateStamp2Long
// Converts a datestamp to a pseudo numeric value
long DateStamp2Long(struct DateStamp *date)
{
  char *s;
  char datestr[64]; // we don't use LEN_DATSTRING as OS3.1 anyway ignores it.
  struct DateStamp dsnow;
  struct DateTime dt;
  int y;
  long res = 0;

  ENTER();

  if(!date)
    date = DateStamp(&dsnow);

  memset(&dt, 0, sizeof(struct DateTime));
  dt.dat_Stamp   = *date;
  dt.dat_Format  = FORMAT_USA;
  dt.dat_StrDate = datestr;

  if(DateToStr(&dt))
  {
    s = Trim(datestr);

    // get the year
    y = atoi(&s[6]);

    // this is a Y2K patch
    // if less then 8035 days has passed since 1.1.1978 then we are in the 20th century
    if(date->ds_Days < 8035) y += 1900;
    else y += 2000;

    res = (100*atoi(&s[3])+atoi(s))*10000+y;
  }

  RETURN(res);
  return res;
}
///
/// String2DateStamp
//  Tries to converts a string into a datestamp via StrToDate()
BOOL String2DateStamp(struct DateStamp *dst, char *string, enum DateStampType mode, enum TZConvert tzc)
{
  char datestr[64], timestr[64]; // we don't use LEN_DATSTRING as OS3.1 anyway ignores it.
  BOOL result = FALSE;

  ENTER();

  // depending on the DateStampType we have to try to split the string differently
  // into the separate datestr/timestr combo
  switch(mode)
  {
    case DSS_UNIXDATE:
    {
      char *p;

      // we walk from the front to the back and skip the week
      // day name
      if((p = strchr(string, ' ')))
      {
        int month;

        // extract the month
        for(month=0; month < 12; month++)
        {
          if(strnicmp(string, months[month], 3) == 0)
            break;
        }

        if(month >= 12)
          break;

        // extract the day
        if((p = strchr(p, ' ')))
        {
          int day = atoi(p+1);

          if(day < 1 || day > 31)
            break;

          // extract the timestring
          if((p = strchr(p, ' ')))
          {
            strlcpy(timestr, p+1, MIN((ULONG)8, sizeof(timestr)));

            // extract the year
            if((p = strchr(p, ' ')))
            {
              int year = atoi(p+1);

              if(year < 1970 || year > 2070)
                break;

              // now we can compose our datestr
              snprintf(datestr, sizeof(datestr), "%02d-%02d-%02d", month+1, day, year%100);

              result = TRUE;
            }
          }
        }
      }
    }
    break;

    case DSS_DATETIME:
    case DSS_USDATETIME:
    case DSS_RELDATETIME:
    {
      char *p;

      // copy the datestring
      if((p = strchr(string, ' ')))
      {
        strlcpy(datestr, string, MIN(sizeof(datestr), (unsigned int)(p - string + 1)));
        strlcpy(timestr, p + 1, sizeof(timestr));

        result = TRUE;
      }
    }
    break;

    case DSS_WEEKDAY:
    case DSS_DATE:
    {
      strlcpy(datestr, string, sizeof(datestr));
      timestr[0] = '\0';
      result = TRUE;
    }
    break;

    case DSS_TIME:
    {
      strlcpy(timestr, string, sizeof(timestr));
      datestr[0] = '\0';
      result = TRUE;
    }
    break;

    case DSS_BEAT:
    case DSS_DATEBEAT:
    case DSS_RELDATEBEAT:
      // not supported yet.
    break;
  }

  // we continue only if everything until now is fine.
  if(result == TRUE)
  {
    struct DateTime dt;

    // now we fill the DateTime structure with the data for our request.
    dt.dat_Format  = (mode == DSS_USDATETIME || mode == DSS_UNIXDATE) ? FORMAT_USA : FORMAT_DEF;
    dt.dat_Flags   = 0; // perhaps later we can add Weekday substitution
    dt.dat_StrDate = datestr;
    dt.dat_StrTime = timestr;
    dt.dat_StrDay  = NULL;

    // convert the string to a dateStamp
    if(StrToDate(&dt))
    {
      // now we check whether we have to convert the datestamp to a specific TZ or not
      if(tzc != TZC_NONE)
        DateStampTZConvert(&dt.dat_Stamp, tzc);

      // now we do copy the datestamp stuff over the one from our mail
      memcpy(dst, &dt.dat_Stamp, sizeof(struct DateStamp));
    }
    else
      result = FALSE;
  }

  if(result == FALSE)
    W(DBF_UTIL, "couldn't convert string '%s' to struct DateStamp", string);

  RETURN(result);
  return result;
}

///
/// String2TimeVal
// converts a string to a struct TimeVal, if possible.
BOOL String2TimeVal(struct TimeVal *dst, char *string, enum DateStampType mode, enum TZConvert tzc)
{
  struct DateStamp ds;
  BOOL result;

  ENTER();

  // we use the String2DateStamp function for conversion
  if((result = String2DateStamp(&ds, string, mode, tzc)))
  {
    // now we just have to convert the DateStamp to a struct TimeVal
    DateStamp2TimeVal(&ds, dst, TZC_NONE);
  }

  RETURN(result);
  return result;
}

///
/// TZtoMinutes
//  Converts time zone into a numeric offset also using timezone abbreviations
//  Refer to http://www.cise.ufl.edu/~sbeck/DateManip.html#TIMEZONES
int TZtoMinutes(char *tzone)
{
  /*
    The following timezone names are currently understood (and can be used in parsing dates).
    These are zones defined in RFC 822.
      Universal:  GMT, UT
      US zones :  EST, EDT, CST, CDT, MST, MDT, PST, PDT
      Military :  A to Z (except J)
      Other    :  +HHMM or -HHMM
      ISO 8601 :  +HH:MM, +HH, -HH:MM, -HH

      In addition, the following timezone abbreviations are also accepted. In a few
      cases, the same abbreviation is used for two different timezones (for example,
      NST stands for Newfoundland Standard -0330 and North Sumatra +0630). In these
      cases, only 1 of the two is available. The one preceded by a '#' sign is NOT
      available but is documented here for completeness.
   */

   static const struct
   {
     const char *TZname;
     int   TZcorr;
   } time_zone_table[] =
   {
    { "IDLW",   -1200 }, // International Date Line West
    { "NT",     -1100 }, // Nome
    { "HST",    -1000 }, // Hawaii Standard
    { "CAT",    -1000 }, // Central Alaska
    { "AHST",   -1000 }, // Alaska-Hawaii Standard
    { "AKST",    -900 }, // Alaska Standard
    { "YST",     -900 }, // Yukon Standard
    { "HDT",     -900 }, // Hawaii Daylight
    { "AKDT",    -800 }, // Alaska Daylight
    { "YDT",     -800 }, // Yukon Daylight
    { "PST",     -800 }, // Pacific Standard
    { "PDT",     -700 }, // Pacific Daylight
    { "MST",     -700 }, // Mountain Standard
    { "MDT",     -600 }, // Mountain Daylight
    { "CST",     -600 }, // Central Standard
    { "CDT",     -500 }, // Central Daylight
    { "EST",     -500 }, // Eastern Standard
    { "ACT",     -500 }, // Brazil, Acre
    { "SAT",     -400 }, // Chile
    { "BOT",     -400 }, // Bolivia
    { "EDT",     -400 }, // Eastern Daylight
    { "AST",     -400 }, // Atlantic Standard
    { "AMT",     -400 }, // Brazil, Amazon
    { "ACST",    -400 }, // Brazil, Acre Daylight
//# { "NST",     -330 }, // Newfoundland Standard       nst=North Sumatra    +0630
    { "NFT",     -330 }, // Newfoundland
//# { "GST",     -300 }, // Greenland Standard          gst=Guam Standard    +1000
//# { "BST",     -300 }, // Brazil Standard             bst=British Summer   +0100
    { "BRST",    -300 }, // Brazil Standard
    { "BRT",     -300 }, // Brazil Standard
    { "AMST",    -300 }, // Brazil, Amazon Daylight
    { "ADT",     -300 }, // Atlantic Daylight
    { "ART",     -300 }, // Argentina
    { "NDT",     -230 }, // Newfoundland Daylight
    { "AT",      -200 }, // Azores
    { "BRST",    -200 }, // Brazil Daylight (official time)
    { "FNT",     -200 }, // Brazil, Fernando de Noronha
    { "WAT",     -100 }, // West Africa
    { "FNST",    -100 }, // Brazil, Fernando de Noronha Daylight
    { "GMT",     +000 }, // Greenwich Mean
    { "UT",      +000 }, // Universal (Coordinated)
    { "UTC",     +000 }, // Universal (Coordinated)
    { "WET",     +000 }, // Western European
    { "WEST",    +000 }, // Western European Daylight
    { "CET",     +100 }, // Central European
    { "FWT",     +100 }, // French Winter
    { "MET",     +100 }, // Middle European
    { "MEZ",     +100 }, // Middle European
    { "MEWT",    +100 }, // Middle European Winter
    { "SWT",     +100 }, // Swedish Winter
    { "BST",     +100 }, // British Summer              bst=Brazil standard  -0300
    { "GB",      +100 }, // GMT with daylight savings
    { "CEST",    +200 }, // Central European Summer
    { "EET",     +200 }, // Eastern Europe, USSR Zone 1
    { "FST",     +200 }, // French Summer
    { "MEST",    +200 }, // Middle European Summer
    { "MESZ",    +200 }, // Middle European Summer
    { "METDST",  +200 }, // An alias for MEST used by HP-UX
    { "SAST",    +200 }, // South African Standard
    { "SST",     +200 }, // Swedish Summer              sst=South Sumatra    +0700
    { "EEST",    +300 }, // Eastern Europe Summer
    { "BT",      +300 }, // Baghdad, USSR Zone 2
    { "MSK",     +300 }, // Moscow
    { "EAT",     +300 }, // East Africa
    { "IT",      +330 }, // Iran
    { "ZP4",     +400 }, // USSR Zone 3
    { "MSD",     +300 }, // Moscow Daylight
    { "ZP5",     +500 }, // USSR Zone 4
    { "IST",     +530 }, // Indian Standard
    { "ZP6",     +600 }, // USSR Zone 5
    { "NOVST",   +600 }, // Novosibirsk time zone, Russia
    { "NST",     +630 }, // North Sumatra               nst=Newfoundland Std -0330
//# { "SST",     +700 }, // South Sumatra, USSR Zone 6  sst=Swedish Summer   +0200
    { "JAVT",    +700 }, // Java
    { "CCT",     +800 }, // China Coast, USSR Zone 7
    { "AWST",    +800 }, // Australian Western Standard
    { "WST",     +800 }, // West Australian Standard
    { "PHT",     +800 }, // Asia Manila
    { "JST",     +900 }, // Japan Standard, USSR Zone 8
    { "ROK",     +900 }, // Republic of Korea
    { "ACST",    +930 }, // Australian Central Standard
    { "CAST",    +930 }, // Central Australian Standard
    { "AEST",   +1000 }, // Australian Eastern Standard
    { "EAST",   +1000 }, // Eastern Australian Standard
    { "GST",    +1000 }, // Guam Standard, USSR Zone 9  gst=Greenland Std    -0300
    { "ACDT",   +1030 }, // Australian Central Daylight
    { "CADT",   +1030 }, // Central Australian Daylight
    { "AEDT",   +1100 }, // Australian Eastern Daylight
    { "EADT",   +1100 }, // Eastern Australian Daylight
    { "IDLE",   +1200 }, // International Date Line East
    { "NZST",   +1200 }, // New Zealand Standard
    { "NZT",    +1200 }, // New Zealand
    { "NZDT",   +1300 }, // New Zealand Daylight
    { NULL,         0 }  // Others can be added in the future upon request.
   };

   // Military time zone table
   static const struct
   {
      char tzcode;
      int  tzoffset;
   } military_table[] =
   {
    { 'A',  -100 },
    { 'B',  -200 },
    { 'C',  -300 },
    { 'D',  -400 },
    { 'E',  -500 },
    { 'F',  -600 },
    { 'G',  -700 },
    { 'H',  -800 },
    { 'I',  -900 },
    { 'K', -1000 },
    { 'L', -1100 },
    { 'M', -1200 },
    { 'N',  +100 },
    { 'O',  +200 },
    { 'P',  +300 },
    { 'Q',  +400 },
    { 'R',  +500 },
    { 'S',  +600 },
    { 'T',  +700 },
    { 'U',  +800 },
    { 'V',  +900 },
    { 'W', +1000 },
    { 'X', +1100 },
    { 'Y', +1200 },
    { 'Z', +0000 },
    { 0,       0 }
   };

   int tzcorr = -1;

   /*
    * first we check if the timezone string conforms to one of the
    * following standards (RFC 822)
    *
    * 1.Other    :  +HHMM or -HHMM
    * 2.ISO 8601 :  +HH:MM, +HH, -HH:MM, -HH
    * 3.Military :  A to Z (except J)
    *
    * only if none of the 3 above formats match, we take our hughe TZtable
    * and search for the timezone abbreviation
    */

   // check if the timezone definition starts with a + or -
   if(tzone[0] == '+' || tzone[0] == '-')
   {
      tzcorr = atoi(&tzone[1]);

      // check if tzcorr is correct of if it is perhaps a ISO 8601 format
      if(tzcorr != 0 && tzcorr/100 == 0)
      {
        char *c;

        // multiply it by 100 so that we have now a correct format
        tzcorr *= 100;

        // then check if we have a : to seperate HH:MM and add the minutes
        // to tzcorr
        if((c = strchr(tzone, ':'))) tzcorr += atoi(c);
      }

      // now we have to distingush between + and -
      if(tzone[0] == '-') tzcorr = -tzcorr;
   }
   else if(isalpha(tzone[0]))
   {
      int i;

      // if we end up here then the timezone string is
      // probably a abbreviation and we first check if it is a military abbr
      if(isalpha(tzone[1]) == 0) // military need to be 1 char long
      {
        for(i=0; military_table[i].tzcode; i++)
        {
          if(toupper(tzone[0]) == military_table[i].tzcode)
          {
            tzcorr = military_table[i].tzoffset;
            break;
          }
        }
      }
      else
      {
        for(i=0; time_zone_table[i].TZname; i++) // and as a last chance we scan our abbrev table
        {
          if(strnicmp(time_zone_table[i].TZname, tzone, strlen(time_zone_table[i].TZname)) == 0)
          {
            tzcorr = time_zone_table[i].TZcorr;
            D(DBF_UTIL, "TZtoMinutes: found abbreviation '%s' (%ld)", time_zone_table[i].TZname, tzcorr);
            break;
          }
        }

        if(tzcorr == -1)
          W(DBF_UTIL, "TZtoMinutes: abbreviation '%s' NOT found!", tzone);
      }
   }

   if(tzcorr == -1)
     W(DBF_UTIL, "couldn't parse timezone from '%s'", tzone);

   return tzcorr == -1 ? 0 : (tzcorr/100)*60 + (tzcorr%100);
}
///
/// FormatSize
//  Displays large numbers using group separators
void FormatSize(LONG size, char *buf, int buflen, enum SizeFormat forcedPrecision)
{
  const char *dp;
  double dsize;

  ENTER();

  dp = G->Locale ? (const char *)G->Locale->loc_DecimalPoint : ".";
  dsize = (double)size;

  // see if the user wants to force a precision output or if he simply
  // wants to output based on C->SizeFormat (forcedPrecision = SF_AUTO)
  if(forcedPrecision == SF_AUTO)
    forcedPrecision = C->SizeFormat;

  // we check what SizeFormat the user has choosen
  switch(forcedPrecision)
  {
    // the precision modes use sizes as base of 2
    enum { KB = 1024, MB = 1024 * 1024, GB = 1024 * 1024 * 1024 };

    /*
    ** ONE Precision mode
    ** This will result in the following output:
    ** 1.2 GB - 12.3 MB - 123.4 KB - 1234 B
    */
    case SF_1PREC:
    {
      if(size < KB)       snprintf(buf, buflen, "%ld B", size);
      else if(size < MB)  snprintf(buf, buflen, "%.1f KB", dsize/KB);
      else if(size < GB)  snprintf(buf, buflen, "%.1f MB", dsize/MB);
      else                snprintf(buf, buflen, "%.1f GB", dsize/GB);

      if((buf = strchr(buf, '.'))) *buf = *dp;
    }
    break;

    /*
    ** TWO Precision mode
    ** This will result in the following output:
    ** 1.23 GB - 12.34 MB - 123.45 KB - 1234 B
    */
    case SF_2PREC:
    {
      if(size < KB)       snprintf(buf, buflen, "%ld B", size);
      else if(size < MB)  snprintf(buf, buflen, "%.2f KB", dsize/KB);
      else if(size < GB)  snprintf(buf, buflen, "%.2f MB", dsize/MB);
      else                snprintf(buf, buflen, "%.2f GB", dsize/GB);

      if((buf = strchr(buf, '.'))) *buf = *dp;
    }
    break;

    /*
    ** THREE Precision mode
    ** This will result in the following output:
    ** 1.234 GB - 12.345 MB - 123.456 KB - 1234 B
    */
    case SF_3PREC:
    {
      if(size < KB)       snprintf(buf, buflen, "%ld B", size);
      else if(size < MB)  snprintf(buf, buflen, "%.3f KB", dsize/KB);
      else if(size < GB)  snprintf(buf, buflen, "%.3f MB", dsize/MB);
      else                snprintf(buf, buflen, "%.3f GB", dsize/GB);

      if((buf = strchr(buf, '.'))) *buf = *dp;
    }
    break;

    /*
    ** MIXED Precision mode
    ** This will result in the following output:
    ** 1.234 GB - 12.34 MB - 123.4 KB - 1234 B
    */
    case SF_MIXED:
    {
      if(size < KB)       snprintf(buf, buflen, "%ld B", size);
      else if(size < MB)  snprintf(buf, buflen, "%.1f KB", dsize/KB);
      else if(size < GB)  snprintf(buf, buflen, "%.2f MB", dsize/MB);
      else                snprintf(buf, buflen, "%.3f GB", dsize/GB);

      if((buf = strchr(buf, '.'))) *buf = *dp;
    }
    break;

    /*
    ** STANDARD mode
    ** This will result in the following output:
    ** 1,234,567 (bytes)
    */
    case SF_AUTO:
    default:
    {
      const char *gs = G->Locale ? (const char *)G->Locale->loc_GroupSeparator : ",";

      // as we just split the size to another value, we redefine the KB/MB/GB values to base 10 variables
      enum { KB = 1000, MB = 1000 * 1000, GB = 1000 * 1000 * 1000 };

      if(size < KB)      snprintf(buf, buflen, "%ld", size);
      else if(size < MB) snprintf(buf, buflen, "%ld%s%03ld", size/KB, gs, size%KB);
      else if(size < GB) snprintf(buf, buflen, "%ld%s%03ld%s%03ld", size/MB, gs, (size%MB)/KB, gs, size%KB);
      else               snprintf(buf, buflen, "%ld%s%03ld%s%03ld%s%03ld", size/GB, gs, (size%GB)/MB, gs, (size%MB)/KB, gs, size%KB);
    }
    break;
  }

  LEAVE();
}
///
/// MailExists
//  Checks if a message still exists
BOOL MailExists(struct Mail *mailptr, struct Folder *folder)
{
  BOOL exists;

  ENTER();

  if(isVirtualMail(mailptr))
  {
    exists = TRUE;
  }
  else
  {
    if(folder == NULL)
      folder = mailptr->Folder;

    LockMailListShared(folder->messages);

    exists = (FindMailInList(folder->messages, mailptr) != NULL);

    UnlockMailList(folder->messages);
  }

  RETURN(exists);
  return exists;
}
///
/// DisplayMailList
//  Lists folder contents in the message listview
void DisplayMailList(struct Folder *fo, Object *lv)
{
  struct Mail **array;
  int lastActive;

  ENTER();

  lastActive = fo->LastActive;

  BusyText(tr(MSG_BusyDisplayingList), "");

  // we convert the mail list of the folder
  // to a temporary array because that allows us
  // to quickly populate the NList object.
  if((array = MailListToMailArray(fo->messages)) != NULL)
  {
    // We do not encapsulate this Clear&Insert with a NList_Quiet because
    // this will speed up the Insert with about 3-4 seconds for ~6000 items
    DoMethod(lv, MUIM_NList_Clear);
    DoMethod(lv, MUIM_NList_Insert, array, fo->Total, MUIV_NList_Insert_Sorted,
                 C->AutoColumnResize ? MUIF_NONE : MUIV_NList_Insert_Flag_Raw);

    free(array);
  }

  BusyEnd();

  // Now we have to recover the LastActive or otherwise it will be -1 later
  fo->LastActive = lastActive;

  LEAVE();
}
///
/// AddMailToList
//  Adds a message to a folder
struct Mail *AddMailToList(struct Mail *mail, struct Folder *folder)
{
  struct Mail *new;

  ENTER();

  if((new = memdup(mail, sizeof(struct Mail))) != NULL)
  {
    new->Folder = folder;

    // lets add the new Message to our message list
    LockMailList(folder->messages);
    AddNewMailNode(folder->messages, new);
    UnlockMailList(folder->messages);

    // lets summarize the stats
    folder->Total++;
    folder->Size += mail->Size;

    if(hasStatusNew(mail))
      folder->New++;

    if(!hasStatusRead(mail))
      folder->Unread++;

    MA_ExpireIndex(folder);
  }

  RETURN(new);
  return new;
}

///
/// RemoveMailFromList
//  Removes a message from a folder
void RemoveMailFromList(struct Mail *mail, BOOL closeWindows)
{
  struct Folder *folder = mail->Folder;
  struct MailNode *mnode;

  ENTER();

  // now we remove the mail from main mail
  // listviews in case the folder of it is the
  // currently active one.
  if(folder == FO_GetCurrentFolder())
    DoMethod(G->MA->GUI.PG_MAILLIST, MUIM_MainMailListGroup_RemoveMail, mail);

  // remove the mail from the search window's mail list as well, if the
  // search window exists at all
  if(G->FI != NULL)
    DoMethod(G->FI->GUI.LV_MAILS, MUIM_MainMailList_RemoveMail, mail);

  // lets decrease the folder statistics first
  folder->Total--;
  folder->Size -= mail->Size;

  if(hasStatusNew(mail))
    folder->New--;

  if(!hasStatusRead(mail))
    folder->Unread--;

  LockMailList(folder->messages);

  if((mnode = FindMailInList(folder->messages, mail)) != NULL)
  {
    // remove the mail from the folder's mail list
    D(DBF_UTIL, "removing mail with subject '%s' from folder '%s'", mail->Subject, folder->Name);
    RemoveMailNode(folder->messages, mnode);
    DeleteMailNode(mnode);
  }

  UnlockMailList(folder->messages);

  // then we have to mark the folder index as expired so
  // that it will be saved next time.
  MA_ExpireIndex(folder);

  // Now we check if there is any read window with that very same
  // mail currently open and if so we have to close it.
  if(IsListEmpty((struct List *)&G->readMailDataList) == FALSE)
  {
    // search through our ReadDataList
    struct MinNode *curNode;

    for(curNode = G->readMailDataList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
    {
      struct ReadMailData *rmData = (struct ReadMailData *)curNode;

      if(rmData->mail == mail)
      {
        if(closeWindows == TRUE && rmData->readWindow != NULL)
        {
          // Just ask the window to close itself, this will effectively clear the pointer.
          // We cannot set the attribute directly, because a DoMethod() call is synchronous
          // and then the read window would modify the list we are currently walking through
          // by calling CleanupReadMailData(). Hence we just let the application do the dirty
          // work as soon as it has the possibility to do that, but not before this loop is
          // finished. This works, because the ReadWindow class catches any modification to
          // MUIA_Window_CloseRequest itself. A simple set(win, MUIA_Window_Open, FALSE) would
          // visibly close the window, but it would not invoke the associated hook which gets
          // invoked when you close the window by clicking on the close gadget.
          DoMethod(G->App, MUIM_Application_PushMethod, rmData->readWindow, 3, MUIM_Set, MUIA_Window_CloseRequest, TRUE);
        }
        else
        {
          // Just clear pointer to this mail if we don't want to close the window or if
          // there is no window to close at all.
          rmData->mail = NULL;
        }
      }
    }
  }

  // and last, but not least, we have to free the mail
  free(mail);

  LEAVE();
}

///
/// ClearMailList
//  Removes all messages from a folder
void ClearMailList(struct Folder *folder, BOOL resetstats)
{
  ENTER();

  ASSERT(folder != NULL);
  ASSERT(folder->messages != NULL);
  ASSERT(folder->messages->lockSemaphore != NULL);
  D(DBF_FOLDER, "clearing mail list of folder '%s'", folder->Name);

  LockMailList(folder->messages);

  if(IsMailListEmpty(folder->messages) == FALSE)
  {
    struct MailNode *mnode;

    while((mnode = (struct MailNode *)RemHead((struct List *)&folder->messages->list)) != NULL)
    {
      struct Mail *mail = mnode->mail;

      // Now we check if there is any read window with that very same
      // mail currently open and if so we have to clean it.
      if(IsListEmpty((struct List *)&G->readMailDataList) == FALSE)
      {
        // search through our ReadDataList
        struct MinNode *curNode;

        for(curNode = G->readMailDataList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
        {
          struct ReadMailData *rmData = (struct ReadMailData *)curNode;

          if(rmData->mail == mail)
            CleanupReadMailData(rmData, TRUE);
        }
      }

      DeleteMailNode(mnode);
      // free the mail pointer
      free(mail);
    }

    D(DBF_FOLDER, "cleared mail list of folder '%s'", folder->Name);
  }

  // reset the list of mails
  InitMailList(folder->messages);

  UnlockMailList(folder->messages);

  if(resetstats == TRUE)
  {
    folder->Total = 0;
    folder->New = 0;
    folder->Unread = 0;
    folder->Size = 0;
  }

  LEAVE();
}
///
/// GetPackMethod
//  Returns packer type and efficiency
static BOOL GetPackMethod(enum FolderMode fMode, char **method, int *eff)
{
  BOOL result = TRUE;

  ENTER();

  switch(fMode)
  {
    case FM_XPKCOMP:
    {
      *method = C->XPKPack;
      *eff = C->XPKPackEff;
    }
    break;

    case FM_XPKCRYPT:
    {
      *method = C->XPKPackEncrypt;
      *eff = C->XPKPackEncryptEff;
    }
    break;

    default:
    {
      *method = NULL;
      *eff = 0;
      result = FALSE;
    }
    break;
  }

  RETURN(result);
  return result;
}
///
/// CompressMailFile
//  Shrinks a message file
static BOOL CompressMailFile(char *src, char *dst, char *passwd, char *method, int eff)
{
  long error = -1;

  ENTER();

  D(DBF_XPK, "CompressMailFile: %08lx - [%s] -> [%s] - [%s] - [%s] - %ld", XpkBase, src, dst, passwd, method, eff);

  if(XpkBase != NULL)
  {
    error = XpkPackTags(XPK_InName,      src,
                        XPK_OutName,     dst,
                        XPK_Password,    passwd,
                        XPK_PackMethod,  method,
                        XPK_PackMode,    eff,
                        TAG_DONE);

    #if defined(DEBUG)
    if(error != XPKERR_OK)
    {
      char buf[1024];

      XpkFault(error, NULL, buf, sizeof(buf));

      E(DBF_XPK, "XpkPackTags() returned an error %ld: '%s'", error, buf);
    }
    #endif
  }

  RETURN((BOOL)(error == XPKERR_OK));
  return (BOOL)(error == XPKERR_OK);
}
///
/// UncompressMailFile
//  Expands a compressed message file
static BOOL UncompressMailFile(const char *src, const char *dst, const char *passwd)
{
  long error = -1;

  ENTER();

  D(DBF_XPK, "UncompressMailFile: %08lx - [%s] -> [%s] - [%s]", XpkBase, src, dst, passwd);

  if(XpkBase != NULL)
  {
    error = XpkUnpackTags(XPK_InName,    src,
                          XPK_OutName,   dst,
                          XPK_Password,  passwd,
                          TAG_DONE);

    #if defined(DEBUG)
    if(error != XPKERR_OK)
    {
      char buf[1024];

      XpkFault(error, NULL, buf, sizeof(buf));

      E(DBF_XPK, "XpkUnPackTags() returned an error %ld: '%s'", error, buf);
    }
    #endif
  }

  RETURN((BOOL)(error == XPKERR_OK));
  return (BOOL)(error == XPKERR_OK);
}
///
/// TransferMailFile
//  Copies or moves a message file, handles compression
int TransferMailFile(BOOL copyit, struct Mail *mail, struct Folder *dstfolder)
{
  struct Folder *srcfolder = mail->Folder;
  enum FolderMode srcMode = srcfolder->Mode;
  enum FolderMode dstMode = dstfolder->Mode;
  int success = -1;

  ENTER();

  D(DBF_UTIL, "TransferMailFile: %s '%s' to '%s' %ld->%ld", copyit ? "copy" : "move", mail->MailFile, GetFolderDir(dstfolder), srcMode, dstMode);

  if(MA_GetIndex(srcfolder) == TRUE && MA_GetIndex(dstfolder) == TRUE)
  {
    char *pmeth;
    int peff = 0;
    char srcbuf[SIZE_PATHFILE];
    char dstbuf[SIZE_PATHFILE];
    char dstFileName[SIZE_MFILE];
    char *srcpw = srcfolder->Password;
    char *dstpw = dstfolder->Password;
    BOOL counterExceeded = FALSE;

    // get some information we require
    GetPackMethod(dstMode, &pmeth, &peff);
    GetMailFile(srcbuf, srcfolder, mail);

    // check if we can just take the exactly same filename in the destination
    // folder or if we require to increase the mailfile counter to make it
    // unique
    strlcpy(dstFileName, mail->MailFile, sizeof(dstFileName));

    AddPath(dstbuf, GetFolderDir(dstfolder), dstFileName, sizeof(dstbuf));
    if(FileExists(dstbuf) == TRUE)
    {
      int mCounter = atoi(&dstFileName[13]);

      do
      {
        if(mCounter < 1 || mCounter >= 999)
          // no more numbers left
          // now we have to leave this function
          counterExceeded = TRUE;
        else
        {
          mCounter++;

          snprintf(&dstFileName[13], sizeof(dstFileName)-13, "%03d", mCounter);
          dstFileName[16] = ','; // restore it

          AddPath(dstbuf, GetFolderDir(dstfolder), dstFileName, sizeof(dstbuf));
        }
      }
      while(counterExceeded == FALSE && FileExists(dstbuf) == TRUE);

      if(counterExceeded == FALSE)
      {
        // if we end up here we finally found a new mailfilename which we can use, so
        // lets copy it to our MailFile variable
        D(DBF_UTIL, "renaming mail file from '%s' to '%s'", mail->MailFile, dstFileName);
        strlcpy(mail->MailFile, dstFileName, sizeof(mail->MailFile));
      }
    }

    if(counterExceeded == FALSE)
    {
      // now that we have the source and destination filename
      // we can go and do the file operation depending on some data we
      // acquired earlier
      if((srcMode == dstMode && srcMode <= FM_SIMPLE) ||
         (srcMode <= FM_SIMPLE && dstMode <= FM_SIMPLE))
      {
        if(copyit == TRUE)
          success = CopyFile(dstbuf, 0, srcbuf, 0) ? 1 : -1;
        else
          success = MoveFile(srcbuf, dstbuf) ? 1 : -1;
      }
      else if(isXPKFolder(srcfolder))
      {
        if(isXPKFolder(dstfolder) == FALSE)
        {
          // if we end up here the source folder is a compressed folder but the
          // destination one not. so lets uncompress it
          success = UncompressMailFile(srcbuf, dstbuf, srcpw) ? 1 : -2;
          if(success > 0 && copyit == FALSE)
            success = (DeleteFile(srcbuf) != 0) ? 1 : -1;
        }
        else
        {
          // here the source folder is a compressed+crypted folder and the
          // destination one also, so we have to uncompress the file to a
          // temporarly file and compress it immediatly with the destination
          // password again.
          struct TempFile *tf;

          if((tf = OpenTempFile(NULL)) != NULL)
          {
            success = UncompressMailFile(srcbuf, tf->Filename, srcpw) ? 1 : -2;
            if(success > 0)
            {
              // compress it immediatly again
              success = CompressMailFile(tf->Filename, dstbuf, dstpw, pmeth, peff) ? 1 : -2;
              if(success > 0 && copyit == FALSE)
                success = (DeleteFile(srcbuf) != 0) ? 1 : -1;
            }

            CloseTempFile(tf);
          }
        }
      }
      else
      {
        if(isXPKFolder(dstfolder))
        {
          // here the source folder is not compressed, but the destination one
          // so we compress the file in the destionation folder now
          success = CompressMailFile(srcbuf, dstbuf, dstpw, pmeth, peff) ? 1 : -2;
          if(success > 0 && copyit == FALSE)
            success = (DeleteFile(srcbuf) != 0) ? 1 : -1;
        }
        else
          // if we end up here then there is something seriously wrong
          success = -3;
      }
    }
  }

  RETURN(success);
  return success;
}
///
/// RepackMailFile
//  (Re/Un)Compresses a message file
//  Note: If dstMode is -1 and passwd is NULL, then this function packs
//        the current mail. It will assume it is plaintext and needs to be packed now
BOOL RepackMailFile(struct Mail *mail, enum FolderMode dstMode, char *passwd)
{
  char *pmeth = NULL;
  char srcbuf[SIZE_PATHFILE];
  char dstbuf[SIZE_PATHFILE];
  struct Folder *folder;
  int peff = 0;
  enum FolderMode srcMode;
  BOOL success = FALSE;

  ENTER();

  folder = mail->Folder;
  srcMode = folder->Mode;

  // if this function was called with dstxpk=-1 and passwd=NULL then
  // we assume we need to pack the file from plain text to the currently
  // selected pack method of the folder
  if((LONG)dstMode == -1 && passwd == NULL)
  {
    srcMode = FM_NORMAL;
    dstMode = folder->Mode;
    passwd  = folder->Password;
  }

  MA_GetIndex(folder);
  GetMailFile(srcbuf, folder, mail);
  GetPackMethod(dstMode, &pmeth, &peff);
  snprintf(dstbuf, sizeof(dstbuf), "%s.tmp", srcbuf);

  SHOWSTRING(DBF_UTIL, srcbuf);

  if((srcMode == dstMode && srcMode <= FM_SIMPLE) ||
     (srcMode <= FM_SIMPLE && dstMode <= FM_SIMPLE))
  {
    // the FolderModes are the same so lets do nothing
    success = TRUE;

    D(DBF_UTIL, "repack not required.");
  }
  else if(srcMode > FM_SIMPLE)
  {
    if(dstMode <= FM_SIMPLE)
    {
      D(DBF_UTIL, "uncompressing");

      // if we end up here the source folder is a compressed folder so we
      // have to just uncompress the file
      if(UncompressMailFile(srcbuf, dstbuf, folder->Password) &&
         DeleteFile(srcbuf) != 0)
      {
        if(RenameFile(dstbuf, srcbuf) != 0)
          success = TRUE;
      }
    }
    else
    {
      // if we end up here, the source folder is a compressed+crypted one and
      // the destination mode also
      D(DBF_UTIL, "uncompressing/recompress");

      if(UncompressMailFile(srcbuf, dstbuf, folder->Password) &&
         CompressMailFile(dstbuf, srcbuf, passwd, pmeth, peff))
      {
        if(DeleteFile(dstbuf) != 0)
          success = TRUE;
      }
    }
  }
  else
  {
    if(dstMode > FM_SIMPLE)
    {
      D(DBF_UTIL, "compressing");

      // here the source folder is not compressed, but the destination mode
      // signals to compress it
      if(CompressMailFile(srcbuf, dstbuf, passwd, pmeth, peff) &&
         DeleteFile(srcbuf) != 0)
      {
        success = RenameFile(dstbuf, srcbuf);
      }
    }
  }

  MA_UpdateMailFile(mail);

  RETURN(success);
  return success;
}
///
/// DoPack
//  Compresses a file
BOOL DoPack(char *file, char *newfile, struct Folder *folder)
{
  char *pmeth = NULL;
  int peff = 0;
  BOOL result = FALSE;

  ENTER();

  if(GetPackMethod(folder->Mode, &pmeth, &peff) == TRUE)
  {
    if(CompressMailFile(file, newfile, folder->Password, pmeth, peff) == TRUE)
    {
      if(DeleteFile(file) != 0)
      {
        result = TRUE;
      }
    }
  }

  RETURN(result);
  return result;
}
///
/// StartUnpack
//  Unpacks a file to a temporary file
char *StartUnpack(const char *file, char *newfile, const struct Folder *folder)
{
  FILE *fh;
  char *result = NULL;

  ENTER();

  if((fh = fopen(file, "r")) != NULL)
  {
    BOOL xpk = FALSE;

    // check if the source file is really XPK compressed or not.
    if(fgetc(fh) == 'X' && fgetc(fh) == 'P' && fgetc(fh) == 'K')
      xpk = TRUE;

    fclose(fh);
    fh = NULL;

    // now we compose a temporary filename and start
    // uncompressing the source file into it.
    if(xpk == TRUE)
    {
      char nfile[SIZE_FILE];

      snprintf(nfile, sizeof(nfile), "YAMu%08lx.unp", GetUniqueID());
      AddPath(newfile, C->TempDir, nfile, SIZE_PATHFILE);

      // check that the destination filename
      // doesn't already exist
      if(FileExists(newfile) == FALSE && UncompressMailFile(file, newfile, folder ? folder->Password : ""))
        result = newfile;
    }
    else
    {
      strcpy(newfile, file);
      result = newfile;
    }
  }

  RETURN(result);
  return result;
}
///
/// FinishUnpack
//  Deletes temporary unpacked file
void FinishUnpack(char *file)
{
  char ext[SIZE_FILE];

  ENTER();

  // we just delete if this is really related to a unpack file
  stcgfe(ext, file);
  if(strcmp(ext, "unp") == 0)
  {
    if(IsListEmpty((struct List *)&G->readMailDataList) == FALSE)
    {
      // search through our ReadDataList
      struct MinNode *curNode;

      for(curNode = G->readMailDataList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
      {
        struct ReadMailData *rmData = (struct ReadMailData *)curNode;

        // check if the file is still in use and if so we quit immediately
        // leaving the file untouched.
        if(stricmp(file, rmData->readFile) == 0)
        {
          LEAVE();
          return;
        }
      }
    }

    if(DeleteFile(file) == 0)
      AddZombieFile(file);
  }

  LEAVE();
}
///

/*** Editor related ***/
/// EditorToFile
//  Saves contents of a texteditor object to a file
BOOL EditorToFile(Object *editor, char *file)
{
  FILE *fh;
  BOOL result = FALSE;

  ENTER();

  if((fh = fopen(file, "w")) != NULL)
  {
    char *text = (char *)DoMethod((Object *)editor, MUIM_TextEditor_ExportText);

    // write out the whole text to the file
    if(fwrite(text, strlen(text), 1, fh) == 1)
      result = TRUE;

    FreeVec(text); // use FreeVec() because TextEditor.mcc uses AllocVec()
    fclose(fh);
  }

  RETURN(result);
  return result;
}
///
/// FileToEditor
//  Loads a file into a texteditor object
BOOL FileToEditor(const char *file, Object *editor,
                  const BOOL changed, const BOOL useStyles, const BOOL useColors)
{
  char *text;
  BOOL res = FALSE;

  ENTER();

  if((text = FileToBuffer(file)) != NULL)
  {
    char *parsedText;

    // parse the text and do some highlighting and stuff
    if((parsedText = ParseEmailText(text, FALSE, useStyles, useColors)) != NULL)
    {
      // set the new text and tell the editor that its content has changed
      xset(editor, MUIA_TextEditor_Contents,   parsedText,
                   MUIA_TextEditor_HasChanged, changed);

      free(parsedText);

      res = TRUE;
    }

    free(text);
  }

  RETURN(res);
  return res;
}
///

/*** Hooks ***/
/// GeneralDesFunc
//  General purpose destruction hook
HOOKPROTONHNO(GeneralDesFunc, long, void *entry)
{
  free(entry);

  return 0;
}
MakeHook(GeneralDesHook, GeneralDesFunc);

///
/// ExamineDirMatchHook
// dos.library 52.12 from the July update doesn't use the supplied match string
// correctly and simply returns all directory entries instead of just the matching
// ones. So we have to do the dirty work ourself. This bug has been fixed
// since dos.library 52.17.
#if defined(__amigaos4__)
HOOKPROTONH(ExamineDirMatchFunc, LONG, STRPTR matchString, struct ExamineData *ed)
{
  LONG accept = TRUE;

  ENTER();

  if(matchString != NULL)
    accept = MatchPatternNoCase(matchString, ed->Name);

  RETURN(accept);
  return accept;
}
MakeHook(ExamineDirMatchHook, ExamineDirMatchFunc);
#endif
///

/*** MUI related ***/
/// SafeOpenWindow
//  Tries to open a window
BOOL SafeOpenWindow(Object *obj)
{
  BOOL success = FALSE;

  ENTER();

  if(obj != NULL)
  {
    // make sure we open the window object
    set(obj, MUIA_Window_Open, TRUE);

    // now we check whether the window was successfully
    // open or the application has been in iconify state
    if(xget(obj, MUIA_Window_Open) == TRUE ||
       xget(_app(obj), MUIA_Application_Iconified) == TRUE)
    {
      success = TRUE;
    }
  }

  if(success == FALSE)
  {
    // otherwise we perform a DisplayBeep()
    DisplayBeep(NULL);
  }

  RETURN(success);
  return success;
}
///
/// DisposeModule
// Free resources of a MUI window
void DisposeModule(void *modptr)
{
  struct UniversalClassData **module = (struct UniversalClassData **)modptr;

  ENTER();

  if(*module != NULL)
  {
    Object *window = (*module)->GUI.WI;

    D(DBF_GUI, "removing window from app: %08lx", window);

    // close the window
    set(window, MUIA_Window_Open, FALSE);

    // remove the window from our app
    DoMethod(G->App, OM_REMMEMBER, window);

    // dispose the window object
    MUI_DisposeObject(window);

    free(*module);
    *module = NULL;
  }

  LEAVE();
}
HOOKPROTONHNO(DisposeModuleFunc, void, void **arg)
{
  DisposeModule(arg[0]);
}
MakeHook(DisposeModuleHook,DisposeModuleFunc);
///
/// LoadLayout
//  Loads column widths from ENV:MUI/YAM.cfg
void LoadLayout(void)
{
  const char *ls;
  char *endptr;

  ENTER();

  // Load the application configuration from the ENV: directory.
  DoMethod(G->App, MUIM_Application_Load, MUIV_Application_Load_ENV);

  // we encode the different weight factors which are embeeded in a dummy string
  // gadgets:
  //
  // 0:  Horizontal weight of left foldertree in main window.
  // 1:  Horizontal weight of right maillistview in main window.
  // 2:  Vertical weight of top headerlistview in read window
  // 3:  Vertical weight of bottom texteditor field in read window
  // 4:  Horizontal weight of listview group in the glossary window
  // 5:  Horizontal weight of text group in the glossary window
  // 6:  Vertical weight of top right maillistview group in main window.
  // 7:  Vertical weight of bottom right embedded read pane object in the main window.
  // 8:  Vertical weight of top object (headerlist) of the embedded read pane
  // 9:  Vertical weight of bottom object (texteditor) of the embedded read pane
  // 10: Vertical weight of top object (headerlist) in a read window
  // 11: Vertical weight of bottom object (texteditor) in a read window

  if((ls = (STRPTR)xget(G->MA->GUI.ST_LAYOUT, MUIA_String_Contents)) == NULL ||
      ls[0] == '\0')
  {
    //    0  1   2  3   4  5   6  7   8 9   10 11
    ls = "30 100 25 100 30 100 25 100 5 100 5 100";

    D(DBF_UTIL, "using default layout weight factors: '%s'", ls);
  }
  else
    D(DBF_UTIL, "loaded layout weight factors: '%s'", ls);

  // lets get the numbers for each weight factor out of the contents
  // of the fake string gadget
  G->Weights[0] = strtol(ls, &endptr, 10);
  if(endptr == NULL || endptr == ls)
    G->Weights[0] = 30;

  ls = endptr;
  G->Weights[1] = strtol(ls, &endptr, 10);
  if(endptr == NULL || endptr == ls)
    G->Weights[1] = 100;

  ls = endptr;
  G->Weights[2] = strtol(ls, &endptr, 10);
  if(endptr == NULL || endptr == ls)
    G->Weights[2] = 25;

  ls = endptr;
  G->Weights[3] = strtol(ls, &endptr, 10);
  if(endptr == NULL || endptr == ls)
    G->Weights[3] = 100;

  ls = endptr;
  G->Weights[4] = strtol(ls, &endptr, 10);
  if(endptr == NULL || endptr == ls)
    G->Weights[4] = 30;

  ls = endptr;
  G->Weights[5] = strtol(ls, &endptr, 10);
  if(endptr == NULL || endptr == ls)
    G->Weights[5] = 100;

  ls = endptr;
  G->Weights[6] = strtol(ls, &endptr, 10);
  if(endptr == NULL || endptr == ls)
    G->Weights[6] = 25;

  ls = endptr;
  G->Weights[7] = strtol(ls, &endptr, 10);
  if(endptr == NULL || endptr == ls)
    G->Weights[7] = 100;

  ls = endptr;
  G->Weights[8] = strtol(ls, &endptr, 10);
  if(endptr == NULL || endptr == ls)
    G->Weights[8] = 5;

  ls = endptr;
  G->Weights[9] = strtol(ls, &endptr, 10);
  if(endptr == NULL || endptr == ls)
    G->Weights[9] = 100;

  ls = endptr;
  G->Weights[10] = strtol(ls, &endptr, 10);
  if(endptr == NULL || endptr == ls)
    G->Weights[10] = 5;

  ls = endptr;
  G->Weights[11] = strtol(ls, &endptr, 10);
  if(endptr == NULL || endptr == ls)
    G->Weights[11] = 100;

  // lets set the weight factors to the corresponding GUI elements now
  // if they exist
  set(G->MA->GUI.LV_FOLDERS,  MUIA_HorizWeight, G->Weights[0]);
  set(G->MA->GUI.GR_MAILVIEW, MUIA_HorizWeight, G->Weights[1]);
  set(G->MA->GUI.PG_MAILLIST, MUIA_VertWeight,  G->Weights[6]);

  // if the embedded read pane is active we set its weight values
  if(C->EmbeddedReadPane)
  {
    xset(G->MA->GUI.MN_EMBEDDEDREADPANE, MUIA_VertWeight,                 G->Weights[7],
                                         MUIA_ReadMailGroup_HGVertWeight, G->Weights[8],
                                         MUIA_ReadMailGroup_TGVertWeight, G->Weights[9]);
  }

  LEAVE();
}
///
/// SaveLayout
//  Saves column widths to ENV(ARC):MUI/YAM.cfg
void SaveLayout(BOOL permanent)
{
  char buf[SIZE_DEFAULT+1];

  ENTER();

  // we encode the different weight factors which are embeeded in a dummy string
  // gadgets:
  //
  // 0:  Horizontal weight of left foldertree in main window.
  // 1:  Horizontal weight of right maillistview in main window.
  // 2:  Vertical weight of top headerlistview in read window
  // 3:  Vertical weight of bottom texteditor field in read window
  // 4:  Horizontal weight of listview group in the glossary window
  // 5:  Horizontal weight of text group in the glossary window
  // 6:  Vertical weight of top right maillistview group in main window.
  // 7:  Vertical weight of bottom right embedded read pane object in the main window.
  // 8:  Vertical weight of top object (headerlist) of the embedded read pane
  // 9:  Vertical weight of bottom object (texteditor) of the embedded read pane
  // 10: Vertical weight of top object (headerlist) in a read window
  // 11: Vertical weight of bottom object (texteditor) in a read window

  snprintf(buf, sizeof(buf), "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld", G->Weights[0],
                                                                                G->Weights[1],
                                                                                G->Weights[2],
                                                                                G->Weights[3],
                                                                                G->Weights[4],
                                                                                G->Weights[5],
                                                                                G->Weights[6],
                                                                                G->Weights[7],
                                                                                G->Weights[8],
                                                                                G->Weights[9],
                                                                                G->Weights[10],
                                                                                G->Weights[11]);

  setstring(G->MA->GUI.ST_LAYOUT, buf);
  DoMethod(G->App, MUIM_Application_Save, MUIV_Application_Save_ENV);

  // if we want to save to ENVARC:
  if(permanent == TRUE)
  {
    APTR oldWindowPtr;

    // this is for the people out there having their SYS: partition locked and whining about
    // YAM popping up a error requester upon the exit - so it's their fault now if
    // the MUI objects aren't saved correctly.
    oldWindowPtr = SetProcWindow((APTR)-1);

    DoMethod(G->App, MUIM_Application_Save, MUIV_Application_Save_ENVARC);

    D(DBF_UTIL, "permanently saved layout weight factors: '%s'", buf);

    // restore the old windowPtr
    SetProcWindow(oldWindowPtr);
  }
  else
    D(DBF_UTIL, "saved layout weight factors: '%s'", buf);

  LEAVE();
}
///
/// ConvertKey
//  Converts input event to key code
unsigned char ConvertKey(const struct IntuiMessage *imsg)
{
  struct InputEvent ie;
  unsigned char code = 0;

  ENTER();

  ie.ie_NextEvent    = NULL;
  ie.ie_Class        = IECLASS_RAWKEY;
  ie.ie_SubClass     = 0;
  ie.ie_Code         = imsg->Code;
  ie.ie_Qualifier    = imsg->Qualifier;
  ie.ie_EventAddress = (APTR *) *((ULONG *)imsg->IAddress);

  if(MapRawKey(&ie, (STRPTR)&code, 1, NULL) != 1)
    E(DBF_GUI, "MapRawKey retuned != 1");

  RETURN(code);
  return code;
}
///

/*** GFX related ***/
#if !defined(__amigaos4__)
/// struct LayerHookMsg
struct LayerHookMsg
{
  struct Layer *layer;
  struct Rectangle bounds;
  LONG offsetx;
  LONG offsety;
};

///
/// struct BltHook
struct BltHook
{
  struct Hook hook;
  struct BitMap maskBitMap;
  struct BitMap *srcBitMap;
  LONG srcx,srcy;
  LONG destx,desty;
};

///
/// MyBltMaskBitMap
static void MyBltMaskBitMap(const struct BitMap *srcBitMap, LONG xSrc, LONG ySrc, struct BitMap *destBitMap, LONG xDest, LONG yDest, LONG xSize, LONG ySize, struct BitMap *maskBitMap)
{
  ENTER();

  BltBitMap(srcBitMap,xSrc,ySrc,destBitMap, xDest, yDest, xSize, ySize, 0x99,~0,NULL);
  BltBitMap(maskBitMap,xSrc,ySrc,destBitMap, xDest, yDest, xSize, ySize, 0xe2,~0,NULL);
  BltBitMap(srcBitMap,xSrc,ySrc,destBitMap, xDest, yDest, xSize, ySize, 0x99,~0,NULL);

  LEAVE();
}

///
/// BltMaskHook
HOOKPROTO(BltMaskFunc, void, struct RastPort *rp, struct LayerHookMsg *msg)
{
  struct BltHook *h = (struct BltHook*)hook;

  LONG width = msg->bounds.MaxX - msg->bounds.MinX+1;
  LONG height = msg->bounds.MaxY - msg->bounds.MinY+1;
  LONG offsetx = h->srcx + msg->offsetx - h->destx;
  LONG offsety = h->srcy + msg->offsety - h->desty;

  MyBltMaskBitMap(h->srcBitMap, offsetx, offsety, rp->BitMap, msg->bounds.MinX, msg->bounds.MinY, width, height, &h->maskBitMap);
}
MakeStaticHook(BltMaskHook, BltMaskFunc);

///
/// MyBltMaskBitMapRastPort
void MyBltMaskBitMapRastPort(struct BitMap *srcBitMap, LONG xSrc, LONG ySrc, struct RastPort *destRP, LONG xDest, LONG yDest, LONG xSize, LONG ySize, ULONG minterm, APTR bltMask)
{
  ENTER();

  if(GetBitMapAttr(srcBitMap, BMA_FLAGS) & BMF_INTERLEAVED)
  {
    LONG src_depth = GetBitMapAttr(srcBitMap, BMA_DEPTH);
    struct Rectangle rect;
    struct BltHook hook;

    // Define the destination rectangle in the rastport
    rect.MinX = xDest;
    rect.MinY = yDest;
    rect.MaxX = xDest + xSize - 1;
    rect.MaxY = yDest + ySize - 1;

    // Initialize the hook
    InitHook(&hook.hook, BltMaskHook, NULL);
    hook.srcBitMap = srcBitMap;
    hook.srcx = xSrc;
    hook.srcy = ySrc;
    hook.destx = xDest;
    hook.desty = yDest;

    // Initialize a bitmap where all plane pointers points to the mask
    InitBitMap(&hook.maskBitMap, src_depth, GetBitMapAttr(srcBitMap, BMA_WIDTH), GetBitMapAttr(srcBitMap, BMA_HEIGHT));
    while(src_depth)
    {
      hook.maskBitMap.Planes[--src_depth] = bltMask;
    }

    // Blit onto the Rastport */
    DoHookClipRects(&hook.hook, destRP, &rect);
  }
  else
    BltMaskBitMapRastPort(srcBitMap, xSrc, ySrc, destRP, xDest, yDest, xSize, ySize, minterm, bltMask);

  LEAVE();
}

///
#endif

/*** Miscellaneous stuff ***/
/// PGPGetPassPhrase
//  Asks user for the PGP passphrase
void PGPGetPassPhrase(void)
{
  char pgppass[SIZE_DEFAULT];

  ENTER();

  // check if a PGPPASS variable exists already
  if(GetVar("PGPPASS", pgppass, sizeof(pgppass), GVF_GLOBAL_ONLY) < 0)
  {
    // check if we really require to request a passphrase from
    // the user
    if(G->PGPPassPhrase[0] != '\0' &&
       C->PGPPassInterval > 0 && G->LastPGPUsage > 0 &&
       time(NULL)-G->LastPGPUsage <= (time_t)(C->PGPPassInterval*60))
    {
      // nothing
    }
    else
    {
      pgppass[0] = '\0';

      if(PassphraseRequest(pgppass, SIZE_DEFAULT, G->MA->GUI.WI) > 0)
        G->LastPGPUsage = time(NULL);
      else
        G->LastPGPUsage = 0;

      strlcpy(G->PGPPassPhrase, pgppass, sizeof(G->PGPPassPhrase));
    }

    // make sure we delete the passphrase variable immediately after
    // having processed the PGP command
    G->PGPPassVolatile = TRUE;

    // set a global PGPPASS variable, but do not write it
    // to ENVARC:
    SetVar("PGPPASS", G->PGPPassPhrase, -1, GVF_GLOBAL_ONLY);
  }
  else
  {
    W(DBF_MAIL, "ENV:PGPPASS already exists!");

    // don't delete env-variable on PGPClearPassPhrase()
    G->PGPPassVolatile = FALSE;

    // copy the content of the env variable to our
    // global passphrase variable
    strlcpy(G->PGPPassPhrase, pgppass, sizeof(G->PGPPassPhrase));
    G->LastPGPUsage = 0;
  }

  LEAVE();
}
///
/// PGPClearPassPhrase
//  Clears the ENV variable containing the PGP passphrase
void PGPClearPassPhrase(BOOL force)
{
  ENTER();

  if(G->PGPPassVolatile)
    DeleteVar("PGPPASS", GVF_GLOBAL_ONLY);

  if(force)
    G->PGPPassPhrase[0] = '\0';

  LEAVE();
}
///
/// PGPCommand
//  Launches a PGP command
int PGPCommand(const char *progname, const char *options, int flags)
{
  BPTR fhi;
  int error = -1;
  char command[SIZE_LARGE];

  ENTER();

  D(DBF_UTIL, "[%s] [%s] - flags: %ld", progname, options, flags);

  AddPath(command, C->PGPCmdPath, progname, sizeof(command));
  strlcat(command, " >" PGPLOGFILE " ", sizeof(command));
  strlcat(command, options, sizeof(command));

  if((fhi = Open("NIL:", MODE_OLDFILE)) != (BPTR)NULL)
  {
    BPTR fho;

    if((fho = Open("NIL:", MODE_NEWFILE)) != (BPTR)NULL)
    {

      BusyText(tr(MSG_BusyPGPrunning), "");

      // use SystemTags() for executing PGP
      error = SystemTags(command, SYS_Input,    fhi,
                                  SYS_Output,   fho,
                                  SYS_Asynch,   FALSE,
                                  #if defined(__amigaos4__)
                                  SYS_Error,    NULL,
                                  NP_Child,     TRUE,
                                  #endif
                                  NP_Name,      "YAM PGP process",
                                  NP_StackSize, C->StackSize,
                                  NP_WindowPtr, -1, // no requester at all
                                  TAG_DONE);
      D(DBF_UTIL, "command '%s' returned with error code %ld", command, error);

      BusyEnd();

      Close(fho);
    }

    Close(fhi);
  }

  if(error > 0 && !hasNoErrorsFlag(flags))
    ER_NewError(tr(MSG_ER_PGPreturnsError), command, PGPLOGFILE);

  if(error < 0)
    ER_NewError(tr(MSG_ER_PGPnotfound), C->PGPCmdPath);

  if(error == 0 && !hasKeepLogFlag(flags))
  {
    if(DeleteFile(PGPLOGFILE) == 0)
      AddZombieFile(PGPLOGFILE);
  }

  RETURN(error);
  return error;
}
///
/// AppendToLogfile
//  Appends a line to the logfile
void AppendToLogfile(enum LFMode mode, int id, const char *text, ...)
{
  ENTER();

  // check the Logfile mode
  if(C->LogfileMode != LF_NONE &&
     (mode == LF_ALL || C->LogfileMode == mode))
  {
    // check if the event in question should really be logged or
    // not.
    if(C->LogAllEvents == TRUE || (id >= 30 && id <= 49))
    {
      FILE *fh;
      char logfile[SIZE_PATHFILE];
      char filename[SIZE_FILE];

      // if the user wants to split the logfile by date
      // we go and generate the filename now.
      if(C->SplitLogfile == TRUE)
      {
        struct ClockData cd;

        Amiga2Date(GetDateStamp(), &cd);
        snprintf(filename, sizeof(filename), "YAM-%s%d.log", months[cd.month-1], cd.year);
      }
      else
        strlcpy(filename, "YAM.log", sizeof(filename));

      // add the logfile path to the filename.
      AddPath(logfile, C->LogfilePath[0] != '\0' ? C->LogfilePath : G->ProgDir, filename, sizeof(logfile));

      // open the file handle in 'append' mode and output the
      // text accordingly.
      if((fh = fopen(logfile, "a")) != NULL)
      {
        char datstr[64];
        va_list args;

        DateStamp2String(datstr, sizeof(datstr), NULL, DSS_DATETIME, TZC_NONE);

        // output the header
        fprintf(fh, "%s [%02d] ", datstr, id);

        // compose the varags values
        va_start(args, text);
        vfprintf(fh, text, args);
        va_end(args);

        fprintf(fh, "\n");
        fclose(fh);
      }
    }
  }

  LEAVE();
}
///
/// Busy
//  Displays busy message
//  returns FALSE if the user pressed the stop button on an eventually active
//  BusyGauge. The calling method is therefore suggested to take actions to
//  stop its processing.
BOOL Busy(const char *text, const char *parameter, int cur, int max)
{
  // we can have different busy levels (defined BUSYLEVEL)
  static char infotext[BUSYLEVEL][SIZE_DEFAULT];
  BOOL result = TRUE;

  ENTER();

  if(text != NULL)
  {
    if(text[0] != '\0')
    {
      snprintf(infotext[BusyLevel], sizeof(infotext[BusyLevel]), text, parameter);

      if(max > 0)
      {
        // initialize the InfoBar gauge and also make sure it
        // shows a stop gadget in case cur < 0
        if(G->MA != NULL)
          DoMethod(G->MA->GUI.IB_INFOBAR, MUIM_InfoBar_ShowGauge, infotext[BusyLevel], cur, max);

        // check if we are in startup phase so that we also
        // update the gauge elements of the About window
        if(G->InStartupPhase == TRUE)
        {
          static char progressText[SIZE_DEFAULT];

          snprintf(progressText, sizeof(progressText), "%%ld/%d", max);

          DoMethod(G->SplashWinObject, MUIM_Splashwindow_StatusChange, infotext[BusyLevel], -1);
          DoMethod(G->SplashWinObject, MUIM_Splashwindow_ProgressChange, progressText, cur, max);
        }
      }
      else
      {
        // initialize the InfoBar infotext
        if(G->MA != NULL)
          DoMethod(G->MA->GUI.IB_INFOBAR, MUIM_InfoBar_ShowInfoText, infotext[BusyLevel]);
      }

      if(BusyLevel < BUSYLEVEL-1)
        BusyLevel++;
      else
        E(DBF_UTIL, "Error: reached highest BusyLevel!!!");
    }
    else
    {
      if(BusyLevel != 0)
        BusyLevel--;

      if(G->MA != NULL)
      {
        if(BusyLevel <= 0)
          DoMethod(G->MA->GUI.IB_INFOBAR, MUIM_InfoBar_HideBars);
        else
          DoMethod(G->MA->GUI.IB_INFOBAR, MUIM_InfoBar_ShowInfoText, infotext[BusyLevel-1]);
      }
    }
  }
  else
  {
    // If the text is NULL we just have to set the Gauge of the infoBar to the current
    // level
    if(BusyLevel > 0)
    {
      if(G->MA != NULL)
        result = DoMethod(G->MA->GUI.IB_INFOBAR, MUIM_InfoBar_ShowGauge, NULL, cur, max);

      if(G->InStartupPhase == TRUE)
        DoMethod(G->SplashWinObject, MUIM_Splashwindow_ProgressChange, NULL, cur, -1);
    }
  }

  RETURN(result);
  return result;
}

///
/// DisplayStatistics
//  Calculates folder statistics and update mailbox status icon
void DisplayStatistics(struct Folder *fo, BOOL updateAppIcon)
{
  int pos;
  struct MUI_NListtree_TreeNode *tn;
  struct Folder *actfo = FO_GetCurrentFolder();

  ENTER();

  // If the parsed argument is NULL we want to show the statistics from the actual folder
  if(fo == NULL)
    fo = actfo;
  else if(fo == (struct Folder *)-1)
    fo = FO_GetFolderByType(FT_INCOMING, NULL);

  // get the folder's position with in the tree
  if(fo != NULL && (pos = FO_GetFolderPosition(fo, TRUE)) >= 0)
  {
    D(DBF_GUI, "updating statistics for folder '%s', appicon %ld", fo->Name, updateAppIcon);

    // update the stats for this folder
    FO_UpdateStatistics(fo);

    // if this folder hasn't got any own folder image in the folder
    // directory and it is one of our standard folders we have to check which image we put in front of it
    if(fo->imageObject == NULL)
    {
      if(isIncomingFolder(fo))      fo->ImageIndex = (fo->Unread != 0) ? FICON_ID_INCOMING_NEW : FICON_ID_INCOMING;
      else if(isOutgoingFolder(fo)) fo->ImageIndex = (fo->Unread != 0) ? FICON_ID_OUTGOING_NEW : FICON_ID_OUTGOING;
      else if(isTrashFolder(fo))    fo->ImageIndex = (fo->Unread != 0) ? FICON_ID_TRASH_NEW : FICON_ID_TRASH;
      else if(isSentFolder(fo))     fo->ImageIndex = FICON_ID_SENT;
      else if(isSpamFolder(fo))     fo->ImageIndex = (fo->Unread != 0) ? FICON_ID_SPAM_NEW : FICON_ID_SPAM;
      else fo->ImageIndex = -1;
    }

    if(fo == actfo)
    {
      CallHookPkt(&MA_SetMessageInfoHook, 0, 0);
      CallHookPkt(&MA_SetFolderInfoHook, 0, 0);
      DoMethod(G->MA->GUI.IB_INFOBAR, MUIM_InfoBar_SetFolder, fo);
    }

    // Recalc the number of messages of the folder group
    if((tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->MA->GUI.NL_FOLDERS, MUIM_NListtree_GetEntry, MUIV_NListtree_GetEntry_ListNode_Root, pos, MUIF_NONE)))
    {
      struct MUI_NListtree_TreeNode *tn_parent;

      // Now lets redraw the folderentry in the listtree
      DoMethod(G->MA->GUI.NL_FOLDERS, MUIM_NListtree_Redraw, tn, MUIF_NONE);

      // Now we have to recalculate all parent and grandparents treenodes to
      // set their status accordingly.
      while((tn_parent = (struct MUI_NListtree_TreeNode *)DoMethod(G->MA->GUI.NL_FOLDERS, MUIM_NListtree_GetEntry, tn, MUIV_NListtree_GetEntry_Position_Parent, MUIF_NONE)))
      {
        if(tn_parent->tn_User != NULL)
        {
          struct Folder *fo_parent = ((struct FolderNode *)tn_parent->tn_User)->folder;
          int i;

          // clear the parent mailvariables first
          fo_parent->Unread = 0;
          fo_parent->New = 0;
          fo_parent->Total = 0;
          fo_parent->Sent = 0;
          fo_parent->Deleted = 0;

          // Now we scan every child of the parent and count the mails
          for(i=0;;i++)
          {
            struct MUI_NListtree_TreeNode *tn_child;
            struct Folder *fo_child;

            tn_child = (struct MUI_NListtree_TreeNode *)DoMethod(G->MA->GUI.NL_FOLDERS, MUIM_NListtree_GetEntry, tn_parent, i, MUIV_NListtree_GetEntry_Flag_SameLevel);
            if(tn_child == NULL)
              break;

            fo_child = ((struct FolderNode *)tn_child->tn_User)->folder;

            fo_parent->Unread    += fo_child->Unread;
            fo_parent->New       += fo_child->New;
            fo_parent->Total     += fo_child->Total;
            fo_parent->Sent      += fo_child->Sent;
            fo_parent->Deleted   += fo_child->Deleted;
          }

          DoMethod(G->MA->GUI.NL_FOLDERS, MUIM_NListtree_Redraw, tn_parent, MUIF_NONE);

          // for the next step we set tn to the current parent so that we get the
          // grandparents ;)
          tn = tn_parent;
        }
        else
          break;
      }
    }

    if(G->AppIconQuiet == FALSE && updateAppIcon == TRUE)
      UpdateAppIcon();
  }

  LEAVE();
}

///
/// CheckPrinter
//  Checks if printer is ready to print something
BOOL CheckPrinter(void)
{
  BOOL result = FALSE;

  ENTER();

  // check if the user wants us to check the printer state
  // at all.
  if(C->PrinterCheck == TRUE)
  {
    struct MsgPort *mp;

    // create the message port
    if((mp = AllocSysObjectTags(ASOT_PORT, TAG_DONE)) != NULL)
    {
      struct IOStdReq *pio;

      // create the IO request for checking the printer status
      if((pio = AllocSysObjectTags(ASOT_IOREQUEST,
                                   ASOIOR_Size,      sizeof(struct IOStdReq),
                                   ASOIOR_ReplyPort, (ULONG)mp,
                                   TAG_DONE)) != NULL)
      {
        // from here on we assume the printer is online
        // but we do deeper checks.
        result = TRUE;

        // open printer.device unit 0
        if(OpenDevice("printer.device", 0, (struct IORequest *)pio, 0) == 0)
        {
          // we allow to retry the checking so
          // we iterate into a do/while loop
          do
          {
            UWORD ioResult = 0;

            // fill the IO request for querying the
            // device/line status of printer.device
            pio->io_Message.mn_ReplyPort = mp;
            pio->io_Command = PRD_QUERY;
            pio->io_Data = &ioResult;
            pio->io_Actual = 0;

            // initiate the IO request
            if(DoIO((struct IORequest *)pio) == 0)
            {
              // printer seems to be a parallel printer
              if(pio->io_Actual == 1)
              {
                D(DBF_PRINT, "received io request status: %08lx", ioResult);

                // check for any possible error state
                if(isFlagSet(ioResult>>8, (1<<0))) // printer busy (offline)
                {
                  ULONG res;

                  W(DBF_PRINT, "printer found to be in 'busy or offline' status");

                  // issue a requester telling the user about the faulty
                  // printer state
                  res = MUI_Request(G->App, NULL, 0, tr(MSG_ErrorReq),
                                                     tr(MSG_ER_PRINTER_OFFLINE_GADS),
                                                     tr(MSG_ER_PRINTER_OFFLINE));

                  if(res == 0) // Cancel/ESC
                  {
                    result = FALSE;
                    break;
                  }
                  else if(res == 1) // Retry
                    continue;
                  else // Ignore
                    break;
                }
                else if(isFlagSet(ioResult>>8, (1<<1))) // paper out
                {
                  ULONG res;

                  W(DBF_PRINT, "printer found to be in 'paper out' status");

                  // issue a requester telling the user about the faulty
                  // printer state
                  res = MUI_Request(G->App, NULL, 0, tr(MSG_ErrorReq),
                                                     tr(MSG_ER_PRINTER_NOPAPER_GADS),
                                                     tr(MSG_ER_PRINTER_NOPAPER));

                  if(res == 0) // Cancel/ESC
                  {
                    result = FALSE;
                    break;
                  }
                  else if(res == 1) // Retry
                    continue;
                  else // Ignore
                    break;
                }
                else
                {
                  D(DBF_PRINT, "printer was found to be ready");
                  break;
                }
              }
              else
              {
                // the rest signals an unsupported printer device
                // for status checking, so we assume the printer to
                // be online
                W(DBF_PRINT, "unsupported printer device ID '%ld'. Assuming online.", pio->io_Actual);
                break;
              }
            }
            else
            {
              W(DBF_PRINT, "DoIO() on printer status request failed!");
              break;
            }
          }
          while(TRUE);

          CloseDevice((struct IORequest *)pio);
        }
        else
          W(DBF_PRINT, "couldn't open printer.device unit 0");

        FreeSysObject(ASOT_IOREQUEST, pio);
      }
      else
        W(DBF_PRINT, "wasn't able to create io request for printer state checking");

      FreeSysObject(ASOT_PORT, mp);
    }
    else
      W(DBF_PRINT, "wasn't able to create msg port for printer state checking");
  }
  else
  {
    W(DBF_PRINT, "PrinterCheck disabled, assuming printer online");
    result = TRUE;
  }

  RETURN(result);
  return result;
}
///
/// PlaySound
//  Plays a sound file using datatypes
BOOL PlaySound(const char *filename)
{
  BOOL result = FALSE;

  ENTER();

  if(DataTypesBase != NULL)
  {
    // if we previously created a sound object
    // lets dispose it first.
    if(G->NewMailSound_Obj != NULL)
      DisposeDTObject(G->NewMailSound_Obj);

    // create the new datatype object
    if((G->NewMailSound_Obj = NewDTObject((char *)filename, DTA_SourceType, DTST_FILE,
                                                            DTA_GroupID,    GID_SOUND,
                                                            SDTA_Cycles,    1,
                                                            TAG_DONE)) != NULL)
    {
      // create a datatype trigger
      struct dtTrigger dtt;

      // Fill the trigger
      dtt.MethodID     = DTM_TRIGGER;
      dtt.dtt_GInfo    = NULL;
      dtt.dtt_Function = STM_PLAY;
      dtt.dtt_Data     = NULL;

      // Play the sound by calling DoMethodA()
      if(DoMethodA(G->NewMailSound_Obj, (APTR)&dtt) == 1)
        result = TRUE;

      D(DBF_UTIL, "started playback of '%s' returned %ld", filename, result);
    }
    else
      W(DBF_UTIL, "failed to create sound DT object from '%s'", filename);
  }
  else
    W(DBF_UTIL, "datatypes.library missing, no sound playback!");

  RETURN(result);
  return result;
}
///
/// MatchExtension
//  Matches a file extension against a list of extension
static BOOL MatchExtension(const char *fileext, const char *extlist)
{
  BOOL result = FALSE;

  ENTER();

  if(extlist)
  {
    const char *s = extlist;
    size_t extlen = strlen(fileext);

    // now we search for our delimiters step by step
    while(*s)
    {
      const char *e;

      if((e = strpbrk(s, " |;,")) == NULL)
        e = s+strlen(s);

      D(DBF_MIME, "try matching file extension '%s' with '%s' %ld", fileext, s, e-s);

      // now check if the extension matches
      if((size_t)(e-s) == extlen &&
         strnicmp(s, fileext, extlen) == 0)
      {
        D(DBF_MIME, "matched file extension '%s' with type '%s'", fileext, s);

        result = TRUE;
        break;
      }

      // set the next start to our last search
      if(*e)
        s = ++e;
      else
        break;
    }
  }

  RETURN(result);
  return result;
}

///
/// IdentifyFile
// Tries to identify a file and returns its content-type if applicable
// otherwise NULL
const char *IdentifyFile(const char *fname)
{
  char ext[SIZE_FILE];
  const char *ctype = NULL;

  ENTER();

  // Here we try to identify the file content-type in multiple steps:
  //
  // 1: try to walk through the users' mime type list and check if
  //    a specified extension in the list matches the one of our file.
  //
  // 2: check against our hardcoded internal list of known extensions
  //    and try to do some semi-detailed analysis of the file header
  //
  // 3: use datatypes.library to find out the file class and construct
  //    an artifical content-type partly matching the file.

  // extract the extension of the file name first
  stcgfe(ext, fname);
  SHOWSTRING(DBF_MIME, ext);

  // now we try to identify the file by the extension first
  if(ext[0] != '\0')
  {
    struct MinNode *curNode;

    D(DBF_MIME, "identifying file by extension (mimeTypeList)");
    // identify by the user specified mime types
    for(curNode = C->mimeTypeList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
    {
      struct MimeTypeNode *curType = (struct MimeTypeNode *)curNode;

      if(curType->Extension[0] != '\0' &&
         MatchExtension(ext, curType->Extension))
      {
        ctype = curType->ContentType;
        break;
      }
    }

    if(ctype == NULL)
    {
      unsigned int i;

      D(DBF_MIME, "identifying file by extension (hardcoded list)");

      // before we are going to try to identify the file by reading some bytes out of
      // it, we try to identify it only by the extension.
      for(i=0; IntMimeTypeArray[i].ContentType != NULL; i++)
      {
        if(IntMimeTypeArray[i].Extension != NULL &&
           MatchExtension(ext, IntMimeTypeArray[i].Extension))
        {
          ctype = IntMimeTypeArray[i].ContentType;
          break;
        }
      }
    }
  }

  // go on if we haven't got a content-type yet and try to identify
  // it with our own, hardcoded means.
  if(ctype == NULL)
  {
    FILE *fh;

    D(DBF_MIME, "identifying file by binary comparing the first bytes of '%s'", fname);

    // now that we still haven't been able to identify the file, we go
    // and read in some bytes from the file and try to identify it by analyzing
    // the binary data.
    if((fh = fopen(fname, "r")))
    {
      char buffer[SIZE_LARGE];
      int rlen;

      // we read in SIZE_LARGE into our temporary buffer without
      // checking if it worked out.
      rlen = fread(buffer, 1, SIZE_LARGE-1, fh);
      buffer[rlen] = '\0'; // NUL terminate the buffer.

      // close the file immediately.
      fclose(fh);
      fh = NULL;

      if(!strnicmp(buffer, "@database", 9))                                      ctype = IntMimeTypeArray[MT_TX_GUIDE].ContentType;
      else if(!strncmp(buffer, "%PDF-", 5))                                      ctype = IntMimeTypeArray[MT_AP_PDF].ContentType;
      else if(!strncmp(&buffer[2], "-lh5-", 5))                                  ctype = IntMimeTypeArray[MT_AP_LHA].ContentType;
      else if(!strncmp(buffer, "LZX", 3))                                        ctype = IntMimeTypeArray[MT_AP_LZX].ContentType;
      else if(*((long *)buffer) >= HUNK_UNIT && *((long *)buffer) <= HUNK_INDEX) ctype = IntMimeTypeArray[MT_AP_AEXE].ContentType;
      else if(!strncmp(&buffer[6], "JFIF", 4))                                   ctype = IntMimeTypeArray[MT_IM_JPG].ContentType;
      else if(!strncmp(buffer, "GIF8", 4))                                       ctype = IntMimeTypeArray[MT_IM_GIF].ContentType;
      else if(!strncmp(&buffer[1], "PNG", 3))                                    ctype = IntMimeTypeArray[MT_IM_PNG].ContentType;
      else if(!strncmp(&buffer[8], "ILBM", 4) && !strncmp(buffer, "FORM", 4))    ctype = IntMimeTypeArray[MT_IM_ILBM].ContentType;
      else if(!strncmp(&buffer[8], "8SVX", 4) && !strncmp(buffer, "FORM", 4))    ctype = IntMimeTypeArray[MT_AU_8SVX].ContentType;
      else if(!strncmp(&buffer[8], "ANIM", 4) && !strncmp(buffer, "FORM", 4))    ctype = IntMimeTypeArray[MT_VI_ANIM].ContentType;
      else if(stristr(buffer, "\nFrom:"))                                        ctype = IntMimeTypeArray[MT_ME_EMAIL].ContentType;
      else
      {
        // now we do a statistical analysis to see if the file
        // is a binary file or not. Because then we use datatypes.library
        // for generating an artificial MIME type.
        int notascii = 0;
        int i;

        for(i=0; i < rlen; i++)
        {
          unsigned char c = buffer[i];

          // see if the current buffer position is
          // considered an ASCII/SPACE char.
          if((c < 32 || c > 127) && !isspace(c))
            notascii++;
        }

        // if the amount of not ASCII chars is lower than rlen/10 we
        // consider it a text file and don't do a deeper analysis.
        if(notascii < rlen/10)
        {
          ULONG prot;

          ObtainFileInfo(fname, FI_PROTECTION, &prot);
          ctype = IntMimeTypeArray[(prot & FIBF_SCRIPT) ? MT_AP_SCRIPT : MT_TX_PLAIN].ContentType;
        }
        else
        {
          D(DBF_MIME, "identifying file through datatypes.library");

          // per default we end up with an "application/octet-stream" content-type
          ctype = IntMimeTypeArray[MT_AP_OCTET].ContentType;

          if(DataTypesBase != NULL)
          {
            BPTR lock;

            if((lock = Lock(fname, ACCESS_READ)))
            {
              struct DataType *dtn;

              if((dtn = ObtainDataTypeA(DTST_FILE, (APTR)lock, NULL)) != NULL)
              {
                const char *type = NULL;
                struct DataTypeHeader *dth = dtn->dtn_Header;

                switch(dth->dth_GroupID)
                {
                  case GID_SYSTEM:     break;
                  case GID_DOCUMENT:   type = "application"; break;
                  case GID_TEXT:       type = "text"; break;
                  case GID_MUSIC:
                  case GID_SOUND:
                  case GID_INSTRUMENT: type = "audio"; break;
                  case GID_PICTURE:    type = "image"; break;
                  case GID_MOVIE:
                  case GID_ANIMATION:  type = "video"; break;
                }

                if(type)
                {
                  static char contentType[SIZE_CTYPE];

                  snprintf(contentType, sizeof(contentType), "%s/x-%s", type, dth->dth_BaseName);
                  ctype = contentType;
                }

                ReleaseDataType(dtn);
              }

              UnLock (lock);
            }
          }
        }
      }
    }
  }

  RETURN(ctype);
  return ctype;
}
///
/// GetRealPath
//  Function that gets the real path out of a supplied path. It will correctly resolve pathes like PROGDIR: aso.
char *GetRealPath(char *path)
{
  char *realPath;
  BPTR lock;
  BOOL success = FALSE;
  static char buf[SIZE_PATHFILE];

  ENTER();

  // lets try to get a Lock on the supplied path
  if((lock = Lock(path, SHARED_LOCK)))
  {
    // so, if it seems to exists, we get the "real" name out of
    // the lock again.
    if(NameFromLock(lock, buf, sizeof(buf)) != DOSFALSE)
      success = TRUE;

    // And then we unlock the file/dir immediatly again.
    UnLock(lock);
  }

  // only on success we return the realpath.
  realPath = success ? buf : path;

  RETURN(realPath);
  return realPath;
}

///
/// ExecuteCommand
//  Executes a DOS command
BOOL ExecuteCommand(char *cmd, BOOL asynch, enum OutputDefType outdef)
{
  BOOL result = TRUE;
  BPTR path;
  BPTR in = 0;
  BPTR out = 0;
  #if defined(__amigaos4__)
  BPTR err = 0;
  #endif

  ENTER();
  SHOWSTRING(DBF_UTIL, cmd);

  switch(outdef)
  {
    case OUT_DOS:
    {
      in = Input();
      out = Output();
      #if defined(__amigaos4__)
      err = ErrorOutput();
      #endif

      asynch = FALSE;
    }
    break;

    case OUT_NIL:
    {
      in = Open("NIL:", MODE_OLDFILE);
      out = Open("NIL:", MODE_NEWFILE);
    }
    break;
  }

  // path may return 0, but that's fine.
  // and we also don't free it manually as this
  // is done by SystemTags/CreateNewProc itself.
  path = CloneSearchPath();

  if(SystemTags(cmd,
                SYS_Input,    in,
                SYS_Output,   out,
                #if defined(__amigaos4__)
                SYS_Error,    err,
                NP_Child,     TRUE,
                #endif
                NP_Name,      "YAM command process",
                NP_Path,      path,
                NP_StackSize, C->StackSize,
                NP_WindowPtr, -1,           // show no requesters at all
                SYS_Asynch,   asynch,
                TAG_DONE) != 0)
  {
    // an error occurred as SystemTags should always
    // return zero on success, no matter what.
    E(DBF_UTIL, "execution of command '%s' failed, IoErr()=%ld", cmd, IoErr());

    // manually free our search path
    // as SystemTags() shouldn't have freed
    // it itself.
    if(path != 0)
      FreeSearchPath(path);

    result = FALSE;
  }

  if(asynch == FALSE && outdef != OUT_DOS)
  {
    Close(out);
    Close(in);
  }

  RETURN(result);
  return result;
}
///
/// GetSimpleID
//  Returns a unique number
int GetSimpleID(void)
{
  static int num = 0;

  return ++num;
}
///
/// GotoURL
//  Loads an URL using an ARexx script or openurl.library
void GotoURL(const char *url, BOOL newWindow)
{
  ENTER();

  // The ARexx macro to open a URL is only possible after the startup phase
  // and if a script has been configured for this purpose.
  if(G != NULL && G->InStartupPhase == FALSE && C->RX[MACRO_URL].Script[0] != '\0')
  {
    char newurl[SIZE_LARGE];

    snprintf(newurl, sizeof(newurl), "\"%s\"", url);
    MA_StartMacro(MACRO_URL, newurl);
  }
  else if(OpenURLBase != NULL)
  {
    // open the URL in a defined web browser and
    // let the user decide himself if he wants to see
    // it popping up in a new window or not (via OpenURL
    // prefs)
    URL_Open((STRPTR)url, URL_NewWindow, newWindow,
                          TAG_DONE);
  }
  else
    W(DBF_HTML, "No openurl.library v1+ found");

  LEAVE();
}
///
/// SWSSearch
// Smith&Waterman 1981 extended string similarity search algorithm
// X, Y are the two strings that will be compared for similarity
// It will return a pattern which will reflect the similarity of str1 and str2
// in a Amiga suitable format. This is case-insensitive !
char *SWSSearch(char *str1, char *str2)
{
  char *similar;
  static char *Z = NULL;    // the destination string (result)
  int **L        = NULL;    // L matrix
  int **Ind      = NULL;    // Indicator matrix
  char *X;                  // 1.string X
  char *Y        = NULL;    // 2.string Y
  int lx;                   // length of X
  int ly;                   // length of Y
  int lz;                   // length of Z (maximum)
  int i, j;
  BOOL gap = FALSE;
  BOOL success = FALSE;

  // special enum for the Indicator
  enum  IndType { DELX=1, DELY, DONE, TAKEBOTH };

  ENTER();

  // by calling this function with (NULL, NULL) someone wants
  // to signal us to free the destination string
  if(str1 == NULL || str2 == NULL)
  {
    if(Z != NULL)
      free(Z);
    Z = NULL;

    RETURN(NULL);
    return NULL;
  }

  // calculate the length of our buffers we need
  lx = strlen(str1)+1;
  ly = strlen(str2)+1;
  lz = MAX(lx, ly)*3+3;

  // first allocate all resources
  if(!(X   = calloc(lx+1, sizeof(char)))) goto abort;
  if(!(Y   = calloc(ly+1, sizeof(char)))) goto abort;

  // now we have to alloc our help matrixes
  if(!(L   = calloc(lx,   sizeof(int))))  goto abort;
  if(!(Ind = calloc(lx,   sizeof(int))))  goto abort;
  for(i = 0; i < lx; i++)
  {
    if(!(L[i]   = calloc(ly, sizeof(int)))) goto abort;
    if(!(Ind[i] = calloc(ly, sizeof(int)))) goto abort;
  }

  // and allocate the result string separately
  if(Z != NULL)
    free(Z);
  if(!(Z = calloc(lz, sizeof(char)))) goto abort;

  // we copy str1&str2 into X and Y but have to copy a placeholder in front of them
  memcpy(&X[1], str1, lx);
  memcpy(&Y[1], str2, ly);

  for(i = 1; i < lx; i++)
    Ind[i][0] = DELX;

  for(j = 1; j < ly; j++)
    Ind[0][j] = DELY;

  Ind[0][0] = DONE;

  // Now we calculate the L matrix
  // this is the first step of the SW algorithm
  for(i = 1; i < lx; i++)
  {
    for(j = 1; j < ly; j++)
    {
      if(toupper(X[i]) == toupper(Y[j]))  // case insensitive version
      {
        L[i][j] = L[i-1][j-1] + 1;
        Ind[i][j] = TAKEBOTH;
      }
      else
      {
        if(L[i-1][j] > L[i][j-1])
        {
          L[i][j] = L[i-1][j];
          Ind[i][j] = DELX;
        }
        else
        {
          L[i][j] = L[i][j-1];
          Ind[i][j] = DELY;
        }
      }
    }
  }

#ifdef DEBUG
  // for debugging only
  // This will print out the L & Ind matrix to identify problems
/*
  printf(" ");
  for(j=0; j < ly; j++)
  {
    printf(" %c", Y[j]);
  }
  printf("\n");

  for(i=0; i < lx; i++)
  {
    printf("%c ", X[i]);

    for(j=0; j < ly; j++)
    {
      printf("%d", L[i][j]);
      if(Ind[i][j] == TAKEBOTH)  printf("'");
      else if(Ind[i][j] == DELX) printf("^");
      else if(Ind[i][j] == DELY) printf("<");
      else printf("*");
    }
    printf("\n");
  }
*/
#endif

  // the second step of the SW algorithm where we
  // process the Ind matrix which represents which
  // char we take and which we delete

  Z[--lz] = '\0';
  i = lx-1;
  j = ly-1;

  while(i >= 0 && j >= 0 && Ind[i][j] != DONE)
  {
    if(Ind[i][j] == TAKEBOTH)
    {
      Z[--lz] = X[i];

      i--;
      j--;
      gap = FALSE;
    }
    else if(Ind[i][j] == DELX)
    {
      if(!gap)
      {
        if(j > 0)
        {
          Z[--lz] = '?';
          Z[--lz] = '#';
        }
        gap = TRUE;
      }
      i--;
    }
    else if(Ind[i][j] == DELY)
    {
      if(!gap)
      {
        if(i > 0)
        {
          Z[--lz] = '?';
          Z[--lz] = '#';
        }
        gap = TRUE;
      }
      j--;
    }
  }

  success = TRUE;

abort:

  // now we free our temporary buffers now
  if(X != NULL)
    free(X);
  if(Y != NULL)
    free(Y);

  // lets free our help matrixes
  if(L != NULL)
  {
    for(i = 0; i < lx; i++)
    {
      if(L[i] != NULL)
        free(L[i]);
    }
    free(L);
  }
  if(Ind != NULL)
  {
    for(i = 0; i < lx; i++)
    {
      if(Ind[i] != NULL)
        free(Ind[i]);
    }
    free(Ind);
  }

  similar = success ? &(Z[lz]) : NULL;

  RETURN(similar);
  return similar;
}
///
/// CRC32
//  Function that calculates a 32bit crc checksum for a provided buffer.
//  See http://www.4d.com/ACIDOC/CMU/CMU79909.HTM for more information about
//  the CRC32 algorithm.
//  This implementation allows the usage of more than one persistant calls of
//  the crc32 function. This allows to calculate a valid crc32 checksum over
//  an unlimited amount of buffers.
ULONG CRC32(const void *buffer, unsigned int count, ULONG crc)
{
  /* table generated with the following code:
   *
   * #define CRC32_POLYNOMIAL 0xEDB88320L
   *
   * int i, j;
   *
   * for (i = 0; i <= 255; i++) {
   *   unsigned long crc = i;
   *   for (j = 8; j > 0; j--) {
   *     if (crc & 1)
   *       crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
   *     else
   *       crc >>= 1;
   *   }
   *   CRCTable[i] = crc;
   * }
   */
  static const unsigned long CRCTable[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
    0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
    0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
    0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
    0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
    0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
    0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
    0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
    0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
    0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
    0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
    0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
  };
  const unsigned char *p = (const unsigned char *)buffer;

  ENTER();

  // we calculate the crc32 now.
  while(count-- != 0)
  {
    ULONG temp1 = (crc >> 8) & 0x00FFFFFFL;
    ULONG temp2 = CRCTable[((int)crc ^ *p++) & 0xFF];

    crc = temp1 ^ temp2;
  }

  RETURN(crc);
  return crc;
}

///
/// strippedCharsetName()
// return the charset code stripped and without any white spaces
char *strippedCharsetName(const struct codeset* codeset)
{
  char *strStart = TrimStart(codeset->name);
  char *strEnd = strchr(strStart, ' ');

  if(strEnd > strStart || strStart > codeset->name)
  {
    static char strippedName[SIZE_CTYPE+1];

    if(strEnd > strStart && (size_t)(strEnd-strStart) < sizeof(strippedName))
      strlcpy(strippedName, strStart, strEnd-strStart+1);
    else
      strlcpy(strippedName, strStart, sizeof(strippedName));

    return strippedName;
  }
  else
    return codeset->name;
}

///
/// GetPubScreenName
// return the name of a public screen, if the screen is public
void GetPubScreenName(struct Screen *screen, char *pubName, ULONG pubNameSize)
{
  ENTER();

  // we use "Workbench" as the default public screen name
  strlcpy(pubName, "Workbench", pubNameSize);

  if(screen != NULL)
  {
    // try to get the public screen name
    #if defined(__amigaos4__)
    // this very handy function is OS4 only
    if(GetScreenAttr(screen, SA_PubName, pubName, pubNameSize) == 0)
    {
      // GetScreenAttr() failed, copy the default name again, in case it was changed
      strlcpy(pubName, "Workbench", pubNameSize);
    }
    #else
    struct List *pubScreenList;

    // on all other systems we have to obtain the public screen name in the hard way
    // first get the list of all public screens
    if((pubScreenList = LockPubScreenList()) != NULL)
    {
      struct PubScreenNode *psn;

      // then iterate through this list
      for(psn = (struct PubScreenNode *)pubScreenList->lh_Head; psn->psn_Node.ln_Succ != NULL; psn = (struct PubScreenNode *)psn->psn_Node.ln_Succ)
      {
        // check if we found the given screen
        if(psn->psn_Screen == screen)
        {
          // copy the name and get out of the loop
          strlcpy(pubName, psn->psn_Node.ln_Name, pubNameSize);
          break;
        }
      }

      // unlock the list again
      UnlockPubScreenList();
    }
    #endif
  }

  LEAVE();
}

///

/*** REXX interface support ***/
/// AllocReqText
//  Prepare multi-line text for requesters, converts \n to line breaks
char *AllocReqText(char *s)
{
  char *reqtext;

  ENTER();

  if((reqtext = calloc(strlen(s) + 1, 1)) != NULL)
  {
    char *d = reqtext;

    while(*s != '\0')
    {
      if(s[0] == '\\' && s[1] == 'n')
      {
        *d++ = '\n';
        s++;
        s++;
      }
      else
        *d++ = *s++;
    }
  }

  RETURN(reqtext);
  return reqtext;
}
///
/// ToLowerCase
//  Change a complete string to lower case
void ToLowerCase(char *str)
{
  char c;

  ENTER();

  while ((c = *str) != '\0')
    *str++ = tolower(c);

  LEAVE();
}
///
/// WriteUInt32
//  write a 32bit variable to a stream, big endian style
int WriteUInt32(FILE *stream, ULONG value)
{
  int n;

  ENTER();

  // convert the value to big endian style
  value = htonl(value);

  n = fwrite(&value, sizeof(value), 1, stream);

  RETURN(n);
  return n;
}
///
/// ReadUInt32
//  read a 32bit variable from a stream, big endian style
int ReadUInt32(FILE *stream, ULONG *value)
{
  int n;

  ENTER();

  if((n = fread(value, sizeof(*value), 1, stream)) == 1)
  {
    // convert the value to big endian style
    *value = ntohl(*value);
  }

  RETURN(n);
  return n;
}
///
