/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L /* for strdup() and strtok_r() */
#endif

#include <config.h>

#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "auth.h"
#include "log.h"
#include "stream.h"

#ifdef HAVE_VISIBILITY
#pragma GCC visibility push(hidden)
#endif

static void http_auth_new_header(http_auth_t *auth) {
    auth->state.flags.type = HTTP_AUTH_TYPE_NONE;
    auth->state.flags.use_md5_sess = 0;
    auth->state.flags.use_qop_auth = 0;
    free(auth->state.opaque);
    auth->state.opaque = NULL;
}

void _avs_http_auth_reset(http_auth_t *auth) {
    free(auth->state.nonce);
    free(auth->state.realm);
    free(auth->state.opaque);
    memset(&auth->state, 0, sizeof(auth->state));
}

static int match_token(const char **stream, const char *token,
                       const char *delims) {
    size_t len = strlen(token);
    int result;
    /* skip leading whitespace, if any */
    while (**stream && isspace((unsigned char) **stream)) {
        ++*stream;
    }
    result = strncasecmp(*stream, token, len);
    if (result == 0) {
        if ((*stream)[len] && !strchr(delims, (unsigned char) (*stream)[len])) {
            return 1;
        }
        *stream += len;
        if (**stream) {
            ++*stream; // skip the (first) delimiter character
        }
    }
    return result;
}

#define SPACES " \t\v\f\r\n"

static void consume_quotable_token(const char **src,
                                   char *dest,
                                   size_t dest_size) {
    char quote = 0;

    if (dest_size == 0) {
        dest = NULL;
    }
    for (char value; (value = **src); ++*src) {
        if (value == '"') {
            quote = !quote;
            continue;
        } else if (quote && value == '\\') {
            value = *++*src;
        }
        if (!value || (!quote && strchr("," SPACES, (unsigned char) value))) {
            break;
        }
        if (dest_size) {
            *dest++ = value;
            --dest_size;
        }
    }
    if (**src) {
        ++*src; // skip the (first) delimiter character
    }
    if (dest) {
        if (dest_size) {
            *dest = '\0';
        } else {
            *--dest = '\0';
        }
    }
}

static char *consume_alloc_quotable_token(const char **src) {
    const char *src_copy = *src;
    consume_quotable_token(&src_copy, NULL, 0);
    size_t bufsize = (size_t) (src_copy - *src) + 1;
    char *buf = (char *) malloc(bufsize);
    if (buf) {
        consume_quotable_token(src, buf, bufsize);
    }
    return buf;
}

