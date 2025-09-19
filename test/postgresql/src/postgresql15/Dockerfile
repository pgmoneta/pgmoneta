 # Copyright (C) 2025 The pgmoneta community
 #
 # Redistribution and use in source and binary forms, with or without modification,
 # are permitted provided that the following conditions are met:
 #
 # 1. Redistributions of source code must retain the above copyright notice, this list
 # of conditions and the following disclaimer.
 #
 # 2. Redistributions in binary form must reproduce the above copyright notice, this
 # list of conditions and the following disclaimer in the documentation and/or other
 # materials provided with the distribution.
 #
 # 3. Neither the name of the copyright holder nor the names of its contributors may
 # be used to endorse or promote products derived from this software without specific
 # prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 # OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 # THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 # SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 # OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 # HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 # TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 # SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

FROM rockylinux:9

LABEL maintainer="Jesper Pedersen <jesperpedersen.db@gmail.com>"

LABEL summary="PostgreSQL 15" \
      description="PostgreSQL 15"

ENV PGVERSION="15"
ENV PGROOT="/usr/pgsql-${PGVERSION}"
ENV PATH="/usr/pgsql-${PGVERSION}/bin:$PATH"
ENV EXTENSION_NAME="pgmoneta_ext"
ENV GITHUB_REPO="https://github.com/pgmoneta/pgmoneta_ext.git"

# Base installation from the template
RUN rpm -Uvh https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm
RUN dnf -y install --nogpgcheck --allowerasing https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-x86_64/pgdg-redhat-repo-latest.noarch.rpm
RUN dnf -qy --nogpgcheck module disable postgresql
RUN dnf -y upgrade
RUN dnf install -y dnf-plugins-core
RUN dnf config-manager --set-enabled crb
RUN dnf makecache

# Install dependencies
RUN dnf -y install git gcc cmake make postgresql15-devel
RUN dnf -y install procps wget iputils net-tools
RUN dnf -y install postgresql15 postgresql15-server postgresql15-contrib postgresql15-libs 

# Build and Install pgmoneta_ext
WORKDIR /tmp/pg_extension
RUN git clone --depth 1 ${GITHUB_REPO}
WORKDIR /tmp/pg_extension/${EXTENSION_NAME}

RUN mkdir build
WORKDIR /tmp/pg_extension/${EXTENSION_NAME}/build
RUN cmake -DDOCS=false ..
RUN make
RUN make install

RUN rm -rf /tmp/pg_extension
RUN dnf --nogpgcheck clean all

WORKDIR /
RUN rm -Rf /conf /pgconf /pgdata /pgwal
COPY root/ /
RUN mkdir -p /conf /pgconf /pgdata /pgwal /pglog
COPY conf/* /conf/
RUN chown -R postgres:postgres /conf /pgconf /pgdata /pgwal /pglog
RUN chmod 700 /conf /pgconf /pgdata /pgwal
RUN chmod +x /usr/bin/run-postgresql
RUN mkdir -p /usr/local/bin

EXPOSE 5432

USER 26

CMD ["/usr/bin/run-postgresql"]