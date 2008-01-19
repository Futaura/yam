#ifndef TIMER_H
#define TIMER_H

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

***************************************************************************/

// all the different timers YAM is using
enum Timer
{
  TIMER_WRINDEX=0,
  TIMER_CHECKMAIL,
  TIMER_AUTOSAVE,
  TIMER_READPANEUPDATE,
  TIMER_READSTATUSUPDATE,
  TIMER_PROCESSQUICKSEARCH,
  TIMER_POP3_KEEPALIVE,
  TIMER_UPDATECHECK,
  TIMER_SPAMFLUSHTRAININGDATA,
  TIMER_DELETEZOMBIEFILES,
  TIMER_NUM
};

// own Timer structures we use
struct TRequest
{
  struct TimeRequest *tr;       // pointer to the timerequest
  struct TimeVal startTime;     // at which time has this request been started
  struct TimeVal remainingTime; // the remaining time if the request was paused
  BOOL isRunning;               // if the request is currenty active/running
  BOOL isPrepared;              // if the request is prepared to get fired
  BOOL isPaused;                // if the request is currently paused
};

struct Timers
{
  struct MsgPort  *port;
  struct TRequest timer[TIMER_NUM];
};

void PrepareTimer(const enum Timer tid, const int seconds, const int micros);
void StartTimer(const enum Timer tid);
void StopTimer(const enum Timer tid);
void PauseTimer(const enum Timer tid);
void ResumeTimer(const enum Timer tid);
void RestartTimer(const enum Timer tid, const int seconds, const int micros);

BOOL InitTimers(void);
void CleanupTimers(void);
BOOL ProcessTimerEvent(void);

#endif /* TIMER_H */

