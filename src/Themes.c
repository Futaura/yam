/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 by Marcel Beck <mbeck@yam.ch>
 Copyright (C) 2000-2007 by YAM Open Source Team

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

#include <proto/dos.h>
#include <proto/icon.h>

#include "YAM.h"
#include "YAM_global.h"
#include "YAM_locale.h"
#include "YAM_config.h"
#include "YAM_error.h"
#include "extrasrc.h"

#include "ImageCache.h"
#include "Themes.h"

#include "Debug.h"

// The themelayout version defines the current "version" of
// the theme layout we are currently using
//
// Please note that as soon as you change something to the internal
// image arrays of the theme you have to bump the VERSION accordingly
// to make sure the user is reminded of having the correct image layout
// installed or not.
//
// VERSIONS:
// 1: YAM 2.5
//
#define THEME_REQVERSION 1

// static image identifiers
/// config image IDs
static const char * const configImageIDs[ci_Max] =
{
  "config_abook",     "config_abook_big",
  "config_answer",    "config_answer_big",
  "config_filters",   "config_filters_big",
  "config_firststep", "config_firststep_big",
  "config_lists",     "config_lists_big",
  "config_lookfeel",  "config_lookfeel_big",
  "config_mime",      "config_mime_big",
  "config_misc",      "config_misc_big",
  "config_network",   "config_network_big",
  "config_newmail",   "config_newmail_big",
  "config_read",      "config_read_big",
  "config_scripts",   "config_scripts_big",
  "config_security",  "config_security_big",
  "config_signature", "config_signature_big",
  "config_spam",      "config_spam_big",
  "config_start",     "config_start_big",
  "config_update",    "config_update_big",
  "config_write",     "config_write_big",
};
///
/// folder image IDs
static const char * const folderImageIDs[fi_Max] =
{
  "folder_fold",
  "folder_unfold",
  "folder_incoming",
  "folder_incoming_new",
  "folder_outgoing",
  "folder_outgoing_new",
  "folder_sent",
  "folder_spam",
  "folder_spam_new",
  "folder_trash",
  "folder_trash_new",
};
///
/// icon image IDs
static const char * const iconImageIDs[ii_Max] =
{
  "check",
  "empty",
  "new",
  "old",
};
///
/// status image IDs
static const char * const statusImageIDs[si_Max] =
{
  "status_attach",
  "status_crypt",
  "status_delete",
  "status_download",
  "status_error",
  "status_forward",
  "status_group",
  "status_hold",
  "status_mark",
  "status_new",
  "status_old",
  "status_reply",
  "status_report",
  "status_sent",
  "status_signed",
  "status_spam",
  "status_unread",
  "status_urgent",
  "status_waitsend",
};
///
/// toolbar image IDs
static const char * const tbii[tbi_Max][tbim_Max] =
{
 // Normal            Selected            Ghosted
  { "tb_read",        "tb_read_s",        "tb_read_g"       },
  { "tb_edit",        "tb_edit_s",        "tb_edit_g"       },
  { "tb_move",        "tb_move_s",        "tb_move_g"       },
  { "tb_delete",      "tb_delete_s",      "tb_delete_g"     },
  { "tb_getaddr",     "tb_getaddr_s",     "tb_getaddr_g"    },
  { "tb_newmail",     "tb_newmail_s",     "tb_newmail_g"    },
  { "tb_reply",       "tb_reply_s",       "tb_reply_g"      },
  { "tb_forward",     "tb_forward_s",     "tb_forward_g"    },
  { "tb_getmail",     "tb_getmail_s",     "tb_getmail_g"    },
  { "tb_sendall",     "tb_sendall_s",     "tb_sendall_g"    },
  { "tb_spam",        "tb_spam_s",        "tb_spam_g"       },
  { "tb_ham",         "tb_ham_s",         "tb_ham_g"        },
  { "tb_filter",      "tb_filter_s",      "tb_filter_g"     },
  { "tb_find",        "tb_find_s",        "tb_find_g"       },
  { "tb_addrbook",    "tb_addrbook_s",    "tb_addrbook_g"   },
  { "tb_config",      "tb_config_s",      "tb_config_g"     },
  { "tb_prev",        "tb_prev_s",        "tb_prev_g"       },
  { "tb_next",        "tb_next_s",        "tb_next_g"       },
  { "tb_prevthread",  "tb_prevthread_s",  "tb_prevthread_g" },
  { "tb_nextthread",  "tb_nextthread_s",  "tb_nextthread_g" },
  { "tb_display",     "tb_display_s",     "tb_display_g"    },
  { "tb_save",        "tb_save_s",        "tb_save_g"       },
  { "tb_print",       "tb_print_s",       "tb_print_g"      },
  { "tb_editor",      "tb_editor_s",      "tb_editor_g"     },
  { "tb_insert",      "tb_insert_s",      "tb_insert_g"     },
  { "tb_cut",         "tb_cut_s",         "tb_cut_g"        },
  { "tb_copy",        "tb_copy_s",        "tb_copy_g"       },
  { "tb_paste",       "tb_paste_s",       "tb_paste_g"      },
  { "tb_undo",        "tb_undo_s",        "tb_undo_g"       },
  { "tb_bold",        "tb_bold_s",        "tb_bold_g"       },
  { "tb_italic",      "tb_italic_s",      "tb_italic_g"     },
  { "tb_underline",   "tb_underline_s",   "tb_underline_g"  },
  { "tb_colored",     "tb_colored_s",     "tb_colored_g"    },
  { "tb_newuser",     "tb_newuser_s",     "tb_newuser_g"    },
  { "tb_newlist",     "tb_newlist_s",     "tb_newlist_g"    },
  { "tb_newgroup",    "tb_newgroup_s",    "tb_newgroup_g"   },
  { "tb_opentree",    "tb_opentree_s",    "tb_opentree_g"   },
  { "tb_closetree",   "tb_closetree_s",   "tb_closetree_g"  },
};
///

