/*
 * Generated by util/mkerr.pl DO NOT EDIT
 * Copyright (c) 1999-2006 Andrija Antonijevic, Stefan Burstroem.
 * Copyright (c) 2014-2025 AmiSSL Open Source Team.
 * All Rights Reserved.
 *
 * This file has been modified for use with AmiSSL for AmigaOS-based systems.
 *
 * Copyright 1995-2024 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#if !defined(PROTO_AMISSL_H) && !defined(AMISSL_COMPILE)
# include <proto/amissl.h>
#endif

#ifndef OPENSSL_CRYPTOERR_H
# define OPENSSL_CRYPTOERR_H
# if defined(__GNUC__) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 3))
#  pragma once
# endif

# include <openssl/opensslconf.h>
# include <openssl/symhacks.h>
# include <openssl/cryptoerr_legacy.h>



/*
 * CRYPTO reason codes.
 */
# define CRYPTO_R_BAD_ALGORITHM_NAME                      117
# define CRYPTO_R_CONFLICTING_NAMES                       118
# define CRYPTO_R_HEX_STRING_TOO_SHORT                    121
# define CRYPTO_R_ILLEGAL_HEX_DIGIT                       102
# define CRYPTO_R_INSUFFICIENT_DATA_SPACE                 106
# define CRYPTO_R_INSUFFICIENT_PARAM_SIZE                 107
# define CRYPTO_R_INSUFFICIENT_SECURE_DATA_SPACE          108
# define CRYPTO_R_INTEGER_OVERFLOW                        127
# define CRYPTO_R_INVALID_NEGATIVE_VALUE                  122
# define CRYPTO_R_INVALID_NULL_ARGUMENT                   109
# define CRYPTO_R_INVALID_OSSL_PARAM_TYPE                 110
# define CRYPTO_R_NO_PARAMS_TO_MERGE                      131
# define CRYPTO_R_NO_SPACE_FOR_TERMINATING_NULL           128
# define CRYPTO_R_ODD_NUMBER_OF_DIGITS                    103
# define CRYPTO_R_PARAM_CANNOT_BE_REPRESENTED_EXACTLY     123
# define CRYPTO_R_PARAM_NOT_INTEGER_TYPE                  124
# define CRYPTO_R_PARAM_OF_INCOMPATIBLE_TYPE              129
# define CRYPTO_R_PARAM_UNSIGNED_INTEGER_NEGATIVE_VALUE_UNSUPPORTED 125
# define CRYPTO_R_PARAM_UNSUPPORTED_FLOATING_POINT_FORMAT 130
# define CRYPTO_R_PARAM_VALUE_TOO_LARGE_FOR_DESTINATION   126
# define CRYPTO_R_PROVIDER_ALREADY_EXISTS                 104
# define CRYPTO_R_PROVIDER_SECTION_ERROR                  105
# define CRYPTO_R_RANDOM_SECTION_ERROR                    119
# define CRYPTO_R_SECURE_MALLOC_FAILURE                   111
# define CRYPTO_R_STRING_TOO_LONG                         112
# define CRYPTO_R_TOO_MANY_BYTES                          113
# define CRYPTO_R_TOO_MANY_NAMES                          132
# define CRYPTO_R_TOO_MANY_RECORDS                        114
# define CRYPTO_R_TOO_SMALL_BUFFER                        116
# define CRYPTO_R_UNKNOWN_NAME_IN_RANDOM_SECTION          120
# define CRYPTO_R_ZERO_LENGTH_NUMBER                      115

#endif
