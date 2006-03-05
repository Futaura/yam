/* crypto/evp/evp.h */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 * 
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 * 
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from 
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 * 
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#ifndef HEADER_ENVELOPE_H
#define HEADER_ENVELOPE_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <amissl/md2.h>
#include <amissl/md5.h>
#include <amissl/sha.h>
#include <amissl/ripemd.h>
#include <amissl/des.h>
#include <amissl/rc4.h>
#include <amissl/rc2.h>
#include <amissl/rc5.h>
#include <amissl/blowfish.h>
#include <amissl/cast.h>
#include <amissl/idea.h>
#include <amissl/mdc2.h>

#define EVP_RC2_KEY_SIZE		16
#define EVP_RC4_KEY_SIZE		16
#define EVP_BLOWFISH_KEY_SIZE		16
#define EVP_CAST5_KEY_SIZE		16
#define EVP_RC5_32_12_16_KEY_SIZE	16
#define EVP_MAX_MD_SIZE			(16+20) /* The SSLv3 md5+sha1 type */
#define EVP_MAX_KEY_LENGTH		24
#define EVP_MAX_IV_LENGTH		8

#define PKCS5_SALT_LEN			8
/* Default PKCS#5 iteration count */
#define PKCS5_DEFAULT_ITER		2048

#include <amissl/rsa.h>
#include <amissl/dsa.h>
#include <amissl/dh.h>
#include <amissl/objects.h>

#define EVP_PK_RSA	0x0001
#define EVP_PK_DSA	0x0002
#define EVP_PK_DH	0x0004
#define EVP_PKT_SIGN	0x0010
#define EVP_PKT_ENC	0x0020
#define EVP_PKT_EXCH	0x0040
#define EVP_PKS_RSA	0x0100
#define EVP_PKS_DSA	0x0200
#define EVP_PKT_EXP	0x1000 /* <= 512 bit key */

#define EVP_PKEY_NONE	NID_undef
#define EVP_PKEY_RSA	NID_rsaEncryption
#define EVP_PKEY_RSA2	NID_rsa
#define EVP_PKEY_DSA	NID_dsa
#define EVP_PKEY_DSA1	NID_dsa_2
#define EVP_PKEY_DSA2	NID_dsaWithSHA
#define EVP_PKEY_DSA3	NID_dsaWithSHA1
#define EVP_PKEY_DSA4	NID_dsaWithSHA1_2
#define EVP_PKEY_DH	NID_dhKeyAgreement

/* Type needs to be a bit field
 * Sub-type needs to be for variations on the method, as in, can it do
 * arbitary encryption.... */
typedef struct evp_pkey_st
	{
	int type;
	int save_type;
	int references;
	union	{
		char *ptr;
		struct rsa_st *rsa;	/* RSA */
		struct dsa_st *dsa;	/* DSA */
		struct dh_st *dh;	/* DH */
		} pkey;
	int save_parameters;
	STACK /*X509_ATTRIBUTE*/ *attributes; /* [ 0 ] */
	} EVP_PKEY;

#define EVP_PKEY_MO_SIGN	0x0001
#define EVP_PKEY_MO_VERIFY	0x0002
#define EVP_PKEY_MO_ENCRYPT	0x0004
#define EVP_PKEY_MO_DECRYPT	0x0008

typedef struct env_md_st
	{
	int type;
	int pkey_type;
	int md_size;
	void (*init)();
	void (*update)();
	void (*final)();

	int (*sign)();
	int (*verify)();
	int required_pkey_type[5]; /*EVP_PKEY_xxx */
	int block_size;
	int ctx_size; /* how big does the ctx need to be */
	} EVP_MD;



#define EVP_PKEY_NULL_method	NULL,NULL,{0,0,0,0}

#define EVP_PKEY_DSA_method	DSA_sign,DSA_verify, \
				{EVP_PKEY_DSA,EVP_PKEY_DSA2,EVP_PKEY_DSA3, \
					EVP_PKEY_DSA4,0}
#define EVP_PKEY_RSA_method	RSA_sign,RSA_verify, \
				{EVP_PKEY_RSA,EVP_PKEY_RSA2,0,0}
