# valgrind

[valgrind](https://valgrind.org/) is a great tool to verify memory management.

# Run memory management detection

``` bash
valgrind --leak-check=full --show-leak-kinds=all --log-file=%p.log --trace-children=yes --track-origins=yes --read-var-info=yes ./pgmoneta -c pgmoneta.conf -u pgmoneta_users.conf
```

# Use suppressions rules

``` bash
valgrind --suppressions=../../contrib/valgrind/pgmoneta.supp --leak-check=full --show-leak-kinds=all --log-file=%p.log --trace-children=yes --track-origins=yes --read-var-info=yes ./pgmoneta -c pgmoneta.conf -u pgmoneta_users.conf
```

# Generate suppressions rules

``` bash
valgrind --gen-suppressions=all --leak-check=full --show-leak-kinds=all --log-file=%p.log --trace-children=yes --track-origins=yes --read-var-info=yes ./pgmoneta -c pgmoneta.conf -u pgmoneta_users.conf
```
