#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <cctype>
#include <cstdlib>
#include <random>
#include <set>
#include <utility>
#include <exception>
#include <signal.h>
#include <unistd.h>

#include <boost/asio.hpp>  
#include <boost/asio/thread_pool.hpp>
#include <boost/program_options.hpp>
namespace bpo = boost::program_options;

#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>

#include "FedVectorDBQ.grpc.pb.h"

#include "database/VectorDB.hpp"
#include "index/BaseIndex.hpp"
#include "utils/DataType.hpp"
#include "utils/MetricType.hpp"
#include "utils/BenchLogger.hpp"
#include "utils/File_IO.h"
#include "utils/Allocator.hpp"
#include "middleware/BaseSiloConnector.hpp"
#include "middleware/FileSiloConnector.hpp"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using google::protobuf::Empty;

using FedVectorDBQ::KnnSql;
using FedVectorDBQ::KnnJoinSql;
using FedVectorDBQ::KnnCandReq;
using FedVectorDBQ::KnnJoinCandReq;
using FedVectorDBQ::VectorData;
using FedVectorDBQ::KnnCand;
using FedVectorDBQ::KnnJoinCand;
using FedVectorDBQ::SearchParam;
using FedVectorDBQ::FedSqlService;

using Distance = EuclideanSquareDistance;


class SiloReceiver {
public:
    SiloReceiver(std::shared_ptr<grpc::Channel> channel, const int silo_id, const std::string& silo_ipaddr) 
        : m_stub_(FedSqlService::NewStub(channel)), m_silo_id(silo_id), m_silo_ipaddr(silo_ipaddr) {
        
        m_logger.Init();
    }

    void RequestKnnCand(const VectorDataType& query_data, const size_t& query_k) {
        ClientContext context;
        Empty response;
        KnnSql query;

        query.set_vid(query_data.vid);
        query.set_k(query_k);
        query.mutable_data()->Assign(query_data.data.begin(), query_data.data.end());
        // for (auto d : query_data.data) {
        //     query.add_data(d);
        // }

        // std::cout << "Silo " << m_silo_id << ": ``RequestKnnCand``" << std::endl;

        Status status = m_stub_->ExecuteKnnQuery(&context, query, &response); 
        if (!status.ok()) {
            std::cerr << "RPC failed: " << status.error_message() << std::endl;
            std::string error_message;
            error_message = std::string("Request Knn candidates from data silo #(") + std::to_string(m_silo_id) + std::string(") failed");
            throw std::invalid_argument(error_message);  
            std::exit(EXIT_FAILURE);
        }

        float grpc_comm = query.ByteSizeLong() + response.ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);
    }

    void RequestKnnJoinCand(const std::vector<VectorDataType>& query_data_list, const std::vector<size_t>& query_k_list) {
        ClientContext context;
        Empty response;
        KnnJoinSql query;

        // 预分配 repeated 字段的空间（避免多次重新分配）
        query.mutable_k()->Reserve(query_data_list.size());
        query.mutable_queries()->Reserve(query_data_list.size());
        
        for (size_t i = 0; i < query_data_list.size(); ++i) {
            const auto& query_vector = query_data_list[i];
            const size_t query_k = query_k_list[i];
            query.add_k(query_k);
            auto* vec = query.add_queries();
            vec->set_vid(query_vector.vid);
            // 对 data 字段使用 Assign（批量赋值 float）
            vec->mutable_data()->Assign(query_vector.data.begin(), query_vector.data.end());
        }

        // std::cout << "Silo " << m_silo_id << ": ``RequestKnnJoinCand``" << std::endl;

        Status status = m_stub_->ExecuteKnnJoin(&context, query, &response); 
        if (!status.ok()) {
            std::cerr << "RPC failed: " << status.error_message() << std::endl;
            std::string error_message;
            error_message = std::string("Execute Knn Join in data silo #(") + std::to_string(m_silo_id) + std::string(") failed");
            throw std::invalid_argument(error_message);  
            std::exit(EXIT_FAILURE);
        }

        float grpc_comm = query.ByteSizeLong() + response.ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);
    }

    void RequestPartialKnn(const size_t cand_num) {
        ClientContext context;
        KnnCandReq request;
        KnnCand response;

        request.set_k(cand_num);
        float grpc_comm = request.ByteSizeLong();

        Status status = m_stub_->GetKnnCandidate(&context, request, &response);
        grpc_comm += response.ByteSizeLong();

        if (!status.ok()) {
            std::cerr << "RPC failed: " << status.error_message() << std::endl;
            std::string error_message;
            error_message = std::string("Request partial Knn answers from data silo #(") + std::to_string(m_silo_id) + std::string(") failed");
            throw std::invalid_argument(error_message);  
        }

        m_partial_knn.clear();
        m_partial_knn.reserve(response.knn_size());

        for (int i = 0; i < response.knn_size(); ++i) {
            const VectorData& cand = response.knn(i);
            
            const size_t dim = cand.data_size();
            VectorDataDistType knn_data(dim, cand.vid());
            for (int j = 0; j < dim; ++j) {
                knn_data[j] = cand.data(j);
            }
            knn_data.dist = response.dist(i);

            m_partial_knn.emplace_back(std::move(knn_data));
        }

        m_logger.LogAddComm(grpc_comm);  
    }

    void RequestPartialKnnJoin(const std::vector<KnnJoinCandReqParam>& join_request) {
        ClientContext context;
        KnnJoinCandReq request;
        KnnJoinCand response;

        // context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(60));

        for (const auto& req_param : join_request) {
            request.add_qid(req_param.qid);
            request.add_k(req_param.k);
        }

        float grpc_comm = request.ByteSizeLong();

        // RPC 调用
        Status status = m_stub_->GetKnnJoinCandidate(&context, request, &response);
        grpc_comm += response.ByteSizeLong();

        if (!status.ok()) {
            std::cerr << "RPC failed: " << status.error_message() << std::endl;
            std::string error_message;
            error_message = std::string("Request partial Knn Join answers from data silo #(") + std::to_string(m_silo_id) + std::string(") failed");
            throw std::invalid_argument(error_message);  
        }

        // 清空结果
        m_partial_knnjoin.clear();
        m_partial_knnjoin.reserve(response.result_size());

        // 遍历响应结果，按 qid 分组存储
        for (int i = 0; i < response.result_size(); ++i) {
            const KnnCand& knn_result = response.result(i);
            
            KnnQueryResult query_result;
            query_result.qid = knn_result.qid();
            query_result.knn.reserve(knn_result.knn_size());
            
            // 提取该 qid 的所有 KNN 结果
            for (int j = 0; j < knn_result.knn_size(); ++j) {
                const VectorData& cand = knn_result.knn(j);
                
                const size_t dim = cand.data_size();
                VectorDataDistType knn_data(dim, cand.vid());
                for (int k = 0; k < dim; ++k) {
                    knn_data[k] = cand.data(k);
                }
                knn_data.dist = knn_result.dist(j);

                query_result.knn.emplace_back(std::move(knn_data));
            }
            
            m_partial_knnjoin.emplace_back(std::move(query_result));
        }

        m_logger.LogAddComm(grpc_comm);
    }

    void ClearKnnCand() {
        ClientContext context;
        Empty request;
        Empty response;

        Status status = m_stub_->ClearKnnCandidate(&context, request, &response); 
        if (!status.ok()) {
            std::cerr << "RPC failed: " << status.error_message() << std::endl;
            std::string error_message;
            error_message = std::string("Clear Knn candidates from data silo #(") + std::to_string(m_silo_id) + std::string(") failed");
            throw std::invalid_argument(error_message);  
            std::exit(EXIT_FAILURE);
        }

        float grpc_comm = request.ByteSizeLong() + response.ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);
    } 

    void ClearKnnJoinCand() {
        ClientContext context;
        Empty request;
        Empty response;

        Status status = m_stub_->ClearKnnJoinCandidate(&context, request, &response); 
        if (!status.ok()) {
            std::cerr << "RPC failed: " << status.error_message() << std::endl;
            std::string error_message;
            error_message = std::string("Clear Knn Join candidates from data silo #(") + std::to_string(m_silo_id) + std::string(") failed");
            throw std::invalid_argument(error_message);  
            std::exit(EXIT_FAILURE);
        }

        float grpc_comm = request.ByteSizeLong() + response.ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);
    } 

    std::vector<VectorDataDistType> GetPartialKnn() const {
        return m_partial_knn;
    }

    std::vector<KnnQueryResult> GetPartialKnnJoin() const {
        return m_partial_knnjoin;
    }

    double GetQueryComm() const {
        return m_logger.GetQueryComm();
    }

    void InitBenchLogger() {
        m_logger.Init();
    }

    void SetSearchParameter(const std::string search_param_name, const int search_param_value) {
        ClientContext context;
        SearchParam request;
        Empty response;

        request.set_search_param_name(search_param_name);
        request.set_search_param_value(search_param_value);

        Status status = m_stub_->SetSearchParameter(&context, request, &response); 
        if (!status.ok()) {
            std::cerr << "RPC failed: " << status.error_message() << std::endl;
            std::string error_message;
            error_message = std::string("Set search parameter from data silo #(") + std::to_string(m_silo_id) + std::string(") failed");
            throw std::invalid_argument(error_message);  
        }

        float grpc_comm = request.ByteSizeLong() + response.ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);
    }

    static void RequestKnnJoinCandThread(SiloReceiver* silo_receiver, const std::vector<VectorDataType>& query_data_list, const std::vector<size_t>& query_k_list) {  
        silo_receiver->RequestKnnJoinCand(query_data_list, query_k_list);  
    } 

    static void RequestKnnCandThread(SiloReceiver* silo_receiver, const VectorDataType& query_data, const size_t& query_k) {  
        silo_receiver->RequestKnnCand(query_data, query_k);  
    } 

    static void RequestPartialKnnThread(SiloReceiver* silo_receiver, const size_t cand_num) {  
        silo_receiver->RequestPartialKnn(cand_num);
    }  

    static void ClearKnnCandThread(SiloReceiver* silo_receiver) {  
        silo_receiver->ClearKnnCand();  
    }   

    static void RequestPartialKnnJoinThread(SiloReceiver* silo_receiver, const std::vector<KnnJoinCandReqParam>& join_request) {  
        silo_receiver->RequestPartialKnnJoin(join_request);
    }  

    static void ClearKnnJoinCandThread(SiloReceiver* silo_receiver) {  
        silo_receiver->ClearKnnJoinCand();  
    }   

