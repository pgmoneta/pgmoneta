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

/* pgmoneta */
#include <pgmoneta.h>
#include <logging.h>
#include <network.h>

/* system */
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

static int bind_host(const char* hostname, int port, int** fds, int* length);

/**
 *
 */
int
pgmoneta_bind(const char* hostname, int port, int** fds, int* length)
{
   struct ifaddrs* ifaddr, * ifa;
   struct sockaddr_in* sa4;
   struct sockaddr_in6* sa6;
   char addr[50];
   int* star_fds = NULL;
   int star_length = 0;

   if (!strcmp("*", hostname))
   {
      if (getifaddrs(&ifaddr) == -1)
      {
         pgmoneta_log_warn("getifaddrs: %s", strerror(errno));
         errno = 0;
         return 1;
      }

      for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
      {
         if (ifa->ifa_addr != NULL &&
             (ifa->ifa_addr->sa_family == AF_INET || ifa->ifa_addr->sa_family == AF_INET6) &&
             (ifa->ifa_flags & IFF_UP))
         {
            int* new_fds = NULL;
            int new_length = 0;

            memset(addr, 0, sizeof(addr));

            if (ifa->ifa_addr->sa_family == AF_INET)
            {
               sa4 = (struct sockaddr_in*) ifa->ifa_addr;
               inet_ntop(AF_INET, &sa4->sin_addr, addr, sizeof(addr));
            }
            else
            {
               sa6 = (struct sockaddr_in6*) ifa->ifa_addr;
               inet_ntop(AF_INET6, &sa6->sin6_addr, addr, sizeof(addr));
            }

            if (bind_host(addr, port, &new_fds, &new_length))
            {
               free(new_fds);
               continue;
            }

            if (star_fds == NULL)
            {
               star_fds = malloc(new_length * sizeof(int));
               memcpy(star_fds, new_fds, new_length * sizeof(int));
               star_length = new_length;
            }
            else
            {
               star_fds = realloc(star_fds, (star_length + new_length) * sizeof(int));
               memcpy(star_fds + star_length, new_fds, new_length * sizeof(int));
               star_length += new_length;
            }

            free(new_fds);
         }
      }

      freeifaddrs(ifaddr);

      if (star_length == 0)
      {
         // cannot bind to anything!
         return 1;
      }

      *fds = star_fds;
      *length = star_length;

      return 0;
   }

   return bind_host(hostname, port, fds, length);
}

/**
 *
 */
