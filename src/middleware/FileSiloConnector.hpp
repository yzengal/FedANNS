#ifndef MIDDLEWARE_FILE_SILO_CONNECTOR_HPP
#define MIDDLEWARE_FILE_SILO_CONNECTOR_HPP

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <thread>
#include <set>
#include <exception>
#include <signal.h>
#include <unistd.h>
#include <cstdio>
#include <filesystem>

#include "../database/VectorDB.hpp"
#include "../index/BaseIndex.hpp"
#include "../index/HnswIndex.hpp"
#include "../index/LinearScanIndex.hpp"
#include "../index/LshIndex.hpp"
#include "../index/PqIndex.hpp"
#include "../index/IvfIndex.hpp"
#include "../index/IvfPqIndex.hpp"
#include "../utils/DataType.hpp"
#include "../utils/MetricType.hpp"
#include "../utils/BenchLogger.hpp"
#include "../utils/File_IO.h"
#include "BaseSiloConnector.hpp"

template <typename Distance = EuclideanSquareDistance>
class FileSiloConnector final : public BaseSiloConnector<Distance> {
private:
    using BaseIndex_t = BaseIndex<Distance>;

public:
    FileSiloConnector(const int silo_id, const std::string& silo_ipaddr)
            : BaseSiloConnector<Distance>(silo_id, silo_ipaddr) {
        // BaseSiloConnector will init the logger
        m_index_ptr = nullptr;
    }

    FileSiloConnector(const int silo_id, const std::string& silo_ipaddr, const std::string& search_param_name, const int search_param_value)
            : BaseSiloConnector<Distance>(silo_id, silo_ipaddr), m_search_param_name(search_param_name), m_search_param_value(search_param_value) {
        // BaseSiloConnector will init the logger
        m_index_ptr = nullptr;
    }

    FileSiloConnector(const int silo_id, const std::string& silo_ipaddr, const std::string& db_name, const std::string& user_name, const std::string& password, const std::string& db_ipaddr, const std::string& db_port)
            : BaseSiloConnector<Distance>(silo_id, silo_ipaddr, db_name, user_name, password, db_ipaddr, db_port) {
        // BaseSiloConnector will init the logger
        // BaseSiloConnector will connect the database
        m_index_ptr = nullptr;
    }

    ~FileSiloConnector() override {

    }

    void ConnectDB(const std::string& db_name, const std::string& user_name, const std::string& password, const std::string& db_ipaddr, const std::string& db_port) override {
        /* 
            FiloSiloConnector loads data from file instead a real vector database,
            so there is no actual step to connect the database.
        */
        std::cout << "Connect FileDB at silo #(" << this->GetSiloId() <<  ") successfully." << std::endl;
    }

    // import the data from specific file
    void ImportData(const std::string& file_name) override {
        m_file_name = file_name;
        ReadVectorData(file_name, m_data_list);
    }

    // construct the index
    void ConstructIndex(const std::string& index_option) override {
        if (index_option.find("HNSW") != std::string::npos) {
            m_index_ptr = std::make_unique<HnswIndex<Distance>>(m_data_list);
        } else if (index_option.find("Linear") != std::string::npos) {
            m_index_ptr = std::make_unique<LinearScanIndex<Distance>>(m_data_list);
        } else if (index_option.find("LSH") != std::string::npos) {
            m_index_ptr = std::make_unique<LshIndex<Distance>>(m_data_list);
        } else if (index_option.find("IVFPQ") != std::string::npos) {
            m_index_ptr = std::make_unique<IvfPqIndex<Distance>>(m_data_list);
        } else if (index_option.find("PQ") != std::string::npos) {
            m_index_ptr = std::make_unique<PqIndex<Distance>>(m_data_list);
        } else if (index_option.find("IVF") != std::string::npos) {
            m_index_ptr = std::make_unique<IvfIndex<Distance>>(m_data_list);
        }else {// default setting
            m_index_ptr = std::make_unique<LinearScanIndex<Distance>>(m_data_list);
        }
        m_index_ptr->SetSearchParam(m_search_param_name, m_search_param_value);
    }

    // construct the index
    void ConstructIndexWithApprox(const std::string& index_options, bool approximation) override {
        if (index_options.find("HNSW") != std::string::npos) {
            m_index_ptr = std::make_unique<HnswIndex<Distance>>(m_data_list, approximation);
        } else if (index_options.find("FLAT") != std::string::npos) {
            m_index_ptr = std::make_unique<LinearScanIndex<Distance>>(m_data_list, approximation);
        } else {// default setting
            m_index_ptr = std::make_unique<LinearScanIndex<Distance>>(m_data_list, approximation);
        }
    }

