#ifndef Py_STACKLESS_H
#define Py_STACKLESS_H
#ifdef __cplusplus
extern "C" {
#endif

/* this option might go into a global config.
   It says "we want to compile stackless", but it
   is turned off again if our platform doesn't
   implement it.
 */

/****************************************************************************

  Stackless Internal Configuration

  Some preliminary description can be found in the draft document readme.txt

  The configuration is dependant of one #define:
  STACKLESS means that we want to create a stackless version.
  Without STACKLESS set, you will get the standard C-Python(r) application.
  If STACKLESS is set, the necessary fields and initializations are
  expanded, provided that the platform/compiler is supported by special
  header files. They implement the critical assembly part.
  If your platform is configured in this file but not supported,
  you will get a compiler error.

 ****************************************************************************/

/*
 * every platform needs to define its own interface here.
 * If this isn't defined, stackless is simply not compiled in.
 * Repeat the following sequence for every platform you support...
 * ...and then write your support code and mention it in the
 * common slp_platformselect.h file.
 */
#define STACKLESS

#ifdef STACKLESS_OFF
#undef STACKLESS
    /* an option to switch it off */
#elif defined(MS_WIN32) && !defined(MS_WIN64) && defined(_M_IX86)
    /* MS Visual Studio on X86 */
#   define SLP_USE_NATIVE_BITFIELD_LAYOUT 1
#elif defined(_WIN64) && defined(_M_X64)
    /* microsoft on 64 bit x64 thingies */
#   define SLP_USE_NATIVE_BITFIELD_LAYOUT 1
#elif defined(__GNUC__) && defined(__i386__)
    /* gcc on X86 */
#   define SLP_USE_NATIVE_BITFIELD_LAYOUT 1
#elif defined(__GNUC__) && defined(__amd64__)
    /* gcc on AMD64 */
#   define SLP_USE_NATIVE_BITFIELD_LAYOUT 1
#elif defined(__GNUC__) && defined(__PPC__) && defined(__linux__)
    /* gcc on PowerPC */
#elif defined(__GNUC__) && defined(__ppc__) && defined(__APPLE__)
    /* Apple MacOS X on PowerPC */
#elif defined(__GNUC__) && defined(sparc) && defined(sun)
    /* SunOs on sparc */
#elif defined(__GNUC__) && defined(__s390__) && defined(__linux__)
    /* Linux/S390 */
#elif defined(__GNUC__) && defined(__s390x__) && defined(__linux__)
    /* Linux/S390 zSeries */
#elif defined(__GNUC__) && defined(__mips__) && defined(__linux__)
    /* MIPS */
#elif defined(SLP_NO_STACKMAN)
    /* no supported platform */
#undef STACKLESS
#endif

#ifdef STACKLESS
#ifndef SLP_WITH_FRAME_REF_DEBUG
/* Control frame reference count debugging. That's a special mode
 * to find reference leaks during stack transfers and tasklet switching.
 * Usually you do not want to enable this mode.
 *
 * SLP_WITH_FRAME_REF_DEBUG must be either undefined, 1 or 2
 *  undefined: disable frame reference count debugging (default)
 *  1: normal frame reference count debugging
 *  2: heavy (slow) frame reference count debugging
 */
/* #define SLP_WITH_FRAME_REF_DEBUG 1 */
/* #define SLP_WITH_FRAME_REF_DEBUG 2 */
#endif  /* SLP_WITH_FRAME_REF_DEBUG */
#endif  /* STACKLESS */

#ifdef __cplusplus
}
#endif
#endif /* Py_STACKLESS_H */
