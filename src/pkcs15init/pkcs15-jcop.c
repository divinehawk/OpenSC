/*
 * JCOP specific operation for PKCS15 initialization
 *
 * Copyright 2003 Chaskiel Grundman <cg2v@andrew.cmu.edu>
 * Copyright (C) 2002 Olaf Kirch <okir@lst.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <opensc/opensc.h>
#include <opensc/cardctl.h>
#include <opensc/log.h>
#include "pkcs15-init.h"
#include "profile.h"


#define JCOP_MAX_PINS            3

/*
 * Erase the card
 */
static int
jcop_erase_card(struct sc_profile *pro, struct sc_card *card) {
     /* later */
     return SC_ERROR_NOT_SUPPORTED;
}

/*
 * Create a new DF
 * This will usually be the application DF
 * for JCOP, it must be the application DF. no other DF's may exist.
 */
static int
jcop_init_app(sc_profile_t *profile, sc_card_t *card, 
	      struct sc_pkcs15_pin_info *pin_info,
	      const u8 *pin, size_t pin_len, const u8 *puk, size_t puk_len) {
     return SC_ERROR_NOT_SUPPORTED;
}

/*
 * Select a PIN reference
 */
static int
jcop_select_pin_reference(sc_profile_t *profile, sc_card_t *card,
                sc_pkcs15_pin_info_t *pin_info) {
        int     preferred, current;

        if ((current = pin_info->reference) < 0)
                current = 0;

        if (pin_info->flags & SC_PKCS15_PIN_FLAG_SO_PIN) {
	     preferred = 3;
	} else {
	     preferred = current;
	      if (preferred < 1)
		   preferred=1;
	      if (preferred > 2)
		   return SC_ERROR_TOO_MANY_OBJECTS;
	}
	if (current > preferred)
	     return SC_ERROR_TOO_MANY_OBJECTS;
        pin_info->reference = preferred;
        return 0;
}

/*
 * Store a PIN
 */
static int
jcop_create_pin(sc_profile_t *profile, sc_card_t *card, sc_file_t *df,
                sc_pkcs15_object_t *pin_obj,
                const unsigned char *pin, size_t pin_len,
                const unsigned char *puk, size_t puk_len)
{
        sc_pkcs15_pin_info_t *pin_info = (sc_pkcs15_pin_info_t *) pin_obj->data;
        unsigned char   nulpin[16];
        unsigned char   padpin[16];
        int             r, type;

        if (pin_info->flags & SC_PKCS15_PIN_FLAG_SO_PIN) {
                type = SC_PKCS15INIT_SO_PIN;

                /* SO PIN reference must be 0 */
                if (pin_info->reference != 3)
                        return SC_ERROR_INVALID_ARGUMENTS;
        } else {
                type = SC_PKCS15INIT_USER_PIN;
                if (pin_info->reference >= 3)
                        return SC_ERROR_TOO_MANY_OBJECTS;
        }
        if (puk != NULL && puk_len > 0) {
	     return SC_ERROR_NOT_SUPPORTED;
	}
	r = sc_select_file(card, &df->path, NULL);
        if (r < 0)
	     return r;

	/* Current PIN is 00:00:00:00:00:00:00:00... */
        memset(nulpin, 0, sizeof(nulpin));
        memset(padpin, 0, sizeof(padpin));
	memcpy(padpin, pin, pin_len);
        r = sc_change_reference_data(card, SC_AC_CHV,
                        pin_info->reference,
                        nulpin, sizeof(nulpin),
                        padpin, sizeof(padpin), NULL);
        if (r < 0)
                return r;


	     
        sc_keycache_set_pin_name(&df->path,
                        pin_info->reference,
                        type);
	pin_info->flags &= ~SC_PKCS15_PIN_FLAG_LOCAL;
        return r;
}

/*
 * Create a new key file
 */
