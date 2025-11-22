#!/usr/bin/env bash
# Usage: ./opt.sh 1

N="$1"

if [ -z "$N" ]; then
    echo "Usage: $0 <program-index>"
    exit 1
fi

# Run SWI-Prolog optimizer:
swipl -q -s opt/main.pl -g "run('$N'), halt."
