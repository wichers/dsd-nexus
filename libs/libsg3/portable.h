#ifndef _PORTABLE_H_
#define _PORTABLE_H_

#if defined(HAVE_CONFIG_H) && !defined(__CONFIG_H__)
# include "config.h"
# define __CONFIG_H__ 1
#endif

#if !defined(HAVE_FTRUNCATE)
# if defined (_WIN32)
#  define ftruncate chsize
# endif
#endif /*HAVE_FTRUNCATE*/

#if !defined(HAVE_SNPRINTF)
# if defined (_MSC_VER)
#  define snprintf _snprintf
# endif
#endif /*HAVE_SNPRINTF*/

#if !defined(HAVE_VSNPRINTF)
# if defined (_MSC_VER)
#  define vsnprintf _vsnprintf
# endif
#endif /*HAVE_SNPRINTF*/

#if !defined(HAVE_DRAND48) && defined(HAVE_RAND)
# define drand48()   (rand() / (double)RAND_MAX)
#endif

#endif /* _PORTABLE_H_ */
