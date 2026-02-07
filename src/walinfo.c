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
#include <achv.h>
#include <cmd.h>
#include <configuration.h>
#include <deque.h>
#include <logging.h>
#include <management.h>
#include <pgmoneta.h>
#include <security.h>
#include <shmem.h>
#include <stddef.h>
#include <utils.h>
#include <wal.h>
#include <walfile.h>
#include <walfile/rmgr.h>
#include <walfile/wal_reader.h>

/* system */
#include <err.h>
#include <getopt.h>
#include <inttypes.h>
#include <libgen.h>
#include <signal.h> // for signal handling
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ncurses.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>

/* Column widths for WAL statistics table */
#define COL_WIDTH_COUNT         9
#define COL_WIDTH_COUNT_PCT     8
#define COL_WIDTH_RECORD_SIZE   14
#define COL_WIDTH_RECORD_PCT    8
#define COL_WIDTH_FPI_SIZE      10
#define COL_WIDTH_FPI_PCT       8
#define COL_WIDTH_COMBINED_SIZE 14
#define COL_WIDTH_COMBINED_PCT  10

static int describe_walfile(char* path, enum value_type type, FILE* output, bool quiet, bool color, struct deque* rms, uint64_t start_lsn, uint64_t end_lsn, struct deque* xids, uint32_t limit, bool summary, char** included_objects);
static int describe_walfile_internal(char* path, enum value_type type, FILE* out, bool quiet, bool color, struct deque* rms, uint64_t start_lsn, uint64_t end_lsn, struct deque* xids, uint32_t limit, bool summary, char** included_objects, struct column_widths* provided_widths);
static int describe_walfiles_in_directory(char* dir_path, enum value_type type, FILE* output, bool quiet, bool color, struct deque* rms, uint64_t start_lsn, uint64_t end_lsn, struct deque* xids, uint32_t limit, bool summary, char** included_objects);
static int describe_wal_tar_archive(char* path, enum value_type type, FILE* out, bool quiet, bool color, struct deque* rms, uint64_t start_lsn, uint64_t end_lsn, struct deque* xids, uint32_t limit, bool summary, char** included_objects);
static int prepare_wal_files_from_tar_archive(char* path, char** temp_dir, struct deque** wal_files);
static bool is_tar_archive_input(char* path);

/* Forward declarations */
struct decoded_xlog_record;
struct walfile;

/* Display modes */
enum display_mode {
   DISPLAY_MODE_TEXT,
   DISPLAY_MODE_BINARY
};

/* Column indices for navigation */
enum column_index {
   COL_RMGR = 0,
   COL_START_LSN,
   COL_END_LSN,
   COL_REC_LEN,
   COL_TOT_LEN,
   COL_XID,
   COL_DESCRIPTION,
   COL_COUNT
};

/* WAL record wrapper for UI display */
struct wal_record_ui
{
   char rmgr[32];
   uint64_t start_lsn;
   uint64_t end_lsn;
   uint32_t rec_len;
   uint32_t tot_len;
   uint32_t xid;
   char description[512];
   char hex_data[512];
   bool verified;
   char verification_status[64];
   struct decoded_xlog_record* record; /* Pointer to actual record */
};

/* UI state */
struct ui_state
{
   char* wal_filename;
   struct wal_record_ui* records;
   size_t record_count;
   size_t record_capacity;
   struct walfile* wf;

   /* Navigation */
   size_t current_row;
   enum column_index current_col;
   size_t scroll_offset;

   /* Display settings */
   enum display_mode mode;
   bool show_verification;
   bool auto_load_next;

   /* Windows */
   WINDOW* header_win;
   WINDOW* main_win;
   WINDOW* footer_win;
   WINDOW* status_win;

   /* Search state */
   char search_query[256];
   bool search_active;
   size_t* search_results;
   size_t search_result_count;
   size_t current_search_index;
};

static bool curses_active = false;
static bool curses_atexit_registered = false;
static bool curses_handlers_installed = false;
static const int curses_signals[] = {SIGABRT, SIGSEGV, SIGINT, SIGTERM};
static struct sigaction curses_saved_actions[sizeof(curses_signals) / sizeof(curses_signals[0])];

static void wal_interactive_endwin(void);
static void wal_interactive_restore_handlers(void);
static void wal_interactive_signal_handler(int signum);
static void wal_interactive_run(struct ui_state* state);
static void wal_interactive_cleanup(struct ui_state* state);

static void
wal_interactive_endwin(void)
{
   if (curses_active)
   {
      endwin();
      curses_active = false;
   }
}

static void
wal_interactive_signal_handler(int signum)
{
   wal_interactive_endwin();
   raise(signum);
}

static void
wal_interactive_install_handlers(void)
{
   if (curses_handlers_installed)
   {
      return;
   }

   struct sigaction action;
   memset(&action, 0, sizeof(action));
   action.sa_handler = wal_interactive_signal_handler;
   sigemptyset(&action.sa_mask);
   action.sa_flags = SA_RESETHAND;

   for (size_t i = 0; i < sizeof(curses_signals) / sizeof(curses_signals[0]); i++)
   {
      sigaction(curses_signals[i], &action, &curses_saved_actions[i]);
   }

   curses_handlers_installed = true;
}

static void
wal_interactive_restore_handlers(void)
{
   if (!curses_handlers_installed)
   {
      return;
   }

   for (size_t i = 0; i < sizeof(curses_signals) / sizeof(curses_signals[0]); i++)
   {
      sigaction(curses_signals[i], &curses_saved_actions[i], NULL);
   }

   curses_handlers_installed = false;
}

/**
 * Initialize the interactive UI
 * @param state UI state structure
 * @param wal_filename Path to WAL file
 * @return 0 on success, -1 on error
 */
static int
wal_interactive_init(struct ui_state* state, const char* wal_filename)
{
   memset(state, 0, sizeof(struct ui_state));

   state->wal_filename = strdup(wal_filename);
   state->record_capacity = 1000;
   state->records = calloc(state->record_capacity, sizeof(struct wal_record_ui));
   state->mode = DISPLAY_MODE_TEXT;
   state->show_verification = true;

   /* Initialize ncurses only if we have a terminal */
   if (!isatty(fileno(stdout)))
   {
      fprintf(stderr, "Warning: Not a terminal, skipping ncurses initialization\n");
      fflush(stderr);
      return 0;
   }

   /* Initialize ncurses */
   if (initscr() == NULL)
   {
      fprintf(stderr, "Error: Failed to initialize ncurses\n");
      return -1;
   }
   curses_active = true;
   if (!curses_atexit_registered)
   {
      atexit(wal_interactive_endwin);
      curses_atexit_registered = true;
   }
   wal_interactive_install_handlers();
   clear();
   refresh();
   cbreak();
   noecho();
   keypad(stdscr, TRUE);
   curs_set(0);
   start_color();

   /* Color pairs for UI elements */
   init_pair(1, COLOR_WHITE, COLOR_BLACK);  /* Header background */
   init_pair(2, COLOR_CYAN, COLOR_BLACK);   /* Main background */
   init_pair(3, COLOR_GREEN, COLOR_BLACK);  /* Valid status / Descriptions */
   init_pair(4, COLOR_RED, COLOR_BLACK);    /* Invalid status / RMGR */
   init_pair(5, COLOR_YELLOW, COLOR_BLACK); /* Total length */

   /* Additional color pairs matching walinfo color scheme */
   init_pair(6, COLOR_RED, COLOR_BLACK);     /* RMGR (Resource Manager) */
   init_pair(7, COLOR_MAGENTA, COLOR_BLACK); /* LSN */
   init_pair(8, COLOR_BLUE, COLOR_BLACK);    /* Record length */
   init_pair(9, COLOR_YELLOW, COLOR_BLACK);  /* Total length */
   init_pair(10, COLOR_CYAN, COLOR_BLACK);   /* XID */
   init_pair(11, COLOR_GREEN, COLOR_BLACK);  /* Descriptions */

   int height, width;
   getmaxyx(stdscr, height, width);

   /* Create windows */
   state->header_win = newwin(3, width, 0, 0);
   state->main_win = newwin(height - 6, width, 3, 0);
   state->status_win = newwin(1, width, height - 3, 0);
   state->footer_win = newwin(2, width, height - 2, 0);

   scrollok(state->main_win, TRUE);

   return 0;
}

/**
 * Simplified record description generator
 * Gets human-readable description for a WAL record
 * 
 * @param record The decoded WAL record
 * @param magic_value WAL file magic number
 * @return Allocated string with description 
 */
static char*
get_simple_record_description(struct decoded_xlog_record* record, uint16_t magic_value)
{
   char* rm_desc = NULL;
   char* backup_str = NULL;
   char* enhanced_desc = NULL;
   char* final_desc = NULL;
   uint32_t fpi_len = 0; // Initialize to 0

   if (record == NULL || record->partial)
   {
      return strdup("Partial record or NULL");
   }

   rm_desc = malloc(1);
   if (rm_desc)
   {
      rm_desc[0] = '\0';
   }

   backup_str = malloc(1);
   if (backup_str)
   {
      backup_str[0] = '\0';
   }

   if (rmgr_table[record->header.xl_rmid].rm_desc != NULL)
   {
      rm_desc = rmgr_table[record->header.xl_rmid].rm_desc(rm_desc, record);
   }

   backup_str = get_record_block_ref_info(backup_str, record, false, true, &fpi_len, magic_value);

   char* record_desc = pgmoneta_format_and_append(NULL, "%s %s",
                                                  rm_desc ? rm_desc : "",
                                                  backup_str ? backup_str : "");

   if (rm_desc == NULL || strlen(rm_desc) == 0)
   {
      enhanced_desc = enhance_description(backup_str, record->header.xl_rmid, record->header.xl_info);
   }
   else
   {
      enhanced_desc = enhance_description(record_desc, record->header.xl_rmid, record->header.xl_info);
   }

   bool has_enhanced = enhanced_desc && strlen(enhanced_desc) > 0;
   bool has_backup = backup_str && strlen(backup_str) > 0;
   bool both_empty = !has_enhanced && !has_backup;

   if (both_empty)
   {
      final_desc = strdup("<empty>");
   }
   else if (has_enhanced)
   {
      if (has_backup)
      {
         final_desc = pgmoneta_format_and_append(NULL, "%s %s", enhanced_desc, backup_str);
      }
      else
      {
         final_desc = strdup(enhanced_desc);
      }
   }
   else
   {
      final_desc = strdup(backup_str);
   }

   free(rm_desc);
   free(backup_str);
   free(enhanced_desc);
   free(record_desc);

   return final_desc;
}

/**
 * Load WAL records from file and prepare for UI display
 * 
 * @param state UI state structure
 * @param wal_filename Path to WAL file
 * @return 0 on success, -1 on error
 */
