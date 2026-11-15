#include "File_IO.h"

#include <vector>
#include <fstream>
#include <iostream>
#include <cassert>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <iterator>
#include <set>

// Check the suffix of a specific file
static bool EndsWith(const std::string &str, const std::string &suffix);

// READ from .txt file
static void __ReadVectorDataFromTxt(const std::string &file_name, std::vector <VectorDataType> &data_list);

static void __ReadVectorQueryFromTxt(const std::string &file_name, std::vector <VectorDataType> &query_data_list,
                                     std::vector <size_t> &query_k_list);

static void __ReadGroundTruthFromTxt(const std::string &file_name, std::vector <std::vector<VidType>> &answer_list);

// DUMP into .txt file
static void __DumpVectorQueryToTxt(const std::string &file_name, const std::vector <VectorDataType> &query_data_list,
                                   const std::vector <size_t> &query_k_list);

static void __DumpVectorDataToTxt(const std::string &file_name, const std::vector <VectorDataType> &data_list);

static void __DumpGroundTruthToTxt(const std::string &file_name, const std::vector <std::vector<VidType>> &answer_list);

// READ from .fvecs, .bvecs, .ivecs file
static void __ReadVectorDataFromFvecs(const std::string &file_name, std::vector <VectorDataType> &data_list);

static void __ReadVectorDataFromBvecs(const std::string &file_name, std::vector <VectorDataType> &data_list);

static void __ReadVectorQueryFromFvecs(const std::string &file_name, std::vector <VectorDataType> &query_data_list,
                                       std::vector <size_t> &query_k_list);

static void __ReadVectorQueryFromBvecs(const std::string &file_name, std::vector <VectorDataType> &query_data_list,
                                       std::vector <size_t> &query_k_list);

static void __ReadGroundTruthFromIvecs(const std::string &file_name, std::vector <std::vector<VidType>> &answer_list);

static void __DumpVectorQueryToFvecs(const std::string &file_name, const std::vector <VectorDataType> &query_data_list,
                                     const std::vector <size_t> &query_k_list);

static void __DumpVectorDataToFvecs(const std::string &file_name, const std::vector <VectorDataType> &data_list);

static void
__DumpGroundTruthToIvecs(const std::string &file_name, const std::vector <std::vector<VidType>> &answer_list);


/// both vid and data are stored for each vector data
static void __ReadVectorDataFromFivecs(const std::string &file_name, std::vector <VectorDataType> &data_list);

static void __DumpVectorDataToFivecs(const std::string &file_name, const std::vector <VectorDataType> &data_list);

//READ from .fbin .bin
static void __ReadVectorDataFromFbin(const std::string &file_name, std::vector <VectorDataType> &data_list);

static void __ReadVectorQueryFromFbin(const std::string &file_name, std::vector <VectorDataType> &query_data_list,
                                      std::vector <size_t> &query_k_list);

static void __ReadGroundTruthFromBin(const std::string &file_name, std::vector <std::vector<VidType>> &answer_list);

static void __DumpGroundTruthToBin(const std::string &file_name, std::vector <VectorDataType> &data_list);


