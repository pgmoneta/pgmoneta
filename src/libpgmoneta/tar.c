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

/* pgmoneta */
#include <logging.h>
#include <pgmoneta.h>
#include <tar.h>
#include <utils.h>

#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int write_tar_file(struct archive* a, char* src, char* dst);

int
pgmoneta_tar(char* src, char* dst)
{
   struct archive* a = NULL;
   char archive_root[MAX_PATH];
   int status;
   size_t src_len = 0;
   size_t end = 0;
   const char* start = NULL;
   size_t root_len = 0;

   if (src == NULL || dst == NULL)
   {
      goto error;
   }

   src_len = strlen(src);
   end = src_len;
   while (end > 0 && src[end - 1] == '/')
   {
      end--;
   }

   if (end == 0)
   {
      goto error;
   }

   start = src;
   for (size_t i = end; i > 0; i--)
   {
      if (src[i - 1] == '/')
      {
         start = &src[i];
         break;
      }
   }

   root_len = end - (size_t)(start - src);
   if (root_len == 0 || root_len >= sizeof(archive_root))
   {
      goto error;
   }

   memset(archive_root, 0, sizeof(archive_root));
   memcpy(archive_root, start, root_len);
   archive_root[root_len] = '\0';

   a = archive_write_new();
   if (a == NULL)
   {
      goto error;
   }

   archive_write_set_format_ustar(a);
   status = archive_write_open_filename(a, dst);

   if (status != ARCHIVE_OK)
   {
      pgmoneta_log_error("Could not create tar file %s", dst);
      goto error;
   }
   if (write_tar_file(a, src, archive_root))
   {
      goto error;
   }

   archive_write_close(a);
   archive_write_free(a);

   return 0;

error:
   if (a != NULL)
   {
      archive_write_close(a);
      archive_write_free(a);
   }

   return 1;
}

int
pgmoneta_untar(char* src, char* dst)
{
   struct archive* a = NULL;
   struct archive_entry* entry = NULL;
   la_int64_t entry_size = 0;
   uint64_t extracted_size = 0;
   unsigned long free_space = 0;

   if (src == NULL || dst == NULL)
   {
      goto error;
   }

   a = archive_read_new();
   archive_read_support_format_tar(a);

   if (archive_read_open_filename(a, src, 10240) != ARCHIVE_OK)
   {
      pgmoneta_log_error("Failed to open the tar file for reading");
      goto error;
   }

   while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
   {
      entry_size = archive_entry_size(entry);
      if (entry_size > 0)
      {
         if (extracted_size > UINT64_MAX - (uint64_t)entry_size)
         {
            pgmoneta_log_error("Extracted TAR size overflow for file: %s", src);
            goto error;
         }
         extracted_size += (uint64_t)entry_size;
      }
   }

   archive_read_close(a);
   archive_read_free(a);
   a = NULL;

   free_space = pgmoneta_free_space(dst);
   if (extracted_size > 0 && (free_space == 0 || extracted_size > (uint64_t)free_space))
   {
      pgmoneta_log_error("Not enough space to extract TAR archive: %s", src);
      goto error;
   }

   a = archive_read_new();
   archive_read_support_format_tar(a);

   if (archive_read_open_filename(a, src, 10240) != ARCHIVE_OK)
   {
      pgmoneta_log_error("Failed to open the tar file for reading");
      goto error;
   }

   while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
   {
      char dst_file_path[MAX_PATH];
      const char* entry_path = archive_entry_pathname(entry);

      memset(dst_file_path, 0, sizeof(dst_file_path));
      if (pgmoneta_ends_with(dst, "/"))
      {
         pgmoneta_snprintf(dst_file_path, sizeof(dst_file_path), "%s%s", dst, entry_path);
      }
      else
      {
         pgmoneta_snprintf(dst_file_path, sizeof(dst_file_path), "%s/%s", dst, entry_path);
      }

      archive_entry_set_pathname(entry, dst_file_path);
      if (archive_read_extract(a, entry, 0) != ARCHIVE_OK)
      {
         pgmoneta_log_error("Failed to extract entry: %s", archive_error_string(a));
         goto error;
      }
   }

   archive_read_close(a);
   archive_read_free(a);
   return 0;

error:
   if (a != NULL)
   {
      archive_read_close(a);
      archive_read_free(a);
   }
   return 1;
}