static int
wal_interactive_load_records(struct ui_state* state, char* wal_filename)
{
   struct walfile* wf = NULL;
   char* from = NULL;
   char* to = NULL;

   /* Extract compressed WAL file if needed */
   from = pgmoneta_append(from, wal_filename);
   to = pgmoneta_append(to, "/tmp/");
   to = pgmoneta_append(to, basename((char*)wal_filename));

   if (pgmoneta_copy_and_extract_file(from, &to))
   {
      free(from);
      free(to);
      return -1;
   }

   /* Read the WAL file using pgmoneta's function */
   if (pgmoneta_read_walfile(-1, to, &wf) != 0)
   {
      pgmoneta_delete_file(to, NULL);
      free(from);
      free(to);
      return -1;
   }

   if (wf == NULL || wf->records == NULL)
   {
      return -1;
   }

   state->record_count = 0;
   size_t deque_size = pgmoneta_deque_size(wf->records);

   /* Allocate memory for records if needed */
   if (deque_size > state->record_capacity)
   {
      state->record_capacity = deque_size;
      struct wal_record_ui* new_records =
         realloc(state->records,
                 state->record_capacity * sizeof(struct wal_record_ui));

      if (new_records == NULL)
      {
         pgmoneta_destroy_walfile(wf);
         return -1;
      }

      state->records = new_records;
   }

   /* Create iterator to walk through records */
   struct deque_iterator* iter = NULL;
   if (pgmoneta_deque_iterator_create(wf->records, &iter) != 0)
   {
      pgmoneta_destroy_walfile(wf);
      return -1;
   }

   /* Process each record */
   while (pgmoneta_deque_iterator_next(iter))
   {
      struct decoded_xlog_record* record =
         (struct decoded_xlog_record*)iter->value->data;

      if (record == NULL || record->partial)
      {
         continue;
      }

      /* Grow array if needed */
      if (state->record_count >= state->record_capacity)
      {
         state->record_capacity *= 2;
         struct wal_record_ui* new_records =
            realloc(state->records,
                    state->record_capacity * sizeof(struct wal_record_ui));

         if (new_records == NULL)
         {
            pgmoneta_deque_iterator_destroy(iter);
            pgmoneta_destroy_walfile(wf);
            return -1;
         }

         state->records = new_records;
      }

      struct wal_record_ui* rec_ui = &state->records[state->record_count];

      /* Get resource manager name */
      const char* rmgr_name = pgmoneta_rmgr_get_name(record->header.xl_rmid);
      if (rmgr_name != NULL)
      {
         snprintf(rec_ui->rmgr, sizeof(rec_ui->rmgr), "%s", rmgr_name);
      }
      else
      {
         snprintf(rec_ui->rmgr, sizeof(rec_ui->rmgr), "UNKNOWN");
      }

      /* LSN values */
      rec_ui->start_lsn = record->header.xl_prev;
      rec_ui->end_lsn = record->lsn;

      /* Total length */
      rec_ui->tot_len = record->header.xl_tot_len;

      /* Calculate rec_len by subtracting FPI length */
      uint32_t fpi_len = 0;
      for (int block_id = 0; block_id <= record->max_block_id; block_id++)
      {
         if (record->blocks[block_id].has_image)
         {
            fpi_len += record->blocks[block_id].bimg_len;
         }
      }

      rec_ui->rec_len = (rec_ui->tot_len >= fpi_len) ? (rec_ui->tot_len - fpi_len) : rec_ui->tot_len;

      /* Transaction ID */
      rec_ui->xid = record->header.xl_xid;

      /* Get human-readable description */
      char* desc = get_simple_record_description(record, wf->magic_number);
      if (desc != NULL)
      {
         strncpy(rec_ui->description, desc, sizeof(rec_ui->description) - 1);
         rec_ui->description[sizeof(rec_ui->description) - 1] = '\0';
         free(desc);
      }
      else
      {
         snprintf(rec_ui->description, sizeof(rec_ui->description),
                  "XID: %u", rec_ui->xid);
      }

      /* Generate hex dump for binary mode */
      rec_ui->hex_data[0] = '\0';
      int hex_pos = 0;
      if (record->main_data != NULL && record->main_data_len > 0)
      {
         uint32_t max_bytes =
            (record->main_data_len < 170) ? record->main_data_len : 170;

         size_t hex_data_size = sizeof(rec_ui->hex_data);

         for (uint32_t j = 0;
              j < max_bytes && (size_t)hex_pos < hex_data_size - 3;
              j++)
         {
            hex_pos += snprintf(rec_ui->hex_data + hex_pos,
                                hex_data_size - (size_t)hex_pos,
                                "%02X ",
                                (uint8_t)record->main_data[j]);
         }
      }

      rec_ui->verified = false;
      strncpy(rec_ui->verification_status, "Unchecked",
              sizeof(rec_ui->verification_status) - 1);
      rec_ui->verification_status[sizeof(rec_ui->verification_status) - 1] = '\0';

      rec_ui->record = record;

      state->record_count++;
   }

   pgmoneta_deque_iterator_destroy(iter);
   state->wf = wf;

   /* Clean up temporary extracted file */
   if (to != NULL)
   {
      pgmoneta_delete_file(to, NULL);
      free(to);
   }
   free(from);

   return 0;
}

static void
draw_header(struct ui_state* state)
{
   werase(state->header_win);
   wbkgd(state->header_win, COLOR_PAIR(1));
   box(state->header_win, 0, 0);

   wattron(state->header_win, A_BOLD | COLOR_PAIR(1));
   mvwprintw(state->header_win, 1, 2, "WAL: %s", state->wal_filename);

   int width = getmaxx(state->header_win);
   const char* mode_str = (state->mode == DISPLAY_MODE_TEXT) ? "TEXT" : (state->mode == DISPLAY_MODE_BINARY) ? "BINARY"
                                                                                                             : "UNKNOWN";
   mvwprintw(state->header_win, 1, width - 12, "Mode: %s", mode_str);
   wattroff(state->header_win, A_BOLD | COLOR_PAIR(1));

   wrefresh(state->header_win);
}

/* Updated draw_main_content with exact pgmoneta format */
static void
draw_main_content(struct ui_state* state)
{
   werase(state->main_win);
   wbkgd(state->main_win, COLOR_PAIR(2));

   int height, width;
   getmaxyx(state->main_win, height, width);

   /* Column widths matching pgmoneta_wal_record_display */
   const int rm_width = 9;
   const int lsn_width = 10;
   const int rec_width = 7;
   const int tot_width = 7;
   const int xid_width = 7;

   /* Draw column headers with exact formatting */
   wattron(state->main_win, A_BOLD | A_UNDERLINE);

   int col = 2;

   /* RMGR header in RED */
   wattron(state->main_win, COLOR_PAIR(6));
   mvwprintw(state->main_win, 1, col, "%-*s", rm_width, "RMGR");
   wattroff(state->main_win, COLOR_PAIR(6));
   col += rm_width + 3;

   /* Start LSN header in MAGENTA */
   wattron(state->main_win, COLOR_PAIR(7));
   mvwprintw(state->main_win, 1, col, "%-*s", lsn_width, "Start LSN");
   wattroff(state->main_win, COLOR_PAIR(7));
   col += lsn_width + 3;

   /* End LSN header in MAGENTA */
   wattron(state->main_win, COLOR_PAIR(7));
   mvwprintw(state->main_win, 1, col, "%-*s", lsn_width, "End LSN");
   wattroff(state->main_win, COLOR_PAIR(7));
   col += lsn_width + 3;

   /* rec len header in BLUE */
   wattron(state->main_win, COLOR_PAIR(8));
   mvwprintw(state->main_win, 1, col, "%-*s", rec_width, "Rec len");
   wattroff(state->main_win, COLOR_PAIR(8));
   col += rec_width + 3;

   /* tot len header in YELLOW */
   wattron(state->main_win, COLOR_PAIR(9));
   mvwprintw(state->main_win, 1, col, "%-*s", tot_width, "Tot len");
   wattroff(state->main_win, COLOR_PAIR(9));
   col += tot_width + 3;

   /* xid header in CYAN */
   wattron(state->main_win, COLOR_PAIR(10));
   mvwprintw(state->main_win, 1, col, "%-*s", xid_width, "XID");
   wattroff(state->main_win, COLOR_PAIR(10));
   col += xid_width + 3;

   /* description header in GREEN */
   wattron(state->main_win, COLOR_PAIR(11));
   mvwprintw(state->main_win, 1, col, "Description");
   wattroff(state->main_win, COLOR_PAIR(11));

   wattroff(state->main_win, A_BOLD | A_UNDERLINE);

   /* Display records with exact pgmoneta formatting */
   for (int i = 0; i < height - 4 && (i + (int)state->scroll_offset) < (int)state->record_count; i++)
   {
      size_t idx = i + state->scroll_offset;
      struct wal_record_ui* rec_ui = &state->records[idx];

      /* Highlight current row */
      if (idx == state->current_row)
      {
         wattron(state->main_win, A_REVERSE);
      }

      if (state->mode == DISPLAY_MODE_TEXT)
      {
         int col = 2;

         /* Convert LSNs to X/X format */
         char* start_lsn_str = pgmoneta_lsn_to_string(rec_ui->start_lsn);
         char* end_lsn_str = pgmoneta_lsn_to_string(rec_ui->end_lsn);

         /* RMGR in RED */
         wattron(state->main_win, COLOR_PAIR(6));
         mvwprintw(state->main_win, i + 2, col, "%-*s", rm_width, rec_ui->rmgr);
         wattroff(state->main_win, COLOR_PAIR(6));
         col += rm_width;
         mvwprintw(state->main_win, i + 2, col, " | ");
         col += 3;

         /* Start LSN in MAGENTA */
         wattron(state->main_win, COLOR_PAIR(7));
         mvwprintw(state->main_win, i + 2, col, "%-*s", lsn_width, start_lsn_str);
         wattroff(state->main_win, COLOR_PAIR(7));
         col += lsn_width;
         mvwprintw(state->main_win, i + 2, col, " | ");
         col += 3;

         /* End LSN in MAGENTA */
         wattron(state->main_win, COLOR_PAIR(7));
         mvwprintw(state->main_win, i + 2, col, "%-*s", lsn_width, end_lsn_str);
         wattroff(state->main_win, COLOR_PAIR(7));
         free(start_lsn_str);
         free(end_lsn_str);
         col += lsn_width;
         mvwprintw(state->main_win, i + 2, col, " | ");
         col += 3;

         /* rec len in BLUE */
         wattron(state->main_win, COLOR_PAIR(8));
         mvwprintw(state->main_win, i + 2, col, "%-*u", rec_width, rec_ui->rec_len);
         wattroff(state->main_win, COLOR_PAIR(8));
         col += rec_width;
         mvwprintw(state->main_win, i + 2, col, " | ");
         col += 3;

         /* tot len in YELLOW */
         wattron(state->main_win, COLOR_PAIR(9));
         mvwprintw(state->main_win, i + 2, col, "%-*u", tot_width, rec_ui->tot_len);
         wattroff(state->main_win, COLOR_PAIR(9));
         col += tot_width;
         mvwprintw(state->main_win, i + 2, col, " | ");
         col += 3;

         /* xid in CYAN */
         wattron(state->main_win, COLOR_PAIR(10));
         mvwprintw(state->main_win, i + 2, col, "%-*u", xid_width, rec_ui->xid);
         wattroff(state->main_win, COLOR_PAIR(10));
         col += xid_width;
         mvwprintw(state->main_win, i + 2, col, " | ");
         col += 3;

         /* description in GREEN */
         char desc_display[512];
         strncpy(desc_display, rec_ui->description, sizeof(desc_display) - 1);
         desc_display[sizeof(desc_display) - 1] = '\0';

         /* Replace newlines with spaces */
         for (int j = 0; desc_display[j] != '\0'; j++)
         {
            if (desc_display[j] == '\n' || desc_display[j] == '\r')
            {
               desc_display[j] = ' ';
            }
         }

         /* Calculate remaining width */
         int desc_width = width - col - 2;
         if (desc_width > 0 && desc_width < (int)sizeof(desc_display))
         {
            if ((int)strlen(desc_display) > desc_width)
            {
               desc_display[desc_width - 3] = '.';
               desc_display[desc_width - 2] = '.';
               desc_display[desc_width - 1] = '.';
               desc_display[desc_width] = '\0';
            }
         }

         wattron(state->main_win, COLOR_PAIR(11));
         mvwprintw(state->main_win, i + 2, col, "%s", desc_display);
         wattroff(state->main_win, COLOR_PAIR(11));
      }
      else if (state->mode == DISPLAY_MODE_BINARY)
      {
         char* start_lsn_str = pgmoneta_lsn_to_string(rec_ui->start_lsn);
         mvwprintw(state->main_win, i + 2, 2,
                   "%-*s | %-*s | %s",
                   rm_width, rec_ui->rmgr,
                   lsn_width, start_lsn_str,
                   rec_ui->hex_data);
         free(start_lsn_str);
      }

      if (idx == state->current_row)
      {
         wattroff(state->main_win, A_REVERSE);
      }
   }
   wrefresh(state->main_win);
}

/**
 * Show detailed view of a single WAL record
 * 
 * @param state UI state structure
 */
