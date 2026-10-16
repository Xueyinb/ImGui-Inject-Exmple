#!/bin/bash
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
INJECT="$SCRIPT_DIR/inject/inject"
SO="$SCRIPT_DIR/obj/local/arm64-v8a/libimgui_demo.so"

PKG="$1"
while [ -z "$PKG" ]; do
    printf "请输入要注入的包名: "
    read PKG
    if [ -z "$PKG" ]; then
        echo "包名不能为空"
    elif [ ! -d "/data/data/$PKG" ]; then
        echo "找不到 /data/data/$PKG"
        PKG=""
    fi
done

APPDIR="/data/data/$PKG"

if [ ! -f "$INJECT" ]; then
    echo "找不到 $INJECT"
    exit 1
fi
if [ ! -f "$SO" ]; then
    echo "找不到 $SO，请先编译"
    exit 1
fi


echo "启动 $PKG"
cp -f "$INJECT" "$APPDIR/"
cp -f "$SO" "$APPDIR/"
chmod 777 "$APPDIR/inject"
chmod 777 "$APPDIR/libimgui_demo.so"

echo "开始注入$PKG"
monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sleep 3
"$APPDIR/inject" -n "$PKG" -so "$APPDIR/libimgui_demo.so"

echo "已清理临时文件"
rm -rf "$APPDIR/inject"
rm -rf "$APPDIR/libimgui_demo.so"