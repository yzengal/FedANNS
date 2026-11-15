#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
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

#include "FedVectorDBO.grpc.pb.h"

#include "database/VectorDB.hpp"
#include "index/BaseIndex.hpp"
#include "utils/DataType.hpp"
#include "utils/MetricType.hpp"
#include "utils/BenchLogger.hpp"
#include "utils/File_IO.h"
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

using FedVectorDBO::KnnSql;
using FedVectorDBO::CandSql;
using FedVectorDBO::MpcComm;
using FedVectorDBO::MinMpcBobList;
using FedVectorDBO::MinMpcAlicePair;
using FedVectorDBO::MinMpcSecretShares;
using FedVectorDBO::VectorData;
using FedVectorDBO::KnnCand;
using FedVectorDBO::SearchParam;
using FedVectorDBO::FedSqlService;
using FedVectorDBO::KnnJoinSql;
using FedVectorDBO::KnnCand;
using FedVectorDBO::KnnJoinCandReq;
using FedVectorDBO::KnnJoinCand;
using FedVectorDBO::JoinMinMpcBobList;
using FedVectorDBO::JoinMinMpcAlicePair;
using FedVectorDBO::JoinMinMpcSecretShares;

using Distance = EuclideanSquareDistance;
const int MAX_QUERY_NUM = 100;

class SiloReceiver {
public:
    SiloReceiver(std::shared_ptr<grpc::Channel> channel, const int silo_id, const std::string& silo_ipaddr) 
        : m_silo_stub(FedSqlService::NewStub(channel)), m_silo_id(silo_id), m_silo_ipaddr(silo_ipaddr) {
        
        m_logger.Init();

        m_join_secret_share_list.resize(MAX_QUERY_NUM);
    }

    void RequestKnnCand(const VectorDataType& query_data, const size_t& query_k) {
        ClientContext context;
        Empty response;
        KnnSql query;

        query.set_vid(query_data.vid);
        query.set_k(query_k);
        for (auto d : query_data.data) {
            query.add_data(d);
        }

        // std::cout << "Silo " << m_silo_id << ": ``RequestKnnCand``" << std::endl;

        Status status = m_silo_stub->ExecuteKnnQuery(&context, query, &response); 
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

    void RequestPartialKnn(const size_t cand_num) {
        ClientContext context;
        KnnCand response;
        CandSql request;

        request.set_k(cand_num);
        float grpc_comm = request.ByteSizeLong();

        Status status = m_silo_stub->GetKnnCandidate(&context, request, &response);
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
        Status status = m_silo_stub->GetKnnJoinCandidate(&context, request, &response);
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

        Status status = m_silo_stub->ExecuteKnnJoin(&context, query, &response); 
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
    
    void ClearKnnCand() {
        ClientContext context;
        Empty request;
        Empty response;

        Status status = m_silo_stub->ClearKnnCandidate(&context, request, &response); 
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

        Status status = m_silo_stub->ClearKnnJoinCandidate(&context, request, &response); 
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

    void StartMinMpc(int bob_silo_id) {
        ClientContext context;
        MinMpcBobList request;
        Empty response;

        request.set_bob(bob_silo_id); // negative means all silos should be regarded as Bob
        Status status = m_silo_stub->StartMinMpc(&context, request, &response); 
        if (!status.ok()) {
            std::cerr << "RPC failed: " << status.error_message() << std::endl;
            std::string error_message;
            error_message = std::string("Start the protocol for Min-MPC from data silo #(") + std::to_string(m_silo_id) + std::string(") failed");
            throw std::invalid_argument(error_message);  
            std::exit(EXIT_FAILURE);
        }

        float grpc_comm = request.ByteSizeLong() + response.ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);
    }

    void JoinStartMinMpc(const std::vector<QidType>& qids, const int bob_silo_id) {
        ClientContext context;
        JoinMinMpcBobList request;
        Empty response;

        request.set_bob(bob_silo_id); // negative means all silos should be regarded as Bob
        for (auto qid : qids)
            request.add_qid(qid);
        Status status = m_silo_stub->JoinStartMinMpc(&context, request, &response); 
        if (!status.ok()) {
            std::cerr << "RPC failed: " << status.error_message() << std::endl;
            std::string error_message;
            error_message = std::string("Start the protocol for Min-MPC from data silo #(") + std::to_string(m_silo_id) + std::string(") failed");
            throw std::invalid_argument(error_message);  
            std::exit(EXIT_FAILURE);
        }

        float grpc_comm = request.ByteSizeLong() + response.ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);
    }

    void CollectMinMpc(int bob_silo_id) {
        ClientContext context;
        MinMpcBobList request;
        MinMpcSecretShares response;

        request.set_bob(bob_silo_id); // negative means all silos should be regarded as Bob
        Status status = m_silo_stub->CollectMinMpc(&context, request, &response); 
        if (!status.ok()) {
            std::cerr << "RPC failed: " << status.error_message() << std::endl;
            std::string error_message;
            error_message = std::string("Get secret share list from data silo #(") + std::to_string(m_silo_id) + std::string(") failed");
            throw std::invalid_argument(error_message);  
            std::exit(EXIT_FAILURE);
        }

        if (bob_silo_id < 0) {
            m_secret_share_list.clear();
            int secret_share_num = response.r_size();
            for (int i=0; i<secret_share_num; ++i) {
                m_secret_share_list.emplace_back(response.r(i));
            }
        } else {
            assert(m_silo_id != bob_silo_id);
            int secret_share_num = response.r_size();
            assert(secret_share_num == 2);
            m_secret_share_list[m_silo_id] = response.r(0);
            m_secret_share_list[bob_silo_id] = response.r(1);
        }

        #ifdef LOCAL_DEBUG
        std::cout << "Server receives secret share from silo " << m_silo_id << ": [";
        for (int i=0; i<m_secret_share_list.size(); ++i) {
            if (i > 0)
                std::cout << ", ";
            std::cout << m_secret_share_list[i];
        }
        std::cout << "]" << std::endl;
        #endif

        float grpc_comm = request.ByteSizeLong() + response.ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);
    }

