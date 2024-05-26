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

#include <wal/walfile/rm.h>
#include <wal/walfile/rm_generic.h>
#include <utils.h>

#include <string.h>

char*
pgmoneta_wal_generic_desc(char* buf, struct decoded_xlog_record* record)
{
   pointer ptr = XLOG_REC_GET_DATA(record),
           end = ptr + XLOG_REC_GET_DATA_LEN(record);

   while (ptr < end)
   {
      offset_number offset,
                    length;

      memcpy(&offset, ptr, sizeof(offset));
      ptr += sizeof(offset);
      memcpy(&length, ptr, sizeof(length));
      ptr += sizeof(length);
      ptr += length;

      if (ptr < end)
      {
         buf = pgmoneta_format_and_append(buf, "offset %u, length %u; ", offset, length);
      }
      else
      {
         buf = pgmoneta_format_and_append(buf, "offset %u, length %u", offset, length);
      }
   }
   return buf;
}
