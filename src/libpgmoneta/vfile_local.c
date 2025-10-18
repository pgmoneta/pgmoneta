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
 */
#include <pgmoneta.h>
#include <logging.h>
#include <utils.h>
#include <vfile.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int vfile_local_read(struct vfile* vfile, void* buffer, size_t capacity, size_t* size, bool* last_chunk);
static int vfile_local_write(struct vfile* vfile, void* buffer, size_t size, bool last_chunk);
static int vfile_local_delete(struct vfile* vfile);
static void vfile_local_close(struct vfile* vfile);

struct vfile_local
{
   struct vfile super;
   char file_path[MAX_PATH];
   FILE* fp;
};

int
pgmoneta_vfile_create_local(char* file_path, char* mode, struct vfile** vfile)
{
   struct vfile_local* file = NULL;
   file = malloc(sizeof(struct vfile_local));
   memset(file, 0, sizeof(struct vfile_local));

   file->super.close = vfile_local_close;
   file->super.delete = vfile_local_delete;
   file->super.read = vfile_local_read;
   file->super.write = vfile_local_write;

   file->fp = fopen(file_path, mode);
   if (file->fp == NULL)
   {
      pgmoneta_log_error("vfile_local: Failed to open file '%s' (mode='%s'): %s",
                         file_path, mode, strerror(errno));
      errno = 0;
      goto error;
   }
   memcpy(file->file_path, file_path, strlen(file_path));

   *vfile = (struct vfile*)file;
   return 0;

error:
   pgmoneta_vfile_destroy((struct vfile*)file);
   return 1;
}

static int
vfile_local_read(struct vfile* vfile, void* buffer, size_t capacity, size_t* size, bool* last_chunk)
{
   size_t s = 0;
   struct vfile_local* this = (struct vfile_local*)vfile;

   if (this == NULL || this->fp == NULL)
   {
      goto error;
   }

   s = fread(buffer, 1, capacity, this->fp);
   if (ferror(this->fp))
   {
      pgmoneta_log_error("vfile_local: Failed to read file '%s': %s",
                         this->file_path, strerror(errno));
      errno = 0;
      goto error;
   }
   *last_chunk = feof(this->fp);
   *size = s;

   return 0;

error:
   return 1;
}

static int
vfile_local_write(struct vfile* vfile, void* buffer, size_t size, bool last_chunk)
{
   size_t s = 0;
   struct vfile_local* this = (struct vfile_local*)vfile;

   (void)last_chunk;

   if (this == NULL || this->fp == NULL)
   {
      goto error;
   }

   s = fwrite(buffer, 1, size, this->fp);
   if (s != size)
   {
      pgmoneta_log_error("vfile_local: Failed to write to file '%s'", this->file_path);
      goto error;
   }

   return 0;

error:
   return 1;
}

static int
vfile_local_delete(struct vfile* vfile)
{
   struct vfile_local* this = (struct vfile_local*)vfile;
   if (this == NULL)
   {
      goto error;
   }
   if (remove(this->file_path))
   {
      pgmoneta_log_error("vfile_local: failed to delete file %s", this->file_path);
      goto error;
   }
   fclose(this->fp);

   this->fp = NULL;
   return 0;
error:
   return 1;
}

static void
vfile_local_close(struct vfile* vfile)
{
   struct vfile_local* this = NULL;
   this = (struct vfile_local*)vfile;

   if (this == NULL)
   {
      return;
   }

   if (this->fp != NULL)
   {
      fclose(this->fp);
      this->fp = NULL;
   }
}