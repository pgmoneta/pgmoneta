#!/usr/bin/env bash
#
# Copyright (C) 2026 The pgmoneta community
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
#
# End-to-end harness for pgmoneta-walbridge: PostgreSQL 18 primary ->
# walbridge -> PostgreSQL 19 client.  See test/walbridge/README.md.
#
#   run.sh [-o DIR] [-k] [-l] [WORKLOAD | stream ...]
#
set -uo pipefail

# Known failures on the current code, one line per issue and check.
#
#   workload/check | reason | issue | kind      (fields separated by " | ")
#
# workload/check is a bash extglob pattern matched against
# "<workload>/<check>". kind is one of:
#   xfail  fails every run: reports XFAIL. A pass reports XPASS, which fails
#          the run (see WB_E2E_STRICT_XPASS): remove the line with the fix.
#   flaky  a race in walbridge or the WAL client decides the outcome:
#          reports FLAKY-PASS or FLAKY-FAIL and never fails the run.
EXPECTED_FAILURES=(
   "*/live_tail_delivered_without_switch | a record in the upstream segment that is still being written is not sent until that segment is complete: the WAL client pre-sizes the .partial file, and the sender re-reads a .partial only when its size changes | #1714 | xfail"
   "*/identify_system_sysid_no_plain_segment | IDENTIFY_SYSTEM reads the system identifier from the oldest raw segment with a plain 24-character file name; once every sealed segment is compressed or encrypted there is none, and it reports 0 | #1715 | xfail"
   "checkpoint/checkpoint_redo_lsn_translated | the redo pointer inside checkpoint records is copied unchanged, so downstream checkpoints point at upstream LSNs | #1716 | xfail"
   "@(crash|crash_repeat)/overwrite_contrecord_not_forwarded | XLOG_OVERWRITE_CONTRECORD is forwarded unchanged; it names the upstream LSN of the aborted record, which does not exist in the downstream stream | #1717 | xfail"
   "bigmsg/@(stream_reaches_end_marker|waldump_clean|record_sequence_matches_upstream|big_logical_message_intact) | the encoder rejects a record larger than one downstream segment and retries it forever, so the stream stops | #1709 | xfail"
   "crash/record_sequence_matches_upstream | after an immediate stop and restart of the primary, records written before the stop in the segment that held the torn record can be missing downstream while the stream goes on | #1707 | flaky"
   # flaky until #1707/#1708 are fixed; the crash fix PR makes these required.
   # When the loss is silent (#1707) the stream still reaches the end marker
   # and pg_waldump reads it cleanly, so these checks can pass.
   "crash_repeat/@(stream_reaches_end_marker|waldump_clean|record_sequence_matches_upstream) | after several torn crashes in a row the raw archive has missing segments and the sender stops with \"pruned or missing\"; when the loss is silent the stream goes on | #1708 | flaky"
   "@(compress_gzip|compress_lz4)/@(stream_reaches_end_marker|waldump_clean|record_sequence_matches_upstream) | the sender only decompresses .zstd raw segments, so the stream stops at the first sealed .gz or .lz4 segment | #1710 | xfail"
   "compress_bzip2/@(stream_reaches_end_marker|waldump_clean|record_sequence_matches_upstream) | the sender cannot read .bz2 raw segments; the stream only gets through when the slow bzip2 compression has not reached a segment yet (or skipped it, see #1713) by the time the sender reads it | #1710 | flaky"
   "@(encrypt_aes|compress_zstd_aes)/@(stream_reaches_end_marker|waldump_clean|record_sequence_matches_upstream) | the sender never decrypts .aes raw segments, so the stream stops at the first encrypted segment | #1711 | xfail"
   "@(compress_*|encrypt_*)/lsnmap_not_compressed | the WAL client's startup scan compresses and encrypts every file in the raw WAL directory, walbridge.lsnmap included | #1712 | xfail"
   "@(compress_*|encrypt_*)/raw_archive_fully_processed | concurrent compress/encrypt children compete for the WAL repository lock and give up, leaving sealed segments unprocessed | #1713 | flaky"
)

# ERROR/FATAL lines in walbridge.log that belong to a known issue. They do
# not fail walbridge_log_no_errors, which counts them in its detail.
#
#   workload pattern | extended regex | issue ("-": expected behaviour)
#
# "-@restart" lines are expected only while the primary restarts: their
# timestamp must fall inside a window recorded by wl_crash (from the
# immediate stop until shortly after pg_ctl start returns). The WAL client
# logs "Authentication failed" when its startup packet is answered with
# 57P03 "the database system is starting up"; the same line outside a
# restart window is a real authentication failure and fails the check.
KNOWN_LOG_ERRORS=(
   "* | Invalid PostgreSQL WAL magic number: 0x0000 in file .*\.partial | #1714"
   "* | Could not parse WAL file | #1714"
   "* | Did not get WAL repository lock | #1713"
   "bigmsg | is larger than a downstream segment | #1709"
   "encrypt_aes | pgmoneta_compress: no (suffix|compression callback) found for type 0 | #1711"
   "@(compress_*|encrypt_*) | walbridge\.lsnmap | #1712"
   "crash* | Connection closed by server primary|the database system is (starting up|shutting down|not yet accepting connections)|Authentication failed for user .* on primary | -@restart"
)

shopt -s extglob

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

PG18BIN="${PG18BIN:-}"
PG19BIN="${PG19BIN:-}"
PGMONETA_BUILD="${PGMONETA_BUILD:-$PROJECT_DIR/build}"
TIMEOUT="${WB_E2E_TIMEOUT:-120}"
LIVE_TAIL_WAIT="${WB_E2E_LIVE_TAIL_WAIT:-10}"
STRICT_XPASS="${WB_E2E_STRICT_XPASS:-1}"

STREAM_WORKLOADS=(heap multixact gist checkpoint bigmsg switch crash crash_repeat reconnect seg1mb
                  compress_gzip compress_zstd compress_lz4 compress_bzip2
                  encrypt_aes compress_zstd_aes)
REPL_USER=walbridge
SLOT=walbridge

ARTIFACTS=""
KEEP_DATA=0
WORK=""
BIN=""
SOCK=""
RESULTS_TSV=""
USED_PORTS=()

# ---------------------------------------------------------------- utilities

log()
{
   local line
   line="$(date '+%T') $*"
   printf '%s\n' "$line" >&2
   if [ -n "$ARTIFACTS" ] && [ -d "$ARTIFACTS" ]; then
      printf '%s\n' "$line" >> "$ARTIFACTS/run.log"
   fi
}

die()
{
   log "FATAL: $*"
   exit 2
}

usage()
{
   cat <<EOF
Usage: $(basename "$0") [-o DIR] [-k] [-l] [WORKLOAD | stream ...]

  stream    every workload (the default)
Workloads:  ${STREAM_WORKLOADS[*]}

  -o DIR    artifact directory (default: <temporary work dir>/artifacts)
  -k        keep the data directories of every workload
  -l        list workloads and exit
  -h        this help

Environment: PG18BIN PG19BIN PGMONETA_BUILD WB_E2E_TIMEOUT
             WB_E2E_LIVE_TAIL_WAIT WB_E2E_STRICT_XPASS

PG18BIN and PG19BIN are the bin directories of PostgreSQL 18 and 19. When
one is not set, the first directory on PATH whose postgres reports that
major version is used.
EOF
}

now_s()
{
   date +%s
}

# lsn_to_int "16/B374D848" -> decimal
lsn_to_int()
{
   local hi=${1%/*}
   local lo=${1#*/}
   printf '%d\n' "$(( (0x$hi << 32) + 0x$lo ))"
}

# cleanup registry: one action per line, shared with subshells
register()
{
   printf '%s\n' "$*" >> "$WORK/cleanup.list"
}

