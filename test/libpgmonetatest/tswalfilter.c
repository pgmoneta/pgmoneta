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

/* pgmoneta */
#include <pgmoneta.h>
#include <tscommon.h>
#include <tswalfilter.h>
#include <utils.h>

/* system */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
pgmoneta_walfilter_cli(const char* yaml_path, const char* arguments, char** output, int* exit_code)
{
   char walfilter_bin[MAX_PATH];
   char* command = NULL;
   int ret = 1;

   if (output == NULL || exit_code == NULL)
   {
      return 1;
   }

   if (pgmoneta_test_resolve_binary_path("pgmoneta-walfilter", walfilter_bin))
   {
      return 1;
   }

   if (yaml_path != NULL && arguments != NULL && strlen(arguments) > 0)
   {
      command = pgmoneta_format_and_append(command, "\"%s\" \"%s\" %s", walfilter_bin, yaml_path, arguments);
   }
   else if (yaml_path != NULL)
   {
      command = pgmoneta_format_and_append(command, "\"%s\" \"%s\"", walfilter_bin, yaml_path);
   }
   else if (arguments != NULL && strlen(arguments) > 0)
   {
      command = pgmoneta_format_and_append(command, "\"%s\" %s", walfilter_bin, arguments);
   }
   else
   {
      command = pgmoneta_format_and_append(command, "\"%s\"", walfilter_bin);
   }

   if (command == NULL)
   {
      return 1;
   }

   ret = pgmoneta_test_exec_command(command, output, exit_code);
   free(command);
   return ret;
}
