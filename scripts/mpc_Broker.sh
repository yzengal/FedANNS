#!/bin/bash

# 默认值
broker_port="50060"
server_name="BDA03"  # 默认服务器
silo_num=10           # 默认 silo 数量

# 服务器映射
declare -A SERVERS=(
    ["BDA02"]="172.21.0.8"
    ["BDA03"]="172.21.0.16"
    ["BDA05"]="172.21.0.15"
)

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
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
        --list-servers)
            echo "Available servers:"
            for server in "${!SERVERS[@]}"; do
                echo "  $server -> ${SERVERS[$server]}"
            done
            exit 0
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --server=<name>         Server name (BDA02, BDA03, BDA05) (default: BDA03)"
            echo "  --broker-port=<port>    Broker port number (default: 50049)"
            echo "  --silo-num=<num>        Number of silos (default: 5)"
            echo "  --list-servers          List all available server mappings"
            echo "  --help, -h              Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0 --server=BDA02"
            echo "  $0 --server=BDA03 --broker-port=60000"
            echo "  $0 --server=BDA05 --silo-num=3"
            echo "  $0 --list-servers"
            echo ""
            echo "Default values:"
            echo "  server: BDA03 -> ${SERVERS["BDA03"]}:$broker_port"
            echo "  silo-num: 5"
            exit 0
            ;;
        *)
            echo "Warning: Ignoring unknown argument: $1"
            ;;
    esac
    shift
done

# 验证服务器名称是否有效
if [[ -z "${SERVERS[$server_name]}" ]]; then
    echo "Error: Invalid server name '$server_name'"
    echo "Available servers:"
    for server in "${!SERVERS[@]}"; do
        echo "  $server -> ${SERVERS[$server]}"
    done
    exit 1
fi

# 根据服务器名称获取 IP
broker_ip="${SERVERS[$server_name]}"

# 验证参数
if [[ -z "$broker_port" ]]; then
    echo "Error: broker-port cannot be empty"
    exit 1
fi

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

# 构建命令
cmd="./Broker --ip=$broker_ip --port=${broker_port} --silo-num=$silo_num"

# 打印执行信息
echo "=========================================="
echo "Starting Broker Service"
echo "=========================================="
echo "Server: $server_name -> $broker_ip"
echo "Broker Port: $broker_port"
echo "Silo Num: $silo_num"
echo "Command: $cmd"
echo "=========================================="

# 执行命令
$cmd

# 检查执行结果
if [ $? -ne 0 ]; then  
    echo ""
    echo "❌ Broker execution FAILED"
    echo "Failed command: $cmd"  
    exit 1  
fi 

echo ""
echo "✅ Broker execution completed successfully"

# 返回原始目录
cd "$ORIGINAL_DIR"