private:
    std::unique_ptr<FedSqlService::Stub> m_stub_;
    std::vector<VectorDataDistType> m_partial_knn;
    std::vector<KnnQueryResult> m_partial_knnjoin;
    std::string m_silo_ipaddr;
    int m_silo_id;  
    BenchLogger m_logger;  
};

class FedSqlServer {
public:
    FedSqlServer(const QueryConfig& config, const std::string& silo_ip_filename) 
        : m_config(config), m_thread_num(std::thread::hardware_concurrency()), m_silo_ip_filename(silo_ip_filename) {

        ReadSiloIPaddr(silo_ip_filename, m_silo_ipaddr_list);
        if (m_silo_ipaddr_list.empty()) {
            throw std::invalid_argument("No data silo's IP addresses");  
            std::exit(EXIT_FAILURE);
        }

        std::cout << "Server is running\n";

        m_CreateSiloReceiver();

        m_config.m = m_silo_num;

        // Set search parameter in all silos
        m_SetSearchParameter(m_config.search_param_name, m_config.search_param_value);
    }

    FedSqlServer(const QueryConfig& config, const std::string& silo_ip_filename, const std::string& query_filename, const std::string& output_filename, const std::string& truth_filename, const std::string& drf_option, const int& query_k)
                : FedSqlServer(config, silo_ip_filename) {
        
        if (false == m_IsJoin(drf_option)) {
            ProcessFedQuery(query_filename, output_filename, drf_option, query_k);
        } else {
            ProcessFedQueryBatch(query_filename, output_filename, drf_option, query_k);
        }
        
        if (!output_filename.empty() && !truth_filename.empty()) {
            std::cout << "Server's output file name: " << output_filename << std::endl;
            std::cout << "Server's ground truth file name: " << truth_filename << std::endl;
            double avg_recall = EvaluateAnswerRecall(output_filename, truth_filename);
            double avg_mrr = EvaluateAnswerMRR(output_filename, truth_filename);
            double avg_ndcg = EvaluateAnswerNDCG(output_filename, truth_filename);

            std::ostringstream oss;
            oss << ", recall = " << std::fixed << std::setprecision(6) << avg_recall;
            oss << ", MRR = " << std::fixed << std::setprecision(6) << avg_mrr;
            oss << ", NDCG = " << std::fixed << std::setprecision(6) << avg_ndcg;

            std::string result_log = oss.str();
            std::ofstream outfile;
            outfile.open(output_filename, std::ios::app);

            if (outfile.is_open()) {
                outfile << result_log;
                outfile.close();
            } else {
                throw std::runtime_error("Cannot open file: " + output_filename);
            }
        }
    }