    void JoinCollectMinMpc(const std::vector<QidType>& qids, int bob_silo_id) {
        ClientContext context;
        JoinMinMpcBobList request;
        JoinMinMpcSecretShares response;

        request.set_bob(bob_silo_id); // negative means all silos should be regarded as Bob
        for (auto qid : qids) {
            request.add_qid(qid);
        }
        Status status = m_silo_stub->JoinCollectMinMpc(&context, request, &response); 
        if (!status.ok()) {
            std::cerr << "RPC failed: " << status.error_message() << std::endl;
            std::string error_message;
            error_message = std::string("Get join secret share list from data silo #(") + std::to_string(m_silo_id) + std::string(") failed");
            throw std::invalid_argument(error_message);  
         }

        if (bob_silo_id < 0) {
            for (auto qid : qids) {
                m_join_secret_share_list[qid].clear();
            }
            int query_num = response.qid_size();
            assert(query_num == qids.size());
            
            for (int q = 0; q < query_num; ++q) {
                assert(qids[q] == response.qid(q));
                auto& min_ss = response.min_mss(q);
                const int qid = qids[q];

                int secret_share_num = min_ss.r_size();
                for (int i = 0; i < secret_share_num; ++i) {
                    m_join_secret_share_list[qid].emplace_back(min_ss.r(i));
                }
            }

        } else {
           assert(m_silo_id != bob_silo_id);

            int query_num = response.qid_size();
            assert(query_num == qids.size());
            
            for (int q = 0; q < query_num; ++q) {
                assert(qids[q] == response.qid(q));
                auto& min_ss = response.min_mss(q);
                const int qid = qids[q];

                int secret_share_num = min_ss.r_size();
                assert(secret_share_num == 2);
                m_join_secret_share_list[qid][m_silo_id] = min_ss.r(0);
                m_join_secret_share_list[qid][bob_silo_id] = min_ss.r(1);
            }
        }

        #ifdef LOCAL_DEBUG
        for (auto qid : qids) {
            std::cout << "Query #" << qid << std::endl;
            std::cout << "Server receives secret share from silo " << m_silo_id << ": [";
            for (int i=0; i<m_join_secret_share_list[qid].size(); ++i) {
                if (i > 0)
                    std::cout << ", ";
                std::cout << m_join_secret_share_list[qid][i];
            }
            std::cout << "]" << std::endl;
        }
        #endif

        float grpc_comm = request.ByteSizeLong() + response.ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);
    }

    std::vector<int> GetSecretShareList() const {
        return m_secret_share_list;
    }

    std::vector<int> GetJoinSecretShareList(const int qid) const {
        return m_join_secret_share_list[qid];
    }

    int GetSecretShare(int idx) const {
        return m_secret_share_list[idx];
    }

    int GetJoinSecretShare(const int qid, const int idx) const {
        return m_join_secret_share_list[qid][idx];
    }

    std::vector<VectorDataDistType> GetPartialKnn() const {
        return m_partial_knn;
    }

    std::vector<KnnQueryResult> GetPartialKnnJoin() const {
        return m_partial_knnjoin;
    }

    double GetMpcComm() const {
        return m_mpc_comm;
    }

    void CollectMpcComm() {
        double mpc_comm = 0.0;

        ClientContext context;
        Empty request;
        MpcComm response;

        Status status = m_silo_stub->GetMpcComm(&context, request, &response); 
        if (!status.ok()) {
            std::cerr << "RPC failed: " << status.error_message() << std::endl;
            std::string error_message;
            error_message = std::string("Get MPC communication from data silo #(") + std::to_string(m_silo_id) + std::string(") failed");
            throw std::invalid_argument(error_message);  
            std::exit(EXIT_FAILURE);
        }
        m_mpc_comm = response.comm();
    }

    void SetSearchParameter(const std::string search_param_name, const int search_param_value) {
        ClientContext context;
        SearchParam request;
        Empty response;

        request.set_search_param_name(search_param_name);
        request.set_search_param_value(search_param_value);

        Status status = m_silo_stub->SetSearchParameter(&context, request, &response); 
        if (!status.ok()) {
            std::cerr << "RPC failed: " << status.error_message() << std::endl;
            std::string error_message;
            error_message = std::string("Set search parameter from data silo #(") + std::to_string(m_silo_id) + std::string(") failed");
            throw std::invalid_argument(error_message);  
        }

        float grpc_comm = request.ByteSizeLong() + response.ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);
    }

    double GetQueryComm() const {
        return m_logger.GetQueryComm();
    }

    void InitBenchLogger() {
        m_logger.Init();
    }

    static void StartMinMpcThread(SiloReceiver* silo_receiver, int bob_silo_id) {  
        silo_receiver->StartMinMpc(bob_silo_id);  
    } 

    
    static void JoinStartMinMpcThread(SiloReceiver* silo_receiver, const std::vector<QidType>& qids, const int bob_silo_id) {  
        silo_receiver->JoinStartMinMpc(qids, bob_silo_id);  
    } 

    static void CollectMinMpcThread(SiloReceiver* silo_receiver, int bob_silo_id) {  
        silo_receiver->CollectMinMpc(bob_silo_id);  
    } 

    static void JoinCollectMinMpcThread(SiloReceiver* silo_receiver, const std::vector<QidType>& qids, int bob_silo_id) {  
        silo_receiver->JoinCollectMinMpc(qids, bob_silo_id);  
    } 

    static void RequestKnnCandThread(SiloReceiver* silo_receiver, const VectorDataType& query_data, const size_t& query_k) {  
        silo_receiver->RequestKnnCand(query_data, query_k);  
    } 

    static void RequestKnnJoinCandThread(SiloReceiver* silo_receiver, const std::vector<VectorDataType>& query_data_list, const std::vector<size_t>& query_k_list) {  
        silo_receiver->RequestKnnJoinCand(query_data_list, query_k_list);  
    } 

    static void RequestPartialKnnJoinThread(SiloReceiver* silo_receiver, const std::vector<KnnJoinCandReqParam>& join_request) {  
        silo_receiver->RequestPartialKnnJoin(join_request);
    }  

    static void RequestPartialKnnThread(SiloReceiver* silo_receiver, const size_t cand_num) {  
        silo_receiver->RequestPartialKnn(cand_num);
    }  

    static void ClearKnnCandThread(SiloReceiver* silo_receiver) {  
        silo_receiver->ClearKnnCand();  
    }  

    static void ClearKnnJoinCandThread(SiloReceiver* silo_receiver) {  
        silo_receiver->ClearKnnJoinCand();  
    }  

