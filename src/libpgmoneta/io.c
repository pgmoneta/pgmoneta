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

#include <io.h>
#include <logging.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WRITE_SIZE BLOCK_SIZE*256

ssize_t
pgmoneta_write_file(int fd, void* buffer, size_t bytes)
{
   ssize_t bytes_written = 0;
   int flags = fcntl(fd, F_GETFL);
   int odd_bytes = bytes % BLOCK_SIZE;
   int misalignment = (uintptr_t)buffer % BLOCK_SIZE;

   if (flags & O_DIRECT)
   {
      if (misalignment != 0)
      {
         if (fcntl(fd, F_SETFL, flags & ~O_DIRECT))
         {
            pgmoneta_log_error("Failed to disable O_DIRECT");
            return -1;
         }

         bytes_written = write(fd, buffer, bytes);

         if (fcntl(fd, F_SETFL, flags))
         {
            pgmoneta_log_error("Failed to re-enable O_DIRECT");
            return -1;
         }
      }
      else if (odd_bytes != 0)
      {
         char* tmp_buf = (char*)buffer;

         while (bytes_written + WRITE_SIZE < bytes)
         {
            bytes_written += write(fd, tmp_buf + bytes_written, WRITE_SIZE);
         }

         if (fcntl(fd, F_SETFL, flags & ~O_DIRECT))
         {
            pgmoneta_log_error("Failed to disable O_DIRECT");
            return -1;
         }

         bytes_written += write(fd, tmp_buf + bytes_written, bytes - bytes_written);

         if (fcntl(fd, F_SETFL, flags))
         {
            pgmoneta_log_error("Failed to re-enable O_DIRECT");
            return -1;
         }
      }
      else
      {
         bytes_written = write(fd, buffer, bytes);
      }
   }
   else
   {
      bytes_written = write(fd, buffer, bytes);
   }

   if (bytes_written < 0)
   {
      pgmoneta_log_error("Error writing to fd=%d : %s", fd, strerror(errno));
      errno = 0;
      return -1;
   }

   return bytes_written;
}

void*
pgmoneta_aligned_malloc(size_t size)
{
   return aligned_alloc(BLOCK_SIZE, size);
}

void*
pgmoneta_unaligned_to_aligned_buffer(void* buffer, size_t size)
{
   int misalignment = (uintptr_t)buffer % BLOCK_SIZE;

   if (misalignment == 0)
   {
      return buffer;
   }

   void* aligned_buf =
      pgmoneta_aligned_malloc((size / BLOCK_SIZE + 1) * BLOCK_SIZE);
   memcpy(aligned_buf, buffer, size);
   return aligned_buf;
}
