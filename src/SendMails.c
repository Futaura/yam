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
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/muimaster.h>

#include <mui/NList_mcc.h>

#include "YAM.h"
#include "YAM_addressbookEntry.h"
#include "YAM_config.h"
#include "YAM_error.h"
#include "YAM_find.h"
#include "YAM_main.h"
#include "YAM_mainFolder.h"
#include "YAM_stringsizes.h"

#include "mime/base64.h"
#include "mime/md5.h"

#include "extrasrc.h"

#include "mui/Classes.h"

#include "Locale.h"
#include "MailList.h"
#include "MUIObjects.h"
#include "SendMails.h"
#include "Transfer.h"

#include "Debug.h"

/**************************************************************************/
// SMTP commands (RFC 821) and extended ESMTP.
// the order of the following enum & pointer array is important and have to
// match each other or weird things will happen.
enum SMTPCommand
{
  // SMTP commands
  SMTP_HELO, SMTP_MAIL, SMTP_RCPT, SMTP_DATA, SMTP_SEND, SMTP_SOML, SMTP_SAML, SMTP_RSET,
  SMTP_VRFY, SMTP_EXPN, SMTP_HELP, SMTP_NOOP, SMTP_QUIT, SMTP_TURN, SMTP_FINISH, SMTP_CONNECT,

  // ESMTP commands
  ESMTP_EHLO, ESMTP_STARTTLS, ESMTP_AUTH_CRAM_MD5, ESMTP_AUTH_DIGEST_MD5, ESMTP_AUTH_LOGIN,
  ESMTP_AUTH_PLAIN
};

static const char *const SMTPcmd[] =
{
  // SMTP commands
  "HELO", "MAIL", "RCPT", "DATA", "SEND", "SOML", "SAML", "RSET",
  "VRFY", "EXPN", "HELP", "NOOP", "QUIT", "TURN", "\r\n.", "",

  // ESMTP commands
  "EHLO", "STARTTLS", "AUTH CRAM-MD5", "AUTH DIGEST-MD5", "AUTH LOGIN",
  "AUTH PLAIN"
};

// SMTP Status Messages
#define SMTP_SERVICE_NOT_AVAILABLE 421
#define SMTP_ACTION_OK             250

// SMTP server capabilities flags & macros
#define SMTP_FLG_ESMTP               (1<<0)
#define SMTP_FLG_AUTH_CRAM_MD5       (1<<1)
#define SMTP_FLG_AUTH_DIGEST_MD5     (1<<2)
#define SMTP_FLG_AUTH_LOGIN          (1<<3)
#define SMTP_FLG_AUTH_PLAIN          (1<<4)
#define SMTP_FLG_STARTTLS            (1<<5)
#define SMTP_FLG_SIZE                (1<<6)
#define SMTP_FLG_PIPELINING          (1<<7)
#define SMTP_FLG_8BITMIME            (1<<8)
#define SMTP_FLG_DSN                 (1<<9)
#define SMTP_FLG_ETRN                (1<<10)
#define SMTP_FLG_ENHANCEDSTATUSCODES (1<<11)
#define SMTP_FLG_DELIVERBY           (1<<12)
#define SMTP_FLG_HELP                (1<<13)
#define hasESMTP(v)                  (isFlagSet((v), SMTP_FLG_ESMTP))
#define hasCRAM_MD5_Auth(v)          (isFlagSet((v), SMTP_FLG_AUTH_CRAM_MD5))
#define hasDIGEST_MD5_Auth(v)        (isFlagSet((v), SMTP_FLG_AUTH_DIGEST_MD5))
#define hasLOGIN_Auth(v)             (isFlagSet((v), SMTP_FLG_AUTH_LOGIN))
#define hasPLAIN_Auth(v)             (isFlagSet((v), SMTP_FLG_AUTH_PLAIN))
#define hasSTARTTLS(v)               (isFlagSet((v), SMTP_FLG_STARTTLS))
#define hasSIZE(v)                   (isFlagSet((v), SMTP_FLG_SIZE))
#define hasPIPELINING(v)             (isFlagSet((v), SMTP_FLG_PIPELINING))
#define has8BITMIME(v)               (isFlagSet((v), SMTP_FLG_8BITMIME))
#define hasDSN(v)                    (isFlagSet((v), SMTP_FLG_DSN))
#define hasETRN(v)                   (isFlagSet((v), SMTP_FLG_ETRN))
#define hasENHANCEDSTATUSCODES(v)    (isFlagSet((v), SMTP_FLG_ENHANCEDSTATUSCODES))
#define hasDELIVERBY(v)              (isFlagSet((v), SMTP_FLG_DELIVERBY))
#define hasHELP(v)                   (isFlagSet((v), SMTP_FLG_HELP))

// help macros for SMTP routines
#define getResponseCode(str)          ((int)strtol((str), NULL, 10))

/**************************************************************************/
// static function prototypes

/**************************************************************************/
// local macros & defines
#define GetLong(p,o)  ((((unsigned char*)(p))[o])       | \
                       (((unsigned char*)(p))[o+1]<<8)  | \
                       (((unsigned char*)(p))[o+2]<<16) | \
                       (((unsigned char*)(p))[o+3]<<24))

/***************************************************************************
 Module: SendMails
***************************************************************************/

