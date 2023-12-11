#!/bin/bash

echo pulling main branch of KernelSU

git subtree pull --prefix=drivers/staging/kernelsu https://github.com/tiann/KernelSU.git main --squash -m "drivers: kernelsu: update"

echo done
