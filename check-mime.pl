#!/usr/bin/perl -w
# -*- Mode: perl; indent-tabs-mode: nil -*-

#
#  GNOME VFS
#
#  Copyright (C) 2000 Eazel, Inc.
#
#  This library is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License as
#  published by the Free Software Foundation; either version 2 of the
#  License, or (at your option) any later version.
#
#  This library is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this library; if not, write to the Free Software
#  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
#  Author: Darin Adler <darin@eazel.com>,
#

# check-mime.pl: 

# What we check:
#     types are lower-case in all files
#     types in gnome-vfs.keys.in are in alphabetical order
#     types in gnome-vfs.mime are in alphabetical order
#     types in gnome-vfs.mime have descriptions in gnome-vfs.keys.in
#     types in gnome-vfs-mime-magic have descriptions in gnome-vfs.keys.in
#     types in gnome-vfs.applications have descriptions in gnome-vfs.keys.in

# Other things to check later:
#     OAFIIDs are consistent (same UUID part for same prefix)
#     some way of detecting gnome-vfs.keys.in entries for nonexistent MIME types

use diagnostics;
use strict;

my %seen;

print STDERR "Reading gnome-vfs.keys.in.\n";

my $previous_type = "";
my $type;
my %in_keys;
my %described;

open KEYS, "data/mime/gnome-vfs.keys.in" or die;
while (<KEYS>)
  {
    chomp;
    if (/^\s+$/)
      {
        print "- blank line with spaces on line $.\n";
      }
    else
      {
        if (/^ /)
          {
            print "- leading spaces used instead of a tab on line $.\n";
          }
        if (/\s$/)
          {
            print "- trailing whitespace on line $.\n";
          }
        if (/\t /)
          {
            print "- space after tab on line $.\n";
          }
        if (/  /)
          {
            print "- multiple spaces on line $.\n";
          }
      }
    next if /^\s*\#/;
    if (/\s=/)
      {
        print "- space before = on line $.\n";
      }
    if (/=\s/)
      {
        print "- space after = on line $.\n";
      }
    if (/^(\S.*)/)
      {
	$previous_type = $type if $type;
	$type = $1;
	if ($type ne lc $type && !$seen{$type})
	  {
	    print "- $type contains upper-case letters\n";
	  }
	if (lc $type le lc $previous_type)
	  {
	    print "- $type is after $previous_type\n";
	  }
	$seen{$type} = 1;
	$in_keys{$type} = 1;
      }
    elsif (/^\s*_description=/)
      {
	if ($type eq "")
	  {
	    print "- description outside any type at line $.\n";
	  }
	$described{$type} = 1;
      }
    elsif (/^\s*icon_filename\s*=\s*i-regular/)
      {
        if ($type eq "")
          {
            print "- i-regular specified explicitly at line $.\n";
          }
        else
          {
            print "- i-regular specified for $type at line $.\n";
          }
      }
    elsif (/^\s*$/)
      {
	$previous_type = $type if $type;
	$type = "";
      }
  }
close KEYS;

print STDERR "Reading gnome-vfs-mime-magic.\n";

my %in_magic;

open MAGIC, "data/mime/gnome-vfs-mime-magic" or die;
while (<MAGIC>)
  {
    chomp;
    if (/^\s+$/)
      {
        print "- blank line with spaces on line $.\n";
      }
    else
      {
        if (/^ /)
          {
            print "- leading spaces used instead of a tab on line $.\n";
          }
        if (/\s$/)
          {
            print "- trailing whitespace on line $.\n";
          }
        if (/\t /)
          {
            print "- space after tab on line $.\n";
          }
      }
    next if /^\s*\#/;
    next if /^\s*$/;
    if (/^[0-9:]+\s+[a-z]+\s+([^\\\s]|\\.)+\s+(&\s*\S+\s+)?(\S+)\s*$/)
      {
	$type = $3;
	if ($type ne lc $type && !$seen{$type})
	  {
	    print "- $type contains upper-case letters\n";
	  }
	if (!$described{$type} && !$in_magic{$type})
	  {
	    if (!$in_keys{$type})
	      {
		print "- $type is in gnome-vfs-mime-magic, but not gnome-vfs.keys.in\n";
	      }
	    else
	      {
		print "- $type is in gnome-vfs-mime-magic, but has no description in gnome-vfs.keys.in\n";
	      }
	  }
	$seen{$type} = 1;
	$in_magic{$type} = 1;
      }
    else
      {
	print "Could not parse line in gnome-vfs-mime-magic:\n$_";
      }
  }
