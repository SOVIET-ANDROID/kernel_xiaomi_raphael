#!/bin/bash
# kernelSu script by INHPKUL007 @ github.com
##

repository_url="https://github.com/tiann/KernelSU.git"
prefix="drivers/staging/kernelsu"
branch="main"
commit_message="drivers: kernelsu: update"

read -p "Choose your action (subtree add (a) or pull (p)): " action

case $action in
    a | add)
        echo "Performing subtree add..."
        git subtree add --prefix=$prefix $repository_url $branch --squash -m "$commit_message"
        ;;
    p | pull)
        echo "Performing subtree pull..."
        git subtree pull --prefix=$prefix $repository_url $branch --squash -m "$commit_message"
        ;;
    *)
        echo "Invalid action. Aborted."
        exit 1
        ;;
esac

if [ $? -eq 0 ]; then
    echo "Operation successful. Changes squashed with commit message: '$commit_message'."
else
    echo "Error: Operation failed."
    exit 1
fi

echo "Done."
