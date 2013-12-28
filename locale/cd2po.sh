#!/bin/bash
#
# a shell script that allows to convert between Amiga-style catalog
# description/translation files (.cd/.ct) and gettext-style translation
# files (.pot/.po).
#
# Copyright 2013 Jens Maus <mail@jens-maus.de>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
# $Id$
#

########################################################
# Script starts here
#

if [ "$1" != "" ]; then
  CDFILE="$1"
else
  echo "ERROR: missing cmdline argument"
  exit 1
fi

################################
# AWK scripts                  #
################################

# the following is an awk script that converts an
# Amiga-style catalog description file (.cd) to a gettext
# PO-style translation template file (.pot).
read -d '' cd2pot << 'EOF'
BEGIN {
  tagfound=0
  multiline=0
  print "#"
  print "#, fuzzy"
  print "msgid \\"\\""
  print "msgstr \\"\\""
  print "\\"Project-Id-Version: YAM\\\\n\\""
  print "\\"Report-Msgid-Bugs-To: http://yam.ch/\\\\n\\""
  print "\\"POT-Creation-Date: 2012-12-23 10:29+0000\\\\n\\""
  print "\\"PO-Revision-Date: YEAR-MO-DA HO:MI+ZONE\\\\n\\""
  print "\\"Last-Translator: FULL NAME <EMAIL@ADDRESS>\\\\n\\""
  print "\\"Language-Team: LANGUAGE <LL@li.org>\\\\n\\""
  print "\\"MIME-Version: 1.0\\\\n\\""
  print "\\"Content-Type: text/plain; charset=ISO-8859-1\\\\n\\""
  print "\\"Content-Transfer-Encoding: 8bit\\\\n\\""
  print "\\"Language: \\\\n\\""
}
{
  if($0 ~ /^MSG_.*\(.*\)/)
  {
    tagfound=1
    print "\\n#: " $0
    print "msgctxt \\"" $1 "\\""

    next
  }
  else if($0 ~ /^;.*/)
  {
    if(tagfound == 1)
    {
      print "msgstr \\"\\""
    }

    tagfound=0
    multiline=0
  }

  if(tagfound == 1)
  {
    // remove any backslash at the end of line
    gsub(/\\\\$/, "")

    # replace \e with \033
    gsub(/\\\\\\e/, "\\\\033")

    # replace plain " with \" but make
    # sure to check if \" is already there
    gsub(/\\\\"/, "\\"") # replace \" with "
    gsub(/"/, "\\\\\\"") # replace " with \"

    if(multiline == 0)
    {
      # the .po format doesn't allow empty msgid
      # strings, thus lets escape them with <EMPTY>
      if(length($0) == 0)
      {
        print "msgid \\"<EMPTY>\\""
      }
      else
      {
        print "msgid \\"" $0 "\\""
      }

      multiline=1
    }
    else
    {
      print "\\"" $0 "\\""
    }
  }
}
EOF

