#ifndef SACDVFS_EXPORT_H
#define SACDVFS_EXPORT_H

/*
 * Auto-generated export header for libsacdvfs.
 * Controls symbol visibility for shared library builds.
 *
 * When building the library:  SACDVFS_API = __declspec(dllexport)  [Windows]
 * When consuming the library: SACDVFS_API = __declspec(dllimport)  [Windows]
 * On non-Windows with visibility: SACDVFS_API = __attribute__((visibility("default")))
 * Otherwise (static builds):  SACDVFS_API = (empty)
 */

#if defined(_WIN32) || defined(__CYGWIN__)
    #ifdef SACDVFS_BUILDING_DLL
        #define SACDVFS_API __declspec(dllexport)
    #elif defined(SACDVFS_USING_DLL)
        #define SACDVFS_API __declspec(dllimport)
    #else
        #define SACDVFS_API
    #endif
#else
    #if defined(__GNUC__) && __GNUC__ >= 4
        #define SACDVFS_API __attribute__((visibility("default")))
    #else
        #define SACDVFS_API
    #endif
#endif

#endif /* SACDVFS_EXPORT_H */
