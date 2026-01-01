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
#include <fcntl.h>
#include <logging.h>
#include <security.h>
#include <utils.h>
#include <walfile/pg_control.h>

static int get_controlfile_by_exact_path(int pg_version, const char* control_file_path, struct control_file_data** controldata, bool* crc_ok);

int
pgmoneta_read_control_data(int server, char* directory, struct control_file_data** controldata)
{
   struct main_configuration* config = NULL;
   char* control_data_dir = NULL;
   struct control_file_data* cntldata = NULL;
   bool crc_ok = false;

   *controldata = NULL;

   config = (struct main_configuration*)shmem;

   control_data_dir = pgmoneta_append(control_data_dir, directory);
   control_data_dir = pgmoneta_append(control_data_dir, "/global/pg_control");

   if (get_controlfile_by_exact_path(config->common.servers[server].version, control_data_dir, &cntldata, &crc_ok))
   {
      pgmoneta_log_error("Failed to retrieve control file from '%s'", control_data_dir);
      goto error;
   }

   if (cntldata == NULL)
   {
      pgmoneta_log_error("Control file pointer is NULL after reading from '%s'", control_data_dir);
      goto error;
   }

   if (!crc_ok)
   {
      pgmoneta_log_error("CRC validation failed for control file at '%s'", control_data_dir);
      goto error;
   }

   *controldata = cntldata;
   free(control_data_dir);
   return 0;

error:
   free(control_data_dir);
   return 1;
}

static int
get_controlfile_by_exact_path(int pg_version, const char* control_file_path, struct control_file_data** controldata, bool* crc_ok)
{
   void* controlfile_buf = NULL;
   struct control_file_data* cntldata = NULL;
   uint32_t pg_control_version;
   pg_crc32c crc;
   bool crcok = false;
   void* crc_ptr = NULL;
   size_t crc_offset = 0;
   FILE* fd = NULL;
   size_t r = 0;

   *controldata = NULL;
   *crc_ok = false;

   controlfile_buf = malloc(PG_CONTROL_MAX_SAFE_SIZE);
   cntldata = malloc(sizeof(struct control_file_data));

   if (!controlfile_buf || !cntldata)
   {
      pgmoneta_log_error("Memory allocation failed");
      goto error;
   }

   fd = fopen(control_file_path, "rb");
   if (fd == NULL)
   {
      pgmoneta_log_error("Could not open file \"%s\" for reading: %m", control_file_path);
      goto error;
   }

   r = fread(controlfile_buf, 1, PG_CONTROL_MAX_SAFE_SIZE, fd);
   if (r != PG_CONTROL_MAX_SAFE_SIZE)
   {
      if (ferror(fd))
      {
         pgmoneta_log_error("Could not read file \"%s\": %m", control_file_path);
      }
      else
      {
         pgmoneta_log_error("Could not read file \"%s\": read %zu of %d", control_file_path, r, PG_CONTROL_MAX_SAFE_SIZE);
      }
      goto error;
   }

   fclose(fd);
   fd = NULL;

   memcpy(&pg_control_version, (char*)controlfile_buf + 8, sizeof(uint32_t));

   // | PostgreSQL version | pg control version |
   // |--------------------|--------------------|
   // | 13-16              | 1300               |
   // | 17                 | 1700               |
   // | 18                 | 1800               |
   switch (pg_control_version)
   {
      case 1300:
         switch (pg_version)
         {
            case 13:
               cntldata->version = CONTROL_FILE_V13;
               memcpy(&cntldata->data.v13, controlfile_buf, sizeof(struct control_file_data_v13));
               crc_ptr = &cntldata->data.v13.crc;
               break;
            case 14:
               cntldata->version = CONTROL_FILE_V14;
               memcpy(&cntldata->data.v14, controlfile_buf, sizeof(struct control_file_data_v13));
               crc_ptr = &cntldata->data.v14.crc;
               break;
            case 15:
               cntldata->version = CONTROL_FILE_V15;
               memcpy(&cntldata->data.v15, controlfile_buf, sizeof(struct control_file_data_v13));
               crc_ptr = &cntldata->data.v15.crc;
               break;
            case 16:
               cntldata->version = CONTROL_FILE_V16;
               memcpy(&cntldata->data.v16, controlfile_buf, sizeof(struct control_file_data_v13));
               crc_ptr = &cntldata->data.v16.crc;
               break;
            default:
               pgmoneta_log_error("pg_control version (%d) does not match PostgreSQL version (%d)", pg_control_version, pg_version);
               goto error;
         }
         crc_offset = offsetof(struct control_file_data_v13, crc);
         break;

      case 1700:
         cntldata->version = CONTROL_FILE_V17;
         memcpy(&cntldata->data.v17, controlfile_buf, sizeof(struct control_file_data_v17));
         crc_ptr = &cntldata->data.v17.crc;
         crc_offset = offsetof(struct control_file_data_v17, crc);
         break;

      case 1800:
         cntldata->version = CONTROL_FILE_V18;
         memcpy(&cntldata->data.v18, controlfile_buf, sizeof(struct control_file_data_v18));
         crc_ptr = &cntldata->data.v18.crc;
         crc_offset = offsetof(struct control_file_data_v18, crc);
         break;

      default:
         pgmoneta_log_error("Unsupported pg_control version: %u", pg_control_version);
         goto error;
   }

   if (pgmoneta_init_crc32c(&crc))
   {
      pgmoneta_log_error("Failed to initialize CRC32C computation");
      goto error;
   }

   if (pgmoneta_create_crc32c_buffer(controlfile_buf, crc_offset, &crc))
   {
      pgmoneta_log_error("Failed to compute CRC32C");
      goto error;
   }

   if (pgmoneta_finalize_crc32c(&crc))
   {
      pgmoneta_log_error("Failed to finalize CRC32C computation");
      goto error;
   }

   crcok = pgmoneta_compare_crc32c(crc, *(pg_crc32c*)crc_ptr);

   if ((pg_control_version % 65536 == 0) &&
       (pg_control_version / 65536 != 0))
   {
      pgmoneta_log_error("Possible byte ordering mismatch");
   }

   *controldata = cntldata;
   *crc_ok = crcok;
   free(controlfile_buf);
   return 0;

error:
   free(controlfile_buf);
   free(cntldata);
   if (fd != NULL)
   {
      fclose(fd);
   }
   return 1;
}
