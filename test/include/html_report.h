/*
 * Copyright (C) 2026 The pgmoneta community
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef PGMONETA_HTML_REPORT_H
#define PGMONETA_HTML_REPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <mctf.h>

/**
 * Build the path for the HTML report file
 * The report is written to the same base directory as the MCTF log,
 * using a fixed filename: pgmoneta-test-report.html
 *
 * @param path Output buffer for the path
 * @param size Size of the path buffer
 * @return 0 on success, 1 on failure
 */
int
html_report_build_path(char* path, size_t size);

/**
 * Generate an HTML report from MCTF test results
 *
 * The HTML report includes:
 * - Summary cards showing total, passed, failed, and skipped counts
 * - A detailed table with all test results
 * - Filter information showing what tests were executed
 *
 * @param path Filesystem path where the HTML report should be written
 * @param filter_type Type of filter that was applied during test execution
 * @param filter Filter string that was used (if any)
 * @return 0 on success, 1 on failure
 */
int
html_report_generate(const char* path, mctf_filter_type_t filter_type, const char* filter);

#ifdef __cplusplus
}
#endif

#endif