#define EVP_PKEY_RSA_ASN1_OCTET_STRING_method \
				RSA_sign_ASN1_OCTET_STRING, \
				RSA_verify_ASN1_OCTET_STRING, \
				{EVP_PKEY_RSA,EVP_PKEY_RSA2,0,0}

typedef struct env_md_ctx_st
	{
	const EVP_MD *digest;
	union	{
		unsigned char base[4];
		MD2_CTX md2;
		MD5_CTX md5;
		RIPEMD160_CTX ripemd160;
		SHA_CTX sha;
		MDC2_CTX mdc2;
		} md;
	} EVP_MD_CTX;

typedef struct evp_cipher_st
	{
	int nid;
	int block_size;
	int key_len;
	int iv_len;
	void (*init)();		/* init for encryption */
	void (*do_cipher)();	/* encrypt data */
	void (*cleanup)();	/* used by cipher method */ 
	int ctx_size;		/* how big the ctx needs to be */
	/* int set_asn1_parameters(EVP_CIPHER_CTX,ASN1_TYPE *); */
	int (*set_asn1_parameters)(); /* Populate a ASN1_TYPE with parameters */
	/* int get_asn1_parameters(EVP_CIPHER_CTX,ASN1_TYPE *); */
	int (*get_asn1_parameters)(); /* Get parameters from a ASN1_TYPE */
	} EVP_CIPHER;

typedef struct evp_cipher_info_st
	{
	const EVP_CIPHER *cipher;
	unsigned char iv[EVP_MAX_IV_LENGTH];
	} EVP_CIPHER_INFO;

typedef struct evp_cipher_ctx_st
	{
	const EVP_CIPHER *cipher;
	int encrypt;		/* encrypt or decrypt */
	int buf_len;		/* number we have left */

	unsigned char  oiv[EVP_MAX_IV_LENGTH];	/* original iv */
	unsigned char  iv[EVP_MAX_IV_LENGTH];	/* working iv */
	unsigned char buf[EVP_MAX_IV_LENGTH];	/* saved partial block */
	int num;				/* used by cfb/ofb mode */

	char *app_data;		/* aplication stuff */
	union	{
		struct
			{
			unsigned char key[EVP_RC4_KEY_SIZE];
			RC4_KEY ks;	/* working key */
			} rc4;
		des_key_schedule des_ks;/* key schedule */
		struct
			{
			des_key_schedule ks;/* key schedule */
			des_cblock inw;
			des_cblock outw;
			} desx_cbc;
		struct
			{
			des_key_schedule ks1;/* key schedule */
			des_key_schedule ks2;/* key schedule (for ede) */
			des_key_schedule ks3;/* key schedule (for ede3) */
			} des_ede;
		IDEA_KEY_SCHEDULE idea_ks;/* key schedule */
		RC2_KEY rc2_ks;/* key schedule */
		RC5_32_KEY rc5_ks;/* key schedule */
		BF_KEY bf_ks;/* key schedule */
		CAST_KEY cast_ks;/* key schedule */
		} c;
	} EVP_CIPHER_CTX;

typedef struct evp_Encode_Ctx_st
	{
	int num;	/* number saved in a partial encode/decode */
	int length;	/* The length is either the output line length
			 * (in input bytes) or the shortest input line
			 * length that is ok.  Once decoding begins,
			 * the length is adjusted up each time a longer
			 * line is decoded */
	unsigned char enc_data[80];	/* data to encode */
	int line_num;	/* number read on current line */
	int expect_nl;
	} EVP_ENCODE_CTX;

/* Password based encryption function */
typedef int (EVP_PBE_KEYGEN)(EVP_CIPHER_CTX *ctx, const char *pass, int passlen,
		ASN1_TYPE *param, EVP_CIPHER *cipher,
                EVP_MD *md, int en_de);

#define EVP_PKEY_assign_RSA(pkey,rsa) EVP_PKEY_assign((pkey),EVP_PKEY_RSA,\
					(char *)(rsa))
