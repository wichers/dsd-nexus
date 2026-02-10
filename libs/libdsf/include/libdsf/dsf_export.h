#ifndef DSF_EXPORT_H
#define DSF_EXPORT_H

/*
 * Auto-generated export header for libdsf.
 * Controls symbol visibility for shared library builds.
 *
 * When building the library:  DSF_API = __declspec(dllexport)  [Windows]
 * When consuming the library: DSF_API = __declspec(dllimport)  [Windows]
 * On non-Windows with visibility: DSF_API = __attribute__((visibility("default")))
 * Otherwise (static builds):  DSF_API = (empty)
 */

#if defined(_WIN32) || defined(__CYGWIN__)
    #ifdef DSF_BUILDING_DLL
        #define DSF_API __declspec(dllexport)
    #elif defined(DSF_USING_DLL)
        #define DSF_API __declspec(dllimport)
    #else
        #define DSF_API
    #endif
#else
    #if defined(__GNUC__) && __GNUC__ >= 4
        #define DSF_API __attribute__((visibility("default")))
    #else
        #define DSF_API
    #endif
#endif

#endif /* DSF_EXPORT_H */