/*** SMTP routines ***/
/// TR_SendSMTPCmd
//  Sends a command to the SMTP server and returns the response message
//  described in (RFC 2821)
static char *TR_SendSMTPCmd(struct TransferNode *tfn, const enum SMTPCommand command,
                            const char *parmtext, const char *errorMsg)
{
  BOOL result = FALSE;
  static char buf[SIZE_LINE]; // RFC 2821 says 1000 should be enough

  ENTER();

  // first we check if the socket is ready
  if(tfn->socket != TCP_NO_SOCKET)
  {
    // now we prepare the SMTP command
    if(parmtext == NULL || parmtext[0] == '\0')
      snprintf(buf, sizeof(buf), "%s\r\n", SMTPcmd[command]);
    else
      snprintf(buf, sizeof(buf), "%s %s\r\n", SMTPcmd[command], parmtext);

    D(DBF_NET, "TCP: SMTP cmd '%s' with param '%s'", SMTPcmd[command], SafeStr(parmtext));

    // lets send the command via TR_WriteLine, but not if we are in connection
    // state
    if(command == SMTP_CONNECT || TR_WriteLine(tfn, buf) > 0)
    {
      int len = 0;

      // after issuing the SMTP command we read out the server response to it
      // but only if this wasn't the SMTP_QUIT command.
      if((len = TR_ReadLine(tfn, buf, sizeof(buf))) > 0)
      {
        // get the response code
        int rc = strtol(buf, NULL, 10);

        // if the response is a multiline response we have to get out more
        // from the socket
        if(buf[3] == '-') // (RFC 2821) - section 4.2.1
        {
          char tbuf[SIZE_LINE];

          // now we concatenate the multiline reply to
          // out main buffer
          do
          {
            // lets get out the next line from the socket
            if((len = TR_ReadLine(tfn, tbuf, sizeof(tbuf))) > 0)
            {
              // get the response code
              int rc2 = strtol(buf, NULL, 10);

              // check if the response code matches the one
              // of the first line
              if(rc == rc2)
              {
                // lets concatenate both strings while stripping the
                // command code and make sure we didn't reach the end
                // of the buffer
                if(strlcat(buf, tbuf, sizeof(buf)) >= sizeof(buf))
                  W(DBF_NET, "buffer overrun on trying to concatenate a multiline reply!");
              }
              else
              {
                E(DBF_NET, "response codes of multiline reply doesn't match!");

                errorMsg = NULL;
                len = 0;
                break;
              }
            }
            else
            {
              errorMsg = tr(MSG_ER_CONNECTIONBROKEN);
              break;
            }
          }
          while(tbuf[3] == '-');
        }

        // check that the concatentation worked
        // out fine and that the rc is valid
        if(len > 0 && rc >= 100)
        {
          // Now we check if we got the correct response code for the command
          // we issued
          switch(command)
          {
            //  Reponse    Description (RFC 2821 - section 4.2.1)
            //  1xx        Positive Preliminary reply
            //  2xx        Positive Completion reply
            //  3xx        Positive Intermediate reply
            //  4xx        Transient Negative Completion reply
            //  5xx        Permanent Negative Completion reply

            case SMTP_HELP:    { result = (rc == 211 || rc == 214); } break;
            case SMTP_VRFY:    { result = (rc == 250 || rc == 251); } break;
            case SMTP_CONNECT: { result = (rc == 220); } break;
            case SMTP_QUIT:    { result = (rc == 221); } break;
            case SMTP_DATA:    { result = (rc == 354); } break;

            // all codes that accept 250 response code
            case SMTP_HELO:
            case SMTP_MAIL:
            case SMTP_RCPT:
            case SMTP_FINISH:
            case SMTP_RSET:
            case SMTP_SEND:
            case SMTP_SOML:
            case SMTP_SAML:
            case SMTP_EXPN:
            case SMTP_NOOP:
            case SMTP_TURN:    { result = (rc == 250); } break;

            // ESMTP commands & response codes
            case ESMTP_EHLO:            { result = (rc == 250); } break;
            case ESMTP_STARTTLS:        { result = (rc == 220); } break;

            // ESMTP_AUTH command responses
            case ESMTP_AUTH_CRAM_MD5:
            case ESMTP_AUTH_DIGEST_MD5:
            case ESMTP_AUTH_LOGIN:
            case ESMTP_AUTH_PLAIN:      { result = (rc == 334); } break;
          }
        }
      }
      else
      {
        // Unfortunately, there are broken SMTP server implementations out there
        // like the one used by "smtp.googlemail.com" or "smtp.gmail.com".
        //
        // It seems these broken SMTP servers do automatically drop the
        // data connection right after the 'QUIT' command was send and don't
        // reply with a status message like it is clearly defined in RFC 2821
        // (section 4.1.1.10). Unfortunately we can't do anything about
        // it really and have to consider this a bad and ugly workaround. :(
        if(command == SMTP_QUIT)
        {
          W(DBF_NET, "broken SMTP server implementation found on QUIT, keeping quiet...");

          result = TRUE;
          buf[0] = '\0';
        }
        else
          errorMsg = tr(MSG_ER_CONNECTIONBROKEN);
      }
    }
    else
      errorMsg = tr(MSG_ER_CONNECTIONBROKEN);
  }
  else
    errorMsg = tr(MSG_ER_CONNECTIONBROKEN);

  // the rest of the responses throws an error
  if(result == FALSE)
  {
    if(errorMsg != NULL)
      ER_NewError(errorMsg, tfn->msn->hostname, (char *)SMTPcmd[command], buf);

    RETURN(NULL);
    return NULL;
  }
  else
  {
    RETURN(buf);
    return buf;
  }
}
///
/// TR_ConnectSMTP
//  Connects to a SMTP mail server - here we always try to do an ESMTP connection
//  first via an EHLO command and then check if it succeeded or not.
static BOOL TR_ConnectSMTP(struct TransferNode *tfn)
{
  BOOL result = FALSE;

  ENTER();

  // If we did a TLS negotitaion previously we have to skip the
  // welcome message, but if it was another connection like a normal or a SSL
  // one we have wait for the welcome
  if(tfn->useSSL == FALSE || hasServerSSL(tfn->msn))
  {
    set(G->transferWindowObject, MUIA_TransferWindow_StatusLabel, tr(MSG_TR_WaitWelcome));

    result = (TR_SendSMTPCmd(tfn, SMTP_CONNECT, NULL, tr(MSG_ER_BADRESPONSE_SMTP)) != NULL);
  }
  else
    result = TRUE;

  // now we either send a HELO (non-ESMTP) or EHLO (ESMTP) command to
  // signal we wanting to start a session accordingly (RFC 1869 - section 4)
  if(result && tfn->socket != TCP_NO_SOCKET)
  {
    ULONG flags = 0;
    char *resp = NULL;

    // per default we flag the SMTP to be capable of an ESMTP
    // connection.
    SET_FLAG(flags, SMTP_FLG_ESMTP);

    // set the connection status
    set(G->transferWindowObject, MUIA_TransferWindow_StatusLabel, tr(MSG_TR_SendHello));

    D(DBF_NET, "trying ESMTP negotation");

    // in case we require SMTP-AUTH or a TLS secure connection we
    // have to force an ESMTP connection
    if(hasServerAuth(tfn->msn) || hasServerTLS(tfn->msn))
      resp = TR_SendSMTPCmd(tfn, ESMTP_EHLO, tfn->msn->domain, tr(MSG_ER_BADRESPONSE_SMTP));
    else
    {
      // in all other cases, we first try to get an ESMTP connection
      // and if that doesn't work we go and do a normal SMTP connection
      if((resp = TR_SendSMTPCmd(tfn, ESMTP_EHLO, tfn->msn->domain, NULL)) == NULL)
      {
        D(DBF_NET, "ESMTP negotation failed, trying normal SMTP negotation");

        // according to RFC 1869 section 4.7 we send an RSET command
        // between the two EHLO and HELO commands to play save
        TR_SendSMTPCmd(tfn, SMTP_RSET, NULL, NULL); // no error code check

        // now we send a HELO command which signals we are not
        // going to use any ESMTP stuff
        resp = TR_SendSMTPCmd(tfn, SMTP_HELO, tfn->msn->domain, tr(MSG_ER_BADRESPONSE_SMTP));

        // signal we are not into ESMTP stuff
        CLEAR_FLAG(flags, SMTP_FLG_ESMTP);
      }
    }

    // check the EHLO/HELO answer.
    if(resp != NULL)
    {
      // check the ESMTP flags if this is an
      // ESMTP connection
      if(hasESMTP(flags))
      {
        // Now lets see what features this ESMTP Server really has
        while(resp[3] == '-')
        {
          // now lets iterate to the next line
          resp = strchr(resp, '\n');

          // if we do not find any new line or the next one would be anyway
          // too short we break here.
          if(resp == NULL || strlen(++resp) < 4)
            break;

          // lets see what features this server returns
          if(strnicmp(resp+4, "STARTTLS", 8) == 0)          // STARTTLS (RFC 2487)
            SET_FLAG(flags, SMTP_FLG_STARTTLS);
          else if(strnicmp(resp+4, "AUTH", 4) == 0)         // SMTP-AUTH (RFC 2554)
          {
            if(NULL != strstr(resp+9,"CRAM-MD5"))
              SET_FLAG(flags, SMTP_FLG_AUTH_CRAM_MD5);

            if(NULL != strstr(resp+9,"DIGEST-MD5"))
              SET_FLAG(flags, SMTP_FLG_AUTH_DIGEST_MD5);

            if(NULL != strstr(resp+9,"PLAIN"))
              SET_FLAG(flags, SMTP_FLG_AUTH_PLAIN);

            if(NULL != strstr(resp+9,"LOGIN"))
              SET_FLAG(flags, SMTP_FLG_AUTH_LOGIN);
          }
          else if(strnicmp(resp+4, "SIZE", 4) == 0)         // STD:10 - SIZE declaration (RFC 1870)
            SET_FLAG(flags, SMTP_FLG_SIZE);
          else if(strnicmp(resp+4, "PIPELINING", 10) == 0)  // STD:60 - PIPELINING (RFC 2920)
            SET_FLAG(flags, SMTP_FLG_PIPELINING);
          else if(strnicmp(resp+4, "8BITMIME", 8) == 0)     // 8BITMIME support (RFC 1652)
            SET_FLAG(flags, SMTP_FLG_8BITMIME);
          else if(strnicmp(resp+4, "DSN", 3) == 0)          // DSN - Delivery Status Notifications (RFC 1891)
            SET_FLAG(flags, SMTP_FLG_DSN);
          else if(strnicmp(resp+4, "ETRN", 4) == 0)         // ETRN - Remote Message Queue Starting (RFC 1985)
            SET_FLAG(flags, SMTP_FLG_ETRN);
          else if(strnicmp(resp+4, "ENHANCEDSTATUSCODES", 19) == 0) // Enhanced Status Codes (RFC 2034)
            SET_FLAG(flags, SMTP_FLG_ENHANCEDSTATUSCODES);
          else if(strnicmp(resp+4, "DELIVERBY", 9) == 0)    // DELIVERBY Extension (RFC 2852)
            SET_FLAG(flags, SMTP_FLG_DELIVERBY);
          else if(strnicmp(resp+4, "HELP", 4) == 0)         // HELP Extension (RFC 821)
            SET_FLAG(flags, SMTP_FLG_HELP);
        }
      }

      #ifdef DEBUG
      D(DBF_NET, "SMTP Server '%s' serves:", tfn->msn->hostname);
      D(DBF_NET, "  ESMTP..............: %ld", hasESMTP(flags));
      D(DBF_NET, "  AUTH CRAM-MD5......: %ld", hasCRAM_MD5_Auth(flags));
      D(DBF_NET, "  AUTH DIGEST-MD5....: %ld", hasDIGEST_MD5_Auth(flags));
      D(DBF_NET, "  AUTH LOGIN.........: %ld", hasLOGIN_Auth(flags));
      D(DBF_NET, "  AUTH PLAIN.........: %ld", hasPLAIN_Auth(flags));
      D(DBF_NET, "  STARTTLS...........: %ld", hasSTARTTLS(flags));
      D(DBF_NET, "  SIZE...............: %ld", hasSIZE(flags));
      D(DBF_NET, "  PIPELINING.........: %ld", hasPIPELINING(flags));
      D(DBF_NET, "  8BITMIME...........: %ld", has8BITMIME(flags));
      D(DBF_NET, "  DSN................: %ld", hasDSN(flags));
      D(DBF_NET, "  ETRN...............: %ld", hasETRN(flags));
      D(DBF_NET, "  ENHANCEDSTATUSCODES: %ld", hasENHANCEDSTATUSCODES(flags));
      D(DBF_NET, "  DELIVERBY..........: %ld", hasDELIVERBY(flags));
      D(DBF_NET, "  HELP...............: %ld", hasHELP(flags));
      #endif

      // now we check the 8BITMIME extension against
      // the user configured Allow8bit setting and if it collides
      // we raise a warning.
      if(has8BITMIME(flags) == FALSE && hasServer8bit(tfn->msn) == TRUE)
        result = FALSE;
    }
    else
    {
      result = FALSE;

      W(DBF_NET, "error on SMTP server negotation");
    }

    tfn->smtpFlags = flags;
  }
  else
    W(DBF_NET, "SMTP connection failure!");

  RETURN(result);
  return result;
}
///
/// TR_AbortnClose
//  Aborts a transfer
static void TR_AbortnClose(struct TransferNode *tfn)
{
  ENTER();

  set(G->transferWindowObject, MUIA_Window_Open, FALSE);
  TR_Cleanup(tfn);
  MA_ChangeTransfer(TRUE);

  #warning "TODO: Who disposes the transferWindowObject???"
//  DisposeModulePush(&G->TR);

  LEAVE();
}
///
/// TR_ApplySentFilters
//  Applies filters to a sent message
static BOOL TR_ApplySentFilters(struct MailTransferNode *mtn)
{
  BOOL result = TRUE;

  ENTER();

  // only if we have a positiv search count we start
  // our filtering at all, otherwise we return immediatly
  if(mtn->tfn->processFilters == TRUE)
  {
    struct Node *curNode;

    // Now we process the read header to set all flags accordingly
    IterateList(&C->filterList, curNode)
    {
      struct FilterNode *filter = (struct FilterNode *)curNode;

      if(DoFilterSearch(filter, mtn->mail) == TRUE)
      {
        if(ExecuteFilterAction(filter, mtn->mail) == FALSE)
        {
          result = FALSE;
          break;
        }
      }
    }
  }

  RETURN(result);
  return result;
}
///
/// TR_InitSMTPAUTH()
// function to authenticate to a ESMTP Server
static BOOL TR_InitSMTPAUTH(struct TransferNode *tfn)
{
  int rc = SMTP_SERVICE_NOT_AVAILABLE;
  char *resp;
  char buffer[SIZE_LINE];
  char challenge[SIZE_LINE];
  int selectedMethod = MSF_AUTH_AUTO;

  ENTER();

  set(G->transferWindowObject, MUIA_TransferWindow_StatusLabel, tr(MSG_TR_SENDAUTH));

  // first we check if the user has supplied the User&Password
  // and if not we return with an error
  if(tfn->msn->username[0] == '\0' || tfn->msn->password[0] == '\0')
  {
    ER_NewError(tr(MSG_ER_NOAUTHUSERPASS));

    RETURN(FALSE);
    return FALSE;
  }

  // now we find out which of the SMTP-AUTH methods we process and which to skip
  // the user explicitly set an auth method. However, we have to
  // check wheter the SMTP server told us that it really
  // supports that method or not
  if(hasServerAuth_AUTO(tfn->msn))
  {
    // we select the most secure one the server supports
    if(hasDIGEST_MD5_Auth(tfn->smtpFlags))
      selectedMethod = MSF_AUTH_DIGEST;
    else if(hasCRAM_MD5_Auth(tfn->smtpFlags))
      selectedMethod = MSF_AUTH_CRAM;
    else if(hasLOGIN_Auth(tfn->smtpFlags))
      selectedMethod = MSF_AUTH_LOGIN;
    else if(hasPLAIN_Auth(tfn->smtpFlags))
      selectedMethod = MSF_AUTH_PLAIN;
  }
  else if(hasServerAuth_DIGEST(tfn->msn))
  {
    if(hasDIGEST_MD5_Auth(tfn->smtpFlags))
      selectedMethod = MSF_AUTH_DIGEST;
    else
      W(DBF_NET, "User selected SMTP-Auth 'DIGEST-MD5', but server doesn't support it!");
  }
  else if(hasServerAuth_CRAM(tfn->msn))
  {
    if(hasCRAM_MD5_Auth(tfn->smtpFlags))
      selectedMethod = MSF_AUTH_CRAM;
    else
      W(DBF_NET, "User selected SMTP-Auth 'CRAM-MD5', but server doesn't support it!");
  }
  else if(hasServerAuth_LOGIN(tfn->msn))
  {
    if(hasLOGIN_Auth(tfn->smtpFlags))
      selectedMethod = MSF_AUTH_LOGIN;
    else
      W(DBF_NET, "User selected SMTP-Auth 'LOGIN', but server doesn't support it!");
  }
  else if(hasServerAuth_PLAIN(tfn->msn))
  {
    if(hasPLAIN_Auth(tfn->smtpFlags))
      selectedMethod = MSF_AUTH_PLAIN;
    else
      W(DBF_NET, "User selected SMTP-Auth 'PLAIN', but server doesn't support it!");
  }

  // now we process the SMTP Authentication by choosing the method the user
  // or the automatic did specify
  switch(selectedMethod)
  {
    // SMTP AUTH DIGEST-MD5 (RFC 2831)
    case MSF_AUTH_DIGEST:
    {
      D(DBF_NET, "processing AUTH DIGEST-MD5:");

      // send the AUTH command and get the response back
      if((resp = TR_SendSMTPCmd(tfn, ESMTP_AUTH_DIGEST_MD5, NULL, tr(MSG_ER_BADRESPONSE_SMTP))) != NULL)
      {
        char *realm = NULL;
        char *nonce = NULL;
        char cnonce[16+1];
        char response[32+1];
        char *chalRet;

        // get the challenge code from the response line of the
        // AUTH command.
        strlcpy(challenge, &resp[4], sizeof(challenge));

        // now that we have the challenge phrase we need to base64decode
        // it, but have to take care to remove the ending "\r\n" cookie.
        chalRet = strpbrk(challenge, "\r\n"); // find the first CR or LF
        if(chalRet)
          *chalRet = '\0'; // strip it

        D(DBF_NET, "received DIGEST-MD5 challenge: '%s'", challenge);

        // lets base64 decode it
        if(base64decode(challenge, (unsigned char *)challenge, strlen(challenge)) <= 0)
        {
          RETURN(FALSE);
          return FALSE;
        }

        D(DBF_NET, "decoded  DIGEST-MD5 challenge: '%s'", challenge);

        // we now analyze the received challenge identifier and pick out
        // the value which we are going to need for our challenge response.
        // This is the refered STEP ONE in the RFC 2831
        {
          char *pstart;
          char *pend;

          // first we try to find out the "realm" of the challenge
          if((pstart = strstr(challenge, "realm=")))
          {
            // iterate to the beginning of the realm
            pstart += 6;

            // skip a leading "
            if(*pstart == '"')
              pstart++;

            // find the end of the string
            pend = strpbrk(pstart, "\","); // find a ending " or ,
            if(!pend)
              pend = pstart + strlen(pstart);

            // now copy the found realm into our realm string
            realm = malloc((pend-pstart)+1);
            if(realm)
              strlcpy(realm, pstart, (pend-pstart)+1);
            else
            {
              RETURN(FALSE);
              return FALSE;
            }
          }
          else
          {
            W(DBF_NET, "'realm' not found in challenge, using '%s' instead", tfn->msn->domain);

            // if the challenge doesn't have a "realm" we assume our
            // choosen SMTP domain to be the realm
            realm = strdup(tfn->msn->domain);
          }

          D(DBF_NET, "realm: '%s'", realm);

          // grab the "nonce" token for later reference
          if((pstart = strstr(challenge, "nonce=")))
          {
            // iterate to the beginning of the nonce
            pstart += 6;

            // skip a leading "
            if(*pstart == '"')
              pstart++;

            // find the end of the string
            pend = strpbrk(pstart, "\","); // find a ending " or ,
            if(!pend)
              pend = pstart + strlen(pstart);

            // now copy the found nonce into our nonce string
            nonce = malloc((pend-pstart)+1);
            if(nonce)
              strlcpy(nonce, pstart, (pend-pstart)+1);
            else
            {
              free(realm);

              RETURN(FALSE);
              return FALSE;
            }
          }
          else
          {
            E(DBF_NET, "no 'nonce=' token found!");

            free(realm);

            RETURN(FALSE);
            return FALSE;
          }

          D(DBF_NET, "nonce: '%s'", nonce);

          // now we check the "qop" to carry "auth" so that we are
          // sure that this server really wants an authentification from us
          // RFC 2831 says that it is OK if no qop is present, because this
          // assumes the server to support at least "auth"
          if((pstart = strstr(challenge, "qop=")))
          {
            char *qop;

            // iterate to the beginning of the qop=
            pstart += 4;

            // skip a leading "
            if(*pstart == '"')
              pstart++;

            // find the end of the string
            pend = strpbrk(pstart, "\","); // find a ending " or ,
            if(!pend)
              pend = pstart + strlen(pstart);

            // now copy the found qop into our qop string
            qop = malloc((pend-pstart)+1);
            if(qop)
              strlcpy(qop, pstart, (pend-pstart)+1);
            else
            {
              free(realm);
              free(nonce);

              RETURN(FALSE);
              return FALSE;
            }

            // then we check whether we have a plain "auth" within
            // qop or not
            pstart = qop;
            while((pstart = strstr(qop+(pstart-qop), "auth")))
            {
              if(*(pstart+1) != '-')
                break;
            }

            // we don't need the qop string anymore
            free(qop);

            // check if we found a plain auth
            if(!pstart)
            {
              E(DBF_NET, "no 'auth' in 'qop' token found!");

              free(realm);
              free(nonce);

              RETURN(FALSE);
              return FALSE;
            }
          }
        }

        // if we passed here, the server seems to at least support all
        // mechanisms we need for a proper DIGEST-MD5 authentication.
        // so it's time for STEP TWO

        // let us now generate a more or less random and unique cnonce
        // identifier which we can supply to our SMTP server.
        snprintf(cnonce, sizeof(cnonce), "%08x%08x", (unsigned int)rand(), (unsigned int)rand());

        // the we generate the response according to RFC 2831 with A1
        // and A2 as MD5 encoded strings
        {
          unsigned char digest[16]; // 16 octets
          ULONG digest_hex[4];      // 16 octets
          struct MD5Context context;
          char buf[SIZE_LARGE];
          char A1[32+1];
          int  A1_len = 16;         // 16 octects minimum
          char A2[32+1];

          // lets first generate the A1 string
          // A1 = { H( { username-value, ":", realm-value, ":", passwd } ),
          //      ":", nonce-value, ":", cnonce-value }
          snprintf(buf, sizeof(buf), "%s:%s:%s", tfn->msn->username, realm, tfn->msn->password);
          md5init(&context);
          md5update(&context, (unsigned char *)buf, strlen(buf));
          md5final(digest, &context);
          memcpy(buf, digest, 16);
          A1_len += snprintf(&buf[16], sizeof(buf)-16, ":%s:%s", nonce, cnonce);
          D(DBF_NET, "unencoded A1: '%s' (%ld)", buf, A1_len);

          // then we directly build the hexadecimal representation
          // HEX(H(A1))
          md5init(&context);
          md5update(&context, (unsigned char *)buf, A1_len);
          md5final((UBYTE *)digest_hex, &context);
          snprintf(A1, sizeof(A1), "%08x%08x%08x%08x", (unsigned int)digest_hex[0], (unsigned int)digest_hex[1],
                                                       (unsigned int)digest_hex[2], (unsigned int)digest_hex[3]);
          D(DBF_NET, "encoded   A1: '%s'", A1);


          // then we generate the A2 string accordingly
          // A2 = { "AUTHENTICATE:", digest-uri-value }
          snprintf(buf, sizeof(buf), "AUTHENTICATE:smtp/%s", realm);
          D(DBF_NET, "unencoded A2: '%s'", buf);

          // and also directly build the hexadecimal representation
          // HEX(H(A2))
          md5init(&context);
          md5update(&context, (unsigned char *)buf, strlen(buf));
          md5final((UBYTE *)digest_hex, &context);
          snprintf(A2, sizeof(A2), "%08x%08x%08x%08x", (unsigned int)digest_hex[0], (unsigned int)digest_hex[1],
                                                       (unsigned int)digest_hex[2], (unsigned int)digest_hex[3]);
          D(DBF_NET, "encoded   A2: '%s'", A2);

          // now we build the string from which we also build the MD5
          // HEX(H(A1)), ":",
          // nonce-value, ":", nc-value, ":",
          // cnonce-value, ":", qop-value, ":", HEX(H(A2))
          snprintf(buf, sizeof(buf), "%s:%s:00000001:%s:auth:%s", A1, nonce, cnonce, A2);
          D(DBF_NET, "unencoded resp: '%s'", buf);

          // and finally build the respone-value =
          // HEX( KD( HEX(H(A1)), ":",
          //          nonce-value, ":", nc-value, ":",
          //          cnonce-value, ":", qop-value, ":", HEX(H(A2)) }))
          md5init(&context);
          md5update(&context, (unsigned char *)buf, strlen(buf));
          md5final((UBYTE *)digest_hex, &context);
          snprintf(response, sizeof(response), "%08x%08x%08x%08x", (unsigned int)digest_hex[0], (unsigned int)digest_hex[1],
                                                                   (unsigned int)digest_hex[2], (unsigned int)digest_hex[3]);
          D(DBF_NET, "encoded   resp: '%s'", response);
        }

        // form up the challenge to authenticate according to RFC 2831
        snprintf(challenge, sizeof(challenge),
                 "username=\"%s\","        // the username token
                 "realm=\"%s\","           // the realm token
                 "nonce=\"%s\","           // the nonce token
                 "cnonce=\"%s\","          // the client nonce (cnonce)
                 "nc=00000001,"            // the nonce count (here always 1)
                 "qop=\"auth\","           // we just use auth
                 "digest-uri=\"smtp/%s\"," // the digest-uri token
                 "response=\"%s\"",        // the response
                 tfn->msn->username,
                 realm,
                 nonce,
                 cnonce,
                 realm,
                 response);

        D(DBF_NET, "prepared challenge answer....: '%s'", challenge);
        base64encode(buffer, (unsigned char *)challenge, strlen(challenge));
        D(DBF_NET, "encoded  challenge answer....: '%s'", buffer);
        strlcat(buffer, "\r\n", sizeof(buffer));

        // now we send the SMTP AUTH response
        if(TR_WriteLine(tfn, buffer) > 0)
        {
          // get the server response and see if it was valid
          if(TR_ReadLine(tfn, buffer, SIZE_LINE) <= 0 || (rc = getResponseCode(buffer)) != 334)
            ER_NewError(tr(MSG_ER_BADRESPONSE_SMTP), tfn->msn->hostname, (char *)SMTPcmd[ESMTP_AUTH_DIGEST_MD5], buffer);
          else
          {
            // now that we have received the 334 code we just send a plain line
            // to signal that we don't need any option
            if(TR_WriteLine(tfn, "\r\n") > 0)
            {
              if(TR_ReadLine(tfn, buffer, SIZE_LINE) <= 0 || (rc = getResponseCode(buffer)) != 235)
                ER_NewError(tr(MSG_ER_BADRESPONSE_SMTP), tfn->msn->hostname, (char *)SMTPcmd[ESMTP_AUTH_DIGEST_MD5], buffer);
              else
                rc = SMTP_ACTION_OK;
            }
            else
              E(DBF_NET, "couldn't write empty line...");
          }
        }
        else
          E(DBF_NET, "couldn't write empty line...");

        // free all our dynamic buffers
        free(realm);
        free(nonce);
      }
    }
    break;

    // SMTP AUTH CRAM-MD5 (RFC 2195)
    case MSF_AUTH_CRAM:
    {
      D(DBF_NET, "processing AUTH CRAM-MD5:");

      // send the AUTH command and get the response back
      if((resp = TR_SendSMTPCmd(tfn, ESMTP_AUTH_CRAM_MD5, NULL, tr(MSG_ER_BADRESPONSE_SMTP))) != NULL)
      {
        ULONG digest[4]; // 16 chars
        char buf[512];
        char *login = tfn->msn->username;
        char *password = tfn->msn->password;
        char *chalRet;

        // get the challenge code from the response line of the
        // AUTH command.
        strlcpy(challenge, &resp[4], sizeof(challenge));

        // now that we have the challenge phrase we need to base64decode
        // it, but have to take care to remove the ending "\r\n" cookie.
        chalRet = strpbrk(challenge, "\r\n"); // find the first CR or LF
        if(chalRet)
          *chalRet = '\0'; // strip it

        D(DBF_NET, "received CRAM-MD5 challenge: '%s'", challenge);

        // lets base64 decode it
        if(base64decode(challenge, (unsigned char *)challenge, strlen(challenge)) <= 0)
        {
          RETURN(FALSE);
          return FALSE;
        }

        D(DBF_NET, "decoded  CRAM-MD5 challenge: '%s'", challenge);

        // compose the md5 challenge
        md5hmac((unsigned char *)challenge, strlen(challenge), (unsigned char *)password, strlen(password), (unsigned char *)digest);
        snprintf(buf, sizeof(buf), "%s %08x%08x%08x%08x", login, (unsigned int)digest[0], (unsigned int)digest[1],
                                                                 (unsigned int)digest[2], (unsigned int)digest[3]);

        D(DBF_NET, "prepared CRAM-MD5 reponse..: '%s'", buf);
        // lets base64 encode the md5 challenge for the answer
        base64encode(buffer, (unsigned char *)buf, strlen(buf));
        D(DBF_NET, "encoded  CRAM-MD5 reponse..: '%s'", buffer);
        strlcat(buffer, "\r\n", sizeof(buffer));

        // now we send the SMTP AUTH response
        if(TR_WriteLine(tfn, buffer) > 0)
        {
          // get the server response and see if it was valid
          if(TR_ReadLine(tfn, buffer, SIZE_LINE) <= 0 || (rc = getResponseCode(buffer)) != 235)
            ER_NewError(tr(MSG_ER_BADRESPONSE_SMTP), tfn->msn->hostname, (char *)SMTPcmd[ESMTP_AUTH_CRAM_MD5], buffer);
          else
            rc = SMTP_ACTION_OK;
        }
      }
    }
    break;

    // SMTP AUTH LOGIN
    case MSF_AUTH_LOGIN:
    {
      D(DBF_NET, "processing AUTH LOGIN:");

      // send the AUTH command
      if((resp = TR_SendSMTPCmd(tfn, ESMTP_AUTH_LOGIN, NULL, tr(MSG_ER_BADRESPONSE_SMTP))) != NULL)
      {
        // prepare the username challenge
        D(DBF_NET, "prepared AUTH LOGIN challenge: '%s'", tfn->msn->username);
        base64encode(buffer, (unsigned char *)tfn->msn->username, strlen(tfn->msn->username));
        D(DBF_NET, "encoded  AUTH LOGIN challenge: '%s'", buffer);
        strlcat(buffer, "\r\n", sizeof(buffer));

        // now we send the SMTP AUTH response (UserName)
        if(TR_WriteLine(tfn, buffer) > 0)
        {
          // get the server response and see if it was valid
          if(TR_ReadLine(tfn, buffer, SIZE_LINE) > 0
             && (rc = getResponseCode(buffer)) == 334)
          {
            // prepare the password challenge
            D(DBF_NET, "prepared AUTH LOGIN challenge: '%s'", tfn->msn->password);
            base64encode(buffer, (unsigned char *)tfn->msn->password, strlen(tfn->msn->password));
            D(DBF_NET, "encoded  AUTH LOGIN challenge: '%s'", buffer);
            strlcat(buffer, "\r\n", sizeof(buffer));

            // now lets send the Password
            if(TR_WriteLine(tfn, buffer) > 0)
            {
              // get the server response and see if it was valid
              if(TR_ReadLine(tfn, buffer, SIZE_LINE) > 0
                 && (rc = getResponseCode(buffer)) == 235)
              {
                rc = SMTP_ACTION_OK;
              }
            }
          }

          if(rc != SMTP_ACTION_OK)
            ER_NewError(tr(MSG_ER_BADRESPONSE_SMTP), tfn->msn->hostname, (char *)SMTPcmd[ESMTP_AUTH_LOGIN], buffer);
        }
      }
    }
    break;

    // SMTP AUTH PLAIN (RFC 2595)
    case MSF_AUTH_PLAIN:
    {
      int len=0;
      D(DBF_NET, "processing AUTH PLAIN:");

      // The AUTH PLAIN command string is a single command string, so we go
      // and prepare the challenge first
      // According to RFC 2595 this string consists of three parts:
      // "[authorize-id] \0 authenticate-id \0 password"
      // where we can left out the first one

      // we don't have a "authorize-id" so we set the first char to \0
      challenge[len++] = '\0';
      len += snprintf(challenge+len, sizeof(challenge)-len, "%s", tfn->msn->username)+1; // authenticate-id
      len += snprintf(challenge+len, sizeof(challenge)-len, "%s", tfn->msn->password);   // password

      // now we base64 encode this string and send it to the server
      base64encode(buffer, (unsigned char *)challenge, len);

      // lets now form up the AUTH PLAIN command we are going to send
      // to the SMTP server for authorization purposes:
      snprintf(challenge, sizeof(challenge), "%s %s\r\n", SMTPcmd[ESMTP_AUTH_PLAIN], buffer);

      // now we send the SMTP AUTH command (UserName+Password)
      if(TR_WriteLine(tfn, challenge) > 0)
      {
        // get the server response and see if it was valid
        if(TR_ReadLine(tfn, buffer, SIZE_LINE) <= 0 || (rc = getResponseCode(buffer)) != 235)
          ER_NewError(tr(MSG_ER_BADRESPONSE_SMTP), tfn->msn->hostname, (char *)SMTPcmd[ESMTP_AUTH_PLAIN], buffer);
        else
          rc = SMTP_ACTION_OK;
      }
    }
    break;

    default:
    {
      W(DBF_NET, "The SMTP server seems not to support any of the selected or automatic specified SMTP-AUTH methods");

      // if we don't have any of the Authentication Flags turned on we have to
      // exit with an error
      ER_NewError(tr(MSG_CO_ER_SMTPAUTH), tfn->msn->hostname);
    }
    break;
  }

  D(DBF_NET, "Server responded with %ld", rc);

  RETURN((BOOL)(rc == SMTP_ACTION_OK));
  return (BOOL)(rc == SMTP_ACTION_OK);
}
///
/// TR_InitSTARTTLS()
// function to initiate a TLS connection to the ESMTP server via STARTTLS
static BOOL TR_InitSTARTTLS(struct TransferNode *tfn)
{
  BOOL result = FALSE;

  ENTER();

  // If this server doesn't support TLS at all we return with an error
  if(hasSTARTTLS(tfn->smtpFlags))
  {
    // If we end up here the server supports STARTTLS and we can start
    // initializing the connection
    set(G->transferWindowObject, MUIA_TransferWindow_StatusLabel, tr(MSG_TR_INITTLS));

    // Now we initiate the STARTTLS command (RFC 2487)
    if(TR_SendSMTPCmd(tfn, ESMTP_STARTTLS, NULL, tr(MSG_ER_BADRESPONSE_SMTP)) != NULL)
    {
      // setup the TLS/SSL session
      if(TR_InitTLS(tfn) == TRUE && TR_StartTLS(tfn) == TRUE)
      {
        tfn->useSSL = TRUE;
        result = TRUE;
      }
      else
        ER_NewError(tr(MSG_ER_INITTLS_SMTP), tfn->msn->hostname);
    }
  }
  else
    ER_NewError(tr(MSG_ER_NOSTARTTLS_SMTP), tfn->msn->hostname);

  RETURN(result);
  return result;
}