private:
    std::unique_ptr<FedSqlService::Stub> m_silo_stub;
    std::vector<VectorDataDistType> m_partial_knn;
    std::vector<KnnQueryResult> m_partial_knnjoin;
    std::vector<int> m_secret_share_list;
    std::vector<std::vector<int>> m_join_secret_share_list;
    std::string m_silo_ipaddr;
    double m_mpc_comm;
    int m_silo_id;  
    BenchLogger m_logger;  
};

class FedSqlServer {
public:
    FedSqlServer(const std::string& silo_ip_filename, const std::string& search_param_name, const int search_param_value) 
            : m_thread_num(
                #ifdef LOCAL_DEBUG
                        1  // Debug模式使用单线程，便于调试
                #else
                        std::thread::hardware_concurrency()
                #endif
                ), 
                m_silo_ip_filename(silo_ip_filename) {

        ReadSiloIPaddr(silo_ip_filename, m_silo_ipaddr_list);
        if (m_silo_ipaddr_list.empty()) {
            throw std::invalid_argument("No data silo's IP addresses");  
            std::exit(EXIT_FAILURE);
        }

        std::cout << "Server is running\n";

        m_CreateSiloReceiver();
        m_InitLocalCache();

        // Set search parameter in all silos
        m_SetSearchParameter(search_param_name, search_param_value);
    }

