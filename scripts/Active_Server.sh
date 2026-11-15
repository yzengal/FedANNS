#!/bin/bash

# 默认值
query_k=128
search_param_value=0
search_param_name=""
nprobe=0
dataset_name="audio"
silo_num=2  # 默认 silo 数量
algo_name="active"
option="greedy"  # 默认 option 值
comm_round=10
index_type="HNSW"

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
        --index-type=*)
            index_type="${1#*=}"
            ;;
        --index-type)
            shift
            if [[ $# -gt 0 && "$1" != --* ]]; then
                index_type="$1"
            else
                echo "Error: --index-type requires an argument"
                exit 1
            fi
            ;;
        --nprobe=*)
            nprobe="${1#*=}"
            ;;
        --nprobe)
            shift
            if [[ $# -gt 0 && "$1" != --* ]]; then
                nprobe="$1"
            else
                echo "Error: --nprobe requires an argument"
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
        --comm-round=*)
            comm_round="${1#*=}"
            ;;
        --comm-round)
            shift
            if [[ $# -gt 0 && "$1" != --* ]]; then
                comm_round="$1"
            else
                echo "Error: --comm_round requires an argument"
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
            echo "  --comm-round=<num>      Number of communication rounds (default: 10)"
            echo "  --option=<name>         Search option (default: greedy)"
            echo "  --help, -h              Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0 --query-k=100 --efsearch=200 --dataset=siftsmall"
            echo "  $0 --query-k=50 --efsearch=100 --dataset=gist"
            echo "  $0 --dataset=wikipedia --silo-num=5 --option=greedy"
            echo ""
            echo "Default values:"
            echo "  query-k: 128"
            echo "  efsearch: 200"
            echo "  dataset: siftsmall"
            echo "  silo-num: 2"
            echo "  option: greedy"
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

if [[ ! "$nprobe" =~ ^[0-9]+$ ]]; then
    echo "Error: nprobe must be a positive integer"
    exit 1
fi

if [[ ! "$silo_num" =~ ^[0-9]+$ ]]; then
    echo "Error: silo-num must be a positive integer"
    exit 1
fi

if [[ ! "$comm_round" =~ ^[0-9]+$ ]]; then
    echo "Error: comm-round must be a positive integer"
    exit 1
fi

# 检查 index_type 是否支持
case "$index_type" in
    FLAT|HNSW|IVF|PQ|IVFPQ)
        # 合法，继续
        ;;
    *)
        echo "Invalid --index-type: '$index_type'" >&2
        echo "  --index-type=<name>     Index Type: FLAT, HNSW, IVF, PQ, IVFPQ (default: HNSW)" >&2
        exit 1
        ;;
esac

# 根据索引类型决定搜索参数名和值
if [ "$index_type" = "HNSW" ]; then
    search_param_name="efsearch"
    search_param_value="${search_param_value}"   # 你原来的 search_param_value
elif [ "$index_type" = "IVF" ] || [ "$index_type" = "IVFPQ" ]; then
    search_param_name="nprobe"
    search_param_value="$nprobe"
else
    # FLAT / PQ 不需要搜索参数
    search_param_name=""
    search_param_value=0
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
if [ "$index_type" = "HNSW" ]; then
    output_path="/home/result/${dataset_name}/${silo_num}/${option}_${dataset_name}_ans_k${query_k}_efs${search_param_value}_comm${comm_round}_nc.txt"
elif [ "$index_type" = "IVF" ] || [ "$index_type" = "IVFPQ" ]; then
    output_path="/home/result/index/${dataset_name}/${silo_num}/${option}_${dataset_name}_ans_k${query_k}_index${index_type}_para${nprobe}_comm${comm_round}_nc.txt"
else
    output_path="/home/result/index/${dataset_name}/${silo_num}/${option}_${dataset_name}_ans_k${query_k}_index${index_type}_comm${comm_round}_nc.txt"
fi

# 创建输出目录（取 output_path 的目录部分）
mkdir -p "$(dirname "$output_path")"

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

# 构建命令
if [ -z "$search_param_name" ]; then
    cmd="./aserver --query-path=$query_path --silo-ip=$ip_path --output-path=$output_path --truth-path=$truth_path --option=$option --query-k=$query_k --comm-round=$comm_round"
else
    cmd="./aserver --query-path=$query_path --silo-ip=$ip_path --output-path=$output_path --truth-path=$truth_path --option=$option --query-k=$query_k --comm-round=$comm_round --search-param-name=$search_param_name --search-param-value=$search_param_value"
fi

# 打印执行信息
echo "=========================================="
echo "Starting KNN query evaluation"
echo "Dataset: $dataset_name"
echo "Silo num: $silo_num"
echo "Query K: $query_k"
echo "Comm Round: $comm_round"
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