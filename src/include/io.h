/*
 * Copyright (C) 2024 The pgmoneta community
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PGMONETA_IO_H
#define PGMONETA_IO_H

#ifdef __cpluscplus
extern "C" {
#endif

#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>

#define BLOCK_SIZE 512

#if !defined (HAVE_LINUX) && !defined (HAVE_FREEBSD)
#define O_DIRECT 0
#endif

/**
 * Returns an aligned buffer with same data as unaligned buffer
 * @param buffer The unaligned buffer
 * @param size The buffer size
 * @return The aligned buffer
 */
void*
pgmoneta_unaligned_to_aligned_buffer(void* buffer, size_t size);

/**
 * Wrapper around write() libc call
 * @param fd The file descriptor
 * @param buffer The buffer to be written
 * @param bytes The number of bytes to be written
 * @return The number of bytes written, -1 on error
 */
ssize_t
pgmoneta_write_file(int fd, void* buffer, size_t bytes);

/**
 * Wrapper around aligned_alloc() libc call
 * @param size The size of the buffer
 */
void*
pgmoneta_aligned_malloc(size_t size);

#ifdef __cpluscplus
}
#endif

#endif