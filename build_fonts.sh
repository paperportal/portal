#!/bin/sh

ZIG=~/zig/zig

pushd ../zig-fonts
$ZIG build run -- system_fonts/static/Rubik-Medium.ttf ../portal/main/assets/rubik_medium_12.bbf 12 32 255
$ZIG build run -- system_fonts/static/Rubik-Medium.ttf ../portal/main/assets/rubik_medium_20.bbf 20 32 255
popd
