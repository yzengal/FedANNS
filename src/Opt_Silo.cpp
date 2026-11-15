#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <cstdlib>
#include <functional>
#include <exception>
#include <signal.h>
#include <unistd.h>
#include <omp.h>

#include <boost/asio.hpp>  
#include <boost/asio/thread_pool.hpp>
#include <boost/program_options.hpp>
namespace bpo = boost::program_options;

#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>

#include "FedVectorDBO.grpc.pb.h"
#include "Broker.grpc.pb.h"

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
using FedVectorDBO::VectorData;
using FedVectorDBO::KnnCand;
using FedVectorDBO::MpcComm;
using FedVectorDBO::MinMpcBobList;
using FedVectorDBO::MinMpcAlicePair;
using FedVectorDBO::MinMpcSecretShares;
using FedVectorDBO::JoinMinMpcSecretShares;
using FedVectorDBO::FedSqlService;
using FedVectorDBO::KnnJoinSql;
using FedVectorDBO::KnnJoinCandReq;
using FedVectorDBO::KnnJoinCand;
using FedVectorDBO::JoinMinMpcAlicePair;
using FedVectorDBO::JoinMinMpcBobList;
using FedVectorDBO::SearchParam;

using Broker::PerturbResult;
using Broker::RequestRandom;
using Broker::RandomValue;
using Broker::RequestSecretShare;
using Broker::SecretShare;
using Broker::RequestSecretShareList;
using Broker::SecretShareList;
using Broker::BrokerService;
using Broker::JoinPerturbResult;
using Broker::JoinRequestSecretShare;
using Broker::JoinSecretShare;
using Broker::JoinRequestSecretShareList;
using Broker::JoinSecretShareList;

using Distance = EuclideanSquareDistance;
const int MAX_QUERY_NUM = 100;

class FedVectorDBImpl final : public FedSqlService::Service {
public:
    explicit FedVectorDBImpl(const int silo_id, const std::string& silo_ipaddr, const std::string& data_filename, 
                                const std::string& third_party_ipaddr, const std::string& silo_ip_filename, const std::string& index_type)
                                : m_thread_num(std::thread::hardware_concurrency()), m_silo_id(silo_id), m_broker_ipaddr(third_party_ipaddr), m_rd(), m_gen(m_rd()), m_uniform_dis(1, 100) {
        
        m_InitClientStub(silo_ip_filename);
        #ifdef LOCAL_DEBUG
        std::cout << "[FINISH] Init the client stub to the other silos" << std::endl;
        #endif

        m_silo_ptr = std::make_unique<FileSiloConnector<Distance>>(silo_id, silo_ipaddr);
        
        m_silo_ptr->ImportData(data_filename);
        size_t data_size = m_silo_ptr->DataSize();
        std::cout << "Data silo holds " << std::to_string(data_size) << " vector data" << std::endl;

        m_logger.SetStartTimer();
        if(m_silo_ptr->IndexExists(data_filename, index_type)) {
            std::cout << "Index loaded from disk" << std::endl;
            fflush(stdout);
            m_silo_ptr->LoadIndex(data_filename, index_type);
        } else {
            std::cout << "Construct index in advance" << std::endl;
            fflush(stdout);
            m_silo_ptr->ConstructIndex(index_type);
        }
        m_logger.SetEndTimer();
        double construct_time = m_logger.GetDurationTime();
        std::cout << std::fixed << std::setprecision(6) << "Index construct time = " << construct_time/1000.0 << " [s]" << std::endl;
        size_t index_size = m_silo_ptr->IndexSize();
        std::cout << "Index size: " << index_size/1024 << " [KB]" << std::endl;
    }

    Status SetSearchParameter(ServerContext* context,
                                const SearchParam* request,
                                Empty* response) override {
        
        // 1. 检查参数名是否为空
        if (request->search_param_name().empty()) {
            return grpc::Status(
                grpc::StatusCode::INVALID_ARGUMENT,
                "Parameter name cannot be empty"
            );
        }
        
        // 2. 检查参数值是否有效
        if (request->search_param_value() < 0) {
            return grpc::Status(
                grpc::StatusCode::INVALID_ARGUMENT,
                "Parameter value must be non-negative"
            );
        }
        
        // 3. 检查参数是否支持（根据当前索引类型）
        std::string search_param_name = request->search_param_name();
        if (search_param_name != "nprobe" && search_param_name != "efsearch") {
            return grpc::Status(
                grpc::StatusCode::INVALID_ARGUMENT,
                "Unsupported parameter: " + search_param_name
            );
        }
        
        int search_param_value = request->search_param_value();
        m_silo_ptr->SetSearchParam(search_param_name, search_param_value);
        
        return Status::OK;
    }

    Status ExecuteKnnQuery(ServerContext* context,
                            const KnnSql* request,
                            Empty* empty_response) override {
        m_logger.SetStartTimer();

        const size_t dim = request->data_size();
        VectorDataType query_data(dim, request->vid());
        for (int i=0; i<dim; ++i) {
            query_data[i] = request->data(i);
        }
        size_t query_k = request->k();

        m_local_knn_list = m_silo_ptr->KnnQueryWithDist(query_data, query_k);
        m_local_knn_head = 0;

        double grpc_comm = request->ByteSizeLong() + empty_response->ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);

        m_logger.SetEndTimer();
        m_index_query_num++;
        m_index_query_time += m_logger.GetDurationTime();
        m_logger.LogAddTime();

        #ifdef LOCAL_DEBUG
        std::cout << "Silo local knn:";
        for (auto vector_data : m_local_knn_list) {
            std::cout << " " << vector_data.vid;
        }
        std::cout << std::endl;
        std::cout << "Silo local knn distance:";
        for (auto vector_data : m_local_knn_list) {
            std::cout << " " << vector_data.dist;
        }
        std::cout << std::endl;
        #endif