    void ProcessFedQuery(const std::string& query_filename, const std::string& output_filename, const std::string& drf_option, const int& query_k) {
        m_query_filename = query_filename;

        // Step 0: Initialize local variables
        m_InitBenchLogger();
        m_CheckDrfOption(drf_option);

        // Step 1: Read vector query from file
        ReadVectorQuery(query_filename, m_query_data_list, m_query_k_list);
        std::fill(m_query_k_list.begin(), m_query_k_list.end(), query_k);
        std::cout << "Server receives " << m_query_data_list.size() << " queries" << std::endl;

        // Step 2: Enumerate all vector queries
        size_t query_num = m_query_data_list.size();
        std::vector<std::vector<VidType>> answer_list;
        for (size_t i=0; i<query_num; ++i) {
            std::vector<VectorDataType> answer = ProcessFedKnnQueryOne(m_query_data_list[i], m_query_k_list[i], drf_option);
            std::vector<VidType> answer_vid(answer.size());
            for (size_t j=0,sz=answer_vid.size(); j<sz; ++j) {
                answer_vid[j] = answer[j].vid;
            }
            answer_list.emplace_back(answer_vid);
        }

        if (output_filename.empty()) {
            // Dump the result into std.cout by default
            std::cout << "Server will dump query answer into std::cout" << std::endl;
        } else {
            // Dump the result into specific file
            std::cout << "Server will dump query answer into file: " << output_filename << std::endl;
            std::string result_log = m_logger.to_string();
            DumpGroundTruth(output_filename, answer_list, result_log);
        }

        // Step 3: Print the log information
        m_logger.Print();
    }

    void ProcessFedQueryBatch(const std::string& query_filename, const std::string& output_filename, const std::string& drf_option, const int& query_k) {
        m_query_filename = query_filename;

        // Step 0: Initialize local variables
        m_InitBenchLogger();
        m_CheckDrfOption(drf_option);

        // Step 1: Read vector query from file
        ReadVectorQuery(query_filename, m_query_data_list, m_query_k_list);
        std::fill(m_query_k_list.begin(), m_query_k_list.end(), query_k);
        size_t query_num = m_query_data_list.size();
        std::cout << "Server receives " << query_num << " queries" << std::endl;

        // Step 2: Enumerate all vector queries
        std::vector<std::vector<VectorDataType>> answer = ProcessFedKnnQueryBatch(m_query_data_list, m_query_k_list, drf_option);
        std::vector<std::vector<VidType>> answer_list;
        for (size_t i=0; i<query_num; ++i) {
            std::vector<VidType> answer_ids;
            for (auto& vector_data : answer[i]) {
                answer_ids.emplace_back(vector_data.vid);
            }
            answer_list.emplace_back(answer_ids);
        }

        if (output_filename.empty()) {
            // Dump the result into std.cout by default
            std::cout << "Server will dump query answer into std::cout" << std::endl;
        } else {
            // Dump the result into specific file
            std::cout << "Server will dump query answer into file: " << output_filename << std::endl;            
            std::string result_log = m_logger.to_string();
            DumpGroundTruth(output_filename, answer_list, result_log);
        }

        // Step 3: Print the log information
        m_logger.Print();
    }

    std::vector<std::vector<VectorDataType>> ProcessFedKnnQueryBatch(const std::vector<VectorDataType>& query_data_list, const std::vector<size_t>& query_k_list, const std::string& drf_option) {
        std::vector<std::vector<VectorDataType>> answer;
        
        if (drf_option == std::string("batch-g")) {
            answer = m_ProcessFedKnnQueryBatch_ByGreedy(query_data_list, query_k_list);
        } else if (drf_option == std::string("batch-a")) {
            answer = m_ProcessFedKnnQueryBatch_ByActive(query_data_list, query_k_list);
        } else {
            throw std::invalid_argument("Invalid (batch) drf_option: " + drf_option + 
                                    ". Expected 'batch-g' or 'batch-a'");
        }
        return answer;
    }

    std::vector<VectorDataType> ProcessFedKnnQueryOne(const VectorDataType& query_data, const size_t& query_k, const std::string& drf_option) {
        std::vector<VectorDataType> answer;
        
        if (drf_option == std::string("greedy")) {
            answer = m_ProcessFedKnnQuery_ByGreedy(query_data, query_k);
        } else if (drf_option == std::string("active")) {
            answer = m_ProcessFedKnnQuery_ByActive(query_data, query_k);
        } else if (drf_option == std::string("ablation")) {
            answer = m_ProcessFedKnnQuery_ByAblation(query_data, query_k);
        } else {
            throw std::invalid_argument("Invalid (single) drf_option: " + drf_option + 
                                    ". Expected 'greedy' or 'active'");
        }

        return answer;
    }

