/*
 * Copyright (C) 2022 Red Hat
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
#include <node.h>
#include <pgmoneta.h>
#include <info.h>
#include <management.h>
#include <network.h>
#include <logging.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <libgen.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#ifndef HAVE_OPENBSD
#include <sys/sysmacros.h>
#endif
#include <sys/types.h>

/* */
/* This file contains a minimal port of https://github.com/tklauser/libtar licensed under 3-Clause BSD */
/* */
/* Copyright (c) 1998-2003  University of Illinois Board of Trustees */
/* Copyright (c) 1998-2003  Mark D. Roth */
/* All rights reserved. */

/* Developed by: Campus Information Technologies and Educational Services, */
/*               University of Illinois at Urbana-Champaign */

/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the */
/* ``Software''), to deal with the Software without restriction, including */
/* without limitation the rights to use, copy, modify, merge, publish, */
/* distribute, sublicense, and/or sell copies of the Software, and to */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions: */

/* * Redistributions of source code must retain the above copyright */
/*   notice, this list of conditions and the following disclaimers. */

/* * Redistributions in binary form must reproduce the above copyright */
/*   notice, this list of conditions and the following disclaimers in the */
/*   documentation and/or other materials provided with the distribution. */

/* * Neither the names of Campus Information Technologies and Educational */
/*   Services, University of Illinois at Urbana-Champaign, nor the names */
/*   of its contributors may be used to endorse or promote products derived */
/*   from this Software without specific prior written permission. */

/* THE SOFTWARE IS PROVIDED ``AS IS'', WITHOUT WARRANTY OF ANY KIND, */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR */
/* ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE */
/* OR THE USE OR OTHER DEALINGS WITH THE SOFTWARE. */

#define REGTYPE  '0'
#define AREGTYPE '\0'
#define LNKTYPE  '1'
#define SYMTYPE  '2'
#define CHRTYPE  '3'
#define BLKTYPE  '4'
#define DIRTYPE  '5'
#define FIFOTYPE '6'
#define CONTTYPE '7'

typedef int (* openfunc_t)(const char*, int, ...);
typedef int (* closefunc_t)(int);
typedef ssize_t (* readfunc_t)(int, void*, size_t);
typedef ssize_t (* writefunc_t)(int, const void*, size_t);

#define TH_ISREG(t) ((t)->th_buf.typeflag == REGTYPE \
                     || (t)->th_buf.typeflag == AREGTYPE     \
                     || (t)->th_buf.typeflag == CONTTYPE                \
                     || (S_ISREG((mode_t)oct_to_int((t)->th_buf.mode))  \
                         && (t)->th_buf.typeflag != LNKTYPE))
#define TH_ISSYM(t) ((t)->th_buf.typeflag == SYMTYPE \
                     || S_ISLNK((mode_t)oct_to_int((t)->th_buf.mode)))
#define TH_ISDIR(t) ((t)->th_buf.typeflag == DIRTYPE                     \
                     || S_ISDIR((mode_t)oct_to_int((t)->th_buf.mode))   \
                     || ((t)->th_buf.typeflag == AREGTYPE               \
                         && strlen((t)->th_buf.name)                    \
                         && ((t)->th_buf.name[strlen((t)->th_buf.name) - 1] == '/')))

static int oct_to_int(char* oct);
static size_t oct_to_size(char* oct);
static void int_to_oct_nonull(int num, char* oct, size_t octlen);
static void int_to_oct(int num, char* oct, size_t octlen);

#define th_get_size(t) oct_to_size((t)->th_buf.size)
#define th_set_size(t, fsize) \
   int_to_oct_nonull((fsize), (t)->th_buf.size, 12)

typedef unsigned int (* libtar_hashfunc_t)(void*, unsigned int);
typedef void (* libtar_freefunc_t)(void*);
typedef int (* libtar_cmpfunc_t)(void*, void*);
typedef int (* libtar_matchfunc_t)(void*, void*);

struct libtar_node
{
   void* data;
   struct libtar_node* next;
   struct libtar_node* prev;
};
typedef struct libtar_node* libtar_listptr_t;