///

/*** EXPORT ***/
/// TR_ProcessEXPORT
//  Saves a list of messages to a MBOX mailbox file
BOOL TR_ProcessEXPORT(char *fname, struct MailList *mlist, BOOL append)
{
  BOOL success = FALSE;
  struct TransferNode *tfn;

  ENTER();

  // for allowing to process the export operation through our
  // transfer functions we go and create a TransferNode
  if((tfn = CreateNewTransfer(NULL)) != NULL)
  {
    BOOL abort = FALSE;
    struct MailNode *mnode;
    int i;

    // reset our processing list
    NewList((struct List *)&tfn->mailTransferList);

    // temporarly copy all data out of our mlist to the
    // processing list and mark all mails to get "loaded"
    i = 0;

    LockMailListShared(mlist);

    ForEachMailNode(mlist, mnode)
    {
      struct Mail *mail = mnode->mail;

      if(mail != NULL)
      {
        struct MailTransferNode *mtn;

        if((mtn = calloc(1, sizeof(struct MailTransferNode))) != NULL)
        {
          if((mtn->mail = memdup(mail, sizeof(struct Mail))) != NULL)
          {
            mtn->index = i + 1;

            // set to LOAD
            mtn->tflags = TRF_LOAD;

            AddTail((struct List *)&tfn->mailTransferList, (struct Node *)mtn);
          }
          else
          {
            // we end up in a low memory condition, lets exit
            // after having freed everything
            free(mtn);
            abort = TRUE;
            break;
          }
        }
        else
        {
          // we end up in a low memory condition, lets exit
          abort = TRUE;
          break;
        }
      }

      i++;
    }

    UnlockMailList(mlist);

    // if we have now something in our processing list,
    // lets go on
    if(abort == FALSE &&
       IsMinListEmpty(&tfn->mailTransferList) == FALSE)
    {
      FILE *fh;
      struct TransStat ts;

      xset(G->transferWindowObject, MUIA_TransferWindow_Title, FilePart(fname));
      DoMethod(G->transferWindowObject, MUIM_TransferWindow_TransStat_Init);
      DoMethod(G->transferWindowObject, MUIM_TransferWindow_TransStat_Start);

      // open our final destination file either in append or in a fresh
      // write mode.
      if((fh = fopen(fname, append ? "a" : "w")) != NULL)
      {
        struct Node *curNode;

        setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);

        success = TRUE;

        IterateList(&tfn->mailTransferList, curNode)
        {
          struct MailTransferNode *mtn = (struct MailTransferNode *)curNode;
          struct Mail *mail = mtn->mail;
          char fullfile[SIZE_PATHFILE];

          // update the transfer status
          DoMethod(G->transferWindowObject, MUIM_TransferWindow_TransStat_NextMsg, mtn->index, -1, mail->Size, tr(MSG_TR_Exporting));

          if(StartUnpack(GetMailFile(NULL, NULL, mail), fullfile, mail->Folder) != NULL)
          {
            FILE *mfh;

            // open the message file to start exporting it
            if((mfh = fopen(fullfile, "r")) != NULL)
            {
              char datstr[64];
              char *buf = NULL;
              size_t buflen = 0;
              ssize_t curlen;
              BOOL inHeader = TRUE;

              // printf out our leading "From " MBOX format line first
              DateStamp2String(datstr, sizeof(datstr), &mail->Date, DSS_UNIXDATE, TZC_NONE);
              fprintf(fh, "From %s %s", mail->From.Address, datstr);

              // let us put out the Status: header field
              fprintf(fh, "Status: %s\n", MA_ToStatusHeader(mail));

              // let us put out the X-Status: header field
              fprintf(fh, "X-Status: %s\n", MA_ToXStatusHeader(mail));

              // now we iterate through every line of our mail and try to substitute
              // found "From " line with quoted ones
              while(tfn->abort == FALSE &&
                    (curlen = getline(&buf, &buflen, mfh)) > 0)
              {
                char *tmp = buf;

                // check if this is a single \n so that it
                // signals the end if a line
                if(buf[0] == '\n' || (buf[0] == '\r' && buf[1] == '\n'))
                {
                  inHeader = FALSE;

                  if(fwrite(buf, curlen, 1, fh) != 1)
                  {
                    // write error, bail out
                    break;
                  }

                  continue;
                }

                // the mboxrd format specifies that we need to quote any
                // From, >From, >>From etc. occurance.
                // http://www.qmail.org/man/man5/mbox.html
                while(*tmp == '>')
                  tmp++;

                if(strncmp(tmp, "From ", 5) == 0)
                {
                  if(fputc('>', fh) == EOF)
                  {
                    // write error, bail out
                    break;
                  }
                }
                else if(inHeader == TRUE)
                {
                  // let us skip some specific headerlines
                  // because we placed our own here
                  if(strncmp(buf, "Status: ", 8) == 0 ||
                     strncmp(buf, "X-Status: ", 10) == 0)
                  {
                    // skip line
                    continue;
                  }
                }

                // write the line to our destination file
                if(fwrite(buf, curlen, 1, fh) != 1)
                {
                  // write error, bail out
                  break;
                }

                // make sure we have a newline at the end of the line
                if(buf[curlen-1] != '\n')
                {
                  if(fputc('\n', fh) == EOF)
                  {
                    // write error, bail out
                    break;
                  }
                }

                // update the transfer status
                DoMethod(G->transferWindowObject, MUIM_TransferWindow_TransStat_Update, curlen, tr(MSG_TR_Exporting));
              }

              // check why we exited the while() loop and if everything is fine
              if(ferror(fh) != 0)
              {
                E(DBF_NET, "error on writing data! ferror(fh)=%ld", ferror(fh));

                // an error occurred, lets return failure
                success = FALSE;
              }
              else if(ferror(mfh) != 0 || feof(mfh) == 0)
              {
                E(DBF_NET, "error on reading data! ferror(mfh)=%ld feof(mfh)=%ld", ferror(mfh), feof(mfh));

                // an error occurred, lets return failure
                success = FALSE;
              }

              // close file pointer
              fclose(mfh);

              if(buf != NULL)
                free(buf);

              // put the transferStat to 100%
              DoMethod(G->transferWindowObject, MUIM_TransferWindow_TransStat_Update, TS_SETMAX, tr(MSG_TR_Exporting));
            }
            else
             success = FALSE;

            FinishUnpack(fullfile);
          }
          else
            success = FALSE;

          if(tfn->abort == TRUE || success == FALSE)
            break;
        }

        // close file pointer
        fclose(fh);

        // write the status to our logfile
        mnode = FirstMailNode(mlist);
        #warning "TODO:reenable AppendToLog"
        //AppendToLogfile(LF_ALL, 51, tr(MSG_LOG_Exporting), ts.Msgs_Done, mnode->mail->Folder->Name, fname);
      }

      DoMethod(G->transferWindowObject, MUIM_TransferWindow_TransStat_Finish);
    }

    TR_AbortnClose(tfn);

    free(tfn->msn);
    free(tfn);
  }

  RETURN(success);
  return success;
}
///

