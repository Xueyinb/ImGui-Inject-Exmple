#!/bin/bash
[[ "$(id -u)" != "2000" && "$(id -u)" != "0" ]] && echo "\e[31m需求ROOT\e[0m" && exit

Pns=("com.idragoncheats.studio" "com.idragoncheats.studiopro" "com.goxome.aidestudio" "aidepro.top")
Fd=()

for Pn in "${Pns[@]}"
do
    [ -e "/data/user/0/${Pn}/no_backup/ndksupport-1710240003/android-ndk-aide/ndk-build" ] && Fd+=("${Pn}")
done
[ ${#Fd[@]} -eq 0 ] && echo "\e[31m未找到ndk-build\e[0m" && exit 1

if [ ${#Fd[@]} != 1 ] ; then
    echo "\e[31m找到多个ndk-build，输入序号使用\e[0m"
    for i in "${!Fd[@]}"; do
        echo "\e[33m[$((i+1))]\e[90m ->\e[37m ${Fd[i]}\e[0m"
    done
    read Input
    if [[ $Input -ge 1 && $Input -le ${#Fd[@]} ]]; then
        UsePN="${Fd[$((Input-1))]}"
    else
        echo "\e[31m请输入1~${#Fd[@]}\e[0m"
        exit 1
    fi
else
    UsePN="${Fd[0]}"
fi

/data/user/0/${UsePN}/no_backup/ndksupport-1710240003/android-ndk-aide/ndk-build 2>&1

if [ $? -eq 0 ]; then
    echo "\e[32mMake Success\e[0m"
    echo "\e[32mso 输出目录：libs/arm64-v8a/\e[0m"
else
    echo "\e[31mMake Failed\e[0m"
    target_file="/data/user/0/${UsePN}/no_backup/ndksupport-1710240003/android-ndk-aide/build/core/setup-app.mk"
    if [ -f "$target_file" ]; then
        sed -i '1i SHELL := /system/bin/sh' "$target_file"
        echo "\e[33m编译出错 已在 $target_file 第一行添加 SHELL := /system/bin/sh 请重新启动\e[0m"
    else
        echo "\e[33m文件 $target_file 不存在，无法添加 请手动查找并添加\e[0m"
    fi
    exit 1
fi