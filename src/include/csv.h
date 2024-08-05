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

#ifndef PGMONETA_CSV_H
#define PGMONETA_CSV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/** @struct csv_reader
 * Defines a CSV reader
 */
struct csv_reader
{
   FILE* file;     /**< The file */
   char line[512]; /**< The line */
};

/** @struct csv_writer
 * Defines a CSV writer
 */
struct csv_writer
{
   FILE* file; /**< The file */
};

/**
 * Initialize a csv reader
 * @param path The path to the csv file
 * @param reader The reader
 * @return 0 on success, 1 if otherwise
 */
int
pgmoneta_csv_reader_init(char* path, struct csv_reader** reader);

/**
 * Get the next row in csv file.
 * You need to do free(cols) as the structure is allocated by the function
 * @param reader The reader
 * @param num_col [out] The number of columns in the row
 * @param cols [out] The columns in the row
 * @return true if has next row, false if otherwise
 */
bool
pgmoneta_csv_next_row(struct csv_reader* reader, int* num_col, char*** cols);

/**
 * Reset the reader pointer to the head of the file
 * @param reader The reader
 * @return 0 on success, 1 if otherwise
 */
int
pgmoneta_csv_reader_reset(struct csv_reader* reader);

/**
 * Destroy a csv reader
 * @param reader The reader
 * @return 0 on success, 1 if otherwise
 */
int
pgmoneta_csv_reader_destroy(struct csv_reader* reader);

/**
 * Initialize a csv writer
 * @param path The path to the csv file
 * @param writer The writer
 * @return 0 on success, 1 if otherwise
 */
int
pgmoneta_csv_writer_init(char* path, struct csv_writer** writer);

/**
 * Write a row to csv file
 * @param writer The csv writer
 * @param num_col The number of columns
 * @param cols The columns of the row
 * @return 0 on success, 1 if otherwise
 */
int
pgmoneta_csv_write(struct csv_writer* writer, int num_col, char** cols);

/**
 * Destroy a csv writer
 * @param writer The writer
 * @return 0 on success, 1 if otherwise
 */
int
pgmoneta_csv_writer_destroy(struct csv_writer* writer);

#ifdef __cplusplus
}
#endif

#endif
