
## C programming

[**pgmoneta**](https://github.com/pgmoneta/pgmoneta) is developed using the [C programming language](https://en.wikipedia.org/wiki/C_(programming_language)) so it is a good
idea to have some knowledge about the language before you begin to make changes.

There are books like,

* [C in a Nutshell](https://www.oreilly.com/library/view/c-in-a/9781491924174/)
* [21st Century C](https://www.oreilly.com/library/view/21st-century-c/9781491904428/)

that can help you

### Debugging

In order to debug problems in your code you can use [gdb](https://www.sourceware.org/gdb/), or add extra logging using
the `pgmoneta_log_XYZ()` API

## Git guide

Here are some links that will help you

* [How to Squash Commits in Git][git_squash]
* [ProGit book][progit]

### Basic steps

**Start by forking the repository**

This is done by the "Fork" button on GitHub.

**Clone your repository locally**

This is done by

```sh
git clone git@github.com:<username>/pgmoneta.git
```

**Add upstream**

Do

```sh
cd pgmoneta
git remote add upstream https://github.com/pgmoneta/pgmoneta.git
```

**Do a work branch**

```sh
git checkout -b mywork main
```

**Make the changes**

Remember to verify the compile and execution of the code.

Use

```
[#xyz] Description
```

as the commit message where `[#xyz]` is the issue number for the work, and
`Description` is a short description of the issue in the first line

**Multiple commits**

If you have multiple commits on your branch then squash them

``` sh
git rebase -i HEAD~2
```

for example. It is `p` for the first one, then `s` for the rest

**Rebase**

Always rebase

``` sh
git fetch upstream
git rebase -i upstream/main
```

**Force push**

When you are done with your changes force push your branch

``` sh
git push -f origin mywork
```

and then create a pull request for it

**Format source code**

Use

``` sh
./uncrustify.sh
```

to format the source code

**Repeat**

Based on feedback keep making changes, squashing, rebasing and force pushing

**Undo**

Normally you can reset to an earlier commit using `git reset <commit hash> --hard`.

But if you accidentally squashed two or more commits, and you want to undo that, you need to know where to reset to, and the commit seems to have lost after you rebased.

But they are not actually lost - using `git reflog`, you can find every commit the HEAD pointer has ever pointed to. Find the commit you want to reset to, and do `git reset --hard`.
