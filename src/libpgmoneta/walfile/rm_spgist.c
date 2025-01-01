/*
 * Copyright (C) 2025 The pgmoneta community
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

#include <walfile/rm.h>
#include <walfile/rm_spgist.h>
#include <utils.h>

struct spg_xlog_vacuum_redirect*
create_spg_xlog_vacuum_redirect(void)
{
   struct spg_xlog_vacuum_redirect* wrapper = malloc(sizeof(struct spg_xlog_vacuum_redirect));

   if (server_config->version >= 16)
   {
      wrapper->parse = pgmoneta_wal_parse_spg_xlog_vacuum_redirect_v16;
      wrapper->format = pgmoneta_wal_format_spg_xlog_vacuum_redirect_v16;
   }
   else
   {
      wrapper->parse = pgmoneta_wal_parse_spg_xlog_vacuum_redirect_v15;
      wrapper->format = pgmoneta_wal_format_spg_xlog_vacuum_redirect_v15;
   }

   return wrapper;
}

void
pgmoneta_wal_parse_spg_xlog_vacuum_redirect_v15(struct spg_xlog_vacuum_redirect* wrapper, const void* rec)
{
   memcpy(&wrapper->data.v15, rec, sizeof(struct spg_xlog_vacuum_redirect_v15));
}

void
pgmoneta_wal_parse_spg_xlog_vacuum_redirect_v16(struct spg_xlog_vacuum_redirect* wrapper, const void* rec)
{
   memcpy(&wrapper->data.v16, rec, sizeof(struct spg_xlog_vacuum_redirect_v16));
}

char*
pgmoneta_wal_format_spg_xlog_vacuum_redirect_v15(struct spg_xlog_vacuum_redirect* wrapper, char* buf)
{
   struct spg_xlog_vacuum_redirect_v15* xlrec = &wrapper->data.v15;
   buf = pgmoneta_format_and_append(buf, "ntoplaceholder: %u, firstplaceholder: %u, newestredirectxid: %u",
                                    xlrec->nToPlaceholder,
                                    xlrec->firstPlaceholder,
                                    xlrec->newestRedirectXid);
   return buf;
}

char*
pgmoneta_wal_format_spg_xlog_vacuum_redirect_v16(struct spg_xlog_vacuum_redirect* wrapper, char* buf)
{
   struct spg_xlog_vacuum_redirect_v16* xlrec = &wrapper->data.v16;
   buf = pgmoneta_format_and_append(buf, "ntoplaceholder: %u, firstplaceholder: %u, snapshot_conflict_horizon_id: %u",
                                    xlrec->n_to_placeholder,
                                    xlrec->first_placeholder,
                                    xlrec->snapshot_conflict_horizon);
   return buf;
}

char*
pgmoneta_wal_spg_desc(char* buf, struct decoded_xlog_record* record)
{
   char* rec = XLOG_REC_GET_DATA(record);
   uint8_t info = XLOG_REC_GET_INFO(record) & ~XLR_INFO_MASK;

   switch (info)
   {
      case XLOG_SPGIST_ADD_LEAF:
      {
         struct spg_xlog_add_leaf* xlrec = (struct spg_xlog_add_leaf*) rec;

         buf = pgmoneta_format_and_append(buf, "off: %u, headoff: %u, parentoff: %u, node_i: %u",
                                          xlrec->offnum_leaf, xlrec->offnum_head_leaf,
                                          xlrec->offnum_parent, xlrec->node_i);
         if (xlrec->new_page)
         {
            buf = pgmoneta_format_and_append(buf, " (newpage)");
         }
         if (xlrec->stores_nulls)
         {
            buf = pgmoneta_format_and_append(buf, " (nulls)");
         }
      }
      break;
      case XLOG_SPGIST_MOVE_LEAFS:
      {
         struct spg_xlog_move_leafs* xlrec = (struct spg_xlog_move_leafs*) rec;

         buf = pgmoneta_format_and_append(buf, "nmoves: %u, parentoff: %u, node_i: %u",
                                          xlrec->n_moves,
                                          xlrec->offnum_parent, xlrec->node_i);
         if (xlrec->new_page)
         {
            buf = pgmoneta_format_and_append(buf, " (newpage)");
         }
         if (xlrec->replace_dead)
         {
            buf = pgmoneta_format_and_append(buf, " (replacedead)");
         }
         if (xlrec->stores_nulls)
         {
            buf = pgmoneta_format_and_append(buf, " (nulls)");
         }
      }
      break;
      case XLOG_SPGIST_ADD_NODE:
      {
         struct spg_xlog_add_node* xlrec = (struct spg_xlog_add_node*) rec;

         buf = pgmoneta_format_and_append(buf, "off: %u, newoff: %u, parent_blk: %d, "
                                          "parentoff: %u, node_i: %u",
                                          xlrec->offnum,
                                          xlrec->offnum_new,
                                          xlrec->parent_blk,
                                          xlrec->offnum_parent,
                                          xlrec->node_i);
         if (xlrec->new_page)
         {
            buf = pgmoneta_format_and_append(buf, " (newpage)");
         }
      }
      break;
      case XLOG_SPGIST_SPLIT_TUPLE:
      {
         struct spg_xlog_split_tuple* xlrec = (struct spg_xlog_split_tuple*) rec;

         buf = pgmoneta_format_and_append(buf, "prefixoff: %u, postfixoff: %u",
                                          xlrec->offnum_prefix,
                                          xlrec->offnum_postfix);
         if (xlrec->new_page)
         {
            buf = pgmoneta_format_and_append(buf, " (newpage)");
         }
         if (xlrec->postfix_blk_same)
         {
            buf = pgmoneta_format_and_append(buf, " (same)");
         }
      }
      break;
      case XLOG_SPGIST_PICKSPLIT:
      {
         struct spg_xlog_pick_split* xlrec = (struct spg_xlog_pick_split*) rec;

         buf = pgmoneta_format_and_append(buf, "ndelete: %u, ninsert: %u, inneroff: %u, "
                                          "parentoff: %u, node_i: %u",
                                          xlrec->n_delete, xlrec->n_insert,
                                          xlrec->offnum_inner,
                                          xlrec->offnum_parent, xlrec->node_i);
         if (xlrec->inner_is_parent)
         {
            buf = pgmoneta_format_and_append(buf, " (inner_is_parent)");
         }
         if (xlrec->stores_nulls)
         {
            buf = pgmoneta_format_and_append(buf, " (nulls)");
         }
         if (xlrec->is_root_split)
         {
            buf = pgmoneta_format_and_append(buf, " (is_root_split)");
         }
      }
      break;
      case XLOG_SPGIST_VACUUM_LEAF:
      {
         struct spg_xlog_vacuum_leaf* xlrec = (struct spg_xlog_vacuum_leaf*) rec;

         buf = pgmoneta_format_and_append(buf, "ndead: %u, nplaceholder: %u, nmove: %u, nchain: %u",
                                          xlrec->n_dead, xlrec->n_placeholder,
                                          xlrec->n_move, xlrec->n_chain);
      }
      break;
      case XLOG_SPGIST_VACUUM_ROOT:
      {
         struct spg_xlog_vacuum_root* xlrec = (struct spg_xlog_vacuum_root*) rec;

         buf = pgmoneta_format_and_append(buf, "ndelete: %u",
                                          xlrec->n_delete);
      }
      break;
      case XLOG_SPGIST_VACUUM_REDIRECT:
      {
         struct spg_xlog_vacuum_redirect* xlrec = create_spg_xlog_vacuum_redirect();
         xlrec->parse(xlrec, rec);
         buf = xlrec->format(xlrec, buf);
         free(xlrec);
      }
      break;
   }
   return buf;
}