    std::string to_string() const {
        std::stringstream ss;

        ss << "-------------- Service Log --------------\n";
        ss << m_logger.to_string() << std::endl;

        return ss.str();
    }

private:
    std::vector<std::vector<VectorDataType>> m_ProcessFedKnnQueryBatch_ByActive(const std::vector<VectorDataType>& query_data_list, const std::vector<size_t>& query_k_list) {
        m_logger.SetStartTimer();
        size_t query_num = query_data_list.size();

        // Step 0: Intialize some variables
        std::vector<std::vector<VectorDataType>> join_answer(query_num);
        std::vector<Allocator> join_allocator;
        std::vector<int> join_remaining(query_num);
        for (int i=0; i<query_num; ++i) {
            join_remaining[i] = m_config.k = query_k_list[i];
            join_allocator.emplace_back(Allocator(m_config));
        }
        std::vector<std::vector<VidType>> join_collected_ids(query_num);
        std::vector<std::vector<SidType>> join_collected_owners(query_num);
        std::vector<std::vector<VectorDimensionType>> join_collected_dists(query_num);

        // Step 1: Perform local knn query at each silo
        m_PerformLocalKnnJoin(query_data_list, query_k_list);

        for (int rid=0; rid<m_config.R; ++rid) {
            // Step 2: check whether all queries are processed
            bool process_finished = true;
            for (int i=0; i<query_num; ++i) {
                if (join_remaining[i] > 0) {
                    process_finished = false;
                    break;
                }
            }
            if (process_finished) break;

            // Step 3: Determine the allocation in this batch
            std::vector<std::vector<KnnJoinCandReqParam>> join_allocation(m_silo_num);

            for (int qid=0; qid<query_num; ++qid) {
                if (join_remaining[qid] <= 0)
                    continue;

                // 3.1 计算本轮预算
                auto& allocator = join_allocator[qid];
                int budget = allocator.compute_budget(join_remaining[qid], m_config.R-rid);
                
                // 3.2 获取分配方案
                std::vector<size_t> allocations = allocator.inner_allocate(rid, budget);
                for (SidType sid=0; sid<m_silo_num; ++sid) {
                    if (allocations[sid] > 0) {
                        KnnJoinCandReqParam req_param;
                        req_param.k = allocations[sid];
                        req_param.qid = qid;
                        join_allocation[sid].emplace_back(req_param);
                    }
                }
            }

            // 3.3 请求各方本地knn数据
            m_RetrievePartialKnnJoin(join_allocation);

            // 3.4 根据分配方案从各 silo 采集数据
            for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
                if (join_allocation[silo_id].empty()) continue;

                std::vector<KnnQueryResult> partial_knnjoin_list = m_silo_receiver_list[silo_id]->GetPartialKnnJoin();
                for (auto& query_result : partial_knnjoin_list) {
                    QidType qid = query_result.qid;
                    auto& knn = query_result.knn;
                    for (auto& nn : knn) {
                        join_collected_ids[qid].emplace_back(nn.vid);
                        join_collected_owners[qid].emplace_back(silo_id);
                        join_collected_dists[qid].emplace_back(nn.dist);

                        VectorDataType vector_data(nn.Dimension(), nn.vid);
                        vector_data.data = nn.data;
                        join_answer[qid].emplace_back(vector_data);
                        // 更新剩余可取的样本数量
                        --join_remaining[qid];
                    }
                }
            }

            // 添加到 Allocator
            for (int qid=0; qid<query_num; ++qid) {
                if (!join_collected_ids.empty()) {
                    auto& allocator = join_allocator[qid];
                    allocator.add_collected(join_collected_ids[qid], join_collected_owners[qid], join_collected_dists[qid], rid);
                }
                join_collected_ids[qid].clear();
                join_collected_owners[qid].clear();
                join_collected_dists[qid].clear();
            }
        }

        // Step 4: Clear Knn candidates at each silo
        m_ClearKnnJoinCand();

        // Step 5: Print the log information
        m_logger.SetEndTimer();
        double query_comm = 0.0;
        for (int i=0; i<m_silo_num; ++i) {
            query_comm += m_silo_receiver_list[i]->GetQueryComm();
        }
        query_comm -= m_logger.GetQueryComm();
        double query_time = m_logger.GetDurationTime();
        m_logger.LogOneQuery(query_comm);

        std::cout << std::fixed << std::setprecision(6) 
                    << "Batch " << query_num << " queries" << ": runtime = " << query_time/1000.0 << " [s], communication = " << query_comm/1024.0 << " [KB]" << std::endl;
    