struct libtar_hashptr
{
   int bucket;
   libtar_listptr_t node;
};
typedef struct libtar_hashptr libtar_hashptr_t;

struct libtar_list
{
   libtar_listptr_t first;
   libtar_listptr_t last;
   libtar_cmpfunc_t cmpfunc;
   unsigned int nents;
};
typedef struct libtar_list libtar_list_t;

struct libtar_hash
{
   int numbuckets;
   libtar_list_t** table;
   libtar_hashfunc_t hashfunc;
   unsigned int nents;
};
typedef struct libtar_hash libtar_hash_t;

static libtar_hash_t* libtar_hash_new(int, libtar_hashfunc_t);
static void libtar_hash_free(libtar_hash_t*, libtar_freefunc_t);
static int libtar_hash_getkey(libtar_hash_t*, libtar_hashptr_t*, void*, libtar_matchfunc_t);
static void libtar_hashptr_reset(libtar_hashptr_t*);
static void* libtar_hashptr_data(libtar_hashptr_t*);
static int libtar_hash_add(libtar_hash_t*, void*);
static unsigned int libtar_str_hashfunc(char*, unsigned int);
static int libtar_str_match(char*, char*);

static void libtar_list_free(libtar_list_t*, libtar_freefunc_t);
static int libtar_list_search(libtar_list_t*, libtar_listptr_t*, void*, libtar_matchfunc_t);
static void libtar_listptr_reset(libtar_listptr_t*);
static void* libtar_listptr_data(libtar_listptr_t*);
static libtar_list_t* libtar_list_new(libtar_cmpfunc_t);
static int libtar_list_add(libtar_list_t*, void*);
static void libtar_list_empty(libtar_list_t*, libtar_freefunc_t);

#define T_BLOCKSIZE  512
#define T_NAMELEN 100
#define T_PREFIXLEN  155
#define T_MAXPATHLEN (T_NAMELEN + T_PREFIXLEN)

#define GNU_LONGNAME_TYPE 'L'
#define GNU_LONGLINK_TYPE 'K'

struct tar_header
{
   char name[100];
   char mode[8];
   char uid[8];
   char gid[8];
   char size[12];
   char mtime[12];
   char chksum[8];
   char typeflag;
   char linkname[100];
   char magic[6];
   char version[2];
   char uname[32];
   char gname[32];
   char devmajor[8];
   char devminor[8];
   char prefix[155];
   char padding[12];
   char* gnu_longname;
   char* gnu_longlink;
};

typedef struct
{
   openfunc_t openfunc;
   closefunc_t closefunc;
   readfunc_t readfunc;
   writefunc_t writefunc;
}
tartype_t;

#define tar_block_write(t, buf) \
   (*((t)->type->writefunc))((t)->fd, (char*)(buf), T_BLOCKSIZE)

static tartype_t default_type = {open, close, read, write};

struct tar_dev
{
   dev_t td_dev;
   libtar_hash_t* td_h;
};
typedef struct tar_dev tar_dev_t;

struct tar_ino
{
   ino_t ti_ino;
   char ti_name[MAXPATHLEN];
};
typedef struct tar_ino tar_ino_t;

typedef struct
{
   tartype_t* type;
   const char* pathname;
   long fd;
   int oflags;
   struct tar_header th_buf;
   libtar_hash_t* h;
   char* th_pathname;
}
TAR;

static int tar_open(TAR** t, const char* pathname, tartype_t* type, int oflags, int mode);
static int tar_close(TAR* t);
static int tar_append_tree(TAR* t, char* realdir, char* savedir);
static int tar_append_file(TAR* t, const char* realname, const char* savename);
static int tar_append_regfile(TAR* t, const char* realname);
static void th_set_path(TAR* t, const char* pathname);
static void th_set_link(TAR* t, const char* linkname);
static int th_write(TAR* t);
static void th_finish(TAR* t);
static int th_crc_calc(TAR* t);
static void th_set_from_stat(TAR* t, struct stat* s);
static void th_set_type(TAR* t, mode_t mode);
static void th_set_device(TAR* t, dev_t device);
static void th_set_user(TAR* t, uid_t uid);
static void th_set_group(TAR* t, gid_t gid);
static void th_set_mode(TAR* t, mode_t fmode);

