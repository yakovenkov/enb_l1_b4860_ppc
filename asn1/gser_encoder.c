#include <asn_internal.h>
#include <stdio.h>
#include <errno.h>

#include "gser_encoder.h"

/*
 * The XER encoder of any type. May be invoked by the application.
 */
asn_enc_rval_t
gser_encode(const asn_TYPE_descriptor_t *td, const void *sptr,
           enum gser_encoder_flags_e gser_flags, asn_app_consume_bytes_f *cb,
           void *app_key) {
    asn_enc_rval_t er = {0, 0, 0};
	asn_enc_rval_t tmper;

	if(!td || !sptr) 
		goto cb_failed;

	tmper = td->op->gser_encoder(td, sptr, 1, gser_flags, cb, app_key);
	
	if(tmper.encoded == -1) 
		return tmper;
		
	er.encoded += tmper.encoded;

	//Терминирующий ноль
	ASN__CALLBACK("\0", 1);

	ASN__ENCODED_OK(er);
	
cb_failed:
	ASN__ENCODE_FAILED;
}