        return join_answer;
    }

    std::vector<std::vector<VectorDataType>> m_ProcessFedKnnQueryBatch_ByGreedy(const std::vector<VectorDataType>& query_data_list, const std::vector<size_t>& query_k_list) {
        m_logger.SetStartTimer();
        size_t query_num = query_data_list.size();
        size_t sample_step_size = 1;

        // Step 0: Intialize some variables
        std::vector<std::vector<VectorDataType>> join_answer(query_num);

        // Step 1: Perform local knn query at each silo
        m_PerformLocalKnnJoin(query_data_list, query_k_list);

        // Step 2: Determine the resource allocation for each silo
        std::vector<std::vector<VectorDimensionType>> join_dist_list(query_num, std::vector<VectorDimensionType>(m_silo_num));
        std::vector<std::set<DistSample>> join_minQ(query_num, std::set<DistSample>());

        for (int rid=0; ; ++rid) {
            bool process_finished = true;
            for (int i=0; i<query_num; ++i) {
                if (join_answer[i].size() < query_k_list[i]) {
                    process_finished = false;
                    break;
                }
            }
            if (process_finished) break;

            if (rid == 0) {
                // Step 3a: Retrieve one candidate from each silo
                std::vector<std::vector<KnnJoinCandReqParam>> allocation(m_silo_num);
                std::vector<KnnJoinCandReqParam> init_req_param;
                for (int j=0; j<query_num; ++j) {
                    init_req_param.emplace_back(KnnJoinCandReqParam((QidType)j, sample_step_size));
                }
                std::fill(allocation.begin(), allocation.end(), init_req_param);    

                m_RetrievePartialKnnJoin(allocation);
                
                /// Collect the query answer
                for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
                    std::vector<KnnQueryResult> partial_knnjoin_list = m_silo_receiver_list[silo_id]->GetPartialKnnJoin();
                    for (auto& query_result : partial_knnjoin_list) {
                        QidType qid = query_result.qid;
                        auto& knn = query_result.knn;
                        for (auto& nn : knn) {
                            VectorDataType vector_data(nn.Dimension(), nn.vid);
                            vector_data.data = nn.data;
                            join_answer[qid].emplace_back(vector_data);
                            join_dist_list[qid][silo_id] = nn.dist;
                        
                            join_minQ[qid].insert(DistSample(silo_id, nn.dist));
                        }
                    }
                }

            } else {

                // Step 3b: Retrieve one candidate from the most relevant silo
                std::vector<std::vector<KnnJoinCandReqParam>> allocation(m_silo_num);
                for (int qid=0; qid<query_num; ++qid) {
                    if (join_answer[qid].size() >= query_k_list[qid]) 
                        continue;
                    KnnJoinCandReqParam req_param;
                    req_param.k = sample_step_size;
                    req_param.qid = qid;
                    int silo_id = join_minQ[qid].begin()->silo_id;
                    allocation[silo_id].emplace_back(req_param); 
                }
                
                m_RetrievePartialKnnJoin(allocation);

                /// Collect the query answer
                for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
                    if (allocation[silo_id].empty()) continue;

                    std::vector<KnnQueryResult> partial_knnjoin_list = m_silo_receiver_list[silo_id]->GetPartialKnnJoin();
                    for (auto& query_result : partial_knnjoin_list) {
                        QidType qid = query_result.qid;
                        auto& knn = query_result.knn;
                        for (auto& nn : knn) {
                            VectorDataType vector_data(nn.Dimension(), nn.vid);
                            vector_data.data = nn.data;
                            join_answer[qid].emplace_back(vector_data);

                            join_minQ[qid].erase(DistSample(silo_id, join_dist_list[qid][silo_id]));
                            join_dist_list[qid][silo_id] = nn.dist;
                            join_minQ[qid].insert(DistSample(silo_id, nn.dist));
                        }
                    }
                }
            }

        }
        
        // Step 4: Clear Knn candidates at each silo
        m_ClearKnnJoinCand();

        // Step 5: Print the log information
        m_logger.SetEndTimer();
        double query_comm = 0.0;
        for (int i=0; i<m_silo_num; ++i) {
            query_comm += m_silo_receiver_list[i]->GetQueryComm();
        }
        query_comm -= m_logger.GetQueryComm();
        double query_time = m_logger.GetDurationTime();
        m_logger.LogOneQuery(query_comm);

        std::cout << std::fixed << std::setprecision(6) 
                    << "Batch " << query_num << " queries" << ": runtime = " << query_time/1000.0 << " [s], communication = " << query_comm/1024.0 << " [KB]" << std::endl;
    
        return join_answer;
    }

    std::vector<VectorDataType> m_ProcessFedKnnQuery_ByAblation(const VectorDataType& query_data, const size_t& query_k) {
        m_logger.SetStartTimer();

        // Step 0: Intialize some variables
        std::vector<VectorDataType> answer;
        int remaining = m_config.k;

        // Step 1: Perform local knn query at each silo
        m_PerformLocalKnn(query_data, query_k);

        for (int rid=0; remaining>0 && rid<m_config.R; ++rid) {
            
            // 3.2 获取分配方案
            std::vector<size_t> allocations(m_silo_num, 0);
            if (rid == 0) {
                std::fill(allocations.begin(), allocations.end(), (size_t)m_config.b_init);
            } else {
                // 3.1 计算本轮预算
                int budget = (m_config.R-rid <= 1) ? remaining : ((int)remaining / (m_config.R-rid));
                int avg_budget = budget / m_silo_num;
                int remainder = budget % m_silo_num;
                for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
                    allocations[silo_id] = avg_budget + (silo_id < remainder ? 1 : 0);
                }
            }

            // 3.3 请求各方本地knn数据
            m_RetrievePartialKnn(allocations);

            // 3.4 根据分配方案从各 silo 采集数据
            std::vector<VidType> collected_ids;
            std::vector<SidType> collected_owners;
            std::vector<VectorDimensionType> collected_dists;
            for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
                if (allocations[silo_id] <= 0) continue;

                std::vector<VectorDataDistType> partial_knn_list = m_silo_receiver_list[silo_id]->GetPartialKnn();
                for (auto& knn : partial_knn_list) {
                    collected_ids.emplace_back(knn.vid);
                    collected_owners.emplace_back(silo_id);
                    collected_dists.emplace_back(knn.dist);

                    VectorDataType vector_data(knn.Dimension(), knn.vid);
                    vector_data.data = knn.data;
                    answer.emplace_back(vector_data);
                }

                // 更新剩余可取的样本数量
                remaining -= partial_knn_list.size();
            }
        }

        // Step 4: Clear Knn candidates at each silo
        m_ClearKnnCand();

        // Step 5: Print the log information
        m_logger.SetEndTimer();
        double query_comm = 0.0;
        for (int i=0; i<m_silo_num; ++i) {
            query_comm += m_silo_receiver_list[i]->GetQueryComm();
        }
        query_comm -= m_logger.GetQueryComm();
        double query_time = m_logger.GetDurationTime();
        m_logger.LogOneQuery(query_comm);

        std::cout << std::fixed << std::setprecision(6) 
                    << "Query #(" << query_data.vid << "): runtime = " << query_time/1000.0 << " [s], communication = " << query_comm/1024.0 << " [KB]" << std::endl;
    
        return answer;
    }

    std::vector<VectorDataType> m_ProcessFedKnnQuery_ByActive(const VectorDataType& query_data, const size_t& query_k) {
        m_logger.SetStartTimer();

        // Step 0: Intialize some variables
        std::vector<VectorDataType> answer;
        Allocator allocator(m_config);
        int remaining = m_config.k;

        // Step 1: Perform local knn query at each silo
        m_PerformLocalKnn(query_data, query_k);

        for (int rid=0; remaining>0 && rid<m_config.R; ++rid) {
            // 3.1 计算本轮预算
            int budget = allocator.compute_budget(remaining, m_config.R-rid);
            
            // 3.2 获取分配方案
            std::vector<size_t> allocations = allocator.inner_allocate(rid, budget);

            // 3.3 请求各方本地knn数据
            m_RetrievePartialKnn(allocations);

            // 3.4 根据分配方案从各 silo 采集数据
            std::vector<VidType> collected_ids;
            std::vector<SidType> collected_owners;
            std::vector<VectorDimensionType> collected_dists;
            for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
                if (allocations[silo_id] <= 0) continue;

                std::vector<VectorDataDistType> partial_knn_list = m_silo_receiver_list[silo_id]->GetPartialKnn();
                for (auto& knn : partial_knn_list) {
                    collected_ids.emplace_back(knn.vid);
                    collected_owners.emplace_back(silo_id);
                    collected_dists.emplace_back(knn.dist);

                    VectorDataType vector_data(knn.Dimension(), knn.vid);
                    vector_data.data = knn.data;
                    answer.emplace_back(vector_data);
                }

                // 更新剩余可取的样本数量
                remaining -= partial_knn_list.size();
            }

            // 添加到 Allocator
            allocator.add_collected(collected_ids, collected_owners, collected_dists, rid);
        }

        // Step 4: Clear Knn candidates at each silo
        m_ClearKnnCand();

        // Step 5: Print the log information
        m_logger.SetEndTimer();
        double query_comm = 0.0;
        for (int i=0; i<m_silo_num; ++i) {
            query_comm += m_silo_receiver_list[i]->GetQueryComm();
        }
        query_comm -= m_logger.GetQueryComm();
        double query_time = m_logger.GetDurationTime();
        m_logger.LogOneQuery(query_comm);

        std::cout << std::fixed << std::setprecision(6) 
                    << "Query #(" << query_data.vid << "): runtime = " << query_time/1000.0 << " [s], communication = " << query_comm/1024.0 << " [KB]" << std::endl;
    
        return answer;
    }

    std::vector<VectorDataType> m_ProcessFedKnnQuery_ByGreedy(const VectorDataType& query_data, const size_t& query_k) {
        m_logger.SetStartTimer();
        size_t sample_step_size = 1;

        // Step 0: Intialize some variables
        std::vector<VectorDataType> answer;

        // Step 1: Perform local knn query at each silo
        m_PerformLocalKnn(query_data, query_k);

        // Step 2: Determine the resource allocation for each silo
        std::vector<VectorDimensionType> dist_list(m_silo_num);
        std::set<DistSample> minQ;
        std::vector<size_t> allocation(m_silo_num);

        for (int rid=0, total_answer=0; total_answer<query_k; ++rid) {
            if (rid == 0) {
                // Step 3a: Retrieve one candidate from each silo
                std::fill(allocation.begin(), allocation.end(), sample_step_size);
                m_RetrievePartialKnn(allocation);
                for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
                    std::vector<VectorDataDistType> partial_knn_list = m_silo_receiver_list[silo_id]->GetPartialKnn();
                    for (auto& knn : partial_knn_list) {
                        VectorDataType vector_data(knn.Dimension(), knn.vid);
                        vector_data.data = knn.data;
                        answer.emplace_back(vector_data);
                        dist_list[silo_id] = knn.dist;
                        minQ.insert(DistSample(silo_id, knn.dist));
                    }
                    total_answer += partial_knn_list.size();
                }

            } else {

                // Step 3b: Retrieve one candidate from the most relevant silo
                int silo_id = minQ.begin()->silo_id;
                allocation[silo_id] = sample_step_size;
                SiloReceiver::RequestPartialKnnThread(m_silo_receiver_list[silo_id].get(), allocation[silo_id]);
                std::vector<VectorDataDistType> partial_knn_list = m_silo_receiver_list[silo_id]->GetPartialKnn();
                for (auto& knn : partial_knn_list) {
                    VectorDataType vector_data(knn.Dimension(), knn.vid);
                    vector_data.data = knn.data;
                    answer.emplace_back(vector_data);

                    minQ.erase(DistSample(silo_id, dist_list[silo_id]));
                    dist_list[silo_id] = knn.dist;
                    minQ.insert(DistSample(silo_id, knn.dist));
                }
                total_answer += partial_knn_list.size();
            }

        }

        // Step 4: Clear Knn candidates at each silo
        m_ClearKnnCand();

        // Step 5: Print the log information
        m_logger.SetEndTimer();
        double query_comm = 0.0;
        for (int i=0; i<m_silo_num; ++i) {
            query_comm += m_silo_receiver_list[i]->GetQueryComm();
        }
        query_comm -= m_logger.GetQueryComm();
        double query_time = m_logger.GetDurationTime();
        m_logger.LogOneQuery(query_comm);

        std::cout << std::fixed << std::setprecision(6) 
                    << "Query #(" << query_data.vid << "): runtime = " << query_time/1000.0 << " [s], communication = " << query_comm/1024.0 << " [KB]" << std::endl;
    
        return answer;
    }

    void m_ClearKnnCand() {
        boost::asio::thread_pool pool(m_thread_num);

        for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
            boost::asio::post(pool, std::bind(SiloReceiver::ClearKnnCandThread, m_silo_receiver_list[silo_id].get())); 
        }

        pool.join();
    }

    void m_ClearKnnJoinCand() {
        boost::asio::thread_pool pool(m_thread_num);

        for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
            boost::asio::post(pool, std::bind(SiloReceiver::ClearKnnJoinCandThread, m_silo_receiver_list[silo_id].get())); 
        }

        pool.join();
    }

    void m_RetrievePartialKnn(const std::vector<size_t>& allocation) {
        if (allocation.size() != m_silo_num) {
            std::cerr << "The scheme for dominant resource allocation only covers " << allocation.size() 
                        << " out of " << m_silo_num << " data silos" << std::endl;
            std::exit(EXIT_FAILURE);   
        }

        boost::asio::thread_pool pool(m_thread_num);

        for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
            if (allocation[silo_id] > 0) {
                boost::asio::post(pool, std::bind(SiloReceiver::RequestPartialKnnThread, m_silo_receiver_list[silo_id].get(), allocation[silo_id])); 
            }
        }

        pool.join();
    }

    void m_RetrievePartialKnnJoin(const std::vector<std::vector<KnnJoinCandReqParam>>& allocation) {
        if (allocation.size() != m_silo_num) {
            std::cerr << "The scheme for dominant resource allocation only covers " << allocation.size() 
                        << " out of " << m_silo_num << " data silos" << std::endl;
            std::exit(EXIT_FAILURE);   
        }

        boost::asio::thread_pool pool(m_thread_num);

        for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
            if (!allocation[silo_id].empty()) {
                boost::asio::post(pool, std::bind(SiloReceiver::RequestPartialKnnJoinThread, m_silo_receiver_list[silo_id].get(), allocation[silo_id])); 
            }
        }

        pool.join();
    }

    void m_PerformLocalKnnJoin(const std::vector<VectorDataType>& query_data_list, const std::vector<size_t>& query_k_list) {
        boost::asio::thread_pool pool(m_thread_num);

        for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
            boost::asio::post(pool, std::bind(SiloReceiver::RequestKnnJoinCandThread, m_silo_receiver_list[silo_id].get(), query_data_list, query_k_list)); 
        }

        pool.join();
    }

    void m_PerformLocalKnn(const VectorDataType& query_data, const size_t& query_k) {
        boost::asio::thread_pool pool(m_thread_num);

        for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
            boost::asio::post(pool, std::bind(SiloReceiver::RequestKnnCandThread, m_silo_receiver_list[silo_id].get(), query_data, query_k)); 
        }

        pool.join();
    }

    void m_CheckDrfOption(const std::string& drf_option) {
        std::string input_drf_option(drf_option);
        std::transform(input_drf_option.begin(), input_drf_option.end(), input_drf_option.begin(),  
                        [](unsigned char c){ return std::tolower(c); });

        if (input_drf_option == "active") {
            std::cout << "Using ``active selection`` as our sampling algorithm" << std::endl;
        } else if (input_drf_option == "greedy") {
            std::cout << "Using ``greedy nearest neighbor`` as our sampling algorithm" << std::endl;
        } else if (input_drf_option == "batch-a") {
            std::cout << "Using ``batch active selection`` as our sampling algorithm" << std::endl;
        } else if (input_drf_option == "batch-g") {
            std::cout << "Using ``batch greedy nearest neighbor`` as our sampling algorithm" << std::endl;
        } else if (input_drf_option == "ablation") {
            std::cout << "Using ``equality assignment`` as our sampling algorithm" << std::endl;
        } else {
            std::cerr << "Found unknown option for dominant resource allocation: " << drf_option 
                        << ", possible options are [active]" << std::endl;
            std::exit(EXIT_FAILURE);            
        }
    }

    void m_CreateSiloReceiver() {
        m_silo_num = m_silo_ipaddr_list.size();
        m_silo_receiver_list.resize(m_silo_num);
        grpc::ChannelArguments args;  
        args.SetInt(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH, INT_MAX);  
        args.SetInt(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, INT_MAX); 
        for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
            std::string silo_ipaddr = m_silo_ipaddr_list[silo_id];
            std::cout << "Server is connecting Silo #(" << std::to_string(silo_id+1) << ") on IP address " << silo_ipaddr << std::endl;

            std::shared_ptr<grpc::Channel> channel = grpc::CreateCustomChannel(silo_ipaddr, grpc::InsecureChannelCredentials(), args);
            m_silo_receiver_list[silo_id] = std::make_shared<SiloReceiver>(channel, silo_id+1, silo_ipaddr);
        }
    }

    std::vector<VectorDataType> m_PickGlobalKnn(const VectorDataType& query_data, const size_t& query_k) {
        std::vector<VectorDataType> answer;
        if (std::is_same<Distance, InnerProductDistance>::value) {
            /** do nothing **/;
        } else {
            answer = m_PickGlobalKnn_ByDefault(query_data, query_k);
        }
        return answer;
    }

    std::vector<VectorDataType> m_PickGlobalKnn_ByDefault(const VectorDataType& query_data, const size_t& query_k) {
        std::vector<VectorDataType> query_answer;
        std::vector<VectorDataType> cand_answer;
        std::priority_queue<std::pair<VectorDimensionType, size_t>, std::vector<std::pair<VectorDimensionType, size_t>>, std::less<std::pair<VectorDimensionType, size_t>>> min_heap;  
        size_t min_heap_size = 0;
        Distance dist_function;

        for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
            std::vector<VectorDataDistType> partial_knn_list = m_silo_receiver_list[silo_id]->GetPartialKnn();
            cand_answer.insert(cand_answer.end(), partial_knn_list.begin(), partial_knn_list.end());
        }
        for (size_t idx=0, cand_size=cand_answer.size(); idx<cand_size; ++idx) {  
            const auto& vector_data = cand_answer[idx];
            VectorDimensionType dist = dist_function(query_data, vector_data);
    
            if (min_heap_size < query_k) {
                ++min_heap_size;
                min_heap.push(std::make_pair(dist, idx));
            } else if (dist < min_heap.top().first) {  
                min_heap.pop();
                min_heap.push(std::make_pair(dist, idx));
            }  
        }

        while (!min_heap.empty()) {  
            size_t idx = min_heap.top().second; 
            query_answer.emplace_back(cand_answer[idx]);
            min_heap.pop();
        }  
    
        std::reverse(query_answer.begin(), query_answer.end());  

        return query_answer;
    }

    void m_InitBenchLogger() {
        for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
            m_silo_receiver_list[silo_id]->InitBenchLogger();
        }    
        m_logger.Init();    
    }

    bool m_IsJoin(const std::string& option) {
        const std::string prefix("batch");
        const size_t prefix_len = prefix.length();  // 5
        
        if (option.length() < prefix_len) {
            return false;
        }
        return option.compare(0, prefix_len, prefix) == 0;
    }

    void m_SetSearchParameter(const std::string search_param_name, const int search_param_value) {
        if (search_param_name.empty() || search_param_value<=0) return ;

        for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
            m_silo_receiver_list[silo_id]->SetSearchParameter(search_param_name, search_param_value);
        }    
    }

    std::vector<std::shared_ptr<SiloReceiver>> m_silo_receiver_list;
    std::string m_silo_ip_filename;
    std::string m_query_filename;
    std::vector<std::string> m_silo_ipaddr_list;
    std::vector<VectorDataType> m_query_data_list;
    std::vector<size_t> m_query_k_list;
    BenchLogger m_logger;
    int m_silo_num;
    const int m_thread_num;
    QueryConfig m_config;
};