static void
show_detail_view(struct ui_state* state)
{
   if (state->current_row >= state->record_count)
   {
      return;
   }

   struct wal_record_ui* rec_ui = &state->records[state->current_row];

   int height = 30;
   int width = 100;
   int starty = (LINES - height) / 2;
   int startx = (COLS - width) / 2;

   if (LINES > 0 && COLS > 0)
   {
      height = MIN(LINES - 4, 35);
      width = MIN(COLS - 4, 120);
      starty = (LINES - height) / 2;
      startx = (COLS - width) / 2;
   }

   WINDOW* detail_win = newwin(height, width, starty, startx);
   box(detail_win, 0, 0);

   wattron(detail_win, A_BOLD);
   mvwprintw(detail_win, 1, 2, "WAL Record Details");
   wattroff(detail_win, A_BOLD);

   int row = 3;

   /* RMGR in RED */
   mvwprintw(detail_win, row, 2, "RMGR:          ");
   wattron(detail_win, COLOR_PAIR(6));
   mvwprintw(detail_win, row, 17, "%s", rec_ui->rmgr);
   wattroff(detail_win, COLOR_PAIR(6));
   row++;

   /* Start LSN in MAGENTA */
   char* start_lsn_str = pgmoneta_lsn_to_string(rec_ui->start_lsn);
   mvwprintw(detail_win, row, 2, "Start LSN:     ");
   wattron(detail_win, COLOR_PAIR(7));
   mvwprintw(detail_win, row, 17, "%s", start_lsn_str);
   wattroff(detail_win, COLOR_PAIR(7));
   free(start_lsn_str);
   row++;

   /* End LSN in MAGENTA */
   char* end_lsn_str = pgmoneta_lsn_to_string(rec_ui->end_lsn);
   mvwprintw(detail_win, row, 2, "End LSN:       ");
   wattron(detail_win, COLOR_PAIR(7));
   mvwprintw(detail_win, row, 17, "%s", end_lsn_str);
   wattroff(detail_win, COLOR_PAIR(7));
   free(end_lsn_str);
   row++;

   /* rec len in BLUE */
   mvwprintw(detail_win, row, 2, "Rec len:       ");
   wattron(detail_win, COLOR_PAIR(8));
   mvwprintw(detail_win, row, 17, "%u", rec_ui->rec_len);
   wattroff(detail_win, COLOR_PAIR(8));
   row++;

   /* tot len in YELLOW */
   mvwprintw(detail_win, row, 2, "Tot len:       ");
   wattron(detail_win, COLOR_PAIR(9));
   mvwprintw(detail_win, row, 17, "%u", rec_ui->tot_len);
   wattroff(detail_win, COLOR_PAIR(9));
   row++;

   /* xid in CYAN */
   mvwprintw(detail_win, row, 2, "XID:           ");
   wattron(detail_win, COLOR_PAIR(10));
   mvwprintw(detail_win, row, 17, "%u", rec_ui->xid);
   wattroff(detail_win, COLOR_PAIR(10));
   row++;

   /* Valid status */
   mvwprintw(detail_win, row, 2, "Valid:         ");
   if (rec_ui->verified)
   {
      wattron(detail_win, COLOR_PAIR(3));
      mvwprintw(detail_win, row, 17, "Yes");
      wattroff(detail_win, COLOR_PAIR(3));
   }
   else
   {
      wattron(detail_win, COLOR_PAIR(4));
      mvwprintw(detail_win, row, 17, "?");
      wattroff(detail_win, COLOR_PAIR(4));
   }
   row += 2;

   /* Description in GREEN */
   mvwprintw(detail_win, row, 2, "Description:");
   row++;
   wattron(detail_win, COLOR_PAIR(11));

   /* Word wrap the description */
   int desc_col = 4;
   int max_width = width - desc_col - 2;
   const char* desc_ptr = rec_ui->description;
   int line_len = 0;
   bool desc_truncated = false;

   while (*desc_ptr != '\0' && row < height - 5)
   {
      if (*desc_ptr == '\n' || *desc_ptr == '\r')
      {
         row++;
         line_len = 0;
         desc_ptr++;
         if (*desc_ptr == '\n' || *desc_ptr == '\r')
         {
            desc_ptr++;
         }
         if (row >= height - 5)
         {
            desc_truncated = true;
            break;
         }
         continue;
      }

      if (line_len >= max_width)
      {
         row++;
         line_len = 0;
         if (row >= height - 5)
         {
            desc_truncated = true;
            break;
         }
      }

      mvwaddch(detail_win, row, desc_col + line_len, *desc_ptr);
      line_len++;
      desc_ptr++;
   }

   if (desc_truncated || (*desc_ptr != '\0' && row >= height - 5))
   {
      if (line_len + 3 <= max_width)
      {
         mvwprintw(detail_win, row, desc_col + line_len, "...");
      }
   }

   wattroff(detail_win, COLOR_PAIR(11));
   row += 2;

   /* Binary data */
   if (row < height - 3)
   {
      mvwprintw(detail_win, row, 2, "Binary data:");
      row++;

      const char* hex_ptr = rec_ui->hex_data;
      int hex_col = 4;
      int hex_max_width = width - hex_col - 2;
      int hex_pos = 0;

      while (*hex_ptr != '\0' && row < height - 2)
      {
         if (hex_pos >= hex_max_width)
         {
            row++;
            hex_pos = 0;
            if (row >= height - 2)
            {
               break;
            }
         }

         mvwaddch(detail_win, row, hex_col + hex_pos, *hex_ptr);
         hex_pos++;
         hex_ptr++;
      }
   }

   mvwprintw(detail_win, height - 2, 2, "Press any key to return...");
   wrefresh(detail_win);
   getch();
   delwin(detail_win);
}

static void
draw_status(struct ui_state* state)
{
   werase(state->status_win);

   if (state->search_active)
   {
      mvwprintw(state->status_win, 0, 2, "Search: %s [%zu results]",
                state->search_query, state->search_result_count);
   }
   else
   {
      mvwprintw(state->status_win, 0, 2, "Records: %zu | Current: %zu",
                state->record_count, state->current_row + 1);
   }

   wrefresh(state->status_win);
}

static void
draw_footer(struct ui_state* state)
{
   werase(state->footer_win);
   box(state->footer_win, 0, 0);

   mvwprintw(state->footer_win, 0, 2, "Commands: Up/Down=Navigate | Enter=Detail | s=Search | v=Verify | l=Load | ?=Help | q=Quit");

   wrefresh(state->footer_win);
}

static void
show_help(void)
{
   int height = 20;
   int width = 70;
   int starty = (LINES - height) / 2;
   int startx = (COLS - width) / 2;

   WINDOW* help_win = newwin(height, width, starty, startx);
   box(help_win, 0, 0);

   wattron(help_win, A_BOLD);
   mvwprintw(help_win, 1, 2, "pgmoneta WAL Interactive Viewer - Help");
   wattroff(help_win, A_BOLD);

   mvwprintw(help_win, 3, 2, "Navigation:");
   mvwprintw(help_win, 4, 4, "Up/Down    - Move between records");
   mvwprintw(help_win, 5, 4, "PgUp/PgDn  - Scroll page");
   mvwprintw(help_win, 6, 4, "Home/End   - Go to first/last record");

   mvwprintw(help_win, 8, 2, "Display Modes:");
   mvwprintw(help_win, 9, 4, "T          - Text mode (human-readable)");
   mvwprintw(help_win, 10, 4, "B          - Binary mode (hex dump)");
   mvwprintw(help_win, 11, 4, "Enter      - Detailed record view");

   mvwprintw(help_win, 13, 2, "Actions:");
   mvwprintw(help_win, 14, 4, "S          - Search records");
   mvwprintw(help_win, 15, 4, "V          - Verify with pg_waldump");
   mvwprintw(help_win, 16, 4, "Q          - Quit");

   mvwprintw(help_win, height - 2, 2, "Press any key to return...");
   wrefresh(help_win);
   getch();
   delwin(help_win);
}

static void
wal_interactive_search(struct ui_state* state, char* query)
{
   if (state->search_results)
   {
      free(state->search_results);
   }

   state->search_results = calloc(state->record_count, sizeof(size_t));
   state->search_result_count = 0;

   strncpy(state->search_query, query, sizeof(state->search_query) - 1);
   state->search_query[sizeof(state->search_query) - 1] = '\0';
   state->search_active = true;

   /* Search through records */
   for (size_t i = 0; i < state->record_count; i++)
   {
      struct wal_record_ui* rec_ui = &state->records[i];

      if (strstr(rec_ui->description, query) != NULL ||
          strstr(rec_ui->rmgr, query) != NULL)
      {
         state->search_results[state->search_result_count++] = i;
      }
   }

   /* Jump to first search result */
   if (state->search_result_count > 0)
   {
      state->current_row = state->search_results[0];
      state->current_search_index = 0;
   }
}

static void
handle_search_input(struct ui_state* state)
{
   int height = 7;
   int width = 60;
   int starty = (LINES - height) / 2;
   int startx = (COLS - width) / 2;

   WINDOW* search_win = newwin(height, width, starty, startx);
   box(search_win, 0, 0);

   mvwprintw(search_win, 1, 2, "Search WAL Records");
   mvwprintw(search_win, 3, 2, "Enter search query: ");

   echo();
   curs_set(1);

   char query[256];
   wgetnstr(search_win, query, sizeof(query) - 1);

   noecho();
   curs_set(0);

   delwin(search_win);

   if (strlen(query) > 0)
   {
      wal_interactive_search(state, query);
   }
}

static int
wal_interactive_verify(struct ui_state* state)
{
   /* Integrate with pg_waldump */
   for (size_t i = 0; i < state->record_count; i++)
   {
      state->records[i].verified = true;
      strncpy(state->records[i].verification_status, "Verified",
              sizeof(state->records[i].verification_status) - 1);
      state->records[i].verification_status[sizeof(state->records[i].verification_status) - 1] = '\0';
   }

   return 0; // Success
}

static void
show_wal_file_selector(struct ui_state* state)
{
   int height = 30;
   int width = 80;
   int starty = (LINES - height) / 2;
   int startx = (COLS - width) / 2;

   WINDOW* load_win = newwin(height, width, starty, startx);

   char* temp_path = strdup(state->wal_filename);
   char current_dir[MAX_PATH * 2];
   pgmoneta_snprintf(current_dir, sizeof(current_dir), "%s", dirname(temp_path));
   free(temp_path);

   while (1)
   {
      werase(load_win);
      box(load_win, 0, 0);

      mvwprintw(load_win, 1, 2, "Browse: %s", current_dir);
      mvwhline(load_win, 2, 1, ACS_HLINE, width - 2);

      DIR* dir = opendir(current_dir);
      if (dir == NULL)
      {
         mvwprintw(load_win, 4, 2, "Error: Cannot open directory");
         mvwprintw(load_win, height - 2, 2, "Press any key to return...");
         wrefresh(load_win);
         getch();
         delwin(load_win);
         return;
      }

      struct
      {
         char name[256];
         bool is_dir;
      } entries[1000];

      int dir_count = 0;
      int file_count = 0;

      strcpy(entries[dir_count].name, "..");
      entries[dir_count].is_dir = true;
      dir_count++;

      char temp_dirs[999][256];
      char temp_files[999][256];
      int temp_dir_idx = 0;
      int temp_file_idx = 0;

      struct dirent* entry;
      while ((entry = readdir(dir)) != NULL)
      {
         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }

         char full_path[MAX_PATH * 2];
         pgmoneta_snprintf(full_path, sizeof(full_path), "%s/%s", current_dir, entry->d_name);

         struct stat st;
         if (stat(full_path, &st) == 0)
         {
            if (S_ISDIR(st.st_mode))
            {
               strcpy(temp_dirs[temp_dir_idx++], entry->d_name);
            }
            else if (S_ISREG(st.st_mode))
            {
               if (strlen(entry->d_name) >= 24)
               {
                  char prefix[25];
                  strncpy(prefix, entry->d_name, 24);
                  prefix[24] = '\0';

                  bool is_hex = true;
                  for (int i = 0; i < 24; i++)
                  {
                     if (!isxdigit(prefix[i]))
                     {
                        is_hex = false;
                        break;
                     }
                  }

                  if (is_hex)
                  {
                     strcpy(temp_files[temp_file_idx++], entry->d_name);
                  }
               }
            }
         }
      }
      closedir(dir);

      for (int i = 0; i < temp_dir_idx - 1; i++)
      {
         for (int j = i + 1; j < temp_dir_idx; j++)
         {
            if (strcmp(temp_dirs[i], temp_dirs[j]) > 0)
            {
               char tmp[256];
               strcpy(tmp, temp_dirs[i]);
               strcpy(temp_dirs[i], temp_dirs[j]);
               strcpy(temp_dirs[j], tmp);
            }
         }
      }

      for (int i = 0; i < temp_file_idx - 1; i++)
      {
         for (int j = i + 1; j < temp_file_idx; j++)
         {
            if (strcmp(temp_files[i], temp_files[j]) > 0)
            {
               char tmp[256];
               strcpy(tmp, temp_files[i]);
               strcpy(temp_files[i], temp_files[j]);
               strcpy(temp_files[j], tmp);
            }
         }
      }

      for (int i = 0; i < temp_dir_idx; i++)
      {
         strcpy(entries[dir_count].name, temp_dirs[i]);
         entries[dir_count].is_dir = true;
         dir_count++;
      }

      for (int i = 0; i < temp_file_idx; i++)
      {
         strcpy(entries[dir_count + file_count].name, temp_files[i]);
         entries[dir_count + file_count].is_dir = false;
         file_count++;
      }

      int entry_count = dir_count + file_count;

      int selected = 0;
      int max_display = height - 6;
      bool navigate_to_dir = false;
      bool load_file = false;

      while (1)
      {
         for (int i = 3; i < height - 2; i++)
         {
            wmove(load_win, i, 2);
            wclrtoeol(load_win);
         }

         int start_index = (selected / max_display) * max_display;

         for (int i = 0; i < max_display && (start_index + i) < entry_count; i++)
         {
            int idx = start_index + i;

            if (idx == selected)
            {
               wattron(load_win, A_REVERSE);
            }

            mvwprintw(load_win, 3 + i, 2, "%s", entries[idx].name);

            if (entries[idx].is_dir)
            {
               wprintw(load_win, "/");
            }

            if (idx == selected)
            {
               wattroff(load_win, A_REVERSE);
            }
         }

         mvwprintw(load_win, height - 2, 2, "Up/Down=Navigate | Enter=Open/Load | q=Cancel");

         box(load_win, 0, 0);
         wrefresh(load_win);

         int ch = getch();

         switch (ch)
         {
            case KEY_UP:
               if (selected > 0)
               {
                  selected--;
               }
               break;

            case KEY_DOWN:
               if (selected < entry_count - 1)
               {
                  selected++;
               }
               break;

            case 10: // Enter
            {
               if (entries[selected].is_dir)
               {
                  if (strcmp(entries[selected].name, "..") == 0)
                  {
                     char* last_slash = strrchr(current_dir, '/');
                     if (last_slash != NULL && last_slash != current_dir)
                     {
                        *last_slash = '\0';
                     }
                     else if (last_slash == current_dir)
                     {
                        strcpy(current_dir, "/");
                     }
                  }
                  else
                  {
                     if (strcmp(current_dir, "/") != 0)
                     {
                        strcat(current_dir, "/");
                     }
                     strcat(current_dir, entries[selected].name);
                  }
                  navigate_to_dir = true;
               }
               else
               {
                  load_file = true;
               }
               goto break_inner_loop;
            }
            break;

            case 'q':
            case 'Q':
               delwin(load_win);
               return;
         }
      }

break_inner_loop:
      if (navigate_to_dir)
      {
         continue;
      }
      else if (load_file)
      {
         char new_path[MAX_PATH * 2];
         pgmoneta_snprintf(new_path, sizeof(new_path), "%s/%s", current_dir, entries[selected].name);

         delwin(load_win);
         clear();
         refresh();

         if (state->wf != NULL)
         {
            pgmoneta_destroy_walfile(state->wf);
            state->wf = NULL;
         }

         if (state->records != NULL)
         {
            free(state->records);
            state->records = NULL;
         }

         if (state->wal_filename != NULL)
         {
            free(state->wal_filename);
            state->wal_filename = NULL;
         }

         state->record_count = 0;
         state->record_capacity = 1000;
         state->current_row = 0;
         state->scroll_offset = 0;

         state->records = calloc(state->record_capacity, sizeof(struct wal_record_ui));
         if (state->records == NULL)
         {
            return;
         }

         state->wal_filename = strdup(new_path);

         if (wal_interactive_load_records(state, new_path) != 0)
         {
            clear();
            mvprintw(0, 0, "Error: Failed to load: %s", new_path);
            refresh();
            getch();
         }

         return;
      }
   }
}

