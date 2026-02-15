-- Copyright (C) 2026 The pgmoneta community
--
-- Redistribution and use in source and binary forms, with or without modification,
-- are permitted provided that the following conditions are met:
--
-- 1. Redistributions of source code must retain the above copyright notice, this list
-- of conditions and the following disclaimer.
--
-- 2. Redistributions in binary form must reproduce the above copyright notice, this
-- list of conditions and the following disclaimer in the documentation and/or other
-- materials provided with the distribution.
--
-- 3. Neither the name of the copyright holder nor the names of its contributors may
-- be used to endorse or promote products derived from this software without specific
-- prior written permission.
--
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
-- EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
-- OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
-- THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
-- SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
-- OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
-- HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
-- TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
-- SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

CREATE ROLE PG_USER_NAME WITH LOGIN PASSWORD 'PG_USER_PASSWORD';
CREATE DATABASE PG_DATABASE WITH OWNER PG_USER_NAME TEMPLATE template0 ENCODING UTF8;

-- create a replication role
CREATE ROLE PG_REPL_USER_NAME WITH LOGIN REPLICATION PASSWORD 'PG_REPL_PASSWORD';

-- create extension
DROP EXTENSION IF EXISTS pgmoneta_ext;
CREATE EXTENSION pgmoneta_ext;

GRANT EXECUTE ON FUNCTION pgmoneta_ext_get_file(text) TO PG_REPL_USER_NAME;
GRANT EXECUTE ON FUNCTION pgmoneta_ext_get_files(text) TO PG_REPL_USER_NAME;

-- GRANT REPL_USER priviledge to fetch file from server
GRANT pg_read_server_files TO PG_REPL_USER_NAME;

-- GRANT REPL_USER privilege to perform CHECKPOINT and pg_switch_wal
GRANT pg_checkpoint TO PG_REPL_USER_NAME;
GRANT EXECUTE ON FUNCTION pg_catalog.pg_switch_wal() TO PG_REPL_USER_NAME;

GRANT EXECUTE ON FUNCTION pg_read_binary_file(text, bigint, bigint, boolean) TO PG_REPL_USER_NAME;
GRANT EXECUTE ON FUNCTION pg_stat_file(text, boolean) TO PG_REPL_USER_NAME;
-- GRANT REPL_USER execute privileges on backup admin functions
GRANT EXECUTE ON FUNCTION pg_backup_start(text, boolean) TO PG_REPL_USER_NAME;
GRANT EXECUTE ON FUNCTION pg_backup_stop(boolean) TO PG_REPL_USER_NAME;
