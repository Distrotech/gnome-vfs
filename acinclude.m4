AC_DEFUN(AM_GNOME_CHECK_TYPE,
  [AC_CACHE_CHECK([$1 in <sys/types.h>], ac_cv_type_$1,
     [AC_TRY_COMPILE([
#include <sys/types.h>
#if STDC_HEADERS
#include <stdlib.h>
#include <stddef.h>
#endif
],[$1 foo;],
     ac_cv_type_$1=yes, ac_cv_type_$1=no)])
   if test $ac_cv_type_$1 = no; then
      AC_DEFINE($1, $2, $1)
   fi
])

AC_DEFUN(AM_GNOME_SIZE_T,
  [AM_GNOME_CHECK_TYPE(size_t, unsigned)
   AC_PROVIDE([AC_TYPE_SIZE_T])
])

AC_DEFUN(AM_GNOME_OFF_T,
  [AM_GNOME_CHECK_TYPE(off_t, long)
   AC_PROVIDE([AC_TYPE_OFF_T])
])

#serial 2

dnl From Bruno Haible.

AC_DEFUN([jm_LANGINFO_CODESET],
[
  AC_CHECK_HEADERS(langinfo.h)
  AC_CHECK_FUNCS(nl_langinfo)

  AC_CACHE_CHECK([for nl_langinfo and CODESET], jm_cv_langinfo_codeset,
    [AC_TRY_LINK([#include <langinfo.h>],
      [char* cs = nl_langinfo(CODESET);],
      jm_cv_langinfo_codeset=yes,
      jm_cv_langinfo_codeset=no)
    ])
  if test $jm_cv_langinfo_codeset = yes; then
    AC_DEFINE(HAVE_LANGINFO_CODESET, 1,
      [Define if you have <langinfo.h> and nl_langinfo(CODESET).])
  fi
])