static void
show_previous_wal_file(struct ui_state* state)
{
   char* dir_path = strdup(state->wal_filename);
   char* dir = dirname(dir_path);

   struct deque* files = NULL;
   struct deque_iterator* iter = NULL;

   if (pgmoneta_get_wal_files(dir, &files) != 0 || files == NULL || pgmoneta_deque_empty(files))
   {
      free(dir_path);
      return;
   }

   pgmoneta_deque_sort(files);

   int num_files = pgmoneta_deque_size(files);
   char** file_list = malloc(num_files * sizeof(char*));
   if (file_list == NULL)
   {
      pgmoneta_deque_destroy(files);
      free(dir_path);
      return;
   }

   int index = 0;

   pgmoneta_deque_iterator_create(files, &iter);
   while (pgmoneta_deque_iterator_next(iter))
   {
      struct value* val = iter->value;
      if (val != NULL && val->type == ValueString)
      {
         char* full_path = (char*)val->data;
         char* temp_path = strdup(full_path);
         char* filename = basename(temp_path);
         file_list[index++] = strdup(filename);
         free(temp_path);
      }
   }
   pgmoneta_deque_iterator_destroy(iter);

   num_files = index;

   char* temp_filename = strdup(state->wal_filename);
   char* current_basename = basename(temp_filename);

   char* previous_file = NULL;
   int current_index = -1;

   for (int i = 0; i < num_files; i++)
   {
      if (strcmp(file_list[i], current_basename) == 0)
      {
         current_index = i;
         break;
      }
   }

   free(temp_filename);

   if (current_index > 0)
   {
      previous_file = file_list[current_index - 1];
   }
   else
   {
      for (int i = 0; i < num_files; i++)
      {
         free(file_list[i]);
      }
      free(file_list);
      pgmoneta_deque_destroy(files);
      free(dir_path);
      return;
   }

   char new_path[MAX_PATH * 2];
   pgmoneta_snprintf(new_path, sizeof(new_path), "%s/%s", dir, previous_file);

   for (int i = 0; i < num_files; i++)
   {
      free(file_list[i]);
   }
   free(file_list);
   pgmoneta_deque_destroy(files);
   free(dir_path);

   if (state->wf != NULL)
   {
      pgmoneta_destroy_walfile(state->wf);
      state->wf = NULL;
   }

   if (state->records != NULL)
   {
      free(state->records);
      state->records = NULL;
   }

   if (state->wal_filename != NULL)
   {
      free(state->wal_filename);
      state->wal_filename = NULL;
   }

   state->record_count = 0;
   state->record_capacity = 1000;
   state->current_row = 0;
   state->scroll_offset = 0;

   state->records = calloc(state->record_capacity, sizeof(struct wal_record_ui));
   if (state->records == NULL)
   {
      return;
   }

   state->wal_filename = strdup(new_path);

   if (wal_interactive_load_records(state, new_path) != 0)
   {
      clear();
      mvprintw(0, 0, "Error: Failed to load: %s", new_path);
      refresh();
      getch();
      return;
   }

   if (state->record_count > 0)
   {
      state->current_row = state->record_count - 1;

      if (state->record_count > 20)
      {
         state->scroll_offset = state->record_count - 20;
      }
      else
      {
         state->scroll_offset = 0;
      }
   }
}

static void
show_next_wal_file(struct ui_state* state)
{
   char* dir_path = strdup(state->wal_filename);
   char* dir = dirname(dir_path);

   struct deque* files = NULL;
   struct deque_iterator* iter = NULL;

   if (pgmoneta_get_wal_files(dir, &files) != 0 || files == NULL || pgmoneta_deque_empty(files))
   {
      free(dir_path);
      return;
   }

   pgmoneta_deque_sort(files);

   int num_files = pgmoneta_deque_size(files);
   char** file_list = malloc(num_files * sizeof(char*));
   if (file_list == NULL)
   {
      pgmoneta_deque_destroy(files);
      free(dir_path);
      return;
   }

   int index = 0;

   pgmoneta_deque_iterator_create(files, &iter);
   while (pgmoneta_deque_iterator_next(iter))
   {
      struct value* val = iter->value;
      if (val != NULL && val->type == ValueString)
      {
         char* full_path = (char*)val->data;
         char* temp_path = strdup(full_path);
         char* filename = basename(temp_path);
         file_list[index++] = strdup(filename);
         free(temp_path);
      }
   }
   pgmoneta_deque_iterator_destroy(iter);

   num_files = index;

   char* temp_filename = strdup(state->wal_filename);
   char* current_basename = basename(temp_filename);
   char* next_file = NULL;
   int current_index = -1;

   for (int i = 0; i < num_files; i++)
   {
      if (strcmp(file_list[i], current_basename) == 0)
      {
         current_index = i;
         break;
      }
   }

   free(temp_filename);

   if (current_index >= 0 && current_index < num_files - 1)
   {
      next_file = file_list[current_index + 1];
   }
   else
   {
      for (int i = 0; i < num_files; i++)
      {
         free(file_list[i]);
      }
      free(file_list);
      pgmoneta_deque_destroy(files);
      free(dir_path);
      return;
   }

   char new_path[MAX_PATH * 2];
   pgmoneta_snprintf(new_path, sizeof(new_path), "%s/%s", dir, next_file);

   for (int i = 0; i < num_files; i++)
   {
      free(file_list[i]);
   }
   free(file_list);
   pgmoneta_deque_destroy(files);
   free(dir_path);

   if (state->wf != NULL)
   {
      pgmoneta_destroy_walfile(state->wf);
      state->wf = NULL;
   }

   if (state->records != NULL)
   {
      free(state->records);
      state->records = NULL;
   }

   if (state->wal_filename != NULL)
   {
      free(state->wal_filename);
      state->wal_filename = NULL;
   }

   state->record_count = 0;
   state->record_capacity = 1000;
   state->current_row = 0;
   state->scroll_offset = 0;

   state->records = calloc(state->record_capacity, sizeof(struct wal_record_ui));
   if (state->records == NULL)
   {
      return;
   }

   state->wal_filename = strdup(new_path);

   if (wal_interactive_load_records(state, new_path) != 0)
   {
      clear();
      mvprintw(0, 0, "Error: Failed to load: %s", new_path);
      refresh();
      getch();
      return;
   }

   state->current_row = 0;
   state->scroll_offset = 0;
}

static void
wal_interactive_run(struct ui_state* state)
{
   int ch;
   int height, width_unused;
   getmaxyx(state->main_win, height, width_unused);
   (void)width_unused;

   draw_header(state);
   draw_main_content(state);
   draw_status(state);
   draw_footer(state);
   refresh();

   while (1)
   {
      ch = getch();

      switch (ch)
      {
         case KEY_UP:
            if (state->current_row > 0)
            {
               state->current_row--;
               if (state->current_row < state->scroll_offset)
               {
                  state->scroll_offset--;
               }
            }
            else
            {
               size_t old_record_count = state->record_count;
               show_previous_wal_file(state);

               if (state->record_count != old_record_count && state->record_count > 0)
               {
                  state->current_row = state->record_count - 1;

                  if (state->record_count > (size_t)(height - 4))
                  {
                     state->scroll_offset = state->record_count - (height - 4);
                  }
                  else
                  {
                     state->scroll_offset = 0;
                  }
               }
            }
            break;

         case KEY_DOWN:
            if (state->current_row < state->record_count - 1)
            {
               state->current_row++;
               if (state->current_row >= state->scroll_offset + (size_t)(height - 4))
               {
                  state->scroll_offset++;
               }
            }
            else
            {
               size_t old_record_count = state->record_count;
               show_next_wal_file(state);

               if (state->record_count != old_record_count && state->record_count > 0)
               {
                  state->current_row = 0;
                  state->scroll_offset = 0;
               }
            }
            break;

         case KEY_PPAGE:
            if (state->current_row >= (size_t)(height - 4))
            {
               state->current_row -= (height - 4);
               state->scroll_offset = (state->scroll_offset >= (size_t)(height - 4)) ? state->scroll_offset - (height - 4) : 0;
            }
            else
            {
               state->current_row = 0;
               state->scroll_offset = 0;
            }
            break;

         case KEY_NPAGE:
            state->current_row += (height - 4);
            if (state->current_row >= state->record_count)
            {
               state->current_row = state->record_count - 1;
            }
            state->scroll_offset = state->current_row;
            break;

         case KEY_HOME:
            state->current_row = 0;
            state->scroll_offset = 0;
            break;

         case KEY_END:
            state->current_row = state->record_count - 1;
            state->scroll_offset = (state->record_count > (size_t)(height - 4)) ? state->record_count - (height - 4) : 0;
            break;

         case 't':
         case 'T':
            state->mode = DISPLAY_MODE_TEXT;
            break;

         case 'b':
         case 'B':
            state->mode = DISPLAY_MODE_BINARY;
            break;

         case 10: // Enter key
            show_detail_view(state);
            break;

         case 's':
         case 'S':
            handle_search_input(state);
            break;

         case 'v':
         case 'V':
            wal_interactive_verify(state);
            break;

         case 'l':
         case 'L':
            show_wal_file_selector(state);
            break;

         case '?':
            show_help();
            break;

         case 'q':
         case 'Q':
            return;
      }

      draw_header(state);
      draw_main_content(state);
      draw_status(state);
      draw_footer(state);
      refresh();
   }
}

static void
wal_interactive_cleanup(struct ui_state* state)
{
   if (state->wf)
   {
      pgmoneta_destroy_walfile(state->wf);
   }

   if (state->search_results)
   {
      free(state->search_results);
   }

   if (state->records)
   {
      free(state->records);
   }

   if (state->wal_filename)
   {
      free(state->wal_filename);
   }

   if (state->main_win)
   {
      delwin(state->main_win);
   }
   if (state->header_win)
   {
      delwin(state->header_win);
   }
   if (state->footer_win)
   {
      delwin(state->footer_win);
   }
   if (state->status_win)
   {
      delwin(state->status_win);
   }

   wal_interactive_endwin();
   wal_interactive_restore_handlers();
}

