#!/bin/bash
# create multiresolution windows icon
#mainnet
ICON_SRC=../../src/qt/res/icons/rpdchain.png
ICON_DST=../../src/qt/res/icons/rpdchain.ico
convert ${ICON_SRC} -resize 16x16 rpdchain-16.png
convert ${ICON_SRC} -resize 32x32 rpdchain-32.png
convert ${ICON_SRC} -resize 48x48 rpdchain-48.png
convert rpdchain-16.png rpdchain-32.png rpdchain-48.png ${ICON_DST}
#testnet
ICON_SRC=../../src/qt/res/icons/rpdchain_testnet.png
ICON_DST=../../src/qt/res/icons/rpdchain_testnet.ico
convert ${ICON_SRC} -resize 16x16 rpdchain-16.png
convert ${ICON_SRC} -resize 32x32 rpdchain-32.png
convert ${ICON_SRC} -resize 48x48 rpdchain-48.png
convert rpdchain-16.png rpdchain-32.png rpdchain-48.png ${ICON_DST}
rm rpdchain-16.png rpdchain-32.png rpdchain-48.png
