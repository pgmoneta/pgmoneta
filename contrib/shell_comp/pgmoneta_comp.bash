#/usr/bin/env bash

# COMP_WORDS contains
# at index 0 the executable name (pgmoneta-cli)
# at index 1 the command name (e.g., backup)
pgmoneta_cli_completions()
{

    if [ "${#COMP_WORDS[@]}" == "2" ]; then
        # main completion: the user has specified nothing at all
        # or a single word, that is a command
        COMPREPLY=($(compgen -W "backup list-backup restore archive delete retain expunge is-alive stop status details reload reset" "${COMP_WORDS[1]}"))
    fi
}


pgmoneta_admin_completions()
{
    if [ "${#COMP_WORDS[@]}" == "2" ]; then
        # main completion: the user has specified nothing at all
        # or a single word, that is a command
        COMPREPLY=($(compgen -W "master-key add-user update-user remove-user list-users" "${COMP_WORDS[1]}"))
    fi
}

# install the completion functions
complete -F pgmoneta_cli_completions pgmoneta-cli
complete -F pgmoneta_admin_completions pgmoneta-admin