int
pgmoneta_bind_unix_socket(const char* directory, const char* file, int* fd)
{
   int status;
   char buf[107];
   struct stat st = {0};
   struct sockaddr_un addr;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if ((*fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
   {
      pgmoneta_log_error("pgmoneta_bind_unix_socket: socket: %s %s", directory, strerror(errno));
      errno = 0;
      goto error;
   }

   memset(&addr, 0, sizeof(addr));
   addr.sun_family = AF_UNIX;

   if (!directory)
   {
      directory = "/tmp/";
   }

   memset(&buf, 0, sizeof(buf));
   snprintf(&buf[0], sizeof(buf), "%s", directory);

   if (stat(&buf[0], &st) == -1)
   {
      status = mkdir(&buf[0], S_IRWXU);
      if (status == -1)
      {
         pgmoneta_log_error("pgmoneta_bind_unix_socket: permission defined for %s (%s)", directory, strerror(errno));
         errno = 0;
         goto error;
      }
   }

   memset(&buf, 0, sizeof(buf));
   snprintf(&buf[0], sizeof(buf), "%s/%s", directory, file);

   strncpy(addr.sun_path, &buf[0], sizeof(addr.sun_path) - 1);
   unlink(&buf[0]);

   if (bind(*fd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
   {
      pgmoneta_log_error("pgmoneta_bind_unix_socket: bind: %s/%s %s", directory, file, strerror(errno));
      errno = 0;
      goto error;
   }

   if (listen(*fd, config->backlog) == -1)
   {
      pgmoneta_log_error("pgmoneta_bind_unix_socket: listen: %s/%s %s", directory, file, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

/**
 *
 */
int
pgmoneta_remove_unix_socket(const char* directory, const char* file)
{
   char buf[MISC_LENGTH];

   memset(&buf, 0, sizeof(buf));
   snprintf(&buf[0], sizeof(buf), "%s/%s", directory, file);

   unlink(&buf[0]);

   return 0;
}

/**
 *
 */
int
pgmoneta_connect(const char* hostname, int port, int* fd)
{
   struct addrinfo hints = {0};
   struct addrinfo* servinfo = NULL;
   struct addrinfo* p = NULL;
   int yes = 1;
   size_t buffer_size = DEFAULT_BUFFER_SIZE;
   socklen_t optlen = sizeof(int);
   int rv;
   char sport[6];
   int error = 0;
   struct configuration* config;

   config = (struct configuration*)shmem;

   memset(&sport, 0, sizeof(sport));
   sprintf(&sport[0], "%d", port);

   /* Connect to server */
   memset(&hints, 0, sizeof hints);
   hints.ai_family = AF_UNSPEC;
   hints.ai_socktype = SOCK_STREAM;

   if ((rv = getaddrinfo(hostname, &sport[0], &hints, &servinfo)) != 0)
   {
      pgmoneta_log_debug("getaddrinfo: %s", gai_strerror(rv));
      if (servinfo != NULL)
      {
         freeaddrinfo(servinfo);
      }
      return 1;
   }

   *fd = -1;

   /* Loop through all the results and connect to the first we can */
   for (p = servinfo; *fd == -1 && p != NULL; p = p->ai_next)
   {
      if ((*fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
      {
         error = errno;
         errno = 0;
      }

      if (*fd != -1)
      {
         if (config != NULL && config->keep_alive)
         {
            if (setsockopt(*fd, SOL_SOCKET, SO_KEEPALIVE, &yes, optlen) == -1)
            {
               error = errno;
               pgmoneta_disconnect(*fd);
               errno = 0;
               *fd = -1;
               continue;
            }
         }

         if (config != NULL && config->nodelay)
         {
            if (setsockopt(*fd, IPPROTO_TCP, TCP_NODELAY, &yes, optlen) == -1)
            {
               error = errno;
               pgmoneta_disconnect(*fd);
               errno = 0;
               *fd = -1;
               continue;
            }
         }

         if (config != NULL)
         {
            if (setsockopt(*fd, SOL_SOCKET, SO_RCVBUF, &buffer_size, optlen) == -1)
            {
               error = errno;
               pgmoneta_disconnect(*fd);
               errno = 0;
               *fd = -1;
               continue;
            }

            if (setsockopt(*fd, SOL_SOCKET, SO_SNDBUF, &buffer_size, optlen) == -1)
            {
               error = errno;
               pgmoneta_disconnect(*fd);
               errno = 0;
               *fd = -1;
               continue;
            }
         }

         if (connect(*fd, p->ai_addr, p->ai_addrlen) == -1)
         {
            error = errno;
            pgmoneta_disconnect(*fd);
            errno = 0;
            *fd = -1;
            continue;
         }
      }
   }

   if (*fd == -1)
   {
      goto error;
   }

   freeaddrinfo(servinfo);

   /* Set O_NONBLOCK on the socket */
   if (config != NULL && config->non_blocking)
   {
      pgmoneta_socket_nonblocking(*fd, true);
   }

   return 0;

error:

   pgmoneta_log_debug("pgmoneta_connect: %s", strerror(error));

   if (servinfo != NULL)
   {
      freeaddrinfo(servinfo);
   }

   return 1;
}

int
pgmoneta_connect_unix_socket(const char* directory, const char* file, int* fd)
{
   char buf[107];
   struct sockaddr_un addr;

   if ((*fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
   {
      pgmoneta_log_warn("pgmoneta_connect_unix_socket: socket: %s %s", directory, strerror(errno));
      goto error;
   }

   memset(&addr, 0, sizeof(addr));
   addr.sun_family = AF_UNIX;

   memset(&buf, 0, sizeof(buf));
   snprintf(&buf[0], sizeof(buf), "%s/%s", directory, file);

   strncpy(addr.sun_path, &buf[0], sizeof(addr.sun_path) - 1);

   if (connect(*fd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
   {
      pgmoneta_log_warn("pgmoneta_connect_unix_socket: %d %s", *fd, strerror(errno));
      goto error;
   }

   return 0;

error:

   errno = 0;

   pgmoneta_disconnect(*fd);

   *fd = -1;

   return 1;
}

bool
pgmoneta_socket_isvalid(int fd)
{
   int error = 0;
   socklen_t length;
   int r;

   r = fcntl(fd, F_GETFL);

   if (r == -1)
   {
      errno = 0;
      return false;
   }

   length = sizeof(error);
   r = getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length);

   if (r != 0 || error != 0)
   {
      errno = 0;
      return false;
   }

   return true;
}

/**
 *
 */
int
pgmoneta_disconnect(int fd)
{
   if (fd == -1)
   {
      return 1;
   }

   return close(fd);
}

void*
pgmoneta_get_sockaddr(struct sockaddr* sa)
{
   if (sa->sa_family == AF_INET)
   {
      return &(((struct sockaddr_in*)sa)->sin_addr);
   }

   return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void
pgmoneta_get_address(struct sockaddr* sa, char* address, size_t length)
{
   if (sa->sa_family == AF_INET)
   {
      inet_ntop(AF_INET, &(((struct sockaddr_in*)sa)->sin_addr), address, length);
   }
   else
   {
      inet_ntop(AF_INET6, &(((struct sockaddr_in6*)sa)->sin6_addr), address, length);
   }
}

int
pgmoneta_socket_nonblocking(int fd, bool value)
{
   int flags;

   flags = fcntl(fd, F_GETFL);

   if (value)
   {
      fcntl(fd, F_SETFL, flags | O_NONBLOCK);
   }
   else
   {
      fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
   }

   return 0;
}

bool
pgmoneta_socket_is_nonblocking(int fd)
{
   int flags;

   flags = fcntl(fd, F_GETFL);

   return flags & O_NONBLOCK;
}

int
pgmoneta_socket_has_error(int fd)
{
   int error = 0;
   socklen_t length = sizeof(int);

   if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == -1)
   {
      pgmoneta_log_trace("error getting socket error code: %s (%d)", strerror(errno), fd);
      errno = 0;
      goto error;
   }

   if (error != 0)
   {
      pgmoneta_log_trace("socket error: %s (%d)", strerror(error), fd);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_tcp_nodelay(int fd)
{
   struct configuration* config;
   int yes = 1;
   socklen_t optlen = sizeof(int);

   config = (struct configuration*)shmem;

   if (config->nodelay)
   {
      if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, optlen) == -1)
      {
         pgmoneta_log_warn("tcp_nodelay: %d %s", fd, strerror(errno));
         errno = 0;
         return 1;
      }
   }

   return 0;
}

int
pgmoneta_socket_buffers(int fd)
{
   socklen_t optlen = sizeof(int);
   size_t buffer_size = DEFAULT_BUFFER_SIZE;

   if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buffer_size, optlen) == -1)
   {
      pgmoneta_log_warn("socket_buffers: SO_RCVBUF %d %s", fd, strerror(errno));
      errno = 0;
      return 1;
   }

   if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer_size, optlen) == -1)
   {
      pgmoneta_log_warn("socket_buffers: SO_SNDBUF %d %s", fd, strerror(errno));
      errno = 0;
      return 1;
   }

   return 0;
}

/**
 *
 */
static int
bind_host(const char* hostname, int port, int** fds, int* length)
{
   int* result = NULL;
   int index, size;
   int sockfd;
   struct addrinfo hints, * servinfo, * addr;
   int yes = 1;
   int rv;
   char* sport;
   struct configuration* config;

   config = (struct configuration*)shmem;

   index = 0;
   size = 0;

   sport = malloc(6);

   if (sport == NULL)
   {
      goto error;
   }

   memset(sport, 0, 6);
   sprintf(sport, "%d", port);

   /* Find all SOCK_STREAM addresses */
   memset(&hints, 0, sizeof hints);
   hints.ai_family = AF_UNSPEC;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_flags = AI_PASSIVE;

   if ((rv = getaddrinfo(hostname, sport, &hints, &servinfo)) != 0)
   {
      free(sport);
      pgmoneta_log_error("getaddrinfo: %s:%d (%s)", hostname, port, gai_strerror(rv));
      return 1;
   }

   free(sport);

   for (addr = servinfo; addr != NULL; addr = addr->ai_next)
   {
      size++;
   }

   result = malloc(size * sizeof(int));

   if (result == NULL)
   {
      goto error;
   }

   memset(result, 0, size * sizeof(int));

   /* Loop through all the results and bind to the first we can */
   for (addr = servinfo; addr != NULL; addr = addr->ai_next)
   {
      if ((sockfd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol)) == -1)
      {
         pgmoneta_log_debug("server: socket: %s:%d (%s)", hostname, port, strerror(errno));
         continue;
      }

      if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
      {
         pgmoneta_log_debug("server: so_reuseaddr: %d %s", sockfd, strerror(errno));
         pgmoneta_disconnect(sockfd);
         continue;
      }

      if (config->non_blocking)
      {
         if (pgmoneta_socket_nonblocking(sockfd, true))
         {
            pgmoneta_disconnect(sockfd);
            continue;
         }
      }

      if (pgmoneta_socket_buffers(sockfd))
      {
         pgmoneta_disconnect(sockfd);
         continue;
      }

      if (pgmoneta_tcp_nodelay(sockfd))
      {
         pgmoneta_disconnect(sockfd);
         continue;
      }

      if (bind(sockfd, addr->ai_addr, addr->ai_addrlen) == -1)
      {
         pgmoneta_disconnect(sockfd);
         pgmoneta_log_debug("server: bind: %s:%d (%s)", hostname, port, strerror(errno));
         continue;
      }

      if (listen(sockfd, config->backlog) == -1)
      {
         pgmoneta_disconnect(sockfd);
         pgmoneta_log_debug("server: listen: %s:%d (%s)", hostname, port, strerror(errno));
         continue;
      }

      *(result + index) = sockfd;
      index++;
   }

   freeaddrinfo(servinfo);

   if (index == 0)
   {
      free(result);
      return 1;
   }

   *fds = result;
   *length = index;

   return 0;

error:

   return 1;
}

int
pgmoneta_get_network_max_rate(int server)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->servers[server].network_max_rate != -1)
   {
      return config->servers[server].network_max_rate;
   }

   return config->network_max_rate;
}