/*** SEND ***/
/// TR_SendMessage
// Sends a single message (-1 signals an error in DATA phase, 0 signals
// error in start phase, 1 and 2 signals success)
static int TR_SendMessage(struct MailTransferNode *mtn)
{
  int result = 0;
  struct Folder *outfolder = FO_GetFolderByType(FT_OUTGOING, NULL);
  char *mf = GetMailFile(NULL, outfolder, mtn->mail);
  FILE *fh = NULL;
  char *buf = NULL;
  size_t buflen = SIZE_LINE;

  ENTER();

  D(DBF_NET, "about to send mail '%s' via SMTP", mf);

  // open the mail file for reading
  if((buf = malloc(buflen)) != NULL &&
     mf != NULL && (fh = fopen(mf, "r")) != NULL)
  {
    setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);

    // now we put together our parameters for our MAIL command
    // which in fact may contain serveral parameters as well according
    // to ESMTP extensions.
    snprintf(buf, buflen, "FROM:<%s>", C->EmailAddress);

    // in case the server supports the ESMTP SIZE extension lets add the
    // size
    if(hasSIZE(mtn->tfn->smtpFlags) && mtn->mail->Size > 0)
      snprintf(buf, buflen, "%s SIZE=%ld", buf, mtn->mail->Size);

    // in case the server supports the ESMTP 8BITMIME extension we can
    // add information about the encoding mode
    if(has8BITMIME(mtn->tfn->smtpFlags))
      snprintf(buf, buflen, "%s BODY=%s", buf, hasServer8bit(mtn->tfn->msn) ? "8BITMIME" : "7BIT");

    // send the MAIL command with the FROM: message
    if(TR_SendSMTPCmd(mtn->tfn, SMTP_MAIL, buf, tr(MSG_ER_BADRESPONSE_SMTP)) != NULL)
    {
      struct ExtendedMail *email = MA_ExamineMail(outfolder, mtn->mail->MailFile, TRUE);

      if(email != NULL)
      {
        BOOL rcptok = TRUE;
        int j;

        // if the mail in question has some "Resent-To:" mail
        // header information we use that information instead
        // of the one of the original mail
        if(email->NoResentTo > 0)
        {
          for(j=0; j < email->NoResentTo; j++)
          {
            snprintf(buf, buflen, "TO:<%s>", email->ResentTo[j].Address);
            if(TR_SendSMTPCmd(mtn->tfn, SMTP_RCPT, buf, tr(MSG_ER_BADRESPONSE_SMTP)) == NULL)
              rcptok = FALSE;
          }
        }
        else
        {
          // specify the main 'To:' recipient
          snprintf(buf, buflen, "TO:<%s>", mtn->mail->To.Address);
          if(TR_SendSMTPCmd(mtn->tfn, SMTP_RCPT, buf, tr(MSG_ER_BADRESPONSE_SMTP)) == NULL)
            rcptok = FALSE;

          // now add the additional 'To:' recipients of the mail
          for(j=0; j < email->NoSTo && rcptok; j++)
          {
            snprintf(buf, buflen, "TO:<%s>", email->STo[j].Address);
            if(TR_SendSMTPCmd(mtn->tfn, SMTP_RCPT, buf, tr(MSG_ER_BADRESPONSE_SMTP)) == NULL)
              rcptok = FALSE;
          }

          // add the 'Cc:' recipients
          for(j=0; j < email->NoCC && rcptok; j++)
          {
            snprintf(buf, buflen, "TO:<%s>", email->CC[j].Address);
            if(TR_SendSMTPCmd(mtn->tfn, SMTP_RCPT, buf, tr(MSG_ER_BADRESPONSE_SMTP)) == NULL)
              rcptok = FALSE;
          }

          // add the 'BCC:' recipients
          for(j=0; j < email->NoBCC && rcptok; j++)
          {
            snprintf(buf, buflen, "TO:<%s>", email->BCC[j].Address);
            if(TR_SendSMTPCmd(mtn->tfn, SMTP_RCPT, buf, tr(MSG_ER_BADRESPONSE_SMTP)) == NULL)
              rcptok = FALSE;
          }
        }

        if(rcptok == TRUE)
        {
          D(DBF_NET, "RCPTs accepted, sending mail data");

          // now we send the actual main data of the mail
          if(TR_SendSMTPCmd(mtn->tfn, SMTP_DATA, NULL, tr(MSG_ER_BADRESPONSE_SMTP)) != NULL)
          {
            BOOL lineskip = FALSE;
            BOOL inbody = FALSE;
            ssize_t curlen;
            ssize_t proclen = 0;
            size_t sentbytes = 0;

            // as long there is no abort situation we go on reading out
            // from the stream and sending it to our SMTP server
            while(mtn->tfn->abort == FALSE && G->Error == FALSE &&
                  (curlen = getline(&buf, &buflen, fh)) > 0)
            {
              #if defined(DEBUG)
              if(curlen > 998)
                W(DBF_NET, "RFC2822 violation: line length in source file is too large: %ld", curlen);
              #endif

              // as long as we process header lines we have to make differ in some ways.
              if(inbody == FALSE)
              {
                // we check if we found the body of the mail now
                // the start of a body is seperated by the header with a single
                // empty line and we have to make sure that it isn't the beginning of the file
                if(curlen == 1 && buf[0] == '\n' && proclen > 0)
                {
                  inbody = TRUE;
                  lineskip = FALSE;
                }
                else if(isspace(*buf) == FALSE) // headerlines don't start with a space
                {
                  // headerlines with bcc or x-yam- will be skipped by us.
                  lineskip = (strnicmp(buf, "bcc", 3) == 0 || strnicmp(buf, "x-yam-", 6) == 0);
                }
              }

              // lets save the length we have processed already
              proclen = curlen;

              // if we don't skip this line we write it out to the SMTP server
              if(lineskip == FALSE)
              {
                // RFC 821 says a starting period needs a second one
                // so we send out a period in advance
                if(buf[0] == '.')
                {
                  if(TR_Send(mtn->tfn, ".", 1, TCPF_NONE) <= 0)
                  {
                    E(DBF_NET, "couldn't send single '.' to SMTP server");

                    ER_NewError(tr(MSG_ER_CONNECTIONBROKEN), mtn->tfn->msn->hostname, (char *)SMTPcmd[SMTP_DATA]);
                    break;
                  }
                  else
                    sentbytes++;
                }

                // if the last char is a LF we have to skip it right now
                // as we have to send a CRLF per definition of the RFC
                if(buf[curlen-1] == '\n')
                  curlen--;

                // now lets send the data buffered to the socket.
                // we will flush it later then.
                if(curlen > 0 && TR_Send(mtn->tfn, buf, curlen, TCPF_NONE) <= 0)
                {
                  E(DBF_NET, "couldn't send buffer data to SMTP server (%ld)", curlen);

                  ER_NewError(tr(MSG_ER_CONNECTIONBROKEN), mtn->tfn->msn->hostname, (char *)SMTPcmd[SMTP_DATA]);
                  break;
                }
                else
                  sentbytes += curlen;

                // now we send the final CRLF (RFC 2822)
                if(TR_Send(mtn->tfn, "\r\n", 2, TCPF_NONE) <= 0)
                {
                  E(DBF_NET, "couldn't send CRLF to SMTP server");

                  ER_NewError(tr(MSG_ER_CONNECTIONBROKEN), mtn->tfn->msn->hostname, (char *)SMTPcmd[SMTP_DATA]);
                  break;
                }
                else
                  sentbytes += 2;
              }

              DoMethod(G->transferWindowObject, MUIM_TransferWindow_TransStat_Update, proclen, tr(MSG_TR_Sending));
            }

            D(DBF_NET, "transfered %ld bytes (raw: %ld bytes) error: %ld/%ld", sentbytes, mtn->mail->Size, mtn->tfn->abort, G->Error);

            if(mtn->tfn->abort == FALSE && G->Error == FALSE)
            {
              // check if any of the above getline() operations caused a ferror or
              // if we didn't walk until the end of the mail file
              if(ferror(fh) != 0 || feof(fh) == 0)
              {
                E(DBF_NET, "input mail file returned error state: ferror(fh)=%ld feof(fh)=%ld", ferror(fh), feof(fh));

                ER_NewError(tr(MSG_ER_ErrorReadMailfile), mf);
                result = -1; // signal error
              }
              else
              {
                // we have to flush the write buffer if this wasn't a error or
                // abort situation
                TR_WriteFlush(mtn->tfn);

                // send a CRLF+octet "\r\n." to signal that the data is finished.
                // we do it here because if there was an error and we send it, the message
                // will be send incomplete.
                if(TR_SendSMTPCmd(mtn->tfn, SMTP_FINISH, NULL, tr(MSG_ER_BADRESPONSE_SMTP)) != NULL)
                {
                  // put the transferStat to 100%
                  DoMethod(G->transferWindowObject, MUIM_TransferWindow_TransStat_Update, TS_SETMAX, tr(MSG_TR_Sending));

                  // now that we are at 100% we have to set the transfer Date of the message
                  GetSysTimeUTC(&mtn->mail->Reference->transDate);

                  result = email->DelSend ? 2 : 1;
                  AppendToLogfile(LF_VERBOSE, 42, tr(MSG_LOG_SendingVerbose), AddrName(mtn->mail->To), mtn->mail->Subject, mtn->mail->Size);
                }
              }
            }

            if(mtn->tfn->abort == TRUE || G->Error == TRUE)
              result = -1; // signal the caller that we aborted within the DATA part
          }
        }

        MA_FreeEMailStruct(email);
      }
      else
        ER_NewError(tr(MSG_ER_CantOpenFile), mf);
    }

    fclose(fh);
  }
  else if(buf != NULL)
    ER_NewError(tr(MSG_ER_CantOpenFile), mf);

  if(buf != NULL)
    free(buf);

  RETURN(result);
  return result;
}
///
/// SendMails()
//  Sends a list of messages through their corresponding SMTP servers
BOOL SendMails(struct MailList *mlist, enum SendMode mode)
{
  BOOL success = FALSE;
  struct TransferNode *tfn;
  struct MailNode *mnode;
  int c = 0;

  ENTER();

  // we iterate through the mail list and find out which
  // mail we transfer through which SMTP server.

  #warning "FIXME: Add multiple SMTP server support here"
  if((tfn = CreateNewTransfer(GetMailServer(&C->mailServerList, MST_SMTP, 0))) == NULL)
  {
    RETURN(FALSE);
    return FALSE;
  }

  tfn->mode = mode;

  LockMailListShared(mlist);

  ForEachMailNode(mlist, mnode)
  {
    struct Mail *mail = mnode->mail;

    if(mail != NULL)
    {
      if(hasStatusQueued(mail) || hasStatusError(mail))
      {
        struct MailTransferNode *mtn;

        if((mtn = calloc(1, sizeof(struct MailTransferNode))) != NULL)
        {
          struct Mail *newMail;

          if((newMail = memdup(mail, sizeof(struct Mail))) != NULL)
          {
            newMail->Reference = mail;

            // set index and transfer flags to LOAD
            mtn->index = ++c;
            mtn->tflags = TRF_LOAD;
            mtn->mail = newMail;

            // set the transfernode to the server infor we are having
            mtn->tfn = tfn;

            AddTail((struct List *)&tfn->mailTransferList, (struct Node *)mtn);

            success = TRUE;
          }
          else
            free(mtn);
        }
      }
    }
  }

  UnlockMailList(mlist);

  if(success == TRUE)
  {
    // add this new transfer to our global list
    AddTail((struct List *)&G->transferQueue, (struct Node *)tfn);

    // start the PRESEND macro first
    MA_StartMacro(MACRO_PRESEND, NULL);

    ProcessTransferQueue(MST_SMTP);
  }

  RETURN(success);
  return success;
}

