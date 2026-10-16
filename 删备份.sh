
echo "找到以下.bak文件:"
find . -type f -name "*.bak" | sed 's/^/  /'

echo "要删除上述所有.bak文件吗? (输入'y'确认删除, 其他键取消)"
printf "选择[y/N]: "
read choice

if [ "$choice" = "y" ] || [ "$choice" = "Y" ]; then
    find . -type f -name "*.bak" -delete
    echo "已删除所有.bak备份文件"
else
    echo "操作已取消 - 没有删除任何文件"
fi
