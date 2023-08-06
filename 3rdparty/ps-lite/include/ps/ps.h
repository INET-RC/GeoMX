/*!
 *  Copyright (c) 2015 by Contributors
 *  Modifications Copyright (c) 2021 by Contributors at INET-RC
 * @file   ps.h
 * \brief  The parameter server interface
 */
#ifndef PS_PS_H_
#define PS_PS_H_
/** \brief basic setups in ps */
#include "ps/base.h"
/** \brief communicating with a pair of (int, string). */
#include "ps/simple_app.h"
/** \brief communcating with a list of key-value paris. */
#include "ps/kv_app.h"
namespace ps {
/** \brief Returns the number of worker nodes */
inline int NumWorkers() { return Postoffice::Get()->num_workers(); }
/** \brief Returns the number of server nodes */
inline int NumServers() { return Postoffice::Get()->num_servers(); }
/** \brief Returns the number of servers (datacenters) */
inline int NumGlobalWorkers() { return Postoffice::Get()->num_global_workers(); }
/** \brief Returns the number of global servers */
inline int NumGlobalServers() { return Postoffice::Get()->num_global_servers(); }
/** \brief Returns the number of all worker nodes in global */
inline int NumAllWorkers() { return Postoffice::Get()->num_all_workers(); }
/** \brief Returns true if this node is a worker node */
inline bool IsWorker() { return Postoffice::Get()->is_worker(); }
/** \brief Returns true if this node is a server node. */
inline bool IsServer() { return Postoffice::Get()->is_server(); }
/** \brief Returns true if this node is a scheduler node. */
inline bool IsScheduler() { return Postoffice::Get()->is_scheduler(); }
/** \brief Returns true if this node is a master worker node. */
inline bool IsMasterWorker() { return Postoffice::Get()->is_master_worker(); }
/** \brief Returns true if this node is a global server node. */
inline bool IsGlobalServer() { return Postoffice::Get()->is_global_server(); }
/** \brief Returns true if this node is a global scheduler node. */
inline bool isGlobalScheduler() { return Postoffice::Get()->is_global_scheduler(); }
/** \brief Returns true if the workers in central party participate in training. */
inline bool EnableCentralWorkers() { return Postoffice::Get()->enable_central_workers(); }
/** \brief Returns the rank of this node in its group
 *
 * Each worker will have a unique rank within [0, NumWorkers()). So are
 * servers. This function is available only after \ref Start has been called.
 */
inline int MyRank(bool is_global = false) { return Postoffice::Get()->my_rank(is_global); }
/**
 * \brief start the system
 *
 * This function will block until every nodes are started.
 * \param argv0 the program name, used for logging
 */
inline void Start(int customer_id, const char* argv0 = nullptr) {
  Postoffice::Get()->Start(customer_id, argv0, true);
  if (Postoffice::Get()->is_server() ||
      Postoffice::Get()->is_global_scheduler()) {
    Postoffice::Get()->StartGlobal(customer_id, true);
  }
}
/**
 * \brief start the system
 *
 * This function will NOT block.
 * \param argv0 the program name, used for logging
 */
inline void StartAsync(int customer_id, const char* argv0 = nullptr) {
  Postoffice::Get()->Start(customer_id, argv0, false);
  if (Postoffice::Get()->is_server() ||
      Postoffice::Get()->is_global_scheduler()) {
    Postoffice::Get()->StartGlobal(customer_id, false);
  }
}
/**
 * \brief terminate the system
 *
 * All nodes should call this function before existing. 
 * \param do_barrier whether to block until every node is finalized, default true.
 */
inline void Finalize(int customer_id, const bool do_barrier = true) {
  Postoffice::Get()->Finalize(customer_id, do_barrier, false);
  if (Postoffice::Get()->is_server() ||
    Postoffice::Get()->is_global_scheduler()) {
    Postoffice::Get()->Finalize(customer_id, do_barrier, true);
  }
}

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
inline void RegisterExitCallback(const std::function<void()>& cb) {
  Postoffice::Get()->RegisterExitCallback(cb);
}

}  // namespace ps
#endif  // PS_PS_H_