std::unique_ptr<FedSqlServer> fed_sqlserver_ptr = nullptr;
QueryConfig config;
double eta = 0.3, lambda = 0.3;
int init_budget = 1;

void RunService(const std::string& silo_ip_filename, const std::string& query_filename, const std::string& output_filename, const std::string& truth_filename, const std::string& drf_option, const int query_k, const int comm_round, const std::string& search_param_name, const int search_param_value) {
    // ==================== 1. 配置 ====================
    config.k = query_k;          // Top-100
    config.R = comm_round;       // 最大轮数
    config.lambda = lambda;
    config.eta = eta;
    config.b_init = init_budget;
    // The following parameters are useless
    config.m = 4;                
    config.T_max = 2.0;
    config.T_min = 0.15;
    config.g_init = 0.0001;

    config.search_param_name = search_param_name;
    config.search_param_value = search_param_value;

    fed_sqlserver_ptr = std::make_unique<FedSqlServer>(config, silo_ip_filename, query_filename, output_filename, truth_filename, drf_option, query_k);

    std::string log_info = fed_sqlserver_ptr->to_string();
    std::cout << log_info;
    std::cout.flush();
}    

// Ensure the log file is output, when the program is terminated.
void SignalHandler(int signal) {
    if (fed_sqlserver_ptr != nullptr) {
        std::string log_info = fed_sqlserver_ptr->to_string();
        std::cout << log_info;
        std::cout.flush();
    }
    quick_exit(0);
}