BOOL ProcessSMTPTransfer(struct TransferNode *tfn)
{
  BOOL success = FALSE;
  ENTER();

  if(SetupTransferWindow((tfn->mode == SEND_ALL_AUTO || tfn->mode == SEND_ACTIVE_AUTO) ? TR_SEND_AUTO : TR_SEND_USER) == TRUE)
  {
    // open the transfer window
    if(SafeOpenWindow(G->transferWindowObject) == TRUE)
    {
      // reset some global variables
      tfn->abort = FALSE;
      tfn->pause = FALSE;
      tfn->start = FALSE;
      G->Error = FALSE;

      // now we have to check whether SSL/TLS is selected for SMTP account,
      // and if it is usable. Or if no secure connection is requested
      // we can go on right away.
      if((hasServerSSL(tfn->msn) == FALSE && hasServerTLS(tfn->msn)) ||
         G->TR_UseableTLS == TRUE)
      {
        enum ConnectError err;
        int port;
        char *p;
        char host[SIZE_HOST];
        struct TransStat ts;

        // check if we have to process any filters
        #warning "TODO: who frees the Filter search?"
        tfn->processFilters = (AllocFilterSearch(APPLY_SENT) > 0);

        // If the hostname has a explicit :xxxxx port statement at the end we
        // take this one, even if its not needed anymore.
        strlcpy(host, tfn->msn->hostname, sizeof(host));
        if((p = strchr(host, ':')) != NULL)
        {
          *p = '\0';
          port = atoi(++p);
        }
        else
          port = tfn->msn->port;

        DoMethod(G->transferWindowObject, MUIM_TransferWindow_TransStat_Init);
        DoMethod(G->transferWindowObject, MUIM_TransferWindow_TransStat_Start);
        xset(G->transferWindowObject, MUIA_TransferWindow_StatusLabel, tr(MSG_TR_Connecting),
                                      MUIA_TransferWindow_Title,       host);

        BusyText(tr(MSG_TR_MailTransferTo), host);

        if((err = TR_Connect(tfn)) == CONNECTERR_SUCCESS)
        {
          BOOL connected = FALSE;

          // first we check whether the user wants to connect to a plain SSLv3 server
          // so that we initiate the SSL connection now
          if(hasServerSSL(tfn->msn) == TRUE)
          {
            // lets try to establish the SSL connection via AmiSSL
            if(TR_InitTLS(tfn) == TRUE && TR_StartTLS(tfn) == TRUE)
              tfn->useSSL = TRUE;
            else
              err = CONNECTERR_SSLFAILED; // special SSL connection error
          }

          // first we have to check whether the TCP/IP connection could
          // be successfully opened so that we can init the SMTP connection
          // and query the SMTP server for its capabilities now.
          if(err == CONNECTERR_SUCCESS)
          {
            // initialize the SMTP connection which will also
            // query the SMTP server for its capabilities
            connected = TR_ConnectSMTP(tfn);

            // Now we have to check whether the user has selected SSL/TLS
            // and then we have to initiate the STARTTLS command followed by the TLS negotiation
            if(hasServerTLS(tfn->msn) == TRUE && connected == TRUE)
            {
              connected = TR_InitSTARTTLS(tfn);

              // then we have to refresh the SMTPflags and check
              // again what features we have after the STARTTLS
              if(connected == TRUE)
              {
                // first we flag this connection as a sucessful
                // secure session
                tfn->useSSL = TRUE;

                // now run the connect SMTP function again
                // so that the SMTP server flags will be refreshed
                // accordingly.
                connected = TR_ConnectSMTP(tfn);
              }
            }

            // If the user selected SMTP_AUTH we have to initiate
            // a AUTH connection
            if(hasServerAuth(tfn->msn) == TRUE && connected == TRUE)
              connected = TR_InitSMTPAUTH(tfn);
          }

          // If we are still "connected" we can proceed with transfering the data
          if(connected == TRUE)
          {
            struct Folder *outfolder = FO_GetFolderByType(FT_OUTGOING, NULL);
            struct Folder *sentfolder = FO_GetFolderByType(FT_SENT, NULL);
            struct Node *curNode;

            // set the success to TRUE as everything worked out fine
            // until here.
            success = TRUE;
            AppendToLogfile(LF_VERBOSE, 41, tr(MSG_LOG_ConnectSMTP), host);

            IterateList(&tfn->mailTransferList, curNode)
            {
              struct MailTransferNode *mtn = (struct MailTransferNode *)curNode;
              struct Mail *mail = mtn->mail;

              if(tfn->abort == TRUE || G->Error == TRUE)
                break;

              DoMethod(G->transferWindowObject, MUIM_TransferWindow_TransStat_NextMsg, mtn->index, -1, mail->Size, tr(MSG_TR_Sending));

              switch(TR_SendMessage(mtn))
              {
                // -1 means that SendMessage was aborted within the
                // DATA part and so we cannot issue a RSET command and have to abort
                // immediatly by leaving the mailserver alone.
                case -1:
                {
                  setStatusToError(mail->Reference);
                  G->Error = TRUE;
                }
                break;

                // 0 means that a error occured before the DATA part and
                // so we can abort the transaction cleanly by a RSET and QUIT
                case 0:
                {
                  setStatusToError(mail->Reference);
                  TR_SendSMTPCmd(tfn, SMTP_RSET, NULL, NULL); // no error check
                  G->Error = FALSE;
                }
                break;

                // 1 means we filter the mails and then copy/move the mail to the send folder
                case 1:
                {
                  setStatusToSent(mail->Reference);
                  if(TR_ApplySentFilters(mtn) == TRUE)
                    MA_MoveCopy(mail->Reference, outfolder, sentfolder, FALSE, TRUE);
                }
                break;

                // 2 means we filter and delete afterwards
                case 2:
                {
                  setStatusToSent(mail->Reference);
                  if (TR_ApplySentFilters(mtn) == TRUE)
                    MA_DeleteSingle(mail->Reference, DELF_UPDATE_APPICON);
                }
                break;
              }
            }

            #warning "TODO: reenable AppendToLog with c == number of transfered mails"
            /*
            if(G->Error == FALSE)
              AppendToLogfile(LF_NORMAL, 40, tr(MSG_LOG_Sending), c, host);
            else
              AppendToLogfile(LF_NORMAL, 40, tr(MSG_LOG_SENDING_FAILED), c, host);
            */

            // now we can disconnect from the SMTP
            // server again
            xset(G->transferWindowObject, MUIA_TransferWindow_StatusLabel, tr(MSG_TR_Disconnecting));

            // send a 'QUIT' command, but only if
            // we didn't receive any error during the transfer
            if(G->Error == FALSE)
              TR_SendSMTPCmd(tfn, SMTP_QUIT, NULL, tr(MSG_ER_BADRESPONSE_SMTP));
          }
          else
          {
            // check if we end up here cause of the 8BITMIME differences
            if(has8BITMIME(tfn->smtpFlags) == FALSE && hasServer8bit(tfn->msn) == TRUE)
            {
              W(DBF_NET, "incorrect Allow8bit setting!");
              err = CONNECTERR_INVALID8BIT;
            }
            else if(err != CONNECTERR_SSLFAILED)
              err = CONNECTERR_UNKNOWN_ERROR;
          }

          // make sure to shutdown the socket
          // and all possible SSL connection stuff
          TR_Disconnect(tfn);
        }

        DoMethod(G->transferWindowObject, MUIM_TransferWindow_TransStat_Finish);

        // if we got an error here, let's throw it
        switch(err)
        {
          case CONNECTERR_SUCCESS:
          case CONNECTERR_ABORTED:
          case CONNECTERR_NO_ERROR:
            // do nothing
          break;

          // a socket is already in use so we return
          // a specific error to the user
          case CONNECTERR_SOCKET_IN_USE:
            ER_NewError(tr(MSG_ER_CONNECTERR_SOCKET_IN_USE_SMTP), host);
          break;

          // socket() execution failed
          case CONNECTERR_NO_SOCKET:
            ER_NewError(tr(MSG_ER_CONNECTERR_NO_SOCKET_SMTP), host);
          break;

          // couldn't establish non-blocking IO
          case CONNECTERR_NO_NONBLOCKIO:
            ER_NewError(tr(MSG_ER_CONNECTERR_NO_NONBLOCKIO_SMTP), host);
          break;

          // the specified hostname isn't valid, so
          // lets tell the user
          case CONNECTERR_UNKNOWN_HOST:
            ER_NewError(tr(MSG_ER_UNKNOWN_HOST_SMTP), host);
          break;

          // the connection request timed out, so tell
          // the user
          case CONNECTERR_TIMEDOUT:
            ER_NewError(tr(MSG_ER_CONNECTERR_TIMEDOUT_SMTP), host);
          break;

          // an error occurred while checking for 8bit MIME
          // compatibility
          case CONNECTERR_INVALID8BIT:
            ER_NewError(tr(MSG_ER_NO8BITMIME_SMTP), host);
          break;

          // error during initialization of an SSL connection
          case CONNECTERR_SSLFAILED:
            ER_NewError(tr(MSG_ER_INITTLS_SMTP), host);
          break;

          // an unknown error occurred so lets show
          // a generic error message
          case CONNECTERR_UNKNOWN_ERROR:
            ER_NewError(tr(MSG_ER_CANNOT_CONNECT_SMTP), host);
          break;
        }

        FreeFilterSearch();
        tfn->processFilters = FALSE;

        BusyEnd();
      }
      else
        ER_NewError(tr(MSG_ER_UNUSABLEAMISSL));
    }
  }

#warning "TODO: can we really put the POSTSEND macro here?"
  // start the POSTSEND macro so that others
  // notice that the send process finished.
  //MA_StartMacro(MACRO_POSTSEND, NULL);

  RETURN(success);
  return success;
}
