/**
 *  Copyright (c) 2015 by Contributors
 *  Modifications Copyright (c) 2021 by Contributors at INET-RC
 */
#ifndef PS_INTERNAL_POSTOFFICE_H_
#define PS_INTERNAL_POSTOFFICE_H_
#include <mutex>
#include <algorithm>
#include <vector>
#include "ps/range.h"
#include "ps/internal/env.h"
#include "ps/internal/customer.h"
#include "ps/internal/van.h"
namespace ps {
/**
 * \brief the center of the system
 */
class Postoffice {
 public:
  /**
   * \brief return the singleton object
   */
  static Postoffice* Get() {
    static Postoffice e; return &e;
  }
  /** \brief get the van */
  Van* van() { return van_; }
  /**
   * \brief start the system
   *
   * These function will block until every nodes are started.
   * \param argv0 the program name, used for logging.
   * \param do_barrier whether to block until every nodes are started.
   */
  void Start(int customer_id, const char* argv0, const bool do_barrier);
  void StartGlobal(int customer_id, const bool do_barrier);
  /**
   * \brief terminate the system
   *
   * All nodes should call this function before existing. 
   * \param do_barrier whether to do block until every node is finalized, default true.
   * \param is_global whether the scope is global.
   */
  void Finalize(const int customer_id, const bool do_barrier = true, bool is_global = false);
  /**
   * \brief add an customer to the system. threadsafe
   */
  void AddCustomer(Customer* customer);
  /**
   * \brief remove a customer by given it's id. threasafe
   */
  void RemoveCustomer(Customer* customer);
  /**
   * \brief get the customer by id, threadsafe
   * \param app_id the application id
   * \param customer_id the customer id
   * \param timeout timeout in sec
   * \return return nullptr if doesn't exist and timeout
   */
  Customer* GetCustomer(int app_id, int customer_id, int timeout = 0) const;
  /**
   * \brief get the id of a node (group), threadsafe
   *
   * if it is a node group, return the list of node ids in this
   * group. otherwise, return {node_id}
   */
  const std::vector<int>& GetNodeIDs(int node_id, bool is_global = false) const {
    const auto& node_ids = is_global ? node_global_ids_ : node_ids_;
    const auto it = node_ids.find(node_id);
    CHECK(it != node_ids.cend()) << "node" << node_id << "doesn't exist";
    return it->second;
  }
  /**
   * \brief return the key ranges of all server / global server nodes
   */
  const std::vector<Range>& GetServerKeyRanges(bool is_global = false);
  /**
   * \brief the template of a callback
   */
  using Callback = std::function<void()>;
  /**
   * \brief Register a callback to the system which is called after Finalize()
   *
   * The following codes are equal
   * \code {cpp}
   * RegisterExitCallback(cb);
   * Finalize();
   * \endcode
   *
   * \code {cpp}
   * Finalize();
   * cb();
   * \endcode
   * \param cb the callback function
   */
  void RegisterExitCallback(const Callback& cb) {
    exit_callback_ = cb;
  }
  /**
   * \brief convert from a worker rank into a node id
   * \param rank the worker rank
   * \param is_global whether to use the global id
   */
  static inline int WorkerRankToID(int rank, bool is_global = false) {
    if (is_global) return rank * 2 + 9;
    else return kOffset + rank * 2 + 1;
  }
  /**
   * \brief convert from a server rank into a node id
   * \param rank the server rank
   * \param is_global whether to use the global id
   */
  static inline int ServerRankToID(int rank, bool is_global = false) {
    if (is_global) return rank * 2 + 8;
    else return kOffset + rank * 2;
  }
  /**
   * \brief convert from a node id into a server or worker rank
   * \param id the node id
   */
  static inline int IDtoRank(int id) {
#ifdef _MSC_VER
#undef max
#endif
    if (id < kOffset) return std::max((id - 8) / 2, 0);
    else return std::max((id - kOffset) / 2, 0);
  }
  /** \brief Returns the number of worker nodes within a party */
  int num_workers() const { return num_workers_; }
  /** \brief Returns the number of server nodes within a party */
  int num_servers() const { return num_servers_; }
  /** \brief Returns the number of servers in global */
  int num_global_workers() const { return num_global_workers_; }
  /** \brief Returns the number of global servers */
  int num_global_servers() const { return num_global_servers_; }
  /** \brief Returns the number of all worker nodes in global */
  int num_all_workers() const { return num_all_workers_; }
  /** \brief Returns the rank of this node in its group
   *
   * Each worker will have a unique rank in a group, so are servers.
   * This function is available only after \ref Start or \ref StartGlobal has been called.
   */
  int my_rank(bool is_global = false) const { return IDtoRank(van_->my_node(is_global).id); }
  /** \brief Returns true if this node is a worker node */
  int is_worker() const { return is_worker_; }
  /** \brief Returns true if this node is a server node. */
  int is_server() const { return is_server_; }
  /** \brief Returns true if this node is a scheduler node. */
  int is_scheduler() const { return is_scheduler_; }
  /** \brief Returns true if this node is a master worker node. */
  int is_master_worker() const { return is_master_worker_; }
  /** \brief Returns true if this node is a global scheduler node. */
  int is_global_scheduler() const { return is_global_scheduler_; }
  /** \brief Returns true if this node is a global server node. */
  int is_global_server() const { return is_global_server_; }
  /** \brief Returns true if workers in the central party participate in training. */
  int enable_central_workers() const { return enable_central_workers_; }
  /** \brief Returns the verbose level. */
  int verbose() const { return verbose_; }
  /** \brief Return whether this node is a recovery node */
  bool is_recovery() const { return van_->my_node().is_recovery; }
  /**
   * \brief barrier
   * \param node_id the barrier group id
   * \param is_global whether the scope is global
   */
  void Barrier(int customer_id, int node_group, bool is_global = false);
  /**
   * \brief process a control message, called by van
   * \param the received message
   * \param is_global whether the scope is global
   */
  void Manage(const Message& recv, bool is_global = false);
  /**
   * \brief update the heartbeat record map
   * \param node_id the \ref Node id
   * \param t the last received heartbeat time
   */
  void UpdateHeartbeat(int node_id, time_t t) {
    std::lock_guard<std::mutex> lk(heartbeat_mu_);
    heartbeats_[node_id] = t;
  }
  /**
   * \brief get node ids that haven't reported heartbeats for over t seconds
   * \param t timeout in sec
   */
  std::vector<int> GetDeadNodes(int t = 60);