// window toolbar mappings
/// main window toolbar image IDs
static const enum ToolbarImages mainWindowToolbarImageIDs[mwtbi_Max] =
{
  tbi_Read,
  tbi_Edit,
  tbi_Move,
  tbi_Delete,
  tbi_GetAddr,
  tbi_NewMail,
  tbi_Reply,
  tbi_Forward,
  tbi_GetMail,
  tbi_SendAll,
  tbi_Spam,
  tbi_Ham,
  tbi_Filter,
  tbi_Find,
  tbi_AddrBook,
  tbi_Config
};
///
/// read window toolbar image IDs
static const enum ToolbarImages readWindowToolbarImageIDs[rwtbi_Max] =
{
  tbi_Prev,
  tbi_Next,
  tbi_PrevThread,
  tbi_NextThread,
  tbi_Display,
  tbi_Save,
  tbi_Print,
  tbi_Delete,
  tbi_Move,
  tbi_Reply,
  tbi_Forward,
  tbi_Spam,
  tbi_Ham
};
///
/// write window toolbar image IDs
static const enum ToolbarImages writeWindowToolbarImageIDs[wwtbi_Max] =
{
  tbi_Editor,
  tbi_Insert,
  tbi_Cut,
  tbi_Copy,
  tbi_Paste,
  tbi_Undo,
  tbi_Bold,
  tbi_Italic,
  tbi_Underline,
  tbi_Colored,
  tbi_Find,
};
///
/// addressbook window toolbar image IDs
static const enum ToolbarImages abookWindowToolbarImageIDs[awtbi_Max] =
{
  tbi_Save,
  tbi_Find,
  tbi_NewUser,
  tbi_NewList,
  tbi_NewGroup,
  tbi_Edit,
  tbi_Delete,
  tbi_Print,
  tbi_OpenTree,
  tbi_CloseTree,
};
///