static bool EndsWith(const std::string &str, const std::string &suffix) {
    if (suffix.size() > str.size()) {
        return false;
    }
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void ReadVectorData(const std::string &file_name, std::vector <VectorDataType> &data_list) {
    if (EndsWith(file_name, ".txt")) {
        __ReadVectorDataFromTxt(file_name, data_list);
    } else if (EndsWith(file_name, ".fvecs")) {
        __ReadVectorDataFromFvecs(file_name, data_list);
    } else if (EndsWith(file_name, ".fivecs")) {
        __ReadVectorDataFromFivecs(file_name, data_list);
    } else if (EndsWith(file_name, ".bvecs")) {
        __ReadVectorDataFromBvecs(file_name, data_list);
    } else {
        __ReadVectorDataFromFbin(file_name, data_list);
    }
}

void DumpVectorData(const std::string &file_name, const std::vector <VectorDataType> &data_list) {
    if (EndsWith(file_name, ".txt")) {
        __DumpVectorDataToTxt(file_name, data_list);
    } else if (EndsWith(file_name, ".fvecs")) {
        __DumpVectorDataToFvecs(file_name, data_list);
    } else if (EndsWith(file_name, ".fivecs")) {
        __DumpVectorDataToFivecs(file_name, data_list);
    } else {
        std::cerr << "Unknown file type: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

void ReadVectorQuery(const std::string& file_name, std::vector<VectorDataType>& query_data_list,
                     std::vector<size_t>& query_k_list) {
    if (EndsWith(file_name, ".txt")) {
        __ReadVectorQueryFromTxt(file_name, query_data_list, query_k_list);
    } else if (EndsWith(file_name, ".fvecs")) {
        __ReadVectorQueryFromFvecs(file_name, query_data_list, query_k_list);
    } else if (EndsWith(file_name, ".bvecs")) {
        __ReadVectorQueryFromBvecs(file_name, query_data_list, query_k_list);
    } else if (EndsWith(file_name, ".fbin") || EndsWith(file_name, ".bin")) {
        __ReadVectorQueryFromFbin(file_name, query_data_list, query_k_list);
    } else {
        std::cerr << "Unknown file type: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

void DumpVectorQuery(const std::string &file_name, const std::vector <VectorDataType> &query_data_list,
                     const std::vector <size_t> &query_k_list) {
    if (EndsWith(file_name, ".txt")) {
        __DumpVectorQueryToTxt(file_name, query_data_list, query_k_list);
    } else if (EndsWith(file_name, ".fvecs")) {
        __DumpVectorQueryToFvecs(file_name, query_data_list, query_k_list);
    } else {
        std::cerr << "Unknown file type: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

void ReadGroundTruth(const std::string &file_name, std::vector <std::vector<VidType>> &answer_list) {
    if (EndsWith(file_name, ".txt")) {
        __ReadGroundTruthFromTxt(file_name, answer_list);
    } else if (EndsWith(file_name, ".ivecs")) {
        __ReadGroundTruthFromIvecs(file_name, answer_list);
    } else if (EndsWith(file_name, ".bin")) {
        __ReadGroundTruthFromBin(file_name, answer_list);
    } else {
        std::cerr << "Unknown file type: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

void DumpGroundTruth(const std::string& file_name, const std::vector <std::vector<VidType>> &answer_list) {
    if (EndsWith(file_name, ".txt")) {
        __DumpGroundTruthToTxt(file_name, answer_list);
    } else if (EndsWith(file_name, ".ivecs")) {
        __DumpGroundTruthToIvecs(file_name, answer_list);
    } else {
        std::cerr << "Unknown file type: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

void DumpGroundTruth(const std::string& file_name, const std::vector<std::vector<VidType>>& answer_list, std::string result_log) {
    DumpGroundTruth(file_name, answer_list);

    if (EndsWith(file_name, ".txt") && !result_log.empty()) {
        std::ofstream outfile;
        outfile.open(file_name, std::ios::app);

        if (outfile.is_open()) {
            outfile << result_log;
            outfile.close();
        } else {
            throw std::runtime_error("Cannot open file: " + file_name);
        }
    }
}

void ReadSiloIPaddr(const std::string &file_name, std::vector <std::string> &silo_ipaddr_list) {
    std::ifstream file(file_name);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading data silos' IP addresses: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    size_t n;
    if (!(file >> n)) {
        std::cerr << "Failed to read n from file: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    silo_ipaddr_list.resize(n);
    for (size_t i = 0; i < n; ++i) {
        if (!(file >> silo_ipaddr_list[i])) {
            std::cerr << "Failed to read data silos' IP addresses from line " << (i + 2) << " in file: " << file_name
                      << std::endl;
            std::exit(EXIT_FAILURE);
        }
    }
    file.close();
}

void DumpSiloIPaddr(const std::string &file_name, const std::vector <std::string> &silo_ipaddr_list) {
    std::ofstream file(file_name);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading data silos' IP addresses: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    size_t n = silo_ipaddr_list.size();

    file << n << "\n";
    for (auto ipaddr: silo_ipaddr_list) {
        file << ipaddr << "\n";
    }
    file.close();
}

void SplitVectorData(const std::string& file_name, const int silo_num, const std::string& des_file_path,
                     const std::vector<VectorDataType> data_list) {
    std::string file_path = des_file_path;
    size_t last_slash_pos = file_name.find_last_of('/');

    if (file_path.empty()) {
        if (last_slash_pos == std::string::npos) {
            file_path = "./";
        } else {
            file_path = file_name.substr(0, last_slash_pos);
        }
    }

    std::string file_name_token;
    std::string file_name_prefix, file_name_suffix;

    {
        if (last_slash_pos == std::string::npos) {
            file_name_token = file_name;
        } else {
            file_name_token = file_name.substr(last_slash_pos + 1);
        }

        std::cout << "file_path: " << file_path << ", file_name = " << file_name_token << std::endl;
        size_t last_dot_pos = file_name_token.find_last_of('.');
        if (last_dot_pos == std::string::npos) {
            file_name_prefix = file_name_token;
            file_name_suffix = "";
        } else {
            file_name_prefix = file_name_token.substr(0, last_dot_pos);
            file_name_suffix = file_name_token.substr(last_dot_pos);
        }
        std::cout << "file_name_prefix: " << file_name_prefix << ", file_name_suffix = " << file_name_suffix
                  << std::endl;
    }

    if (silo_num <= 0) {
        std::string error_message;
        error_message = std::string("Invalid silo_num = ") + std::to_string(silo_num) + std::string("when splitting");
        throw std::invalid_argument(error_message);
    }

    size_t data_size = data_list.size();
    size_t silo_data_size = data_size / silo_num;
    size_t cur_silo_data_size, idx = 0;

    for (int i = 1; i <= silo_num; ++i) {
        cur_silo_data_size = silo_data_size;
        if (i <= data_size % silo_num) {
            ++cur_silo_data_size;
        }

        std::vector<VectorDataType> silo_data_list(data_list.begin() + idx,
                                                    data_list.begin() + idx + cur_silo_data_size);
        idx += cur_silo_data_size;

        std::string des_file_name;
//        if (file_name_suffix == ".fvecs") {
        des_file_name = file_name_prefix + std::string("_") + std::to_string(i - 1) + std::string(".fivecs");
//        } else {
//            des_file_name = file_name_prefix + std::string("_") + std::to_string(i-1) + file_name_suffix;
//        }

        des_file_name = file_path + std::string("/") + des_file_name;
        DumpVectorData(des_file_name, silo_data_list);

        std::cout << "Dump Silo #" << i - 1 << "'s " << std::to_string(cur_silo_data_size) << " vector data into "
                  << des_file_name << std::endl;
    }
}

void SplitVectorDataForDigData(const std::string &file_name, const int silo_num, const std::string &des_file_path) {
    std::string file_path = des_file_path;
    size_t last_slash_pos = file_name.find_last_of('/');

    if (file_path.empty()) {
        if (last_slash_pos == std::string::npos) {
            file_path = "./";
        } else {
            file_path = file_name.substr(0, last_slash_pos);
        }
    }

    std::string file_name_token;
    std::string file_name_prefix, file_name_suffix;

    {
        if (last_slash_pos == std::string::npos) {
            file_name_token = file_name;
        } else {
            file_name_token = file_name.substr(last_slash_pos + 1);
        }

        std::cout << "file_path: " << file_path << ", file_name = " << file_name_token << std::endl;
        size_t last_dot_pos = file_name_token.find_last_of('.');
        if (last_dot_pos == std::string::npos) {
            file_name_prefix = file_name_token;
            file_name_suffix = "";
        } else {
            file_name_prefix = file_name_token.substr(0, last_dot_pos);
            file_name_suffix = file_name_token.substr(last_dot_pos);
        }
        std::cout << "file_name_prefix: " << file_name_prefix << ", file_name_suffix = " << file_name_suffix
                  << std::endl;
    }

    if (silo_num <= 0) {
        std::string error_message;
        error_message = std::string("Invalid silo_num = ") + std::to_string(silo_num) + std::string("when splitting");
        throw std::invalid_argument(error_message);
    }

    std::ifstream file(file_name, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading vector data: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }
    VidType vid = 0;
    int32_t nvecs, dim;
    file.read(reinterpret_cast<char *>(&nvecs), sizeof(int32_t));
    file.read(reinterpret_cast<char *>(&dim), sizeof(int32_t));
    std::cout << "nvecs: " << nvecs << ", dim: " << dim << std::endl;
    size_t silo_data_size = nvecs / silo_num;
    std::cout << "silo_data_size: " << silo_data_size << std::endl;
    std::vector<VectorDataType> silo_data_list;
    for (int i = 1; i <= silo_num; ++i) {
        size_t cur_silo_data_size = silo_data_size;
        if (i <= nvecs % silo_num) {
            ++cur_silo_data_size;
        }
        std::vector<float> arr((cur_silo_data_size * dim));
        std::cout << "start" << std::endl;
        file.read(reinterpret_cast<char *>(arr.data()), cur_silo_data_size * dim * sizeof(float));
        if (!file) {
            throw std::runtime_error("Error reading file");
        }
        for (int i = 0; i < cur_silo_data_size; ++i) {
            VectorDataType vector_data(dim, vid++);
            for (int j = 0; j < dim; ++j) {
                vector_data.data[j] = arr[i * dim + j];
            }
            silo_data_list.emplace_back(vector_data);
        }
        std::cout << "finish" << std::endl;
        std::string des_file_name;
        des_file_name = file_name_prefix + std::string("_") + std::to_string(i - 1) + std::string(".fivecs");
        des_file_name = file_path + std::string("/") + des_file_name;
        std::ofstream file_out(des_file_name, std::ios::binary);
        if (!file_out.is_open()) {
            std::cerr << "Failed to open file for writing vector data: " << des_file_name << std::endl;
            std::exit(EXIT_FAILURE);
        }
        int32_t sub_nvecs = static_cast<int32_t>(silo_data_list.size());
        file_out.write(reinterpret_cast<const char *>(&sub_nvecs), sizeof(int32_t));
        file_out.write(reinterpret_cast<const char *>(&dim), sizeof(int32_t));
        for (const auto &vector_data: silo_data_list) {
            file_out.write(reinterpret_cast<const char *>(&vector_data.vid), sizeof(VidType));
            file_out.write(reinterpret_cast<const char *>(vector_data.data.data()), dim * sizeof(VectorDimensionType));
        }
        file_out.close();
        silo_data_list.clear();
    }
    file.close();
}

void SplitVectorDataForDigData_new(const std::string &file_name, const int silo_num, const std::string &des_file_path) {
    std::string file_path = des_file_path;
    size_t last_slash_pos = file_name.find_last_of('/');

    if (file_path.empty()) {
        if (last_slash_pos == std::string::npos) {
            file_path = "./";
        } else {
            file_path = file_name.substr(0, last_slash_pos);
        }
    }

    std::string file_name_token;
    std::string file_name_prefix, file_name_suffix;

    {
        if (last_slash_pos == std::string::npos) {
            file_name_token = file_name;
        } else {
            file_name_token = file_name.substr(last_slash_pos + 1);
        }

        std::cout << "file_path: " << file_path << ", file_name = " << file_name_token << std::endl;
        size_t last_dot_pos = file_name_token.find_last_of('.');
        if (last_dot_pos == std::string::npos) {
            file_name_prefix = file_name_token;
            file_name_suffix = "";
        } else {
            file_name_prefix = file_name_token.substr(0, last_dot_pos);
            file_name_suffix = file_name_token.substr(last_dot_pos);
        }
        std::cout << "file_name_prefix: " << file_name_prefix << ", file_name_suffix = " << file_name_suffix
                  << std::endl;
    }

    if (silo_num <= 0) {
        std::string error_message;
        error_message = std::string("Invalid silo_num = ") + std::to_string(silo_num) + std::string(" when splitting");
        throw std::invalid_argument(error_message);
    }

    std::ifstream file(file_name, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading vector data: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    // 读取fvecs格式：先读第一个向量获取维度，并计算总向量数
    int32_t dim = 0;
    file.read(reinterpret_cast<char*>(&dim), sizeof(int32_t));
    if (!file) {
        std::cerr << "Failed to read first vector dimension" << std::endl;
        file.close();
        return;
    }
    
    // 计算每个向量的字节数：dim(4字节) + data(dim * 4字节)
    size_t bytes_per_vector = sizeof(int32_t) + dim * sizeof(float);
    
    // 获取文件总大小
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // 计算总向量数
    size_t total_vectors = file_size / bytes_per_vector;
    std::cout << "File size: " << file_size << " bytes" << std::endl;
    std::cout << "Bytes per vector: " << bytes_per_vector << " bytes" << std::endl;
    std::cout << "Total vectors: " << total_vectors << ", dimension: " << dim << std::endl;
    
    // 重置到文件开头重新读取
    file.clear();
    file.seekg(0, std::ios::beg);

    // 计算每个silo的数据量
    size_t silo_data_size = total_vectors / silo_num;
    size_t remainder = total_vectors % silo_num;
    std::cout << "silo_data_size: " << silo_data_size << ", remainder: " << remainder << std::endl;
    
    // 流式处理：逐个读取向量并分发到对应的silo
    std::vector<float> buffer(dim);
    VidType vid = 0;
    size_t vector_idx = 0;
    
    // 为每个silo准备输出文件（保持fivecs格式）
    std::vector<std::ofstream> output_files;
    std::vector<size_t> silo_counts(silo_num, 0);
    
    for (int i = 0; i < silo_num; ++i) {
        std::string des_file_name;
        des_file_name = file_name_prefix + std::string("_") + std::to_string(i) + std::string(".fivecs");
        des_file_name = file_path + std::string("/") + des_file_name;
        
        output_files.emplace_back(des_file_name, std::ios::binary);
        if (!output_files[i].is_open()) {
            std::cerr << "Failed to open file for writing: " << des_file_name << std::endl;
            std::exit(EXIT_FAILURE);
        }
    }
    
    // 记录每个silo的起始位置，用于后续写入头部
    std::vector<std::streampos> header_positions(silo_num);
    std::vector<size_t> silo_vector_counts(silo_num, 0);
    
    // 先为每个silo文件预留头部空间（nvecs和dim）
    for (int i = 0; i < silo_num; ++i) {
        // 先写入占位数据
        int32_t placeholder_nvecs = 0;
        int32_t placeholder_dim = dim;
        output_files[i].write(reinterpret_cast<const char*>(&placeholder_nvecs), sizeof(int32_t));
        output_files[i].write(reinterpret_cast<const char*>(&placeholder_dim), sizeof(int32_t));
        header_positions[i] = 0; // 记录头部位置
    }
    
    // 流式读取fvecs并写入对应的fivecs文件
    while (file) {
        int32_t current_dim;
        file.read(reinterpret_cast<char*>(&current_dim), sizeof(int32_t));
        if (file.eof()) break;
        
        if (current_dim != dim) {
            std::cerr << "Warning: Dimension mismatch at vector " << vector_idx 
                      << ": expected " << dim << ", got " << current_dim << std::endl;
            // 跳过这个向量
            file.seekg(current_dim * sizeof(float), std::ios::cur);
            continue;
        }
        
        // 读取向量数据
        file.read(reinterpret_cast<char*>(buffer.data()), dim * sizeof(float));
        if (!file) {
            throw std::runtime_error("Error reading file at vector " + std::to_string(vector_idx));
        }
        
        // 计算这个向量应该属于哪个silo
        int target_silo = 0;
        size_t cumulative = 0;
        for (int i = 0; i < silo_num; ++i) {
            size_t current_silo_size = silo_data_size + (i < remainder ? 1 : 0);
            if (vector_idx < cumulative + current_silo_size) {
                target_silo = i;
                break;
            }
            cumulative += current_silo_size;
        }
        
        // 写入到对应的silo文件（fivecs格式：先vid，再data）
        output_files[target_silo].write(reinterpret_cast<const char*>(&vid), sizeof(VidType));
        output_files[target_silo].write(reinterpret_cast<const char*>(buffer.data()), 
                                        dim * sizeof(float));
        
        silo_vector_counts[target_silo]++;
        vector_idx++;
        vid++;
        
        // 进度提示
        if (vector_idx % 100000 == 0) {
            std::cout << "Processed " << vector_idx << " vectors..." << std::endl;
        }
    }
    
    file.close();
    
    // 更新每个文件的头部信息（nvecs）
    for (int i = 0; i < silo_num; ++i) {
        // 移动到文件开头
        output_files[i].seekp(0, std::ios::beg);
        // 写入实际的向量数量
        int32_t actual_nvecs = static_cast<int32_t>(silo_vector_counts[i]);
        output_files[i].write(reinterpret_cast<const char*>(&actual_nvecs), sizeof(int32_t));
        // dim已经在开头写过了，不需要再写
        
        output_files[i].close();
        
        std::cout << "Silo " << i << ": " << silo_vector_counts[i] << " vectors" << std::endl;
    }
    
    std::cout << "\n=== Split Complete ===" << std::endl;
    std::cout << "Total vectors processed: " << vector_idx << std::endl;
}

void EvaluateAnswer(const std::string &answer_filename, const std::string &truth_filename) {
    std::vector <std::vector<VidType>> answer_list;
    std::vector <std::vector<VidType>> truth_list;

    ReadGroundTruth(answer_filename, answer_list);
    ReadGroundTruth(truth_filename, truth_list);

    size_t query_num = answer_list.size();

    if (query_num != truth_list.size()) {
        std::cerr << "Evaluation answer with sizes different from ground truth: " << answer_list.size() << " vs "
                  << truth_list.size() << std::endl;
        std::exit(EXIT_FAILURE);
    }

    double avg_recall = 0.0;

    for (size_t i = 0; i < query_num; ++i) {
        std::set <VidType> truth_set(truth_list[i].begin(), truth_list[i].end());
        double recall = 0.0;

        for (VidType vid: answer_list[i]) {
            if (truth_set.count(vid) > 0) {
                recall += 1.0;
            }
        }
        if (truth_list[i].size() > 0)
            recall /= truth_list[i].size();

        avg_recall += recall;

        std::cout << "Query #" << i + 1 << ": recall = " << std::fixed << std::setprecision(6) << recall << std::endl;
    }

    if (query_num > 0)
        avg_recall /= query_num;

    std::cout << query_num << " queries: recall = " << std::fixed << std::setprecision(6) << avg_recall << " per query"
              << std::endl;
}

double EvaluateAnswerRecall(const std::string& answer_filename, const std::string& truth_filename) {
    std::vector<std::vector<VidType>> answer_list;
    std::vector<std::vector<VidType>> truth_list;

    ReadGroundTruth(answer_filename, answer_list);
    ReadGroundTruth(truth_filename, truth_list);

    size_t query_num = answer_list.size();

    if (query_num != truth_list.size()) {
        std::cerr << "Evaluation answer with sizes different from ground truth: " << answer_list.size() << " vs "
                  << truth_list.size() << std::endl;
        std::exit(EXIT_FAILURE);
    }

    double avg_recall = 0.0;

    for (size_t i = 0; i < query_num; ++i) {
        std::set <VidType> truth_set(truth_list[i].begin(), truth_list[i].end());
        double recall = 0.0;

        for (VidType vid: answer_list[i]) {
            if (truth_set.count(vid) > 0) {
                recall += 1.0;
            }
        }
        if (truth_list[i].size() > 0)
            recall /= truth_list[i].size();

        avg_recall += recall;

        std::cout << "Query #" << i + 1 << ": recall = " << std::fixed << std::setprecision(6) << recall << std::endl;
    }

    if (query_num > 0)
        avg_recall /= query_num;

    std::cout << query_num << " queries: recall = " << std::fixed << std::setprecision(6) << avg_recall << " per query"
              << std::endl;

    return avg_recall;
}

double EvaluateAnswerMRR(const std::string& answer_filename,
                         const std::string& truth_filename) {
    std::vector<std::vector<VidType>> answer_list;
    std::vector<std::vector<VidType>> truth_list;

    ReadGroundTruth(answer_filename, answer_list);
    ReadGroundTruth(truth_filename, truth_list);

    size_t query_num = answer_list.size();

    if (query_num != truth_list.size()) {
        std::cerr << "Evaluation answer with sizes different from ground truth: "
                  << answer_list.size() << " vs " << truth_list.size() << std::endl;
        std::exit(EXIT_FAILURE);
    }

    double avg_mrr = 0.0;

    for (size_t i = 0; i < query_num; ++i) {
        const auto& answer = answer_list[i];
        const auto& truth = truth_list[i];

        // truth_set = set(truth)
        std::set<VidType> truth_set(truth.begin(), truth.end());

        double reciprocal_rank = 0.0;

        // for idx, vid in enumerate(answer):
        //     if vid in truth_set:
        //         return 1.0 / (idx + 1)
        for (size_t idx = 0; idx < answer.size(); ++idx) {
            if (truth_set.count(answer[idx]) > 0) {
                reciprocal_rank = 1.0 / static_cast<double>(idx + 1);
                break;  // 找到第一个命中就停止
            }
        }
        // 无命中时 reciprocal_rank 保持 0.0

        avg_mrr += reciprocal_rank;

        std::cout << "Query #" << i + 1 << ": MRR = " << std::fixed
                  << std::setprecision(6) << reciprocal_rank << std::endl;
    }

    if (query_num > 0) {
        avg_mrr /= static_cast<double>(query_num);
    }

    std::cout << query_num << " queries: MRR = " << std::fixed
              << std::setprecision(6) << avg_mrr << " per query" << std::endl;

    return avg_mrr;
}

double EvaluateAnswerNDCG(const std::string& answer_filename,
                          const std::string& truth_filename,
                          size_t k = 0) {  // k=0 表示 None，即 k=len(truth)
    std::vector<std::vector<VidType>> answer_list;
    std::vector<std::vector<VidType>> truth_list;

    ReadGroundTruth(answer_filename, answer_list);
    ReadGroundTruth(truth_filename, truth_list);

    size_t query_num = answer_list.size();

    if (query_num != truth_list.size()) {
        std::cerr << "Evaluation answer with sizes different from ground truth: "
                  << answer_list.size() << " vs " << truth_list.size() << std::endl;
        std::exit(EXIT_FAILURE);
    }

    double avg_ndcg = 0.0;

    for (size_t i = 0; i < query_num; ++i) {
        const auto& answer = answer_list[i];
        const auto& truth = truth_list[i];

        // truth_set = set(truth)
        std::set<VidType> truth_set(truth.begin(), truth.end());

        // if k is None: k = len(truth)
        size_t cur_k = (k == 0) ? truth.size() : k;

        // k = min(k, len(answer))
        cur_k = std::min(cur_k, answer.size());

        // dcg = 0.0
        // for idx in range(k):
        //     gain = 1.0 if answer[idx] in truth_set else 0.0
        //     dcg += gain / math.log2(idx + 2)
        double dcg = 0.0;
        for (size_t idx = 0; idx < cur_k; ++idx) {
            double gain = (truth_set.count(answer[idx]) > 0) ? 1.0 : 0.0;
            dcg += gain / std::log2(static_cast<double>(idx + 2));
        }

        // ideal_k = min(k, len(truth))
        size_t ideal_k = std::min(cur_k, truth.size());

        // idcg = sum(1.0 / math.log2(i + 2) for i in range(ideal_k))
        double idcg = 0.0;
        for (size_t idx = 0; idx < ideal_k; ++idx) {
            idcg += 1.0 / std::log2(static_cast<double>(idx + 2));
        }

        // return dcg / idcg if idcg > 0 else 0.0
        double ndcg = (idcg > 0.0) ? (dcg / idcg) : 0.0;

        avg_ndcg += ndcg;

        std::cout << "Query #" << i + 1 << ": NDCG@" << cur_k << " = "
                  << std::fixed << std::setprecision(6) << ndcg
                  << " (DCG=" << std::setprecision(6) << dcg
                  << ", IDCG=" << std::setprecision(6) << idcg << ")"
                  << std::endl;
    }

    if (query_num > 0) {
        avg_ndcg /= static_cast<double>(query_num);
    }

    std::cout << query_num << " queries: NDCG@average = " << std::fixed
              << std::setprecision(6) << avg_ndcg << " per query" << std::endl;

    return avg_ndcg;
}

static void __ReadVectorDataFromTxt(const std::string &file_name, std::vector <VectorDataType> &data_list) {
    std::ifstream file(file_name);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading vector data: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    std::string line;
    std::getline(file, line);
    std::istringstream iss(line);
    size_t n, dim;
    if (!(iss >> n >> dim)) {
        std::cerr << "Failed to read n and dim from file: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    data_list.clear();
    data_list.resize(n, VectorDataType(dim));

    for (int i = 0; i < n; ++i) {
        std::getline(file, line);
        std::istringstream line_iss(line);
        if (!(line_iss >> data_list[i].vid)) {
            std::cerr << "Failed to read vector_data.vid from line " << (i + 2) << " in file: " << file_name
                      << std::endl;
            std::exit(EXIT_FAILURE);
        }

        data_list[i].data.reserve(dim);
        data_list[i].data.resize(dim);
        for (int j = 0; j < dim; ++j) {
            if (!(line_iss >> data_list[i].data[j])) {
                std::cerr << "Failed to read vector_data.data from line " << (i + 2) << " in file: " << file_name
                          << std::endl;
                std::exit(EXIT_FAILURE);
            }
        }
    }

    file.close();
}

static void __DumpVectorDataToTxt(const std::string &file_name, const std::vector <VectorDataType> &data_list) {
    std::ofstream file(file_name);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for dumping vector data: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    size_t n = data_list.size(), dim = 1;
    if (n == 0) {
        file << n << " " << dim << "\n";
        file.close();
        return;
    }

    dim = data_list[0].Dimension();
    file << n << " " << dim << "\n";
    for (const auto &vector_data: data_list) {
        file << vector_data.vid;
        for (auto d: vector_data.data) {
            file << " " << d;
        }
        file << "\n";
    }

    file.close();
}

static void __ReadVectorQueryFromTxt(const std::string &file_name, std::vector <VectorDataType> &query_data_list,
                                     std::vector <size_t> &query_k_list) {
    std::ifstream file(file_name);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading vector queries: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    std::string line;
    std::getline(file, line);
    std::istringstream iss(line);
    size_t n, dim;
    if (!(iss >> n >> dim)) {
        std::cerr << "Failed to read n and dim from file: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    query_data_list.clear();
    query_data_list.resize(n, VectorDataType(dim));
    query_k_list.clear();
    query_k_list.resize(n);

    for (int i = 0; i < n; ++i) {
        std::getline(file, line);
        std::istringstream line_iss(line);

        query_data_list[i].vid = i;
        query_data_list[i].data.reserve(dim);
        query_data_list[i].data.resize(dim);
        for (int j = 0; j < dim; ++j) {
            if (!(line_iss >> query_data_list[i].data[j])) {
                std::cerr << "Failed to read vector_query.data from line " << (i + 2) << " in file: " << file_name
                          << std::endl;
                std::exit(EXIT_FAILURE);
            }
        }

        if (!(line_iss >> query_k_list[i])) {
            std::cerr << "Failed to read vector_query.k from line " << (i + 2) << " in file: " << file_name
                      << std::endl;
            std::exit(EXIT_FAILURE);
        }
        if (i == 100) break;
    }

    file.close();
}

static void __DumpVectorQueryToTxt(const std::string &file_name, const std::vector <VectorDataType> &query_data_list,
                                   const std::vector <size_t> &query_k_list) {
    std::ofstream file(file_name);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for dumping vector queries: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    size_t n = query_data_list.size(), dim = 1;
    if (n == 0) {
        file << n << " " << dim << "\n";
        file.close();
        return;
    }

    dim = query_data_list[0].Dimension();
    file << n << " " << dim << "\n";
    for (size_t i = 0; i < n; ++i) {
        for (auto d: query_data_list[i].data) {
            file << d << " ";
        }
        file << query_k_list[i] << "\n";
    }

    file.close();
}

static void __ReadGroundTruthFromTxt(const std::string &file_name, std::vector <std::vector<VidType>> &answer_list) {
    std::ifstream file(file_name);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading ground truth: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    std::string line;
    std::getline(file, line);
    std::istringstream iss(line);
    size_t n, k;
    if (!(iss >> n)) {
        std::cerr << "Failed to read n from file: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    answer_list.clear();
    for (int i = 0; i < n; ++i) {
        std::getline(file, line);
        std::istringstream line_iss(line);
        if (!(line_iss >> k)) {
            std::cerr << "Failed to read ground truth k from line " << (i + 2) << " in file: " << file_name
                      << std::endl;
            std::exit(EXIT_FAILURE);
        }

        std::vector <VidType> answer(k);
        answer.reserve(k);
        for (int j = 0; j < k; ++j) {
            if (!(line_iss >> answer[j])) {
                std::cerr << "Failed to read ground truth vid from line " << (i + 2) << " in file: " << file_name
                          << std::endl;
                std::exit(EXIT_FAILURE);
            }
        }
        answer_list.emplace_back(answer);
    }

    file.close();
}

static void
__DumpGroundTruthToTxt(const std::string &file_name, const std::vector <std::vector<VidType>> &answer_list) {
    std::ofstream file(file_name);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for dumping vector queries: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    size_t n = answer_list.size(), k = 0;

    file << n << "\n";
    for (size_t i = 0; i < n; ++i) {
        file << answer_list[i].size();
        for (auto vid: answer_list[i]) {
            file << " " << vid;
        }
        file << "\n";
    }
    file.close();
}

static void __ReadVectorDataFromFvecs(const std::string &file_name, std::vector <VectorDataType> &data_list) {
    std::ifstream file(file_name, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading vector data: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    data_list.clear();
    VidType vid = 0;
    while (file) {
        int32_t dim;
        file.read(reinterpret_cast<char *>(&dim), sizeof(int32_t));
        if (file.eof()) break; // End of file reached

        std::vector<float> vec(dim);
        file.read(reinterpret_cast<char *>(vec.data()), dim * sizeof(float));
        if (!file) {
            throw std::runtime_error("Error reading file");
        }

        VectorDataType vector_data(dim, vid++);
        for (int i = 0; i < dim; ++i)
            vector_data.data[i] = vec[i];
        data_list.emplace_back(vector_data);
    }
    file.close();
}

static void __ReadVectorDataFromFbin(const std::string& file_name, std::vector<VectorDataType> &data_list) {
    std::ifstream file(file_name, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading vector data: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    data_list.clear();
    VidType vid = 0;
    int32_t nvecs, dim;
    file.read(reinterpret_cast<char *>(&dim), sizeof(int32_t));
    file.read(reinterpret_cast<char *>(&nvecs), sizeof(int32_t));
    std::vector<float> arr((nvecs * dim));
    file.read(reinterpret_cast<char *>(arr.data()), nvecs * dim * sizeof(float));
    if (!file) {
        throw std::runtime_error("Error reading file");
    }
    for (int i = 0; i < nvecs; ++i) {
        VectorDataType vector_data(dim, vid++);
        for (int j = 0; j < dim; ++j) {
            vector_data.data[j] = arr[i * dim + j];
        }
        data_list.emplace_back(vector_data);
    }
    file.close();
}


static void __ReadVectorDataFromBvecs(const std::string &file_name, std::vector <VectorDataType> &data_list) {
    std::ifstream file(file_name, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading vector data: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    data_list.clear();
    VidType vid = 0;
    while (file) {
        int32_t dim;
        file.read(reinterpret_cast<char *>(&dim), sizeof(int32_t));
        if (file.eof()) break; // End of file reached

        std::vector <uint8_t> vec(dim);
        file.read(reinterpret_cast<char *>(vec.data()), dim);
        if (!file) {
            throw std::runtime_error("Error reading file");
        }

        VectorDataType vector_data(dim, vid++);
        for (int i = 0; i < dim; ++i)
            vector_data.data[i] = vec[i];
        data_list.emplace_back(vector_data);
    }
    file.close();
}

static void __ReadVectorDataFromFivecs(const std::string &file_name, std::vector <VectorDataType> &data_list) {
    std::ifstream file(file_name, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading vector data: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    data_list.clear();

    int32_t nvecs, dim;
    file.read(reinterpret_cast<char *>(&nvecs), sizeof(int32_t));
    file.read(reinterpret_cast<char *>(&dim), sizeof(int32_t));
    std::cout << "Read data: size = " << nvecs << ", dimension = " << dim << std::endl;

    for (int i = 0; i < nvecs; ++i) {
        VidType vid;
        file.read(reinterpret_cast<char *>(&vid), sizeof(VidType));
        std::vector<float> vec(dim);
        file.read(reinterpret_cast<char *>(vec.data()), dim * sizeof(float));
        if (!file) {
            throw std::runtime_error("Error reading file");
        }

        VectorDataType vector_data(dim, vid);
        for (int j = 0; j < dim; ++j)
            vector_data.data[j] = vec[j];
        data_list.emplace_back(vector_data);
    }
    file.close();
}


static void __ReadVectorQueryFromFvecs(const std::string &file_name, std::vector <VectorDataType> &query_data_list,
                                       std::vector <size_t> &query_k_list) {
    std::ifstream file(file_name, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading vector query: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    query_data_list.clear();
    VidType vid = 0;
    while (file) {
        int32_t dim;
        file.read(reinterpret_cast<char *>(&dim), sizeof(int32_t));
        if (file.eof()) break; // End of file reached

        std::vector<float> vec(dim);
        file.read(reinterpret_cast<char *>(vec.data()), dim * sizeof(float));
        if (!file) {
            throw std::runtime_error("Error reading file");
        }

        VectorDataType vector_data(dim, vid++);
        for (int i = 0; i < dim; ++i)
            vector_data.data[i] = vec[i];
        query_data_list.emplace_back(vector_data);
        // if (vid == 100) break;
    }
    file.close();

    query_k_list.resize(query_data_list.size());
    if (file_name.find("bigann") != std::string::npos) {
        std::fill(query_k_list.begin(), query_k_list.end(), (size_t) 1000);
    } else {
        std::fill(query_k_list.begin(), query_k_list.end(), (size_t) 100);
    }
}

static void __ReadVectorQueryFromFbin(const std::string &file_name, std::vector <VectorDataType> &query_data_list,
                                      std::vector <size_t> &query_k_list) {
    std::ifstream file(file_name, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading vector query: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    query_data_list.clear();
    VidType vid = 0;
    int32_t nvecs, dim;
    file.read(reinterpret_cast<char *>(&dim), sizeof(int32_t));
    file.read(reinterpret_cast<char *>(&nvecs), sizeof(int32_t));
    std::vector<float> arr(nvecs * dim);
    file.read(reinterpret_cast<char *>(arr.data()), nvecs * dim * sizeof(float));
    if (!file) {
        throw std::runtime_error("Error reading file");
    }
    for (int i = 0; i < nvecs; ++i) {
        VectorDataType vector_data(dim, vid++);
        for (int j = 0; j < dim; ++j) {
            vector_data.data[j] = arr[i * dim + j];
        }
        query_data_list.emplace_back(vector_data);
        if (i == 100) break;
    }
    file.close();

    query_k_list.resize(query_data_list.size());
    if (file_name.find("bigann") != std::string::npos) {
        std::fill(query_k_list.begin(), query_k_list.end(), (size_t) 1000);
    } else {
        std::fill(query_k_list.begin(), query_k_list.end(), (size_t) 100);
    }
}


static void __ReadVectorQueryFromBvecs(const std::string &file_name, std::vector <VectorDataType> &query_data_list,
                                       std::vector <size_t> &query_k_list) {
    std::ifstream file(file_name, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading vector query: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    query_data_list.clear();
    VidType vid = 0;
    while (file) {
        int32_t dim;
        file.read(reinterpret_cast<char *>(&dim), sizeof(int32_t));
        if (file.eof()) break; // End of file reached

        std::vector <uint8_t> vec(dim);
        file.read(reinterpret_cast<char *>(vec.data()), dim);
        if (!file) {
            throw std::runtime_error("Error reading file");
        }

        VectorDataType vector_data(dim, vid++);
        for (int i = 0; i < dim; ++i)
            vector_data.data[i] = vec[i];
        query_data_list.emplace_back(vector_data);
        if (vid == 100) break;
    }
    file.close();

    query_k_list.resize(query_data_list.size());
    if (file_name.find("bigann") != std::string::npos) {
        std::fill(query_k_list.begin(), query_k_list.end(), (size_t) 1000);
    } else {
        std::fill(query_k_list.begin(), query_k_list.end(), (size_t) 100);
    }
}

static void __ReadGroundTruthFromIvecs(const std::string &file_name, std::vector <std::vector<VidType>> &answer_list) {
    std::ifstream file(file_name, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading ground truth: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    answer_list.clear();
    while (file) {
        int32_t dim;
        file.read(reinterpret_cast<char *>(&dim), sizeof(int32_t));
        if (file.eof()) break; // End of file reached

        std::vector<int32_t> vec(dim);
        file.read(reinterpret_cast<char *>(vec.data()), dim * sizeof(int32_t));
        if (!file) {
            throw std::runtime_error("Error reading file");
        }

        std::vector <VidType> answer(dim);
        for (int i = 0; i < dim; ++i)
            answer[i] = vec[i];
        answer_list.emplace_back(answer);
    }
    file.close();
}

static void __ReadGroundTruthFromBin(const std::string &file_name, std::vector <std::vector<VidType>> &answer_list) {
    std::ifstream file(file_name, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading ground truth: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    answer_list.clear();
    int32_t n, d;
    file.read(reinterpret_cast<char *>(&n), sizeof(int32_t));
    file.read(reinterpret_cast<char *>(&d), sizeof(int32_t));
    std::streampos fileSize = file.tellg();
    file.seekg(0, std::ios::end);
    fileSize = file.tellg() - fileSize;
    assert(fileSize == n * d * (4 + 4));
    file.seekg(8, std::ios::beg);
    std::vector <int32_t> arr((n * d));
    file.read(reinterpret_cast<char *>(arr.data()), n * d * sizeof(int32_t));
    if (!file) {
        throw std::runtime_error("Error reading file");
    }
    for (int i = 0; i < n; ++i) {
        std::vector <VidType> answer(d);
        for (int j = 0; j < d; ++j) {
            answer[j] = arr[i * d + j];
        }
        answer_list.emplace_back(answer);
    }
    file.close();
}


static void __DumpVectorQueryToFvecs(const std::string &file_name, const std::vector <VectorDataType> &query_data_list,
                                     const std::vector <size_t> &query_k_list) {
    std::ofstream file(file_name, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing vector query: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    for (const auto &vector_data: query_data_list) {
        int32_t dim = static_cast<int32_t>(vector_data.data.size());
        file.write(reinterpret_cast<const char *>(&dim), sizeof(int32_t));
        file.write(reinterpret_cast<const char *>(vector_data.data.data()), dim * sizeof(VectorDimensionType));
    }

    file.close();
}

static void __DumpVectorDataToFvecs(const std::string &file_name, const std::vector <VectorDataType> &data_list) {
    std::ofstream file(file_name, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing vector data: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }
    int i = 0;
    for (const auto &vector_data: data_list) {
        int32_t dim = static_cast<int32_t>(vector_data.data.size());
        file.write(reinterpret_cast<const char *>(&dim), sizeof(int32_t));
        file.write(reinterpret_cast<const char *>(vector_data.data.data()), dim * sizeof(VectorDimensionType));
        i++;
        std::cout << "Writing ground truth: " << i << std::endl;
        if (i == 100) break;
    }
    file.close();
}

static void __DumpVectorDataToFivecs(const std::string &file_name, const std::vector <VectorDataType> &data_list) {
    std::ofstream file(file_name, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing vector data: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    int32_t nvecs = static_cast<int32_t>(data_list.size());
    file.write(reinterpret_cast<const char *>(&nvecs), sizeof(int32_t));
    int32_t dim = static_cast<int32_t>(data_list[0].data.size());
    file.write(reinterpret_cast<const char *>(&dim), sizeof(int32_t));
    for (const auto &vector_data: data_list) {
        file.write(reinterpret_cast<const char *>(&vector_data.vid), sizeof(VidType));
        file.write(reinterpret_cast<const char *>(vector_data.data.data()), dim * sizeof(VectorDimensionType));
    }
    file.close();
}

static void
__DumpGroundTruthToIvecs(const std::string &file_name, const std::vector <std::vector<VidType>> &answer_list) {
    std::ofstream file(file_name, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing ground truth: " << file_name << std::endl;
        std::exit(EXIT_FAILURE);
    }

    int i = 0;
    for (const auto &answer: answer_list) {
        std::vector <int32_t> vec;
        for (auto vid: answer)
            vec.emplace_back(vid);

        int32_t k = static_cast<int32_t>(answer.size());
        file.write(reinterpret_cast<const char *>(&k), sizeof(int32_t));
        file.write(reinterpret_cast<const char *>(vec.data()), k * sizeof(int32_t));
        i++;
        std::cout << "Writing ground truth: " << i << std::endl;
        if (i == 100) break;
    }
    file.close();
}

void MergeFivecs(const std::string& file_a, const std::string& file_b, const std::string& file_c) {
    // 读取第一个文件
    std::vector<VectorDataType> data_list_a;
    __ReadVectorDataFromFivecs(file_a, data_list_a);
    
    // 读取第二个文件
    std::vector<VectorDataType> data_list_b;
    __ReadVectorDataFromFivecs(file_b, data_list_b);
    
    // 检查维度是否一致
    if (!data_list_a.empty() && !data_list_b.empty()) {
        if (data_list_a[0].Dimension() != data_list_b[0].Dimension()) {
            std::cerr << "Error: Dimension mismatch between files!" << std::endl;
            std::cerr << "File A dimension: " << data_list_a[0].Dimension() << std::endl;
            std::cerr << "File B dimension: " << data_list_b[0].Dimension() << std::endl;
            std::exit(EXIT_FAILURE);
        }
    }
    
    // 合并两个vector
    std::vector<VectorDataType> merged_data;
    merged_data.reserve(data_list_a.size() + data_list_b.size());
    
    // 先添加文件A的数据（保持vid不变）
    merged_data.insert(merged_data.end(), data_list_a.begin(), data_list_a.end());
    
    // 再添加文件B的数据（保持vid不变）
    merged_data.insert(merged_data.end(), data_list_b.begin(), data_list_b.end());
    
    // 写入合并后的文件（fivecs格式）
    std::ofstream file_out(file_c, std::ios::binary);
    if (!file_out.is_open()) {
        std::cerr << "Failed to open file for writing: " << file_c << std::endl;
        std::exit(EXIT_FAILURE);
    }
    
    // 写入头部：nvecs和dim
    int32_t nvecs = static_cast<int32_t>(merged_data.size());
    int32_t dim = merged_data.empty() ? 0 : merged_data[0].Dimension();
    
    file_out.write(reinterpret_cast<const char*>(&nvecs), sizeof(int32_t));
    file_out.write(reinterpret_cast<const char*>(&dim), sizeof(int32_t));
    
    // 写入每个向量
    for (const auto& vector_data : merged_data) {
        // 写入vid
        file_out.write(reinterpret_cast<const char*>(&vector_data.vid), sizeof(VidType));
        // 写入向量数据
        file_out.write(reinterpret_cast<const char*>(vector_data.data.data()), 
                      dim * sizeof(float));
        if (!file_out) {
            throw std::runtime_error("Error writing to file: " + file_c);
        }
    }
    
    file_out.close();
    
    std::cout << "Merged " << data_list_a.size() << " + " << data_list_b.size() 
              << " = " << merged_data.size() << " vectors into " << file_c << std::endl;
    std::cout << "Dimension: " << dim << std::endl;
}

// 辅助函数：读取文件头部，返回 nvecs 和 dim，并返回文件流（已定位到数据起始位置）
static bool ReadHeaderAndSeekToData(const std::string& filename, int32_t& nvecs, int32_t& dim, std::ifstream& file) {
    file.open(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return false;
    }
    file.read(reinterpret_cast<char*>(&nvecs), sizeof(int32_t));
    file.read(reinterpret_cast<char*>(&dim), sizeof(int32_t));
    if (!file) {
        std::cerr << "Failed to read header from: " << filename << std::endl;
        return false;
    }
    // 此时文件指针位于数据部分起始位置（跳过头部 8 字节）
    return true;
}

void MergeFivecsBigData(const std::string& file_a, const std::string& file_b, const std::string& file_c) {
    int32_t nvecs_a = 0, dim_a = 0;
    int32_t nvecs_b = 0, dim_b = 0;
    std::ifstream in_a, in_b;

    // 1. 读取两个文件的头部并验证
    if (!ReadHeaderAndSeekToData(file_a, nvecs_a, dim_a, in_a)) return;
    if (!ReadHeaderAndSeekToData(file_b, nvecs_b, dim_b, in_b)) return;

    if (dim_a != dim_b) {
        std::cerr << "Dimension mismatch: A=" << dim_a << ", B=" << dim_b << std::endl;
        return;
    }

    // 2. 写入输出文件头部
    std::ofstream out(file_c, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Failed to create output file: " << file_c << std::endl;
        return;
    }
    int32_t total_nvecs = nvecs_a + nvecs_b;
    int32_t dim = dim_a;
    out.write(reinterpret_cast<const char*>(&total_nvecs), sizeof(int32_t));
    out.write(reinterpret_cast<const char*>(&dim), sizeof(int32_t));
    if (!out) {
        std::cerr << "Failed to write header to: " << file_c << std::endl;
        return;
    }

    // 3. 复制数据部分的函数（使用 100MB 缓冲区）
    auto copy_data = [&](std::ifstream& in, size_t bytes_to_copy) {
        const size_t BUFFER_SIZE = 100 * 1024 * 1024; // 100MB
        std::vector<char> buffer(BUFFER_SIZE);
        while (bytes_to_copy > 0) {
            size_t to_read = std::min(bytes_to_copy, BUFFER_SIZE);
            in.read(buffer.data(), to_read);
            if (!in) {
                std::cerr << "Error reading source file" << std::endl;
                return false;
            }
            out.write(buffer.data(), to_read);
            if (!out) {
                std::cerr << "Error writing to output file" << std::endl;
                return false;
            }
            bytes_to_copy -= to_read;
        }
        return true;
    };

    // 计算每个文件数据部分的总字节数（每个向量 = vid + dim * float）
    size_t bytes_per_vector = sizeof(VidType) + dim * sizeof(float);
    size_t bytes_a = nvecs_a * bytes_per_vector;
    size_t bytes_b = nvecs_b * bytes_per_vector;

    // 复制文件 A 的数据
    if (!copy_data(in_a, bytes_a)) return;
    // 复制文件 B 的数据
    if (!copy_data(in_b, bytes_b)) return;

    in_a.close();
    in_b.close();
    out.close();

    std::cout << "Merged " << nvecs_a << " + " << nvecs_b << " = " << total_nvecs
              << " vectors into " << file_c << std::endl;
    std::cout << "Dimension: " << dim << std::endl;
}