// An ANSI Terminal Renderer for Markdown
/*
MIT License

Copyright (c) 6over3 Institute <code@6over3.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#ifndef MD4C_ANSI_H
#define MD4C_ANSI_H

#include "md4c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ANSI renderer flags */
#define MD_ANSI_FLAG_DEBUG 0x0001
#define MD_ANSI_FLAG_NO_COLOR 0x0002
#define MD_ANSI_FLAG_VERBATIM_ENTITIES 0x0004
#define MD_ANSI_FLAG_SKIP_UTF8_BOM 0x0008
#define MD_ANSI_FLAG_COMPACT 0x0010 /* Less vertical spacing */

/* Render Markdown into ANSI terminal sequences.
 *
 * This renders markdown with colors, formatting, and structured layout
 * suitable for terminal display.
 *
 * Params input and input_size specify the Markdown input.
 * Callback process_output() gets called with chunks of ANSI output.
 * Param userdata is just propagated back to process_output() callback.
 * Param parser_flags are flags from md4c.h propagated to md_parse().
 * Param render_flags is bitmask of MD_ANSI_FLAG_xxxx.
 *
 * Returns -1 on error (if md_parse() fails.)
 * Returns 0 on success.
 */
int md_ansi(const MD_CHAR *input, MD_SIZE input_size,
            void (*process_output)(const MD_CHAR *, MD_SIZE, void *),
            void *userdata, unsigned parser_flags, unsigned renderer_flags);

#ifdef __cplusplus
} /* extern "C" { */
#endif

#endif /* MD4C_ANSI_H */