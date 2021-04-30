/**
 *  Copyright (c) 2015 by Contributors
 *  Modifications Copyright (c) 2021 by Contributors at INET-RC
 */
#include <unistd.h>
#include <thread>
#include <chrono>
#include "ps/internal/postoffice.h"
#include "ps/internal/message.h"
#include "ps/base.h"

namespace ps {
Postoffice::Postoffice() {
  van_ = Van::Create("zmq");
  env_ref_ = Environment::_GetSharedRef();
}

void Postoffice::InitEnvironment() {
  const char* val = NULL;
  std::string role;
  verbose_ = GetEnv("PS_VERBOSE", 0);
  val = Environment::Get()->find("DMLC_ROLE");
  if (val) {
    role = (std::string)val;
    is_worker_ = role == "worker";
    is_server_ = role == "server";
    is_scheduler_ = role == "scheduler";
    val = CHECK_NOTNULL(Environment::Get()->find("DMLC_NUM_WORKER"));
    num_workers_ = atoi(val);
    val = CHECK_NOTNULL(Environment::Get()->find("DMLC_NUM_SERVER"));
    num_servers_ = atoi(val);
    is_master_worker_ = GetEnv("DMLC_ROLE_MASTER_WORKER", false);
    if (is_master_worker_) CHECK(is_worker_);
  }
  val = Environment::Get()->find("DMLC_ROLE_GLOBAL");
  if (val) {
    role = (std::string) val;
    is_global_scheduler_ = role == "global_scheduler";
    is_global_server_ = role == "global_server";
    if (is_global_server_) is_server_ = true;
  }
  if (is_server_ || is_global_scheduler_) {
    val = CHECK_NOTNULL(Environment::Get()->find("DMLC_NUM_GLOBAL_WORKER"));
    num_global_workers_ = atoi(val);
    val = CHECK_NOTNULL(Environment::Get()->find("DMLC_NUM_GLOBAL_SERVER"));
    num_global_servers_ = atoi(val);
  }
  if (is_worker_ || is_global_server_) {
    val = CHECK_NOTNULL(Environment::Get()->find("DMLC_NUM_ALL_WORKER"));
    num_all_workers_ = atoi(val);
  }
  if (is_global_server_) {
    enable_central_workers_ = GetEnv("DMLC_ENABLE_CENTRAL_WORKER", false);
  }
  if (is_server_ && !is_global_server_) {
    CHECK_EQ(num_servers_, 1) << "load balance at the first layer is not supported.";
  }
}

void Postoffice::Start(int customer_id, const char* argv0, const bool do_barrier) {
  start_mu_.lock();
  if (init_stage_ == 0) {
    InitEnvironment();
    // init glog
    if (argv0) {
      dmlc::InitLogging(argv0);
    } else {
      dmlc::InitLogging("ps-lite\0");
    }

    // init node info.
    for (int i = 0; i < num_workers_; ++i) {
      int id = WorkerRankToID(i, false);
      for (int g : {id, kWorkerGroup, kWorkerGroup + kServerGroup,
                    kWorkerGroup + kScheduler,
                    kWorkerGroup + kServerGroup + kScheduler}) {
        node_ids_[g].push_back(id);
      }
    }

    for (int i = 0; i < num_servers_; ++i) {
      int id = ServerRankToID(i, false);
      for (int g : {id, kServerGroup, kWorkerGroup + kServerGroup,
                    kServerGroup + kScheduler,
                    kWorkerGroup + kServerGroup + kScheduler}) {
        node_ids_[g].push_back(id);
      }
    }

    for (int g : {kScheduler, kScheduler + kServerGroup + kWorkerGroup,
                  kScheduler + kWorkerGroup, kScheduler + kServerGroup}) {
      node_ids_[g].push_back(kScheduler);
    }

    init_stage_++;
  }
  start_mu_.unlock();

  // start van
  van_->Start(customer_id);

  start_mu_.lock();
  if (init_stage_ == 1) {
    // record start time
    start_time_ = time(NULL);
    init_stage_++;
  }
  start_mu_.unlock();
  // do a barrier here
  if (do_barrier) Barrier(customer_id, kWorkerGroup + kServerGroup + kScheduler);
}

void Postoffice::StartGlobal(int customer_id, const bool do_barrier) {
  start_mu_.lock();

  // init node info.
  for (int i = 0; i < num_global_workers_; ++i) {
    int id = WorkerRankToID(i, true);
    for (int g : {id, kWorkerGroupGlobal, kWorkerGroupGlobal + kServerGroupGlobal,
                  kWorkerGroupGlobal + kSchedulerGlobal,
                  kWorkerGroupGlobal + kServerGroupGlobal + kSchedulerGlobal}) {
      node_global_ids_[g].push_back(id);
    }
  }

  for (int i = 0; i < num_global_servers_; ++i) {
    int id = ServerRankToID(i, true);
    for (int g : {id, kServerGroupGlobal, kWorkerGroupGlobal + kServerGroupGlobal,
                  kServerGroupGlobal + kSchedulerGlobal,
                  kWorkerGroupGlobal + kServerGroupGlobal + kSchedulerGlobal}) {
      node_global_ids_[g].push_back(id);
    }
  }

  for (int g : {kSchedulerGlobal, kSchedulerGlobal + kWorkerGroupGlobal,
                kSchedulerGlobal + kServerGroupGlobal,
                kSchedulerGlobal + kWorkerGroupGlobal + kServerGroupGlobal}) {
    node_global_ids_[g].push_back(kSchedulerGlobal);
  }

  start_mu_.unlock();

  // start van
  van_->StartGlobal(customer_id);

  // do a barrier here
  if (do_barrier) Barrier(customer_id, kWorkerGroupGlobal + kServerGroupGlobal + kSchedulerGlobal, true);
}

void Postoffice::Finalize(const int customer_id, const bool do_barrier, bool is_global) {
  int barrier_group = is_global ? kWorkerGroupGlobal + kServerGroupGlobal + kSchedulerGlobal :
                                  kWorkerGroup + kServerGroup + kScheduler;
  if (do_barrier) Barrier(customer_id, barrier_group, is_global);
  if ((is_global && ( is_server_ || is_global_server_ || is_global_scheduler_ ) && customer_id == 0)
        || (!is_global && ( is_worker_ || is_scheduler_ ) && customer_id == 0)) {
    van_->Stop(is_global);
    if (exit_callback_) exit_callback_();
  }
}

void Postoffice::AddCustomer(Customer* customer) {
  std::lock_guard<std::mutex> lk(mu_);
  int app_id = CHECK_NOTNULL(customer)->app_id();
  // check if the customer id has existed
  int customer_id = CHECK_NOTNULL(customer)->customer_id();
  CHECK_EQ(customers_[app_id].count(customer_id), (size_t) 0) << "customer_id " \
    << customer_id << " already exists\n";
  customers_[app_id].insert(std::make_pair(customer_id, customer));
  std::unique_lock<std::mutex> ulk(barrier_mu_);
  barrier_done_[app_id].insert(std::make_pair(customer_id, false));
}


void Postoffice::RemoveCustomer(Customer* customer) {
  std::lock_guard<std::mutex> lk(mu_);
  int app_id = CHECK_NOTNULL(customer)->app_id();
  int customer_id = CHECK_NOTNULL(customer)->customer_id();
  customers_[app_id].erase(customer_id);
  if (customers_[app_id].empty()) {
    customers_.erase(app_id);
  }
}


Customer* Postoffice::GetCustomer(int app_id, int customer_id, int timeout) const {
  Customer* obj = nullptr;
  for (int i = 0; i < timeout * 1000 + 1; ++i) {
    {
      std::lock_guard<std::mutex> lk(mu_);
      const auto it = customers_.find(app_id);
      if (it != customers_.end()) {
        std::unordered_map<int, Customer*> customers_in_app = it->second;
        obj = customers_in_app[customer_id];
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return obj;
}

void Postoffice::Barrier(int customer_id, int node_group, bool is_global) {
  if (GetNodeIDs(node_group, is_global).size() <= 1) return;
  auto role = van_->my_node(is_global).role;
  if (role == Node::SCHEDULER) {
    CHECK(node_group & kScheduler);
  } else if (role == Node::WORKER) {
    CHECK(node_group & kWorkerGroup);
  } else if (role == Node::GLOBAL_SCHEDULER) {
    CHECK(node_group & kSchedulerGlobal);
  } else if (role == Node::GLOBAL_SERVER) {
    CHECK(node_group & kServerGroupGlobal);
  } else if (role == Node::SERVER) {
    if (is_global) {
      CHECK(node_group & kWorkerGroupGlobal);
    } else {
      CHECK(node_group & kServerGroup);
    }
  }

  Message req;
  req.meta.recver = is_global ? kSchedulerGlobal : kScheduler;
  req.meta.request = true;
  req.meta.control.cmd = is_global ? Control::BARRIER_GLOBAL : Control::BARRIER;
  req.meta.app_id = 0;
  req.meta.customer_id = customer_id;
  req.meta.control.barrier_group = node_group;
  req.meta.timestamp = van_->GetTimestamp();
  CHECK_GT(van_->Send(req, is_global), 0);

  if (is_global) {
    std::unique_lock<std::mutex> ulk(barrier_mu_);
    barrier_global_done_[0][customer_id] = false;
    barrier_global_cond_.wait(ulk, [this, customer_id] {
        return barrier_global_done_[0][customer_id];
    });
  } else {
    std::unique_lock<std::mutex> ulk(barrier_mu_);
    barrier_done_[0][customer_id] = false;
    barrier_cond_.wait(ulk, [this, customer_id] {
        return barrier_done_[0][customer_id];
    });
  }
}

const std::vector<Range>& Postoffice::GetServerKeyRanges(bool is_global) {
  server_key_ranges_mu_.lock();
  auto& server_key_ranges = is_global ? server_key_ranges_global_ : server_key_ranges_;
  const int num_servers = is_global ? num_global_servers_ : num_servers_;
  if (server_key_ranges.empty()) {
    for (int i = 0; i < num_servers; ++i) {
      server_key_ranges.push_back(Range(
          kMaxKey / num_servers * i,
          kMaxKey / num_servers * (i+1)));
    }
  }
  server_key_ranges_mu_.unlock();
  return server_key_ranges;
}

void Postoffice::Manage(const Message& recv, bool is_global) {
  CHECK(!recv.meta.control.empty());
  const auto& ctrl = recv.meta.control;
  const auto& cmd = is_global ? Control::BARRIER_GLOBAL : Control::BARRIER;
  if (ctrl.cmd == cmd && !recv.meta.request) {
    if (is_global) {
      barrier_mu_.lock();
      for (size_t customer_id = 0; customer_id < barrier_global_done_[recv.meta.app_id].size(); customer_id++) {
        barrier_global_done_[recv.meta.app_id][customer_id] = true;
      }
      barrier_mu_.unlock();
      barrier_global_cond_.notify_all();
    } else {
      barrier_mu_.lock();
      for (size_t customer_id = 0; customer_id < barrier_done_[recv.meta.app_id].size(); customer_id++) {
        barrier_done_[recv.meta.app_id][customer_id] = true;
      }
      barrier_mu_.unlock();
      barrier_cond_.notify_all();
    }
  }
}

std::vector<int> Postoffice::GetDeadNodes(int t) {
  std::vector<int> dead_nodes;
  if (!van_->IsReady() || t == 0) return dead_nodes;

  time_t curr_time = time(NULL);
  const auto& nodes = is_scheduler_
    ? GetNodeIDs(kWorkerGroup + kServerGroup)
    : GetNodeIDs(kScheduler);
  {
    std::lock_guard<std::mutex> lk(heartbeat_mu_);
    for (int r : nodes) {
      auto it = heartbeats_.find(r);
      if ((it == heartbeats_.end() || it->second + t < curr_time)
            && start_time_ + t < curr_time) {
        dead_nodes.push_back(r);
      }
    }
  }
  return dead_nodes;
}
}  // namespace ps
