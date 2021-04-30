/**
 *  Copyright (c) 2015 by Contributors
 *  Modifications Copyright (c) 2021 by Contributors at INET-RC
 */
#ifndef PS_BASE_H_
#define PS_BASE_H_
#include <limits>
#include "ps/internal/utils.h"
namespace ps {

#if USE_KEY32
/*! \brief Use unsigned 32-bit int as the key type */
using Key = uint32_t;
#else
/*! \brief Use unsigned 64-bit int as the key type */
using Key = uint64_t;
#endif
/*! \brief The maximal allowed key value */
static const Key kMaxKey = std::numeric_limits<Key>::max();
/** \brief node ID for the scheduler */
static const int kScheduler = 1;
/**
 * \brief the server node group ID
 *
 * group id can be combined:
 * - kServerGroup + kScheduler means all server nodes and the scheuduler
 * - kServerGroup + kWorkerGroup means all server and worker nodes
 */
static const int kServerGroup = 2;
/** \brief the worker node group ID */
static const int kWorkerGroup = 4;
/** similar but used for global group ID */
static const int kSchedulerGlobal = 1;
static const int kServerGroupGlobal = 2;
static const int kWorkerGroupGlobal = 4;
/** worker id starts from kOffset+1 and server id starts from kOffset,
 *  globally, server id starts from 9 and global server id starts from 8. */
static const int kOffset = 100;
}  // namespace ps
#endif  // PS_BASE_H_
