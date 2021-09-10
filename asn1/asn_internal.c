#include <asn_internal.h>

char* gser_decoder_str = NULL;
int gser_decoder_is_error_str_filled = 0;
char gser_decoder_error_str[GSER_DECODER_ERROR_STR_LEN+1] = {};

ssize_t
asn__format_to_callback(int (*cb)(const void *, size_t, void *key), void *key,
                        const char *fmt, ...) {
    char scratch[64];
    char *buf = scratch;
    size_t buf_size = sizeof(scratch);
    int wrote;
    int cb_ret;

    do {
        va_list args;
        va_start(args, fmt);

        wrote = vsnprintf(buf, buf_size, fmt, args);
        va_end(args);
        if(wrote < (ssize_t)buf_size) {
            if(wrote < 0) {
                if(buf != scratch) FREEMEM(buf);
                return -1;
            }
            break;
        }

        buf_size <<= 1;
        if(buf == scratch) {
            buf = MALLOC(buf_size);
            if(!buf) return -1;
        } else {
            void *p = REALLOC(buf, buf_size);
            if(!p) {
                FREEMEM(buf);
                return -1;
            }
            buf = p;
        }
    } while(1);

    cb_ret = cb(buf, wrote, key);
    if(buf != scratch) FREEMEM(buf);
    if(cb_ret < 0) {
        return -1;
    }

    return wrote;
}