# the following is an awk script that converts a
# gettext PO-style translation template file (.pot)
# to an Amiga-style catalog description file (.cd)
read -d '' pot2cd << 'EOF'
BEGIN {
  tagfound=0
  msgidfound=0
  print "; YAM.cd - YAM catalog description file"
  print "; $Id$"
  print ";"
  print "; WARNING: This file was automatically generated via cd2po.sh"
  print ";"
  print "#version X"
  print "#language english"
  print ";"
}
{
  if($0 ~ /^#: MSG_.*\(.*\).*/)
  {
    tagfound=1
    msgidfound=0

    # extract the tag "MSG_XXXXX (X//)" as tag
    tag=substr($0, length($1)+2)
  }
  else if(length($0) == 0 && length(tag) != 0)
  {
    tagfound=0
    msgidfound=0

    print tag
    print msgid
    print ";"

    tag=""
  }

  if(tagfound == 1)
  {
    if($0 ~ /^msgid ".*/)
    {
      # get the msgid text only
      msgid=substr($0, length($1)+2)

      # strip quotes (") from start&end
      gsub(/^"/, "", msgid)
      gsub(/"$/, "", msgid)

      # replace "<EMPTY>" with ""
      gsub(/<EMPTY>/, "", msgid)

      msgidfound=1
    }
    else if($1 ~ /^msgstr.*/)
    {
      # ignore msgstr
      msgidfound=0
    }
    else if(msgidfound == 1)
    {
      # strip quotes (") from start&end
      gsub(/^"/, "")
      gsub(/"$/, "")

      msgid = msgid "\\\\\\n" $0
    }
  }
}
END {
  if(length(tag) != 0)
  {
    print tag
    print msgid
    print ";"
  }
}
EOF


read -d '' ct2po << 'EOF'
BEGIN {
  tagfound=0
  multiline=0
  print "#"
  print "# Translators:"
  print "msgid \\"\\""
  print "msgstr \\"\\""
  print "\\"Project-Id-Version: yam\\\\n\\""
  print "\\"Report-Msgid-Bugs-To: http://yam.ch/\\\\n\\""
  print "\\"POT-Creation-Date: 2012-12-23 10:29+0000\\\\n\\""
  print "\\"PO-Revision-Date: YEAR-MO-DA HO:MI+ZONE\\\\n\\""
  print "\\"Last-Translator: FULL NAME <EMAIL@ADDRESS>\\\\n\\""
  print "\\"Language-Team: LANGUAGE <LL@li.org>\\\\n\\""
  print "\\"MIME-Version: 1.0\\\\n\\""
  print "\\"Content-Type: text/plain; charset=UTF-8\\\\n\\""
  print "\\"Content-Transfer-Encoding: 8bit\\\\n\\""
  print "\\"Language: \\\\n\\""
}
{
  if($1 ~ /^MSG_.*$/)
  {
    tagfound=1
    multiline=0
    i=0

    # now we have to search in the CD file for the same string
    cmd="sed -e '1,/" $1 " /d' -e '/^;/,$d' YAM.cd"
    while((cmd |& getline output) > 0)
    {
      i++

      # remove any backslash at the end of line
      gsub(/\\\\$/, "", output)

      # replace \e with \033
      gsub(/\\\\\\e/, "\\\\033", output)

      # replace plain " with \" but make
      # sure to check if \" is already there
      gsub(/\\\\"/, "\\"", output) # replace \" with "
      gsub(/"/, "\\\\\\"", output) # replace " with \"

      if(length(output) == 0)
      {
        output="<EMPTY>"
      }

      if(i == 1)
      {
        print "\\n#: " $0
        print "msgctxt \\"" $1 "\\""
        print "msgid \\"" output "\\""
      }
      else
      {
        print "\\"" output "\\""
      }
    }
    close(cmd)

    if(i == 0)
    {
      tagfound=0
    }

    next
  }
  else if($1 ~ /^;/)
  {
    tagfound=0
    multiline=0
  }

  if(tagfound == 1)
  {
    # remove any backslash at the end of line
    gsub(/\\\\$/, "")

    # replace \e with \033
    gsub(/\\\\\\e/, "\\\\033")

    # replace plain " with \" but make
    # sure to check if \" is already there
    gsub(/\\\\"/, "\\"") # replace \" with "
    gsub(/"/, "\\\\\\"") # replace " with \"

    if(multiline == 0)
    {
      # the .po format doesn't allow empty msgid
      # strings, thus lets escape them with <EMPTY>
      if(length($0) == 0)
      {
        print "msgstr \\"<EMPTY>\\""
      }
      else
      {
        print "msgstr \\"" $0 "\\""
      }

      multiline=1
    }
    else
    {
      print "\\"" $0 "\\""
    }
  }
}
EOF

read -d '' po2ct << 'EOF'
BEGIN {
  tagfound=0
  msgidfound=0
  msgstrfound=0
  print "## version $VER: YAM.catalog 1.0 (27.12.2013)"
  print "## language XXXXXXX"
  print "## codeset 0"
  print "## chunk AUTH XXXXXXXXX"
  print ";"
  print "; $Id$"
  print ";"
}
{
  if($1 ~ /^msgctxt.*/)
  {
    tagfound=1
    msgidfound=0
    msgstrfound=0

    # strip quotes (") so that we get the plain MSG_XXXX
    # tag names
    gsub(/"/, "", $2);
    tag=$2
  }
  else if(length($0) == 0 && length(tag) != 0)
  {
    tagfound=0
    msgidfound=0
    msgstrfound=0

    print tag
    print msgstr
    print msgid
    print ";"

    tag=""
  }

  if(tagfound == 1)
  {
    if($1 ~ /^msgid.*/)
    {
      # get the msgid text only
      msgid=substr($0, length($1)+2)

      # strip quotes (") from start&end
      gsub(/^"/, "", msgid)
      gsub(/"$/, "", msgid)

      # replace "<EMPTY>" with ""
      gsub(/<EMPTY>/, "", msgid)

      msgid = "; " msgid

      msgstrfound=0
      msgidfound=1
    }
    else if($1 ~ /^msgstr.*/)
    {
      # get the msgid text only
      msgstr=substr($0, length($1)+2)

      # strip quotes (") from start&end
      gsub(/^"/, "", msgstr)
      gsub(/"$/, "", msgstr)

      # replace "<EMPTY>" with ""
      gsub(/<EMPTY>/, "", msgstr)

      msgstrfound=1
      msgidfound=0
    }
    else if(msgidfound == 1)
    {
      # strip quotes (") from start&end
      gsub(/^"/, "")
      gsub(/"$/, "")

      msgid = msgid "\\\\\\n; " $0
    }
    else if(msgstrfound == 1)
    {
      # strip quotes (") from start&end
      gsub(/^"/, "")
      gsub(/"$/, "")

      msgstr = msgstr "\\\\\\n" $0
    }
  }
}
END {
  if(length(tag) != 0)
  {
    print tag
    print msgstr
    print msgid
    print ";"
  }
}
EOF

# convert from cd -> pot
#iconv -c -f iso-8859-1 -t utf8 ${CDFILE} | awk "${cd2pot}"

# convert from ct -> po
#iconv -c -f iso-8859-1 -t utf8 ${CDFILE} | awk "${ct2po}"
#iconv -c -f iso-8859-2 -t utf8 ${CDFILE} | awk "${ct2po}" # czech/polish/slovenian
#iconv -c -f windows-1251 -t utf8 ${CDFILE} | awk "${ct2po}" # russian
#iconv -c -f iso-8859-7 -t utf8 ${CDFILE} | awk "${ct2po}" # greek

# convert from po -> ct
#awk "${po2ct}" ${CDFILE} | iconv -c -f utf8 -t iso-8859-1

# convert from pot -> cd
awk "${pot2cd}" ${CDFILE} | iconv -c -f utf8 -t iso-8859-1

exit 0