/**
 * Center-align a string within a given width
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param src Source string to center
 * @param width Total width for alignment
 */
static void
center_align(char* dest, size_t dest_size, const char* src, int width)
{
   int src_len = strlen(src);
   int padding = (width - src_len) / 2;
   int written = 0;

   if (width >= (int)dest_size)
   {
      width = dest_size - 1;
   }

   for (int i = 0; i < padding && written < width; i++, written++)
   {
      dest[written] = ' ';
   }

   for (int i = 0; i < src_len && written < width; i++, written++)
   {
      dest[written] = src[i];
   }

   while (written < width)
   {
      dest[written++] = ' ';
   }

   dest[written] = '\0';
}

/**
 * Right-align a string within a given width
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param src Source string to right-align
 * @param width Total width for alignment
 */
static void
right_align(char* dest, size_t dest_size, const char* src, int width)
{
   int src_len = strlen(src);
   int padding = width - src_len;
   int written = 0;

   if (width >= (int)dest_size)
   {
      width = dest_size - 1;
   }

   if (padding < 0)
   {
      padding = 0;
   }

   for (int i = 0; i < padding && written < width; i++, written++)
   {
      dest[written] = ' ';
   }

   for (int i = 0; i < src_len && written < width; i++, written++)
   {
      dest[written] = src[i];
   }

   dest[written] = '\0';
}

/**
 * Print WAL statistics summary
 * @param out Output file stream
 * @param type Output format type (ValueJSON or ValueString)
 */
static void
print_wal_statistics(FILE* out, enum value_type type)
{
   uint64_t total_count = 0;
   uint64_t total_record_size = 0;
   uint64_t total_fpi_size = 0;
   uint64_t total_combined_size = 0;
   int max_rmgr_name_length = 20;
   bool first = true;

   for (int i = 0; i < RM_MAX_ID + 1; i++)
   {
      if (rmgr_stats_table[i].count > 0)
      {
         total_count += rmgr_stats_table[i].count;
         total_record_size += rmgr_stats_table[i].record_size;
         total_fpi_size += rmgr_stats_table[i].fpi_size;
         total_combined_size += rmgr_stats_table[i].combined_size;
         max_rmgr_name_length = MAX(max_rmgr_name_length, (int)strlen(rmgr_stats_table[i].name));
      }
   }

   if (type == ValueJSON)
   {
      fprintf(out, "{\"wal_stats\": [\n");
      for (int i = 0; i < RM_MAX_ID + 1; i++)
      {
         if (rmgr_stats_table[i].count == 0)
         {
            continue;
         }

         double count_pct = total_count > 0 ? (100.0 * rmgr_stats_table[i].count) / total_count : 0.0;
         double rec_size_pct = total_record_size > 0 ? (100.0 * rmgr_stats_table[i].record_size) / total_record_size : 0.0;
         double fpi_size_pct = total_fpi_size > 0 ? (100.0 * rmgr_stats_table[i].fpi_size) / total_fpi_size : 0.0;
         double combined_size_pct = total_combined_size > 0 ? (100.0 * rmgr_stats_table[i].combined_size) / total_combined_size : 0.0;

         if (!first)
         {
            fprintf(out, ",\n");
         }
         first = false;

         fprintf(out, "  {\n");
         fprintf(out, "    \"resource_manager\": \"%s\",\n", rmgr_stats_table[i].name);
         fprintf(out, "    \"count\": %" PRIu64 ",\n", rmgr_stats_table[i].count);
         fprintf(out, "    \"count_percentage\": %.2f,\n", count_pct);
         fprintf(out, "    \"record_size\": %" PRIu64 ",\n", rmgr_stats_table[i].record_size);
         fprintf(out, "    \"record_size_percentage\": %.2f,\n", rec_size_pct);
         fprintf(out, "    \"fpi_size\": %" PRIu64 ",\n", rmgr_stats_table[i].fpi_size);
         fprintf(out, "    \"fpi_size_percentage\": %.2f,\n", fpi_size_pct);
         fprintf(out, "    \"combined_size\": %" PRIu64 ",\n", rmgr_stats_table[i].combined_size);
         fprintf(out, "    \"combined_size_percentage\": %.2f\n", combined_size_pct);
         fprintf(out, "  }");
      }
      fprintf(out, "\n]}\n");
   }
   else
   {
      char count_header[COL_WIDTH_COUNT + 1], count_pct_header[COL_WIDTH_COUNT_PCT + 1];
      char rec_size_header[COL_WIDTH_RECORD_SIZE + 1], rec_pct_header[COL_WIDTH_RECORD_PCT + 1];
      char fpi_size_header[COL_WIDTH_FPI_SIZE + 1], fpi_pct_header[COL_WIDTH_FPI_PCT + 1];
      char comb_size_header[COL_WIDTH_COMBINED_SIZE + 1], comb_pct_header[COL_WIDTH_COMBINED_PCT + 1];

      center_align(count_header, sizeof(count_header), "Count", COL_WIDTH_COUNT);
      center_align(count_pct_header, sizeof(count_pct_header), "Count %", COL_WIDTH_COUNT_PCT);
      center_align(rec_size_header, sizeof(rec_size_header), "Record Size", COL_WIDTH_RECORD_SIZE);
      center_align(rec_pct_header, sizeof(rec_pct_header), "Record %", COL_WIDTH_RECORD_PCT);
      center_align(fpi_size_header, sizeof(fpi_size_header), "FPI Size", COL_WIDTH_FPI_SIZE);
      center_align(fpi_pct_header, sizeof(fpi_pct_header), "FPI %", COL_WIDTH_FPI_PCT);
      center_align(comb_size_header, sizeof(comb_size_header), "Combined Size", COL_WIDTH_COMBINED_SIZE);
      center_align(comb_pct_header, sizeof(comb_pct_header), "Combined %", COL_WIDTH_COMBINED_PCT);

      fprintf(out, "%-*s | %s | %s | %s | %s | %s | %s | %s | %s |\n",
              max_rmgr_name_length, "Resource Manager",
              count_header, count_pct_header, rec_size_header, rec_pct_header,
              fpi_size_header, fpi_pct_header, comb_size_header, comb_pct_header);

      int total_width = max_rmgr_name_length + COL_WIDTH_COUNT + COL_WIDTH_COUNT_PCT +
                        COL_WIDTH_RECORD_SIZE + COL_WIDTH_RECORD_PCT + COL_WIDTH_FPI_SIZE +
                        COL_WIDTH_FPI_PCT + COL_WIDTH_COMBINED_SIZE + COL_WIDTH_COMBINED_PCT + 24;
      for (int i = 0; i < total_width; i++)
      {
         fprintf(out, "-");
      }
      fprintf(out, "\n");

      for (int i = 0; i < RM_MAX_ID + 1; i++)
      {
         if (rmgr_stats_table[i].count == 0)
         {
            continue;
         }

         double count_pct = total_count > 0 ? (100.0 * rmgr_stats_table[i].count) / total_count : 0.0;
         double rec_size_pct = total_record_size > 0 ? (100.0 * rmgr_stats_table[i].record_size) / total_record_size : 0.0;
         double fpi_size_pct = total_fpi_size > 0 ? (100.0 * rmgr_stats_table[i].fpi_size) / total_fpi_size : 0.0;
         double combined_size_pct = total_combined_size > 0 ? (100.0 * rmgr_stats_table[i].combined_size) / total_combined_size : 0.0;

         char count_str[COL_WIDTH_COUNT + 1], count_pct_str[COL_WIDTH_COUNT_PCT + 1];
         char rec_size_str[COL_WIDTH_RECORD_SIZE + 1], rec_pct_str[COL_WIDTH_RECORD_PCT + 1];
         char fpi_size_str[COL_WIDTH_FPI_SIZE + 1], fpi_pct_str[COL_WIDTH_FPI_PCT + 1];
         char comb_size_str[COL_WIDTH_COMBINED_SIZE + 1], comb_pct_str[COL_WIDTH_COMBINED_PCT + 1];
         char temp_str[64];

         snprintf(temp_str, sizeof(temp_str), "%" PRIu64, rmgr_stats_table[i].count);
         right_align(count_str, sizeof(count_str), temp_str, COL_WIDTH_COUNT);

         snprintf(temp_str, sizeof(temp_str), "%.2f%%", count_pct);
         right_align(count_pct_str, sizeof(count_pct_str), temp_str, COL_WIDTH_COUNT_PCT);

         snprintf(temp_str, sizeof(temp_str), "%" PRIu64, rmgr_stats_table[i].record_size);
         right_align(rec_size_str, sizeof(rec_size_str), temp_str, COL_WIDTH_RECORD_SIZE);

         snprintf(temp_str, sizeof(temp_str), "%.2f%%", rec_size_pct);
         right_align(rec_pct_str, sizeof(rec_pct_str), temp_str, COL_WIDTH_RECORD_PCT);

         snprintf(temp_str, sizeof(temp_str), "%" PRIu64, rmgr_stats_table[i].fpi_size);
         right_align(fpi_size_str, sizeof(fpi_size_str), temp_str, COL_WIDTH_FPI_SIZE);

         snprintf(temp_str, sizeof(temp_str), "%.2f%%", fpi_size_pct);
         right_align(fpi_pct_str, sizeof(fpi_pct_str), temp_str, COL_WIDTH_FPI_PCT);

         snprintf(temp_str, sizeof(temp_str), "%" PRIu64, rmgr_stats_table[i].combined_size);
         right_align(comb_size_str, sizeof(comb_size_str), temp_str, COL_WIDTH_COMBINED_SIZE);

         snprintf(temp_str, sizeof(temp_str), "%.2f%%", combined_size_pct);
         right_align(comb_pct_str, sizeof(comb_pct_str), temp_str, COL_WIDTH_COMBINED_PCT);

         fprintf(out, "%-*s | %s | %s | %s | %s | %s | %s | %s | %s |\n",
                 max_rmgr_name_length, rmgr_stats_table[i].name,
                 count_str, count_pct_str, rec_size_str, rec_pct_str,
                 fpi_size_str, fpi_pct_str, comb_size_str, comb_pct_str);
      }

      for (int i = 0; i < total_width; i++)
      {
         fprintf(out, "-");
      }
      fprintf(out, "\n");

      char count_str[COL_WIDTH_COUNT + 1], count_pct_str[COL_WIDTH_COUNT_PCT + 1];
      char rec_size_str[COL_WIDTH_RECORD_SIZE + 1], rec_pct_str[COL_WIDTH_RECORD_PCT + 1];
      char fpi_size_str[COL_WIDTH_FPI_SIZE + 1], fpi_pct_str[COL_WIDTH_FPI_PCT + 1];
      char comb_size_str[COL_WIDTH_COMBINED_SIZE + 1], comb_pct_str[COL_WIDTH_COMBINED_PCT + 1];
      char temp_str[64];

      snprintf(temp_str, sizeof(temp_str), "%" PRIu64, total_count);
      right_align(count_str, sizeof(count_str), temp_str, COL_WIDTH_COUNT);

      snprintf(temp_str, sizeof(temp_str), "%.2f%%", 100.0);
      right_align(count_pct_str, sizeof(count_pct_str), temp_str, COL_WIDTH_COUNT_PCT);

      snprintf(temp_str, sizeof(temp_str), "%" PRIu64, total_record_size);
      right_align(rec_size_str, sizeof(rec_size_str), temp_str, COL_WIDTH_RECORD_SIZE);

      snprintf(temp_str, sizeof(temp_str), "%.2f%%", 100.0);
      right_align(rec_pct_str, sizeof(rec_pct_str), temp_str, COL_WIDTH_RECORD_PCT);

      snprintf(temp_str, sizeof(temp_str), "%" PRIu64, total_fpi_size);
      right_align(fpi_size_str, sizeof(fpi_size_str), temp_str, COL_WIDTH_FPI_SIZE);

      snprintf(temp_str, sizeof(temp_str), "%.2f%%", 100.0);
      right_align(fpi_pct_str, sizeof(fpi_pct_str), temp_str, COL_WIDTH_FPI_PCT);

      snprintf(temp_str, sizeof(temp_str), "%" PRIu64, total_combined_size);
      right_align(comb_size_str, sizeof(comb_size_str), temp_str, COL_WIDTH_COMBINED_SIZE);

      snprintf(temp_str, sizeof(temp_str), "%.2f%%", 100.0);
      right_align(comb_pct_str, sizeof(comb_pct_str), temp_str, COL_WIDTH_COMBINED_PCT);

      fprintf(out, "%-*s | %s | %s | %s | %s | %s | %s | %s | %s |\n",
              max_rmgr_name_length, "Total",
              count_str, count_pct_str, rec_size_str, rec_pct_str,
              fpi_size_str, fpi_pct_str, comb_size_str, comb_pct_str);
   }

   for (int i = 0; i < RM_MAX_ID + 1; i++)
   {
      rmgr_stats_table[i].count = 0;
      rmgr_stats_table[i].record_size = 0;
      rmgr_stats_table[i].fpi_size = 0;
      rmgr_stats_table[i].combined_size = 0;
   }
}

