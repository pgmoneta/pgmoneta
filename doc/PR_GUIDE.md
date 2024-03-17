# Pull Request Guide

## Set up local machine

### Fork pgmoneta into your GitHub account

1. Go to [pgmoneta](https://github.com/pgmoneta/pgmoneta)
2. Create a fork by cliking at fork button on the right top corner
3. A forked repository will be created in your account.

### Clone on your local machine

Navigate to the directory where you wish to place the source code. In this example, we'll use `/usr/local/` as the base directory.

    cd /usr/local/
    git clone https://github.com/YOUR_USERNAME/pgmoneta pgmoneta
    cd pgmoneta
    git remote add upstream https://github.com/pgmoneta/pgmoneta
    git fetch --all

Now you can check your remote using

    git remote -v

One is from your GitHub account repository, and the other is from the `pgmoneta` repository

## Commit

If you want to make some changes, it's better to create a new branch.

    git checkout main
    git pull
    git branch
    git checkout -b new-branch main
    git branch

After this, you will switch to the new-branch branch, where you can manipulate the code and make some changes.

You can also delete a branch other than the current one using the following command:

    git branch -D branch-name

### Submit commit

Before committing, you need to stage the changes you want to include in the commit. You can stage changes to specific files or all changes in the working directory.

- To stage a specific file:

        git add filename

- To stage all changes:

        git add .

Once the changes are staged, you can commit them to your local repository.

    git commit -m "Your commit message"

You can check the commit using:

    git log

### Delete commit

1. Remove the last commits from git:

        git reset --hard <the one you revert to, using the commit hash value>

2. Remove a specific one commit:

    - First, get commits (Here, we retrieve the last 4 commits)

            git rebase -i HEAD~4    `
    - If you encounter an error stating `error: unable to start editor 'emacs'`, you can use the command below to change the editor.

            git config --global core.editor "vim"

3. You will see

        pick be5b2e1 4-th commit
        pick 3d50463 3-rd commit
        pick 93d3de2 2-nd commit
        pick 347a2c5 1-st commit

4. Change the line you want to delete by changing `pick` to `drop`, then quit and save.

### Squash commit

1. Start an interactive rebase:

    - Determine how many commits you want to combine.

            git rebase -i HEAD~3

2. Choose commits to combine:

    - You wil see

            pick 3d50463 3-rd commit
            pick 93d3de2 2-nd commit
            pick 347a2c5 1-st commit

    - To combine these commits, you'll need to change the word pick to squash. The first commit will be the base for the squash.

3. Rewrite commit message:

    - After you save and close the file, another editor window will open, allowing you to rewrite the commit message for the new, combined commit.

4. Complete the Rebase:

    - Once you save and close the commit message file, Git will complete the rebase process and squash the commits into one.

## Push

Nomally we just use

    git push origin branch_name

After squashing commits or rewriting history, we have to use force push

    git push --force origin branch_name

## Rebase

When working with a forked repository, it's common to keep your fork up to date with the upstream repository. Before we submit pull request, we should rebase our forked repository with the upstream main branch.

    git fetch upstream          # fetch the latest changes
    git checkout main           # switch to the repository you want to rebase.
    git rebase upstream    # rebase the current branch with the upstream repository

## Pull request

Now you can use your GitHub account to submit a pull request to the community.
