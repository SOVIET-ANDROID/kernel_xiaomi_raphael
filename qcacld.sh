#!/bin/bash
#
# Script to import or update qcacld-4.0, qca-wifi-host-cmn, fw-api, and audio-kernel.

perform_git_subtree() {
    local prefix=$1
    local repo_url=$2
    local branch=$3

    read -p "Are you sure you want to $operation $prefix (y/n)? " confirm
    if [[ ! "$confirm" =~ ^[Yy]$ ]]; then
        echo "Aborted by user for $prefix."
        exit 0
    fi

    case $operation in
        import | i)
            git subtree add --prefix=$prefix $repo_url $branch
            ;;
        update | u)
            git subtree pull --prefix=$prefix $repo_url $branch
            ;;
        *)
            echo "Invalid operation"
            exit 1
            ;;
    esac

    local exit_status=$?
    if [ $exit_status -eq 0 ]; then
        echo "Operation successful for $prefix."
    else
        echo "Error: Operation failed for $prefix (Exit Status: $exit_status)."
        exit 1
    fi
}

# Prompt the user for input
read -p "Please input the tag/branch name: " branch
read -p "What do you want to do (import (i) or update (u)): " operation

# Perform Git subtree operations for each repository
perform_git_subtree "drivers/staging/qcacld-3.0" "https://git.codelinaro.org/clo/la/platform/vendor/qcom-opensource/wlan/qcacld-3.0" $branch
perform_git_subtree "drivers/staging/qca-wifi-host-cmn" "https://git.codelinaro.org/clo/la/platform/vendor/qcom-opensource/wlan/qca-wifi-host-cmn" $branch
perform_git_subtree "drivers/staging/fw-api" "https://git.codelinaro.org/clo/la/platform/vendor/qcom-opensource/wlan/fw-api" $branch
perform_git_subtree "techpack/audio" "https://git.codelinaro.org/clo/la/platform/vendor/opensource/audio-kernel/" $branch

echo "Done."