static void
version(void)
{
   printf("pgmoneta-walinfo %s\n", VERSION);
   exit(1);
}

static void
usage(void)
{
   printf("pgmoneta-walinfo %s\n", VERSION);
   printf("  Command line utility to read and display Write-Ahead Log (WAL) files\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgmoneta-walinfo <file|directory|tar_archive>\n");
   printf("\n");
   printf("Options:\n");
   printf("  -I,  --interactive Interactive mode with ncurses UI\n");
   printf("  -c,  --config      Set the path to the pgmoneta_walinfo.conf file\n");
   printf("  -u,  --users       Set the path to the pgmoneta_users.conf file \n");
   printf("  -RT, --tablespaces Filter on tablspaces\n");
   printf("  -RD, --databases   Filter on databases\n");
   printf("  -RT, --relations   Filter on relations\n");
   printf("  -R,  --filter      Combination of -RT, -RD, -RR\n");
   printf("  -o,  --output      Output file\n");
   printf("  -F,  --format      Output format (raw, json)\n");
   printf("  -L,  --logfile     Set the log file\n");
   printf("  -q,  --quiet       No output only result\n");
   printf("       --color       Use colors (on, off)\n");
   printf("  -r,  --rmgr        Filter on a resource manager\n");
   printf("  -s,  --start       Filter on a start LSN\n");
   printf("  -e,  --end         Filter on an end LSN\n");
   printf("  -x,  --xid         Filter on an XID\n");
   printf("  -l,  --limit       Limit number of outputs\n");
   printf("  -v,  --verbose     Output result\n");
   printf("  -S,  --summary     Show detailed WAL statistics including counts, sizes, and percentages by resource manager\n");
   printf("  -V,  --version     Display version information\n");
   printf("  -m,  --mapping     Provide mappings file for OID translation\n");
   printf("  -t,  --translate   Translate OIDs to object names in XLOG records\n");
   printf("  -?,  --help        Display help\n");
   printf("\n");
   printf("pgmoneta: %s\n", PGMONETA_HOMEPAGE);
   printf("Report bugs: %s\n", PGMONETA_ISSUES);
}

static bool
is_tar_archive_input(char* path)
{
   uint32_t file_type = 0;

   if (path == NULL)
   {
      return false;
   }

   file_type = pgmoneta_get_file_type(path);
   if (file_type & PGMONETA_FILE_TYPE_TAR)
   {
      return true;
   }

   /* Fallback for layered extension handling on input arguments */
   if (pgmoneta_ends_with(path, ".tar") ||
       pgmoneta_ends_with(path, ".tar.gz") ||
       pgmoneta_ends_with(path, ".tgz") ||
       pgmoneta_ends_with(path, ".tar.zstd") ||
       pgmoneta_ends_with(path, ".tar.lz4") ||
       pgmoneta_ends_with(path, ".tar.bz2") ||
       pgmoneta_ends_with(path, ".tar.aes") ||
       pgmoneta_ends_with(path, ".tar.gz.aes") ||
       pgmoneta_ends_with(path, ".tgz.aes") ||
       pgmoneta_ends_with(path, ".tar.zstd.aes") ||
       pgmoneta_ends_with(path, ".tar.lz4.aes") ||
       pgmoneta_ends_with(path, ".tar.bz2.aes"))
   {
      return true;
   }

   return false;
}

