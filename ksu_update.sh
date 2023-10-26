#!/bin/bash

repository_url="https://github.com/tiann/KernelSU.git"
directory="drivers/staging/kernelsu"
branch="main"
commit_message="drivers: kernelsu: update"

# Check if the repository directory exists
if [ ! -d "$directory" ]; then
    echo "Error: Directory '$directory' does not exist. Please ensure it exists before updating."
    exit 1
fi

echo "Pulling main branch of KernelSU"

# Run the git subtree pull command and check for errors
if git subtree pull --prefix="$directory" "$repository_url" "$branch" --squash -m "$commit_message"; then
    echo "Update successful."
else
    echo "Error: Update failed."
fi