cleanup()
{
   local kind a b
   [ -n "$WORK" ] && [ -f "$WORK/cleanup.list" ] || return 0
   while IFS='|' read -r kind a b; do
      case "$kind" in
         pid) kill -9 "$a" 2> /dev/null ;;
         pkill) pkill -9 -f -- "$a" 2> /dev/null ;;
         pgdata)
            if [ -f "$b/postmaster.pid" ]; then
               "$a/pg_ctl" -D "$b" -m immediate stop > /dev/null 2>&1
            fi
            ;;
      esac
   done < <(tac "$WORK/cleanup.list")
   : > "$WORK/cleanup.list"
}

# pick_port VAR: a free TCP port in 20000-59999, distinct from the others
pick_port()
{
   local p i
   for i in $(seq 1 500); do
      p=$(( 20000 + (RANDOM * 32768 + RANDOM) % 40000 ))
      case " ${USED_PORTS[*]-} " in *" $p "*) continue ;; esac
      if listening "$p"; then
         continue
      fi
      if [ -e "$SOCK/.s.PGSQL.$p" ]; then
         continue
      fi
      USED_PORTS+=("$p")
      printf -v "$1" '%s' "$p"
      return 0
   done
   die "no free port found"
}

# listening PORT: something listens on TCP PORT
listening()
{
   [ -n "$(ss -Hltn "sport = :$1" 2> /dev/null)" ]
}

rand_token()
{
   od -An -N6 -tx1 /dev/urandom | tr -d ' \n'
}

# ---------------------------------------------------------------- results

CUR_WL=""
DETAIL=""

