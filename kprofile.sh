#!/bin/bash
#
# Script to import or update kprofiles using Git subtree

# Prompt the user for input
read -p "Please input Kprofiles branch name: " branch
read -p "Choose your options to do (import (i) or update (u)): " option

# Validate user input
if [ -z "$branch" ]; then
    echo "Error: Branch name cannot be empty."
    exit 1
fi

if [[ "$option" != "import" && "$option" != "i" && "$option" != "update" && "$option" != "u" ]]; then
    echo "Error: Invalid option."
    exit 1
fi

# Perform Git subtree command based on user's choice
case $option in
    import | i)
        git subtree add --prefix=drivers/misc/kprofiles https://github.com/dakkshesh07/Kprofiles $branch
        ;;
    update | u)
        git subtree pull --prefix=drivers/misc/kprofiles https://github.com/dakkshesh07/kprofiles $branch
        ;;
    *)
        echo "Invalid character"
        exit 1
        ;;
esac

# Check the exit status of the Git command and provide feedback
if [ $? -eq 0 ]; then
    echo "Operation successful."
else
    echo "Error: Operation failed."
    exit 1
fi