static int path_hashfunc(char* key, int numbuckets);
static unsigned int dev_hash_wrap(void* p, unsigned int n);
static void tar_dev_free(struct tar_dev* tdp);
static int dev_match(dev_t* dev1, dev_t* dev2);
static unsigned int ino_hash_wrap(void* p, unsigned int n);
static int ino_match(ino_t* ino1, ino_t* ino2);
static int dev_hash(dev_t* dev);
static int ino_hash(ino_t* inode);

#define th_set_mtime(t, fmtime)                                 \
   int_to_oct_nonull((fmtime), (t)->th_buf.mtime, 12)

static TAR* tar = NULL;

static int archive_setup(int, char*, struct node*, struct node**);
static int archive_execute(int, char*, struct node*, struct node**);
static int archive_teardown(int, char*, struct node*, struct node**);

struct workflow*
pgmoneta_workflow_create_archive(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->setup = &archive_setup;
   wf->execute = &archive_execute;
   wf->teardown = &archive_teardown;
   wf->next = NULL;

   return wf;
}

static int
archive_setup(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* tarfile = NULL;
   char* directory = NULL;
   char* id = NULL;
   struct node* o_tarfile = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   directory = pgmoneta_get_node_string(i_nodes, "directory");
   id = pgmoneta_get_node_string(i_nodes, "id");

   tarfile = pgmoneta_append(tarfile, directory);
   tarfile = pgmoneta_append(tarfile, "/");
   tarfile = pgmoneta_append(tarfile, config->servers[server].name);
   tarfile = pgmoneta_append(tarfile, "-");
   tarfile = pgmoneta_append(tarfile, id);
   tarfile = pgmoneta_append(tarfile, ".tar");

   if (pgmoneta_create_node_string(tarfile, "tarfile", &o_tarfile))
   {
      goto error;
   }

   pgmoneta_append_node(o_nodes, o_tarfile);

   tar_open(&tar, tarfile, NULL, O_WRONLY | O_CREAT, 0644);

   free(tarfile);

   return 0;

error:

   free(tarfile);

   return 1;
}

static int
archive_execute(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* output = NULL;

   output = pgmoneta_get_node_string(i_nodes, "output");

   if (output == NULL)
   {
      goto error;
   }

   tar_append_tree(tar, output, ".");

   return 0;

error:

   return 1;
}

static int
archive_teardown(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* output = NULL;

   output = pgmoneta_get_node_string(i_nodes, "output");

   if (output == NULL)
   {
      goto error;
   }

   tar_close(tar);

   pgmoneta_delete_directory(output);

   return 0;

error:

   return 1;
}

static int
tar_init(TAR** t, const char* pathname, tartype_t* type, int oflags, int mode)
{
   if ((oflags & O_ACCMODE) == O_RDWR)
   {
      errno = EINVAL;
      return -1;
   }

   *t = (TAR*)calloc(1, sizeof(TAR));
   if (*t == NULL)
   {
      return -1;
   }

   (*t)->pathname = pathname;
   (*t)->type = (type ? type : &default_type);
   (*t)->oflags = oflags;

   if ((oflags & O_ACCMODE) == O_RDONLY)
   {
      (*t)->h = libtar_hash_new(256, (libtar_hashfunc_t)path_hashfunc);
   }
   else
   {
      (*t)->h = libtar_hash_new(16, dev_hash_wrap);
   }

   if ((*t)->h == NULL)
   {
      free(*t);
      return -1;
   }

   return 0;
}

static int
tar_open(TAR** t, const char* pathname, tartype_t* type, int oflags, int mode)
{
   if (tar_init(t, pathname, type, oflags, mode) == -1)
   {
      return -1;
   }

   (*t)->fd = (*((*t)->type->openfunc))(pathname, oflags, mode);
   if ((*t)->fd == -1)
   {
      libtar_hash_free((*t)->h, NULL);
      free(*t);
      return -1;
   }

   return 0;
}

