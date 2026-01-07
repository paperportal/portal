#!/usr/bin/env bash
rm -rf sdkconfig
idf.py fullclean

if [ -z "$1" ]; then
    idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults" reconfigure
else
    CONFIG_FILE="sdkconfig.$1"
    if [ ! -f "$CONFIG_FILE" ]; then
        echo "Error: Configuration file '$CONFIG_FILE' not found!"
        exit 1
    fi
    idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;$CONFIG_FILE" reconfigure
fi
