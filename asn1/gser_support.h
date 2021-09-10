#ifndef	_GSER_SUPPORT_H_
#define	_GSER_SUPPORT_H_

#include <asn_system.h>		/* Platform-specific types */
#include <gser_tokens.h>

#ifdef __cplusplus
extern "C" {
#endif

ssize_t gser_parse(int *_state, const void *_buf, size_t _size, gser_token_e *token);

#ifdef __cplusplus
}
#endif

#endif	/* _GSER_SUPPORT_H_ */