record()
{
   # record STATUS NAME DETAIL
   local d=${3//$'\n'/ }
   d=${d//$'\t'/ }
   printf '%s\t%s\t%s\t%s\n' "$CUR_WL" "$2" "$1" "${d:0:600}" >> "$RESULTS_TSV"
   log "$(printf '  %-10s %-17s %-40s %s' "$1" "$CUR_WL" "$2" "${d:0:200}")"
}

# expected_failure NAME: "<workload>/NAME" matches EXPECTED_FAILURES;
# sets EF_ISSUE and EF_KIND
expected_failure()
{
   local e pat rest
   for e in "${EXPECTED_FAILURES[@]}"; do
      pat=${e%% | *}
      # shellcheck disable=SC2053
      if [[ "$CUR_WL/$1" == $pat ]]; then
         rest=${e% | *}
         EF_ISSUE=${rest##* | }
         EF_KIND=${e##* | }
         return 0
      fi
   done
   return 1
}

# check NAME FUNC [ARGS...]: the check passes when FUNC returns 0.
# FUNC may set DETAIL to explain the outcome.
check()
{
   local name=$1 ok=0
   shift
   DETAIL=""
   "$@" && ok=1
   if expected_failure "$name"; then
      if [ "$EF_KIND" = flaky ]; then
         if [ "$ok" = 1 ]; then
            record FLAKY-PASS "$name" "($EF_ISSUE) $DETAIL"
         else
            record FLAKY-FAIL "$name" "($EF_ISSUE) $DETAIL"
         fi
      elif [ "$ok" = 1 ]; then
         record XPASS "$name" "expected to fail ($EF_ISSUE) but passed: remove it from EXPECTED_FAILURES. $DETAIL"
      else
         record XFAIL "$name" "($EF_ISSUE) $DETAIL"
      fi
   elif [ "$ok" = 1 ]; then
      record PASS "$name" "$DETAIL"
   else
      record FAIL "$name" "$DETAIL"
   fi
}

skip()
{
   record SKIP "$1" "$2"
}

# ---------------------------------------------------------------- PostgreSQL

# Per-workload state
P18D=""
P18_PORT=""
WB_PORT=""
WB_CONF=""
WB_BASE=""
WB_RAW=""
RECV=""
LOGS=""
WL_ART=""
REPL_PASS=""
RECV_PID=""
RECV_N=0
TOK=""
START_UP=""
END_UP=""

q18()
{
   "$PG18BIN/psql" -X -q -v ON_ERROR_STOP=1 -h "$SOCK" -p "$P18_PORT" -U postgres \
      -d "${2:-postgres}" -tAc "$1"
}

p18()
{
   "$PG18BIN/psql" -X -q -v ON_ERROR_STOP=1 -h "$SOCK" -p "$P18_PORT" -U postgres "$@"
}

# replication-protocol query against walbridge (simple query protocol)
qwb()
{
   PGPASSWORD="$REPL_PASS" "$PG19BIN/psql" -X -tA \
      "host=127.0.0.1 port=$WB_PORT user=$REPL_USER dbname=replication replication=true connect_timeout=10" \
      -c "$1"
}

start_primary()
{
   # start_primary [WAL segment size in MB]
   local segsize=${1:-}
   P18D="$WORK/data/$WL_ID/pg18"
   mkdir -p "$(dirname "$P18D")"
   "$PG18BIN/initdb" -D "$P18D" -U postgres --auth-local=trust --auth-host=scram-sha-256 \
      -E UTF8 --locale=C.UTF-8 ${segsize:+--wal-segsize=$segsize} > "$LOGS/initdb18.log" 2>&1 ||
      return 1
   cat >> "$P18D/postgresql.conf" <<EOF
# --- walbridge e2e ---
listen_addresses = '127.0.0.1'
unix_socket_directories = '$SOCK'
port = $P18_PORT
wal_level = replica
max_wal_senders = 10
shared_buffers = 128MB
max_wal_size = 2GB
checkpoint_timeout = 30min
autovacuum = off
log_line_prefix = '%m [%p] %b %a: '
log_checkpoints = on
EOF
   echo "host replication $REPL_USER 127.0.0.1/32 scram-sha-256" >> "$P18D/pg_hba.conf"
   register "pgdata|$PG18BIN|$P18D"
   "$PG18BIN/pg_ctl" -D "$P18D" -l "$LOGS/pg18.log" -w -t 120 start > /dev/null || return 1
   q18 "CREATE ROLE $REPL_USER LOGIN REPLICATION PASSWORD '$REPL_PASS'" || return 1
   q18 "SELECT pg_create_physical_replication_slot('$SLOT', true)" > /dev/null || return 1
}

start_walbridge()
{
   # start_walbridge COMPRESSION ENCRYPTION
   local comp=${1:-none} enc=${2:-none}
   WB_CONF="$WORK/data/$WL_ID/etc"
   WB_BASE="$WORK/data/$WL_ID/pgmoneta"
   WB_RAW="$WB_BASE/primary/wal"
   mkdir -p "$WB_CONF" "$WB_BASE"
   cat > "$WB_CONF/pgmoneta.conf" <<EOF
[pgmoneta]
host = 127.0.0.1
metrics = 0
base_dir = $WB_BASE
compression = $comp
encryption = $enc
retention = 7
log_type = file
log_level = info
log_path = $LOGS/walbridge.log
unix_socket_dir = $SOCK

[pgmoneta-walbridge]
host = 127.0.0.1
port = $WB_PORT

[primary]
host = 127.0.0.1
port = $P18_PORT
user = $REPL_USER
wal_slot = $SLOT
EOF
   HOME="$WORK/home" LD_LIBRARY_PATH="$BIN" "$BIN/pgmoneta-admin" -f "$WB_CONF/pgmoneta_users.conf" \
      -U "$REPL_USER" -P "$REPL_PASS" user add > "$LOGS/admin.log" 2>&1 || return 1
   register "pkill|-c $WB_CONF/pgmoneta.conf"
   launch_walbridge
}

launch_walbridge()
{
   HOME="$WORK/home" LD_LIBRARY_PATH="$BIN" ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" \
      nohup "$BIN/pgmoneta-walbridge" -c "$WB_CONF/pgmoneta.conf" -u "$WB_CONF/pgmoneta_users.conf" \
      >> "$LOGS/walbridge.out" 2>&1 < /dev/null &
   register "pid|$!"
   disown "$!"
   wait_walbridge_ready
}

# restart_walbridge: stop every walbridge process of this workload, wait
# until the slot is released, start it again
restart_walbridge()
{
   local t0
   pkill -TERM -f -- "-c $WB_CONF/pgmoneta.conf" 2> /dev/null
   t0=$(now_s)
   while walbridge_running && [ $(( $(now_s) - t0 )) -lt 30 ]; do
      sleep 0.5
   done
   pkill -9 -f -- "-c $WB_CONF/pgmoneta.conf" 2> /dev/null
   t0=$(now_s)
   while [ "$(q18 "SELECT count(*) FROM pg_replication_slots WHERE slot_name = '$SLOT' AND active")" != 0 ] &&
         [ $(( $(now_s) - t0 )) -lt 30 ]; do
      sleep 0.5
   done
   launch_walbridge
}

walbridge_running()
{
   pgrep -f -- "-c $WB_CONF/pgmoneta.conf" > /dev/null
}

wait_walbridge_ready()
{
   local t0
   t0=$(now_s)
   while [ $(( $(now_s) - t0 )) -lt 60 ]; do
      if [ "$(q18 "SELECT count(*) FROM pg_replication_slots WHERE slot_name = '$SLOT' AND active" 2> /dev/null)" = 1 ] &&
         listening "$WB_PORT"; then
         return 0
      fi
      walbridge_running || return 1
      sleep 0.5
   done
   return 1
}

start_receivewal()
{
   RECV_N=$((RECV_N + 1))
   PGPASSWORD="$REPL_PASS" "$PG19BIN/pg_receivewal" -h 127.0.0.1 -p "$WB_PORT" -U "$REPL_USER" \
      -D "$RECV" -v --no-sync > "$LOGS/pg_receivewal.$RECV_N.log" 2>&1 < /dev/null &
   RECV_PID=$!
   register "pid|$RECV_PID"
   sleep 1
   kill -0 "$RECV_PID" 2> /dev/null
}

stop_receivewal()
{
   # stop_receivewal [SIGNAL]
   local t0
   [ -n "$RECV_PID" ] || return 0
   kill "-${1:-INT}" "$RECV_PID" 2> /dev/null
   # On SIGINT pg_receivewal sends CopyDone and waits for the server's
   # reply. walbridge does not answer CopyDone, so a sender that is not
   # streaming keeps pg_receivewal waiting: give it 15 s, then kill it.
   t0=$(now_s)
   while kill -0 "$RECV_PID" 2> /dev/null && [ $(( $(now_s) - t0 )) -lt 15 ]; do
      sleep 0.5
   done
   if kill -0 "$RECV_PID" 2> /dev/null; then
      log "pg_receivewal did not exit 15 s after SIG${1:-INT}; killed"
      kill -9 "$RECV_PID" 2> /dev/null
   fi
   wait "$RECV_PID" 2> /dev/null
   RECV_PID=""
}

# emit_marker KIND -> prints the upstream end LSN of the marker record
emit_marker()
{
   q18 "SELECT pg_logical_emit_message(false, 'wbe2e:$1:$TOK', 'm')"
}

seal()
{
   q18 "SELECT pg_switch_wal()" > /dev/null
}

# received_has TEXT: TEXT occurs in a received WAL file (fast, byte level)
received_has()
{
   grep -qraF -- "$1" "$RECV" 2> /dev/null
}

# wait_received TEXT SECONDS
wait_received()
{
   local t0
   t0=$(now_s)
   while [ $(( $(now_s) - t0 )) -lt "$2" ]; do
      received_has "$1" && return 0
      sleep 1
   done
   received_has "$1"
}

# ---------------------------------------------------------------- WAL dumps

SCR=""
FIRST_SEG=""

# Hard-link the received WAL into a scratch dir, naming the .partial
# segment like a complete one so that pg_waldump reads it.
prepare_scratch()
{
   local f b
   SCR="$WORK/data/$WL_ID/scratch"
   rm -rf "$SCR"
   mkdir -p "$SCR"
   for f in "$RECV"/*; do
      [ -f "$f" ] || continue
      b=$(basename "$f")
      case "$b" in
         ????????????????????????) ln "$f" "$SCR/$b" ;;
         ????????????????????????.partial)
            [ -e "$RECV/${b%.partial}" ] || ln "$f" "$SCR/${b%.partial}" ;;
      esac
   done
   FIRST_SEG=$(ls "$SCR" 2> /dev/null | grep -E '^[0-9A-F]{24}$' | sort | head -1)
   FIRST_LSN=""
   if [ -n "$FIRST_SEG" ]; then
      local segsz log seg
      segsz=$(stat -c %s "$SCR/$FIRST_SEG")
      log=$((0x${FIRST_SEG:8:8}))
      seg=$((0x${FIRST_SEG:16:8}))
      # pg_waldump treats LSN 0 as "no start location": begin a fresh
      # stream at its first record, 0/28 (after the long page header)
      FIRST_LSN=$(printf '%X/%X' "$(( (log * (0x100000000 / segsz) + seg) * segsz >> 32 ))" \
                  "$(( ((log * (0x100000000 / segsz) + seg) * segsz & 0xFFFFFFFF) + 0x28 ))")
   fi
}

# Dump the received (downstream, PG19) and the primary's (upstream, PG18)
# WAL; reduce both to the record sequence between the start and end markers.
build_dumps()
{
   prepare_scratch
   : > "$WL_ART/waldump.txt"
   : > "$WL_ART/waldump.err"
   if [ -n "$FIRST_SEG" ]; then
      "$PG19BIN/pg_waldump" -p "$SCR" -s "$FIRST_LSN" 2> "$WL_ART/waldump.err" |
         cut -c1-400 > "$WL_ART/waldump.txt"
   else
      echo "no received WAL segment" > "$WL_ART/waldump.err"
   fi
   "$PG18BIN/pg_waldump" -p "$P18D/pg_wal" -s "$START_UP" -e "$END_UP" 2> "$WL_ART/upstream.err" |
      cut -c1-400 > "$WL_ART/upstream.txt"
   normalize up < "$WL_ART/upstream.txt" > "$WL_ART/seq_up.txt"
   normalize down < "$WL_ART/waldump.txt" > "$WL_ART/seq_down.txt"
   diff "$WL_ART/seq_up.txt" "$WL_ART/seq_down.txt" > "$WL_ART/seq.diff"
}

# normalize up|down: "rmgr type" per record strictly between the markers.
# Mirrors walbridge's documented translations: upstream XLOG SWITCH and
# Gist ASSIGN_LSN are dropped, Heap2 VISIBLE becomes Heap2
# PRUNE_VACUUM_CLEANUP (migration_engine.c).  SWITCH is ignored on both
# sides because the downstream store places its own segment ends.
normalize()
{
   awk -v side="$1" -v s="prefix \"wbe2e:start:$TOK\"" -v e="prefix \"wbe2e:end:$TOK\"" '
      /^rmgr: / {
         if (index($0, s)) { on = 1; next }
         if (index($0, e)) { exit }
         if (!on) next
         rm = $2; t = $0; sub(/.*desc: /, "", t); split(t, a, " "); ty = a[1]
         if (rm == "XLOG" && ty == "SWITCH") next
         if (side == "up" && rm == "Gist" && ty == "ASSIGN_LSN") next
         if (side == "up" && rm == "Heap2" && ty == "VISIBLE") ty = "PRUNE_VACUUM_CLEANUP"
         print rm, ty
      }'
}

# ---------------------------------------------------------------- common checks

chk_end_marker()
{
   if received_has "wbe2e:end:$TOK"; then
      return 0
   fi
   if grep -qF "prefix \"wbe2e:end:$TOK\"" "$WL_ART/waldump.txt" 2> /dev/null; then
      return 0
   fi
   DETAIL="end marker not received within ${TIMEOUT}s; walbridge: $(grep -E 'ERROR|FATAL|WARN' "$LOGS/walbridge.log" 2> /dev/null | tail -2 | cut -c1-220 | tr '\n' ' ')"
   return 1
}

chk_waldump_clean()
{
   local err end_line elsn
   err=$(grep -v '^pg_waldump: first record is after' "$WL_ART/waldump.err" | head -3)
   end_line=$(grep -nF "prefix \"wbe2e:end:$TOK\"" "$WL_ART/waldump.txt" | head -1 | cut -d: -f1)
   if [ -z "$end_line" ]; then
      DETAIL="end marker not in pg_waldump output; ${err:0:300}"
      return 1
   fi
   if [ -z "$err" ]; then
      return 0
   fi
   # only an ordinary end of WAL after the end marker is acceptable
   if echo "$err" | grep -qE 'invalid record length at .*got 0$|invalid magic number 0000 |could not find file'; then
      elsn=$(echo "$err" | sed -nE 's/.*error in WAL record at ([0-9A-F]+\/[0-9A-F]+).*/\1/p' | head -1)
      if [ -z "$elsn" ] || [ "$(lsn_to_int "$elsn")" -ge "$(lsn_to_int "$(sed -n "${end_line}p" "$WL_ART/waldump.txt" | sed -nE 's/.*lsn: ([0-9A-F]+\/[0-9A-F]+).*/\1/p')")" ]; then
         return 0
      fi
   fi
   DETAIL="${err:0:400}"
   return 1
}

chk_sequence()
{
   local nu nd
   nu=$(wc -l < "$WL_ART/seq_up.txt")
   nd=$(wc -l < "$WL_ART/seq_down.txt")
   if [ "$nu" -eq 0 ]; then
      DETAIL="no upstream records between the markers: $(head -c 200 "$WL_ART/upstream.err")"
      return 1
   fi
   if [ -s "$WL_ART/seq.diff" ]; then
      DETAIL="upstream $nu records, downstream $nd; first differences: $(grep '^[<>]' "$WL_ART/seq.diff" | head -4 | tr '\n' ';')"
      return 1
   fi
   DETAIL="$nu records"
}

chk_walbridge_alive()
{
   walbridge_running && return 0
   DETAIL="walbridge exited: $(tail -3 "$LOGS/walbridge.out" 2> /dev/null | tr '\n' ' ')"
   return 1
}

chk_sanitizer()
{
   local hits
   hits=$(grep -hE 'ERROR: (Address|Leak|UndefinedBehavior)Sanitizer|runtime error:' "$LOGS/walbridge.out" 2> /dev/null | head -3)
   [ -z "$hits" ] && return 0
   DETAIL="$hits"
   return 1
}

# known_log_issue LINE: LINE matches KNOWN_LOG_ERRORS for this workload;
# prints the issue
known_log_issue()
{
   local e wl rest re issue
   for e in "${KNOWN_LOG_ERRORS[@]}"; do
      wl=${e%% | *}
      rest=${e#* | }
      re=${rest% | *}
      issue=${e##* | }
      # shellcheck disable=SC2053
      if [[ "$CUR_WL" == $wl ]] && [[ "$1" =~ $re ]]; then
         if [ "$issue" = "-@restart" ]; then
            in_restart_window "$1" || continue
            issue="-(restart)"
         fi
         echo "$issue"
         return 0
      fi
   done
   return 1
}

# RESTART_WINDOWS: "from to" epoch seconds, one per primary restart
RESTART_WINDOWS=()
RESTART_GRACE=10

# in_restart_window LINE: LINE's "YYYY-MM-DD HH:MM:SS" timestamp falls inside
# a recorded primary restart window
in_restart_window()
{
   local ts w
   ts=$(date -d "${1:0:19}" +%s 2> /dev/null) || return 1
   for w in "${RESTART_WINDOWS[@]}"; do
      [ "$ts" -ge "${w% *}" ] && [ "$ts" -le "${w#* }" ] && return 0
   done
   return 1
}

chk_walbridge_log()
{
   local line issue unknown="" counts
   counts=$(grep -E ' (ERROR|FATAL) ' "$LOGS/walbridge.log" 2> /dev/null |
      while IFS= read -r line; do
         if issue=$(known_log_issue "$line"); then
            echo "known $issue"
         else
            echo "unknown ${line:0:250}"
         fi
      done)
   unknown=$(printf '%s\n' "$counts" | sed -n 's/^unknown //p' | head -3)
   DETAIL="known lines: $(printf '%s\n' "$counts" | sed -n 's/^known //p' | sort | uniq -c | awk '{ printf "%s x%s ", $2, $1 }')"
   [ -z "$unknown" ] && return 0
   DETAIL="$unknown"
   return 1
}

chk_upstream_has()
{
   # chk_upstream_has "rmgr type" ...
   local want missing=""
   for want in "$@"; do
      grep -qx -- "$want" "$WL_ART/seq_up_raw.txt" || missing="$missing $want;"
   done
   [ -z "$missing" ] && return 0
   DETAIL="workload did not produce:$missing"
   return 1
}

chk_identify_system()
{
   local row sysid hdr
   row=$(qwb "IDENTIFY_SYSTEM" 2>&1 | head -1)
   sysid=${row%%|*}
   hdr=$(od -An -t u8 -j 24 -N 8 "$SCR/$FIRST_SEG" 2> /dev/null | tr -d ' ')
   DETAIL="IDENTIFY_SYSTEM systemid=$sysid, downstream page header sysid=${hdr:-?}, PG18 sysid=$(q18 'SELECT system_identifier FROM pg_control_system()')"
   [[ "$sysid" =~ ^[0-9]+$ ]] && [ "$sysid" != 0 ] && [ "$sysid" = "$hdr" ]
}

common_checks()
{
   # upstream record types without the translation mapping, for preconditions
   awk -v s="prefix \"wbe2e:start:$TOK\"" -v e="prefix \"wbe2e:end:$TOK\"" '
      /^rmgr: / { if (index($0, s)) { on = 1; next } if (index($0, e)) exit
                  if (!on) next; t = $0; sub(/.*desc: /, "", t); split(t, a, " "); print $2, a[1] }' \
      "$WL_ART/upstream.txt" | sort -u > "$WL_ART/seq_up_raw.txt"

   check stream_reaches_end_marker chk_end_marker
   check waldump_clean chk_waldump_clean
   check record_sequence_matches_upstream chk_sequence
   check walbridge_alive chk_walbridge_alive
   check walbridge_sanitizer_clean chk_sanitizer
   check walbridge_log_no_errors chk_walbridge_log
   # sender_get_stream_info() reads the system ID from the oldest raw file
   # with a 24-character name: a separate check name covers the case where
   # every sealed segment is compressed or encrypted
   if [ -z "$(ls "$WB_RAW" 2> /dev/null | grep -E '^[0-9A-F]{24}$')" ]; then
      check identify_system_sysid_no_plain_segment chk_identify_system
   else
      check identify_system_sysid chk_identify_system
   fi
}

# ---------------------------------------------------------------- workloads
#
# wl_<name> runs on the primary between the start and end markers.
# post_<name> (optional) adds assertions once the dumps exist.
# cfg_<name> (optional) sets WB_SEGSIZE / WB_COMPRESSION / WB_ENCRYPTION.

chk_live_tail()
{
   emit_marker live > /dev/null
   if wait_received "wbe2e:live:$TOK" "$LIVE_TAIL_WAIT"; then
      return 0
   fi
   DETAIL="record in the growing upstream segment not delivered within ${LIVE_TAIL_WAIT}s (sender skips a .partial whose size is unchanged; the WAL client pre-sizes it)"
   return 1
}

wl_heap()
{
   p18 -d postgres <<'SQL' > /dev/null || return 1
CREATE TABLE wb_heap (id int PRIMARY KEY, v text) WITH (autovacuum_enabled = off);
INSERT INTO wb_heap SELECT g, md5(g::text) FROM generate_series(1, 5000) g;
UPDATE wb_heap SET v = v || 'u' WHERE id % 2 = 0;
UPDATE wb_heap SET id = id + 100000 WHERE id % 7 = 0;
DELETE FROM wb_heap WHERE id % 3 = 0;
VACUUM wb_heap;
DELETE FROM wb_heap WHERE id % 5 = 0;
VACUUM (FREEZE) wb_heap;
SQL
   # live tail: no segment switch has happened yet
   check live_tail_delivered_without_switch chk_live_tail
}

post_heap()
{
   check heap_workload_generated chk_upstream_has "Heap INSERT" "Heap DELETE" "Heap HOT_UPDATE" \
      "Heap UPDATE" "Heap2 PRUNE_VACUUM_SCAN" "Heap2 VISIBLE"
}

wl_multixact()
{
   local i pids=()
   q18 "CREATE TABLE wb_mx (id int PRIMARY KEY, v text); INSERT INTO wb_mx VALUES (1, 'a'), (2, 'b')" || return 1
   for i in 1 2 3; do
      p18 -d postgres -c "BEGIN" -c "SELECT id FROM wb_mx WHERE id = 1 FOR SHARE" \
         -c "SELECT pg_sleep(2)" -c "COMMIT" > /dev/null &
      pids+=("$!")
   done
   sleep 0.5
   p18 -d postgres -c "BEGIN" -c "SELECT id FROM wb_mx WHERE id = 2 FOR KEY SHARE" \
      -c "SELECT pg_sleep(1)" -c "COMMIT" > /dev/null &
   pids+=("$!")
   p18 -d postgres -c "BEGIN" -c "SELECT id FROM wb_mx WHERE id = 2 FOR SHARE" \
      -c "SELECT pg_sleep(1)" -c "COMMIT" > /dev/null &
   pids+=("$!")
   # only the lockers: pg_receivewal is a background job of this shell too
   wait "${pids[@]}"
   q18 "SELECT xmax FROM wb_mx ORDER BY id" > "$WL_ART/multixact_xmax.txt"
}

chk_multixact_desc()
{
   grep -E '^rmgr: MultiXact .*desc: CREATE_ID' "$WL_ART/upstream.txt" | sed 's/.*desc: //' > "$WL_ART/mx_up.txt"
   grep -E '^rmgr: MultiXact .*desc: CREATE_ID' "$WL_ART/waldump.txt" | sed 's/.*desc: //' > "$WL_ART/mx_down.txt"
   if [ ! -s "$WL_ART/mx_up.txt" ]; then
      DETAIL="no upstream CREATE_ID"
      return 1
   fi
   if cmp -s "$WL_ART/mx_up.txt" "$WL_ART/mx_down.txt"; then
      DETAIL="$(wc -l < "$WL_ART/mx_up.txt") CREATE_ID records"
      return 0
   fi
   DETAIL="up: $(head -1 "$WL_ART/mx_up.txt") / down: $(head -1 "$WL_ART/mx_down.txt")"
   return 1
}

post_multixact()
{
   check multixact_workload_generated chk_upstream_has "MultiXact CREATE_ID" "Heap LOCK"
   check multixact_members_and_offsets_match chk_multixact_desc
}

wl_gist()
{
   p18 -d postgres <<'SQL' > /dev/null || return 1
CREATE TABLE wb_gist (id serial PRIMARY KEY, p point) WITH (autovacuum_enabled = off);
INSERT INTO wb_gist (p) SELECT point(random() * 1000, random() * 1000) FROM generate_series(1, 20000);
CREATE INDEX wb_gist_p ON wb_gist USING gist (p);
INSERT INTO wb_gist (p) SELECT point(random() * 1000, random() * 1000) FROM generate_series(1, 30000);
DELETE FROM wb_gist WHERE id % 2 = 0;
VACUUM wb_gist;
DELETE FROM wb_gist;
VACUUM wb_gist;
INSERT INTO wb_gist (p) SELECT point(random() * 10, random() * 10) FROM generate_series(1, 5000);
SQL
}

post_gist()
{
   check gist_workload_generated chk_upstream_has "Gist PAGE_UPDATE" "Gist PAGE_SPLIT"
}

wl_checkpoint()
{
   q18 "CREATE TABLE wb_ckpt AS SELECT g AS id FROM generate_series(1, 1000) g" || return 1
   q18 "CHECKPOINT" || return 1
   q18 "INSERT INTO wb_ckpt SELECT g FROM generate_series(1, 1000) g" || return 1
   q18 "CHECKPOINT" || return 1
}

# Every online checkpoint's redo pointer must be the downstream LSN of its
# CHECKPOINT_REDO record (PG17+), at or before the checkpoint record itself.
chk_checkpoint_redo()
{
   local bad="" n=0 lsn redo
   awk '/^rmgr: /{ if (match($0, /lsn: [0-9A-F]+\/[0-9A-F]+/)) print substr($0, RSTART + 5, RLENGTH - 5) }' \
      "$WL_ART/waldump.txt" | sort -u > "$WL_ART/down_starts.txt"
   while read -r lsn redo; do
      n=$((n + 1))
      if ! grep -qx -- "$redo" "$WL_ART/down_starts.txt" ||
         [ "$(lsn_to_int "$redo")" -gt "$(lsn_to_int "$lsn")" ]; then
         bad="$bad checkpoint at $lsn has redo $redo;"
      fi
   done < <(grep -E '^rmgr: XLOG .*desc: CHECKPOINT_ONLINE' "$WL_ART/waldump.txt" |
            sed -nE 's/.*lsn: ([0-9A-F]+\/[0-9A-F]+),.*desc: CHECKPOINT_ONLINE redo ([0-9A-F]+\/[0-9A-F]+);.*/\1 \2/p')
   if [ "$n" -eq 0 ]; then
      DETAIL="no CHECKPOINT_ONLINE received"
      return 1
   fi
   DETAIL="${bad:-$n checkpoints, redo pointers translated}"
   [ -z "$bad" ]
}

post_checkpoint()
{
   check checkpoint_workload_generated chk_upstream_has "XLOG CHECKPOINT_ONLINE" "XLOG CHECKPOINT_REDO"
   check checkpoint_redo_lsn_translated chk_checkpoint_redo
}

BIGMSG_BYTES=$((20 * 1024 * 1024))

wl_bigmsg()
{
   q18 "SELECT pg_logical_emit_message(false, 'wbe2e:big:$TOK', repeat('x', $BIGMSG_BYTES))" > /dev/null
}

chk_bigmsg()
{
   local n
   n=$(grep -cF "prefix \"wbe2e:big:$TOK\"; payload ($BIGMSG_BYTES bytes): 78 78 78" "$WL_ART/waldump.txt")
   [ "$n" = 1 ] && return 0
   DETAIL="found $n intact ${BIGMSG_BYTES}-byte messages downstream"
   return 1
}

post_bigmsg()
{
   check big_logical_message_intact chk_bigmsg
}

wl_switch()
{
   local i
   q18 "CREATE TABLE wb_sw (id int)" || return 1
   for i in 1 2 3; do
      q18 "INSERT INTO wb_sw VALUES ($i)" || return 1
      emit_marker "sw$i" > /dev/null
      seal
   done
   seal   # a switch on an empty segment is a no-op upstream
   q18 "INSERT INTO wb_sw VALUES (4)" || return 1
}

chk_after_switches()
{
   local i missing=""
   for i in 1 2 3; do
      grep -qF "prefix \"wbe2e:sw$i:$TOK\"" "$WL_ART/waldump.txt" || missing="$missing sw$i"
   done
   [ -z "$missing" ] && return 0
   DETAIL="markers after a switch not received:$missing"
   return 1
}

post_switch()
{
   check switch_workload_generated chk_upstream_has "XLOG SWITCH"
   check records_after_each_switch_received chk_after_switches
}

CRASH_TORN=0
CRASH_WANT=1

# Immediate stop while a 128 MB record is being written: the flushed WAL
# ends in an incomplete record, and crash recovery on restart writes
# XLOG_OVERWRITE_CONTRECORD (xlog.c, CreateOverwriteContrecordRecord()).
wl_crash()
{
   local attempt before cur bg t0 t_stop
   q18 "CREATE TABLE wb_crash (id int, v text)" || return 1
   for attempt in $(seq 1 $((CRASH_WANT * 3))); do
      q18 "INSERT INTO wb_crash SELECT g, md5(g::text) FROM generate_series(1, 1000) g" || return 1
      before=$(lsn_to_int "$(q18 'SELECT pg_current_wal_flush_lsn()')")
      q18 "SELECT pg_logical_emit_message(false, 'wbe2e:tear:$TOK', repeat('t', 128 * 1024 * 1024))" \
         > /dev/null 2>&1 &
      bg=$!
      t0=$(now_s)
      while [ $(( $(now_s) - t0 )) -lt 30 ]; do
         cur=$(q18 'SELECT pg_current_wal_flush_lsn()' 2> /dev/null) || break
         [ $(( $(lsn_to_int "$cur") - before )) -gt $((24 * 1024 * 1024)) ] && break
         sleep 0.05
      done
      t_stop=$(now_s)
      "$PG18BIN/pg_ctl" -D "$P18D" -m immediate stop > /dev/null 2>&1
      wait "$bg" 2> /dev/null
      "$PG18BIN/pg_ctl" -D "$P18D" -l "$LOGS/pg18.log" -w -t 120 start > /dev/null || return 1
      RESTART_WINDOWS+=("$((t_stop - 1)) $(( $(now_s) + RESTART_GRACE ))")
      "$PG18BIN/pg_waldump" -p "$P18D/pg_wal" -s "$START_UP" --rmgr=XLOG > "$WL_ART/upstream_xlog.txt" 2>&1
      CRASH_TORN=$(grep -c 'desc: OVERWRITE_CONTRECORD' "$WL_ART/upstream_xlog.txt")
      log "crash: attempt $attempt, $CRASH_TORN torn record(s) so far"
      [ "$CRASH_TORN" -ge "$CRASH_WANT" ] && break
   done
   # walbridge's WAL client has to reconnect to the restarted primary
   t0=$(now_s)
   while [ $(( $(now_s) - t0 )) -lt 60 ]; do
      [ "$(q18 "SELECT count(*) FROM pg_replication_slots WHERE slot_name = '$SLOT' AND active")" = 1 ] && break
      sleep 1
   done
   q18 "INSERT INTO wb_crash VALUES (-1, 'after restart')" || return 1
}

chk_crash_torn()
{
   DETAIL="$CRASH_TORN torn record(s), wanted $CRASH_WANT"
   [ "$CRASH_TORN" -ge "$CRASH_WANT" ]
}

chk_upstream_reconnected()
{
   [ "$(q18 "SELECT count(*) FROM pg_replication_slots WHERE slot_name = '$SLOT' AND active")" = 1 ] && return 0
   DETAIL="slot $SLOT not active after the primary restart"
   return 1
}

# walbridge only emits complete records, so a downstream
# OVERWRITE_CONTRECORD (upstream overwritten_lsn) is never valid.
chk_no_downstream_contrecord()
{
   local n
   n=$(grep -c 'desc: OVERWRITE_CONTRECORD' "$WL_ART/waldump.txt")
   [ "$n" = 0 ] && return 0
   DETAIL="$n OVERWRITE_CONTRECORD passed through: $(grep -m1 'desc: OVERWRITE_CONTRECORD' "$WL_ART/waldump.txt" | sed 's/.*desc: //')"
   return 1
}

cfg_crash_repeat()
{
   CRASH_WANT=3
}

wl_crash_repeat()
{
   wl_crash
}

post_crash_repeat()
{
   post_crash
}

post_crash()
{
   check crash_tore_a_record chk_crash_torn
   check walbridge_reconnected_upstream chk_upstream_reconnected
   # a pass is only meaningful when the stream got past the torn record: a
   # stream that stopped earlier and forwarded nothing proves nothing
   if [ "$CRASH_TORN" -lt 1 ]; then
      skip overwrite_contrecord_not_forwarded "no torn record produced"
   elif ! grep -qF "prefix \"wbe2e:end:$TOK\"" "$WL_ART/waldump.txt" &&
        ! grep -q 'desc: OVERWRITE_CONTRECORD' "$WL_ART/waldump.txt"; then
      skip overwrite_contrecord_not_forwarded "the stream did not reach the end marker"
   else
      check overwrite_contrecord_not_forwarded chk_no_downstream_contrecord
   fi
}

RECONNECT_KILLED=0

wl_reconnect()
{
   local t0
   q18 "CREATE TABLE wb_rc (id int, v text)" || return 1
   # > 1 segment, so that walbridge seals and serves something (see live_tail_delivered_without_switch)
   q18 "INSERT INTO wb_rc SELECT g, repeat('r', 200) FROM generate_series(1, 120000) g" || return 1
   t0=$(now_s)
   # Kill only once the newest .partial is fully pre-allocated:
   # pg_receivewal zero-fills a new segment file to its full size, and a
   # file killed half-way ("has N bytes, should be 0 or 16777216") is one
   # it refuses to resume from. That would be the harness's doing.
   local part
   while [ $(( $(now_s) - t0 )) -lt "$TIMEOUT" ]; do
      part=$(ls "$RECV" | grep -E '^[0-9A-F]{24}\.partial$' | sort | tail -1)
      if [ -n "$part" ] &&
         [ "$(stat -c %s "$RECV/$part" 2> /dev/null)" = "$(( ${WB_SEGSIZE:-16} * 1024 * 1024 ))" ] &&
         received_has "wbe2e:start:$TOK"; then
         break
      fi
      sleep 1
   done
   ls -l "$RECV" > "$WL_ART/received_before_kill.ls"
   stop_receivewal KILL
   RECONNECT_KILLED=1
   emit_marker mid > /dev/null
   q18 "INSERT INTO wb_rc SELECT g, repeat('s', 200) FROM generate_series(1, 120000) g" || return 1
   seal
   start_receivewal || return 1
}

chk_reconnect_resumed()
{
   local n
   n=$(grep -cF "prefix \"wbe2e:mid:$TOK\"" "$WL_ART/waldump.txt")
   if [ "$RECONNECT_KILLED" = 1 ] && [ "$n" = 1 ] && grep -q 'starting log streaming' "$LOGS/pg_receivewal.$RECV_N.log"; then
      DETAIL="$(grep -m1 'starting log streaming' "$LOGS/pg_receivewal.$RECV_N.log")"
      return 0
   fi
   DETAIL="killed=$RECONNECT_KILLED, mid marker seen $n times; $(grep -E 'error|could not' "$LOGS/pg_receivewal.$RECV_N.log" | head -2 | tr '\n' ' ')"
   return 1
}

post_reconnect()
{
   check receivewal_resumed_after_kill chk_reconnect_resumed
}

cfg_seg1mb()
{
   WB_SEGSIZE=1
}

wl_seg1mb()
{
   wl_heap_body_small || return 1
   # spans segment boundaries but stays below the 1 MiB segment size:
   # records larger than a downstream segment are covered by bigmsg
   q18 "SELECT pg_logical_emit_message(false, 'wbe2e:mid:$TOK', repeat('y', 600 * 1024))" > /dev/null
   q18 "INSERT INTO wb_small SELECT g, repeat('z', 500) FROM generate_series(10001, 14000) g" > /dev/null
}

wl_heap_body_small()
{
   p18 -d postgres <<'SQL' > /dev/null
CREATE TABLE wb_small (id int PRIMARY KEY, v text) WITH (autovacuum_enabled = off);
INSERT INTO wb_small SELECT g, md5(g::text) FROM generate_series(1, 3000) g;
UPDATE wb_small SET v = v || 'u' WHERE id % 2 = 0;
DELETE FROM wb_small WHERE id % 3 = 0;
VACUUM wb_small;
SQL
}

chk_segsize_reported()
{
   local v
   v=$(qwb "SHOW wal_segment_size" 2>&1 | head -1)
   DETAIL="SHOW wal_segment_size = $v"
   [ "$v" = 1MB ]
}

chk_segsize_files()
{
   local bad
   bad=$(find "$RECV" -maxdepth 1 -type f -regextype posix-extended -regex '.*/[0-9A-F]{24}(\.partial)?' \
      ! -size 1048576c -printf '%f=%s ' | head -c 300)
   DETAIL="$(ls "$RECV" | wc -l) received files${bad:+; wrong size: $bad}"
   [ -z "$bad" ]
}

post_seg1mb()
{
   check seg1mb_wal_segment_size_reported chk_segsize_reported
   check seg1mb_received_segment_size chk_segsize_files
}

# compression / encryption: the WAL client compresses and encrypts every
# sealed raw segment; produce several sealed segments.
wl_archive_body()
{
   q18 "CREATE TABLE wb_arch (id int, v text)" || return 1
   q18 "INSERT INTO wb_arch SELECT g, md5(g::text) FROM generate_series(1, 300000) g" || return 1
   seal
   q18 "UPDATE wb_arch SET v = 'x' WHERE id % 10 = 0" || return 1
}

# sealed_unprocessed: sealed raw segments not yet carrying $WB_SUFFIX
sealed_unprocessed()
{
   ls "$WB_RAW" 2> /dev/null | grep -E '^[0-9A-F]{24}(\..+)?$' | grep -v '\.partial$' |
      grep -vF -- "$WB_SUFFIX" | grep -vE '\.tmp$'
}

# wait_processed SECONDS: until every sealed raw segment is compressed and
# encrypted as configured
wait_processed()
{
   local t0
   t0=$(now_s)
   while [ -n "$(sealed_unprocessed)" ] && [ $(( $(now_s) - t0 )) -lt "$1" ]; do
      sleep 1
   done
   [ -z "$(sealed_unprocessed)" ]
}

chk_raw_fully_processed()
{
   wait_processed 30 && return 0
   DETAIL="sealed segments left without $WB_SUFFIX after 30s: $(sealed_unprocessed | tr '\n' ' ')"
   return 1
}

chk_lsnmap_not_compressed()
{
   local bad
   bad=$(ls "$WB_RAW" 2> /dev/null | grep -E '^walbridge\.lsnmap' | grep -E '\.(gz|zstd|lz4|bz2|aes)$' | tr '\n' ' ')
   DETAIL="${bad:+compressed/encrypted map files: }${bad:-walbridge.lsnmap plain}"
   [ -z "$bad" ]
}

# Compression and encryption run in the background after each segment is
# sealed. Before the archive checks: wait until they are done, then
# restart walbridge, whose WAL client scans and processes the whole raw
# directory on startup, and wait again.
settle_archive()
{
   check raw_archive_fully_processed chk_raw_fully_processed
   if ! restart_walbridge; then
      record FAIL walbridge_restart "walbridge did not come back after a restart: $(tail -2 "$LOGS/walbridge.out")"
      return
   fi
   wait_processed 30
   ls -l "$WB_RAW" > "$WL_ART/raw_archive_after_restart.ls" 2>&1
   check lsnmap_not_compressed chk_lsnmap_not_compressed
}

chk_raw_transformed()
{
   local suffix=$1
   ls -l "$WB_RAW" > "$WL_ART/raw_archive.ls" 2>&1
   if [ -n "$(ls "$WB_RAW" 2> /dev/null | grep -E "^[0-9A-F]{24}.*\\$suffix\$")" ]; then
      DETAIL="$(ls "$WB_RAW" | grep -cE "\\$suffix\$") raw segments end in $suffix"
      return 0
   fi
   DETAIL="no raw segment ends in $suffix: $(ls "$WB_RAW" | tr '\n' ' ' | head -c 200)"
   return 1
}

cfg_compress_gzip()     { WB_COMPRESSION=gzip; WB_SUFFIX=.gz; }
cfg_compress_zstd()     { WB_COMPRESSION=zstd; WB_SUFFIX=.zstd; }
cfg_compress_lz4()      { WB_COMPRESSION=lz4; WB_SUFFIX=.lz4; }
cfg_compress_bzip2()    { WB_COMPRESSION=bzip2; WB_SUFFIX=.bz2; }
cfg_encrypt_aes()       { WB_ENCRYPTION=aes-256-gcm; WB_SUFFIX=.aes; }
cfg_compress_zstd_aes() { WB_COMPRESSION=zstd; WB_ENCRYPTION=aes-256-gcm; WB_SUFFIX=.zstd.aes; }
wl_compress_gzip()      { wl_archive_body; }
wl_compress_zstd()      { wl_archive_body; }
wl_compress_lz4()       { wl_archive_body; }
wl_compress_bzip2()     { wl_archive_body; }
wl_encrypt_aes()        { wl_archive_body; }
wl_compress_zstd_aes()  { wl_archive_body; }
post_compress_gzip()    { check raw_archive_compressed chk_raw_transformed .gz; }
post_compress_zstd()    { check raw_archive_compressed chk_raw_transformed .zstd; }
post_compress_lz4()     { check raw_archive_compressed chk_raw_transformed .lz4; }
post_compress_bzip2()   { check raw_archive_compressed chk_raw_transformed .bz2; }
post_encrypt_aes()      { check raw_archive_encrypted chk_raw_transformed .aes; }
post_compress_zstd_aes() { check raw_archive_compressed_encrypted chk_raw_transformed .zstd.aes; }

# ---------------------------------------------------------------- stream driver

run_stream_workload()
{
   stream_workload_body "$1"
   # always: also after a setup failure
   cleanup
   if [ "$KEEP_DATA" = 0 ]; then
      rm -rf "$WORK/data/$WL_ID"
   fi
}

stream_workload_body()
{
   local wl=$1 n
   CUR_WL=$wl
   WB_SEGSIZE=""
   WB_COMPRESSION=none
   WB_ENCRYPTION=none
   WB_SUFFIX=""
   RECV_PID=""
   RECV_N=0
   CRASH_TORN=0
   CRASH_WANT=1
   RESTART_WINDOWS=()
   RECONNECT_KILLED=0
   TOK=$(rand_token)
   REPL_PASS="wb-$(rand_token)"
   # a workload named twice on the command line gets wl.2, wl.3, ...
   WL_ID=$wl
   n=1
   while [ -e "$ARTIFACTS/$WL_ID" ]; do
      n=$((n + 1))
      WL_ID="$wl.$n"
   done
   WL_ART="$ARTIFACTS/$WL_ID"
   LOGS="$WL_ART/logs"
   RECV="$WL_ART/received"
   mkdir -p "$LOGS" "$RECV"
   pick_port P18_PORT
   pick_port WB_PORT
   if declare -F "cfg_$wl" > /dev/null; then
      "cfg_$wl"
   fi
   log "== $wl (PG18 port $P18_PORT, walbridge port $WB_PORT, segsize ${WB_SEGSIZE:-16}MB, compression $WB_COMPRESSION, encryption $WB_ENCRYPTION)"

   if ! start_primary "$WB_SEGSIZE"; then
      record FAIL setup "PG18 primary did not start: $(tail -2 "$LOGS/pg18.log" 2> /dev/null)"
      return
   fi
   if ! start_walbridge "$WB_COMPRESSION" "$WB_ENCRYPTION"; then
      record FAIL setup "walbridge did not connect upstream: $(grep -E 'ERROR|FATAL' "$LOGS/walbridge.log" 2> /dev/null | tail -2 | tr '\n' ' ')$(tail -2 "$LOGS/walbridge.out" 2> /dev/null)"
      return
   fi
   if ! start_receivewal; then
      record FAIL setup "pg_receivewal exited: $(tail -2 "$LOGS/pg_receivewal.1.log")"
      return
   fi

   START_UP=$(q18 "SELECT pg_current_wal_insert_lsn()")
   emit_marker start > /dev/null
   if ! "wl_$wl"; then
      record FAIL workload "workload SQL failed; see $LOGS"
   fi
   END_UP=$(emit_marker end)
   seal
   wait_received "wbe2e:end:$TOK" "$TIMEOUT"
   sleep 1
   stop_receivewal INT

   build_dumps
   if [ -n "$WB_SUFFIX" ]; then
      settle_archive
   fi
   common_checks
   if declare -F "post_$wl" > /dev/null; then
      "post_$wl"
   fi

   ls -l "$WB_RAW" > "$WL_ART/raw_archive.ls" 2>&1
   "$PG19BIN/pg_waldump" --stats=record -p "$SCR" -s "$FIRST_LSN" > "$WL_ART/waldump_stats.txt" 2>&1
}

# ---------------------------------------------------------------- main

# pg_major DIR: major version of DIR/postgres, or nothing
pg_major()
{
   [ -x "$1/postgres" ] || return 1
   "$1/postgres" --version 2> /dev/null | sed -nE 's/^postgres \(PostgreSQL\) ([0-9]+).*/\1/p'
}

# find_pg_bin VAR MAJOR TOOL...: VAR (PG18BIN/PG19BIN) names a bin directory
# of PostgreSQL MAJOR; when unset, take the first matching one on PATH
find_pg_bin()
{
   local var=$1 major=$2 dir=${!1} d t found=""
   shift 2
   if [ -n "$dir" ]; then
      [ "$(pg_major "$dir")" = "$major" ] ||
         die "$var=$dir does not contain a PostgreSQL $major postgres binary (found: $("$dir/postgres" --version 2>&1 | head -1))"
      found=$dir
   else
      local IFS=:
      for d in $PATH; do
         if [ -n "$d" ] && [ "$(pg_major "$d")" = "$major" ]; then
            found=$d
            break
         fi
      done
      [ -n "$found" ] ||
         die "PostgreSQL $major not found: set $var to its bin directory (for example /usr/lib/postgresql/$major/bin) or put that directory on PATH"
   fi
   for t in "$@"; do
      [ -x "$found/$t" ] || die "$var=$found has no $t"
   done
   printf -v "$var" '%s' "$found"
}

main()
{
   local opt sel=() w
   while getopts "o:klh" opt; do
      case "$opt" in
         o) ARTIFACTS=$OPTARG ;;
         k) KEEP_DATA=1 ;;
         l) printf '%s\n' "${STREAM_WORKLOADS[@]}"; exit 0 ;;
         h) usage; exit 0 ;;
         *) usage; exit 2 ;;
      esac
   done
   shift $((OPTIND - 1))
   [ $# -eq 0 ] && set -- stream
   for w in "$@"; do
      case "$w" in
         stream) sel+=("${STREAM_WORKLOADS[@]}") ;;
         *)
            declare -F "wl_$w" > /dev/null || { usage; die "unknown workload: $w"; }
            sel+=("$w")
            ;;
      esac
   done