static int
tar_close(TAR* t)
{
   int i;

   i = (*(t->type->closefunc))(t->fd);

   if (t->h != NULL)
   {
      libtar_hash_free(t->h, ((t->oflags & O_ACCMODE) == O_RDONLY ? free : (libtar_freefunc_t)tar_dev_free));
   }

   if (t->th_pathname != NULL)
   {
      free(t->th_pathname);
   }
   free(t);

   return i;
}

static int
tar_append_tree(TAR* t, char* realdir, char* savedir)
{
   int ret = -1;
   char realpath[MAXPATHLEN];
   char savepath[MAXPATHLEN];
   struct dirent* dent;
   DIR* dp;
   struct stat s;

   if (tar_append_file(t, realdir, savedir) != 0)
   {
      return -1;
   }

   dp = opendir(realdir);
   if (dp == NULL)
   {
      if (errno == ENOTDIR)
      {
         return 0;
      }

      return -1;
   }

   while ((dent = readdir(dp)) != NULL)
   {
      if (strcmp(dent->d_name, ".") == 0 || strcmp(dent->d_name, "..") == 0)
      {
         continue;
      }

      snprintf(realpath, MAXPATHLEN, "%s/%s", realdir, dent->d_name);

      if (savedir)
      {
         snprintf(savepath, MAXPATHLEN, "%s/%s", savedir, dent->d_name);
      }

      if (lstat(realpath, &s) != 0)
      {
         goto out;
      }

      if (S_ISDIR(s.st_mode))
      {
         if (tar_append_tree(t, realpath, (savedir ? savepath : NULL)) != 0)
         {
            goto out;
         }

         continue;
      }

      if (tar_append_file(t, realpath, (savedir ? savepath : NULL)) != 0)
      {
         goto out;
      }
   }

   ret = 0;

out:

   if (dp != NULL)
   {
      closedir(dp);
   }

   return ret;
}

static int
tar_append_file(TAR* t, const char* realname, const char* savename)
{
   struct stat s;
   int i;
   libtar_hashptr_t hp;
   tar_dev_t* td = NULL;
   tar_ino_t* ti = NULL;
   char path[MAXPATHLEN];

   if (lstat(realname, &s) != 0)
   {
      return -1;
   }

   memset(&(t->th_buf), 0, sizeof(struct tar_header));
   th_set_from_stat(t, &s);

   th_set_path(t, (savename ? savename : realname));

   libtar_hashptr_reset(&hp);
   if (libtar_hash_getkey(t->h, &hp, &(s.st_dev), (libtar_matchfunc_t)dev_match) != 0)
   {
      td = (tar_dev_t*)libtar_hashptr_data(&hp);
   }
   else
   {
      td = (tar_dev_t*)calloc(1, sizeof(tar_dev_t));

      if (td == NULL)
      {
         return -1;
      }

      td->td_dev = s.st_dev;
      td->td_h = libtar_hash_new(256, ino_hash_wrap);
      if (td->td_h == NULL)
      {
         free(td);
         return -1;
      }

      if (libtar_hash_add(t->h, td) == -1)
      {
         return -1;
      }
   }

   libtar_hashptr_reset(&hp);
   if (libtar_hash_getkey(td->td_h, &hp, &(s.st_ino), (libtar_matchfunc_t)ino_match) != 0)
   {
      ti = (tar_ino_t*)libtar_hashptr_data(&hp);
      t->th_buf.typeflag = LNKTYPE;
      th_set_link(t, ti->ti_name);
   }
   else
   {
      ti = (tar_ino_t*)calloc(1, sizeof(tar_ino_t));

      if (ti == NULL)
      {
         return -1;
      }

      ti->ti_ino = s.st_ino;
      snprintf(ti->ti_name, sizeof(ti->ti_name), "%s", savename ? savename : realname);
      libtar_hash_add(td->td_h, ti);
   }

   if (TH_ISSYM(t))
   {
      i = readlink(realname, path, sizeof(path));
      if (i == -1)
      {
         return -1;
      }

      if (i >= MAXPATHLEN)
      {
         i = MAXPATHLEN - 1;
      }

      path[i] = '\0';
      th_set_link(t, path);
   }

   if (th_write(t) != 0)
   {
      return -1;
   }

   if (TH_ISREG(t) && tar_append_regfile(t, realname) != 0)
   {
      return -1;
   }

   return 0;
}