        return Status::OK;
    }

    Status ExecuteKnnJoin(ServerContext* context,
                            const KnnJoinSql* request,
                            Empty* empty_response) override {
        m_logger.SetStartTimer();

        #ifdef LOCAL_DEBUG
        std::cout << "Silo will perform ``ExecuteKnnJoin``" << std::endl;
        #endif

        const size_t query_num = request->queries_size();

        m_local_knnjoin_list.assign(query_num, std::vector<VectorDataDistType>());
        m_local_knnjoin_head.assign(query_num, 0);
        
        for (int i = 0; i < query_num; ++i) {
            const int query_k = request->k(i);
            const auto& vector_data = request->queries(i);
            const int qid = vector_data.vid();

            #ifdef LOCAL_DEBUG
            std::cout << "Query #" << qid << "/" << query_num << " : ";
            #endif

            VectorDataType query_data(vector_data.data_size(), qid);
            for (int j = 0; j < vector_data.data_size(); ++j) {
                query_data.data[j] = vector_data.data(j);
            }

            #ifdef LOCAL_DEBUG
            std::cout << query_data.to_string() << std::endl;
            #endif
            
            m_local_knnjoin_list[qid] = m_silo_ptr->KnnQueryWithDist(query_data, query_k);
            m_local_knnjoin_head[qid] = 0;

            #ifdef LOCAL_DEBUG
            assert(m_local_knnjoin_list[qid].size() == query_k);
            #endif
        }
        
        double grpc_comm = request->ByteSizeLong() + empty_response->ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);
        m_logger.SetEndTimer();
        m_logger.LogAddTime();

        #ifdef LOCAL_DEBUG
        std::cout << "Silo local knn join:";
        for (size_t qid = 0; qid < m_local_knnjoin_list.size(); ++qid) {
            const auto& candidates = m_local_knnjoin_list[qid];
            if (!candidates.empty()) {
                std::cout << " QID" << qid << ":[";
                for (size_t i = 0; i < candidates.size(); ++i) {
                    std::cout << candidates[i].vid;
                    if (i < candidates.size() - 1) {
                        std::cout << ",";
                    }
                }
                std::cout << "]";
            }
        }
        std::cout << std::endl;
        #endif
        
        return Status::OK;
    }

    Status GetKnnCandidate(ServerContext* context,
                            const CandSql* request,
                            KnnCand* response) override {
        m_logger.SetStartTimer();
        double grpc_comm = request->ByteSizeLong();

        const size_t cand_num = m_local_knn_list.size();
        size_t retrieve_num = request->k();

        if (m_local_knn_head+retrieve_num > cand_num) {
            throw std::invalid_argument("vector data silo has insufficient candidates for knn");
            std::exit(EXIT_FAILURE);
        }

        for (size_t i = 0; i < retrieve_num; ++i) {
            const VectorDataDistType& local_knn = m_local_knn_list[m_local_knn_head++];
            VectorData* vector_data = response->add_knn();
            vector_data->set_vid(local_knn.vid);
            for (auto d : local_knn.data) {
                vector_data->add_data(d);
            }
            response->add_dist(local_knn.dist);
        }

        grpc_comm += response->ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);
        m_logger.SetEndTimer();
        m_logger.LogAddTime();

        return Status::OK;
    }

    Status GetKnnJoinCandidate(ServerContext* context,
                                const KnnJoinCandReq* request,
                                KnnJoinCand* response) override {
        m_logger.SetStartTimer();
        double grpc_comm = request->ByteSizeLong();

        size_t query_num = request->qid_size();
        
        for (size_t i = 0; i < query_num; ++i) {
            const int qid = request->qid(i);
            const size_t retrieve_num = request->k(i);

            if (qid >= m_local_knnjoin_list.size()) {
                return Status(grpc::StatusCode::OUT_OF_RANGE, 
                            "KNN Join #" + std::to_string(qid) + " out of range. Max: " + 
                            std::to_string(m_local_knnjoin_list.size() - 1));
            }
            
            const size_t cand_num = m_local_knnjoin_list[qid].size();
            
            if (m_local_knnjoin_head[qid]+retrieve_num > cand_num) {
                return Status(grpc::StatusCode::RESOURCE_EXHAUSTED, 
                            "Insufficient candidates in KNN Join #" + std::to_string(qid) + ". Remain: " + std::to_string(cand_num-m_local_knnjoin_head[qid]) + 
                            ", Need: " + std::to_string(retrieve_num));
            }
        }

        for (size_t i = 0; i < query_num; ++i) {
            const int qid = request->qid(i);
            const size_t retrieve_num = request->k(i);
            
            KnnCand* knn_cand = response->add_result();
            knn_cand->set_qid(qid);
            
            auto& candidate_list = m_local_knnjoin_list[qid];
            size_t& head = m_local_knnjoin_head[qid];
            
            for (size_t j = 0; j < retrieve_num; ++j) {
                const VectorDataDistType& local_knn = candidate_list[head++];
                
                VectorData* vector_data = knn_cand->add_knn();
                vector_data->set_vid(local_knn.vid);
                for (auto d : local_knn.data) {
                    vector_data->add_data(d);
                }

                knn_cand->add_dist(local_knn.dist);
            }
        }

        grpc_comm += response->ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);
        m_logger.SetEndTimer();
        m_logger.LogAddTime();

        return Status::OK;
    }

    Status ClearKnnCandidate(ServerContext* context,
                        const Empty* request,
                        Empty* empty_response) override {

        m_logger.SetStartTimer();

        m_local_knn_list.clear();
        m_local_knn_head = 0;

        double grpc_comm = request->ByteSizeLong() + empty_response->ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);

        m_logger.SetEndTimer();
        m_logger.LogOneQuery();

        return Status::OK;
    }

    Status ClearKnnJoinCandidate(ServerContext* context,
                                const Empty* request,
                                Empty* empty_response) override {

        m_logger.SetStartTimer();

        m_local_knnjoin_list.clear();
        m_local_knnjoin_head.clear();

        double grpc_comm = request->ByteSizeLong() + empty_response->ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);

        m_logger.SetEndTimer();
        m_logger.LogOneQuery();

        return Status::OK;
    }

    Status GetMpcComm(ServerContext* context,
                        const Empty* request,
                        MpcComm* response) override {

        response->set_comm(m_mpc_comm);

        return Status::OK;
    }

    Status StartMinMpc(ServerContext* context,
                        const MinMpcBobList* request,
                        Empty* response) override {

        #ifdef LOCAL_DEBUG
        std::cout << "------------------------------------------------" << std::endl;
        #endif
        
        const int bob_silo_id = request->bob();

        // 1. Every 100 queries, request a random value from the 3rd party
        if (bob_silo_id<0 && (1+bob_silo_id)%100==0) {
            ClientContext context;
            RequestRandom request;
            RandomValue response;

            request.set_alice(m_silo_id);
            request.set_m(m_silo_num);
            
            Status status = m_broker_stub->RequestRandomValue(&context, request, &response); 
            if (!status.ok()) {
                std::cerr << "RPC failed: " << status.error_message() << std::endl;
                std::string error_message;
                error_message = std::string("Alice at silo #(") + std::to_string(m_silo_id) + std::string(") cannot request random values from the untrusted 3rd party");
                throw std::invalid_argument(error_message);  
                std::exit(EXIT_FAILURE);
            }

            for (int i=0, idx=0; i<m_silo_num; ++i) {
                if (i == m_silo_id) continue;
                m_alice_Ra_list[i] = response.r(idx);
                ++idx;
                #ifdef LOCAL_DEBUG
                std::cout << "Alice at silo " << m_silo_id << " receives Ra = " << m_alice_Ra_list[i] << " from the broker" << std::endl;
                #endif
            }

            m_mpc_comm += (request.ByteSizeLong() + response.ByteSizeLong());
        }

        // 2. when bob_silo_id==-1 or -2, Generate a random and secret non-zero value
        if (bob_silo_id < 0) {
            for (int i=0; i<m_silo_num; ++i) {
                if (i == m_silo_id) continue;
                // the random value will be re-genearted when calculating next knn
                m_alice_R_list[i] = m_GenRandomValue();
            }
        }
        #ifdef LOCAL_DEBUG
        std::cout << "Alice at silo " << m_silo_id << " generates random non-zero R = [";
        for (int i=0; i<m_silo_num; ++i) {
            if (i == 0)
                std::cout << m_alice_R_list[i];
            else
                std::cout << ", " << m_alice_R_list[i];
        }
        std::cout << "]" << std::endl;
        #endif

        // 3. Send the perturbed value to Bob
        if (bob_silo_id < 0) {
            std::vector<double> mpc_comm_list(m_silo_num, 0.0);
            #ifdef LOCAL_DEBUG
            boost::asio::thread_pool pool(1);
            #else
            boost::asio::thread_pool pool(m_thread_num);
            #endif

            for (int other_silo_id=0; other_silo_id<m_silo_num; ++other_silo_id) {
                if (other_silo_id == m_silo_id) continue;

                boost::asio::post(pool, std::bind(m_RequestMinMpcBobThread, this, other_silo_id, std::ref(mpc_comm_list[other_silo_id]))); 
            }

            // for (int i=0; i<m_silo_num; ++i) {
            //     if (i == m_silo_id) continue;
                
            //     ClientContext context;
            //     MinMpcAlicePair request;
            //     Empty response;
            //     double value = m_local_knnd_list[m_local_knn_head] * m_alice_R_list[i] + m_alice_Ra_list[i];

            //     request.set_alice(m_silo_id);
            //     request.set_r(m_alice_R_list[i]);
            //     request.set_value(value);
                
            //     Status status = m_silo_stub_list[i]->RequestMinMpcBob(&context, request, &response); 
            //     if (!status.ok()) {
            //         std::cerr << "RPC failed: " << status.error_message() << std::endl;
            //         std::string error_message;
            //         error_message = std::string("Alice at silo #(") + std::to_string(m_silo_id) + std::string(") cannot send his perturbed value to Bob at silo #(") + std::to_string(i) + std::string(")");
            //         throw std::invalid_argument(error_message);  
            //         std::exit(EXIT_FAILURE);
            //     }

            //     m_mpc_comm += 0.5 * (request.ByteSizeLong() + response.ByteSizeLong());
            // }

            pool.join(); 

            for (int silo_id=0; silo_id<m_silo_num; ++silo_id) {
                m_mpc_comm += mpc_comm_list[silo_id];
            }

        } else {
            double tmp_mpc_comm;

            m_RequestMinMpcBob(bob_silo_id, tmp_mpc_comm);

            m_mpc_comm += tmp_mpc_comm;
        }        

        double grpc_comm = request->ByteSizeLong() + response->ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);

        return Status::OK;
    }

    Status JoinStartMinMpc(ServerContext* context,
                            const JoinMinMpcBobList* request,
                            Empty* response) override {

        const int bob_silo_id = request->bob();
        const int query_num = request->qid_size();
        std::vector<QidType> qids(query_num);
        for (int i = 0; i < query_num; ++i) {
            qids[i] = request->qid(i);
        }

        // 1. when bob_silo_id==-1 and every 100 queies, request a random value from the 3rd party
        if (bob_silo_id<0 && (1+bob_silo_id)%100==0) {
            ClientContext context;
            RequestRandom request;
            RandomValue response;

            request.set_alice(m_silo_id);
            request.set_m(m_silo_num);
            
            Status status = m_broker_stub->RequestRandomValue(&context, request, &response); 
            if (!status.ok()) {
                std::cerr << "RPC failed: " << status.error_message() << std::endl;
                std::string error_message;
                error_message = std::string("Alice at silo #(") + std::to_string(m_silo_id) + std::string(") cannot request random values from the untrusted 3rd party");
                throw std::invalid_argument(error_message);  
            }

            for (int i = 0, idx = 0; i < m_silo_num; ++i) {
                if (i == m_silo_id) continue;
                m_alice_Ra_list[i] = response.r(idx);
                ++idx;
            }

            m_mpc_comm += (request.ByteSizeLong() + response.ByteSizeLong());
        }

        // 2. Generate a random and secret non-zero value
        if (bob_silo_id < 0) {
            for (int i=0; i<m_silo_num; ++i) {
                if (i == m_silo_id) continue;
                m_alice_R_list[i] = m_GenRandomValue();
            }
        }

        if (bob_silo_id < 0) {
            std::vector<double> mpc_comm_list(m_silo_num, 0.0);
            #ifdef LOCAL_DEBUG
            boost::asio::thread_pool pool(1);
            #else
            boost::asio::thread_pool pool(m_thread_num);
            #endif

            for (int other_silo_id = 0; other_silo_id < m_silo_num; ++other_silo_id) {
                if (other_silo_id == m_silo_id) continue;
                boost::asio::post(pool, std::bind(m_JoinRequestMinMpcBobThread, this, other_silo_id, std::ref(mpc_comm_list[other_silo_id]), qids)); 
            }

            pool.join(); 

            for (int silo_id = 0; silo_id < m_silo_num; ++silo_id) {
                m_mpc_comm += mpc_comm_list[silo_id];
            }

        } else {
            double tmp_mpc_comm = 0.0;
            m_JoinRequestMinMpcBob(bob_silo_id, tmp_mpc_comm, qids);
            m_mpc_comm += tmp_mpc_comm;
        }        

        double grpc_comm = request->ByteSizeLong() + response->ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);

        return Status::OK;
    }

    Status CollectMinMpc(ServerContext* context,
                            const MinMpcBobList* request,
                            MinMpcSecretShares* response) override {
        
        const int bob_silo_id = request->bob();
        
        // 1. Alice will first collect his secret share from the 3rd party
        if (bob_silo_id < 0) {// meaning secret shares of all silos need to be collected from the 3rd party
            ClientContext context;
            RequestSecretShareList request;
            SecretShareList response;

            request.set_alice(m_silo_id);
            request.set_m(m_silo_num);
            
            Status status = m_broker_stub->RequestSecretShareListAlice(&context, request, &response); 
            if (!status.ok()) {
                std::cerr << "RPC failed: " << status.error_message() << std::endl;
                std::string error_message;
                error_message = std::string("Bob at silo #(") + std::to_string(m_silo_id) + std::string(") cannot his secret share from the untrusted 3rd party");
                throw std::invalid_argument(error_message);  
                std::exit(EXIT_FAILURE);
            }

            for (int i=0, idx=0; i<m_silo_num; ++i) {
                if (i == m_silo_id) continue;
                int r = m_alice_R_list[i];
                if (r > 0) {
                    m_alice_secret_share_list[i] = response.r_zero(idx);
                } else if (r < 0) {
                    m_alice_secret_share_list[i] = response.r_one(idx);
                } else {
                    std::string error_message;
                    error_message = std::string("Alice at silo #(") + std::to_string(m_silo_id) + std::string(") generates a non-zero random value");
                    throw std::invalid_argument(error_message);  
                    std::exit(EXIT_FAILURE);
                }  
                ++idx;
            }

            m_mpc_comm += (request.ByteSizeLong() + response.ByteSizeLong());
            
            #ifdef LOCAL_DEBUG
            std::cout << "Alice at silo " << m_silo_id << " collects secret shares from the 3rd party: [";
            for (int i=0; i<m_silo_num; ++i) {
                if (i > 0)
                    std::cout << ", ";
                std::cout << m_alice_secret_share_list[i];
            }
            std::cout << "]" << std::endl;
            #endif

        } else {

            ClientContext context;
            RequestSecretShare request;
            SecretShare response;

            request.set_alice(m_silo_id);
            request.set_bob(bob_silo_id);
            
            Status status = m_broker_stub->RequestSecretShareAlice(&context, request, &response); 
            if (!status.ok()) {
                std::cerr << "RPC failed: " << status.error_message() << std::endl;
                std::string error_message;
                error_message = std::string("Bob at silo #(") + std::to_string(m_silo_id) + std::string(") cannot his secret share from the untrusted 3rd party");
                throw std::invalid_argument(error_message);  
                std::exit(EXIT_FAILURE);
            }

            int r = m_alice_R_list[bob_silo_id];
            if (r > 0) {
                m_alice_secret_share_list[bob_silo_id] = response.r_zero();
            } else if (r < 0) {
                m_alice_secret_share_list[bob_silo_id] = response.r_one();
            } else {
                std::string error_message;
                error_message = std::string("Alice at silo #(") + std::to_string(m_silo_id) + std::string(") generates a non-zero random value");
                throw std::invalid_argument(error_message);  
                std::exit(EXIT_FAILURE);
            }

            m_mpc_comm += (request.ByteSizeLong() + response.ByteSizeLong());
            
                        
            #ifdef LOCAL_DEBUG
            std::cout << "Alice at silo " << m_silo_id << " collects secret shares from the 3rd party: [";
            std::cout << m_alice_secret_share_list[bob_silo_id];
            std::cout << "]" << " for Bob at silo " << bob_silo_id << std::endl;
            #endif
        }

        // 2. Alice then needs to prepare the response
        if (bob_silo_id < 0) {// meaning all secret shares that need to be returned
            for (int i=0; i<m_silo_num; ++i) {
                if (i == m_silo_id) {
                    int sum_alice_share = 0;
                    for (auto r : m_alice_secret_share_list)
                        sum_alice_share += r;
                    response->add_r(sum_alice_share);
                } else {
                    response->add_r(m_bob_secret_share_list[i]);
                }
            }

            #ifdef LOCAL_DEBUG
            std::cout << "Alice at silo " << m_silo_id << " sends secret shares back to server: [";
            int r_sz = response->r_size();
            for (int idx=0; idx<r_sz; ++idx) {
                if (idx > 0)
                    std::cout << ", ";
                std::cout << response->r(idx);
            }
            std::cout << "]" << std::endl;
            #endif
        } else {// meaning only bob's secret share that needs to be returned
            assert(m_silo_id != bob_silo_id);

            // when current silo acts as alice, then all his alice secret share will be changed
            int sum_alice_share = 0;
            for (auto r : m_alice_secret_share_list)
                sum_alice_share += r;
            response->add_r(sum_alice_share);

            // when current silo acts as alice and bob_silo_id acts as alice, then only his bob secret share w.r.t. alice_silo_id will be changed
            int alice_silo_id = bob_silo_id;
            response->add_r(m_bob_secret_share_list[alice_silo_id]);

            #ifdef LOCAL_DEBUG
            std::cout << "Alice at silo " << m_silo_id << " sends secret shares back to server: [";
            int r_sz = response->r_size();
            for (int idx=0; idx<r_sz; ++idx) {
                if (idx > 0)
                    std::cout << ", ";
                std::cout << response->r(idx);
            }
            std::cout << "]" << std::endl;
            #endif
        }

        double grpc_comm = request->ByteSizeLong() + response->ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);

        return Status::OK;
    }

    Status JoinCollectMinMpc(ServerContext* context,
                                const JoinMinMpcBobList* request,
                                JoinMinMpcSecretShares* response) override {
        
        const int bob_silo_id = request->bob();
        const int query_num = request->qid_size();
        std::vector<QidType> qids(query_num);
        for (int i = 0; i < query_num; ++i) {
            qids[i] = request->qid(i);
        }

        // std::cout << "alice: " << m_silo_id << ", bob: " << bob_silo_id << std::endl;
        // std::cout << "qids:";
        // for (auto qid : qids) {
        //     std::cout << " " << qid;
        // }
        // std::cout << std::endl;

        // 1. Alice will first collect his secret share from the 3rd party
        if (bob_silo_id < 0) {// meaning secret shares of all silos need to be collected from the 3rd party
            ClientContext context;
            JoinRequestSecretShareList request_ssl;
            JoinSecretShareList response_ssl;

            RequestSecretShareList ssl;
            ssl.set_alice(m_silo_id);
            ssl.set_m(m_silo_num);
            for (int i = 0; i < query_num; ++i) {
                request_ssl.add_qid(qids[i]);
                RequestSecretShareList* new_ssl = request_ssl.add_request_ssl();
                new_ssl->CopyFrom(ssl);
            }
            
            Status status = m_broker_stub->JoinRequestSecretShareListAlice(&context, request_ssl, &response_ssl); 
            if (!status.ok()) {
                std::cerr << "RPC failed: " << status.error_message() << std::endl;
                std::string error_message;
                error_message = std::string("Bob at silo #(") + std::to_string(m_silo_id) + std::string(") cannot his secret share from the untrusted 3rd party");
                throw std::invalid_argument(error_message);  
                std::exit(EXIT_FAILURE);
            }

            for (int q = 0; q < query_num; ++q) {
                const SecretShareList& ssl = response_ssl.secret_share_list(q);
                const int qid = qids[q];
                assert(qid == response_ssl.qid(q));

                for (int i = 0, idx = 0; i < m_silo_num; ++i) {
                    if (i == m_silo_id) continue;

                    double r = m_alice_R_list[i];
                    if (r > 0) {
                        m_join_alice_secret_share_list[qid][i] = ssl.r_zero(idx);
                    } else if (r < 0) {
                        m_join_alice_secret_share_list[qid][i] = ssl.r_one(idx);
                    } else {
                        std::string error_message;
                        error_message = std::string("Alice at silo #(") + std::to_string(m_silo_id) + std::string(") generates a non-zero random value");
                        throw std::invalid_argument(error_message);  
                    }  
                    ++idx;
                }
            }

            m_mpc_comm += (request_ssl.ByteSizeLong() + response_ssl.ByteSizeLong());

        } else {

            ClientContext context;
            JoinRequestSecretShare request_ss;
            JoinSecretShare response_ss;

            RequestSecretShare rss;
            rss.set_alice(m_silo_id);
            rss.set_bob(bob_silo_id);
            for (int i = 0; i < query_num; ++i) {
                request_ss.add_qid(qids[i]);
                RequestSecretShare* new_rss = request_ss.add_request_ss();
                new_rss->CopyFrom(rss);
            }
            
            Status status = m_broker_stub->JoinRequestSecretShareAlice(&context, request_ss, &response_ss); 
            if (!status.ok()) {
                std::cerr << "RPC failed: " << status.error_message() << std::endl;
                std::string error_message;
                error_message = std::string("Bob at silo #(") + std::to_string(m_silo_id) + std::string(") cannot his secret share from the untrusted 3rd party");
                throw std::invalid_argument(error_message);  

            }

            double r = m_alice_R_list[bob_silo_id];

            for (int q = 0; q < query_num; ++q) {
                const int qid = qids[q];
                const SecretShare& ss = response_ss.secret_share(q);
                assert(qid == response_ss.qid(q));

                if (r > 0) {
                    m_join_alice_secret_share_list[qid][bob_silo_id] = ss.r_zero();
                } else if (r < 0) {
                    m_join_alice_secret_share_list[qid][bob_silo_id] = ss.r_one();
                } else {
                    std::string error_message;
                    error_message = std::string("Alice at silo #(") + std::to_string(m_silo_id) + std::string(") generates a non-zero random value");
                    throw std::invalid_argument(error_message);  
                }
            }

            m_mpc_comm += (request_ss.ByteSizeLong() + response_ss.ByteSizeLong());
        }

        // 2. Alice then needs to prepare the response
        if (bob_silo_id < 0) {// meaning all secret shares that need to be returned
            for (int q = 0; q < query_num; ++q) {
                int qid = qids[q];
                response->add_qid(qid);
                auto* min_mss = response->add_min_mss();
                for (int i = 0; i < m_silo_num; ++i) {
                    if (i == m_silo_id) {
                        int sum_alice_share = 0;
                        for (auto r : m_join_alice_secret_share_list[qid])
                            sum_alice_share += r;
                        min_mss->add_r(sum_alice_share);
                    } else {
                        min_mss->add_r(m_join_bob_secret_share_list[qid][i]);
                    }
                }
            }
        } else {// meaning only bob's secret share that needs to be returned

            for (int q = 0; q < query_num; ++q) {
                int qid = qids[q];
                response->add_qid(qid);
                auto* min_mss = response->add_min_mss();

                // when current silo acts as alice, then all his alice secret share will be changed
                int sum_alice_share = 0;
                for (auto r : m_join_alice_secret_share_list[qid])
                    sum_alice_share += r;
                min_mss->add_r(sum_alice_share);

                // when current silo acts as alice and bob_silo_id acts as alice, then only his bob secret share w.r.t. alice_silo_id will be changed
                int alice_silo_id = bob_silo_id;
                min_mss->add_r(m_join_bob_secret_share_list[qid][alice_silo_id]);
            }
        }

        double grpc_comm = request->ByteSizeLong() + response->ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);

        return Status::OK;
    }

    Status RequestMinMpcBob(ServerContext* context,
                            const MinMpcAlicePair* request,
                            Empty* response) override {

        int r = request->r();
        double value = request->value();
        int alice_silo_id = request->alice();

        #ifdef LOCAL_DEBUG
        std::cout << "Bob at silo " << m_silo_id << " receives from Alice at silo " << alice_silo_id << " with " << value;
        #endif

        value -= r * m_local_knn_list[m_local_knn_head].dist;

        #ifdef LOCAL_DEBUG
        std::cout << ", and changes it into " << value << ", r = " << r << std::endl;
        #endif

        // Bob will send value to the untrust third party
        {
            ClientContext context;
            PerturbResult perturb_result;
            SecretShare secret_share;

            perturb_result.set_alice(alice_silo_id);
            perturb_result.set_bob(m_silo_id);
            perturb_result.set_value(value);
            
            Status status = m_broker_stub->RequestSecretShareBob(&context, perturb_result, &secret_share); 
            if (!status.ok()) {
                std::cerr << "RPC failed: " << status.error_message() << std::endl;
                std::string error_message;
                error_message = std::string("Bob at silo #(") + std::to_string(m_silo_id) + std::string(") cannot his secret share from the untrusted 3rd party");
                throw std::invalid_argument(error_message);  
                std::exit(EXIT_FAILURE);
            }

            if (r > 0) {
                m_bob_secret_share_list[alice_silo_id] = secret_share.r_zero();
            } else if (r < 0) {
                m_bob_secret_share_list[alice_silo_id] = secret_share.r_one();
            } else {
                std::string error_message;
                error_message = std::string("Alice at silo #(") + std::to_string(m_silo_id) + std::string(") generates a non-zero random value");
                throw std::invalid_argument(error_message);  
                std::exit(EXIT_FAILURE);
            }

            m_mpc_comm += (perturb_result.ByteSizeLong() + secret_share.ByteSizeLong());

            #ifdef LOCAL_DEBUG
            std::cout << "Bob at silo " << m_silo_id << " keeps " << m_bob_secret_share_list[alice_silo_id] << " as his secret share with Alice at silo " << alice_silo_id << std::endl;
            #endif
        }

        double grpc_comm = request->ByteSizeLong() + response->ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);

        return Status::OK;
    }

    Status JoinRequestMinMpcBob(ServerContext* context,
                                const JoinMinMpcAlicePair* request,
                                Empty* response) override {
        const int query_num = request->qid_size();
        std::vector<QidType> qids(query_num);
        for (int i = 0; i < query_num; ++i) {
            qids[i] = request->qid(i);
        }

        ClientContext client_context;
        JoinPerturbResult join_perturb_result;
        JoinSecretShare join_secret_share;

        for (int q = 0; q < query_num; ++q) {
            int qid = qids[q];
            int r = request->r(q);
            double value = request->value(q);
            int alice_silo_id = request->alice(q);

            value -= r * m_local_knnjoin_list[qid][m_local_knnjoin_head[qid]].dist;

            join_perturb_result.add_qid(qid);
            auto* perturb_result = join_perturb_result.add_perturb_result();

            // Bob will send value to the untrust third party
            perturb_result->set_alice(alice_silo_id);
            perturb_result->set_bob(m_silo_id);
            perturb_result->set_value(value);
        }

        Status status = m_broker_stub->JoinRequestSecretShareBob(&client_context, join_perturb_result, &join_secret_share); 
        if (!status.ok()) {
            std::cerr << "RPC failed: " << status.error_message() << std::endl;
            std::string error_message;
            error_message = std::string("Bob at silo #(") + std::to_string(m_silo_id) + std::string(") cannot his secret share from the untrusted 3rd party");
            throw std::invalid_argument(error_message);  
        }
        m_mpc_comm += (join_perturb_result.ByteSizeLong() + join_secret_share.ByteSizeLong());
        
        for (int q = 0; q < query_num; ++q) {
            int qid = qids[q];
            int r = request->r(q);
            int alice_silo_id = request->alice(q);
            auto& secret_share = join_secret_share.secret_share(q);

            if (r > 0) {
                m_join_bob_secret_share_list[qid][alice_silo_id] = secret_share.r_zero();
            } else if (r < 0) {
                m_join_bob_secret_share_list[qid][alice_silo_id] = secret_share.r_one();
            } else {
                std::string error_message;
                error_message = std::string("Alice at silo #(") + std::to_string(m_silo_id) + std::string(") generates a non-zero random value");
                throw std::invalid_argument(error_message);  
            }
        }

        double grpc_comm = request->ByteSizeLong() + response->ByteSizeLong();
        m_logger.LogAddComm(grpc_comm);

        return Status::OK;
    }

    std::string to_string() const {
        std::stringstream ss;

        ss << "-------------- Data Silo #(" << m_silo_ptr->GetSiloId() << ") Log --------------\n";
        ss << m_logger.to_string();

        return ss.str();
    }

    double GetIndexQueryTime() const {
        return (m_index_query_num==0) ? 0.0 : (m_index_query_time/m_index_query_num);
    }

    size_t GetIndexQueryNum() const {
        return m_index_query_num;
    }