static int
jcop_create_key(sc_profile_t *profile, sc_card_t *card, sc_pkcs15_object_t *obj
)
{
        sc_pkcs15_prkey_info_t *key_info = (sc_pkcs15_prkey_info_t *) obj->data;
        struct sc_file  *keyfile = NULL;
        size_t          bytes, mod_len, exp_len, prv_len, pub_len;
        int             r;

	if (obj->type != SC_PKCS15_TYPE_PRKEY_RSA) {
	     sc_error(card->ctx, "JCOP supports only RSA keys.");
	     return SC_ERROR_NOT_SUPPORTED;
	}
        /* The caller is supposed to have chosen a key file path for us */
        if (key_info->path.len == 0 || key_info->modulus_length == 0)
                return SC_ERROR_INVALID_ARGUMENTS;

        /* Get the file we're supposed to create */
        r = sc_profile_get_file_by_path(profile, &key_info->path, &keyfile);
        if (r < 0)
                return r;

        mod_len = key_info->modulus_length / 8;
        exp_len = 4;
        bytes   = mod_len / 2;
        pub_len = 2 + mod_len + exp_len;
	prv_len = 2 + 5 * bytes;
        keyfile->size = prv_len;

        /* Fix up PIN references in file ACL */
        r = sc_pkcs15init_fixup_file(profile, keyfile);

        if (r >= 0)
                r = sc_pkcs15init_create_file(profile, card, keyfile);

        if (keyfile)
                sc_file_free(keyfile);
        return r;
}

static void
jcop_bn2bin(unsigned char *dest, sc_pkcs15_bignum_t *bn, unsigned int size)
{
        u8              *src;
        unsigned int    n;

        assert(bn->len <= size);
        memset(dest, 0, size);
        for (n = size-bn->len, src = bn->data; n < size; n++,src++)
                dest[n] = *src;
}

/*
 * Store a private key
 * Private key file formats: (transparent file)
 * Non-CRT:
 * byte 0     0x05
 * byte 1     Modulus length (in byte/4)
 * byte 2     Modulus (n)
 * byte 2+x   private exponent (d)
 * 
 * CRT:
 * byte 0     0x06
 * byte 1     component length (in byte/2; component length is half 
 *            of modulus length
 * byte 2     Prime (p)
 * byte 2+x   Prime (q)
 * byte 2+2*x Exponent 1 (d mod (p-1))
 * byte 2+3*x Exponent 2 (d mod (q-1))
 * byte 2+4*x Coefficient ((p ^ -1) mod q
 *
 * We use the CRT format, since that's what key generation does.
 *
 * Numbers are stored big endian.
 */
static int
jcop_store_key(sc_profile_t *profile, sc_card_t *card,
                sc_pkcs15_object_t *obj,
                sc_pkcs15_prkey_t *key)
{
        sc_pkcs15_prkey_info_t *key_info = (sc_pkcs15_prkey_info_t *) obj->data;
        sc_file_t       *keyfile;
        unsigned char   keybuf[1024];
        size_t          size,base;
        int             r;

        if (obj->type != SC_PKCS15_TYPE_PRKEY_RSA) {
                sc_error(card->ctx, "JCOP supports only RSA keys.");
                return SC_ERROR_NOT_SUPPORTED;
        }
        r = sc_profile_get_file_by_path(profile, &key_info->path, &keyfile);
        if (r < 0)
                return r;
	base=key_info->modulus_length / 16;
	size=2+5*base;
	keybuf[0]=6;
	keybuf[1]=base/4;
	jcop_bn2bin(&keybuf[2 + 0 * base], &key->u.rsa.p, base);
	jcop_bn2bin(&keybuf[2 + 1 * base], &key->u.rsa.q, base);
	jcop_bn2bin(&keybuf[2 + 2 * base], &key->u.rsa.dmp1, base);
	jcop_bn2bin(&keybuf[2 + 3 * base], &key->u.rsa.dmq1, base);
	jcop_bn2bin(&keybuf[2 + 4 * base], &key->u.rsa.iqmp, base);
        r = sc_pkcs15init_update_file(profile, card, keyfile, keybuf, size);

	sc_file_free(keyfile);
	return r;
}

/*
 * Generate a keypair
 */