    FedSqlServer(const std::string& silo_ip_filename, const std::string& query_filename, const std::string& output_filename, const std::string& truth_filename, const std::string& drf_option, const int& query_k, const std::string& search_param_name, const int search_param_value)
                : FedSqlServer(silo_ip_filename, search_param_name, search_param_value) {
        
        if (false == m_IsJoin(drf_option)) {
            ProcessFedKnnQuery(query_filename, output_filename, drf_option, query_k);
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

    void ProcessFedQueryBatch(const std::string& query_filename, const std::string& output_filename, const std::string& drf_option, const int& query_k) {
       m_query_filename = query_filename;

        // Step 0: Initialize local variables
        m_InitBenchLogger();
        m_CheckDrfOption(drf_option);
        m_CheckIncrementalMode(drf_option);

        // Step 1: Read vector query from file
        ReadVectorQuery(query_filename, m_query_data_list, m_query_k_list);
        std::cout << "Server receives " << m_query_data_list.size() << " queries" << std::endl;

        // Step 2: Enumerate all vector queries
        const int query_num = m_query_data_list.size();
        m_query_k_list.assign(query_num, query_k);
        
        std::vector<std::vector<VectorDataType>> answer = ProcessFedKnnQueryBatch(m_query_data_list, m_query_k_list, drf_option);
        std::vector<std::vector<VidType>> answer_list;
        for (int i = 0; i < query_num; ++i) {
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

    void ProcessFedKnnQuery(const std::string& query_filename, const std::string& output_filename="", const std::string& drf_option="equality", const int &query_k=128) {
        m_query_filename = query_filename;

        // Step 0: Initialize local variables
        m_InitBenchLogger();
        m_CheckDrfOption(drf_option);
        m_CheckIncrementalMode(drf_option);

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

    std::vector<VectorDataType> ProcessFedKnnQueryOne(const VectorDataType& query_data, const size_t& query_k, const std::string& drf_option="equality") {
        std::vector<VectorDataType> answer;
        
        answer = m_ProcessFedKnnQuery_ByMpc(query_data, query_k, drf_option);
        
        return answer;
    }

    std::vector<std::vector<VectorDataType>> ProcessFedKnnQueryBatch(const std::vector<VectorDataType>& query_data_list, const std::vector<size_t>& query_k_list, const std::string& drf_option) {
        std::vector<std::vector<VectorDataType>> answer;
        
        if (drf_option==std::string("batch-pga") || drf_option==std::string("batch-pga-O0")) {
            answer = m_ProcessFedKnnQueryBatch(query_data_list, query_k_list);
        } else {
            throw std::invalid_argument("Invalid (batch) drf_option: " + drf_option + 
                                        ". Expected 'batch-pga'");
        }
        return answer;
    }

    std::string to_string() const {
        std::stringstream ss;

        ss << "-------------- Service Log --------------\n";
        ss << m_logger.to_string();

        return ss.str();
    }

private:
    std::vector<std::vector<VectorDataType>> m_ProcessFedKnnQueryBatch(const std::vector<VectorDataType>& query_data_list, const std::vector<size_t>& query_k_list) {
        m_logger.SetStartTimer();
        const int query_num = query_data_list.size();

        // Step 0: Intialize some variables
        std::vector<std::vector<VectorDataType>> join_answer(query_num);
        std::vector<std::vector<KnnJoinCandReqParam>> join_allocation(m_silo_num);
        std::vector<int> prev_optimal_silo_ids(query_num, -1);
        for (int i = 0; i < query_num; ++i) {
            prev_optimal_silo_ids[i] = -1 - query_data_list[i].vid;
        }

        // Step 1: Perform local knn query at each silo
        // std::cout << "Step 1: perform local knn join" << std::endl;
        m_PerformLocalKnnJoin(query_data_list, query_k_list);

        // Step 2: Determine the resource allocation for each silo
        const int retrieve_step_size = 1;
        const int query_k = query_k_list[0];
        for (int rid = 0; rid < query_k; ++rid) {
            // std::cout << "Round #" << rid << ": get (join) most relevant silo" << std::endl;
            std::vector<int> optimal_silo_ids = m_GetJoinMostRelevantSilo(rid, prev_optimal_silo_ids);
            for (int i = 0; i < m_silo_num; ++i) {
                join_allocation[i].clear();
            }
            for (int qid = 0; qid < query_num; ++qid) {
                int optimal_silo_id = optimal_silo_ids[qid];

                KnnJoinCandReqParam req_param;
                req_param.k = retrieve_step_size;
                req_param.qid = qid;
                join_allocation[optimal_silo_id].emplace_back(req_param);

                prev_optimal_silo_ids[qid] = optimal_silo_id;
            }
            m_RetrievePartialKnnJoin(join_allocation);

            /// Collect the query answer
            for (int silo_id = 0; silo_id < m_silo_num; ++silo_id) {
                if (join_allocation[silo_id].empty()) continue;
                
                std::vector<KnnQueryResult> partial_knnjoin_list = m_silo_receiver_list[silo_id]->GetPartialKnnJoin();
                for (auto& query_result : partial_knnjoin_list) {
                    QidType qid = query_result.qid;
                    auto& knn = query_result.knn;
                    for (auto& nn : knn) {
                        VectorDataType vector_data(nn.Dimension(), nn.vid);
                        vector_data.data = nn.data;
                        join_answer[qid].emplace_back(vector_data);
                    }
                }
            }
        }

        // Step 4: Clear Knn candidates at each silo
        m_ClearKnnJoinCand();

        // Step 5: Print the log information
        m_logger.SetEndTimer();
        double mpc_comm = 0.0;
        double query_comm = 0.0;
        for (int i=0; i<m_silo_num; ++i) {
            m_silo_receiver_list[i]->CollectMpcComm();
            double cur_mpc_comm = m_silo_receiver_list[i]->GetMpcComm();
            mpc_comm += cur_mpc_comm;
        }
        query_comm += mpc_comm;
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

    std::vector<VectorDataType> m_ProcessFedKnnQuery_ByMpc(const VectorDataType& query_data, const size_t& query_k, const std::string& drf_option) {
        m_logger.SetStartTimer();

        // Step 0: Intialize some variables
        std::vector<VectorDataType> answer;

        // Step 1: Perform local knn query at each silo
        m_PerformLocalKnn(query_data, query_k);

        // Step 2: Determine the resource allocation for each silo
        const int retrieve_step_size = 1;
        int prev_optimal_silo_id = -1 - query_data.vid;
        for (size_t rid = 0; rid < query_k; ++rid) {
            int optimal_silo_id = m_GetMostRelevantSilo(rid, prev_optimal_silo_id);

            SiloReceiver::RequestPartialKnnThread(m_silo_receiver_list[optimal_silo_id].get(), retrieve_step_size);
            std::vector<VectorDataDistType> partial_knn_list = m_silo_receiver_list[optimal_silo_id]->GetPartialKnn();
            for (auto& knn : partial_knn_list) {
                VectorDataType vector_data(knn.Dimension(), knn.vid);
                vector_data.data = knn.data;
                answer.emplace_back(vector_data);
            }
            
            prev_optimal_silo_id = optimal_silo_id;
        }

        // Step 4: Clear Knn candidates at each silo
        m_ClearKnnCand();

        // Step 5: Print the log information
        m_logger.SetEndTimer();
        double mpc_comm = 0.0;
        double query_comm = 0.0;
        for (int i=0; i<m_silo_num; ++i) {
            m_silo_receiver_list[i]->CollectMpcComm();
            double cur_mpc_comm = m_silo_receiver_list[i]->GetMpcComm();
            mpc_comm += cur_mpc_comm;
        }
        query_comm += mpc_comm;
        for (int i=0; i<m_silo_num; ++i) {
            query_comm += m_silo_receiver_list[i]->GetQueryComm();
        }
        query_comm -= m_logger.GetQueryComm();
        double query_time = m_logger.GetDurationTime();
        m_logger.LogOneQuery(query_comm);

        std::cout << std::fixed << std::setprecision(6) 
                    << "Query #(" << query_data.vid << "): runtime = " << query_time << " [ms], communication = " << query_comm/1024.0 << " [KB]" << std::endl;
    
        return answer;
    }

    int m_GetMostRelevantSilo(int rid, int prev_optimal_silo_id) {
        int optimal_silo_id = -1;

        // 1. enable all data silo as alice
        m_StartMinMpc(prev_optimal_silo_id);

        // 2. collect the secret shares from all silos
        m_CollectMinMpc(prev_optimal_silo_id);

        // 3. pick the minimum (if there're ties, randomly pick on)
        m_AssembleSecretShare(prev_optimal_silo_id);

        int mn = INT_MAX, cnt = 0;
        for (int i=0; i<m_silo_num; ++i) {
            if (m_sum_secret_list[i] < mn) {
                mn = m_sum_secret_list[i];
                optimal_silo_id = i;
                cnt = 1;
            } else {
                ++cnt;
            }
        }
        #ifdef LOCAL_DEBUG
        std::cout << "Round #" << rid << ": min = " << mn << ", cnt = " << cnt << std::endl;
        assert(optimal_silo_id>=0 && mn==0);
        #endif

        #ifdef LOCAL_DEBUG
        std::cout << "Sum of secret shares: [";
        for (int i=0; i<m_silo_num; ++i) {
            if (i > 0)
                std::cout << ", ";
            std::cout << m_sum_secret_list[i];
        }
        std::cout << "]" << std::endl;
        std::cout << "Most relevant silo: " << optimal_silo_id << ", value = " << std::fixed << std::setprecision(6) << mn << std::endl;
        std::cout << "------------------------------------------------" << std::endl;
        #endif

        return optimal_silo_id;
    }

    std::vector<int> m_GetJoinMostRelevantSilo(int rid, const std::vector<int>& prev_optimal_silo_ids) {
        // 1. enable all data silo as alice
        // std::cout << "Round #" << rid << ": join start min mpc" << std::endl;
        m_JoinStartMinMpc(prev_optimal_silo_ids);

        // 2. collect the secret shares from all silos
        // std::cout << "Round #" << rid << ": join collect min mpc" << std::endl;
        m_JoinCollectMinMpc(prev_optimal_silo_ids);

        // 3. pick the minimum (if there're ties, randomly pick on)
        // std::cout << "Round #" << rid << ": join assemble secret share" << std::endl;
        m_JoinAssembleSecretShare(prev_optimal_silo_ids);

        // 4. compute the minimum, which is the optimal silo id
        // std::cout << "Round #" << rid << ": join compute optimal silo ID" << std::endl;
        const int query_num = prev_optimal_silo_ids.size();
        std::vector<int> optimal_silo_ids(query_num, -1);

        #ifdef LOCAL_DEBUG
        std::vector<int> mn_list;
        #endif

        for (int qid = 0; qid < query_num; ++qid) {
            int mn = INT_MAX, optimal_silo_id = -1, cnt = 0;
            for (int i = 0; i < m_silo_num; ++i) {
                if (m_join_sum_secret_list[qid][i] < mn) {
                    mn = m_join_sum_secret_list[qid][i];
                    optimal_silo_id = i;
                    cnt = 1;
                } else if (m_join_sum_secret_list[qid][i] == mn) {
                    ++cnt;
                }
            }
            optimal_silo_ids[qid] = optimal_silo_id;

            #ifdef LOCAL_DEBUG
            std::cout << "Round #" << rid << ": qid = " << qid << ", min = " << mn << ", cnt = " << cnt << std::endl;
            assert(optimal_silo_id >= 0 && mn==0);
            mn_list.emplace_back(mn);
            #endif
        }

        #ifdef LOCAL_DEBUG
        for (int qid = 0; qid < query_num; ++qid) {
            std::cout << "Query #" << qid << std::endl;
            std::cout << "Sum of secret shares: [";
            for (int i=0; i<m_silo_num; ++i) {
                if (i > 0)
                    std::cout << ", ";
                std::cout << m_join_sum_secret_list[qid][i];
            }
            std::cout << "]" << std::endl;
            std::cout << "Most relevant silo: " << optimal_silo_ids[qid] << ", value = " << std::fixed << std::setprecision(6) << mn_list[qid] << std::endl;
            std::cout << "------------------------------------------------" << std::endl;
        }
        #endif

        return optimal_silo_ids;
    }

    void m_AssembleSecretShare(int prev_optimal_silo_id) {
        if (false==m_incremental_mpc) prev_optimal_silo_id = -1;

        if (prev_optimal_silo_id < 0) {
            std::fill(m_sum_secret_list.begin(), m_sum_secret_list.end(), 0);
            for (int i=0; i<m_silo_num; ++i) {
                m_secret_share_list[i] = m_silo_receiver_list[i]->GetSecretShareList();
                for (int j=0; j<m_silo_num; ++j) {
                    m_sum_secret_list[j] += m_secret_share_list[i][j];
                }
            }

        } else {
            for (int i=0; i<m_silo_num; ++i) {
                if (i == prev_optimal_silo_id) {
                    std::vector<int> tmp_secret_share_list = m_silo_receiver_list[i]->GetSecretShareList();
                    for (int j=0; j<m_silo_num; ++j) {
                        m_sum_secret_list[j] += (tmp_secret_share_list[j] - m_secret_share_list[i][j]);
                        m_secret_share_list[i][j] = tmp_secret_share_list[j];
                    }
                                   
                } else {

                    // update the secret share when $i$th silo acts as alice
                    int alice_secret_share = m_silo_receiver_list[i]->GetSecretShare(i);
                    m_sum_secret_list[i] += (alice_secret_share - m_secret_share_list[i][i]);
                    m_secret_share_list[i][i] = alice_secret_share;

                    // update the secret share when $i$th silo acts as bob
                    int bob_secret_share = m_silo_receiver_list[i]->GetSecretShare(prev_optimal_silo_id);
                    m_sum_secret_list[prev_optimal_silo_id] += (bob_secret_share - m_secret_share_list[i][prev_optimal_silo_id]);
                    m_secret_share_list[i][prev_optimal_silo_id] = bob_secret_share;
                }
            }

        }

        #ifdef LOCAL_DEBUG
        std::cout << "Server maintains sum of secret shares: [";
        for (int i=0; i<m_sum_secret_list.size(); ++i) {
            if (i > 0)
                std::cout << ", ";
            std::cout << m_sum_secret_list[i];
        }
        std::cout << "]" << std::endl;
        #endif
    }

    void m_JoinAssembleSecretShare(const std::vector<int>& prev_optimal_silo_ids) {
        const int query_num = prev_optimal_silo_ids.size();
        int neg_count = 0;

        for (auto prev_optimal_sid : prev_optimal_silo_ids) {
            if (prev_optimal_sid < 0) ++neg_count;
        }
        if (neg_count>0 && neg_count!=query_num) {
            throw std::invalid_argument(
                "prev_optimal_silo_ids must contain all or zero negative elements, "
                "but found " + std::to_string(neg_count)
            );
        }

        if (false==m_incremental_mpc || neg_count>0) {
            m_join_sum_secret_list.assign(query_num, std::vector<int>(m_silo_num, 0));
            for (int qid = 0; qid < query_num; ++qid) {
                for (int i = 0; i < m_silo_num; ++i) {
                    m_join_secret_share_list[qid][i] = m_silo_receiver_list[i]->GetJoinSecretShareList(qid);
                    for (int j = 0; j < m_silo_num; ++j) {
                        m_join_sum_secret_list[qid][j] += m_join_secret_share_list[qid][i][j];
                    }
                }
            }
        } else {
            for (int qid = 0; qid < query_num; ++qid) {
                int prev_optimal_silo_id = prev_optimal_silo_ids[qid];
                for (int i = 0; i < m_silo_num; ++i) {
                    if (i == prev_optimal_silo_id) {
                        std::vector<int> tmp_secret_share_list = m_silo_receiver_list[i]->GetJoinSecretShareList(qid);
                        for (int j = 0; j < m_silo_num; ++j) {
                            m_join_sum_secret_list[qid][j] += (tmp_secret_share_list[j] - m_join_secret_share_list[qid][i][j]);
                            m_join_secret_share_list[qid][i][j] = tmp_secret_share_list[j];
                        }
                    } else {
                        // update the secret share when $i$th silo acts as alice
                        int alice_secret_share = m_silo_receiver_list[i]->GetJoinSecretShare(qid, i);
                        m_join_sum_secret_list[qid][i] += (alice_secret_share - m_join_secret_share_list[qid][i][i]);
                        m_join_secret_share_list[qid][i][i] = alice_secret_share;

                        // update the secret share when $i$th silo acts as bob
                        int bob_secret_share = m_silo_receiver_list[i]->GetJoinSecretShare(qid, prev_optimal_silo_id);
                        m_join_sum_secret_list[qid][prev_optimal_silo_id] += (bob_secret_share - m_join_secret_share_list[qid][i][prev_optimal_silo_id]);
                        m_join_secret_share_list[qid][i][prev_optimal_silo_id] = bob_secret_share;
                    }
                }
            }
        }

        #ifdef LOCAL_DEBUG
        for (int qid = 0; qid < query_num; ++qid) {
            std::cout << "Query #" << qid << std::endl;
            std::cout << "Server maintains sum of secret shares: [";
            for (int i=0; i<m_sum_secret_list.size(); ++i) {
                if (i > 0)
                    std::cout << ", ";
                std::cout << m_sum_secret_list[i];
            }
            std::cout << "]" << std::endl;
        }
        #endif
    }

    void m_StartMinMpc(int prev_optimal_silo_id) {
        if (false==m_incremental_mpc) prev_optimal_silo_id = -1;

        if (prev_optimal_silo_id < 0) {
            boost::asio::thread_pool pool(m_thread_num);
            const int bob_silo_id = prev_optimal_silo_id;

            for (int alice_silo_id=0; alice_silo_id<m_silo_num; ++alice_silo_id) {
                boost::asio::post(pool, std::bind(SiloReceiver::StartMinMpcThread, m_silo_receiver_list[alice_silo_id].get(), bob_silo_id)); 
            }

            pool.join(); 

        } else {
            int alice_silo_id, bob_silo_id;

            boost::asio::thread_pool pool(m_thread_num);

            // 1. currnt silo (i.e., prev_optimal_silo_id) acts as Alice and changes all
            alice_silo_id = prev_optimal_silo_id;
            bob_silo_id = -2;
            boost::asio::post(pool, std::bind(SiloReceiver::StartMinMpcThread, m_silo_receiver_list[alice_silo_id].get(), bob_silo_id)); 

            // 2. currnt silo (i.e., prev_optimal_silo_id) acts as Bob and is changed by Alice
            bob_silo_id = prev_optimal_silo_id;
            for (alice_silo_id=0; alice_silo_id<m_silo_num; ++alice_silo_id) {
                if (alice_silo_id == bob_silo_id)
                    continue;
                boost::asio::post(pool, std::bind(SiloReceiver::StartMinMpcThread, m_silo_receiver_list[alice_silo_id].get(), bob_silo_id)); 
            }            
     
            pool.join(); 
        }
    }

    void m_JoinStartMinMpc(const std::vector<int>& prev_optimal_silo_ids) {
        const int query_num = prev_optimal_silo_ids.size();
        int neg_count = 0;

        for (auto prev_optimal_sid : prev_optimal_silo_ids) {
            if (prev_optimal_sid < 0) ++neg_count;
        }
        if (neg_count>0 && neg_count!=query_num) {
            throw std::invalid_argument(
                "prev_optimal_silo_ids must contain all or zero negative elements, "
                "but found " + std::to_string(neg_count)
            );
        }

        if (false==m_incremental_mpc || neg_count>0) {
            std::vector<QidType> qids(query_num);
            for (int i = 0; i < query_num; ++i) {
                qids[i] = i;
            }

            boost::asio::thread_pool pool(m_thread_num);
            // change into a negative value
            const int bob_silo_id = -1; //prev_optimal_silo_ids[0];

            for (int alice_silo_id = 0; alice_silo_id < m_silo_num; ++alice_silo_id) {
                boost::asio::post(pool, std::bind(SiloReceiver::JoinStartMinMpcThread, m_silo_receiver_list[alice_silo_id].get(), qids, bob_silo_id)); 
            }

            pool.join(); 

        } else {
            std::map<std::pair<int, int>, std::vector<QidType>> marked;
            int alice_silo_id, bob_silo_id;

            for (int qid = 0; qid < query_num; ++qid) {
                int prev_optimal_silo_id = prev_optimal_silo_ids[qid];

                alice_silo_id = prev_optimal_silo_id;
                bob_silo_id = -2;
                std::pair<int,int> key(alice_silo_id, bob_silo_id);
                marked[key].emplace_back(qid);

                bob_silo_id = prev_optimal_silo_id;
                for (alice_silo_id=0; alice_silo_id<m_silo_num; ++alice_silo_id) {
                    if (alice_silo_id == bob_silo_id)
                        continue;
                    key = std::make_pair(alice_silo_id, bob_silo_id);
                    marked[key].emplace_back(qid);
                }
            }

            boost::asio::thread_pool pool(m_thread_num);
            for (auto it = marked.begin(); it != marked.end(); ++it) {
                const auto& key = it->first;
                const auto& qids = it->second;
                alice_silo_id = key.first;
                bob_silo_id = key.second;
                
                boost::asio::post(pool, std::bind(SiloReceiver::JoinStartMinMpcThread, m_silo_receiver_list[alice_silo_id].get(), qids, bob_silo_id)); 
            }      
     
            pool.join(); 
        }
    }

    void m_JoinCollectMinMpc(const std::vector<int>& prev_optimal_silo_ids) {
        const int query_num = prev_optimal_silo_ids.size();
        int neg_count = 0;

        for (auto prev_optimal_sid : prev_optimal_silo_ids) {
            if (prev_optimal_sid < 0) ++neg_count;
        }
        if (neg_count>0 && neg_count!=query_num) {
            throw std::invalid_argument(
                "prev_optimal_silo_ids must contain all or zero negative elements, "
                "but found " + std::to_string(neg_count)
            );
        }

        if (false==m_incremental_mpc || neg_count>0) {
            std::vector<QidType> qids(query_num);
            for (int i = 0; i < query_num; ++i) {
                qids[i] = i;
            }

            boost::asio::thread_pool pool(m_thread_num);
            // set it as a negative value
            const int bob_silo_id = -1; //prev_optimal_silo_ids[0];

            for (int alice_silo_id = 0; alice_silo_id < m_silo_num; ++alice_silo_id) {
                boost::asio::post(pool, std::bind(SiloReceiver::JoinCollectMinMpcThread, m_silo_receiver_list[alice_silo_id].get(), qids, bob_silo_id)); 
            }

            pool.join(); 
                   
        } else {
            std::map<std::pair<int, int>, std::vector<QidType>> marked;
            int alice_silo_id, bob_silo_id;

            for (int qid = 0; qid < query_num; ++qid) {
                int prev_optimal_silo_id = prev_optimal_silo_ids[qid];

                alice_silo_id = prev_optimal_silo_id;
                bob_silo_id = -1;
                std::pair<int,int> key(alice_silo_id, bob_silo_id);
                marked[key].emplace_back(qid);

                bob_silo_id = prev_optimal_silo_id;
                for (alice_silo_id = 0; alice_silo_id < m_silo_num; ++alice_silo_id) {
                    if (alice_silo_id == bob_silo_id)
                        continue;
                    key = std::make_pair(alice_silo_id, bob_silo_id);
                    marked[key].emplace_back(qid);
                }
            }

            boost::asio::thread_pool pool(m_thread_num);
            for (auto it = marked.begin(); it != marked.end(); ++it) {
                const auto& key = it->first;
                const auto& qids = it->second;
                alice_silo_id = key.first;
                bob_silo_id = key.second;
                
                boost::asio::post(pool, std::bind(SiloReceiver::JoinCollectMinMpcThread, m_silo_receiver_list[alice_silo_id].get(), qids, bob_silo_id)); 
            }      
     
            pool.join(); 
        }       
    }

    void m_CollectMinMpc(int prev_optimal_silo_id) {
        if (false==m_incremental_mpc) prev_optimal_silo_id = -1;

        if (prev_optimal_silo_id < 0) {
            boost::asio::thread_pool pool(m_thread_num);
            const int bob_silo_id = prev_optimal_silo_id;

            for (int alice_silo_id=0; alice_silo_id<m_silo_num; ++alice_silo_id) {
                boost::asio::post(pool, std::bind(SiloReceiver::CollectMinMpcThread, m_silo_receiver_list[alice_silo_id].get(), bob_silo_id)); 
            }

            pool.join(); 
                   
        } else {
            int alice_silo_id, bob_silo_id;

            boost::asio::thread_pool pool(m_thread_num);

            // 1. currnt silo (i.e., prev_optimal_silo_id) acts as Alice and changes all
            alice_silo_id = prev_optimal_silo_id;
            bob_silo_id = -1;
            boost::asio::post(pool, std::bind(SiloReceiver::CollectMinMpcThread, m_silo_receiver_list[alice_silo_id].get(), bob_silo_id)); 

            // 2. currnt silo (i.e., prev_optimal_silo_id) acts as Bob and is changed by Alice
            bob_silo_id = prev_optimal_silo_id;
            for (alice_silo_id = 0; alice_silo_id < m_silo_num; ++alice_silo_id) {
                if (alice_silo_id == bob_silo_id)
                    continue;
                boost::asio::post(pool, std::bind(SiloReceiver::CollectMinMpcThread, m_silo_receiver_list[alice_silo_id].get(), bob_silo_id)); 
            }            
     
            pool.join(); 
        }       
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

    void m_PerformLocalKnn(const VectorDataType& query_data, const size_t& query_k) {
        boost::asio::thread_pool pool(m_thread_num);

        for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
            boost::asio::post(pool, std::bind(SiloReceiver::RequestKnnCandThread, m_silo_receiver_list[silo_id].get(), query_data, query_k)); 
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

    void m_CheckIncrementalMode(const std::string& drf_option) {
        if (drf_option=="PGA" || drf_option == "batch-pga") {
            m_incremental_mpc = true;
        } else {// drf_option=="PGA-O0" || drf_option == "batch-pga-O0"
            m_incremental_mpc = false;
        }
    }

    void m_CheckDrfOption(const std::string& drf_option) {
        std::string input_drf_option(drf_option);
 
        if (input_drf_option == "PGA") {
            std::cout << "Using ``mpc`` as our greedy algorithm for k is smaller than m" << std::endl;
        } else if (input_drf_option == "batch-pga") {
            std::cout << "Using ``batch mpc`` as our greedy algorithm for k is smaller than m" << std::endl;
        } else if (input_drf_option == "PGA-O0") {
            std::cout << "Using ``mpc --O0`` as our greedy algorithm for k is smaller than m" << std::endl;
        } else if (input_drf_option == "batch-pga-O0") {
            std::cout << "Using ``batch mpc --O0`` as our greedy algorithm for k is smaller than m" << std::endl;
        } else {
            std::cerr << "Found unknown option for dominant resource allocation: " << drf_option 
                        << ", possible options are [PGA | PGA-O0 | batch-pga | batch-pga-O0]" << std::endl;
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
            std::cout << "Server is connecting Silo #(" << std::to_string(silo_id) << ") on IP address " << silo_ipaddr << std::endl;

            std::shared_ptr<grpc::Channel> channel = grpc::CreateCustomChannel(silo_ipaddr, grpc::InsecureChannelCredentials(), args);
            m_silo_receiver_list[silo_id] = std::make_shared<SiloReceiver>(channel, silo_id, silo_ipaddr);
        }
    }

    void m_InitLocalCache() {
        m_sum_secret_list.assign(m_silo_num, 0);
        m_secret_share_list.assign(m_silo_num, std::vector<int>(m_silo_num, 0));
        m_join_sum_secret_list.assign(MAX_QUERY_NUM, m_sum_secret_list);
        m_join_secret_share_list.assign(MAX_QUERY_NUM, m_secret_share_list);
    }

    std::vector<VectorDataType> m_PickGlobalKnn(const VectorDataType& query_data, const size_t& query_k) {
        std::vector<VectorDataType> answer;
        if (std::is_same<Distance, InnerProductDistance>::value) {
            answer = m_PickGlobalKnn_ForMIPS(query_data, query_k);
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

        for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
            std::vector<VectorDataDistType> partial_knn_list = m_silo_receiver_list[silo_id]->GetPartialKnn();
            for (auto& knn : partial_knn_list) {
                VectorDataType vector_data(knn.Dimension(), knn.vid);
                vector_data.data = knn.data;
                cand_answer.emplace_back(vector_data);

                VectorDimensionType dist = knn.dist;
                size_t idx = cand_answer.size() - 1;

                if (min_heap_size < query_k) {
                    ++min_heap_size;
                    min_heap.push(std::make_pair(dist, idx));
                } else if (dist < min_heap.top().first) {  
                    min_heap.pop();
                    min_heap.push(std::make_pair(dist, idx));
                }  
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

    // specific for maximum inner product search
    std::vector<VectorDataType> m_PickGlobalKnn_ForMIPS(const VectorDataType& query_data, const size_t& query_k) {
        std::vector<VectorDataType> query_answer;
        std::vector<VectorDataType> cand_answer;
        std::priority_queue<std::pair<VectorDimensionType, size_t>, std::vector<std::pair<VectorDimensionType, size_t>>, std::greater<std::pair<VectorDimensionType, size_t>>> min_heap;  
        size_t min_heap_size = 0;

        for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
            std::vector<VectorDataDistType> partial_knn_list = m_silo_receiver_list[silo_id]->GetPartialKnn();
            for (auto& knn : partial_knn_list) {
                VectorDataType vector_data(knn.Dimension(), knn.vid);
                vector_data.data = knn.data;
                cand_answer.emplace_back(vector_data);

                VectorDimensionType dist = knn.dist;
                size_t idx = cand_answer.size() - 1;

                if (min_heap_size < query_k) {
                    ++min_heap_size;
                    min_heap.push(std::make_pair(dist, idx));
                } else if (dist < min_heap.top().first) {  
                    min_heap.pop();
                    min_heap.push(std::make_pair(dist, idx));
                }  
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
        if (search_param_name.empty()) return ;

        for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
            m_silo_receiver_list[silo_id]->SetSearchParameter(search_param_name, search_param_value);
        }    
    }

    std::vector<std::vector<int>> m_secret_share_list;
    std::vector<int> m_sum_secret_list;
    std::vector<std::vector<int>> m_join_sum_secret_list;
    std::vector<std::vector<std::vector<int>>> m_join_secret_share_list;
    std::vector<std::shared_ptr<SiloReceiver>> m_silo_receiver_list;
    std::string m_silo_ip_filename;
    std::string m_query_filename;
    std::vector<std::string> m_silo_ipaddr_list;
    std::vector<VectorDataType> m_query_data_list;
    std::vector<size_t> m_query_k_list;
    BenchLogger m_logger;
    int m_silo_num;
    const int m_thread_num;
    bool m_incremental_mpc = true;
};

std::unique_ptr<FedSqlServer> fed_sqlserver_ptr = nullptr;

void RunService(const std::string& silo_ip_filename, const std::string& query_filename, const std::string& output_filename, const std::string& truth_filename, const std::string& drf_option, const int& query_k, const std::string& search_param_name, const int search_param_value) {
    fed_sqlserver_ptr = std::make_unique<FedSqlServer>(silo_ip_filename, query_filename, output_filename, truth_filename, drf_option, query_k, search_param_name, search_param_value);

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
    std::string drf_option("mpc");
    int query_k;
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
            ("option", bpo::value<std::string>(), "Option for k allocation: [PGA | PGA-O0 | batch-pga | batch-pga-O0]")
            ("query-k", bpo::value<int>(&query_k)->default_value(128), "The K of KNN")
            // ====== 新增：搜索参数配置 ======
            ("search-param-name", bpo::value<std::string>(), 
            "Search parameter name: [nprobe | efSearch]")
            ("search-param-value", bpo::value<int>(), 
            "Search parameter value (integer)")
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
            std::cout << "Server's k allocation strategy was not set" << "\n";
        }

        if (variable_map.count("query-k")) {
            query_k = variable_map["query-k"].as<int>();
            std::cout << "The K of KNN was set to " << query_k << "\n";
        } else {
            std::cout << "The K of KNN was not set" << "\n";
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

    RunService(silo_ip_filename, query_filename, output_filename, truth_filename, drf_option, query_k, search_param_name, search_param_value);

    return 0;
}