static int
write_tar_file(struct archive* a, char* src, char* dst)
{
   char real_path[MAX_PATH];
   char save_path[MAX_PATH];
   ssize_t size;
   struct archive_entry* entry = NULL;
   struct stat s;
   struct dirent* dent;
   DIR* dir = NULL;

   dir = opendir(src);
   if (!dir)
   {
      pgmoneta_log_error("Could not open directory: %s", src);
      goto error;
   }

   while ((dent = readdir(dir)) != NULL)
   {
      char* entry_name = dent->d_name;

      if (pgmoneta_compare_string(entry_name, ".") || pgmoneta_compare_string(entry_name, ".."))
      {
         continue;
      }

      snprintf(real_path, sizeof(real_path), "%s/%s", src, entry_name);
      snprintf(save_path, sizeof(save_path), "%s/%s", dst, entry_name);

      entry = archive_entry_new();
      if (entry == NULL)
      {
         pgmoneta_log_error("Could not create archive entry");
         goto error;
      }
      archive_entry_copy_pathname(entry, save_path);

      if (lstat(real_path, &s))
      {
         pgmoneta_log_error("Could not stat file %s", real_path);
         goto error;
      }

      if (S_ISDIR(s.st_mode))
      {
         archive_entry_set_filetype(entry, AE_IFDIR);
         archive_entry_set_perm(entry, s.st_mode);
         if (archive_write_header(a, entry) != ARCHIVE_OK)
         {
            pgmoneta_log_error("Could not write directory header: %s", archive_error_string(a));
            goto error;
         }

         archive_entry_free(entry);
         entry = NULL;

         if (write_tar_file(a, real_path, save_path))
         {
            goto error;
         }
      }
      else if (S_ISLNK(s.st_mode))
      {
         char target[MAX_PATH];
         memset(target, 0, sizeof(target));
         size = readlink(real_path, target, sizeof(target));
         if (size <= 0 || size >= (ssize_t)sizeof(target))
         {
            pgmoneta_log_error("Could not read symlink: %s", real_path);
            goto error;
         }

         target[size] = '\0';

         archive_entry_set_filetype(entry, AE_IFLNK);
         archive_entry_set_perm(entry, s.st_mode);
         archive_entry_set_symlink(entry, target);
         if (archive_write_header(a, entry) != ARCHIVE_OK)
         {
            pgmoneta_log_error("Could not write symlink header: %s", archive_error_string(a));
            goto error;
         }
      }
      else if (S_ISREG(s.st_mode))
      {
         FILE* file = NULL;

         archive_entry_set_filetype(entry, AE_IFREG);
         archive_entry_set_perm(entry, s.st_mode);
         archive_entry_set_size(entry, s.st_size);
         int status = archive_write_header(a, entry);
         if (status != ARCHIVE_OK)
         {
            pgmoneta_log_error("Could not write header: %s", archive_error_string(a));
            goto error;
         }

         file = fopen(real_path, "rb");

         if (file == NULL)
         {
            pgmoneta_log_error("Could not open file for reading: %s", real_path);
            goto error;
         }
         else
         {
            char buf[DEFAULT_BUFFER_SIZE];
            size_t bytes_read = 0;
            int has_read_error = 0;

            memset(buf, 0, sizeof(buf));
            while ((bytes_read = fread(buf, 1, sizeof(buf), file)) > 0)
            {
               if (archive_write_data(a, buf, bytes_read) < 0)
               {
                  fclose(file);
                  pgmoneta_log_error("Could not write file data: %s", archive_error_string(a));
                  goto error;
               }
               memset(buf, 0, sizeof(buf));
            }

            has_read_error = ferror(file);
            fclose(file);
            if (has_read_error)
            {
               pgmoneta_log_error("Could not read file data: %s", real_path);
               goto error;
            }
         }
      }

      archive_entry_free(entry);
      entry = NULL;
   }

   closedir(dir);
   return 0;

error:
   if (entry != NULL)
   {
      archive_entry_free(entry);
   }
   if (dir != NULL)
   {
      closedir(dir);
   }
   return 1;
}
