/*
 * Copyright (C) 2024 The pgmoneta community
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
 */

#include <io.h>
#include <logging.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

FILE*
pgmoneta_open_file(const char* path, const char* modes)
{
   int oflag = __O_DIRECT;
   if (strchr(modes, 'r') != NULL)
   {
      if (strchr(modes, '+') != NULL)
      {
         oflag = oflag | O_RDWR | O_CREAT;
      }
      else
      {
         oflag = oflag | O_RDONLY | O_CREAT;
      }
   }
   if (strchr(modes, 'w') != NULL)
   {
      if (strchr(modes, '+') != NULL)
      {
         oflag = oflag | O_RDWR | O_CREAT | O_TRUNC;
      }
      else
      {
         oflag = oflag | O_WRONLY | O_CREAT | O_TRUNC;
      }
   }
   if (strchr(modes, 'a') != NULL)
   {
      if (strchr(modes, '+') != NULL)
      {
         oflag = oflag | O_APPEND | O_CREAT | O_RDONLY;
      }
      else
      {
         oflag = oflag | O_APPEND | O_CREAT;
      }
   }

   int fd = open(path, oflag, 0600);
   if (fd < 0)
   {
      pgmoneta_log_error("unable to open() path=%s, modes=%s", path, modes);
      return NULL;
   }
   pgmoneta_log_debug("fd=%d path=%s modes=%s\n", fd, path, modes);
   FILE* file = fdopen(fd, modes);

   return file;
}

int
pgmoneta_write_file(void* buffer, size_t size, size_t n, FILE* file)
{
   int bytes_left = n * size;
   int chunks_written = 0;
   int bytes_written = 0;
   int total_bytes_written = 0;
   pgmoneta_log_debug("bytes_left=%d x %d = %d", n, size, bytes_left);
   while (bytes_left > 0)
   {
      if (bytes_left < 4096)
      {
         chunks_written = fwrite(buffer, bytes_left, 1, file);
         bytes_written = chunks_written * bytes_left;
      }
      else
      {
         chunks_written = fwrite(buffer, 4096, 1, file);
         bytes_written = chunks_written * 4096;
      }
      total_bytes_written += bytes_written;
      bytes_left -= bytes_written;
   }
   pgmoneta_log_debug("bytes_left=%d total_bytes_written=%d", bytes_left, total_bytes_written);
   return total_bytes_written / size;
}