#define EVP_PKEY_assign_DSA(pkey,dsa) EVP_PKEY_assign((pkey),EVP_PKEY_DSA,\
					(char *)(dsa))
#define EVP_PKEY_assign_DH(pkey,dh) EVP_PKEY_assign((pkey),EVP_PKEY_DH,\
					(char *)(dh))

/* Add some extra combinations */
#define EVP_get_digestbynid(a) EVP_get_digestbyname(OBJ_nid2sn(a))
#define EVP_get_digestbyobj(a) EVP_get_digestbynid(OBJ_obj2nid(a))
#define EVP_get_cipherbynid(a) EVP_get_cipherbyname(OBJ_nid2sn(a))
#define EVP_get_cipherbyobj(a) EVP_get_cipherbynid(OBJ_obj2nid(a))

#define EVP_MD_type(e)			((e)->type)
#define EVP_MD_pkey_type(e)		((e)->pkey_type)
#define EVP_MD_size(e)			((e)->md_size)
#define EVP_MD_block_size(e)		((e)->block_size)

#define EVP_MD_CTX_size(e)		EVP_MD_size((e)->digest)
#define EVP_MD_CTX_block_size(e)	EVP_MD_block_size((e)->digest)
#define EVP_MD_CTX_type(e)		((e)->digest)

#define EVP_CIPHER_nid(e)		((e)->nid)
#define EVP_CIPHER_block_size(e)	((e)->block_size)
#define EVP_CIPHER_key_length(e)	((e)->key_len)
#define EVP_CIPHER_iv_length(e)		((e)->iv_len)

#define EVP_CIPHER_CTX_cipher(e)	((e)->cipher)
#define EVP_CIPHER_CTX_nid(e)		((e)->cipher->nid)
#define EVP_CIPHER_CTX_block_size(e)	((e)->cipher->block_size)
#define EVP_CIPHER_CTX_key_length(e)	((e)->cipher->key_len)
#define EVP_CIPHER_CTX_iv_length(e)	((e)->cipher->iv_len)
#define EVP_CIPHER_CTX_get_app_data(e)	((e)->app_data)
#define EVP_CIPHER_CTX_set_app_data(e,d) ((e)->app_data=(char *)(d))
#define EVP_CIPHER_CTX_type(c)         EVP_CIPHER_type(EVP_CIPHER_CTX_cipher(c))

#define EVP_ENCODE_LENGTH(l)	(((l+2)/3*4)+(l/48+1)*2+80)
#define EVP_DECODE_LENGTH(l)	((l+3)/4*3+80)

#define EVP_SignInit(a,b)		EVP_DigestInit(a,b)
#define EVP_SignUpdate(a,b,c)		EVP_DigestUpdate(a,b,c)
#define	EVP_VerifyInit(a,b)		EVP_DigestInit(a,b)
#define	EVP_VerifyUpdate(a,b,c)		EVP_DigestUpdate(a,b,c)
#define EVP_OpenUpdate(a,b,c,d,e)	EVP_DecryptUpdate(a,b,c,d,e)
#define EVP_SealUpdate(a,b,c,d,e)	EVP_EncryptUpdate(a,b,c,d,e)	

#define BIO_set_md(b,md)		BIO_ctrl(b,BIO_C_SET_MD,0,(char *)md)
#define BIO_get_md(b,mdp)		BIO_ctrl(b,BIO_C_GET_MD,0,(char *)mdp)
#define BIO_get_md_ctx(b,mdcp)     BIO_ctrl(b,BIO_C_GET_MD_CTX,0,(char *)mdcp)
#define BIO_get_cipher_status(b)	BIO_ctrl(b,BIO_C_GET_CIPHER_STATUS,0,NULL)
#define BIO_get_cipher_ctx(b,c_pp)	BIO_ctrl(b,BIO_C_GET_CIPHER_CTX,0,(char *)c_pp)

#define	EVP_Cipher(c,o,i,l)	(c)->cipher->do_cipher((c),(o),(i),(l))

#define EVP_add_cipher_alias(n,alias) \
	OBJ_NAME_add((alias),OBJ_NAME_TYPE_CIPHER_METH|OBJ_NAME_ALIAS,(n))