void ResetSignalHandler() {
    signal(SIGINT, SignalHandler);
    signal(SIGQUIT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    signal(SIGKILL, SignalHandler);
}

int main(int argc, char** argv) {
    // Expect only arg: --silo-ip=../configuration/ip.txt --query-path=../query/query.txt --output-path=../query/answer.txt
    std::string query_filename, output_filename, truth_filename, silo_ip_filename;
    std::string drf_option("active");
    int query_k, comm_round;
    std::string search_param_name;
    int search_param_value = 0;

    try { 
        bpo::options_description option_description("Required options");
        option_description.add_options()
            ("help", "produce help message")
            ("silo-ip", bpo::value<std::string>(), "Data silo's IP configuration file path")
            ("query-path", bpo::value<std::string>(), "Query file path")
            ("output-path", bpo::value<std::string>(), "Answer file path")
            ("truth-path", bpo::value<std::string>(), "Ground truth file path")
            ("option", bpo::value<std::string>(), "Option for k allocation: [active | greedy | batch-a | batch-g | ablation]")
            ("query-k", bpo::value<int>(&query_k)->default_value(128), "The K of KNN")
            // ====== 新增：搜索参数配置 ======
            ("search-param-name", bpo::value<std::string>(), 
            "Search parameter name: [nprobe | efSearch]")
            ("search-param-value", bpo::value<int>(), 
            "Search parameter value (integer)")
            // ====== 新增：算法参数配置 ======
            ("comm-round", bpo::value<int>(&comm_round)->default_value(10), "The number of communication rounds")
            ("init-budget", bpo::value<int>(&init_budget)->default_value(1), "The size of initial sample(s)")
            ("alpha", bpo::value<double>(&eta)->default_value(0.3), "The value of alpha for estimating contribution ratio")
            ("lambda", bpo::value<double>(&lambda)->default_value(0.3), "The value of lambda for deciding budget")
        ;

        bpo::variables_map variable_map;
        bpo::store(bpo::parse_command_line(argc, argv, option_description), variable_map);
        bpo::notify(variable_map);    

        if (variable_map.count("help")) {
            std::cout << option_description << std::endl;
            return 0;
        }

        bool options_all_set = true;

        if (variable_map.count("silo-ip")) {
            silo_ip_filename = variable_map["silo-ip"].as<std::string>();
            std::cout << "Data silo's IP configuration file path was set to " << silo_ip_filename << "\n";
        } else {
            std::cout << "Data silo's IP configuration file path was not set" << "\n";
            options_all_set = false;
        }

        if (variable_map.count("query-path")) {
            query_filename = variable_map["query-path"].as<std::string>();
            std::cout << "Server's received query file path was set to " << query_filename << "\n";
        } else {
            std::cout << "Server's received query file path was not set" << "\n";
            options_all_set = false;
        }

        if (variable_map.count("output-path")) {
            output_filename = variable_map["output-path"].as<std::string>();
            std::cout << "Server's answer output file path was set to " << output_filename << "\n";
        } else {
            std::cout << "Server's answer output file path was not set" << "\n";
        }

        if (variable_map.count("truth-path")) {
            truth_filename = variable_map["truth-path"].as<std::string>();
            std::cout << "Server's ground truth file path was set to " << truth_filename << "\n";
        } else {
            std::cout << "Server's ground truth file path was not set" << "\n";
        }

        if (variable_map.count("option")) {
            drf_option = variable_map["option"].as<std::string>();
            std::cout << "Server's k allocation strategy was set to " << drf_option << "\n";
        } else {
            std::cout << "Server's k allocation strategy was set to default (" << drf_option << ")\n";
        }

        if (variable_map.count("query-k")) {
            query_k = variable_map["query-k"].as<int>();
            std::cout << "The K of KNN was set to " << query_k << "\n";
        } else {
            std::cout << "The K of KNN was set to default (" << query_k << ")\n";
        }

        if (variable_map.count("comm-round")) {
            comm_round = variable_map["comm-round"].as<int>();
            std::cout << "The number of communication rounds was set to " << comm_round << "\n";
        } else {
            std::cout << "The number of communication rounds was set to default (" << comm_round << ")\n";
        }

        if (variable_map.count("init-budget")) {
            init_budget = variable_map["init-budget"].as<int>();
            std::cout << "The size of initial sample(s) was set to " << init_budget << "\n";
        } else {
            std::cout << "The size of initial sample(s) was set to default (" << init_budget << ")\n";
        }

        if (variable_map.count("alpha")) {
            eta = variable_map["alpha"].as<double>();
            std::cout << "The value of alpha for estimating contribution ratio was set to " << eta << "\n";
        } else {
            std::cout << "The value of alpha for estimating contribution ratio was set to default (" << eta << ")\n";
        }

        if (variable_map.count("lambda")) {
            lambda = variable_map["lambda"].as<double>();
            std::cout << "The value of lambda for deciding budget was set to " << lambda << "\n";
        } else {
            std::cout << "The value of lambda for deciding budget was set to default (" << lambda << ")\n";
        }

        // 解析 search-param-name
        if (variable_map.count("search-param-name")) {
            search_param_name = variable_map["search-param-name"].as<std::string>();
            std::cout << "Search parameter name was set to " << search_param_name << "\n";
        } else {
            std::cout << "Search parameter name was not set, using default\n";
        }

        if (variable_map.count("search-param-value")) {
            search_param_value = variable_map["search-param-value"].as<int>();
            std::cout << "Search parameter value was set to " << search_param_value << "\n";
        } else {
            std::cout << "Search parameter value was not set, using default\n";
        }

        if (false == options_all_set) {
            throw std::invalid_argument("Some options were not properly set");
            std::cout.flush();
            std::exit(EXIT_FAILURE);
        }

    } catch (std::exception& e) {  
        std::cerr << "Error: " << e.what() << "\n";  
        std::exit(EXIT_FAILURE);
    }

    ResetSignalHandler();

    RunService(silo_ip_filename, query_filename, output_filename, truth_filename, drf_option, query_k, comm_round, search_param_name, search_param_value);

    return 0;
}

