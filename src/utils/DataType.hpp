#ifndef UTILS_DATA_TYPE_HPP
#define UTILS_DATA_TYPE_HPP

#include <array>
#include <vector>
#include <iomanip>
#include <cstddef>
#include <stdexcept>
#include <sstream>  
#include <string>
#include <iostream>
#include <algorithm>
#include <cstdlib>

typedef float VectorDimensionType;
typedef long VidType;
typedef long QidType;
typedef int SidType;

template<size_t dim>
using VectorPointType = std::array<VectorDimensionType, dim>;
   
struct VectorDataType {  
    VidType vid;  
    std::vector<VectorDimensionType> data;  

    VectorDataType(size_t dim=0, VidType _vid=0): vid(_vid) {
        data.reserve(dim);
        data.resize(dim);
        std::fill(data.begin(), data.end(), (VectorDimensionType)0);
    }

    VectorDataType(size_t dim, VidType _vid, const VectorDataType& vector_point): vid(_vid) {
        if (vector_point.Dimension() != dim) {
            throw std::invalid_argument("vector data dimension does not match");
            std::exit(EXIT_FAILURE);
        }
        data.reserve(dim);
        data.resize(dim);
        std::copy_n(vector_point.data.begin(), dim, data.begin());
    }

    VectorDataType(size_t dim, VidType _vid, const std::vector<VectorDimensionType>& arr) : vid(_vid) {
        if (arr.size() != dim) {
            throw std::invalid_argument("Data size does not match with vector data dimension");
            std::exit(EXIT_FAILURE);
        }
        data.reserve(dim);
        data.resize(dim);
        std::copy_n(arr.begin(), dim, data.begin());
    }

    void SetVid(VidType _vid) {
        vid = _vid;
    }

    void SetVectorPoint(const VectorDataType& vector_point) {
        const size_t dim = this->Dimension();
        if (vector_point.Dimension() != dim) {
            throw std::invalid_argument("vector data dimension does not match");
            std::exit(EXIT_FAILURE);
        }
        std::copy_n(vector_point.data.begin(), dim, data.begin());
    }

    void SetVectorPoint(const std::vector<VectorDimensionType>& arr) {
        const size_t dim = this->Dimension();
        if (arr.size() != dim) {
            throw std::invalid_argument("Data size does not match with vector data dimension");
            std::exit(EXIT_FAILURE);
        }
        std::copy_n(arr.begin(), dim, data.begin());
    }
  
    VectorDataType& operator=(const VectorDataType& other) {
        if (this != &other) {  
            const size_t dim = this->Dimension();
            if (other.Dimension() != dim) {
                throw std::invalid_argument("Data size does not match with vector data dimension");
                std::exit(EXIT_FAILURE);
            }
            vid = other.vid; 
            std::copy_n(other.data.begin(), dim, data.begin());
        }  
        return *this;  
    }  
  
    bool operator==(const VectorDataType& other) const {  
        if (vid != other.vid) return false;
        const size_t dim = this->Dimension();
        if (other.Dimension() != dim) return false;

        for (size_t i=0; i<dim; ++i) {
            if (other.data[i] != data[i])
                return false;
        }

        return true;  
    }  
   
    bool operator!=(const VectorDataType& other) const {  
        return !(*this == other);  
    }  
  
    VectorDimensionType& at(size_t k) {  
        if (k >= this->Dimension()) {  
            throw std::out_of_range("Index out of range");  
        }  
        return data[k];  
    }  

    const VectorDimensionType& at(size_t k) const {  
        if (k >= this->Dimension()) {  
            throw std::out_of_range("Index out of range");  
        }  
        return data[k];  
    }  
  
    VectorDimensionType& operator[](size_t k) {  
        if (k >= this->Dimension()) {  
            throw std::out_of_range("Index out of range");  
        }  
        return data[k];  
    }  

    const VectorDimensionType& operator[](size_t k) const {  
        if (k >= this->Dimension()) {  
            throw std::out_of_range("Index out of range");  
        }  
        return data[k];  
    } 

    size_t Dimension() const {
        return data.size();
    }

    std::string to_string(size_t print_size=4, size_t prec=6) const {
        const size_t dim = this->Dimension();
        std::stringstream ss;  

        ss << std::fixed << std::setprecision(prec);
        ss << "#" << vid << ": (";
        if (dim <= print_size*2) {
            for (size_t i=0; i<dim; ++i) {
                if (i > 0)
                    ss << ", " << data[i];
                else
                    ss << data[i];
            }
        } else {
            for (size_t i=0; i<print_size; ++i) {
                if (i > 0)
                    ss << ", " << data[i];
                else
                    ss << data[i];
            }
            ss << ", ...";
            for (size_t i=dim-print_size; i<dim; ++i) {
                ss << ", " << data[i];
            }
        }
        ss << ")";
        
        return ss.str();  
    } 
};

