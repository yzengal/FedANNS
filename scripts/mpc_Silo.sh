#!/bin/bash

# 默认值
silo_id=""
dataset_name="audio"
silo_num=5  # 默认 silo 数量
ip_path="../../scripts/ip.txt"
broker_host="BDA03"  # 默认 broker 主机名
broker_port=50060    # 默认 broker 端口

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        --id=*)
            silo_id="${1#*=}"
            ;;
        --id)
            shift
            if [[ $# -gt 0 && "$1" != --* ]]; then
                silo_id="$1"
            else
                echo "Error: --id requires an argument"
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
        --ip=*)
            server_ip="${1#*=}"
            ;;
        --ip)
            shift
            if [[ $# -gt 0 && "$1" != --* ]]; then
                server_ip="$1"
            else
                echo "Error: --ip requires an argument"
                exit 1
            fi
            ;;
        --server=*)
            server_name="${1#*=}"
            ;;
        --server)
            shift
            if [[ $# -gt 0 && "$1" != --* ]]; then
                server_name="$1"
            else
                echo "Error: --server requires an argument"
                exit 1
            fi
            ;;
        --broker=*)
            broker_host="${1#*=}"
            ;;
        --broker)
            shift
            if [[ $# -gt 0 && "$1" != --* ]]; then
                broker_host="$1"
            else
                echo "Error: --broker requires an argument"
                exit 1
            fi
            ;;
        --broker-port=*)
            broker_port="${1#*=}"
            ;;
        --broker-port)
            shift
            if [[ $# -gt 0 && "$1" != --* ]]; then
                broker_port="$1"
            else
                echo "Error: --broker-port requires an argument"
                exit 1
            fi
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS] [silo_id]"
            echo ""
            echo "Options:"
            echo "  --id=<id>               Silo ID (required)"
            echo "  --dataset=<name>        Dataset name (default: audio)"
            echo "  --silo-num=<num>        Number of silos (default: 5)"
            echo "  --ip=<ip>               Server IP address (direct specify)"
            echo "  --server=<name>         Server name: BDA02, BDA03, BDA05 (default: BDA03)"
            echo "  --broker=<name>         Broker hostname: BDA02, BDA03, BDA05 (default: BDA03)"
            echo "  --broker-port=<port>    Broker port (default: 50049)"
            echo "  --help, -h              Show this help message"
            echo ""
            echo "Server IP mapping:"
            echo "  BDA02 -> 172.21.0.8"
            echo "  BDA03 -> 172.21.0.16"
            echo "  BDA05 -> 172.21.0.15"
            echo ""
            echo "Examples:"
            echo "  $0 --id=0 --dataset=siftsmall"
            echo "  $0 --id=1 --dataset=gist --silo-num=5"
            echo "  $0 --id=0 --server=BDA02 --broker=BDA05"
            echo "  $0 --id=0 --ip=192.168.1.100 --broker=BDA03"
            echo "  $0 --id=0 --broker=BDA02 --broker-port=50050"
            echo "  $0 0                    # uses default dataset (audio), server BDA03, broker BDA03"
            exit 0
            ;;
        *)
            # 如果没有 --id 参数，将第一个位置参数作为 silo_id
            if [[ -z "$silo_id" ]]; then
                silo_id="$1"
            else
                echo "Warning: Ignoring extra argument: $1"
            fi
            ;;
    esac
    shift
done

# 检查必需的参数
if [[ -z "$silo_id" ]]; then
    echo "Error: silo_id is required"
    echo "Usage: $0 --id=<id> [--dataset=<name>] [--silo-num=<num>]"
    exit 1
fi

# 验证 silo_num
if [[ ! "$silo_num" =~ ^[0-9]+$ ]] || [[ "$silo_num" -le 0 ]]; then
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

# 计算端口
silo_id=$((silo_id))
port=$((50050 + ${silo_id}))

# 构建数据路径（使用 dataset_name 和 silo_num 参数）
data_path="/home/dataset/${dataset_name}/${silo_num}/${dataset_name}_base_${silo_id}.fivecs"

# 检查数据文件是否存在
if [[ ! -f "$data_path" ]]; then
    echo "Warning: Data file not found: $data_path"
    echo "Please check if dataset name and silo-num are correct."
    echo "Expected path: /home/dataset/${dataset_name}/${silo_num}/${dataset_name}_base_${silo_id}.fivecs"
fi

# 固定参数
index_type="HNSW"
search_param_name="efsearch"
search_param_value=200

# 服务器映射
declare -A SERVERS=(
    ["BDA02"]="172.21.0.8"
    ["BDA03"]="172.21.0.16"
    ["BDA05"]="172.21.0.15"
)

# 确定服务器 IP
if [[ -n "$server_ip" ]]; then
    # 直接指定 IP
    final_ip="$server_ip"
    server_info="Custom IP: $server_ip"
elif [[ -n "$server_name" ]] && [[ -n "${SERVERS[$server_name]}" ]]; then
    # 通过服务器名称
    final_ip="${SERVERS[$server_name]}"
    server_info="Server: $server_name -> $final_ip"
else
    # 默认使用 BDA03
    final_ip="${SERVERS[BDA03]}"
    server_info="Server: BDA03 (default) -> $final_ip"
fi

# 确定 Broker IP
if [[ -n "${SERVERS[$broker_host]}" ]]; then
    broker_ip="${SERVERS[$broker_host]}"
    broker_info="Broker: $broker_host -> $broker_ip:$broker_port"
else
    # 如果 broker_host 不在映射中，直接使用该值作为 IP
    broker_ip="$broker_host"
    broker_info="Broker: $broker_ip:$broker_port (custom)"
fi

# 构建完整的 broker 地址
broker_addr="${broker_ip}:${broker_port}"

# 构建命令
cmd="./opt_silo --id=$silo_id --ip=$final_ip --port=$port --data-path=$data_path --silo-ip=$ip_path --index-type=$index_type --3rd-ipaddr=$broker_addr"

# 打印执行信息
echo "=========================================="
echo "Starting Silo Service"
echo "=========================================="
echo "Silo ID: $silo_id"
echo "Dataset: $dataset_name"
echo "Silo Num: $silo_num"
echo "Data path: $data_path"
echo "Server: $server_info"
echo "IP Address: $final_ip"
echo "Port: $port"
echo "Broker: $broker_info"
echo "Index type: $index_type"
echo "Search param: $search_param_name=$search_param_value"
echo "=========================================="
echo "Command: $cmd"
echo "=========================================="

# 执行命令
$cmd

# 检查执行结果
if [ $? -ne 0 ]; then  
    echo ""
    echo "❌ Silo ${silo_id} FAILED"
    echo "Failed command: $cmd"  
    exit 1  
fi 

echo ""
echo "✅ Silo ${silo_id} completed successfully"

# 返回原始目录
cd "$ORIGINAL_DIR"