#define EVP_add_digest_alias(n,alias) \
	OBJ_NAME_add((alias),OBJ_NAME_TYPE_MD_METH|OBJ_NAME_ALIAS,(n))
#define EVP_delete_cipher_alias(alias) \
	OBJ_NAME_remove(alias,OBJ_NAME_TYPE_CIPHER_METH|OBJ_NAME_ALIAS);
#define EVP_delete_digest_alias(alias) \
	OBJ_NAME_remove(alias,OBJ_NAME_TYPE_MD_METH|OBJ_NAME_ALIAS);

/* BEGIN ERROR CODES */
/* The following lines are auto generated by the script mkerr.pl. Any changes
 * made after this point may be overwritten when the script is next run.
 */

/* Error codes for the EVP functions. */

/* Function codes. */
#define EVP_F_D2I_PKEY					 100
#define EVP_F_EVP_DECRYPTFINAL				 101
#define EVP_F_EVP_MD_CTX_COPY				 110
#define EVP_F_EVP_OPENINIT				 102
#define EVP_F_EVP_PBE_ALG_ADD				 115
#define EVP_F_EVP_PBE_CIPHERINIT			 116
#define EVP_F_EVP_PKCS82PKEY				 111
#define EVP_F_EVP_PKCS8_SET_BROKEN			 112
#define EVP_F_EVP_PKEY2PKCS8				 113
#define EVP_F_EVP_PKEY_COPY_PARAMETERS			 103
#define EVP_F_EVP_PKEY_DECRYPT				 104
#define EVP_F_EVP_PKEY_ENCRYPT				 105
#define EVP_F_EVP_PKEY_NEW				 106
#define EVP_F_EVP_SIGNFINAL				 107
#define EVP_F_EVP_VERIFYFINAL				 108
#define EVP_F_PKCS5_PBE_KEYIVGEN			 117
#define EVP_F_PKCS5_V2_PBE_KEYIVGEN			 118
#define EVP_F_RC2_MAGIC_TO_METH				 109

/* Reason codes. */
#define EVP_R_BAD_DECRYPT				 100
#define EVP_R_BN_DECODE_ERROR				 112
#define EVP_R_BN_PUBKEY_ERROR				 113
#define EVP_R_CIPHER_PARAMETER_ERROR			 122
#define EVP_R_DECODE_ERROR				 114
#define EVP_R_DIFFERENT_KEY_TYPES			 101
#define EVP_R_ENCODE_ERROR				 115
#define EVP_R_EVP_PBE_CIPHERINIT_ERROR			 119
#define EVP_R_INPUT_NOT_INITIALIZED			 111
#define EVP_R_IV_TOO_LARGE				 102
#define EVP_R_KEYGEN_FAILURE				 120
#define EVP_R_MISSING_PARMATERS				 103
#define EVP_R_NO_DSA_PARAMETERS				 116
#define EVP_R_NO_SIGN_FUNCTION_CONFIGURED		 104
#define EVP_R_NO_VERIFY_FUNCTION_CONFIGURED		 105
#define EVP_R_PKCS8_UNKNOWN_BROKEN_TYPE			 117
#define EVP_R_PUBLIC_KEY_NOT_RSA			 106
#define EVP_R_UNKNOWN_PBE_ALGORITHM			 121
#define EVP_R_UNSUPPORTED_CIPHER			 107
#define EVP_R_UNSUPPORTED_KEYLENGTH			 123
#define EVP_R_UNSUPPORTED_KEY_DERIVATION_FUNCTION	 124
#define EVP_R_UNSUPPORTED_KEY_SIZE			 108
#define EVP_R_UNSUPPORTED_PRF				 125
#define EVP_R_UNSUPPORTED_PRIVATE_KEY_ALGORITHM		 118
#define EVP_R_UNSUPPORTED_SALT_TYPE			 126
#define EVP_R_WRONG_FINAL_BLOCK_LENGTH			 109
#define EVP_R_WRONG_PUBLIC_KEY_TYPE			 110

#ifdef  __cplusplus
}
#endif
#endif

