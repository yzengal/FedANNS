// allocator.h - 独立的分配器类（支持动态配置）
#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <unordered_set>
#include <queue>
#include <cassert>
#include <limits>

#include "DataType.hpp"
#include "MetricType.hpp"
#include "BenchLogger.hpp"


using Distance = EuclideanSquareDistance;

// ==================== 分配结果 ====================

struct AllocationResult {
    int budget;                     // 本轮总预算
    std::vector<int> allocations;   // 每个 owner 的分配数量
    double entropy;                 // 当前熵值
    std::vector<double> scores;     // 更新后的评分
};

// ==================== 分配器主类 ====================

class Allocator {
private:
    static constexpr VectorDimensionType INF_DIST = 1e10;  // ← 定义静态常量
    static constexpr VectorDimensionType PRECISION_DIST = 1e-7;  // ← 定义静态常量


    QueryConfig cfg;
    
    // 状态变量
    std::vector<double> g_score;                // 每个 owner 的评分
    std::vector<std::vector<int>> candidates;  // 每个 owner 的候选点在 collected 集合中的下标列表（已排序）
    std::vector<int> confirmed_next_idx;        // 每个 owner 下一个确定为 Top-K 的数据下标
    std::vector<VidType> current_topk_ids;     // 当前的 Top-K ID 列表
    std::vector<SidType> current_topk_owners;     // 当前的 Top-K Silo ID 列表
    std::vector<VectorDimensionType> current_topk_dists; // 当前的 Top-K 距离列表

    // collected 作为成员变量
    struct CollectedItem {
        VidType id;
        SidType owner_id;
        VectorDimensionType dist;
        int round_collected;

        // 构造函数
        CollectedItem(VidType _id, SidType _owner, VectorDimensionType _dist, int _round) 
            : id(_id), owner_id(_owner), dist(_dist), round_collected(_round) {}
    };
    std::vector<CollectedItem> collected;
    size_t last_collected_size = 0;        // 记录上次更新时的 collected 大小
    int current_round = 0;
    
    // ==================== 私有辅助函数 ====================
    
    // 计算熵
    double calc_entropy() const {
        double sum = 0.0;
        for (double s : g_score) sum += s;
        if (sum < 1e-10) return 0.0;
        
        double H = 0.0;
        for (double s : g_score) {
            double p = s / sum;
            if (p > 1e-10) H -= p * std::log(p);
        }
        return H;
    }
    
    // ==================== inner_allocate 的依赖函数 ====================
    
    // 获取已采集样本中满足条件的最大距离
    struct OwnerDistanceInfo {
        VectorDimensionType min_dist;                 // 所有 owner 中最小的 max_dist
        SidType min_dist_owner;              // 拥有最小 max_dist 的 owner
    };
    
    OwnerDistanceInfo compute_owner_distances() const {
        OwnerDistanceInfo info;
        info.min_dist = INF_DIST;
        info.min_dist_owner = -1;
        if (current_topk_dists.empty()) return info;

        info.min_dist = *current_topk_dists.rbegin();
        info.min_dist_owner = *current_topk_owners.rbegin();
        
        // for (int i = 0; i < cfg.m; ++i) {
        //     if (confirmed_next_idx[i] >= candidates[i].size())
        //         continue;
        //     int collected_idx = candidates[i][confirmed_next_idx[i]];
        //     const auto& item = collected[collected_idx];
        //     if (item.dist < info.min_dist) {
        //         info.min_dist = item.dist;
        //         info.min_dist_owner = item.owner_id;
        //     }
        //     assert(item.dist > current_kth_dist + PRECISION_DIST);
        // }
        
        return info;
    }
    
    // 获取确认在 GT 中的样本 ID 数量（距离 <= min_dist）
    int compute_confirmed_hit(const int owner_id) const {
        return confirmed_next_idx[owner_id];
    }
    
    // 计算每个 owner 的不确定样本数量（距离 > min_dist）
    int compute_uncertain_count(const int owner_id) const {
        return (int) candidates[owner_id].size() - confirmed_next_idx[owner_id];
    }
    
    // 找到 g_score 最高的 owner（排除指定 owner）
    SidType find_best_owner(int exclude_owner) const {
        SidType best_owner = -1;
        double best_score = -INF_DIST;
        for (int i = 0; i < cfg.m; ++i) {
            if (i == exclude_owner) 
                continue;
            if (g_score[i] > best_score && candidates[i].size() < cfg.k) {
                best_score = g_score[i];
                best_owner = i;
            }
        }
        return best_owner;
    }
    
    // 计算 min_dist_owner 的命中率
    double compute_hit_rate(int total_hit) const {
        int total_confirmed = current_topk_ids.size();
        return (total_confirmed > 0) ? ((double) total_hit / total_confirmed) : 0.0;
    }
    