static int
jcop_generate_key(sc_profile_t *profile, sc_card_t *card,
		  sc_pkcs15_object_t *obj,
		  sc_pkcs15_pubkey_t *pubkey) {
     sc_pkcs15_prkey_info_t *key_info = (sc_pkcs15_prkey_info_t *) obj->data;
     struct sc_cardctl_jcop_genkey args;
     sc_file_t       *temppubfile=NULL, *keyfile=NULL;
     unsigned char   *keybuf=NULL;
     size_t          bytes, mod_len, exp_len, pub_len, keybits;
     int             r,delete_ok=0;

     if (obj->type != SC_PKCS15_TYPE_PRKEY_RSA) {
	  sc_error(card->ctx, "JCOP supports only RSA keys.");
	  return SC_ERROR_NOT_SUPPORTED;
     }

     r=sc_profile_get_file(profile, "temp-pubkey", &temppubfile);
     if (r < 0)
	  goto out;

     r = sc_select_file(card, &key_info->path, &keyfile);
     if (r < 0)
	  goto out;

     mod_len = key_info->modulus_length / 8;
     exp_len = 4;
     bytes   = mod_len / 2;
     pub_len = 2 + mod_len + exp_len;
     temppubfile->size = pub_len;     

     r = sc_pkcs15init_fixup_file(profile, temppubfile);
     if (r < 0)
	  goto out;

     r = sc_pkcs15init_create_file(profile, card, temppubfile);
     if (r < 0)
	  goto out;

     delete_ok=1;
     r = sc_pkcs15init_authenticate(profile, card, temppubfile, SC_AC_OP_UPDATE);
     if (r < 0)
	  goto out;
     r = sc_pkcs15init_authenticate(profile, card, keyfile, SC_AC_OP_UPDATE);
     if (r < 0)
	  goto out;
     
     keybits = key_info->modulus_length;

     /* generate key */
     /* keysize is _not_ passed to the card at any point. it appears to
	infer it from the file size */
     memset(&args, 0, sizeof(args));
     args.exponent = 0x10001;
     sc_append_file_id(&args.pub_file_ref, temppubfile->id);
     sc_append_file_id(&args.pri_file_ref, keyfile->id);
     keybuf=(unsigned char *) malloc(keybits / 8);
     if (!keybuf) {
	  r=SC_ERROR_OUT_OF_MEMORY;
	  goto out;
     }
     args.pubkey = keybuf;
     args.pubkey_len = keybits / 8;
     
     r = sc_card_ctl(card, SC_CARDCTL_JCOP_GENERATE_KEY, (void *)&args);
     if (r < 0) 
	  goto out;

     /* extract public key */
     pubkey->algorithm = SC_ALGORITHM_RSA;
     pubkey->u.rsa.modulus.len   = keybits / 8;
     pubkey->u.rsa.modulus.data  = keybuf;
     pubkey->u.rsa.exponent.len  = 3;
     pubkey->u.rsa.exponent.data = (u8 *) malloc(3);
     if (!pubkey->u.rsa.exponent.data) {
	  pubkey->u.rsa.modulus.data = NULL;
	  r=SC_ERROR_OUT_OF_MEMORY;
	  goto out;
     }
     memcpy(pubkey->u.rsa.exponent.data, "\x01\x00\x01", 3);

 out:
     if (r < 0 && keybuf)
	  free(keybuf);	
     if (delete_ok)
	  sc_pkcs15init_rmdir(card, profile, temppubfile);
     if (keyfile)
	  sc_file_free(keyfile);
     if (temppubfile)
	  sc_file_free(temppubfile);
     return r;
}



static struct sc_pkcs15init_operations sc_pkcs15init_jcop_operations;

struct sc_pkcs15init_operations *sc_pkcs15init_get_jcop_ops(void)
{
     sc_pkcs15init_jcop_operations.erase_card = jcop_erase_card;
     sc_pkcs15init_jcop_operations.init_app = jcop_init_app;
     sc_pkcs15init_jcop_operations.select_pin_reference = jcop_select_pin_reference;
     sc_pkcs15init_jcop_operations.create_pin = jcop_create_pin;
     sc_pkcs15init_jcop_operations.create_key = jcop_create_key;
     sc_pkcs15init_jcop_operations.store_key = jcop_store_key;
     sc_pkcs15init_jcop_operations.generate_key = jcop_generate_key;
     
     return &sc_pkcs15init_jcop_operations;
}


