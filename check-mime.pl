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
#     types in gnome-vfs.keys are in alphabetical order
#     types in gnome-vfs.mime are in alphabetical order
#     all types in gnome-vfs.mime have descriptions in gnome-vfs.keys
#     all types in gnome-vfs-mime-magic has descriptions in gnome-vfs.keys

# Other things to check later:
#     something about the gnome-vfs.applications file
#     OAFIIDs are consistent (same UUID part for same prefix)

use diagnostics;
use strict;

my %seen;

print STDERR "Reading gnome-vfs.keys.\n";

my $previous_type = "";
my $type;
my %in_keys;
my %described;

open KEYS, "data/mime/gnome-vfs.keys" or die;
while (<KEYS>)
  {
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
	    print "- $type is after $previous_type in gnome-vfs.keys\n";
	  }
	$seen{$type} = 1;
	$in_keys{$type} = 1;
      }
    elsif (/^\s*description=/)
      {
	if ($type eq "")
	  {
	    print "- description outside any type at line $.\n";
	  }
	$described{$type} = 1;
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
		print "- $type is in gnome-vfs.mime, but not gnome-vfs.keys\n";
	      }
	    else
	      {
		print "- $type is in gnome-vfs.mime, but has no description in gnome-vfs.keys\n";
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
		print "- $type is in gnome-vfs.mime, but not gnome-vfs.keys\n";
	      }
	    else
	      {
		print "- $type is in gnome-vfs.mime, but has no description in gnome-vfs.keys\n";
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
