#ifndef DSDPIPE_EXPORT_H
#define DSDPIPE_EXPORT_H

/*
 * Auto-generated export header for libdsdpipe.
 * Controls symbol visibility for shared library builds.
 *
 * When building the library:  DSDPIPE_API = __declspec(dllexport)  [Windows]
 * When consuming the library: DSDPIPE_API = __declspec(dllimport)  [Windows]
 * On non-Windows with visibility: DSDPIPE_API = __attribute__((visibility("default")))
 * Otherwise (static builds):  DSDPIPE_API = (empty)
 */

#if defined(_WIN32) || defined(__CYGWIN__)
    #ifdef DSDPIPE_BUILDING_DLL
        #define DSDPIPE_API __declspec(dllexport)
    #elif defined(DSDPIPE_USING_DLL)
        #define DSDPIPE_API __declspec(dllimport)
    #else
        #define DSDPIPE_API
    #endif
#else
    #if defined(__GNUC__) && __GNUC__ >= 4
        #define DSDPIPE_API __attribute__((visibility("default")))
    #else
        #define DSDPIPE_API
    #endif
#endif

#endif /* DSDPIPE_EXPORT_H */
