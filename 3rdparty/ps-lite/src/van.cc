/**
 *  Copyright (c) 2015 by Contributors
 *  Modifications Copyright (c) 2021 by Contributors at INET-RC
 */
#include "ps/internal/van.h"
#include <thread>
#include <chrono>
#include "ps/base.h"
#include "ps/sarray.h"
#include "ps/internal/postoffice.h"
#include "ps/internal/customer.h"
#include "./network_utils.h"
#include "./meta.pb.h"
#include "./zmq_van.h"
#include "./resender.h"
#include "ps/simple_app.h"
#include "./half_float/umHalf.h"
namespace ps {

// interval in second between to heartbeast signals. 0 means no heartbeat.
// don't send heartbeast in default. because if the scheduler received a
// heartbeart signal from a node before connected to that node, then it could be
// problem.
static const int kDefaultHeartbeatInterval = 0;

Van* Van::Create(const std::string& type) {
  if (type == "zmq") {
    return new ZMQVan();
  } else {
    LOG(FATAL) << "unsupported van type: " << type;
    return nullptr;
  }
}

void Van::ProcessTerminateCommand(bool is_global) {
  PS_VLOG(1) << my_node(is_global).ShortDebugString() << " is stopped";
  auto& ready = is_global ? ready_global_ : ready_;
  ready = false;
}

void Van::ProcessAddNodeCommandAtScheduler(
        Message* msg, Meta* nodes, Meta* recovery_nodes) {
  recovery_nodes->control.cmd = Control::ADD_NODE;
  time_t t = time(NULL);
  size_t num_nodes = Postoffice::Get()->num_servers() + Postoffice::Get()->num_workers();
  if (nodes->control.node.size() == num_nodes) {
    // sort the nodes according their ip and port,
    std::sort(nodes->control.node.begin(), nodes->control.node.end(),
              [](const Node& a, const Node& b) {
                  return (a.hostname.compare(b.hostname) | (a.port < b.port)) > 0;
              });
    // assign node rank
    for (auto& node : nodes->control.node) {
      std::string node_host_ip = node.hostname + ":" + std::to_string(node.port);
      if (connected_nodes_.find(node_host_ip) == connected_nodes_.end()) {
        CHECK_EQ(node.id, Node::kEmpty);
        int id = node.role == Node::SERVER ?
                 Postoffice::ServerRankToID(num_servers_, false) :
                 Postoffice::WorkerRankToID(num_workers_, false);
        PS_VLOG(1) << "assign id=" << id << " to node " << node.DebugString();
        node.id = id;
        Connect(node);
        Postoffice::Get()->UpdateHeartbeat(node.id, t);
        connected_nodes_[node_host_ip] = id;
      } else {
        int id = node.role == Node::SERVER ?
                 Postoffice::ServerRankToID(num_servers_, false) :
                 Postoffice::WorkerRankToID(num_workers_, false);
        shared_node_mapping_[id] = connected_nodes_[node_host_ip];
        node.id = connected_nodes_[node_host_ip];
      }
      if (node.role == Node::SERVER) num_servers_++;
      if (node.role == Node::WORKER) num_workers_++;
    }
    nodes->control.node.push_back(my_node_);
    nodes->control.cmd = Control::ADD_NODE;
    Message back;
    back.meta = *nodes;
    for (int r : Postoffice::Get()->GetNodeIDs(kWorkerGroup + kServerGroup, false)) {
      int recver_id = r;
      if (shared_node_mapping_.find(r) == shared_node_mapping_.end()) {
        back.meta.recver = recver_id;
        back.meta.timestamp = timestamp_++;
        CHECK_NE(Send(back), -1);
      }
    }
    PS_VLOG(1) << "the scheduler is connected to "
               << num_workers_ << " workers and " << num_servers_ << " servers";
    ready_ = true;
  } else if (!recovery_nodes->control.node.empty()) {
    auto dead_nodes = Postoffice::Get()->GetDeadNodes(heartbeat_timeout_);
    std::unordered_set<int> dead_set(dead_nodes.begin(), dead_nodes.end());
    // send back the recovery node
    CHECK_EQ(recovery_nodes->control.node.size(), 1);
    Connect(recovery_nodes->control.node[0]);
    Postoffice::Get()->UpdateHeartbeat(recovery_nodes->control.node[0].id, t);
    Message back;
    for (int r : Postoffice::Get()->GetNodeIDs(kWorkerGroup + kServerGroup)) {
      if (r != recovery_nodes->control.node[0].id
          && dead_set.find(r) != dead_set.end()) {
        // do not try to send anything to dead node
        continue;
      }
      // only send recovery_node to nodes already exist
      // but send all nodes to the recovery_node
      back.meta = (r == recovery_nodes->control.node[0].id) ? *nodes : *recovery_nodes;
      back.meta.recver = r;
      back.meta.timestamp = timestamp_++;
      CHECK_NE(Send(back, false), -1);
    }
  }
}

void Van::ProcessAddGlobalNodeCommandAtScheduler(Message* msg, Meta* nodes) {
  // time_t t = time(NULL);
  size_t num_nodes = Postoffice::Get()->num_global_servers() + Postoffice::Get()->num_global_workers();
  PS_VLOG(1) << nodes->control.node.size() << "--" << num_nodes;
  if (nodes->control.node.size() == num_nodes) {
    // sort the nodes according to their ip and port
    std::sort(nodes->control.node.begin(), nodes->control.node.end(),
              [](const Node& a, const Node& b) {
      return (a.hostname.compare(b.hostname) | (a.port < b.port)) > 0;
    });
    // assign node rank
    for (auto& node : nodes->control.node) {
      std::string node_host_ip = node.hostname + ":" + std::to_string(node.port);
      if (connected_nodes_.find(node_host_ip) == connected_nodes_.end()) {
        CHECK_EQ(node.id, Node::kEmpty);
        int id = node.role == Node::GLOBAL_SERVER ?
                 Postoffice::ServerRankToID(num_global_servers_, true) :
                 Postoffice::WorkerRankToID(num_global_workers_, true);
        PS_VLOG(1) << "assign id=" << id << " to node " << node.DebugString();
        node.id = id;
        Connect(node, true);
        connected_nodes_[node_host_ip] = id;
      } else {
        int id = node.role == Node::GLOBAL_SERVER ?
                 Postoffice::ServerRankToID(num_global_servers_, true) :
                 Postoffice::WorkerRankToID(num_global_workers_, true);
        shared_node_mapping_[id] = connected_nodes_[node_host_ip];
        node.id = connected_nodes_[node_host_ip];
      }
      if (node.role == Node::GLOBAL_SERVER) num_global_servers_++;
      if (node.role == Node::SERVER)        num_global_workers_++;
    }
    nodes->control.node.push_back(my_node_global_);
    nodes->control.cmd = Control::ADD_GLOBAL_NODE;
    Message back;
    back.meta = *nodes;
    for (int r : Postoffice::Get()->GetNodeIDs(kWorkerGroupGlobal + kServerGroupGlobal, true)) {
      int recver_id = r;
      if (shared_node_mapping_.find(r) == shared_node_mapping_.end()) {
        back.meta.recver = recver_id;
        back.meta.timestamp = timestamp_++;
        CHECK_NE(Send(back, true), -1);
      }
    }
    PS_VLOG(1) << "the scheduler is connected to " << num_global_workers_
               << " servers and " << num_global_servers_ << " global servers";
    ready_ = true;
    ready_global_ = true;
  }
}

void Van::UpdateLocalID(Message* msg, std::unordered_set<int>* deadnodes_set,
                        Meta* nodes, Meta* recovery_nodes) {
  auto& ctrl = msg->meta.control;
  size_t num_nodes = Postoffice::Get()->num_servers() + Postoffice::Get()->num_workers();
  // assign an id
  if (msg->meta.sender == Meta::kEmpty) {
    CHECK(is_scheduler_);
    CHECK_EQ(ctrl.node.size(), 1);
    if (nodes->control.node.size() < num_nodes) {
      nodes->control.node.push_back(ctrl.node[0]);
    } else {
      // some node dies and restarts
      CHECK(ready_.load());
      for (size_t i = 0; i < nodes->control.node.size() - 1; ++i) {
        const auto& node = nodes->control.node[i];
        if (deadnodes_set->find(node.id) != deadnodes_set->end() &&
            node.role == ctrl.node[0].role) {
          auto& recovery_node = ctrl.node[0];
          // assign previous node id
          recovery_node.id = node.id;
          recovery_node.is_recovery = true;
          PS_VLOG(1) << "replace dead node " << node.DebugString()
                     << " by node " << recovery_node.DebugString();
          nodes->control.node[i] = recovery_node;
          recovery_nodes->control.node.push_back(recovery_node);
          break;
        }
      }
    }
  }

  // update my id
  for (size_t i = 0; i < ctrl.node.size(); ++i) {
    const auto& node = ctrl.node[i];
    if (my_node_.hostname == node.hostname && my_node_.port == node.port) {
    if (getenv("DMLC_RANK") == nullptr || my_node_.id == Meta::kEmpty) {
        my_node_ = node;
        std::string rank = std::to_string(Postoffice::IDtoRank(node.id));
#ifdef _MSC_VER
        _putenv_s("DMLC_RANK", rank.c_str());
#else
        setenv("DMLC_RANK", rank.c_str(), true);
#endif
      }
    }
  }
}

void Van::UpdateServerID(Message* msg, Meta* nodes) {
  auto& ctrl = msg->meta.control;
  size_t num_nodes = Postoffice::Get()->num_global_servers() + Postoffice::Get()->num_global_workers();
  // assign id
  if (msg->meta.sender == Meta::kEmpty) {
    CHECK(is_global_scheduler_);
    CHECK_EQ(ctrl.node.size(), 1);
    if (nodes->control.node.size() < num_nodes) {
      PS_VLOG(1) << "add node";
      nodes->control.node.push_back(ctrl.node[0]);
    } else {
      // TODO: some node dies and restarts
    }
  }
  // update my id
  for (size_t i = 0; i < ctrl.node.size(); ++i) {
    const auto& node = ctrl.node[i];
    if (my_node_global_.hostname == node.hostname && my_node_global_.port == node.port) {
      my_node_global_ = node;
      std::string rank = std::to_string(Postoffice::IDtoRank(node.id));
#ifdef _MSC_VER
      _putenv_s("DMLC_GLOBAL_RANK", rank.c_str());
#else
      setenv("DMLC_GLOBAL_RANK", rank.c_str(), true);
#endif
    }
  }
}

void Van::ProcessHeartbeat(Message* msg) {
  auto& ctrl = msg->meta.control;
  time_t t = time(NULL);
  for (auto &node : ctrl.node) {
    Postoffice::Get()->UpdateHeartbeat(node.id, t);
    if (is_scheduler_) {
      Message heartbeat_ack;
      heartbeat_ack.meta.recver = node.id;
      heartbeat_ack.meta.control.cmd = Control::HEARTBEAT;
      heartbeat_ack.meta.control.node.push_back(my_node_);
      heartbeat_ack.meta.timestamp = timestamp_++;
      // send back heartbeat
      Send(heartbeat_ack);
    }
  }
}

void Van::ProcessBarrierCommand(Message* msg, bool is_global) {
  auto& ctrl = msg->meta.control;
  if (msg->meta.request) {
    if (barrier_count_.empty()) {
      barrier_count_.resize(8, 0);
    }
    int group = ctrl.barrier_group;
    ++barrier_count_[group];
    PS_VLOG(1) << "Barrier count for " << group << " : " << barrier_count_[group];
    if (barrier_count_[group] == static_cast<int>(
            Postoffice::Get()->GetNodeIDs(group, is_global).size())) {
      barrier_count_[group] = 0;
      Message res;
      res.meta.request = false;
      res.meta.app_id = msg->meta.app_id;
      res.meta.customer_id = msg->meta.customer_id;
      res.meta.control.cmd = is_global ? Control::BARRIER_GLOBAL : Control::BARRIER;
      for (int r : Postoffice::Get()->GetNodeIDs(group, is_global)) {
        int recver_id = r;
        if (shared_node_mapping_.find(r) == shared_node_mapping_.end()) {
          res.meta.recver = recver_id;
          res.meta.timestamp = timestamp_++;
          CHECK_GT(Send(res, is_global), 0);
        }
      }
    }
  } else {
    Postoffice::Get()->Manage(*msg, is_global);
  }
}

void Van::MergeMsg(Message* msg1, Message* msg2) {
  std::lock_guard<std::mutex> lk(merge_mu_);
  float *p1 = (float*)msg1->data[1].data();
  float *p2 = (float*)msg2->data[1].data();
  int nlen1 = msg1->data[1].size() / sizeof(float);
  int nlen2 = msg2->data[1].size() / sizeof(float);
  assert(nlen1 == nlen2);
  float* merged = (float*)malloc(nlen1 * sizeof(float));
  float* n = (float*)malloc(nlen1 * sizeof(float));
  memcpy(merged, p1, nlen1 * sizeof(float));
  memcpy(n, p2, nlen1 * sizeof(float));
  for (int i = 0; i < nlen1; i++) {
    merged[i] += n[i];
  }
  msg1->data[1].reset((char*)merged, nlen1 * sizeof(float), [merged](char* buf) {
    free(merged);
  });
  free(n);
}

void Van::MergeMsg_HALF(Message* msg1, Message* msg2) {
  std::lock_guard<std::mutex> lk(merge_mu_);
  half *p1 = (half*)msg1->data[1].data();
  half *p2 = (half*)msg2->data[1].data();
  int nlen1 = msg1->data[1].size() / 16;
  int nlen2 = msg2->data[1].size() / 16;
  assert(nlen1 == nlen2);
  half* merged = (half*)malloc(nlen1 * 16);
  half* n = (half*)malloc(nlen1 * sizeof(half));
  memcpy(merged, p1, nlen1 * sizeof(half));
  memcpy(n, p2, nlen1 * sizeof(half));
  for (int i = 0; i < nlen1; i++) {
    merged[i] += n[i];
  }
  msg1->data[1].reset((char *)merged, nlen1 * sizeof(half), [merged](char* buf) {
    free(merged);
  });
  free(n);
}

void Van::ProcessDataMsg(Message* msg) {
  // data msg
  CHECK_NE(msg->meta.sender, Meta::kEmpty);
  CHECK_NE(msg->meta.recver, Meta::kEmpty);
  CHECK_NE(msg->meta.app_id, Meta::kEmpty);
  int app_id = msg->meta.app_id;
  int customer_id = Postoffice::Get()->is_worker() ? msg->meta.customer_id : app_id;
  auto* obj = Postoffice::Get()->GetCustomer(app_id, customer_id, 5);
  CHECK(obj) << "timeout (5 sec) to wait App " << app_id << " customer " << customer_id \
    << " ready at " << my_node_.role;

  if (enable_dgt && DepairDataHandleType(msg->meta.head).requestType == RequestType::kDefaultPushPull && \
    my_node_global_.role == 3 && msg->meta.msg_type == 1) { // If I am global_server, and recv push msg
    if (enable_dgt == 3) decode(*msg);
    if (msg_map[msg->meta.sender][msg->meta.first_key].find(msg->meta.seq) \
      == msg_map[msg->meta.sender][msg->meta.first_key].end()) {
      msg_map[msg->meta.sender][msg->meta.first_key][msg->meta.seq] = *msg;
    } else {
      if (DepairDataHandleType(msg->meta.head).dtype == 0) { // kFloat32
        MergeMsg(&msg_map[msg->meta.sender][msg->meta.first_key][msg->meta.seq], msg);
      } else if (DepairDataHandleType(msg->meta.head).dtype == 2) { // kFloat16
        MergeMsg_HALF(&msg_map[msg->meta.sender][msg->meta.first_key][msg->meta.seq], msg);
      }
    }

    if (msg->meta.seq == msg->meta.seq_end) {
      char* buf = (char *)malloc(msg->meta.total_bytes);
      memset(buf, 0, msg->meta.total_bytes);
      for (auto &m : msg_map[msg->meta.sender][msg->meta.first_key]) {
        memcpy(buf + m.second.meta.val_bytes, m.second.data[1].data(), m.second.data[1].size());
      }
      msg_map[msg->meta.sender][msg->meta.first_key].clear();
      msg->data[1].reset(buf, msg->meta.total_bytes, [buf](char* p) {
        free(buf);
      });
      obj->Accept(*msg);
    }
  } else {
    obj->Accept(*msg);
  }
}

void Van::ProcessAddNodeCommand(Message* msg, Meta* nodes, Meta* recovery_nodes) {
  auto dead_nodes = Postoffice::Get()->GetDeadNodes(heartbeat_timeout_);
  std::unordered_set<int> dead_set(dead_nodes.begin(), dead_nodes.end());
  auto& ctrl = msg->meta.control;

  UpdateLocalID(msg, &dead_set, nodes, recovery_nodes);

  if (is_scheduler_) {
    ProcessAddNodeCommandAtScheduler(msg, nodes, recovery_nodes);
  } else {
    for (const auto& node : ctrl.node) {
      std::string addr_str = node.hostname + ":" + std::to_string(node.port);
      if (connected_nodes_.find(addr_str) == connected_nodes_.end()) {
        Connect(node);
        connected_nodes_[addr_str] = node.id;
      }
      if (!node.is_recovery && node.role == Node::SERVER) ++num_servers_;
      if (!node.is_recovery && node.role == Node::WORKER) ++num_workers_;
    }
    PS_VLOG(1) << my_node_.ShortDebugString() << " is connected to others";
    ready_ = true;
  }
}

void Van::ProcessAddGlobalNodeCommand(Message* msg, Meta* nodes) {
  UpdateServerID(msg, nodes);
  if (is_global_scheduler_) {
    ProcessAddGlobalNodeCommandAtScheduler(msg, nodes);
  } else {
    auto& ctrl = msg->meta.control;
    for (const auto& node : ctrl.node) {
      std::string addr_str = node.hostname + ":" + std::to_string(node.port);
      if (connected_nodes_.find(addr_str) == connected_nodes_.end()) {
        Connect(node, true);
        PS_VLOG(1) << "Connected to" << node.DebugString();
        if (node.role != Node::Role::GLOBAL_SCHEDULER) {
          PS_VLOG(1) << my_node_global_.DebugString()<< "ready to connect " << node.DebugString();
          Connect_UDP(node);
        }
        connected_nodes_[addr_str] = node.id;
      }
      if (node.role == Node::GLOBAL_SERVER) ++num_global_servers_;
      if (node.role == Node::SERVER)        ++num_global_workers_;
    }
    PS_VLOG(1) << my_node_global_.ShortDebugString() << " is connected to others";
    ready_global_ = true;
  }
}

void Van::Start(int customer_id) {
  if (!Postoffice::Get()->is_global_scheduler()) {
    // get scheduler info
    start_mu_.lock();

    if (init_stage == 0) {
      scheduler_.hostname = std::string(CHECK_NOTNULL(Environment::Get()->find("DMLC_PS_ROOT_URI")));
      scheduler_.port = atoi(CHECK_NOTNULL(Environment::Get()->find("DMLC_PS_ROOT_PORT")));
      scheduler_.role = Node::SCHEDULER;
      scheduler_.id = kScheduler;
      is_scheduler_ = Postoffice::Get()->is_scheduler();

      // get my node info
      if (is_scheduler_) {
        my_node_ = scheduler_;
        if (getenv("MAX_GREED_RATE_TS") == nullptr) {
          #ifdef _MSC_VER
            _putenv_s("MAX_GREED_RATE_TS", "0.9");
          #else
            setenv("MAX_GREED_RATE_TS", "0.9", true);
          #endif
        }
        max_greed_rate = atof(Environment::Get()->find("MAX_GREED_RATE_TS"));
        int num_servers = Postoffice::Get()->num_servers();
        int num_workers = Postoffice::Get()->num_workers();
        int num_max = num_servers > num_workers ? num_servers : num_workers;
        int num_node_id = 2 * num_max;
        std::vector<int> temp(num_node_id, -1);
        for (int i = 0; i < num_node_id; i++) {
          A.push_back(temp);
          lifetime.push_back(temp);
        }
        for (int i = 0; i < num_node_id; i++) {
          B.push_back(0);
          B1.push_back(0);
        }
        ask_q.push(0);
      } else {
        auto role = is_scheduler_ ? Node::SCHEDULER :
          (Postoffice::Get()->is_worker() ? Node::WORKER : Node::SERVER);
        const char *nhost = Environment::Get()->find("DMLC_NODE_HOST");
        std::string ip;
        if (nhost) ip = std::string(nhost);
        if (ip.empty()) {
          const char *itf = Environment::Get()->find("DMLC_INTERFACE");
          std::string interface;
          if (itf) interface = std::string(itf);
          if (interface.size()) {
            GetIP(interface, &ip);
          } else {
            GetAvailableInterfaceAndIP(&interface, &ip);
          }
          CHECK(!interface.empty()) << "failed to get the interface";
        }
        int port = GetAvailablePort();
        const char *pstr = Environment::Get()->find("PORT");
        if (pstr) port = atoi(pstr);
        CHECK(!ip.empty()) << "failed to get ip";
        CHECK(port) << "failed to get a port";
        my_node_.hostname = ip;
        my_node_.role = role;
        my_node_.port = port;
        // cannot determine my id now, the scheduler will assign it later
        // set it explicitly to make re-register within a same process possible
        my_node_.id = Node::kEmpty;
        my_node_.customer_id = customer_id;
      }

      // bind
      my_node_.port = Bind(my_node_, is_scheduler_ ? 0 : 40);
      PS_VLOG(1) << "Bind to " << my_node_.DebugString();
      CHECK_NE(my_node_.port, -1) << "bind failed";

      // connect to the scheduler
      Connect(scheduler_);

      // for debug use
      if (Environment::Get()->find("PS_DROP_MSG")) {
        drop_rate_ = atoi(Environment::Get()->find("PS_DROP_MSG"));
      }
      // start receiver
      receiver_thread_ = std::unique_ptr<std::thread>(new std::thread(&Van::Receiving, this));
      init_stage++;
    }
    start_mu_.unlock();

    if (!is_scheduler_) {
      // let the scheduler know myself
      Message msg;
      Node customer_specific_node = my_node_;
      customer_specific_node.customer_id = customer_id;
      msg.meta.recver = kScheduler;
      msg.meta.control.cmd = Control::ADD_NODE;
      msg.meta.control.node.push_back(customer_specific_node);
      msg.meta.timestamp = timestamp_++;
      Send(msg);
    }

    // wait until ready
    while (!ready_.load()) {
      std::this_thread::sleep_for (std::chrono::milliseconds(100));
    }

    start_mu_.lock();
    if (init_stage == 1) {
      // resender
      if (Environment::Get()->find("PS_RESEND") && atoi(Environment::Get()->find("PS_RESEND")) != 0) {
        int timeout = 1000;
        if (Environment::Get()->find("PS_RESEND_TIMEOUT")) {
          timeout = atoi(Environment::Get()->find("PS_RESEND_TIMEOUT"));
        }
        resender_ = new Resender(timeout, 10, this);
      }

      if (!is_scheduler_) {
        // start heartbeat thread
        heartbeat_thread_ = std::unique_ptr<std::thread>(new std::thread(&Van::Heartbeat, this));
        // start sender thread
        if (getenv("ENABLE_P3") == nullptr) {
          #ifdef _MSC_VER
            _putenv_s("ENABLE_P3", "0");
          #else
            setenv("ENABLE_P3", "0", true);
          #endif
        }
        enable_p3 = atoi(Environment::Get()->find("ENABLE_P3"));
        if (enable_p3) {
          sender_thread_ = std::unique_ptr<std::thread>(new std::thread(&Van::Sending, this));
        }
      }
      init_stage++;
    }
    start_mu_.unlock();
  }
}

void Van::StartGlobal(int customer_id) {
  if (Postoffice::Get()->is_server() || Postoffice::Get()->is_global_scheduler()) {
    start_mu_.lock();

    global_scheduler_.role = Node::GLOBAL_SCHEDULER;
    global_scheduler_.hostname = std::string(CHECK_NOTNULL(Environment::Get()->find("DMLC_PS_GLOBAL_ROOT_URI")));
    global_scheduler_.port = atoi(CHECK_NOTNULL(Environment::Get()->find("DMLC_PS_GLOBAL_ROOT_PORT")));
    global_scheduler_.id = kSchedulerGlobal;

    is_global_scheduler_ = Postoffice::Get()->is_global_scheduler();
    if (is_global_scheduler_) {
      my_node_global_ = global_scheduler_;
      if (getenv("MAX_GREED_RATE_TS") == nullptr) {
        #ifdef _MSC_VER
          _putenv_s("MAX_GREED_RATE_TS", "0.9");
        #else
          setenv("MAX_GREED_RATE_TS", "0.9", true);
        #endif
      }
      max_greed_rate= atof(Environment::Get()->find("MAX_GREED_RATE_TS"));
      int num_servers= Postoffice::Get()->num_global_servers();
      int num_workers= Postoffice::Get()->num_global_workers();
      int num_max = num_servers > num_workers ? num_servers : num_workers;
      int num_node_id = 2 * num_max + 8;
      std::vector<int> temp(num_node_id, -1);
      for (int i = 0; i < num_node_id; i++) {
        A.push_back(temp);
        lifetime.push_back(temp);
      }
      for (int i = 0; i < num_node_id; i++) {
        B.push_back(0);
        B1.push_back(0);
      }
      ask_q.push(8);
    } else {
      auto role = Postoffice::Get()->is_global_server() ? Node::GLOBAL_SERVER : Node::SERVER;
      std::string ip(my_node_.hostname);
      int port = GetAvailablePort();
      CHECK(!ip.empty()) << "Failed to get ip.";
      CHECK(port) << "Failed to get port.";
      my_node_global_.role = role;
      my_node_global_.hostname = ip;
      my_node_global_.port = port;
      my_node_global_.id = Node::kEmpty;
      my_node_global_.customer_id = customer_id;
    }
    
    // bind
    my_node_global_.port = Bind(my_node_global_, is_global_scheduler_ ? 0 : 40, true);
    PS_VLOG(1) << "Bind to " << my_node_global_.DebugString();
    CHECK_NE(my_node_global_.port, -1) << "bind failed";

    // start receiver
    receiver_global_thread_ = std::unique_ptr<std::thread>(new std::thread(&Van::ReceivingGlobal, this));

    if (!is_global_scheduler_) {
      if (getenv("ENABLE_DGT") == nullptr) {
        #ifdef _MSC_VER
          _putenv_s("ENABLE_DGT", "0");
        #else
          setenv("ENABLE_DGT", "0", true);
        #endif
      }
      enable_dgt = atoi(Environment::Get()->find("ENABLE_DGT"));
      if (enable_dgt) {
        if (getenv("DMLC_UDP_CHANNEL_NUM") == nullptr) {
          #ifdef _MSC_VER
            _putenv_s("DMLC_UDP_CHANNEL_NUM", "3");
          #else
            setenv("DMLC_UDP_CHANNEL_NUM", "3", true);
          #endif
        }
        int udp_ch_num = atoi(Environment::Get()->find("DMLC_UDP_CHANNEL_NUM"));
        for (int i = 0; i < udp_ch_num; ++i) {
          int p = GetAvailablePort();
          my_node_global_.udp_port.push_back(p);
        }
        my_node_global_.udp_port = Bind_UDP(my_node_global_, is_global_scheduler_ ? 0 : 40);
        PS_VLOG(1) << "(UDP) Bind to " << my_node_global_.DebugString();
        // start udp receiver
        for (size_t i = 0; i < my_node_global_.udp_port.size(); ++i) {
          udp_receiver_thread_[i] = std::unique_ptr<std::thread>(
            new std::thread(&Van::Receiving_UDP, this, i));
        }
        //start dgt send scheduler
        important_scheduler_thread_ = std::unique_ptr<std::thread>(
          new std::thread(&Van::Important_scheduler, this));
        unimportant_scheduler_thread_ = std::unique_ptr<std::thread>(
          new std::thread(&Van::Unimportant_scheduler, this));
      }
    }
            
    // connect to the global scheduler
    Connect(global_scheduler_, true);
    start_mu_.unlock();

    if (!is_global_scheduler_) {
      // let the global scheduler know myself
      Message msg;
      Node customer_specific_node = my_node_global_;
      customer_specific_node.customer_id = customer_id;
      msg.meta.recver = kSchedulerGlobal;
      msg.meta.control.cmd = Control::ADD_GLOBAL_NODE;
      msg.meta.control.node.push_back(customer_specific_node);
      msg.meta.timestamp = timestamp_++;
      int send_bytes = Send(msg, true);
      CHECK_NE(send_bytes, -1);
    }

    // wait until ready
    while (!ready_global_.load()) {
      std::this_thread::sleep_for (std::chrono::milliseconds(100));
    }
  }
}

void Van::Stop(bool is_global) {
  auto& my_node = is_global ? my_node_global_ : my_node_;
  Message exit;
  exit.meta.control.cmd = Control::TERMINATE;
  exit.meta.customer_id = 0;
  exit.meta.recver = my_node.id;
  int ret = SendMsg(exit, is_global);
  CHECK_NE(ret, -1);
  if (is_global) {
    if (Postoffice::Get()->is_server()) {
      exit.meta.recver = my_node_.id;
      int ret = SendMsg(exit, false);
      CHECK_NE(ret, -1);
      receiver_thread_->join();
    }
    receiver_global_thread_->join();
    for (size_t i = 0; i < udp_receiver_thread_vec.size(); ++i) {
        udp_receiver_thread_[i]->join();
    }
    if (!is_scheduler_) {
        important_scheduler_thread_->join();
        unimportant_scheduler_thread_->join();
    }
    if (!is_global_scheduler_) heartbeat_thread_->join();
  } else {
    receiver_thread_->join();
    if (!is_scheduler_) heartbeat_thread_->join();
    if (!is_scheduler_ && enable_p3) sender_thread_->join();
  }
  if (resender_) delete resender_;
  ready_ = false;
  if (is_global) ready_global_ = false;
}

void Van::AssignMsg(Message& msg, int channel, int tag) {
  if (channel == 0) important_queue_.Push(msg);
  else unimportant_queue_.Push(msg);
}

void Van::Important_scheduler() {
  while (true) {
    Message msg;
    important_queue_.WaitAndPop(&msg);
    Important_send(msg);
  }
}

void Van::Unimportant_scheduler() {
  while (true) {
    if (important_queue_.empty()) {
      Message msg;
      unimportant_queue_.WaitAndPop(&msg);
      Unimportant_send(msg);
    }
  }
}

int Van::Important_send(Message& msg) {
  int send_bytes = SendMsg(msg, true);
  CHECK_NE(send_bytes, -1);
  return send_bytes;
}

int Van::Unimportant_send(Message& msg) {
  int send_bytes = 0;
  if (enable_dgt == 1) {
    send_bytes = SendMsg_UDP(msg.meta.channel - 1, msg, 0);
  } else if (enable_dgt == 2) {
    send_bytes = SendMsg(msg, true); // for tcp-dgt
  } else if (enable_dgt == 3) {
    encode(msg, 4);
    send_bytes = SendMsg(msg, true); // for tcp-dgt and encode
  }
  CHECK_NE(send_bytes, -1);
  return send_bytes;
}

void Van::encode(Message& msg, int bits_num) {
  std::lock_guard<std::mutex> lk(encode_mu_);
  SArray<char>& s_val = msg.data[1];
  float *ps = (float*)s_val.data();
  int d_val_size = 0;

  if (s_val.size() % (msg.meta.bits_num / bits_num) == 0) {
      d_val_size = s_val.size() / (msg.meta.bits_num / bits_num);
  } else {
      d_val_size = s_val.size() / (msg.meta.bits_num / bits_num) + 1;
  }
  SArray<char> d_val(d_val_size);
  char *pd = d_val.data();
  auto it = residual[msg.meta.first_key].find(msg.meta.seq);
  if (it == residual[msg.meta.first_key].end()) {
    residual[msg.meta.first_key][msg.meta.seq] = SArray<char>(s_val.size());
  }
  float *pr = (float*)residual[msg.meta.first_key][msg.meta.seq].data();

  int param_n = s_val.size() / sizeof(float);
  float max_v = 0.0, min_v = 0.0;
  for (int i = 0; i < param_n; ++i) {
    *(pr+i) += *(ps+i);
  }
  for (int i = 0; i < param_n; ++i) {
    max_v = std::max(max_v, *(pr + i));
    min_v = std::min(min_v, *(pr + i));
  }
  for (int i = 0; i < pow(2, bits_num); ++i) {
    float zv = ((min_v + i * (max_v - min_v) / pow(2, bits_num)) \
      + (min_v + (i + 1) * (max_v - min_v) / pow(2, bits_num))) / 2;
    msg.meta.compr.push_back(zv);
  }
  char qj = 0;
  for (int i = 0; i < param_n; ++i) {
    qj = (*(pr + i) - min_v) / ((max_v - min_v) / pow(2, bits_num));
    *pd |= (qj << (((8 / bits_num) - 1 - i % (8 / bits_num)) * bits_num));
    if (i % (8 / bits_num) == 0) pd += 1;
    *(pr+i) -= msg.meta.compr[qj];
  }
  msg.meta.bits_num = bits_num;
  msg.data[1] = d_val;
}

void Van::decode(Message& msg) {
  if (DepairDataHandleType(msg.meta.head).dtype == 0
    && msg.meta.bits_num == 32) { // kFloat32
    return;
  } else if (DepairDataHandleType(msg.meta.head).dtype == 2
    && msg.meta.bits_num == 16) { // kFloat16
    return;
  }

  SArray<char>& s_val = msg.data[1];
  char *ps = (char*)s_val.data();
  int d_val_size = msg.meta.vals_len;
  SArray<char> d_val(d_val_size);
  float *pd = (float*)d_val.data();
  int nlen = s_val.size();
  char qj = 0;
  int param_n = d_val_size / sizeof(float);
  int bits_num  = msg.meta.bits_num;
  assert(pow(2, bits_num) == msg.meta.compr.size());
  char mask = pow(2, bits_num) - 1;
  for (int i = 0; i < nlen; ++i) {
    for (int j = 0; j < 8 / bits_num; ++j) {
      qj = (*(ps + i) >> (8 - bits_num - bits_num * j)) & mask;
      *pd = msg.meta.compr[qj];
      pd += 1;
      param_n -= 1;
      if (param_n == 0) break;
    }
  }
  msg.data[1] = d_val;
}

int Van::DGT_Send(const Message& msg, int channel, int tag) {
  int send_bytes;
  send_bytes = SendMsg_UDP(channel - 1, msg, tag);
  CHECK_NE(send_bytes, -1);
  send_bytes_ += send_bytes;
  if (resender_) resender_->AddOutgoing(msg);
  return send_bytes;
}
                
int Van::Send(const Message& msg, bool is_global) {
  int send_bytes;
  send_bytes = SendMsg(msg, is_global);
  CHECK_NE(send_bytes, -1);
  send_bytes_ += send_bytes;
  if (resender_) resender_->AddOutgoing(msg);
  if (Postoffice::Get()->verbose() >= 2) {
    PS_VLOG(2) << "[SEND] " << msg.DebugString();
  }
  return send_bytes;
}

void Van::PushToSenderQueue(const Message& msg) {
  send_queue_.Push(msg);
}

void Van::Sending() {
  while (true) {
    Message msg;
    send_queue_.WaitAndPop(&msg);
    Send(msg);
    if (!msg.meta.control.empty() && msg.meta.control.cmd == Control::TERMINATE) {
      break;
    }
  }
}

void Van::Receiving() {
  Meta nodes;
  Meta recovery_nodes;  // store recovery nodes
  recovery_nodes.control.cmd = Control::ADD_NODE;

  while (true) {
    Message msg;
    int recv_bytes = RecvMsg(&msg);
    // For debug, drop received message
    if (ready_.load() && drop_rate_ > 0) {
      unsigned seed = time(NULL) + my_node_.id;
      if (rand_r(&seed) % 100 < drop_rate_) {
        LOG(WARNING) << "Drop message " << msg.DebugString();
        continue;
      }
    }
    CHECK_NE(recv_bytes, -1);
    recv_bytes_ += recv_bytes;
    if (Postoffice::Get()->verbose() >= 2) {
      PS_VLOG(2) << "[RECV][LOCAL] " << msg.DebugString();
    }

    // duplicated message
    if (resender_ && resender_->AddIncomming(msg)) continue;

    if (!msg.meta.control.empty()) {
      // control msg
      auto& ctrl = msg.meta.control;
      if (ctrl.cmd == Control::TERMINATE) {
        ProcessTerminateCommand();
        break;
      } else if (ctrl.cmd == Control::ADD_NODE) {
        ProcessAddNodeCommand(&msg, &nodes, &recovery_nodes);
      } else if (ctrl.cmd == Control::BARRIER) {
        ProcessBarrierCommand(&msg);
      } else if (ctrl.cmd == Control::HEARTBEAT) {
        ProcessHeartbeat(&msg);
      } else if (ctrl.cmd == Control::AUTOPULLREPLY) {
        ProcessAutoPullReply();
      } else if (ctrl.cmd == Control::ASKPULL) {
        ProcessAskPullCommand(&msg);
      } else if (ctrl.cmd == Control::ASKPUSH) {
        ProcessAskPushCommand(&msg);
      } else if (ctrl.cmd == Control::REPLY) {
        ProcessReplyCommand(&msg);
      } else {
        LOG(WARNING) << "Drop unknown typed message " << msg.DebugString();
      }
    } else {
      ProcessDataMsg(&msg);
    }
  }
}

void Van::ReceivingGlobal() {
  Meta nodes;

  while (true) {
    Message msg;
    int recv_bytes = RecvMsg(&msg, true);
    CHECK_NE(recv_bytes, -1);
    recv_bytes_ += recv_bytes;
    if (Postoffice::Get()->verbose() >= 2) {
      PS_VLOG(2) << "[RECV][GLOBAL] " << msg.DebugString();
    }

    // duplicated message
    if (resender_ && resender_->AddIncomming(msg)) continue;
    if (!msg.meta.control.empty()) {
      // control msg
      auto& ctrl = msg.meta.control;
      if (ctrl.cmd == Control::TERMINATE) {
        ProcessTerminateCommand(true);
        break;
      } else if (ctrl.cmd == Control::ADD_GLOBAL_NODE) {
        ProcessAddGlobalNodeCommand(&msg, &nodes);
      } else if (ctrl.cmd == Control::BARRIER_GLOBAL) {
        ProcessBarrierCommand(&msg, true);
      } else if (ctrl.cmd == Control::HEARTBEAT) {
        // TODO: perform heartbeat
      } else if (ctrl.cmd == Control::AUTOPULLREPLY) {
        ProcessAutoPullReplyGlobal();
      } else if (ctrl.cmd == Control::ASKPULL) {
        ProcessAskPullGlobalCommand(&msg);
      } else if (ctrl.cmd == Control::ASKPUSH) {
        ProcessAskPushGlobalCommand(&msg);
      } else if (ctrl.cmd == Control::REPLY) {
        ProcessReplyGlobalCommand(&msg);
      } else {
        LOG(WARNING) << "Drop unknown typed message " << msg.DebugString();
      }
    } else {
      ProcessDataMsg(&msg);
    }
  }
}

void Van::Receiving_UDP(int channel) {
  Meta nodes;
  Meta recovery_nodes;  // store recovery nodes
  recovery_nodes.control.cmd = Control::ADD_NODE;
  PS_VLOG(1) << "Start thread {Receiving_UDP} in UDP channel [" << channel + 1 << "]";
  
  while (true) {
    Message msg;
    int recv_bytes = RecvMsg_UDP(channel, &msg);
    // For debug, drop received message
    if (ready_.load() && drop_rate_ > 0) {
      unsigned seed = time(NULL) + my_node_.id;
      if (rand_r(&seed) % 100 < drop_rate_) {
        LOG(WARNING) << "Drop message " << msg.DebugString();
        continue;
      }
    }
    CHECK_NE(recv_bytes, -1);
    recv_bytes_ += recv_bytes;
    if (Postoffice::Get()->verbose() >= 2) {
      PS_VLOG(2) << msg.DebugString();
    }
	if (!msg.meta.control.empty()) {
      // control msg
      auto& ctrl = msg.meta.control;
      if (ctrl.cmd == Control::TERMINATE) {
        ProcessTerminateCommand();
        break;
      } else if (ctrl.cmd == Control::ADD_NODE) {
        ProcessAddNodeCommand(&msg, &nodes, &recovery_nodes);
      } else if (ctrl.cmd == Control::BARRIER) {
        ProcessBarrierCommand(&msg);
      } else if (ctrl.cmd == Control::HEARTBEAT) {
        ProcessHeartbeat(&msg);
	  } else {
        LOG(WARNING) << "Drop unknown typed message " << msg.DebugString();
      }
    } else {
      ProcessDataMsg(&msg);
    }
  }
}

void Van::PackMeta(const Meta& meta, char** meta_buf, int* buf_size, bool is_global) {
  // convert into protobuf
  PBMeta pb;
  pb.set_head(meta.head);
  if (meta.app_id != Meta::kEmpty) pb.set_app_id(meta.app_id);
  if (meta.timestamp != Meta::kEmpty) pb.set_timestamp(meta.timestamp);
  if (meta.version != Meta::kEmpty) pb.set_version(meta.version);
  if (meta.key != Meta::kEmpty) pb.set_key(meta.key);
  if (meta.iters != Meta::kEmpty) pb.set_iters(meta.iters);
  if (meta.body.size()) pb.set_body(meta.body);
  if (is_global) pb.set_sender(my_node_global_.id);
  else pb.set_sender(my_node_.id);
  pb.set_recver(meta.recver);
  pb.set_bits_num(meta.bits_num);
  pb.set_first_key(meta.first_key);
  pb.set_seq_begin(meta.seq_begin);
  pb.set_seq_end(meta.seq_end);
  pb.set_seq(meta.seq);
  pb.set_channel(meta.channel);
  pb.set_msg_type(meta.msg_type);
  pb.set_push_op(meta.push_op_num);
  pb.set_val_bytes(meta.val_bytes);
  pb.set_total_bytes(meta.total_bytes);
  pb.set_keys_len(meta.keys_len);
  pb.set_vals_len(meta.vals_len);
  pb.set_lens_len(meta.lens_len);
  for (auto v : meta.compr) pb.add_compr(v);
  pb.set_push(meta.push);
  pb.set_request(meta.request);
  pb.set_simple_app(meta.simple_app);
  pb.set_customer_id(meta.customer_id);
  pb.set_priority(meta.priority);
  for (auto d : meta.data_type) pb.add_data_type(d);
  if (!meta.control.empty()) {
    auto ctrl = pb.mutable_control();
    ctrl->set_cmd(meta.control.cmd);
    if (meta.control.cmd == Control::BARRIER ||
        meta.control.cmd == Control::BARRIER_GLOBAL) {
      ctrl->set_barrier_group(meta.control.barrier_group);
    } else if (meta.control.cmd == Control::ACK) {
      ctrl->set_msg_sig(meta.control.msg_sig);
    }
    for (const auto& n : meta.control.node) {
      auto p = ctrl->add_node();
      p->set_id(n.id);
      p->set_role(n.role);
      p->set_port(n.port);
      for (auto up : n.udp_port) p->add_udp_port(up);
      p->set_hostname(n.hostname);
      p->set_is_recovery(n.is_recovery);
      p->set_customer_id(n.customer_id);
    }
  }

  // to string
  *buf_size = pb.ByteSize();
  *meta_buf = new char[*buf_size + 1];
  CHECK(pb.SerializeToArray(*meta_buf, *buf_size))
    << "failed to serialize protbuf";
}

void Van::UnpackMeta(const char* meta_buf, int buf_size, Meta* meta) {
  // to protobuf
  PBMeta pb;
  CHECK(pb.ParseFromArray(meta_buf, buf_size))
    << "failed to parse string into protobuf";

  meta->key = pb.has_key() ? pb.key() : Meta::kEmpty;
  meta->version = pb.has_version() ? pb.version() : Meta::kEmpty;
  meta->iters = pb.has_iters() ? pb.iters() : Meta::kEmpty;
  // to meta
  meta->head = pb.head();
  meta->app_id = pb.has_app_id() ? pb.app_id() : Meta::kEmpty;
  meta->timestamp = pb.has_timestamp() ? pb.timestamp() : Meta::kEmpty;
  meta->sender = pb.sender();
  meta->recver = pb.recver();
  meta->bits_num = pb.bits_num();
  meta->first_key = pb.first_key();
  meta->seq = pb.seq();
  meta->seq_begin = pb.seq_begin();
  meta->seq_end = pb.seq_end();
  meta->channel = pb.channel();
  meta->msg_type = pb.msg_type();
  meta->push_op_num = pb.push_op();
  meta->val_bytes = pb.val_bytes();
  meta->total_bytes = pb.total_bytes();
  meta->keys_len = pb.keys_len();
  meta->vals_len = pb.vals_len();
  meta->lens_len = pb.lens_len();
  meta->priority = pb.priority();
  for (int i = 0; i < pb.compr_size(); ++i) {
      meta->compr.push_back(pb.compr(i));
  }
  meta->request = pb.request();
  meta->push = pb.push();
  meta->simple_app = pb.simple_app();
  meta->body = pb.body();
  meta->customer_id = pb.customer_id();
  meta->data_type.resize(pb.data_type_size());
  for (int i = 0; i < pb.data_type_size(); ++i) {
    meta->data_type[i] = static_cast<DataType>(pb.data_type(i));
  }
  if (pb.has_control()) {
    const auto& ctrl = pb.control();
    meta->control.cmd = static_cast<Control::Command>(ctrl.cmd());
    meta->control.barrier_group = ctrl.barrier_group();
    meta->control.msg_sig = ctrl.msg_sig();
    for (int i = 0; i < ctrl.node_size(); ++i) {
      const auto& p = ctrl.node(i);
      Node n;
      n.role = static_cast<Node::Role>(p.role());
      n.port = p.port();
      for (int i = 0; i < p.udp_port_size(); ++i) {
        n.udp_port.push_back(p.udp_port(i));
      }
      n.hostname = p.hostname();
      n.id = p.has_id() ? p.id() : Node::kEmpty;
      n.is_recovery = p.is_recovery();
      n.customer_id = p.customer_id();
      meta->control.node.push_back(n);
    }
  } else {
    meta->control.cmd = Control::EMPTY;
  }
}

void Van::Heartbeat() {
  const char* val = Environment::Get()->find("PS_HEARTBEAT_INTERVAL");
  const int interval = val ? atoi(val) : kDefaultHeartbeatInterval;
  while (interval > 0 && ready_.load()) {
    std::this_thread::sleep_for (std::chrono::seconds(interval));
    Message msg;
    msg.meta.recver = kScheduler;
    msg.meta.control.cmd = Control::HEARTBEAT;
    msg.meta.control.node.push_back(my_node_);
    msg.meta.timestamp = timestamp_++;
    Send(msg);
  }
}

void Van::WaitForFinish() {
  std::unique_lock<std::mutex> ver_lk(ver_mu);
  while (!ver_flag) {
    ver_cond.wait(ver_lk);
  }
  ver_flag = false;
  ver_lk.unlock();
}

void Van::WaitForGlobalFinish() {
  std::unique_lock<std::mutex> ver_global_lk(ver_global_mu);
  while (!ver_global_flag) {
    ver_global_cond.wait(ver_global_lk);
  }
  ver_global_flag = false;
  ver_global_lk.unlock();
}

void Van::ProcessAutoPullReply() {
  std::unique_lock<std::mutex> ver_lk(ver_mu);
  ver_flag = true;
  ver_lk.unlock();
  ver_cond.notify_one();
}

void Van::ProcessAutoPullReplyGlobal() {
  std::unique_lock<std::mutex> ver_global_lk(ver_global_mu);
  ver_global_flag = true;
  ver_global_lk.unlock();
  ver_global_cond.notify_one();
}

void Van::AskForReceiverPull(int throughput, int last_recv_id, int version, bool is_global) {
  Message msg;
  msg.meta.customer_id = last_recv_id;
  msg.meta.app_id      = throughput;
  msg.meta.sender      = (is_global) ? my_node_global_.id : my_node_.id;
  msg.meta.recver      = (is_global) ? kSchedulerGlobal : kScheduler;
  msg.meta.control.cmd = Control::ASKPULL;
  msg.meta.version     = version;
  msg.meta.timestamp   = timestamp_++;
  Send(msg, is_global);
}

void Van::AskForReceiverPush(int app, int customer, int timestamp, bool is_global) {
  Message msg;
  msg.meta.sender = (is_global) ? my_node_global_.id : my_node_.id;
  msg.meta.recver = (is_global) ? kSchedulerGlobal : kScheduler;
  msg.meta.control.cmd = Control::ASKPUSH;
  msg.meta.timestamp = timestamp;
  msg.meta.app_id = app;
  msg.meta.customer_id = customer;
  Send(msg, is_global);
}

void Van::ProcessAskPushCommand(Message* msg) {
  if (ask_q.size() == 1 && ask_q.front() == (msg->meta.sender - 100)) return;

  Message rpl;
  rpl.meta.sender = my_node_.id;
  rpl.meta.app_id = msg->meta.app_id;
  rpl.meta.customer_id = msg->meta.customer_id;
  rpl.meta.timestamp = msg->meta.timestamp;
  rpl.meta.push = true;
  rpl.meta.request = true;
  std::unique_lock<std::mutex> lk_sch1(sched1);
  ask_q.push(msg->meta.sender - 100);

  if (ask_q.size() > 1) {
    int node_a = ask_q.front();
    ask_q.pop();
    int node_b = ask_q.front();
    ask_q.pop();
    if (node_a == 0 || node_b == 0) {
      if (node_a == 0) {
        rpl.meta.iters = node_a + 100;
        rpl.meta.recver = node_b + 100;
        B1[node_b] = 1;
        Send(rpl);
        PS_VLOG(2) << "In local push, " << node_b << " send to server 100";
      } else {
        rpl.meta.iters = node_b + 100;
        rpl.meta.recver = node_a + 100;
        B1[node_a] = 1;
        Send(rpl);
        PS_VLOG(2) << "In local push, " << node_a << " send to server 100";
      }
    } else {
      if (A[node_a][node_b] > A[node_b][node_a]) {
        rpl.meta.iters = node_b + 100;
        rpl.meta.recver = node_a + 100;
        Send(rpl);
        B1[node_a] = 1;
        PS_VLOG(2) << "In local push, sender is " << node_a << " and receiver is " << node_b;
      } else {
        rpl.meta.iters = node_a + 100;
        rpl.meta.recver = node_b + 100;
        Send(rpl);
        B1[node_b] = 1;
        PS_VLOG(2) << "In local push, sender is " << node_b << " and receiver is " << node_a;
      }
    }
  }
  int count = 0;
  for (auto it: B1) count += it;
  if (count == Postoffice::Get()->num_workers()) {
    for (std::size_t i = 0; i < B1.size(); i++) B1[i] = 0;
    PS_VLOG(2) << "Local push over.";
  }
  lk_sch1.unlock();
}

void Van::ProcessAskPushGlobalCommand(Message* msg) {
  if (ask_q.size() == 1 && ask_q.front() == msg->meta.sender) return;

  Message rpl;
  rpl.meta.sender = my_node_global_.id;
  rpl.meta.app_id = msg->meta.app_id;
  rpl.meta.customer_id = msg->meta.customer_id;
  rpl.meta.timestamp = msg->meta.timestamp;
  rpl.meta.push = true;
  rpl.meta.request = true;
  std::unique_lock<std::mutex> lk_sch1(sched1);
  ask_q.push(msg->meta.sender);

  if (ask_q.size() > 1) {
    int node_a = ask_q.front();
    ask_q.pop();
    int node_b = ask_q.front();
    ask_q.pop();
    if (node_a == 8 || node_b == 8) {
      if (node_a == 8) {
        rpl.meta.iters = node_a;
        rpl.meta.recver = node_b;
        B1[node_b] = 1;
        Send(rpl, true);
        PS_VLOG(2) << "In global push, " << node_b << " send to server 8";
      } else {
        rpl.meta.iters = node_b;
        rpl.meta.recver = node_a;
        B1[node_a] = 1;
        Send(rpl, true);
        PS_VLOG(2) << "In global push," << node_a << " send to server 8";
      }
    } else {
      if (A[node_a][node_b] > A[node_b][node_a]) {
        rpl.meta.iters = node_b;
        rpl.meta.recver = node_a;
        Send(rpl, true);
        B1[node_a] = 1;
        PS_VLOG(2) << "In global push, sender is " << node_a << " and receiver is " << node_b;
      } else {
        rpl.meta.iters = node_a;
        rpl.meta.recver = node_b;
        Send(rpl, true);
        B1[node_b] = 1;
        PS_VLOG(2) << "In global push, sender is " << node_b << " and receiver is " << node_a;
      }
    }
  }

  int count = 0;
  for (auto it: B1) count += it;
  if (count == Postoffice::Get()->num_global_workers()) {
      for (std::size_t i = 0; i < B1.size(); i++) B1[i] = 0;
      PS_VLOG(2) << "Global push is over.";
  }
  lk_sch1.unlock();
}

void Van::ProcessAskPullCommand(Message* msg) {
  // update A and B
  std::unique_lock<std::mutex> lks(sched);
  int req_node_id = msg->meta.sender - 100;
  if (msg->meta.app_id != -1) { // not the first ask
    A[req_node_id][msg->meta.customer_id - 100] = msg->meta.app_id;
    lifetime[req_node_id][msg->meta.customer_id - 100] = msg->meta.version;
  }
  // create reply message
  Message reply;
  reply.meta.sender = my_node_.id;
  reply.meta.recver = req_node_id + 100;
  reply.meta.control.cmd = Control::REPLY;
  reply.meta.timestamp = timestamp_++;

  int temp = 0;
  for (std::size_t i = 0; i < B.size(); i++) temp += B[i];

  if (temp == Postoffice::Get()->num_workers()) {
    for (std::size_t i = 0; i < B.size(); i++) B[i] = 0;
    iters++;
  }

  if (msg->meta.version <= iters) {
    reply.meta.customer_id = -1;
  } else {
    int receiver_id = -1;
    int num_know_node = 0, num_unknow_node = 0;
    for (std::size_t i = 0; i < A[req_node_id].size(); i++) {
      if ((i % 2) && B[i] == 0) {
        if (A[req_node_id][i] != -1) num_know_node++;
        else num_unknow_node++;
      }
    }
    unsigned seed;
    seed = time(0);
    srand(seed);
    int rand_number = rand() % 10;
    double greed_rate = double(num_know_node / (num_know_node + num_unknow_node)) < max_greed_rate ?
      double(num_know_node / (num_know_node + num_unknow_node)) : max_greed_rate;
    if ((num_know_node != 0) && (rand_number <= (greed_rate * 10))) { // greedy mode
      int throughput = -1;
      for (std::size_t i = 0; i < A[req_node_id].size(); i++) {
        if (B[i] == 0 && (A[req_node_id][i] > throughput)) {
          receiver_id = i;
          throughput = A[req_node_id][i];
        }
      }
    }
    else { //random mode
      rand_number = (rand() % (num_unknow_node + num_know_node)) + 1;
      int counter = 0;
      for (std::size_t i = 0; i < A[req_node_id].size(); i++) {
        if (B[i] == 0 && (i % 2)) {
          counter++;
          if (counter == rand_number) {
            receiver_id = i;
            break;
          }
        }
      }
    }
    // send reply message
    reply.meta.customer_id = receiver_id;
  }

  if (reply.meta.customer_id != -1) {
    B[reply.meta.customer_id] = 1;
    reply.meta.customer_id += 100;
  }

  PS_VLOG(2) << "In local pull, node " << reply.meta.sender << " send to node " << reply.meta.customer_id;
  lks.unlock();
  Send(reply);
}

void Van::ProcessAskPullGlobalCommand(Message* msg) {
  // update A and B
  std::unique_lock<std::mutex> lks(sched);
  int req_node_id = msg->meta.sender;
  if (msg->meta.app_id != -1) { // not the first ask
    A[req_node_id][msg->meta.customer_id] = msg->meta.app_id;
    lifetime[req_node_id][msg->meta.customer_id] = msg->meta.version;
  }
  // create reply message
  Message reply;
  reply.meta.sender = my_node_global_.id;
  reply.meta.recver = req_node_id;
  reply.meta.control.cmd = Control::REPLY;
  reply.meta.timestamp = timestamp_++;

  int temp = 0;
  for (std::size_t i = 0; i < B.size(); i++) temp += B[i];

  if (temp== Postoffice::Get()->num_global_workers()) {
    for (std::size_t i = 0;i < B.size(); i++) B[i] = 0;
    iters++;
  }
  if (msg->meta.version <= iters) {
    reply.meta.customer_id = -1;
  } else {
    int receiver_id = -1;
    int num_know_node = 0, num_unknow_node = 0;
    for (std::size_t i = 0; i < A[req_node_id].size(); i++) {
      if ((i % 2) && B[i] == 0) {
        if (A[req_node_id][i] != -1) num_know_node++;
        else num_unknow_node++;
      }
    }
    num_unknow_node -= 4;
    unsigned seed;
    seed = time(0);
    srand(seed);
    int rand_number=rand() % 10;
    double greed_rate = double(num_know_node / (num_know_node + num_unknow_node)) < max_greed_rate ?
      double(num_know_node / (num_know_node + num_unknow_node)) : max_greed_rate;
    if ((num_know_node != 0) && (rand_number <= (greed_rate * 10))) { // greedy mode
      int throughput = -1;
      for (std::size_t i = 0; i < A[req_node_id].size(); i++) {
        if (B[i] == 0 && (A[req_node_id][i] > throughput)) {
          receiver_id = i;
          throughput = A[req_node_id][i];
        }
      }
    } else { // random mode
      rand_number = (rand() % (num_unknow_node + num_know_node)) + 5;
      int counter = 0;
      for (std::size_t i = 0; i < A[req_node_id].size(); i++) {
        if (B[i]==0 && (i % 2)) {
          counter++;
          if (counter == rand_number) {
            receiver_id = i;
            break;
          }
        }
      }
    }
    // send reply message
    reply.meta.customer_id = receiver_id;
  }
  if (reply.meta.customer_id != -1) {
    B[reply.meta.customer_id] = 1;
  }
  PS_VLOG(2) << "In global pull, node " << reply.meta.sender << " send to node " << reply.meta.customer_id;
  lks.unlock();
  Send(reply, true);
}

void Van::ProcessReplyCommand(Message* reply) {
  std::unique_lock<std::mutex> ask_lk(ask_mu);
  receiver_ = reply->meta.customer_id;
  ask_lk.unlock();
  ask_cond.notify_one();
}

void Van::ProcessReplyGlobalCommand(Message* reply) {
  std::unique_lock<std::mutex> ask_global_lk(ask_global_mu);
  receiver_global = reply->meta.customer_id;
  ask_global_lk.unlock();
  ask_global_cond.notify_one();
}

int Van::GetReceiver(int throughput, int last_recv_id, int version) {
  // Request a receiver.
  AskForReceiverPull(throughput, last_recv_id, version, false);

  // Lock the mutex and wait until a receiver is set.
  std::unique_lock<std::mutex> ask_lk(ask_mu);
  while (receiver_ == -2) {
    ask_cond.wait(ask_lk);
  }

  int temp = receiver_;
  receiver_ = -2 ;
  ask_lk.unlock();
  return temp;
}

int Van::GetGlobalReceiver(int throughput, int last_recv_id, int version) {
  // Request a receiver.
  AskForReceiverPull(throughput, last_recv_id, version, true);

  // Lock the mutex and wait until a receiver is set.
  std::unique_lock<std::mutex> ask_global_lk(ask_global_mu);
  while (receiver_global == -2) {
    ask_global_cond.wait(ask_global_lk);
  }

  int temp = receiver_global;
  receiver_global = -2;
  ask_global_lk.unlock();
  return temp;
}
}  // namespace ps