#ifndef DSDIFF_EXPORT_H
#define DSDIFF_EXPORT_H

/*
 * Auto-generated export header for libdsdiff.
 * Controls symbol visibility for shared library builds.
 *
 * When building the library:  DSDIFF_API = __declspec(dllexport)  [Windows]
 * When consuming the library: DSDIFF_API = __declspec(dllimport)  [Windows]
 * On non-Windows with visibility: DSDIFF_API = __attribute__((visibility("default")))
 * Otherwise (static builds):  DSDIFF_API = (empty)
 */

#if defined(_WIN32) || defined(__CYGWIN__)
    #ifdef DSDIFF_BUILDING_DLL
        #define DSDIFF_API __declspec(dllexport)
    #elif defined(DSDIFF_USING_DLL)
        #define DSDIFF_API __declspec(dllimport)
    #else
        #define DSDIFF_API
    #endif
#else
    #if defined(__GNUC__) && __GNUC__ >= 4
        #define DSDIFF_API __attribute__((visibility("default")))
    #else
        #define DSDIFF_API
    #endif
#endif

#endif /* DSDIFF_EXPORT_H */
