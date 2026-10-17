#!/bin/bash
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

# CI only. Runs in the background next to test/check.sh. If no test has
# finished for HANG_WATCHDOG_SECONDS (default 480), records what pgmoneta and
# PostgreSQL are waiting on into the uploaded log directory and exits.
# The tests keep running.

PG_VERSION=$1
LOG_DIR=/tmp/pgmoneta-test/log
OUT=$LOG_DIR/hang-diag.txt
LIMIT=${HANG_WATCHDOG_SECONDS:-480}

while sleep 30; do
   pgrep -f "test/check.sh ci" > /dev/null || exit 0
   [ -f $LOG_DIR/pgmoneta-test.log ] || continue
   age=$(( $(date +%s) - $(stat -c %Y $LOG_DIR/pgmoneta-test.log) ))
   [ $age -lt $LIMIT ] && continue
   {
      echo "== no test finished for $age s, last line: $(tail -1 $LOG_DIR/pgmoneta-test.log)"
      echo "== ps"
      ps -eo pid,ppid,stat,etime,wchan:32,args
      echo "== pg_stat_activity"
      runuser -u postgres -- /usr/pgsql-$PG_VERSION/bin/psql -h /tmp -X -c "SELECT pid, backend_type, state, wait_event_type, wait_event, now() - state_change AS since, left(query, 150) AS query FROM pg_stat_activity"
      for p in $(pgrep -f "^pgmoneta: |/pgmoneta -c|pgmoneta-test"); do
         echo "== gdb $p: $(tr '\0' ' ' < /proc/$p/cmdline)"
         gdb -batch -p $p -ex "thread apply all bt" 2>&1 | grep -vE "^\[|^Using host|^warning"
         echo "== strace $p (5 s)"
         timeout 5 strace -f -tt -p $p 2>&1 | head -40
      done
      echo "== ss"
      ss -tanpie
      sleep 20
      echo "== ss, 20 s later"
      ss -tanpie
   } > $OUT 2>&1
   cp /pglog/logfile $LOG_DIR/hang-postgresql.log 2>/dev/null
   cat $OUT
   exit 0
done