private:
    void m_RequestMinMpcBob(int bob_silo_id, double& mpc_comm) {
        ClientContext context;
        MinMpcAlicePair request;
        Empty response;
        double value = m_local_knn_list[m_local_knn_head].dist * m_alice_R_list[bob_silo_id] + m_alice_Ra_list[bob_silo_id];

        request.set_alice(m_silo_id);
        request.set_r(m_alice_R_list[bob_silo_id]);
        request.set_value(value);

        #ifdef LOCAL_DEBUG
        std::cout << "Alice at silo " << m_silo_id << " sends ("
                    << m_alice_R_list[bob_silo_id] << ", " << value 
                    << ") to Bob at silo " << bob_silo_id << std::endl;
        #endif
        
        Status status = m_silo_stub_list[bob_silo_id]->RequestMinMpcBob(&context, request, &response); 
        if (!status.ok()) {
            std::cerr << "RPC failed: " << status.error_message() << std::endl;
            std::string error_message;
            error_message = std::string("Alice at silo #(") + std::to_string(m_silo_id) + std::string(") cannot send his perturbed value to Bob at silo #(") + std::to_string(bob_silo_id) + std::string(")");
            throw std::invalid_argument(error_message);  
        }

        mpc_comm = 0.5 * (request.ByteSizeLong() + response.ByteSizeLong());
    }

    void m_JoinRequestMinMpcBob(int bob_silo_id, double& mpc_comm, const std::vector<QidType>& qids) {
        ClientContext context;
        JoinMinMpcAlicePair request;
        Empty response;

        for (auto qid : qids) {
            double value = m_local_knnjoin_list[qid][m_local_knnjoin_head[qid]].dist * m_alice_R_list[bob_silo_id] + m_alice_Ra_list[bob_silo_id];

            request.add_qid(qid);
            request.add_alice(m_silo_id);
            request.add_r(m_alice_R_list[bob_silo_id]);
            request.add_value(value);
        }
        
        Status status = m_silo_stub_list[bob_silo_id]->JoinRequestMinMpcBob(&context, request, &response); 
        if (!status.ok()) {
            std::cerr << "RPC failed: " << status.error_message() << std::endl;
            std::string error_message;
            error_message = std::string("Alice at silo #(") + std::to_string(m_silo_id) + std::string(") cannot send his perturbed value to Bob at silo #(") + std::to_string(bob_silo_id) + std::string(")");
            throw std::invalid_argument(error_message);
        }

        mpc_comm = 0.5 * (request.ByteSizeLong() + response.ByteSizeLong());
    }

    static void m_RequestMinMpcBobThread(FedVectorDBImpl* fed_vdb, int bob_silo_id, double& mpc_comm) {
        fed_vdb->m_RequestMinMpcBob(bob_silo_id, mpc_comm);
    }

    static void m_JoinRequestMinMpcBobThread(FedVectorDBImpl* fed_vdb, int bob_silo_id, double& mpc_comm, const std::vector<QidType>& qids) {
        fed_vdb->m_JoinRequestMinMpcBob(bob_silo_id, mpc_comm, qids);
    }

    void m_InitClientStub(const std::string& silo_ip_filename) {
        ReadSiloIPaddr(silo_ip_filename, m_silo_ipaddr_list);
        if (m_silo_ipaddr_list.empty()) {
            throw std::invalid_argument("No data silo's IP addresses");  
            std::exit(EXIT_FAILURE);
        }
        m_silo_num = m_silo_ipaddr_list.size();
        #ifdef LOCAL_DEBUG
        std::cout << '\t' << "[FINISH] Read data silo's ip addresses" << std::endl;
        std::cout << '\t' << "m_silo_num = " << m_silo_num << std::endl;
        #endif

        grpc::ChannelArguments args;  
        args.SetInt(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH, INT_MAX);  
        args.SetInt(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, INT_MAX); 

        // create stub for each data slo
        m_silo_stub_list.resize(m_silo_num);
        for (int i=0; i<m_silo_num; ++i) {
            if (i != m_silo_id) {
                #ifdef LOCAL_DEBUG
                std::cout << '\t' << "[FINISH] Create client stub for data silo #(" << i << ") at " << m_silo_ipaddr_list[i] << std::endl;
                #endif
                std::string m_other_silo_ipaddr = m_silo_ipaddr_list[i];
                std::shared_ptr<grpc::Channel> channel = grpc::CreateCustomChannel(m_other_silo_ipaddr, grpc::InsecureChannelCredentials(), args);
                m_silo_stub_list[i] = FedSqlService::NewStub(channel);
            }
        }

        {// create stub for the third party
            #ifdef LOCAL_DEBUG
            std::cout << '\t' << "[FINISH] Create client stub for the broker at " << m_broker_ipaddr << std::endl;
            #endif
            std::shared_ptr<grpc::Channel> channel = grpc::CreateCustomChannel(m_broker_ipaddr, grpc::InsecureChannelCredentials(), args);
            m_broker_stub = BrokerService::NewStub(channel);
        }

        m_mpc_comm = 0;
        m_bob_secret_share_list.resize(m_silo_num);
        std::fill(m_bob_secret_share_list.begin(), m_bob_secret_share_list.end(), 0);
        m_alice_secret_share_list.resize(m_silo_num);
        std::fill(m_alice_secret_share_list.begin(), m_alice_secret_share_list.end(), 0);
        m_alice_R_list.resize(m_silo_num);
        m_alice_Ra_list.resize(m_silo_num);

        m_join_bob_secret_share_list.assign(MAX_QUERY_NUM, m_bob_secret_share_list);
        m_join_alice_secret_share_list.assign(MAX_QUERY_NUM, m_alice_secret_share_list);

        m_index_query_time = 0;
        m_index_query_num = 0;
    }

    int m_GenRandomValue() {
        // #ifdef LOCAL_DEBUG
        // return 13;
        // #endif

        int v = 0;
        while (v == 0) {
            v = m_uniform_dis(m_gen);
        }
        return v;
    }

    std::random_device m_rd;  
    std::mt19937 m_gen;  
    std::uniform_int_distribution<> m_uniform_dis;  
    std::vector<int> m_alice_Ra_list;
    std::vector<int> m_alice_R_list;
    std::vector<int> m_alice_secret_share_list;
    std::vector<int> m_bob_secret_share_list;
    std::vector<std::string> m_silo_ipaddr_list;
    std::string m_broker_ipaddr;
    int m_silo_num;
    int m_silo_id;
    std::unique_ptr<BrokerService::Stub> m_broker_stub;
    std::vector<std::unique_ptr<FedSqlService::Stub>> m_silo_stub_list;
    std::unique_ptr<BaseSiloConnector<Distance>> m_silo_ptr;
    std::vector<VectorDataDistType> m_local_knn_list; // the most similar one appears at the head, the least similar one appears at the tail
    size_t m_local_knn_head;                             // the head pointer of the vector m_local_knn_list
    
    std::vector<std::vector<int>> m_join_alice_secret_share_list;
    std::vector<std::vector<int>> m_join_bob_secret_share_list;
    std::vector<std::vector<VectorDataDistType>> m_local_knnjoin_list; // a list of knn result
    std::vector<size_t> m_local_knnjoin_head;                      // a list of the head pointer of knn result
    double m_mpc_comm;
    double m_index_query_time;
    size_t m_index_query_num;
    BenchLogger m_logger;
    const int m_thread_num;
};