static int
tar_append_regfile(TAR* t, const char* realname)
{
   char block[T_BLOCKSIZE];
   int filefd;
   int i, j;
   size_t size;
   int rv = -1;

   filefd = open(realname, O_RDONLY);
   if (filefd == -1)
   {
      return -1;
   }

   size = th_get_size(t);
   for (i = size; i > T_BLOCKSIZE; i -= T_BLOCKSIZE)
   {
      j = read(filefd, &block, T_BLOCKSIZE);
      if (j != T_BLOCKSIZE)
      {
         if (j != -1)
         {
            errno = EINVAL;
         }

         goto fail;
      }

      if (tar_block_write(t, &block) == -1)
      {
         goto fail;
      }
   }

   if (i > 0)
   {
      j = read(filefd, &block, i);

      if (j == -1)
      {
         goto fail;
      }

      memset(&(block[i]), 0, T_BLOCKSIZE - i);

      if (tar_block_write(t, &block) == -1)
      {
         goto fail;
      }
   }

   rv = 0;

fail:

   close(filefd);

   return rv;
}

#ifndef HAVE_OPENBSD
static size_t
strlcpy(char* dst, const char* src, size_t siz)
{
   register char* d = dst;
   register const char* s = src;
   register size_t n = siz;

   if (n != 0 && --n != 0)
   {
      do
      {
         if ((*d++ = *s++) == 0)
         {
            break;
         }
      }
      while (--n != 0);
   }

   if (n == 0)
   {
      if (siz != 0)
      {
         *d = '\0';
      }
      while (*s++)
         ;
   }

   return (s - src - 1);
}
#endif

static void
th_set_path(TAR* t, const char* pathname)
{
   char suffix[2] = "";
   char* tmp;

   if (t->th_buf.gnu_longname != NULL)
   {
      free(t->th_buf.gnu_longname);
   }

   t->th_buf.gnu_longname = NULL;

   if (pathname[strlen(pathname) - 1] != '/' && TH_ISDIR(t))
   {
      strcpy(suffix, "/");
   }

   if (strlen(pathname) > T_NAMELEN - 1)
   {
      t->th_buf.gnu_longname = strdup(pathname);
      strncpy(t->th_buf.name, t->th_buf.gnu_longname, T_NAMELEN);
   }
   else if (strlen(pathname) > T_NAMELEN)
   {
      tmp = strchr(&(pathname[strlen(pathname) - T_NAMELEN - 1]), '/');

      if (tmp == NULL)
      {
         return;
      }

      snprintf(t->th_buf.name, 100, "%s%s", &(tmp[1]), suffix);
      snprintf(t->th_buf.prefix, ((tmp - pathname + 1) < 155 ? (tmp - pathname + 1) : 155), "%s", pathname);
   }
   else
   {
      snprintf(t->th_buf.name, 100, "%s%s", pathname, suffix);
   }
}

