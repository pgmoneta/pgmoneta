#/usr/bin/env bash

# COMP_WORDS contains
# at index 0 the executable name (pgmoneta-cli)
# at index 1 the command name (e.g., backup)
# at index 2, if required, the subcommand name (e.g., reload)
pgmoneta_cli_completions()
{

    if [ "${#COMP_WORDS[@]}" == "2" ]; then
        # main completion: the user has specified nothing at all
        # or a single word, that is a command
        COMPREPLY=($(compgen -W "backup list-backup restore verify archive delete retain expunge encrypt decrypt info ping stop status conf clear" "${COMP_WORDS[1]}"))
    else
        # the user has specified something else
        # subcommand required?
        case ${COMP_WORDS[1]} in
            status)
                COMPREPLY+=($(compgen -W "details" "${COMP_WORDS[2]}"))
                ;;
            conf)
                COMPREPLY+=($(compgen -W "reload" "${COMP_WORDS[2]}"))
                ;;
            clear)
                COMPREPLY+=($(compgen -W "prometheus" "${COMP_WORDS[2]}"))
                ;;
        esac
    fi
}


pgmoneta_admin_completions()
{
    if [ "${#COMP_WORDS[@]}" == "2" ]; then
        # main completion: the user has specified nothing at all
        # or a single word, that is a command
        COMPREPLY=($(compgen -W "master-key user" "${COMP_WORDS[1]}"))
    else
        # the user has specified something else
        # subcommand required?
        case ${COMP_WORDS[1]} in
            user)
                COMPREPLY+=($(compgen -W "add del edit ls" "${COMP_WORDS[2]}"))
                ;;
        esac
    fi
}

# install the completion functions
complete -F pgmoneta_cli_completions pgmoneta-cli
complete -F pgmoneta_admin_completions pgmoneta-admin