   find_pg_bin PG18BIN 18 initdb pg_ctl psql pg_waldump
   find_pg_bin PG19BIN 19 psql pg_receivewal pg_waldump
   for f in "$PGMONETA_BUILD/src/pgmoneta-walbridge" "$PGMONETA_BUILD/src/pgmoneta-admin"; do
      [ -x "$f" ] || die "missing $f: build pgmoneta first, or set PGMONETA_BUILD to its build directory"
   done

   WORK=$(mktemp -d "${TMPDIR:-/tmp}/walbridge-e2e.XXXXXX") || die "mktemp failed"
   : > "$WORK/cleanup.list"
   trap 'cleanup' EXIT
   trap 'cleanup; exit 130' INT TERM
   ARTIFACTS=${ARTIFACTS:-$WORK/artifacts}
   mkdir -p "$ARTIFACTS" "$WORK/data" "$WORK/run" "$WORK/home" || die "cannot create $ARTIFACTS"
   ARTIFACTS=$(cd "$ARTIFACTS" && pwd)
   SOCK="$WORK/run"
   RESULTS_TSV="$ARTIFACTS/results.tsv"
   printf 'workload\tcheck\tstatus\tdetail\n' > "$RESULTS_TSV"

   # Binaries run from a path without spaces: pgmoneta's backtrace helper
   # passes the executable path to addr2line unquoted.
   BIN="$WORK/bin"
   mkdir -p "$BIN" "$WORK/profraw"
   # coverage builds (-fprofile-instr-generate) would write default.profraw
   # into the current directory
   export LLVM_PROFILE_FILE="$WORK/profraw/%p.profraw"
   cp "$PGMONETA_BUILD/src/pgmoneta-walbridge" "$PGMONETA_BUILD/src/pgmoneta-admin" "$BIN/"
   cp -P "$PGMONETA_BUILD"/src/libpgmoneta.so* "$BIN/" 2> /dev/null
   HOME="$WORK/home" LD_LIBRARY_PATH="$BIN" "$BIN/pgmoneta-admin" -P "mk-$(rand_token)" master-key \
      > "$WORK/master-key.log" 2>&1 || die "master-key failed: $(cat "$WORK/master-key.log")"
   {
      echo "pgmoneta:   $PROJECT_DIR ($(git -C "$PROJECT_DIR" rev-parse --short HEAD 2> /dev/null), $(git -C "$PROJECT_DIR" status --porcelain 2> /dev/null | wc -l) uncommitted changes)"
      echo "PG18:       $("$PG18BIN/postgres" --version)"
      echo "PG19:       $("$PG19BIN/postgres" --version)"
      echo "work dir:   $WORK"
      echo "started:    $(date -u '+%F %T UTC')"
   } | tee "$ARTIFACTS/environment.txt" >&2

   for w in "${sel[@]}"; do
      run_stream_workload "$w"
   done
   cleanup

   summarize
}

summarize()
{
   local fails xpass
   {
      echo "walbridge e2e: $(date -u '+%F %T UTC')"
      awk -F'\t' 'NR > 1 { n[$3]++ } END { for (s in n) printf "  %-10s %d\n", s, n[s] }' "$RESULTS_TSV" | sort
      echo
      awk -F'\t' 'NR > 1 && $3 != "PASS" { printf "%-10s %-18s %-42s %s\n", $3, $1, $2, substr($4, 1, 160) }' "$RESULTS_TSV"
      echo
      echo "artifacts: $ARTIFACTS"
   } | tee "$ARTIFACTS/summary.txt"
   fails=$(awk -F'\t' 'NR > 1 && $3 == "FAIL"' "$RESULTS_TSV" | wc -l)
   xpass=$(awk -F'\t' 'NR > 1 && $3 == "XPASS"' "$RESULTS_TSV" | wc -l)
   if [ "$fails" -gt 0 ] || { [ "$STRICT_XPASS" = 1 ] && [ "$xpass" -gt 0 ]; }; then
      exit 1
   fi
   exit 0
}

main "$@"