static int
th_write(TAR* t)
{
   int i, j;
   char type2;
   size_t sz, sz2;
   char* ptr;
   char buf[T_BLOCKSIZE];

   if (t->th_buf.gnu_longlink != NULL)
   {
      type2 = t->th_buf.typeflag;
      sz2 = th_get_size(t);

      t->th_buf.typeflag = GNU_LONGLINK_TYPE;
      sz = strlen(t->th_buf.gnu_longlink);
      th_set_size(t, sz);
      th_finish(t);
      i = tar_block_write(t, &(t->th_buf));
      if (i != T_BLOCKSIZE)
      {
         if (i != -1)
         {
            errno = EINVAL;
         }
         return -1;
      }

      for (j = (sz / T_BLOCKSIZE) + (sz % T_BLOCKSIZE ? 1 : 0), ptr = t->th_buf.gnu_longlink; j > 1; j--, ptr += T_BLOCKSIZE)
      {
         i = tar_block_write(t, ptr);
         if (i != T_BLOCKSIZE)
         {
            if (i != -1)
            {
               errno = EINVAL;
            }
            return -1;
         }
      }

      memset(buf, 0, T_BLOCKSIZE);
      memcpy(buf, ptr, T_BLOCKSIZE);
      i = tar_block_write(t, &buf);
      if (i != T_BLOCKSIZE)
      {
         if (i != -1)
         {
            errno = EINVAL;
         }
         return -1;
      }

      t->th_buf.typeflag = type2;
      th_set_size(t, sz2);
   }

   if (t->th_buf.gnu_longname != NULL)
   {
      type2 = t->th_buf.typeflag;
      sz2 = th_get_size(t);

      t->th_buf.typeflag = GNU_LONGNAME_TYPE;
      sz = strlen(t->th_buf.gnu_longname);
      th_set_size(t, sz);
      th_finish(t);
      i = tar_block_write(t, &(t->th_buf));
      if (i != T_BLOCKSIZE)
      {
         if (i != -1)
         {
            errno = EINVAL;
         }
         return -1;
      }

      for (j = (sz / T_BLOCKSIZE) + (sz % T_BLOCKSIZE ? 1 : 0), ptr = t->th_buf.gnu_longname; j > 1; j--, ptr += T_BLOCKSIZE)
      {
         i = tar_block_write(t, ptr);
         if (i != T_BLOCKSIZE)
         {
            if (i != -1)
            {
               errno = EINVAL;
            }
            return -1;
         }
      }

      memset(buf, 0, T_BLOCKSIZE);
      memcpy(buf, ptr, T_BLOCKSIZE);
      i = tar_block_write(t, &buf);
      if (i != T_BLOCKSIZE)
      {
         if (i != -1)
         {
            errno = EINVAL;
         }
         return -1;
      }

      t->th_buf.typeflag = type2;
      th_set_size(t, sz2);
   }

   th_finish(t);

   i = tar_block_write(t, &(t->th_buf));
   if (i != T_BLOCKSIZE)
   {
      if (i != -1)
      {
         errno = EINVAL;
      }
      return -1;
   }

   return 0;
}

static void
th_finish(TAR* t)
{
   memcpy(t->th_buf.magic, "ustar ", 6);
   memcpy(t->th_buf.version, " ", 2);

   int_to_oct(th_crc_calc(t), t->th_buf.chksum, 8);
}

static int
oct_to_int(char* oct)
{
   int i;

   return sscanf(oct, "%o", &i) == 1 ? i : 0;
}

static size_t
oct_to_size(char* oct)
{
   size_t i;

   return sscanf(oct, "%zo", &i) == 1 ? i : 0;
}

static void
int_to_oct_nonull(int num, char* oct, size_t octlen)
{
   snprintf(oct, octlen, "%*lo", (int)(octlen - 1), (unsigned long)num);
   oct[octlen - 1] = ' ';
}

static void
int_to_oct(int num, char* oct, size_t octlen)
{
   memset(oct, 0, octlen);
   snprintf(oct, octlen, "%*lo ", (int)(octlen - 2), (unsigned long)num);
}

static void
th_set_link(TAR* t, const char* linkname)
{
   if (strlen(linkname) > T_NAMELEN - 1)
   {
      t->th_buf.gnu_longlink = strdup(linkname);
      strcpy(t->th_buf.linkname, "././@LongLink");
   }
   else
   {
      strlcpy(t->th_buf.linkname, linkname, sizeof(t->th_buf.linkname));

      if (t->th_buf.gnu_longlink != NULL)
      {
         free(t->th_buf.gnu_longlink);
      }

      t->th_buf.gnu_longlink = NULL;
   }
}

static int
th_crc_calc(TAR* t)
{
   int i, sum = 0;

   for (i = 0; i < T_BLOCKSIZE; i++)
   {
      sum += ((unsigned char*)(&(t->th_buf)))[i];
   }

   for (i = 0; i < 8; i++)
   {
      sum += (' ' - (unsigned char)t->th_buf.chksum[i]);
   }

   return sum;
}

