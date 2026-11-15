#!/bin/bash

# 使用说明
usage() {
    echo "Usage: $0 --dataset=<dataset_name> [--silo-num=<num>]"
    echo "  --dataset     Required. Dataset name (e.g., audio, image, text)"
    echo "  --silo-num    Optional. Silo number (default: 5)"
    echo ""
    echo "Example: $0 --dataset=audio --silo-num=5"
    echo "         $0 --dataset=image"
    exit 1
}

# 默认参数
SILO_NUM=5
DATASET=""

# 解析命令行参数
for arg in "$@"; do
    case $arg in
        --dataset=*)
            DATASET="${arg#*=}"
            ;;
        --silo-num=*)
            SILO_NUM="${arg#*=}"
            ;;
        --help|-h)
            usage
            ;;
        *)
            echo "Unknown option: $arg"
            usage
            ;;
    esac
done

# 检查必需参数
if [ -z "$DATASET" ]; then
    echo "Error: --dataset is required"
    usage
fi

echo "=========================================="
echo "Dataset: $DATASET"
echo "Silo Num: $SILO_NUM"
echo "=========================================="

# 定义所有命令（使用变量替代固定的dataset）
# 定义命令数组
if [ $SILO_NUM -eq 10 ]; then
    # SILO_NUM=10 时的命令列表
    if [ "$DATASET" = "skewed" ]; then
        commands=(
            "./hufu_Server.sh --query-k=128 --efsearch=100 --dataset=$DATASET --silo-num=$SILO_NUM"
            "./hufu_Server.sh --query-k=128 --efsearch=200 --dataset=$DATASET --silo-num=$SILO_NUM"
            "./hufu_Server.sh --query-k=128 --efsearch=300 --dataset=$DATASET --silo-num=$SILO_NUM"
            "./hufu_Server.sh --query-k=128 --efsearch=400 --dataset=$DATASET --silo-num=$SILO_NUM"
            "./hufu_Server.sh --query-k=128 --efsearch=500 --dataset=$DATASET --silo-num=$SILO_NUM"
        )
    else
        commands=(
            "./hufu_Server.sh --query-k=512 --efsearch=512 --dataset=$DATASET --silo-num=$SILO_NUM"
            "./hufu_Server.sh --query-k=512 --efsearch=500 --dataset=$DATASET --silo-num=$SILO_NUM"
            "./hufu_Server.sh --query-k=512 --efsearch=600 --dataset=$DATASET --silo-num=$SILO_NUM"
            "./hufu_Server.sh --query-k=512 --efsearch=700 --dataset=$DATASET --silo-num=$SILO_NUM"
            "./hufu_Server.sh --query-k=512 --efsearch=800 --dataset=$DATASET --silo-num=$SILO_NUM"
            "./hufu_Server.sh --query-k=512 --efsearch=900 --dataset=$DATASET --silo-num=$SILO_NUM"
        )
    fi
    commands=(
	"./hufu_Server.sh --query-k=16 --efsearch=200 --dataset=$DATASET --silo-num=$SILO_NUM"
	"./hufu_Server.sh --query-k=8 --efsearch=200 --dataset=$DATASET --silo-num=$SILO_NUM"
	"./hufu_Server.sh --query-k=4 --efsearch=200 --dataset=$DATASET --silo-num=$SILO_NUM"
	"./hufu_Server.sh --query-k=2 --efsearch=200 --dataset=$DATASET --silo-num=$SILO_NUM"
    )
else
    # SILO_NUM=5 或其他值时的命令列表（可以根据需要调整）
    commands=(
        #"./hufu_Server.sh --query-k=1024 --efsearch=200 --dataset=$DATASET --silo-num=$SILO_NUM"
        "./hufu_Server.sh --query-k=128 --efsearch=100 --dataset=$DATASET --silo-num=$SILO_NUM"
        "./hufu_Server.sh --query-k=128 --efsearch=200 --dataset=$DATASET --silo-num=$SILO_NUM"
        "./hufu_Server.sh --query-k=128 --efsearch=300 --dataset=$DATASET --silo-num=$SILO_NUM"
        "./hufu_Server.sh --query-k=128 --efsearch=400 --dataset=$DATASET --silo-num=$SILO_NUM"
        "./hufu_Server.sh --query-k=128 --efsearch=500 --dataset=$DATASET --silo-num=$SILO_NUM"
        "./hufu_Server.sh --query-k=64 --efsearch=200 --dataset=$DATASET --silo-num=$SILO_NUM"
        "./hufu_Server.sh --query-k=32 --efsearch=200 --dataset=$DATASET --silo-num=$SILO_NUM"
        "./hufu_Server.sh --query-k=16 --efsearch=200 --dataset=$DATASET --silo-num=$SILO_NUM"
        "./hufu_Server.sh --query-k=8 --efsearch=200 --dataset=$DATASET --silo-num=$SILO_NUM"
        "./hufu_Server.sh --query-k=4 --efsearch=200 --dataset=$DATASET --silo-num=$SILO_NUM"
        "./hufu_Server.sh --query-k=2 --efsearch=200 --dataset=$DATASET --silo-num=$SILO_NUM"
    )
fi

# 创建日志目录
log_dir="logs_hufu_${DATASET}_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$log_dir"

# 执行每个命令
total=${#commands[@]}
for i in "${!commands[@]}"; do
    cmd="${commands[$i]}"
    echo "[$((i+1))/$total] Executing: $cmd"
    
    # 生成日志文件名（包含dataset信息）
    log_file="$log_dir/run_$((i+1))_${DATASET}.log"
    
    # 执行命令并记录日志
    if eval "$cmd" > "$log_file" 2>&1; then
        echo "[$((i+1))/$total] ✓ Success"
    else
        echo "[$((i+1))/$total] ✗ Failed (check $log_file)"
    fi
    
    # 可选：命令之间添加延迟
    # sleep 1
done

echo ""
echo "=========================================="
echo "All commands executed for dataset: $DATASET"
echo "Logs saved in: $log_dir/"
echo "=========================================="
