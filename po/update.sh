#!/bin/sh

xgettext --default-domain=gnome-vfs --directory=.. \
  --add-comments --keyword=_ --keyword=N_ \
  --files-from=./POTFILES.in \
&& test ! -f gnome-vfs.po \
   || ( rm -f ./gnome-vfs.pot \
    && mv gnome-vfs.po ./gnome-vfs.pot )