static void
th_set_from_stat(TAR* t, struct stat* s)
{
   th_set_type(t, s->st_mode);

   if (S_ISCHR(s->st_mode) || S_ISBLK(s->st_mode))
   {
      th_set_device(t, s->st_rdev);
   }

   th_set_user(t, s->st_uid);
   th_set_group(t, s->st_gid);
   th_set_mode(t, s->st_mode);
   th_set_mtime(t, s->st_mtime);

   if (S_ISREG(s->st_mode))
   {
      th_set_size(t, s->st_size);
   }
   else
   {
      th_set_size(t, 0);
   }
}

static void
th_set_type(TAR* t, mode_t mode)
{
   if (S_ISLNK(mode))
   {
      t->th_buf.typeflag = SYMTYPE;
   }

   if (S_ISREG(mode))
   {
      t->th_buf.typeflag = REGTYPE;
   }

   if (S_ISDIR(mode))
   {
      t->th_buf.typeflag = DIRTYPE;
   }

   if (S_ISCHR(mode))
   {
      t->th_buf.typeflag = CHRTYPE;
   }

   if (S_ISBLK(mode))
   {
      t->th_buf.typeflag = BLKTYPE;
   }

   if (S_ISFIFO(mode) || S_ISSOCK(mode))
   {
      t->th_buf.typeflag = FIFOTYPE;
   }
}

static void
th_set_device(TAR* t, dev_t device)
{
   int_to_oct(major(device), t->th_buf.devmajor, 8);
   int_to_oct(minor(device), t->th_buf.devminor, 8);
}

static void
th_set_user(TAR* t, uid_t uid)
{
   struct passwd* pw;

   pw = getpwuid(uid);

   if (pw != NULL)
   {
      strlcpy(t->th_buf.uname, pw->pw_name, sizeof(t->th_buf.uname));
   }

   int_to_oct(uid, t->th_buf.uid, 8);
}

static void
th_set_group(TAR* t, gid_t gid)
{
   struct group* gr;

   gr = getgrgid(gid);

   if (gr != NULL)
   {
      strlcpy(t->th_buf.gname, gr->gr_name, sizeof(t->th_buf.gname));
   }

   int_to_oct(gid, t->th_buf.gid, 8);
}

static void
th_set_mode(TAR* t, mode_t fmode)
{
   if (S_ISSOCK(fmode))
   {
      fmode &= ~S_IFSOCK;
      fmode |= S_IFIFO;
   }

   int_to_oct(fmode, (t)->th_buf.mode, 8);
}

static int
path_hashfunc(char* key, int numbuckets)
{
   char buf[MAXPATHLEN];
   char* p;

   strcpy(buf, key);
   p = basename(buf);

   return (((unsigned int)p[0]) % numbuckets);
}

static unsigned int
dev_hash_wrap(void* p, unsigned int n)
{
   (void)n;
   return (unsigned int)dev_hash(p);
}

static void
tar_dev_free(tar_dev_t* tdp)
{
   libtar_hash_free(tdp->td_h, free);
   free(tdp);
}

static int
dev_match(dev_t* dev1, dev_t* dev2)
{
   return !memcmp(dev1, dev2, sizeof(dev_t));
}

static unsigned int
ino_hash_wrap(void* p, unsigned int n)
{
   (void)n;
   return (unsigned int)ino_hash(p);
}

static int
ino_match(ino_t* ino1, ino_t* ino2)
{
   return !memcmp(ino1, ino2, sizeof(ino_t));
}

static int
dev_hash(dev_t* dev)
{
   return *dev % 16;
}

static int
ino_hash(ino_t* inode)
{
   return *inode % 256;
}

static libtar_hash_t*
libtar_hash_new(int num, libtar_hashfunc_t hashfunc)
{
   libtar_hash_t* hash;

   hash = (libtar_hash_t*)calloc(1, sizeof(libtar_hash_t));

   if (hash == NULL)
   {
      return NULL;
   }

   hash->numbuckets = num;

   if (hashfunc != NULL)
   {
      hash->hashfunc = hashfunc;
   }
   else
   {
      hash->hashfunc = (libtar_hashfunc_t)libtar_str_hashfunc;
   }

   hash->table = (libtar_list_t**)calloc(num, sizeof(libtar_list_t*));

   if (hash->table == NULL)
   {
      free(hash);
      return NULL;
   }

   return hash;
}

