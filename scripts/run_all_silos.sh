#!/bin/bash
# 启动脚本 start_all.sh

# 默认值
DATASET="audio"
SERVER="BDA03"
ALGO="mpc"
SILO_NUM=10                     # 新增：silo 数量，默认 10
PID_FILE="run_silo_pids.txt"

# 定义服务器对应的 id 范围（根据不同 silo_num）
declare -A ID_RANGES_10
ID_RANGES_10["BDA02"]="0 1 2 3"
ID_RANGES_10["BDA05"]="4 5 6 7"
ID_RANGES_10["BDA03"]="8 9"

declare -A ID_RANGES_5
ID_RANGES_5["BDA02"]="0 1"
ID_RANGES_5["BDA05"]="2 3"
ID_RANGES_5["BDA03"]="4"

# 根据 silo_num 选择对应的 ID 范围
get_id_ranges() {
    local server=$1
    local silo_num=$2
    
    if [[ $silo_num -eq 5 ]]; then
        echo "${ID_RANGES_5[$server]}"
    else
        echo "${ID_RANGES_10[$server]}"
    fi
}

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        --dataset=*)
            DATASET="${1#*=}"
            shift
            ;;
        --dataset)
            DATASET="$2"
            shift 2
            ;;
        --server=*)
            SERVER="${1#*=}"
            shift
            ;;
        --server)
            SERVER="$2"
            shift 2
            ;;
        --algo=*)
            ALGO="${1#*=}"
            shift
            ;;
        --algo)
            ALGO="$2"
            shift 2
            ;;
        --silo-num=*)
            SILO_NUM="${1#*=}"
            shift
            ;;
        --silo-num)
            SILO_NUM="$2"
            shift 2
            ;;
        --help)
            echo "用法: $0 [--dataset=DATASET] [--server=SERVER] [--algo=ALGO] [--silo-num=NUM]"
            echo "或:   $0 [--dataset DATASET] [--server SERVER] [--algo ALGO] [--silo-num NUM]"
            echo ""
            echo "支持的服务器及其 id 范围:"
            echo "  silo-num=10:"
            echo "    BDA02: 0-3"
            echo "    BDA05: 4-7"
            echo "    BDA03: 8-9"
            echo "  silo-num=5:"
            echo "    BDA02: 0-1"
            echo "    BDA05: 2-3"
            echo "    BDA03: 4"
            echo ""
            echo "参数说明:"
            echo "  --dataset   : 数据集名称（默认: audio）"
            echo "  --server    : 服务器名称（默认: BDA03）"
            echo "  --algo      : 算法名称（默认: mpc）"
            echo "  --silo-num  : Silo 数量（默认: 10，支持: 5, 10）"
            echo ""
            echo "示例:"
            echo "  $0 --dataset=skewed --server=BDA05 --algo=mpc --silo-num=10"
            echo "  $0 --dataset skewed --server BDA05 --algo silo --silo-num=5"
            exit 0
            ;;
        *)
            echo "未知参数: $1"
            echo "用法: $0 [--dataset=DATASET] [--server=SERVER] [--algo=ALGO] [--silo-num=NUM]"
            exit 1
            ;;
    esac
done

# 检查 silo_num 是否支持
if [[ $SILO_NUM -ne 5 && $SILO_NUM -ne 10 ]]; then
    echo "错误: 不支持的 silo-num '$SILO_NUM'"
    echo "支持的 silo-num: 5, 10"
    exit 1
fi

# 检查服务器是否支持
if [[ -z "$(get_id_ranges $SERVER $SILO_NUM)" ]]; then
    echo "错误: 不支持的服务器 '$SERVER' 或 silo-num '$SILO_NUM' 组合"
    echo "支持的服务器: BDA02, BDA05, BDA03"
    echo "当 silo-num=5 时，BDA02:0-1, BDA05:2-3, BDA03:4"
    echo "当 silo-num=10 时，BDA02:0-3, BDA05:4-7, BDA03:8-9"
    exit 1
fi

# 构建脚本名称
SCRIPT_NAME="${ALGO}_Silo.sh"

# 检查脚本是否存在
if [[ ! -f "$SCRIPT_NAME" ]]; then
    echo "错误: 脚本 '$SCRIPT_NAME' 不存在"
    echo "请检查 --algo 参数是否正确"
    exit 1
fi

# 获取对应的 id 列表
IDS=($(get_id_ranges $SERVER $SILO_NUM))

# 清空旧的 PID 文件
> $PID_FILE

# 启动任务
for id in "${IDS[@]}"; do
    ./$SCRIPT_NAME --id=$id --dataset=$DATASET --server=$SERVER --silo-num=$SILO_NUM &
    echo $! >> $PID_FILE
done

echo "所有任务已启动，PID 保存在 $PID_FILE"
echo "参数: algo=$ALGO, dataset=$DATASET, server=$SERVER, silo-num=$SILO_NUM, ids=${IDS[@]}"