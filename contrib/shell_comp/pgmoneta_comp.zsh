#compdef _pgmoneta_cli pgmoneta-cli
#compdef _pgmoneta_admin pgmoneta-admin


function _pgmoneta_cli()
{
    local line
    _arguments -C \
               "1: :(backup list-backup restore archive delete retain expunge is-alive stop status details reload reset)" \
               "*::arg:->args"
}

function _pgmoneta_admin()
{
    local line
    _arguments -C \
               "1: :(master-key add-user update-user remove-user list-users)" \
               "*::arg:->args"
}