close MAGIC;

print STDERR "Reading gnome-vfs.mime.\n";

$previous_type = "";
$type = "";
my %in_mime;

open MIME, "data/mime/gnome-vfs.mime" or die;
while (<MIME>)
  {
    chomp;
    if (/^\s+$/)
      {
        print "- blank line with spaces on line $.\n";
      }
    else
      {
        if (/^ /)
          {
            print "- leading spaces used instead of a tab on line $.\n";
          }
        if (/\s$/)
          {
            print "- trailing whitespace on line $.\n";
          }
        if (/\t /)
          {
            print "- space after tab on line $.\n";
          }
        if (/  /)
          {
            print "- multiple spaces on line $.\n";
          }
      }
    next if /^\s*\#/;
    if (/^(\S.*)/)
      {
	$previous_type = $type if $type;
	$type = $1;
	if ($type ne lc $type && !$seen{$type})
	  {
	    print "- $type contains upper-case letters\n";
	  }
	if (lc $type le lc $previous_type)
	  {
	    print "- $type is after $previous_type in gnome-vfs.mime\n";
	  }
	if (!$described{$type} && !$in_mime{$type})
	  {
	    if (!$in_keys{$type})
	      {
		print "- $type is in gnome-vfs.mime, but not gnome-vfs.keys.in\n";
	      }
	    else
	      {
		print "- $type is in gnome-vfs.mime, but has no description in gnome-vfs.keys.in\n";
	      }
	  }
	$seen{$type} = 1;
	$in_mime{$type} = 1;
      }
    elsif (/^\s*$/)
      {
	$previous_type = $type if $type;
	$type = "";
      }
  }
close MIME;

print STDERR "Reading gnome-vfs.applications.\n";

my %in_applications;

# special case for text/* for now
# could put in fancier handling later
$in_applications{"text/*"} = 1;

open MIME, "data/mime/gnome-vfs.applications" or die;
while (<MIME>)
  {
    chomp;
    if (/^\s+$/)
      {
        print "- blank line with spaces on line $.\n";
      }
    else
      {
        if (/^ /)
          {
            print "- leading spaces used instead of a tab on line $.\n";
          }
        if (/\s$/)
          {
            print "- trailing whitespace on line $.\n";
          }
        if (/\t /)
          {
            print "- space after tab on line $.\n";
          }
        if (/  /)
          {
            print "- multiple spaces on line $.\n";
          }
      }
    if (/\s=/)
      {
        print "- space before = on line $.\n";
      }
    if (/=\s/)
      {
        print "- space after = on line $.\n";
      }
    if (/^\s+mime_types=(.*)/)
      {
        foreach my $type (split ",", $1)
          {
            next if $in_applications{$type};

            if ($type ne lc $type && !$seen{$type})
              {
                print "- $type contains upper-case letters\n";
              }
            if (!$described{$type} && !$in_applications{$type})
              {
                if (!$in_keys{$type})
                  {
                    print "- $type is in gnome-vfs.applications, but not gnome-vfs.keys.in\n";
                  }
                else
                  {
                    print "- $type is in gnome-vfs.applications, but has no description in gnome-vfs.keys.in\n";
                  }
              }
            $seen{$type} = 1;
            $in_applications{$type} = 1;
          }
      }
  }
close MIME;
