#!/bin/bash
#
# Import or update qcacld-3.0, qca-wifi-host-cmn, fw-api, and audio-kernel.
#

print_msg() {
    yellow="\e[1;33m"    
    restore="\e[1;0m"
    echo -e ${yellow}${1}${2}${restore}
}

import_or_update() {
    local prefix=$1
    local remote_url=$2
    local branch=$3
    git subtree add --prefix=$prefix $remote_url $branch
}

read -p "Please input the tag/branch name: " branch
read -p "What do you want to do (import (i) or update (u)): " option

case $option in
    import | i)
        import_or_update "drivers/staging/qcacld-3.0" "https://git.codelinaro.org/clo/la/platform/vendor/qcom-opensource/wlan/qcacld-3.0" $branch
        import_or_update "drivers/staging/qca-wifi-host-cmn" "https://git.codelinaro.org/clo/la/platform/vendor/qcom-opensource/wlan/qca-wifi-host-cmn" $branch
        import_or_update "drivers/staging/fw-api" "https://git.codelinaro.org/clo/la/platform/vendor/qcom-opensource/wlan/fw-api" $branch
        import_or_update "techpack/audio" "https://git.codelinaro.org/clo/la/platform/vendor/opensource/audio-kernel/" $branch
        print_msg "Import complete."
        ;;
    update | u)
        import_or_update "drivers/staging/qcacld-3.0" "https://git.codelinaro.org/clo/la/platform/vendor/qcom-opensource/wlan/qcacld-3.0" $branch
        import_or_update "drivers/staging/qca-wifi-host-cmn" "https://git.codelinaro.org/clo/la/platform/vendor/qcom-opensource/wlan/qca-wifi-host-cmn" $branch
        import_or_update "drivers/staging/fw-api" "https://git.codelinaro.org/clo/la/platform/vendor/qcom-opensource/wlan/fw-api" $branch
        import_or_update "techpack/audio" "https://git.codelinaro.org/clo/la/platform/vendor/opensource/audio-kernel/" $branch
        print_msg "Update complete."
        ;;
    *)
        print_msg "Invalid option. Please choose 'import (i)' or 'update (u)."
        ;;
esac
