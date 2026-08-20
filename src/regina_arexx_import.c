/*
 * Regina's ARexx IMPORT shim for ACE.
 *
 * Regina represents an ARexx pointer as sizeof(void *) raw bytes in the
 * inline value union of a streng. The upstream Amiga helper used the union's
 * pointer spelling as if it were a pointer-to-pointer, which dereferences the
 * supplied buffer on a host build. Keep the normal Regina semantics for its
 * own allocations, while validating pointers produced by ACE's
 * rexxsupport.library before copying them.
 */

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "rexx.h"
#include "extern.h"

streng *ace_regina_arexx_import( tsd_t *TSD, cparamboxptr parm1 )
{
  void *memptr;
  cparamboxptr parm2;
  int len, error, memory_status;
  size_t bounded_len;

  checkparam( parm1, 1, 2, "IMPORT" );

  if ( parm1->value->len != sizeof(void *) )
    exiterror( ERR_INCORRECT_CALL, 0 );

  memcpy( &memptr, parm1->value->value, sizeof(memptr) );
  if ( memptr == NULL )
    exiterror( ERR_INCORRECT_CALL, 0 );

  parm2 = parm1->next;
  if ( parm2 == NULL || parm2->value == NULL || parm2->value->len == 0 )
  {
    memory_status = ace_rexxsupport_memory_cstring_length( memptr,
                                                            &bounded_len );
    if ( memory_status < 0 || (memory_status > 0 && bounded_len > INT_MAX) )
      exiterror( ERR_INCORRECT_CALL, 0 );
    len = memory_status > 0 ? (int)bounded_len : strlen((char *)memptr);
  }
  else
  {
    len = streng_to_int( TSD, parm2->value, &error );
    if ( error )
      exiterror( ERR_INCORRECT_CALL, 11, "IMPORT", 2,
                 tmpstr_of( TSD, parm2->value ) );
    if ( len <= 0 )
      exiterror( ERR_INCORRECT_CALL, 14, "IMPORT", 2,
                 tmpstr_of( TSD, parm2->value ) );
    if ( ace_rexxsupport_memory_status( memptr, (size_t)len ) < 0 )
      exiterror( ERR_INCORRECT_CALL, 0 );
  }

  return Str_ncre_TSD( TSD, (const char *)memptr, len );
}
