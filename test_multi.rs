; test_multi.rs - 多行脚本端到端测试
function add($a, $b)
    return $a + $b
end

$sum = 0
for $i = 1, 2, 3, 4, 5 do
    $sum = add($sum, $i)
end
echo sum $sum

$x = 10
if $x > 5
    echo big
elseif $x > 0
    echo small
else
    echo zero
end

$n = 0
while $n < 3
    echo loop $n
    $n++
    wait 10
end

echo done