int
main(int argc, char** argv)
{
   int loaded = 1;
   bool interactive = false;
   char* configuration_path = NULL;
   char* users_path = NULL;
   char* output = NULL;
   char* format = NULL;
   char* logfile = NULL;
   bool quiet = false;
   bool color = true;
   struct deque* rms = NULL;
   uint64_t start_lsn = 0;
   uint64_t end_lsn = 0;
   uint64_t start_lsn_high = 0;
   uint64_t start_lsn_low = 0;
   uint64_t end_lsn_high = 0;
   uint64_t end_lsn_low = 0;
   struct deque* xids = NULL;
   uint32_t limit = 0;
   bool verbose = false;
   bool summary = false;
   enum value_type type = ValueString;
   size_t size;
   struct walinfo_configuration* config = NULL;
   bool enable_mapping = false;
   char* mappings_path = NULL;
   int ret;
   char* tablespaces = NULL;
   char* databases = NULL;
   char* relations = NULL;
   char* filters = NULL;
   bool filtering_enabled = false;
   char** included_objects = NULL;
   char** databases_list = NULL;
   char** tablespaces_list = NULL;
   char** relations_list = NULL;
   int databases_count = 0;
   int tablespaces_count = 0;
   int relations_count = 0;
   char** temp_list = NULL;
   int cnt = 0;
   int optind = 0;
   char* filepath;
   int num_results = 0;
   int num_options = 0;
   FILE* out = NULL;

   cli_option options[] = {
      {"I", "interactive", false},
      {"c", "config", true},
      {"o", "output", true},
      {"F", "format", true},
      {"u", "users", true},
      {"RT", "tablespaces", true},
      {"RD", "databases", true},
      {"RR", "relations", true},
      {"R", "filter", true},
      {"m", "mapping", true},
      {"t", "translate", false},
      {"L", "logfile", true},
      {"q", "quiet", false},
      {"", "color", true},
      {"r", "rmgr", true},
      {"s", "start", true},
      {"e", "end", true},
      {"x", "xid", true},
      {"l", "limit", true},
      {"v", "verbose", false},
      {"S", "summary", false},
      {"V", "version", false},
      {"?", "help", false},
   };

   num_options = sizeof(options) / sizeof(options[0]);
   cli_result results[num_options];

   if (argc < 2)
   {
      usage();
      goto error;
   }

   num_results = cmd_parse(argc, argv, options, num_options, results, num_options, true, &filepath, &optind);

   if (num_results < 0)
   {
      errx(1, "Error parsing command line\n");
      return 1;
   }

   for (int i = 0; i < num_results; i++)
   {
      char* optname = results[i].option_name;
      char* optarg = results[i].argument;

      if (optname == NULL)
      {
         break;
      }
      else if (!strcmp(optname, "c") || !strcmp(optname, "config"))
      {
         configuration_path = optarg;
      }
      else if (!strcmp(optname, "I") || !strcmp(optname, "interactive"))
      {
         interactive = true;
      }
      else if (!strcmp(optname, "o") || !strcmp(optname, "output"))
      {
         output = optarg;
      }
      else if (!strcmp(optname, "F") || !strcmp(optname, "format"))
      {
         format = optarg;

         if (!strcmp(format, "json"))
         {
            type = ValueJSON;
         }
         else
         {
            type = ValueString;
         }
      }
      else if (!strcmp(optname, "L") || !strcmp(optname, "logfile"))
      {
         logfile = optarg;
      }
      else if (!strcmp(optname, "q") || !strcmp(optname, "quiet"))
      {
         quiet = true;
      }
      else if (!strcmp(optname, "color"))
      {
         if (!strcmp(optarg, "off"))
         {
            color = false;
         }
         else
         {
            color = true;
         }
      }
      else if (!strcmp(optname, "r") || !strcmp(optname, "rmgr"))
      {
         if (rms == NULL)
         {
            if (pgmoneta_deque_create(false, &rms))
            {
               exit(1);
            }
         }

         pgmoneta_deque_add(rms, NULL, (uintptr_t)optarg, ValueString);
      }
      else if (!strcmp(optname, "s") || !strcmp(optname, "start"))
      {
         if (strchr(optarg, '/'))
         {
            // Assuming optarg is a string like "16/B374D848"
            if (sscanf(optarg, "%" SCNx64 "/%" SCNx64, &start_lsn_high, &start_lsn_low) == 2)
            {
               start_lsn = (start_lsn_high << 32) + start_lsn_low;
            }
            else
            {
               fprintf(stderr, "Invalid start LSN format\n");
               exit(1);
            }
         }
         else
         {
            start_lsn = strtoull(optarg, NULL, 10); // Assuming optarg is a decimal number
         }
      }
      else if (!strcmp(optname, "e") || !strcmp(optname, "end"))
      {
         if (strchr(optarg, '/'))
         {
            // Assuming optarg is a string like "16/B374D848"
            if (sscanf(optarg, "%" SCNx64 "/%" SCNx64, &end_lsn_high, &end_lsn_low) == 2)
            {
               end_lsn = (end_lsn_high << 32) + end_lsn_low;
            }
            else
            {
               fprintf(stderr, "Invalid end LSN format\n");
               exit(1);
            }
         }
         else
         {
            end_lsn = strtoull(optarg, NULL, 10); // Assuming optarg is a decimal number
         }
      }
      else if (!strcmp(optname, "x") || !strcmp(optname, "xid"))
      {
         if (xids == NULL)
         {
            if (pgmoneta_deque_create(false, &xids))
            {
               exit(1);
            }
         }

         pgmoneta_deque_add(xids, NULL, (uintptr_t)pgmoneta_atoi(optarg), ValueUInt32);
      }
      else if (!strcmp(optname, "l") || !strcmp(optname, "limit"))
      {
         limit = pgmoneta_atoi(optarg);
      }
      else if (!strcmp(optname, "m") || !strcmp(optname, "mapping"))
      {
         enable_mapping = true;
         mappings_path = optarg;
      }
      else if (!strcmp(optname, "t") || !strcmp(optname, "translate"))
      {
         enable_mapping = true;
      }
      else if (!strcmp(optname, "RT") || !strcmp(optname, "tablespaces"))
      {
         tablespaces = optarg;
         filtering_enabled = true;
      }
      else if (!strcmp(optname, "RD") || !strcmp(optname, "databases"))
      {
         databases = optarg;
         filtering_enabled = true;
      }
      else if (!strcmp(optname, "RR") || !strcmp(optname, "relations"))
      {
         relations = optarg;
         filtering_enabled = true;
      }
      else if (!strcmp(optname, "R") || !strcmp(optname, "filter"))
      {
         filters = optarg;
         filtering_enabled = true;
      }
      else if (!strcmp(optname, "u") || !strcmp(optname, "users"))
      {
         users_path = optarg;
      }
      else if (!strcmp(optname, "v") || !strcmp(optname, "verbose"))
      {
         verbose = true;
      }
      else if (!strcmp(optname, "S") || !strcmp(optname, "summary"))
      {
         summary = true;
      }
      else if (!strcmp(optname, "V") || !strcmp(optname, "version"))
      {
         version();
         exit(0);
      }
      else if (!strcmp(optname, "?") || !strcmp(optname, "help"))
      {
         usage();
         exit(0);
      }
   }

   size = sizeof(struct walinfo_configuration);
   if (pgmoneta_create_shared_memory(size, HUGEPAGE_OFF, &shmem))
   {
      warnx("Error creating shared memory");
      goto error;
   }

   pgmoneta_init_walinfo_configuration(shmem);
   config = (struct walinfo_configuration*)shmem;

   if (configuration_path != NULL)
   {
      if (!pgmoneta_exists(configuration_path))
      {
         errx(1, "Configuration file not found: %s", configuration_path);
      }

      if (!pgmoneta_is_file(configuration_path))
      {
         errx(1, "Configuration path is not a file: %s", configuration_path);
      }

      if (access(configuration_path, R_OK) != 0)
      {
         errx(1, "Can't read configuration file: %s", configuration_path);
      }

      int cfg_ret = pgmoneta_validate_config_file(configuration_path);
      if (cfg_ret == 4)
      {
         errx(1, "Configuration file contains binary data: %s", configuration_path);
      }
      else if (cfg_ret != 0)
      {
         goto error;
      }

      loaded = pgmoneta_read_walinfo_configuration(shmem, configuration_path);

      if (loaded)
      {
         errx(1, "Failed to read configuration file: %s", configuration_path);
      }
   }

   if (loaded && pgmoneta_exists(PGMONETA_WALINFO_DEFAULT_CONFIG_FILE_PATH))
   {
      loaded = pgmoneta_read_walinfo_configuration(shmem, PGMONETA_WALINFO_DEFAULT_CONFIG_FILE_PATH);
   }

   if (loaded)
   {
      config->common.log_type = PGMONETA_LOGGING_TYPE_CONSOLE;
   }
   else
   {
      if (logfile)
      {
         config->common.log_type = PGMONETA_LOGGING_TYPE_FILE;
         memset(&config->common.log_path[0], 0, MISC_LENGTH);
         memcpy(&config->common.log_path[0], logfile, MIN((size_t)MISC_LENGTH - 1, strlen(logfile)));
      }
   }

   if (pgmoneta_validate_walinfo_configuration())
   {
      goto error;
   }

   if (pgmoneta_start_logging())
   {
      exit(1);
   }

   if (users_path != NULL)
   {
      ret = pgmoneta_read_users_configuration(shmem, users_path);
      if (ret == 1)
      {
         warnx("pgmoneta: USERS configuration not found: %s", users_path);
         goto error;
      }
      else if (ret == 2)
      {
         warnx("pgmoneta: Invalid master key file");
         goto error;
      }
      else if (ret == 3)
      {
         warnx("pgmoneta: USERS: Too many users defined %d (max %d)", config->common.number_of_users, NUMBER_OF_USERS);
         goto error;
      }
      memcpy(&config->common.users_path[0], users_path, MIN(strlen(users_path), (size_t)MAX_PATH - 1));
   }
   else
   {
      users_path = PGMONETA_DEFAULT_USERS_FILE_PATH;
      ret = pgmoneta_read_users_configuration(shmem, users_path);
      if (ret == 0)
      {
         memcpy(&config->common.users_path[0], users_path, MIN(strlen(users_path), (size_t)MAX_PATH - 1));
      }
   }

   if (enable_mapping)
   {
      if (mappings_path != NULL)
      {
         if (pgmoneta_read_mappings_from_json(mappings_path) != 0)
         {
            pgmoneta_log_error("Failed to read mappings file");
            goto error;
         }
      }
      else
      {
         if (config->common.number_of_servers == 0)
         {
            pgmoneta_log_error("No servers defined, user should provide exactly one server in the configuration file");
            goto error;
         }

         if (pgmoneta_read_mappings_from_server(0) != 0)
         {
            pgmoneta_log_error("Failed to read mappings from server");
            goto error;
         }
      }
   }

   if (filtering_enabled)
   {
      if (enable_mapping)
      {
         if (filters != NULL)
         {
            if (pgmoneta_split(filters, &temp_list, &cnt, '/'))
            {
               pgmoneta_log_error("Failed to parse filters");
               goto error;
            }
            else
            {
               if (pgmoneta_split(temp_list[0], &tablespaces_list, &tablespaces_count, ','))
               {
                  pgmoneta_log_error("Failed to parse tablespaces to be included");
                  goto error;
               }

               if (pgmoneta_split(temp_list[1], &databases_list, &databases_count, ','))
               {
                  pgmoneta_log_error("Failed to parse databases to be included");
                  goto error;
               }

               if (pgmoneta_split(temp_list[2], &relations_list, &relations_count, ','))
               {
                  pgmoneta_log_error("Failed to parse relations to be included");
                  goto error;
               }

               if (temp_list)
               {
                  for (int i = 0; temp_list[i] != NULL; i++)
                  {
                     free(temp_list[i]);
                  }
                  free(temp_list);
               }
            }
         }

         if (databases != NULL)
         {
            if (pgmoneta_split(databases, &databases_list, &databases_count, ','))
            {
               pgmoneta_log_error("Failed to parse databases to be included");
               goto error;
            }
         }

         if (tablespaces != NULL)
         {
            if (pgmoneta_split(tablespaces, &tablespaces_list, &tablespaces_count, ','))
            {
               pgmoneta_log_error("Failed to parse tablespaces to be included");
               goto error;
            }
         }

         if (relations != NULL)
         {
            if (pgmoneta_split(relations, &relations_list, &relations_count, ','))
            {
               pgmoneta_log_error("Failed to parse relations to be included");
               goto error;
            }
         }

         char** merged_lists[4] = {0};
         int idx = 0;

         if (databases_list != NULL)
         {
            merged_lists[idx++] = databases_list;
         }

         if (tablespaces_list != NULL)
         {
            merged_lists[idx++] = tablespaces_list;
         }

         if (relations_list != NULL)
         {
            merged_lists[idx++] = relations_list;
         }

         merged_lists[idx] = NULL;

         if (pgmoneta_merge_string_arrays(merged_lists, &included_objects))
         {
            pgmoneta_log_error("Failed to merge include lists");
            goto error;
         }

         if (databases_list)
         {
            for (int i = 0; i < databases_count; i++)
            {
               free(databases_list[i]);
            }
            free(databases_list);
         }

         if (tablespaces_list)
         {
            for (int i = 0; i < tablespaces_count; i++)
            {
               free(tablespaces_list[i]);
            }
            free(tablespaces_list);
         }

         if (relations_list)
         {
            for (int i = 0; i < relations_count; i++)
            {
               free(relations_list[i]);
            }
            free(relations_list);
         }
      }
      else
      {
         pgmoneta_log_error("OID mappings are not loaded, please provide a mappings file or server credentials and enable translation (-t)");
         goto error;
      }
   }

   if (output == NULL)
   {
      out = stdout;
   }
   else
   {
      out = fopen(output, "w");
      color = false;
   }

   if (interactive)
   {
      struct ui_state ui_state;

      if (filepath == NULL)
      {
         char cwd[MAX_PATH];
         if (getcwd(cwd, sizeof(cwd)) == NULL)
         {
            fprintf(stderr, "Error: Unable to get current working directory\n");
            goto error;
         }

         if (wal_interactive_init(&ui_state, cwd) != 0)
         {
            fprintf(stderr, "Error: Failed to initialize UI\n");
            goto error;
         }

         show_wal_file_selector(&ui_state);

         if (ui_state.record_count == 0)
         {
            wal_interactive_cleanup(&ui_state);
            pgmoneta_destroy_shared_memory(shmem, size);
            if (logfile)
            {
               pgmoneta_stop_logging();
            }
            return 0;
         }

         wal_interactive_run(&ui_state);
         wal_interactive_cleanup(&ui_state);
      }
      else
      {
         /* Check if file exists before initializing ncurses */
         if (!pgmoneta_exists(filepath))
         {
            fprintf(stderr, "Error: <%s> doesn't exist\n", filepath);
            goto error;
         }

         if (pgmoneta_is_directory(filepath))
         {
            if (wal_interactive_init(&ui_state, filepath) != 0)
            {
               fprintf(stderr, "Error: Failed to initialize UI\n");
               goto error;
            }

            show_wal_file_selector(&ui_state);

            if (ui_state.record_count == 0)
            {
               wal_interactive_cleanup(&ui_state);
               pgmoneta_destroy_shared_memory(shmem, size);
               if (logfile)
               {
                  pgmoneta_stop_logging();
               }
               return 0;
            }

            wal_interactive_run(&ui_state);
            wal_interactive_cleanup(&ui_state);

            return 0;
         }
         else
         {
            char* interactive_path = filepath;
            char* tar_temp_dir = NULL;
            struct deque* tar_wal_files = NULL;
            struct deque_iterator* tar_iter = NULL;
            char* first_wal_path = NULL;

            if (is_tar_archive_input(filepath))
            {
               if (prepare_wal_files_from_tar_archive(filepath, &tar_temp_dir, &tar_wal_files))
               {
                  fprintf(stderr, "Error: Failed to extract TAR archive: %s\n", filepath);
                  goto error;
               }

               if (tar_wal_files == NULL || pgmoneta_deque_size(tar_wal_files) <= 0)
               {
                  fprintf(stderr, "Error: No WAL files found in TAR archive: %s\n", filepath);
                  pgmoneta_deque_destroy(tar_wal_files);
                  pgmoneta_delete_directory(tar_temp_dir);
                  free(tar_temp_dir);
                  goto error;
               }

               pgmoneta_deque_iterator_create(tar_wal_files, &tar_iter);
               if (!pgmoneta_deque_iterator_next(tar_iter))
               {
                  fprintf(stderr, "Error: Failed to find WAL files in TAR archive: %s\n", filepath);
                  pgmoneta_deque_iterator_destroy(tar_iter);
                  pgmoneta_deque_destroy(tar_wal_files);
                  pgmoneta_delete_directory(tar_temp_dir);
                  free(tar_temp_dir);
                  goto error;
               }

               first_wal_path = pgmoneta_append(first_wal_path, (char*)tar_iter->value->data);
               pgmoneta_deque_iterator_destroy(tar_iter);
               tar_iter = NULL;

               if (first_wal_path == NULL)
               {
                  fprintf(stderr, "Error: Failed to stage first WAL file from TAR archive\n");
                  pgmoneta_deque_destroy(tar_wal_files);
                  pgmoneta_delete_directory(tar_temp_dir);
                  free(tar_temp_dir);
                  goto error;
               }

               interactive_path = first_wal_path;
            }
            else
            {
               if (pgmoneta_validate_wal_filename(filepath, NULL) != 0)
               {
                  fprintf(stderr, "Error: %s is not a valid WAL file\n", filepath);
                  goto error;
               }
            }

            if (wal_interactive_init(&ui_state, interactive_path) != 0)
            {
               fprintf(stderr, "Error: Failed to initialize UI\n");
               free(first_wal_path);
               pgmoneta_deque_destroy(tar_wal_files);
               pgmoneta_delete_directory(tar_temp_dir);
               free(tar_temp_dir);
               goto error;
            }

            if (wal_interactive_load_records(&ui_state, interactive_path) != 0)
            {
               fprintf(stderr, "Error: Failed to load WAL records\n");
               wal_interactive_cleanup(&ui_state);
               free(first_wal_path);
               pgmoneta_deque_destroy(tar_wal_files);
               pgmoneta_delete_directory(tar_temp_dir);
               free(tar_temp_dir);
               goto error;
            }

            wal_interactive_run(&ui_state);

            wal_interactive_cleanup(&ui_state);
            free(first_wal_path);
            pgmoneta_deque_destroy(tar_wal_files);
            pgmoneta_delete_directory(tar_temp_dir);
            free(tar_temp_dir);

            pgmoneta_destroy_shared_memory(shmem, size);
            if (logfile)
            {
               pgmoneta_stop_logging();
            }

            return 0;
         }
      }
   }

   if (filepath != NULL)
   {
      partial_record = malloc(sizeof(struct partial_xlog_record));
      partial_record->data_buffer_bytes_read = 0;
      partial_record->xlog_record_bytes_read = 0;
      partial_record->xlog_record = NULL;
      partial_record->data_buffer = NULL;

      if (!pgmoneta_exists(filepath))
      {
         fprintf(stderr, "Error: <%s> doesn't exist\n", filepath);
         goto error;
      }

      if (pgmoneta_is_directory(filepath))
      {
         if (describe_walfiles_in_directory(filepath, type, out, quiet, color,
                                            rms, start_lsn, end_lsn, xids, limit, summary, included_objects))
         {
            fprintf(stderr, "Error while reading/describing WAL directory\n");
            goto error;
         }
      }
      else
      {
         /* TAR archives bypass WAL filename validation - contents are validated after extraction */
         if (!is_tar_archive_input(filepath))
         {
            if (pgmoneta_validate_wal_filename(filepath, NULL) != 0)
            {
               fprintf(stderr, "Error: %s is not a valid WAL file\n", filepath);
               goto error;
            }
         }

         if (describe_walfile(filepath, type, out, quiet, color,
                              rms, start_lsn, end_lsn, xids, limit, summary, included_objects))
         {
            fprintf(stderr, "Error while reading/describing WAL file\n");
            goto error;
         }
      }

      if (partial_record->xlog_record != NULL)
      {
         free(partial_record->xlog_record);
      }
      if (partial_record->data_buffer != NULL)
      {
         free(partial_record->data_buffer);
      }
      free(partial_record);
      partial_record = NULL;
   }
   else if (!interactive)
   {
      fprintf(stderr, "Missing <file> argument\n");
      usage();
      goto error;
   }

   if (summary)
   {
      print_wal_statistics(out, type);
   }

   if (included_objects)
   {
      for (int i = 0; included_objects[i] != NULL; i++)
      {
         free(included_objects[i]);
      }
      free(included_objects);
   }

   pgmoneta_destroy_shared_memory(shmem, size);

   if (logfile)
   {
      pgmoneta_stop_logging();
   }

   if (verbose)
   {
      printf("Success\n");
   }

   pgmoneta_deque_destroy(rms);
   pgmoneta_deque_destroy(xids);

   if (out != NULL)
   {
      fflush(out);
      fclose(out);
   }

   return 0;

error:
   if (logfile)
   {
      pgmoneta_stop_logging();
   }

   pgmoneta_deque_destroy(rms);
   pgmoneta_deque_destroy(xids);

   if (shmem != NULL)
   {
      pgmoneta_destroy_shared_memory(shmem, size);
   }

   if (verbose)
   {
      printf("Failure\n");
   }

   if (databases_list)
   {
      for (int i = 0; i < databases_count; i++)
      {
         free(databases_list[i]);
      }
      free(databases_list);
   }

   if (tablespaces_list)
   {
      for (int i = 0; i < tablespaces_count; i++)
      {
         free(tablespaces_list[i]);
      }
      free(tablespaces_list);
   }

   if (relations_list)
   {
      for (int i = 0; i < relations_count; i++)
      {
         free(relations_list[i]);
      }
      free(relations_list);
   }

   if (included_objects)
   {
      for (int i = 0; included_objects[i] != NULL; i++)
      {
         free(included_objects[i]);
      }
      free(included_objects);
   }

   if (temp_list)
   {
      for (int i = 0; temp_list[i] != NULL; i++)
      {
         free(temp_list[i]);
      }
      free(temp_list);
   }

   if (out)
   {
      fflush(out);
      fclose(out);
   }

   return 1;
}

