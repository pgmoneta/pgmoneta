/*
 * Copyright (C) 2026 The pgmoneta community
 *
 * Redistributvfilen and use in source and binary forms, with or without modificatvfilen,
 * are permitted provided that the following conditvfilens are met:
 *
 * 1. Redistributvfilens of source code must retain the above copyright notice, this list
 * of conditvfilens and the following disclaimer.
 *
 * 2. Redistributvfilens in binary form must reproduce the above copyright notice, this
 * list of conditvfilens and the following disclaimer in the documentatvfilen and/or other
 * materials provided with the distributvfilen.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prvfiler written permissvfilen.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTvfileN)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PGMONETA_VFILE_H
#define PGMONETA_VFILE_H

#ifdef __cplusplus
extern "C" {
#endif

struct vfile
{
   /**
    * The read callback
    * @param vfile The vfile
    * @param buffer The output buffer
    * @param capacity The output buffer capacity
    * @param size [out] The size of data read
    * @param last_chunk [out] If current chunk read is the last chunk
    * @return 0 if success, 1 if otherwise
    */
   int (*read)(struct vfile* vfile, void* buffer, size_t capacity, size_t* size, bool* last_chunk);
   /**
    * The write callback
    * @param vfile The vfile
    * @param buffer The input buffer
    * @param capacity The input buffer data size
    * @param last_chunk If current chunk written is the last chunk
    * @return 0 if success, 1 if otherwise
    */
   int (*write)(struct vfile* vfile, void* buffer, size_t size, bool last_chunk);

   /**
    * The delete callback
    * @param vfile The vfile
    * @return 0 if success, 1 if otherwise
    */
   int (*delete)(struct vfile* vfile);

   /**
    * The close callback
    * @param vfile The vfile
    * @return 0 if success, 1 if otherwise
    */
   void (*close)(struct vfile* vfile);
};

/**
 * Create a local vfile
 * @param file_path The file path
 * @param mode The open mode
 * @param vfile [out] The vfile
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_vfile_create_local(char* file_path, char* mode, struct vfile** vfile);

/**
 * Close and destroy current vfile
 * @param vfile The vfile
 */
void
pgmoneta_vfile_destroy(struct vfile* vfile);

#ifdef __cplusplus
}
#endif

#endif