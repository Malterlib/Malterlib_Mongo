#!/bin/bash
# Copyright © Unbroken AB
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set -e

MongoManagerExecutable="$PWD/MongoManager";

echo sudo rm -rf "$PWD/mongo"
sudo rm -rf "$PWD/mongo"

"$MongoManagerExecutable" --list-restore-range

RestoreTime="$1"

if [[ $RestoreTime == "" ]]; then
	echo "Type the time you want to restore to [Latest]:"
	read RestoreTime
fi

if [[ $RestoreTime == "Latest" ]]; then
	RestoreTime=""
fi

echo sudo "$MongoManagerExecutable" --restore "$RestoreTime"
sudo "$MongoManagerExecutable" --restore "$RestoreTime"

echo Restore was successful

exit 0