    // 计算 min_dist_owner 中距离 <= 其当前最大距离的样本数
    int count_less_than_max_dist(int owner_id, double max_dist) const {
        return confirmed_next_idx[owner_id];

        int count = 0;
        for (const auto& item : collected) {
            if (item.owner_id == owner_id && item.dist <= max_dist) {
                count++;
            }
        }
        return count;
    }

    /**
     * 计算并更新当前 Top-K（基于已采集的所有样本）
     * 使用 m 路归并算法
     */
    void update_topk() {
        if (last_collected_size >= collected.size()) return;
        
        // 1. 每个 owner 内部已经按距离排序，直接进行 m 路归并取 Top-K
        struct Candidate {
            VectorDimensionType dist;
            SidType owner_id;
            
            // 构造函数
            Candidate(VectorDimensionType _dist, SidType _owner) 
                : dist(_dist), owner_id(_owner) {}
                
            bool operator>(const Candidate& other) const {
                return dist > other.dist;
            }
        };
        
        std::priority_queue<Candidate, std::vector<Candidate>, std::greater<>> pq;
        for (int i = 0; i < cfg.m; ++i) {
            if (confirmed_next_idx[i] >= candidates[i].size())
                continue;
            const int collected_idx = candidates[i][confirmed_next_idx[i]];
            pq.push(Candidate(collected[collected_idx].dist, i));
        }
        
        int topk_size = std::min(cfg.k, (int)collected.size());
        
        while (!pq.empty() && current_topk_ids.size()<topk_size) {
            Candidate top = pq.top();
            pq.pop();

            const SidType owner_id = top.owner_id;
            const int collected_idx = candidates[owner_id][confirmed_next_idx[owner_id]];
            const auto& item = collected[collected_idx];
            current_topk_ids.push_back(item.id);
            current_topk_dists.push_back(item.dist);
            current_topk_owners.push_back(item.owner_id);

            confirmed_next_idx[owner_id]++;
            if (confirmed_next_idx[owner_id] < (int)candidates[owner_id].size()) {
                const int collected_idx = candidates[owner_id][confirmed_next_idx[owner_id]];
                pq.push(Candidate(collected[collected_idx].dist, owner_id));
            } else {
                break;
            }
        }
        
        #ifdef LOCAL_DEBUG
        std::cout << "[Top-K] Updated: " << current_topk_ids.size() << " items" << std::endl;
        #endif
    }

    /**
     * 手动更新评分（需要在 add_collected 之后调用）
     */
    void update_scores() {
        if (collected.empty()) return;
        if (last_collected_size >= collected.size()) return;
        
        // 计算本轮新增样本的命中率
        std::vector<int> new_hit(cfg.m, 0);
        
        // 遍历当前所有样本，计算各方命中率
        for (auto owner_id : current_topk_owners) {
            new_hit[owner_id]++;
        }
        
        // 计算本轮命中率
        int total_hit = current_topk_ids.size();
        std::vector<double> hr(cfg.m, 0.0);
        for (int i = 0; i < cfg.m; ++i) {
            if (total_hit > 0) {
                hr[i] = (double)new_hit[i] / total_hit;
            }
        }
        
        #ifdef LOCAL_DEBUG
        std::cout << "  第" << current_round << "轮  [本轮命中率 hr] ";
        for (int i = 0; i < cfg.m; ++i) {
            std::cout << "O" << i << "=" << std::fixed << std::setprecision(4) << hr[i] << " ";
        }
        std::cout << " | hit:";
        for (int i = 0; i < cfg.m; ++i) {
            std::cout << " " << new_hit[i];
        }
        std::cout << std::endl;
        #endif
        
        // EWMA 更新 g_score
        for (int i = 0; i < cfg.m; ++i) {
            g_score[i] = (1.0 - cfg.eta) * g_score[i] + cfg.eta * hr[i];
            g_score[i] = std::min(1.0, g_score[i]);
        }
        
        #ifdef LOCAL_DEBUG
        std::cout << "  第" << current_round << "轮  [更新后 g_score] ";
        for (int i = 0; i < cfg.m; ++i) {
            std::cout << "O" << i << "=" << std::fixed << std::setprecision(4) << g_score[i] << " ";
        }
        std::cout << std::endl;
        #endif
        
        // 更新 last_collected_size
        last_collected_size = collected.size();
    }
    

public:
    // ==================== 构造函数 ====================
    Allocator(const QueryConfig& config) : cfg(config) {
        reset_state();
    }
    
    // ==================== 配置管理 ====================
    
