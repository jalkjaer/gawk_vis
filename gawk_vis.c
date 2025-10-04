/*
* gawk_vis: bsd vis encoding and decoding for gawk
* Copyright (C) 2013  J Alkjaer <jalkjaer@gmail.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * gaws_vis.c - Gawk extension library for bsd vis encoding and decoding
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "gawkapi.h"

static const gawk_api_t* api;	/* for convenience macros to work */
static awk_ext_id_t ext_id;
static const char* ext_version = "vis extension: version 1.0";
static awk_bool_t (*init_func)(void) = NULL;

int plugin_is_GPL_compatible;

/* BSD vis flag definitions as strings (https://man.netbsd.org/vis.3) */
#define VIS_DQ      "\""                 /* Also encode double quote */
#define VIS_GLOB    "*?[#"               /* Also encode globbing characters */
#define VIS_SHELL   "';\"&<>()|]\\$!^~`" /* Also encode shell special characters */
#define VIS_SP      " "                  /* Also encode space */
#define VIS_TAB     "\t"                 /* Also encode tab */
#define VIS_NL      "\n"                 /* Also encode newline */

/* Build additional_chars */
static const char additional_chars[] =
	VIS_GLOB
	VIS_SHELL
	VIS_SP
	VIS_TAB
	VIS_NL;

/**
 * \brief           Check if character needs vis encoding
 * \param[in]       c: Character to check
 * \param[in]       custom_chars: Custom character list or NULL for default
 * \return          `1` if character needs encoding, `0` otherwise
 */
static int
needs_vis_encoding(unsigned char c, const char* custom_chars) {
	/* Always encode non-printable characters and high-bit characters */
	if (!isprint(c) || c >= 0x80) {
		return 1;
	}

	/* Use custom characters if provided, otherwise use default */
	const char* chars_to_check = custom_chars ? custom_chars : additional_chars;
	return strchr(chars_to_check, c) != NULL;
}

/**
 * \brief           Calculate encoded buffer size excluding null terminator
 * \param[in]       str: String to encode
 * \param[in]       len: Length of string
 * \param[in]       custom_chars: Custom character list or NULL for default
 * \return          Required buffer size for encoded string
 */
static size_t
vis_encoded_size(const char* str, size_t len, const char* custom_chars) {
	size_t size = 0;
	for (size_t i = 0; i < len; i++) {
		if (needs_vis_encoding((unsigned char)str[i], custom_chars)) {
			size += 4; /* \ddd */
		} else {
			size += 1;
		}
	}
	return size;
}

/**
 * \brief           Encode string to vis format
 * \param[in]       src: Source string to encode
 * \param[in]       src_len: Length of source string
 * \param[in]       custom_chars: Custom character list or NULL for default
 * \param[out]      out_buf: Pointer to output buffer pointer
 * \param[out]      out_len: Pointer to output length
 */
static void
vis_encode_string(const char* src, size_t src_len, const char* custom_chars, char** out_buf, size_t* out_len) {
	if (!src || !out_buf || !out_len) {
		warning(ext_id, "vis_encode_string: NULL argument(s)");
		if (out_buf) {
			*out_buf = NULL;
		}
		if (out_len) {
			*out_len = 0;
		}
		return;
	}

	size_t buf_len = vis_encoded_size(src, src_len, custom_chars);
	char* buf = gawk_malloc(buf_len + 1);
	if (!buf) {
		nonfatal(ext_id, "vis_encode_string: memory allocation failed");
		*out_buf = NULL;
		*out_len = 0;
		return;
	}

	char* d = buf;
	for (size_t i = 0; i < src_len; i++) {
		unsigned char c = (unsigned char)src[i];
		if (needs_vis_encoding(c, custom_chars)) {
			d += sprintf(d, "\\%03o", c);
		} else {
			*d++ = c;
		}
	}
	*d = '\0';
	*out_buf = buf;
	*out_len = buf_len;
}

/**
 * \brief           Decode vis-encoded string
 * \param[in]       src: Source string to decode
 * \param[in]       src_len: Length of source string
 * \param[out]      out_buf: Pointer to output buffer pointer
 * \param[out]      out_len: Pointer to output length
 */