int _avs_http_auth_setup(http_auth_t *auth, const char *challenge) {
    http_auth_new_header(auth);
    if (match_token(&challenge, "Basic", SPACES) == 0) {
        LOG(TRACE, "Basic authentication");
        auth->state.flags.type = HTTP_AUTH_TYPE_BASIC;
    } else if (match_token(&challenge, "Digest", SPACES) == 0) {
        LOG(TRACE, "Digest authentication");
        auth->state.flags.type = HTTP_AUTH_TYPE_DIGEST;
    } else {
        /* unknown scheme, ignore */
        LOG(WARNING, "No authentication");
        return 0;
    }

    while (*challenge) {
        if (match_token(&challenge, "realm", "=") == 0) {
            free(auth->state.realm);
            if (!(auth->state.realm =
                    consume_alloc_quotable_token(&challenge))) {
                LOG(ERROR, "Could not allocate memory for auth realm");
                return -1;
            }
            LOG(TRACE, "Auth realm: %s", auth->state.realm);
        } else if (match_token(&challenge, "nonce", "=") == 0) {
            free(auth->state.nonce);
            if (!(auth->state.nonce =
                    consume_alloc_quotable_token(&challenge))) {
                LOG(ERROR, "Could not allocate memory for auth nonce");
                return -1;
            }
            auth->state.nc = 1;
            LOG(TRACE, "Auth nonce: %s", auth->state.nonce);
        } else if (match_token(&challenge, "opaque", "=") == 0) {
            free(auth->state.opaque);
            if (!(auth->state.opaque =
                    consume_alloc_quotable_token(&challenge))) {
                LOG(ERROR, "Could not allocate memory for auth opaque");
                return -1;
            }
            LOG(TRACE, "Auth opaque: %s", auth->state.opaque);
        } else if (match_token(&challenge, "algorithm", "=") == 0) {
            char algorithm[16];
            consume_quotable_token(&challenge, algorithm, sizeof(algorithm));
            if (strcasecmp(algorithm, "MD5-sess") == 0) {
                auth->state.flags.use_md5_sess = 1;
                LOG(TRACE, "Auth algorithm: MD5-sess");
            } else if (strcasecmp(algorithm, "MD5") == 0) {
                LOG(TRACE, "Auth algorithm: MD5");
            } else {
                LOG(ERROR, "Unknown auth algorithm: %s", algorithm);
                return -1;
            }
        } else if (match_token(&challenge, "qop", "=") == 0) {
            char *qop_options_buf = consume_alloc_quotable_token(&challenge);
            if (!qop_options_buf) {
                LOG(ERROR, "Could not allocate memory for qop");
                return -1;
            }
            char *qop_options_tmp, *qop_options = qop_options_buf;
            const char *qop_option;
            while ((qop_option = strtok_r(qop_options, "," SPACES,
                                          &qop_options_tmp))) {
                qop_options = NULL;
                LOG(TRACE, "Auth qop: %s", qop_option);
                if (strcasecmp(qop_option, "auth") == 0) {
                    auth->state.flags.use_qop_auth = 1;
                    break;
                }
            }
            free(qop_options_buf);
            if (!auth->state.flags.use_qop_auth) {
                LOG(ERROR,
                    "qop option present, but qop=\"auth\" not supported");
                return -1;
            }
        } else {
            consume_quotable_token(&challenge, NULL, 0);
        }
    }
    return 0;
}

int _avs_http_auth_send_header(http_stream_t *stream) {
    LOG(TRACE, "http_send_auth_header");
    switch (stream->auth.state.flags.type) {
    case HTTP_AUTH_TYPE_NONE:
        LOG(TRACE, "HTTP_AUTH_NONE");
        return 0;

    case HTTP_AUTH_TYPE_BASIC:
        LOG(TRACE, "HTTP_AUTH_BASIC");
        return _avs_http_auth_send_header_basic(stream);

    case HTTP_AUTH_TYPE_DIGEST:
        LOG(TRACE, "HTTP_AUTH_DIGEST");
        return _avs_http_auth_send_header_digest(stream);

    default:
        LOG(ERROR, "unknown auth type %d",
                  (int) stream->auth.state.flags.type);
        return -1;
    }
}

int _avs_http_auth_setup_stream(http_stream_t *stream,
                                const avs_url_t *parsed_url,
                                const char *auth_username,
                                const char *auth_password) {
    free(stream->auth.credentials.user);
    stream->auth.credentials.user = NULL;
    if (auth_username) {
        if (!(stream->auth.credentials.user = strdup(auth_username))) {
            goto error;
        }
    } else {
        const char *user = avs_url_user(parsed_url);
        if (user && !(stream->auth.credentials.user = strdup(user))) {
            goto error;
        }
    }

    free(stream->auth.credentials.password);
    stream->auth.credentials.password = NULL;
    if (auth_password) {
        if (!(stream->auth.credentials.password = strdup(auth_password))) {
            goto error;
        }
    } else {
        const char *password = avs_url_password(parsed_url);
        if (password
                && !(stream->auth.credentials.password = strdup(password))) {
            goto error;
        }
    }
    return 0;

error:
    _avs_http_auth_clear(&stream->auth);
    return -1;
}

void _avs_http_auth_clear(http_auth_t *auth) {
    _avs_http_auth_reset(auth);
    free(auth->credentials.user);
    auth->credentials.user = NULL;
    free(auth->credentials.password);
    auth->credentials.password = NULL;
}