#!/bin/bash

# 使用说明
usage() {
    echo "Usage: $0 --dataset=<dataset_name> [--silo-num=<num>] [--options=<opt1,opt2,...>]"
    echo "  --dataset     Required. Dataset name (e.g., audio, image, text)"
    echo "  --silo-num    Optional. Silo number (default: 5)"
    echo "  --comm-round  Optional. Communication round (default: 10)"
    echo "  --options     Optional. Comma-separated list of options to test (default: greedy,active,batch-a,batch-g)"
    echo "                Available options: greedy, active, batch-a, batch-g"
    echo ""
    echo "Example: $0 --dataset=audio --silo-num=5"
    echo "         $0 --dataset=image --options=greedy,active"
    echo "         $0 --dataset=text --options=batch-a,batch-g --silo-num=3"
    exit 1
}

# 默认参数
SILO_NUM=5
COMM_ROUND=10
DATASET=""
OPTIONS="greedy,active,batch-a,batch-g"
# OPTIONS="batch-a,active"

# 解析命令行参数
for arg in "$@"; do
    case $arg in
        --dataset=*)
            DATASET="${arg#*=}"
            ;;
        --silo-num=*)
            SILO_NUM="${arg#*=}"
            ;;
        --com-round=*)
            COMM_ROUND="${arg#*=}"
            ;;
        --options=*)
            OPTIONS="${arg#*=}"
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

if [[ "$SILO_NUM" -eq 10 ]]; then
    if [[ "$DATASET" == "skewed" ]]; then
        COMM_ROUND=10
    else
        COMM_ROUND=28
    fi
else
    if [[ "$DATASET" == "glove" || "$DATASET" == "msturing" || "$DATASET" == "wikipedia" ]]; then
        COMM_ROUND=18
    elif [[ "$DATASET" == "skewed" ]]; then
        COMM_ROUND=10
    else
        COMM_ROUND=22
    fi
fi

# 检查必需参数
if [ -z "$DATASET" ]; then
    echo "Error: --dataset is required"
    usage
fi

# 将选项字符串转换为数组
IFS=',' read -ra OPTION_ARRAY <<< "$OPTIONS"

# 验证选项是否合法
VALID_OPTIONS=("greedy" "active" "batch-a" "batch-g")
for opt in "${OPTION_ARRAY[@]}"; do
    valid=0
    for valid_opt in "${VALID_OPTIONS[@]}"; do
        if [ "$opt" = "$valid_opt" ]; then
            valid=1
            break
        fi
    done
    if [ $valid -eq 0 ]; then
        echo "Error: Invalid option '$opt'. Valid options are: greedy, active, batch-a, batch-g"
        exit 1
    fi
done

echo "=========================================="
echo "Dataset: $DATASET"
echo "Silo Num: $SILO_NUM"
echo "Comm. Round: $COMM_ROUND"
echo "Options to test: ${OPTION_ARRAY[*]}"
echo "=========================================="

# 定义所有命令（使用变量替代固定的dataset）
# 定义命令数组
if [ $SILO_NUM -eq 10 ]; then
    # SILO_NUM=10 时的命令列表
    if [ "$DATASET" = "skewed" ]; then
        BASE_COMMANDS=(
            "--query-k=128 --efsearch=200"
            "--query-k=128 --efsearch=100"
            "--query-k=128 --efsearch=300"
            "--query-k=128 --efsearch=400"
            "--query-k=128 --efsearch=500"
        )
    else
        BASE_COMMANDS=(
            "--query-k=512 --efsearch=500"
            "--query-k=512 --efsearch=512"
            "--query-k=512 --efsearch=600"
            "--query-k=512 --efsearch=700"
            "--query-k=512 --efsearch=800"
            "--query-k=512 --efsearch=900"
        )
    fi
else
    # SILO_NUM=5 或其他值时的命令列表（可以根据需要调整）
    BASE_COMMANDS=(
        # "--query-k=1024 --efsearch=200"
        "--query-k=128 --efsearch=200"
        "--query-k=128 --efsearch=100"
        "--query-k=128 --efsearch=300"
        "--query-k=128 --efsearch=400"
        "--query-k=128 --efsearch=500"
        "--query-k=64 --efsearch=200"
        "--query-k=32 --efsearch=200"
        "--query-k=16 --efsearch=200"
        "--query-k=8 --efsearch=200"
        "--query-k=4 --efsearch=200"
        "--query-k=2 --efsearch=200"
    )
fi

# 创建日志目录
log_dir="logs_active_${DATASET}_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$log_dir"

# 计算总任务数
total_commands=$((${#BASE_COMMANDS[@]} * ${#OPTION_ARRAY[@]}))
current=0

# 执行每个命令
for opt in "${OPTION_ARRAY[@]}"; do
    echo ""
    echo "=========================================="
    echo "Testing option: $opt"
    echo "=========================================="
    
    for base_cmd in "${BASE_COMMANDS[@]}"; do
        current=$((current + 1))
        
        # 构建完整命令
        full_cmd="./Active_Server.sh $base_cmd --dataset=$DATASET --silo-num=$SILO_NUM --comm-round=$COMM_ROUND --option=$opt"
        
        echo "[$current/$total_commands] Executing: $full_cmd"
        
        # 从base_cmd中提取关键参数用于文件名（使用grep -oP，Ubuntu支持）
        query_k=$(echo "$base_cmd" | grep -oP '(?<=--query-k=)\d+')
        efsearch=$(echo "$base_cmd" | grep -oP '(?<=--efsearch=)\d+')
        log_file="$log_dir/run_k${query_k}_efs${efsearch}_${opt}_${DATASET}.log"
        
        # 执行命令并记录日志
        if eval "$full_cmd" > "$log_file" 2>&1; then
            echo "[$current/$total_commands] ✓ Success (option=$opt, k=$query_k, ef=$efsearch)"
        else
            echo "[$current/$total_commands] ✗ Failed (option=$opt, k=$query_k, ef=$efsearch, check $log_file)"
        fi
        
        # 可选：命令之间添加延迟
        # sleep 1
    done
done

echo ""
echo "=========================================="
echo "All commands executed for dataset: $DATASET"
echo "Options tested: ${OPTION_ARRAY[*]}"
echo "Total commands: $total_commands"
echo "Logs saved in: $log_dir/"
echo "=========================================="

# 可选：显示统计信息
echo ""
echo "Log files generated:"
ls -lh "$log_dir/" 2>/dev/null | tail -n +2 | wc -l
echo "Check $log_dir/ for details"