    /**
     * 设置新的配置
     * @param new_cfg 新的配置
     * @param reset 是否重置内部状态（g_score、collected 等）
     *                    默认 false，只更新配置不重置状态
     */
    void set_cfg(const QueryConfig& new_cfg, bool reset=true) {
        // 记录旧的 m，用于判断是否需要调整向量大小
        int old_m = cfg.m;
        
        // 更新配置
        cfg = new_cfg;
        
        // 如果 m 发生变化，需要调整相关向量的大小
        // 如果需要重置状态
        if (cfg.m != old_m || reset) {
            reset_state();
        }
        
        #ifdef LOCAL_DEBUG
        std::cout << "[Allocator] Configuration updated." << std::endl;
        cfg.print();
        #endif
    }
    
    /**
     * 获取当前配置
     */
    const QueryConfig& get_cfg() const {
        return cfg;
    }
    
    // ==================== 状态管理 ====================
    void reset_state() {
        last_collected_size = 0;
        current_round = 0;
        collected.clear();
        current_topk_dists.clear();
        current_topk_ids.clear();
        current_topk_owners.clear();
        g_score.assign(cfg.m, cfg.g_init);
        confirmed_next_idx.assign(cfg.m, 0);
        candidates.assign(cfg.m, std::vector<int>());
    }
    
    // ==================== 核心接口：添加采集样本 ====================
    
    void add_collected(const std::vector<VidType>& ids, 
                        const std::vector<SidType>& owners,
                        const std::vector<VectorDimensionType>& dists,
                        int round) {
        for (size_t i = 0; i < ids.size(); ++i) {
            candidates[owners[i]].push_back(collected.size());
            collected.push_back(CollectedItem(ids[i], owners[i], dists[i], round));
        }
        current_round = round;

        update_topk();
        update_scores();
    }
    
    
    // ==================== 公共接口 1: compute_budget ====================
    int compute_budget(int remaining, int rounds_left) {
        if (rounds_left <= 1) {
            #ifdef LOCAL_DEBUG
            std::cout << "[compute_budget] Last round: returning remaining=" << remaining << std::endl;
            #endif
            return remaining;
        }
        
        double current_entropy = calc_entropy();
        double H_max = std::log((double)cfg.m);
        double H_norm = (H_max > 0) ? std::max(0.0, std::min(1.0, current_entropy / H_max)) : 1.0;
        
        #ifdef LOCAL_DEBUG
        std::cout << "[compute_budget] -------- Budget Calculation --------" << std::endl;
        std::cout << "[compute_budget] remaining=" << remaining 
                  << ", rounds_left=" << rounds_left 
                  << ", m=" << cfg.m << std::endl;
        std::cout << "[compute_budget] current_entropy=" << std::fixed << std::setprecision(4) << current_entropy
                  << ", H_max=" << H_max
                  << ", H_norm=" << H_norm << std::endl;
        #endif
        
        // 熵反比：熵低（确定）→ 多取，熵高（不确定）→ 少取
        double factor = 1.0 + cfg.lambda * (1.0 - H_norm);
        factor = std::max(0.5, std::min(1.5, factor));
        
        #ifdef LOCAL_DEBUG
        std::cout << "[compute_budget] factor_before_clamp=" << factor
                  << " (lambda=" << cfg.lambda << ")" << std::endl;
        #endif
        
        double baseline = (double)remaining / rounds_left;
        int budget = (int)std::round(baseline * factor);
        
        #ifdef LOCAL_DEBUG
        std::cout << "[compute_budget] baseline=" << std::setprecision(4) << baseline
                  << ", budget_before_clamp=" << budget << std::endl;
        #endif
        
        // 应用边界约束
        budget = std::max(1, std::min(budget, remaining / 2 + 1));
        budget = std::max(1, std::min(budget, remaining));
        
        #ifdef LOCAL_DEBUG
        std::cout << "[compute_budget] final_budget=" << budget << std::endl;
        std::cout << "[compute_budget] ---------------------------------------" << std::endl;
        #endif
        
        return budget;
    }
    
