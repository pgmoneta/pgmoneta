#!/bin/bash

function indent()
{
    for f in $1
    do
        uncrustify -c uncrustify.cfg -q --replace --no-backup $f
        rm -f $f.uncrustify
    done
}

indent "src/*.c"
indent "src/include/*.h"
indent "src/include/walfile/*.h"
indent "src/libpgmoneta/*.c"
indent "src/libpgmoneta/walfile/*.c"
indent "test/*.c"
indent "test/include/*.h"
indent "test/testcases/*.h"
indent "test/testcases/*.c"
indent "test/wal/*.c"
