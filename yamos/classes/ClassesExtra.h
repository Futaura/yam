/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 by Marcel Beck <mbeck@yam.ch>
 Copyright (C) 2000-2001 by YAM Open Source Team

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

 YAM Official Support Site :  http://www.yam.ch/
 YAM OpenSource project    :  http://sourceforge.net/projects/yamos/

 $Id$

***************************************************************************/

#ifndef CLASSES_CLASSES_EXTRA_H
#define CLASSES_CLASSES_EXTRA_H

#include <clib/alib_protos.h>
#include <clib/macros.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/timer.h>
#include <proto/utility.h>
#include <libraries/gadtools.h>
#include <libraries/iffparse.h>
#include <libraries/mui.h>
#include <mui/BetterString_mcc.h>
#include <mui/NList_mcc.h>
#include <mui/NListview_mcc.h>
#include <mui/TextEditor_mcc.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "Debug.h"
#include "extra.h"
#include "newmouse.h"
#include "SDI_hook.h"
#include "SDI_stdarg.h"

#include "YAM.h"
#include "YAM_addressbook.h"
#include "YAM_config.h"
#include "YAM_debug.h"
#include "YAM_folderconfig.h"
#include "YAM_global.h"
#include "YAM_locale.h"
#include "YAM_locale.h"
#include "YAM_mail_lex.h"
#include "YAM_main.h"
#include "YAM_mainFolder.h"
#include "YAM_read.h"
#include "YAM_utilities.h"
#include "YAM_write.h"

// some own MUI macros (not official)
#ifndef MUIF_NONE
#define MUIF_NONE                    0
#endif

#define MenuChild										MUIA_Family_Child
#define Menuitem(t,s,e,c,u)					MenuitemObject, 										\
																			MUIA_Menuitem_Title,					(t),\
																			MUIA_Menuitem_Shortcut,				(s),\
																			MUIA_Menuitem_Enabled,				(e),\
																			MUIA_Menuitem_CommandString,	(c),\
																			MUIA_UserData,								(u),\
																		End

#define MenuitemCheck(t,s,e,g,x,u)	MenuitemObject,											\
																			MUIA_Menuitem_Checkit,			 TRUE,\
																			MUIA_Menuitem_Title,					(t),\
																			MUIA_Menuitem_Shortcut,				(s),\
																			MUIA_Menuitem_Checked,				(e),\
																			MUIA_Menuitem_Toggle,					(g),\
																			MUIA_Menuitem_Exclude,				(x),\
																			MUIA_UserData,								(u),\
																		End

#define MenuBarLabel								MenuitemObject,											\
																			MUIA_Menuitem_Title,  NM_BARLABEL,\
																		End


// some private (mostly undocumented) MUI stuff...
#ifndef MUIM_GoActive
#define MUIM_GoActive                0x8042491a /* V8  */
#endif
#ifndef MUIM_GoInactive
#define MUIM_GoInactive              0x80422c0c /* V8  */
#endif
#ifndef MUIA_Window_DisableKeys
#define MUIA_Window_DisableKeys      0x80424c36 /* V15 isg ULONG    */
#endif
#ifndef MUIA_Application_UsedClasses 
#define MUIA_Application_UsedClasses 0x8042e9a7 /* V20 isg STRPTR * */
#endif
#ifndef MUIA_String_Popup
#define MUIA_String_Popup            0x80420d71 /* V9  i.. Object * */
#endif
#ifndef MUIA_List_CursorType
#define MUIA_List_CursorType         0x8042c53e /* V4  is. LONG     */
#endif
#ifndef MUIV_List_CursorType_Bar
#define MUIV_List_CursorType_Bar 		 1
#endif
#ifndef MUIA_Text_HiIndex
#define MUIA_Text_HiIndex            0x804214f5 /* V11 i.. LONG     */
#endif
#ifndef MUIM_DeleteDragImage
#define MUIM_DeleteDragImage 				 0x80423037
#endif
#if (MUIMASTER_VMIN < 18)
#define MUIM_DoDrag 0x804216bb /* private */ /* V18 */
struct  MUIP_DoDrag { ULONG MethodID; LONG touchx; LONG touchy; ULONG flags; }; /* private */
#endif

enum { IECODE_SPACE = 64,
			 IECODE_TAB = 66,
			 IECODE_RETURN = 68,
			 IECODE_ESCAPE = 69,
			 IECODE_HELP = 95,
			 IECODE_BACKSPACE = 65,
			 IECODE_DEL = 70,
			 IECODE_UP = 76,
			 IECODE_DOWN = 77
		 };

// some own usefull MUI-style macros to check mouse positions in objects
#define _between(a,x,b) 					((x)>=(a) && (x)<=(b))
#define _isinobject(o,x,y) 				(_between(_mleft(o),(x),_mright (o)) && _between(_mtop(o) ,(y),_mbottom(o)))
#define _isinwholeobject(o,x,y) 	(_between(_left(o),(x),_right (o)) && _between(_top(o) ,(y),_bottom(o)))

#endif