std::unique_ptr<FedVectorDBImpl> fed_vectordb_ptr = nullptr;

void RunSilo(const int silo_id, const std::string& silo_ipaddr, const std::string& table_filename, 
                const std::string& third_party_ipaddr, const std::string& silo_ip_filename, const std::string& index_type) {
    fed_vectordb_ptr = std::make_unique<FedVectorDBImpl>(silo_id, silo_ipaddr, table_filename, third_party_ipaddr, silo_ip_filename, index_type);

    ServerBuilder builder;
    builder.AddListeningPort(silo_ipaddr, grpc::InsecureServerCredentials());
    builder.RegisterService(fed_vectordb_ptr.get());
    builder.SetMaxSendMessageSize(INT_MAX);
    builder.SetMaxReceiveMessageSize(INT_MAX);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Data Silo #(" << silo_id << ") listening on " << silo_ipaddr << std::endl;
    
    server->Wait();

    std::string log_info = fed_vectordb_ptr->to_string();
    std::cout << log_info;
    double index_query_time = fed_vectordb_ptr->GetIndexQueryTime();
    std::cout << std::fixed << std::setprecision(6) << "Index knn query time = " << index_query_time << " [ms]" << std::endl;
    std::cout.flush();
}

// Ensure the log file is output, when the program is terminated.
void SignalHandler(int signal) {
    if (fed_vectordb_ptr != nullptr) {
        std::string log_info = fed_vectordb_ptr->to_string();
        std::cout << log_info;
        double index_query_time = fed_vectordb_ptr->GetIndexQueryTime();
        std::cout << std::fixed << std::setprecision(6) << "Index knn query time = " << index_query_time << " [ms]" << std::endl;
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
    omp_set_num_threads(4);
    
    // Expect the following args: --ip=0.0.0.0 --port=50051 --data-path=../../data/data_01.txt --silo_id=1
    int silo_port, silo_id;
    std::string silo_ip, silo_ipaddr, data_filename, index_type;
    std::string third_party_ipaddr, silo_ip_filename;
    
    try { 
        bpo::options_description option_description("Required options");
        option_description.add_options()
            ("help", "produce help message")
            ("id", bpo::value<int>(&silo_id)->default_value(0), "Data silo's ID")
            ("ip", bpo::value<std::string>(), "This data silo's IP address")
            ("port", bpo::value<int>(&silo_port), "Data silo's IP port")
            ("data-path", bpo::value<std::string>(), "Data file path")
            ("index-type", bpo::value<std::string>(), "Type of index type: [Linear | HNSW | LSH | PQ | IVF | IVFPQ]")
            ("silo-ip", bpo::value<std::string>(), "All data silo's IP addresses")
            ("3rd-ipaddr", bpo::value<std::string>(), "The untrust broker's IP address")
        ;

        bpo::variables_map variable_map;
        bpo::store(bpo::parse_command_line(argc, argv, option_description), variable_map);
        bpo::notify(variable_map);    

        if (variable_map.count("help")) {
            std::cout << option_description << std::endl;
            return 0;
        }

        bool options_all_set = true;

        std::cout << "Data silo's ID is " << silo_id << "\n";

        if (variable_map.count("ip")) {
            silo_ip = variable_map["ip"].as<std::string>();
            std::cout << "This data silo's IP address was set to " << silo_ip << "\n";
        } else {
            std::cout << "This data silo's IP address was not set" << "\n";
            options_all_set = false;
        }

        if (variable_map.count("port")) {
            silo_port = variable_map["port"].as<int>();
            std::cout << "Data silo's IP port was set to " << silo_port << "\n";
        } else {
            std::cout << "Data silo's IP port was not set" << "\n";
            options_all_set = false;
        }

        if (variable_map.count("data-path")) {
            data_filename = variable_map["data-path"].as<std::string>();
            std::cout << "Data silo's data file path was set to " << data_filename << "\n";
        } else {
            std::cout << "Data silo's data file path was not set" << "\n";
            options_all_set = false;
        }

        if (variable_map.count("index-type")) {
            index_type = variable_map["index-type"].as<std::string>();
            std::cout << "Data silo's index type was set to " << index_type << "\n";
        } else {
            std::cout << "Data silo's index type was not set" << "\n";
            options_all_set = false;
        }

        if (variable_map.count("silo-ip")) {
            silo_ip_filename = variable_map["silo-ip"].as<std::string>();
            std::cout << "All data silo's IP configuration file path was set to " << silo_ip_filename << "\n";
        } else {
            std::cout << "All data silo's IP configuration file path was not set" << "\n";
            options_all_set = false;
        }

        if (variable_map.count("3rd-ipaddr")) {
            third_party_ipaddr = variable_map["3rd-ipaddr"].as<std::string>();
            std::cout << "The untrust broker's IP address was set to " << third_party_ipaddr << "\n";
        } else {
            std::cout << "The untrust broker's IP address was not set" << "\n";
            options_all_set = false;
        }

        if (false == options_all_set) {
            throw std::invalid_argument("Some options were not properly set");
            std::cout.flush();
            std::exit(EXIT_FAILURE);
        }

        silo_ipaddr = silo_ip + std::string(":") + std::to_string(silo_port);
    } catch (std::exception& e) {  
        std::cerr << "Error: " << e.what() << "\n";  
        std::exit(EXIT_FAILURE);
    }

    ResetSignalHandler();

    RunSilo(silo_id, silo_ipaddr, data_filename, third_party_ipaddr, silo_ip_filename, index_type);

    return 0;
}

