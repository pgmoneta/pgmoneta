#compdef _pgmoneta_cli pgmoneta-cli
#compdef _pgmoneta_admin pgmoneta-admin


function _pgmoneta_cli()
{
    local line
    _arguments -C \
               "1: :(backup list-backup restore verify archive delete retain expunge encrypt decrypt info ping shutdown status conf clear)" \
               "*::arg:->args"
    case $line[1] in
        status)
            _pgmoneta_cli_status
            ;;
        conf)
            _pgmoneta_cli_conf
            ;;
        clear)
            _pgmoneta_cli_clear
            ;;
    esac
}

function _pgmoneta_cli_status()
{
    _arguments -C \
               "1: :(details)" \
               "*::arg:->args"
}

function _pgmoneta_cli_conf()
{
    _arguments -C \
               "1: :(reload)" \
               "*::arg:->args"
}

function _pgmoneta_cli_clear()
{
    _arguments -C \
               "1: :(prometheus)" \
               "*::arg:->args"
}

function _pgmoneta_admin()
{
   local line
    _arguments -C \
               "1: :(master-key user)" \
               "*::arg:->args"

    case $line[1] in
        user)
            _pgmoneta_admin_user
            ;;
    esac
}

function _pgmoneta_admin_user()
{
    _arguments -C \
               "1: :(add del edit ls)" \
               "*::arg:->args"
}