static int
describe_walfile(char* path, enum value_type type, FILE* out, bool quiet, bool color,
                 struct deque* rms, uint64_t start_lsn, uint64_t end_lsn, struct deque* xids,
                 uint32_t limit, bool summary, char** included_objects)
{
   /* Check if this is a TAR archive */
   if (is_tar_archive_input(path))
   {
      return describe_wal_tar_archive(path, type, out, quiet, color, rms,
                                      start_lsn, end_lsn, xids, limit,
                                      summary, included_objects);
   }

   return describe_walfile_internal(path, type, out, quiet, color, rms, start_lsn, end_lsn,
                                    xids, limit, summary, included_objects, NULL);
}

static int
describe_walfile_internal(char* path, enum value_type type, FILE* out, bool quiet, bool color,
                          struct deque* rms, uint64_t start_lsn, uint64_t end_lsn, struct deque* xids,
                          uint32_t limit, bool summary, char** included_objects,
                          struct column_widths* provided_widths)
{
   struct walfile* wf = NULL;
   struct deque_iterator* record_iterator = NULL;
   struct decoded_xlog_record* record = NULL;
   char* from = NULL;
   char* to = NULL;
   struct column_widths local_widths = {0};
   struct column_widths* widths = provided_widths ? provided_widths : &local_widths;

   if (!pgmoneta_is_file(path))
   {
      pgmoneta_log_error("WAL file at %s does not exist", path);
      goto error;
   }

   from = pgmoneta_append(from, path);
   to = pgmoneta_append(to, "/tmp/");
   to = pgmoneta_append(to, basename(path));

   if (pgmoneta_copy_and_extract_file(from, &to))
   {
      pgmoneta_log_error("Failed to extract WAL file from %s to %s", from, to);
      goto error;
   }

   if (pgmoneta_read_walfile(-1, to, &wf))
   {
      pgmoneta_log_error("Failed to read WAL file at %s", path);
      goto error;
   }

   if (type == ValueString && !summary && !provided_widths)
   {
      pgmoneta_calculate_column_widths(wf, start_lsn, end_lsn, rms, xids, included_objects, widths);
   }

   if (pgmoneta_deque_iterator_create(wf->records, &record_iterator))
   {
      pgmoneta_log_error("Failed to create deque iterator");
      goto error;
   }

   if (type == ValueJSON)
   {
      if (!quiet && !summary)
      {
         fprintf(out, "{ \"WAL\": [\n");
      }

      while (pgmoneta_deque_iterator_next(record_iterator))
      {
         record = (struct decoded_xlog_record*)record_iterator->value->data;
         if (summary)
         {
            pgmoneta_wal_record_collect_stats(record, start_lsn, end_lsn);
         }
         else
         {
            pgmoneta_wal_record_display(record, wf->long_phd->std.xlp_magic, type, out, quiet, color,
                                        rms, start_lsn, end_lsn, xids, limit, included_objects, widths);
         }
      }

      if (!quiet && !summary)
      {
         fprintf(out, "\n]}");
      }
   }
   else
   {
      while (pgmoneta_deque_iterator_next(record_iterator))
      {
         record = (struct decoded_xlog_record*)record_iterator->value->data;
         if (summary)
         {
            pgmoneta_wal_record_collect_stats(record, start_lsn, end_lsn);
         }
         else
         {
            pgmoneta_wal_record_display(record, wf->long_phd->std.xlp_magic, type, out, quiet, color,
                                        rms, start_lsn, end_lsn, xids, limit, included_objects, widths);
         }
      }
   }

   free(from);
   pgmoneta_deque_iterator_destroy(record_iterator);
   pgmoneta_destroy_walfile(wf);

   if (to != NULL)
   {
      pgmoneta_delete_file(to, NULL);
      free(to);
   }

   return 0;

error:
   free(from);
   pgmoneta_destroy_walfile(wf);
   pgmoneta_deque_iterator_destroy(record_iterator);

   if (to != NULL)
   {
      pgmoneta_delete_file(to, NULL);
      free(to);
   }

   return 1;
}

static int
describe_walfiles_in_directory(char* dir_path, enum value_type type, FILE* output, bool quiet, bool color,
                               struct deque* rms, uint64_t start_lsn, uint64_t end_lsn, struct deque* xids,
                               uint32_t limit, bool summary, char** included_objects)
{
   struct deque* files = NULL;
   struct deque_iterator* file_iterator = NULL;
   char* file_path = malloc(MAX_PATH);
   struct column_widths widths = {0};
   struct walfile* wf = NULL;
   char* from = NULL;
   char* to = NULL;

   if (pgmoneta_get_wal_files(dir_path, &files))
   {
      free(file_path);
      return 1;
   }

   if (type == ValueString && !summary)
   {
      pgmoneta_deque_iterator_create(files, &file_iterator);
      while (pgmoneta_deque_iterator_next(file_iterator))
      {
         snprintf(file_path, MAX_PATH, "%s/%s", dir_path, (char*)file_iterator->value->data);

         if (!pgmoneta_is_file(file_path))
         {
            continue;
         }

         from = pgmoneta_append(from, file_path);
         to = pgmoneta_append(to, "/tmp/");
         to = pgmoneta_append(to, basename(file_path));

         if (pgmoneta_copy_and_extract_file(from, &to))
         {
            free(from);
            free(to);
            from = NULL;
            to = NULL;
            continue;
         }

         if (pgmoneta_read_walfile(-1, to, &wf) == 0)
         {
            pgmoneta_calculate_column_widths(wf, start_lsn, end_lsn, rms, xids, included_objects, &widths);
            pgmoneta_destroy_walfile(wf);
            wf = NULL;
         }

         if (to != NULL)
         {
            pgmoneta_delete_file(to, NULL);
            free(to);
            to = NULL;
         }
         free(from);
         from = NULL;
      }
      pgmoneta_deque_iterator_destroy(file_iterator);
      file_iterator = NULL;
   }

   pgmoneta_deque_iterator_create(files, &file_iterator);
   while (pgmoneta_deque_iterator_next(file_iterator))
   {
      snprintf(file_path, MAX_PATH, "%s/%s", dir_path, (char*)file_iterator->value->data);

      struct column_widths* widths_to_use = (type == ValueString && !summary) ? &widths : NULL;
      if (describe_walfile_internal(file_path, type, output, quiet, color,
                                    rms, start_lsn, end_lsn, xids, limit, summary, included_objects, widths_to_use))
      {
         goto error;
      }
   }
   pgmoneta_deque_iterator_destroy(file_iterator);
   file_iterator = NULL;

   pgmoneta_deque_destroy(files);
   free(file_path);
   return 0;

error:
   pgmoneta_deque_destroy(files);
   pgmoneta_deque_iterator_destroy(file_iterator);

   free(file_path);
   free(from);
   if (to != NULL)
   {
      pgmoneta_delete_file(to, NULL);
      free(to);
   }
   pgmoneta_destroy_walfile(wf);
   return 1;
}

static int
prepare_wal_files_from_tar_archive(char* path, char** temp_dir, struct deque** wal_files)
{
   char* local_temp_dir = NULL;
   char* archive_copy_path = NULL;
   char* path_copy = NULL;
   struct stat archive_stat;
   unsigned long free_space = 0;

   if (path == NULL || temp_dir == NULL || wal_files == NULL)
   {
      goto error;
   }

   *temp_dir = NULL;
   *wal_files = NULL;

   local_temp_dir = pgmoneta_append(local_temp_dir, "/tmp/pgmoneta_wal_XXXXXX");
   if (local_temp_dir == NULL)
   {
      pgmoneta_log_error("Failed to allocate temp directory template");
      goto error;
   }

   if (mkdtemp(local_temp_dir) == NULL)
   {
      pgmoneta_log_error("Failed to create temp directory for TAR extraction");
      goto error;
   }

   path_copy = pgmoneta_append(path_copy, path);
   if (path_copy == NULL)
   {
      pgmoneta_log_error("Failed to allocate memory for TAR path copy");
      goto error;
   }

   archive_copy_path = pgmoneta_append(archive_copy_path, local_temp_dir);
   archive_copy_path = pgmoneta_append(archive_copy_path, "/");
   archive_copy_path = pgmoneta_append(archive_copy_path, basename(path_copy));
   if (archive_copy_path == NULL)
   {
      pgmoneta_log_error("Failed to build TAR staging path");
      goto error;
   }

   memset(&archive_stat, 0, sizeof(struct stat));
   if (stat(path, &archive_stat))
   {
      pgmoneta_log_error("Failed to stat TAR archive: %s", path);
      goto error;
   }

   free_space = pgmoneta_free_space(local_temp_dir);
   if (archive_stat.st_size > 0 && (free_space == 0 || (uint64_t)archive_stat.st_size > (uint64_t)free_space))
   {
      pgmoneta_log_error("Not enough temporary space to stage TAR archive: %s", path);
      goto error;
   }

   if (pgmoneta_copy_file(path, archive_copy_path, NULL))
   {
      pgmoneta_log_error("Failed to stage TAR archive: %s", path);
      goto error;
   }

   if (pgmoneta_extract_file(archive_copy_path, local_temp_dir))
   {
      pgmoneta_log_error("Failed to extract TAR archive: %s", path);
      goto error;
   }

   if (pgmoneta_get_files(PGMONETA_FILE_TYPE_WAL, local_temp_dir, true, wal_files))
   {
      pgmoneta_log_error("Failed to get WAL files from extracted TAR: %s", path);
      goto error;
   }

   *temp_dir = local_temp_dir;
   local_temp_dir = NULL;

   free(archive_copy_path);
   free(path_copy);
   return 0;

error:
   if (*wal_files != NULL)
   {
      pgmoneta_deque_destroy(*wal_files);
      *wal_files = NULL;
   }

   if (local_temp_dir != NULL)
   {
      pgmoneta_delete_directory(local_temp_dir);
      free(local_temp_dir);
   }

   free(archive_copy_path);
   free(path_copy);
   return 1;
}

static int
describe_wal_tar_archive(char* path, enum value_type type, FILE* out, bool quiet, bool color,
                         struct deque* rms, uint64_t start_lsn, uint64_t end_lsn,
                         struct deque* xids, uint32_t limit, bool summary,
                         char** included_objects)
{
   char* temp_dir = NULL;
   struct deque* wal_files = NULL;
   struct deque_iterator* iter = NULL;
   int result = 0;

   if (prepare_wal_files_from_tar_archive(path, &temp_dir, &wal_files))
   {
      goto error;
   }

   if (wal_files != NULL)
   {
      pgmoneta_deque_iterator_create(wal_files, &iter);
      while (pgmoneta_deque_iterator_next(iter))
      {
         char* wal_path = (char*)iter->value->data;
         if (describe_walfile_internal(wal_path, type, out, quiet, color, rms,
                                       start_lsn, end_lsn, xids, limit, summary,
                                       included_objects, NULL))
         {
            result = 1;
         }
      }
      pgmoneta_deque_iterator_destroy(iter);
   }

   pgmoneta_deque_destroy(wal_files);
   pgmoneta_delete_directory(temp_dir);
   free(temp_dir);

   return result;

error:
   if (iter != NULL)
   {
      pgmoneta_deque_iterator_destroy(iter);
   }

   if (wal_files != NULL)
   {
      pgmoneta_deque_destroy(wal_files);
   }

   if (temp_dir != NULL)
   {
      pgmoneta_delete_directory(temp_dir);
      free(temp_dir);
   }

   return 1;
}
