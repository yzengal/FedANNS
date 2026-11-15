#!/bin/bash

# 默认值
query_k=128
search_param_value=200
dataset_name="audio"
silo_num=10  # 默认 silo 数量
option="PGA"  # 默认 option 值

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        --query-k=*)
            query_k="${1#*=}"
            ;;
        --query-k)
            shift
            if [[ $# -gt 0 && "$1" != --* ]]; then
                query_k="$1"
            else
                echo "Error: --query-k requires an argument"
                exit 1
            fi
            ;;
        --efsearch=*)
            search_param_value="${1#*=}"
            ;;
        --efsearch)
            shift
            if [[ $# -gt 0 && "$1" != --* ]]; then
                search_param_value="$1"
            else
                echo "Error: --efsearch requires an argument"
                exit 1
            fi
            ;;
        --dataset=*)
            dataset_name="${1#*=}"
            ;;
        --dataset)
            shift
            if [[ $# -gt 0 && "$1" != --* ]]; then
                dataset_name="$1"
            else
                echo "Error: --dataset requires an argument"
                exit 1
            fi
            ;;
        --silo-num=*)
            silo_num="${1#*=}"
            ;;
        --silo-num)
            shift
            if [[ $# -gt 0 && "$1" != --* ]]; then
                silo_num="$1"
            else
                echo "Error: --silo-num requires an argument"
                exit 1
            fi
            ;;
        --option=*)
            option="${1#*=}"
            ;;
        --option)
            shift
            if [[ $# -gt 0 && "$1" != --* ]]; then
                option="$1"
            else
                echo "Error: --option requires an argument"
                exit 1
            fi
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --query-k=<k>           Number of nearest neighbors to retrieve (default: 128)"
            echo "  --efsearch=<value>      EF search parameter value (default: 200)"
            echo "  --dataset=<name>        Dataset name (default: audio)"
            echo "  --silo-num=<num>        Number of silos (default: 2)"
            echo "  --option=<name>         Search option: PGA, batch-pga (default: PGA)"
            echo "  --help, -h              Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0 --query-k=100 --efsearch=200 --dataset=siftsmall"
            echo "  $0 --query-k=50 --efsearch=100 --dataset=gist"
            echo "  $0 --dataset=wikipedia --silo-num=5 --option=batch-pga"
            echo "  $0 --dataset=audio --option=PGA"
            echo ""
            echo "Default values:"
            echo "  query-k: 128"
            echo "  efsearch: 200"
            echo "  dataset: audio"
            echo "  silo-num: 2"
            echo "  option: PGA"
            exit 0
            ;;
        *)
            echo "Warning: Ignoring unknown argument: $1"
            ;;
    esac
    shift
done

# 验证参数
if [[ ! "$query_k" =~ ^[0-9]+$ ]]; then
    echo "Error: query-k must be a positive integer"
    exit 1
fi

if [[ ! "$search_param_value" =~ ^[0-9]+$ ]]; then
    echo "Error: efsearch must be a positive integer"
    exit 1
fi

if [[ ! "$silo_num" =~ ^[0-9]+$ ]]; then
    echo "Error: silo-num must be a positive integer"
    exit 1
fi

# 保存原始目录
ORIGINAL_DIR=$(pwd)

# 进入工作目录
cd ../src/build || {
    echo "Error: Cannot change to ../src/build directory"
    exit 1
}

# 构建路径（使用 dataset_name 和 silo_num）
query_path="/home/dataset/${dataset_name}/query.fvecs"
if [ "$dataset_name" == "skewed" ]; then
    truth_path="/home/dataset/${dataset_name}/k${silo_num}/${dataset_name}_gt_k${query_k}.ivecs"
else
    truth_path="/home/dataset/${dataset_name}/k/${dataset_name}_gt_k${query_k}.ivecs"
fi
ip_path="../../scripts/ip.txt"
output_path="/home/result/${dataset_name}/${silo_num}/${option}_${dataset_name}_ans_k${query_k}_efs${search_param_value}_nc.txt"

# 检查查询文件是否存在
if [[ ! -f "$query_path" ]]; then
    echo "Warning: Query file not found: $query_path"
    echo "Please check if dataset name is correct."
    echo "Expected path: $query_path"
fi

# 检查 ground truth 文件是否存在
if [[ ! -f "$truth_path" ]]; then
    echo "Warning: Ground truth file not found: $truth_path"
    echo "Please check if dataset name and query-k are correct."
    echo "Expected path: $truth_path"
fi

search_param_name="efsearch"

# 构建命令
cmd="./opt_server --query-path=$query_path --silo-ip=$ip_path --output-path=$output_path --truth-path=$truth_path --option=$option --query-k=$query_k --search-param-name=$search_param_name --search-param-value=$search_param_value"

# 打印执行信息
echo "=========================================="
echo "Starting KNN query evaluation"
echo "Dataset: $dataset_name"
echo "Silo num: $silo_num"
echo "Query K: $query_k"
echo "EF Search: $search_param_value"
echo "Option: $option"
echo "Query path: $query_path"
echo "Truth path: $truth_path"
echo "Output path: $output_path"
echo "Command: $cmd"
echo "=========================================="

# 执行命令
$cmd

# 检查执行结果
if [ $? -ne 0 ]; then  
    echo "❌ Query execution FAILED"
    echo "Failed command: $cmd"  
    exit 1  
fi 

echo "✅ Query execution completed successfully"
echo "Results saved to: $output_path"

# 返回原始目录
cd "$ORIGINAL_DIR"