    //Load the index
    void LoadIndex(const std::string& file_path, const std::string& index_option) override {
        std::string index_filename = GenIndexName(file_path, index_option);

        if (index_option.find("HNSW") != std::string::npos) {
            m_index_ptr = std::make_unique<HnswIndex<Distance>>(index_filename, m_data_list);
        } else if (index_option.find("FLAT") != std::string::npos) {
            m_index_ptr = std::make_unique<LinearScanIndex<Distance>>(index_filename, m_data_list);
        } else if (index_option.find("LSH") != std::string::npos) {
            m_index_ptr = std::make_unique<LshIndex<Distance>>(index_filename, m_data_list);
        } else if (index_option.find("IVFPQ") != std::string::npos) {
            m_index_ptr = std::make_unique<IvfPqIndex<Distance>>(index_filename, m_data_list);
        } else if (index_option.find("PQ") != std::string::npos) {
            m_index_ptr = std::make_unique<PqIndex<Distance>>(index_filename, m_data_list);
        } else if (index_option.find("IVF") != std::string::npos) {
            m_index_ptr = std::make_unique<IvfIndex<Distance>>(index_filename, m_data_list);
        } else {// default setting
            m_index_ptr = std::make_unique<LinearScanIndex<Distance>>(index_filename, m_data_list);
        }

        m_index_ptr->SetSearchParam(m_search_param_name, m_search_param_value);
    }

    void SetSearchParam(const std::string& search_param_name, const int search_param_value) override {
        m_search_param_name = search_param_name;
        m_search_param_value = search_param_value;
        m_index_ptr->SetSearchParam(m_search_param_name, m_search_param_value);
    }

    //Save the index
    void SaveIndex(const std::string& file_path, const std::string& index_option) override {
        m_index_ptr->SetSearchParam(m_search_param_name, m_search_param_value);

        if (IndexExists(file_path, index_option)) {
            std::cout << "Index file already exists" << std::endl;
        } else {
            std::string index_filename = GenIndexName(file_path, index_option);
            std::cout << "start to create index" << std::endl;
            m_index_ptr->SaveIndex(index_filename);
            std::cout << "Index saved to " << index_filename << std::endl;
        }
    }

    bool IndexExists(const std::string& file_path, const std::string& index_option) {
        std::string index_filename = GenIndexName(file_path, index_option);
        return (access(index_filename.c_str(), F_OK) == 0);
    }

    // perform the knn query, where exactness or approximation depends on the index
    std::vector<VectorDataType> KnnQuery(const VectorDataType& query_data, const size_t& query_k) override {
        if (m_index_ptr == nullptr) {
            throw std::invalid_argument("vector data index needs to be built before processing knn query");
            std::exit(EXIT_FAILURE);
        }

#ifdef LOCAL_DEBUG
        PrintLine(__LINE__);
        std::cout << "k = " << query_k << ", query_data = " << query_data.to_string() << std::endl;
#endif

        std::vector<VectorDataType> answer = m_index_ptr->KnnQuery(query_data, query_k);

        return answer;
    }

    // perform the knn query, where exactness or approximation depends on the index
    std::vector<VectorDataDistType> KnnQueryWithDist(const VectorDataType& query_data, const size_t& query_k) override {
        if (m_index_ptr == nullptr) {
            throw std::invalid_argument("vector data index needs to be built before processing knn query");
            std::exit(EXIT_FAILURE);
        }

#ifdef LOCAL_DEBUG
        PrintLine(__LINE__);
        std::cout << "k = " << query_k << ", query_data = " << query_data.to_string() << std::endl;
#endif

        std::vector<VectorDataDistType> answer = m_index_ptr->KnnQueryWithDist(query_data, query_k);

        return answer;
    }

    void KnnQuery(const VectorDataType& query_data, const size_t& query_k, std::vector<VectorDataType>& answer_object, std::vector<VectorDimensionType>& answer_dist) override {
        if (m_index_ptr == nullptr) {
            throw std::invalid_argument("vector data index needs to be built before processing knn query");
            std::exit(EXIT_FAILURE);
        }

#ifdef LOCAL_DEBUG
        PrintLine(__LINE__);
        std::cout << "k = " << query_k << ", query_data = " << query_data.to_string() << std::endl;
#endif

        m_index_ptr->KnnQuery(query_data, query_k, answer_object, answer_dist);
    }

    // Output the current index size
    size_t IndexSize() const override {
        if (m_index_ptr == nullptr)
            return 0;
        else
            return m_index_ptr->IndexSize();
    }

    // Output the current data size
    size_t DataSize() const override {
        return m_data_list.size();
    }

    
    std::string GenIndexName(const std::string& file_path, const std::string& index_option) {
        std::string index_filename = transformPath(file_path) + index_option + "index";
        return index_filename;
    }

private:
    std::string transformPath(const std::string& path) {
        size_t lastSlash = path.find_last_of('/');
        size_t lastDot = path.find_last_of('.');

        if (lastDot != std::string::npos && (lastSlash == std::string::npos || lastDot > lastSlash)) {
            return path.substr(0, lastDot+1);
        }

        return path;

        // std::stringstream ss(path);
        // std::string segment;
        // std::vector<std::string> split;

        // while (std::getline(ss, segment, '/')) {
        //     split.push_back(segment);
        // }

        // size_t len = split.size();

        // if (len > 1) {
        //     split[len - 1] = split[len - 2];
        // }

        // std::ostringstream joinedPath;
        // for (size_t i = 0; i < len; ++i) {
        //     joinedPath << split[i];
        //     if (i < len - 1) {
        //         joinedPath << "/";
        //     }
        // }

        // return joinedPath.str();
    }

    std::string m_file_name;
    std::vector<VectorDataType> m_data_list;
    std::unique_ptr<BaseIndex_t> m_index_ptr;
    std::string m_search_param_name;
    int m_search_param_value;
};

#endif  // MIDDLEWARE_FILE_SILO_CONNECTOR_HPP