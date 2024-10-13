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

#include <pgmoneta.h>
#include <configuration.h>
#include <shmem.h>
#include <walfile.h>
#include <utils.h>

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(void)
{
   printf("pgmoneta-walinfo %s\n", VERSION);
   printf("  Command line utility to read and display Write-Ahead Log (WAL) files\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgmoneta-walinfo <file>\n");
   printf("\n");
   printf("Options:\n");
   printf("  -F, --format Output format (raw, json)\n");
   printf("  -?, --help   Display help\n");
   printf("\n");
}

int
main(int argc, char** argv)
{
   int c;
   int option_index = 0;
   char* format = NULL;
   size_t size;

   if (argc < 2)
   {
      usage();
      goto error;
   }

   while (1)
   {
      static struct option long_options[] =
      {
         {"help", no_argument, 0, '?'},
         {"format", required_argument, 0, 'F'},
         {0, 0, 0, 0}
      };

      c = getopt_long(argc, argv, "?v:F:",
                      long_options, &option_index);

      if (c == -1)
      {
         break;
      }

      switch (c)
      {
         case 'F':
            format = strdup(optarg);
            break;
         case '?':
            usage();
            exit(0);
         default:
            break;
      }
   }

   if (format == NULL)
   {
      format = pgmoneta_append(format, "raw");
   }

   size = sizeof(struct configuration);
   if (pgmoneta_create_shared_memory(size, HUGEPAGE_OFF, &shmem))
   {
      warnx("pgmoneta-cli: Error creating shared memory");
      goto error;
   }
   pgmoneta_init_configuration(shmem);

   if (strcmp(format, "raw") != 0 && strcmp(format, "json") != 0)
   {
      fprintf(stderr, "Invalid format specified. Supported formats: raw, json\n");
      goto error;
   }

   enum value_type type;

   if (strcmp(format, "json") == 0)
   {
      type = ValueJSON;
   }
   else
   {
      type = ValueString;
   }

   if (optind < argc)
   {
      char* file_path = argv[optind];

      if (pgmoneta_describe_walfile(file_path, type))
      {
         fprintf(stderr, "Error while reading/describing WAL file\n");
         goto error;
      }
   }
   else
   {
      fprintf(stderr, "Missing <file> argument\n");
      usage();
      goto error;
   }
   pgmoneta_destroy_shared_memory(shmem, size);

   free(format);
   return 0;

error:
   if (shmem != NULL)
   {
      pgmoneta_destroy_shared_memory(shmem, size);
   }
   if (format != NULL)
   {
      free(format);
   }
   return 1;
}