    // ==================== 公共接口 2: inner_allocate ====================
    std::vector<size_t> inner_allocate(int round_id, int budget) {
        std::vector<size_t> alloc(cfg.m, 0);
        
        if (round_id == 0) {
            std::fill(alloc.begin(), alloc.end(), (size_t)cfg.b_init);
            #ifdef LOCAL_DEBUG
            std::cout << "\t\t" << "Round " << round_id << " [alloc] =";
            for (auto c : alloc)
                std::cout << " " << c;
            std::cout << std::endl;
            #endif
            return alloc;
        }
        
        if (budget <= 0) return alloc;
        
        // ================================================================
        // 1. 计算 min_dist：所有 Owner 已取样本最大距离中的最小值
        // ================================================================
        auto dist_info = compute_owner_distances();
        VectorDimensionType min_dist = dist_info.min_dist;
        SidType min_dist_owner = dist_info.min_dist_owner;
        
        if (min_dist_owner < 0) {
            throw std::runtime_error("No collected samples found when computing distances");
        }
        
        #ifdef LOCAL_DEBUG
        std::cout << "\t\t" << "Round " << round_id 
                  << " [min_dist=" << std::fixed << std::setprecision(4) << min_dist 
                  << ", min_dist_owner=O" << min_dist_owner << "]" << std::endl;
        #endif
        
        // ================================================================
        // 2. 获取 100% 确定在 GT 中的样本
        // ================================================================
        int hit = compute_confirmed_hit(min_dist_owner);
        int uncertain = compute_uncertain_count(min_dist_owner);
        
        // ================================================================
        // 3. 计算命中率
        // ================================================================
        double hit_rate = compute_hit_rate(hit);
        double threshold = cfg.k * hit_rate;
        
        // ================================================================
        // 4. 判断潜力是否耗尽
        // ================================================================
        bool potential_exhausted = (candidates[min_dist_owner].size() >= threshold);
        
        #ifdef LOCAL_DEBUG
        std::cout << "\t\t" << "Round " << round_id 
                  << " [min_dist_owner=O" << min_dist_owner 
                  << ", hit_rate=" << std::fixed << std::setprecision(4) << hit_rate
                  << ", k*hit_rate=" << std::fixed << std::setprecision(2) << threshold
                  << ", uncertain_count=" << uncertain
                  << ", collected_count=" << candidates[min_dist_owner].size()
                  << ", potential_exhausted=" << (potential_exhausted ? "YES" : "NO") << "]" << std::endl;
        #endif
        
        // ================================================================
        // 5. 确定合格的 Owner
        // ================================================================
        SidType best_owner = find_best_owner(min_dist_owner);
        std::vector<bool> is_qualified(cfg.m, false);
        
        if (potential_exhausted && best_owner == -1) {
            is_qualified[min_dist_owner] = true;
        } else if (potential_exhausted && best_owner != -1) {
            is_qualified[best_owner] = true;
            #ifdef LOCAL_DEBUG
            std::cout << "\t\t" << "Round " << round_id 
                      << " [切换] 从 O" << min_dist_owner << " 切换到 O" << best_owner 
                      << " (g_score=" << std::fixed << std::setprecision(4) << g_score[best_owner] << ")" << std::endl;
            #endif
        } else {
            // 潜力充足：只给 min_dist_owner 分配，或分配给两个 owner
            is_qualified[min_dist_owner] = true;
            int potential_remaining = (int)std::ceil(threshold) - candidates[min_dist_owner].size();
            
            if (potential_remaining < budget && best_owner != -1) {
                // 分配给两个 owner
                is_qualified[best_owner] = true;
            }
        }
        
        // ================================================================
        // 6. 分配预算
        // ================================================================
        int qualified_count = 0;
        for (int i = 0; i < cfg.m; ++i) {
            if (is_qualified[i]) qualified_count++;
        }
        
        if (qualified_count == 2) {
            int potential_remaining = (int)std::ceil(threshold) - candidates[min_dist_owner].size();
            potential_remaining = std::max((int)1, potential_remaining);
            alloc[min_dist_owner] = std::min(potential_remaining, budget);
            alloc[best_owner] = budget - alloc[min_dist_owner];
        } else if (qualified_count == 1) {
            int target_owner_id = potential_exhausted ? (best_owner != -1 ? best_owner : min_dist_owner) : min_dist_owner;
            alloc[target_owner_id] = std::max((int)1, budget);
        } else {
            throw std::runtime_error("At least one silo should be qualified");
            // // fallback：均匀分配
            // int base = budget / cfg.m;
            // int rem = budget % cfg.m;
            // for (int i = 0; i < cfg.m; ++i) {
            //     alloc[i] = base + (i < rem ? 1 : 0);
            // }
        }
        
        // ================================================================
        // 7. 最终检查：确保不超过可用候选数
        // ================================================================
        for (int i = 0; i < cfg.m; ++i) {
            int avail = cfg.k - (int)candidates[i].size();
            alloc[i] = std::min((int)alloc[i], avail);
            if (!is_qualified[i]) alloc[i] = 0;
        }
        
        #ifdef LOCAL_DEBUG
        std::cout << "\t\t" << "Round " << round_id << " [qualified] = ";
        for (int i = 0; i < cfg.m; ++i) {
            std::cout << (is_qualified[i] ? "1" : "0");
            if (i < cfg.m - 1) std::cout << " ";
        }
        std::cout << ", [alloc] =";
        for (auto c : alloc)
            std::cout << " " << c;
        std::cout << std::endl;
        #endif
        
        return alloc;
    }
};

#endif // ALLOCATOR_H