 private:
  Postoffice();
  ~Postoffice() { delete van_; }

  void InitEnvironment();
  Van* van_;
  mutable std::mutex mu_;
  // app_id -> (customer_id -> customer pointer)
  std::unordered_map<int, std::unordered_map<int, Customer*>> customers_;
  std::unordered_map<int, std::vector<int>> node_ids_;
  std::unordered_map<int, std::vector<int>> node_global_ids_;
  std::mutex server_key_ranges_mu_;
  std::vector<Range> server_key_ranges_;
  std::vector<Range> server_key_ranges_global_;

  bool is_worker_ = false;
  bool is_server_ = false;
  bool is_scheduler_ = false;
  bool is_master_worker_ = false;
  bool is_global_server_ = false;
  bool is_global_scheduler_ = false;
  bool enable_central_workers_ = false;

  int num_servers_ = 0;
  int num_workers_ = 0;
  int num_global_servers_ = 0;
  int num_global_workers_ = 0;
  int num_all_workers_ = 0;
  int verbose_;

  std::unordered_map<int, std::unordered_map<int, bool> > barrier_done_;
  std::unordered_map<int, std::unordered_map<int, bool> > barrier_global_done_;
  std::condition_variable barrier_cond_;
  std::condition_variable barrier_global_cond_;
  std::mutex barrier_mu_;
  std::mutex heartbeat_mu_;
  std::mutex start_mu_;

  int init_stage_ = 0;
  std::unordered_map<int, time_t> heartbeats_;
  Callback exit_callback_;
  /** \brief Holding a shared_ptr to prevent it from being destructed too early */
  std::shared_ptr<Environment> env_ref_;
  time_t start_time_;
  DISALLOW_COPY_AND_ASSIGN(Postoffice);
};

/** \brief verbose log */
#define PS_VLOG(x) LOG_IF(INFO, x <= Postoffice::Get()->verbose())
}  // namespace ps
#endif  // PS_INTERNAL_POSTOFFICE_H_