static void
vis_decode_string(const char* src, size_t src_len, char** out_buf, size_t* out_len) {
	if (!src || !out_buf || !out_len) {
		warning(ext_id, "vis_decode_string: NULL argument(s)");
		if (out_buf) {
			*out_buf = NULL;
		}
		if (out_len) {
			*out_len = 0;
		}
		return;
	}

	/* Decoded string is at most as long as the input */
	char* buf = gawk_malloc(src_len + 1);
	if (!buf) {
		nonfatal(ext_id, "vis_decode_string: memory allocation failed");
		*out_buf = NULL;
		*out_len = 0;
		return;
	}

	const char* s = src;
	char* d = buf;
	size_t remaining = src_len;

	while (remaining > 0) {
		if (*s == '\\' && remaining >= 4 && 
		    s[1] >= '0' && s[1] <= '7' &&
		    s[2] >= '0' && s[2] <= '7' &&
		    s[3] >= '0' && s[3] <= '7') {
			/* Decode octal escape sequence \ddd */
			unsigned char decoded = (s[1] - '0') * 64 + (s[2] - '0') * 8 + (s[3] - '0');
			*d++ = decoded;
			s += 4;
			remaining -= 4;
		} else {
			/* Regular character */
			*d++ = *s++;
			remaining--;
		}
	}

	*d = '\0';
	*out_buf = buf;
	*out_len = d - buf;
}

/**
 * \brief           Vis encoding function available to awk
 * \param[in]       nargs: Number of arguments
 * \param[out]      result: Result value
 * \return          Pointer to result value
 */
static awk_value_t*
vis_encode(int nargs, awk_value_t* result) {
	assert(result != NULL);

	if (nargs < 1) {
		return make_const_string("", 0, result);
	}

	awk_value_t input;
	if (!get_argument(0, AWK_STRING, &input)) {
		return make_const_string("", 0, result);
	}

	const char* custom_chars = NULL;
	awk_bool_t has_invalid_chars = awk_false;

	/* Check for optional second argument (custom character list) */
	if (nargs >= 2) {
		awk_value_t charlist;
		if (get_argument(1, AWK_STRING, &charlist)) {
			/* Check for multibyte characters */
			for (size_t i = 0; i < charlist.str_value.len; i++) {
				unsigned char c = (unsigned char)charlist.str_value.str[i];
				if (c >= 0x80) {
					warning(ext_id, "vis:enc: Character list contains multibyte characters, returning input unaltered");
					return make_const_string(input.str_value.str, input.str_value.len, result);
				}
				/* Check for non-printable characters */
				if (!isprint(c) && !has_invalid_chars) {
					warning(ext_id, "vis:enc: Character list contains non-printable characters that will be ignored");
					has_invalid_chars = awk_true;
				}
			}
			custom_chars = charlist.str_value.str;
		}
	}

	char* encoded_buf;
	size_t encoded_len;
	vis_encode_string(input.str_value.str, input.str_value.len, custom_chars,
			  &encoded_buf, &encoded_len);

	if (!encoded_buf) {
		return make_const_string("", 0, result);
	}

	awk_value_t* ret = make_malloced_string(encoded_buf, encoded_len, result);
	return ret;
}

/**
 * \brief           Vis decoding function available to awk
 * \param[in]       nargs: Number of arguments
 * \param[out]      result: Result value
 * \return          Pointer to result value
 */
static awk_value_t*
vis_decode(int nargs, awk_value_t* result) {
	assert(result != NULL);

	if (nargs < 1) {
		return make_const_string("", 0, result);
	}

	awk_value_t input;
	if (!get_argument(0, AWK_STRING, &input)) {
		return make_const_string("", 0, result);
	}

	char* decoded_buf;
	size_t decoded_len;
	vis_decode_string(input.str_value.str, input.str_value.len,
			  &decoded_buf, &decoded_len);

	if (!decoded_buf) {
		return make_const_string("", 0, result);
	}

	awk_value_t* ret = make_malloced_string(decoded_buf, decoded_len, result);
	return ret;
}

/* register extension functions */
static awk_ext_func_t func_table[] = {
	{ "enc", vis_encode, 1, 1, awk_false, NULL },
	{ "dec", vis_decode, 1, 1, awk_false, NULL },
};

dl_load_func(func_table, gawk_vis, "vis")