static void
libtar_hash_free(libtar_hash_t* h, libtar_freefunc_t freefunc)
{
   int i;

   for (i = 0; i < h->numbuckets; i++)
   {
      if (h->table[i] != NULL)
      {
         libtar_list_free(h->table[i], freefunc);
      }
   }

   free(h->table);
   free(h);
}

static int
libtar_hash_getkey(libtar_hash_t* h, libtar_hashptr_t* hp, void* key, libtar_matchfunc_t matchfunc)
{
   if (hp->bucket == -1)
   {
      hp->bucket = (*(h->hashfunc))(key, h->numbuckets);
   }

   if (h->table[hp->bucket] == NULL)
   {
      hp->bucket = -1;
      return 0;
   }

   return libtar_list_search(h->table[hp->bucket], &(hp->node), key, matchfunc);
}

static void
libtar_hashptr_reset(libtar_hashptr_t* hp)
{
   libtar_listptr_reset(&(hp->node));
   hp->bucket = -1;
}

static void*
libtar_hashptr_data(libtar_hashptr_t* hp)
{
   return libtar_listptr_data(&(hp->node));
}

static int
libtar_hash_add(libtar_hash_t* h, void* data)
{
   int bucket, i;

   bucket = (*(h->hashfunc))(data, h->numbuckets);

   if (h->table[bucket] == NULL)
   {
      h->table[bucket] = libtar_list_new(NULL);
   }

   i = libtar_list_add(h->table[bucket], data);

   if (i == 0)
   {
      h->nents++;
   }

   return i;
}

static unsigned int
libtar_str_hashfunc(char* key, unsigned int num_buckets)
{
   if (key == NULL)
   {
      return 0;
   }

   return (key[0] % num_buckets);
}

static int
libtar_str_match(char* check, char* data)
{
   return !strcmp(check, data);
}

static void
libtar_list_free(libtar_list_t* l, libtar_freefunc_t freefunc)
{
   libtar_list_empty(l, freefunc);
   free(l);
}

static int
libtar_list_search(libtar_list_t* l, libtar_listptr_t* n, void* data, libtar_matchfunc_t matchfunc)
{
   if (matchfunc == NULL)
   {
      matchfunc = (libtar_matchfunc_t)libtar_str_match;
   }

   if (*n == NULL)
   {
      *n = l->first;
   }
   else
   {
      *n = (*n)->next;
   }

   for (; *n != NULL; *n = (*n)->next)
   {
      if ((*(matchfunc))(data, (*n)->data) != 0)
      {
         return 1;
      }
   }

   return 0;
}

static void
libtar_listptr_reset(libtar_listptr_t* lp)
{
   *lp = NULL;
}

static void*
libtar_listptr_data(libtar_listptr_t* lp)
{
   return (*lp)->data;
}

static libtar_list_t*
libtar_list_new(libtar_cmpfunc_t cmpfunc)
{
   libtar_list_t* newlist;

   newlist = (libtar_list_t*)calloc(1, sizeof(libtar_list_t));

   if (cmpfunc != NULL)
   {
      newlist->cmpfunc = cmpfunc;
   }
   else
   {
      newlist->cmpfunc = (libtar_cmpfunc_t)strcmp;
   }

   return newlist;
}

static int
libtar_list_add(libtar_list_t* l, void* data)
{
   libtar_listptr_t n;

   n = (libtar_listptr_t)malloc(sizeof(struct libtar_node));

   if (n == NULL)
   {
      return -1;
   }

   n->data = data;
   l->nents++;

   if (l->first == NULL)
   {
      l->last = l->first = n;
      n->next = n->prev = NULL;
      return 0;
   }

   n->prev = l->last;
   n->next = NULL;

   if (l->last != NULL)
   {
      l->last->next = n;
   }

   l->last = n;

   return 0;
}

static void
libtar_list_empty(libtar_list_t* l, libtar_freefunc_t freefunc)
{
   libtar_listptr_t n;

   for (n = l->first; n != NULL; n = l->first)
   {
      l->first = n->next;
      if (freefunc != NULL)
      {
         (*freefunc)(n->data);
      }
      free(n);
   }

   l->nents = 0;
}