// public functions
/// AllocTheme
// allocate everything for a theme while setting everything
// to the default first.
void AllocTheme(struct Theme *theme, const char *themeName)
{
  int i;
  int j;
  char dirname[SIZE_PATHFILE];
  char filepath[SIZE_PATHFILE];

  ENTER();

  theme->name = NULL;
  theme->author = NULL;
  theme->version = NULL;

  // contruct the path to the themes directory
  AddPath(theme->directory, G->ProgDir, "Themes", sizeof(theme->directory));
  AddPart(theme->directory, themeName, sizeof(theme->directory));

  D(DBF_THEME, "theme directory: '%s' '%s'", theme->directory, G->ProgDir);

  // construct pathes to config images
  AddPath(dirname, theme->directory, "config", sizeof(dirname));
  for(i=ci_First; i < ci_Max; i++)
  {
    AddPath(filepath, dirname, configImageIDs[i], sizeof(filepath));
    theme->configImages[i] = strdup(filepath);
  }

  // construct pathes to folder images
  AddPath(dirname, theme->directory, "folder", sizeof(dirname));
  for(i=fi_First; i < fi_Max; i++)
  {
    AddPath(filepath, dirname, folderImageIDs[i], sizeof(filepath));
    theme->folderImages[i] = strdup(filepath);
  }

  // construct pathes to icon images
  AddPath(dirname, theme->directory, "icon", sizeof(dirname));
  for(i=ii_First; i < ii_Max; i++)
  {
    AddPath(filepath, dirname, iconImageIDs[i], sizeof(filepath));
    theme->iconImages[i] = strdup(filepath);
  }

  // construct pathes to status images
  AddPath(dirname, theme->directory, "status", sizeof(dirname));
  for(i=si_First; i < si_Max; i++)
  {
    AddPath(filepath, dirname, statusImageIDs[i], sizeof(filepath));
    theme->statusImages[i] = strdup(filepath);
  }

  // construct pathes for the toolbar images
  AddPath(dirname, theme->directory, "toolbar", sizeof(dirname));
  for(j=tbim_Normal; j < tbim_Max; j++)
  {
    // main window toolbar
    for(i=mwtbi_First; i < mwtbi_Null; i++)
    {
      AddPath(filepath, dirname, tbii[mainWindowToolbarImageIDs[i]][j], sizeof(filepath));
      theme->mainWindowToolbarImages[j][i] = strdup(filepath);
    }
    // the array must be NULL terminated
    theme->mainWindowToolbarImages[j][mwtbi_Null] = NULL;

    // read window toolbar
    for(i=rwtbi_First; i < rwtbi_Null; i++)
    {
      AddPath(filepath, dirname, tbii[readWindowToolbarImageIDs[i]][j], sizeof(filepath));
      theme->readWindowToolbarImages[j][i] = strdup(filepath);
    }
    // the array must be NULL terminated
    theme->readWindowToolbarImages[j][rwtbi_Null] = NULL;

    // write window toolbar
    for(i=wwtbi_First; i < wwtbi_Null; i++)
    {
      AddPath(filepath, dirname, tbii[writeWindowToolbarImageIDs[i]][j], sizeof(filepath));
      theme->writeWindowToolbarImages[j][i] = strdup(filepath);
    }
    // the array must be NULL terminated
    theme->writeWindowToolbarImages[j][wwtbi_Null] = NULL;

    // addressbook window toolbar
    for(i=awtbi_First; i < awtbi_Null; i++)
    {
      AddPath(filepath, dirname, tbii[abookWindowToolbarImageIDs[i]][j], sizeof(filepath));
      theme->abookWindowToolbarImages[j][i] = strdup(filepath);
    }
    // the array must be NULL terminated
    theme->abookWindowToolbarImages[j][awtbi_Null] = NULL;
  }

  theme->loaded = FALSE;

  LEAVE();
}
///
/// ParseThemeFile
// parse a complete theme file and returns > 0 in case of success
LONG ParseThemeFile(const char *themeFile, struct Theme *theme)
{
  LONG result = 0; // signals an error
  FILE *fh;

  ENTER();

  if((fh = fopen(themeFile, "r")) != NULL)
  {
    char buffer[SIZE_LARGE];

    setvbuf(fh, NULL, _IOFBF, SIZE_FILEBUF);

    if(fgets(buffer, sizeof(buffer), fh) && strnicmp(buffer, "YTH", 3) == 0)
    {
      int version = buffer[3]-'0';

      // check if the these has the correct version
      if(version == THEME_REQVERSION)
      {
        while(fgets(buffer, sizeof(buffer), fh))
        {
          char *p;
          char *id;
          char *value;

          if((p = strpbrk(buffer, ";#\r\n")) != NULL)
            *p = '\0';

          if((value = strchr(buffer, '=')) != NULL)
          {
            *value++ = '\0';
            value = Trim(value);
          }
          else
          {
            // assume empty filename
            value = (char *)"";
          }

          id = Trim(buffer);
          if(id[0] != '\0')
          {
            int i;
            int j;
            BOOL found = FALSE;
            char *image = strdup(value);

            // theme description
            if(stricmp(id, "Name") == 0)
            {
              if(theme->name != NULL)
                free(theme->name);

              theme->name = image;
              found = TRUE;
            }
            else if(stricmp(id, "Author") == 0)
            {
              if(theme->author != NULL)
                free(theme->author);

              theme->author = image;
              found = TRUE;
            }
            else if(stricmp(id, "URL") == 0)
            {
              if(theme->url != NULL)
                free(theme->url);

              theme->url = image;
              found = TRUE;
            }
            else if(stricmp(id, "Version") == 0)
            {
              if(theme->version != NULL)
                free(theme->version);

              theme->version = image;
              found = TRUE;
            }
            else
            {
              if(strchr(image, ':') == NULL)
              {
                // image filename is relative to the theme directory
                free(image);
                asprintf(&image, "%s/%s", theme->directory, value);
              }

              // config images
              for(i=ci_First; i < ci_Max && found == FALSE; i++)
              {
                if(stricmp(id, configImageIDs[i]) == 0)
                {
                  if(theme->configImages[i] != NULL)
                    free(theme->configImages[i]);

                  theme->configImages[i] = image;
                  found = TRUE;
                }
              }

              // folder images
              for(i=fi_First; i < fi_Max && found == FALSE; i++)
              {
                if(stricmp(id, folderImageIDs[i]) == 0)
                {
                  if(theme->folderImages[i] != NULL)
                    free(theme->folderImages[i]);

                  theme->folderImages[i] = image;
                  found = TRUE;
                }
              }

              // icon images
              for(i=ii_First; i < ii_Max && found == FALSE; i++)
              {
                if(stricmp(id, iconImageIDs[i]) == 0)
                {
                  if(theme->iconImages[i] != NULL)
                    free(theme->iconImages[i]);

                  theme->iconImages[i] = image;
                  found = TRUE;
                }
              }

              // status images
              for(i=si_First; i < si_Max && found == FALSE; i++)
              {
                if(stricmp(id, statusImageIDs[i]) == 0)
                {
                  if(theme->statusImages[i] != NULL)
                    free(theme->statusImages[i]);

                  theme->statusImages[i] = image;
                  found = TRUE;
                }
              }

              // toolbar images
              for(j=tbim_Normal; j < tbim_Max && found == FALSE; j++)
              {
                // main window toolbar
                for(i=mwtbi_First; i < mwtbi_Null && found == FALSE; i++)
                {
                  if(stricmp(id, tbii[mainWindowToolbarImageIDs[i]][j]) == 0)
                  {
                    if(theme->mainWindowToolbarImages[j][i] != NULL)
                      free(theme->mainWindowToolbarImages[j][i]);

                    theme->mainWindowToolbarImages[j][i] = image;
                    found = TRUE;
                  }
                }

                // read window toolbar
                for(i=rwtbi_First; i < rwtbi_Null && found == FALSE; i++)
                {
                  if(stricmp(id, tbii[readWindowToolbarImageIDs[i]][j]) == 0)
                  {
                    if(theme->readWindowToolbarImages[j][i] != NULL)
                      free(theme->readWindowToolbarImages[j][i]);

                    theme->readWindowToolbarImages[j][i] = image;
                    found = TRUE;
                  }
                }

                // write window toolbar
                for(i=wwtbi_First; i < wwtbi_Null && found == FALSE; i++)
                {
                  if(stricmp(id, tbii[writeWindowToolbarImageIDs[i]][j]) == 0)
                  {
                    if(theme->writeWindowToolbarImages[j][i] != NULL)
                      free(theme->writeWindowToolbarImages[j][i]);

                    theme->writeWindowToolbarImages[j][i] = image;
                    found = TRUE;
                  }
                }

                // addressbook window toolbar
                for(i=awtbi_First; i < awtbi_Null && found == FALSE; i++)
                {
                  if(stricmp(id, tbii[abookWindowToolbarImageIDs[i]][j]) == 0)
                  {
                    if(theme->abookWindowToolbarImages[j][i] != NULL)
                      free(theme->abookWindowToolbarImages[j][i]);

                    theme->abookWindowToolbarImages[j][i] = image;
                    found = TRUE;
                  }
                }
              }
            }

            if(found == FALSE)
            {
              W(DBF_IMAGE, "unknown theme setting '%s' = '%s'", id, image);
              free(image);
            }

            result = 1; // signal success
          }
        }
      }
      else
      {
        W(DBF_THEME, "incorrect theme version found: %ld != %ld", version, THEME_REQVERSION);

        result = -1; // signal a version problem
      }
    }
    else
      W(DBF_THEME, "invalid header in .theme file found");
  }
  else
    W(DBF_THEME, "couldn't open .theme file '%s'", themeFile);

  RETURN(result);
  return result;
}
///
/// FreeTheme
// free all the strings of a theme
void FreeTheme(struct Theme *theme)
{
  int i;
  int j;

  ENTER();

  if(theme->loaded == TRUE)
    UnloadTheme(theme);

  if(theme->name != NULL)
  {
    free(theme->name);
    theme->name = NULL;
  }

  if(theme->author != NULL)
  {
    free(theme->author);
    theme->author = NULL;
  }

  if(theme->version != NULL)
  {
    free(theme->version);
    theme->version = NULL;
  }

  for(i=ci_First; i < ci_Max; i++)
  {
    if(theme->configImages[i] != NULL)
    {
      free(theme->configImages[i]);
      theme->configImages[i] = NULL;
    }
  }

  for(i=fi_First; i < fi_Max; i++)
  {
    if(theme->folderImages[i] != NULL)
    {
      free(theme->folderImages[i]);
      theme->folderImages[i] = NULL;
    }
  }

  for(i=ii_First; i < ii_Max; i++)
  {
    if(theme->iconImages[i] != NULL)
    {
      free(theme->iconImages[i]);
      theme->iconImages[i] = NULL;
    }
  }

  for(i=si_First; i < si_Max; i++)
  {
    if(theme->statusImages[i] != NULL)
    {
      free(theme->statusImages[i]);
      theme->statusImages[i] = NULL;
    }
  }

  // free the toolbar images
  for(j=tbim_Normal; j < tbim_Max; j++)
  {
    // main window toolbar
    for(i=mwtbi_First; i < mwtbi_Null; i++)
    {
      if(theme->mainWindowToolbarImages[j][i] != NULL)
      {
        free(theme->mainWindowToolbarImages[j][i]);
        theme->mainWindowToolbarImages[j][i] = NULL;
      }
    }

    // read window toolbar
    for(i=rwtbi_First; i < rwtbi_Null; i++)
    {
      if(theme->readWindowToolbarImages[j][i] != NULL)
      {
        free(theme->readWindowToolbarImages[j][i]);
        theme->readWindowToolbarImages[j][i] = NULL;
      }
    }

    // write window toolbar
    for(i=wwtbi_First; i < wwtbi_Null; i++)
    {
      if(theme->writeWindowToolbarImages[j][i] != NULL)
      {
        free(theme->writeWindowToolbarImages[j][i]);
        theme->writeWindowToolbarImages[j][i] = NULL;
      }
    }

    // addressbook window toolbar
    for(i=awtbi_First; i < awtbi_Null; i++)
    {
      if(theme->abookWindowToolbarImages[j][i] != NULL)
      {
        free(theme->abookWindowToolbarImages[j][i]);
        theme->abookWindowToolbarImages[j][i] = NULL;
      }
    }
  }

  LEAVE();
}
///
/// LoadTheme
// load all images of a theme
void LoadTheme(struct Theme *theme, const char *themeName)
{
  char themeFile[SIZE_PATHFILE];
  int i;
  LONG res;

  ENTER();

  // allocate all resources for the theme
  AllocTheme(theme, themeName);

  // Parse the .theme file within the
  // theme directory
  AddPath(themeFile, theme->directory, ".theme", sizeof(themeFile));
  res = ParseThemeFile(themeFile, theme);

  // check if parsing the theme file worked out or not
  if(res > 0)
    D(DBF_THEME, "successfully parsed theme file '%s'", themeFile);
  else
  {
    // check if it was the default theme that failed or
    // not.
    if(stricmp(themeName, "default") == 0)
    {
      W(DBF_THEME, "parsing of theme file '%s' failed! ignoring...", themeFile);

      // warn the user
      if(res == -1)
        ER_NewError(tr(MSG_ER_THEMEVER_IGNORE));
      else
        ER_NewError(tr(MSG_ER_THEME_FATAL));

      // free the theme resources
      FreeTheme(theme);

      LEAVE();
      return;
    }
    else
    {
      W(DBF_THEME, "parsing of theme file '%s' failed! trying default theme...", themeFile);

      // warn the user
      if(res == -1)
        ER_NewError(tr(MSG_ER_THEMEVER_FALLBACK), themeName);
      else
        ER_NewError(tr(MSG_ER_THEME_FALLBACK), themeName);

      // free the theme resources
      FreeTheme(theme);

      // allocate the default theme
      AllocTheme(theme, "default");
      AddPath(themeFile, theme->directory, ".theme", sizeof(themeFile));
      if(ParseThemeFile(themeFile, theme) <= 0)
      {
        // warn the user
        if(res == -1)
          ER_NewError(tr(MSG_ER_THEMEVER_IGNORE));
        else
          ER_NewError(tr(MSG_ER_THEME_FATAL));

        FreeTheme(theme);

        LEAVE();
        return;
      }
    }
  }

  for(i=ci_First; i < ci_Max; i++)
  {
    char *image = theme->configImages[i];

    if(image != NULL && image[0] != '\0')
    {
      if(ObtainImage(configImageIDs[i], image, NULL) == NULL)
        W(DBF_THEME, "couldn't obtain image '%s' of theme '%s'", image, theme->directory);
    }
  }

  for(i=fi_First; i < fi_Max; i++)
  {
    char *image = theme->folderImages[i];

    if(image != NULL && image[0] != '\0')
    {
      if(ObtainImage(folderImageIDs[i], image, NULL) == NULL)
        W(DBF_THEME, "couldn't obtain image '%s' of theme '%s'", image, theme->directory);
    }
  }

  for(i=si_First; i < si_Max; i++)
  {
    char *image = theme->statusImages[i];

    if(image != NULL && image[0] != '\0')
    {
      if(ObtainImage(statusImageIDs[i], image, NULL) == NULL)
        W(DBF_THEME, "couldn't obtain image '%s' of theme '%s'", image, theme->directory);
    }
  }

  for(i=ii_First; i < ii_Max; i++)
  {
    char *image = theme->iconImages[i];

    if(image != NULL && image[0] != '\0')
    {
      // depending on the icon.library version we use either GetIconTags()
      // or the older GetDiskObject() function
      if(IconBase->lib_Version >= 44)
        theme->icons[i] = GetIconTags(image, TAG_DONE);
      else
        theme->icons[i] = GetDiskObject(image);

      // load the diskobject and report an error if something went wrong.
      if(theme->icons[i] == NULL && G->NoImageWarning == FALSE)
        ER_NewError(tr(MSG_ER_ICONOBJECT_WARNING), FilePart(image), themeName);
    }
  }

  theme->loaded = TRUE;

  LEAVE();
}
///
/// UnloadTheme
// unload all images of a theme
void UnloadTheme(struct Theme *theme)
{
  int i;

  ENTER();

  for(i=ci_First; i < ci_Max; i++)
    ReleaseImage(configImageIDs[i], TRUE);

  for(i=fi_First; i < fi_Max; i++)
    ReleaseImage(folderImageIDs[i], TRUE);

  for(i=si_First; i < si_Max; i++)
    ReleaseImage(statusImageIDs[i], TRUE);

  for(i=ii_First; i < ii_Max; i++)
  {
    if(theme->icons[i] != NULL)
    {
      FreeDiskObject(theme->icons[i]);
      theme->icons[i] = NULL;
    }
  }

  theme->loaded = FALSE;

  LEAVE();
}
///

