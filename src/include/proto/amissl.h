#ifndef PROTO_AMISSL_H
#define PROTO_AMISSL_H

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif
#ifndef AMISSL_AMISSL_H
#include <amissl/amissl.h>
#endif

/****************************************************************************/

#ifndef __NOLIBBASE__
extern struct Library *AmiSSLBase;
#endif /* __NOLIBBASE__ */

/****************************************************************************/

#ifdef __amigaos4__
 #include <interfaces/amissl.h>
 #ifdef __USE_INLINE__
  #include <inline4/amissl.h>
 #endif /* __USE_INLINE__ */
 #ifndef CLIB_AMISSL_PROTOS_H
  #define CLIB_AMISSL_PROTOS_H 1
 #endif /* CLIB_AMISSL_PROTOS_H */
 #ifndef __NOGLOBALIFACE__
  extern struct AmiSSLIFace *IAmiSSL;
 #endif /* __NOGLOBALIFACE__ */
#else /* __amigaos4__ */
 #ifndef CLIB_AMISSL_PROTOS_H
  #include <clib/amissl_protos.h>
 #endif /* CLIB_AMISSL_PROTOS_H */
 #if defined(__GNUC__)
  #ifdef __AROS__
   #include <defines/amissl.h>
  #else
   #ifndef __PPC__
    #include <inline/amissl.h>
   #else
    #include <ppcinline/amissl.h>
   #endif /* __PPC__ */
  #endif /* __AROS__ */
 #elif defined(__VBCC__)
  #ifndef __PPC__
   #include <inline/amissl_protos.h>
  #endif /* __PPC__ */
 #else
  #include <pragmas/amissl_pragmas.h>
 #endif /* __GNUC__ */
#endif /* __amigaos4__ */

/****************************************************************************/

#endif /* PROTO_AMISSL_H */
