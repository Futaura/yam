/*
 * Generated by util/mkerr.pl DO NOT EDIT
 * Copyright (c) 1999-2006 Andrija Antonijevic, Stefan Burstroem.
 * Copyright (c) 2014-2022 AmiSSL Open Source Team.
 * All Rights Reserved.
 *
 * This file has been modified for use with AmiSSL for AmigaOS-based systems.
 *
 * Copyright 1995-2022 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#if !defined(PROTO_AMISSL_H) && !defined(AMISSL_COMPILE)
# include <proto/amissl.h>
#endif

#ifndef OPENSSL_ECERR_H
# define OPENSSL_ECERR_H
# if defined(__GNUC__) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 3))
#  pragma once
# endif

# include <openssl/opensslconf.h>
# include <openssl/symhacks.h>
# include <openssl/cryptoerr_legacy.h>


# ifndef OPENSSL_NO_EC


/*
 * EC reason codes.
 */
#  define EC_R_ASN1_ERROR                                  115
#  define EC_R_BAD_SIGNATURE                               156
#  define EC_R_BIGNUM_OUT_OF_RANGE                         144
#  define EC_R_BUFFER_TOO_SMALL                            100
#  define EC_R_CANNOT_INVERT                               165
#  define EC_R_COORDINATES_OUT_OF_RANGE                    146
#  define EC_R_CURVE_DOES_NOT_SUPPORT_ECDH                 160
#  define EC_R_CURVE_DOES_NOT_SUPPORT_ECDSA                170
#  define EC_R_CURVE_DOES_NOT_SUPPORT_SIGNING              159
#  define EC_R_DECODE_ERROR                                142
#  define EC_R_DISCRIMINANT_IS_ZERO                        118
#  define EC_R_EC_GROUP_NEW_BY_NAME_FAILURE                119
#  define EC_R_EXPLICIT_PARAMS_NOT_SUPPORTED               127
#  define EC_R_FAILED_MAKING_PUBLIC_KEY                    166
#  define EC_R_FIELD_TOO_LARGE                             143
#  define EC_R_GF2M_NOT_SUPPORTED                          147
#  define EC_R_GROUP2PKPARAMETERS_FAILURE                  120
#  define EC_R_I2D_ECPKPARAMETERS_FAILURE                  121
#  define EC_R_INCOMPATIBLE_OBJECTS                        101
#  define EC_R_INVALID_A                                   168
#  define EC_R_INVALID_ARGUMENT                            112
#  define EC_R_INVALID_B                                   169
#  define EC_R_INVALID_COFACTOR                            171
#  define EC_R_INVALID_COMPRESSED_POINT                    110
#  define EC_R_INVALID_COMPRESSION_BIT                     109
#  define EC_R_INVALID_CURVE                               141
#  define EC_R_INVALID_DIGEST                              151
#  define EC_R_INVALID_DIGEST_TYPE                         138
#  define EC_R_INVALID_ENCODING                            102
#  define EC_R_INVALID_FIELD                               103
#  define EC_R_INVALID_FORM                                104
#  define EC_R_INVALID_GENERATOR                           173
#  define EC_R_INVALID_GROUP_ORDER                         122
#  define EC_R_INVALID_KEY                                 116
#  define EC_R_INVALID_LENGTH                              117
#  define EC_R_INVALID_NAMED_GROUP_CONVERSION              174
#  define EC_R_INVALID_OUTPUT_LENGTH                       161
#  define EC_R_INVALID_P                                   172
#  define EC_R_INVALID_PEER_KEY                            133
#  define EC_R_INVALID_PENTANOMIAL_BASIS                   132
#  define EC_R_INVALID_PRIVATE_KEY                         123
#  define EC_R_INVALID_SEED                                175
#  define EC_R_INVALID_TRINOMIAL_BASIS                     137
#  define EC_R_KDF_PARAMETER_ERROR                         148
#  define EC_R_KEYS_NOT_SET                                140
#  define EC_R_LADDER_POST_FAILURE                         136
#  define EC_R_LADDER_PRE_FAILURE                          153
#  define EC_R_LADDER_STEP_FAILURE                         162
#  define EC_R_MISSING_OID                                 167
#  define EC_R_MISSING_PARAMETERS                          124
#  define EC_R_MISSING_PRIVATE_KEY                         125
#  define EC_R_NEED_NEW_SETUP_VALUES                       157
#  define EC_R_NOT_A_NIST_PRIME                            135
#  define EC_R_NOT_IMPLEMENTED                             126
#  define EC_R_NOT_INITIALIZED                             111
#  define EC_R_NO_PARAMETERS_SET                           139
#  define EC_R_NO_PRIVATE_VALUE                            154
#  define EC_R_OPERATION_NOT_SUPPORTED                     152
#  define EC_R_PASSED_NULL_PARAMETER                       134
#  define EC_R_PEER_KEY_ERROR                              149
#  define EC_R_POINT_ARITHMETIC_FAILURE                    155
#  define EC_R_POINT_AT_INFINITY                           106
#  define EC_R_POINT_COORDINATES_BLIND_FAILURE             163
#  define EC_R_POINT_IS_NOT_ON_CURVE                       107
#  define EC_R_RANDOM_NUMBER_GENERATION_FAILED             158
#  define EC_R_SHARED_INFO_ERROR                           150
#  define EC_R_SLOT_FULL                                   108
#  define EC_R_UNDEFINED_GENERATOR                         113
#  define EC_R_UNDEFINED_ORDER                             128
#  define EC_R_UNKNOWN_COFACTOR                            164
#  define EC_R_UNKNOWN_GROUP                               129
#  define EC_R_UNKNOWN_ORDER                               114
#  define EC_R_UNSUPPORTED_FIELD                           131
#  define EC_R_WRONG_CURVE_PARAMETERS                      145
#  define EC_R_WRONG_ORDER                                 130

# endif
#endif