struct VectorDataDistType : public VectorDataType {
    VectorDimensionType dist;
    
    // 构造函数
    VectorDataDistType(size_t dim, VidType _vid=0) 
        : VectorDataType(dim, _vid), dist(0) {}
    
    VectorDataDistType(size_t dim, VidType _vid, const VectorDataType& vector_point) 
        : VectorDataType(dim, _vid, vector_point), dist(0) {}
    
    VectorDataDistType(size_t dim, VidType _vid, const std::vector<VectorDimensionType>& arr) 
        : VectorDataType(dim, _vid, arr), dist(0) {}
    
    // 从 VectorDataType 构造
    VectorDataDistType(const VectorDataType& other) 
        : VectorDataType(other.Dimension(), other.vid, other.data), dist(0) {}

    VectorDataDistType(const VectorDataType& other, VectorDimensionType _dist) 
        : VectorDataType(other.Dimension(), other.vid, other.data), dist(_dist) {}

    // 从 VectorDataDistType 构造
    VectorDataDistType(const VectorDataDistType& other) 
        : VectorDataType(other.Dimension(), other.vid, other.data), dist(other.dist) {}
    
    // 设置距离
    void SetDist(VectorDimensionType _dist) {
        dist = _dist;
    }
    
    VectorDimensionType GetDist() const {
        return dist;
    }
    
    // 距离比较
    bool operator<(const VectorDataDistType& other) const {
        return dist < other.dist;
    }
    
    bool operator>(const VectorDataDistType& other) const {
        return dist > other.dist;
    }
};

struct KnnQueryResult {
    QidType qid;
    std::vector<VectorDataDistType> knn;
};

struct DistSample {
    SidType silo_id;
    VectorDimensionType dist;

    DistSample() {}

    DistSample(SidType _silo_id, VectorDimensionType _dist): 
        silo_id(_silo_id), dist(_dist) {}

    // 小于运算符（std::set 必需）
    bool operator<(const DistSample& other) const {
        if (dist != other.dist) {
            return dist < other.dist;
        }
        return silo_id < other.silo_id;
    }

    // 大于运算符（直接复用小于的逻辑）
    bool operator>(const DistSample& other) const {
        return other < *this;  // 利用已实现的 operator<
    }

    // 等于运算符（可选，但推荐）
    bool operator==(const DistSample& other) const {
        return dist == other.dist && silo_id == other.silo_id;
    }

    // 不等于运算符（可选）
    bool operator!=(const DistSample& other) const {
        return !(*this == other);
    }

    // 小于等于（可选）
    bool operator<=(const DistSample& other) const {
        return !(other < *this);
    }

    // 大于等于（可选）
    bool operator>=(const DistSample& other) const {
        return !(*this < other);
    }
};

struct QueryConfig {
    int k = 200;
    int R = 8;
    int m = 4;
    int b_init = 1;
    double lambda = 0.3;
    double eta = 0.3;
    double g_init = 0.0;
    double T_max = 2.0;
    double T_min = 0.15;
    std::string search_param_name;
    int search_param_value;

    // ==================== 打印配置信息 ====================
    void print() const {
        std::cout << "\n[Configuration]" << std::endl;
        std::cout << "  k (Top-K): " << k << std::endl;
        std::cout << "  R (Max Rounds): " << R << std::endl;
        std::cout << "  m (Data Owners): " << m << std::endl;
        std::cout << "  b_init (Initial Budget): " << b_init << std::endl;
        std::cout << "  Lambda (Schedule adjust): " << lambda << std::endl;
        std::cout << "  Eta (EWMA learning rate): " << eta << std::endl;
        std::cout << "  T_min / T_max (Softmax temp): " << T_min << " / " << T_max << std::endl;
        std::cout << "  g_init (Initial score): " << g_init << std::endl;
        std::cout << "  T_min / T_max (Softmax temp): " << T_min << " / " << T_max << std::endl;
        if (!search_param_name.empty()) {
            std::cout << "  Search parameter name: " << search_param_name << std::endl;
            std::cout << "  Search parameter value: " << search_param_value << std::endl;
        }
    }
};

struct KnnJoinCandReqParam {
    QidType qid;
    size_t k;
    
    KnnJoinCandReqParam() : qid(0), k(0) {}
    
    KnnJoinCandReqParam(QidType _qid, size_t _k) : qid(_qid), k(_k) {}
};

#endif  // UTILS_DATA_TYPE_HPP
