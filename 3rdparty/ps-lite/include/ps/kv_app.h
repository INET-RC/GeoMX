/**
 *  Copyright (c) 2015 by Contributors
 *  Modifications Copyright (c) 2021 by Contributors at INET-RC
 */
#ifndef PS_KV_APP_H_
#define PS_KV_APP_H_
#include <algorithm>
#include <utility>
#include <vector>
#include <ctime>
#include <thread>
#include "ps/base.h"
#include "ps/simple_app.h"
namespace ps {

/**
 * \brief the structure for a list of key-value pairs
 *
 * The keys must be unique and sorted in an increasing order.  The length of a
 * value can be more than one. If \a lens is empty, then the length
 * of a value is determined by `k=vals.size()/keys.size()`.  The \a i-th KV pair
 * is then
 *
 * \verbatim {keys[i], (vals[i*k], ..., vals[(i+1)*k-1])} \endverbatim
 *
 * If \a lens is given, then `lens[i]` is the length of the \a i-th
 * value. Let
 *
 * \verbatim n = lens[0] + .. + lens[i-1]  \endverbatim
 *
 * then the \a i-th KV pair is presented as
 *
 * \verbatim {keys[i], (vals[n], ..., vals[lens[i]+n-1])} \endverbatim
 */
template <typename Val>
struct KVPairs {
  // /** \brief empty constructor */
  // KVPairs() {}
  /** \brief the list of keys */
  SArray<Key> keys;
  /** \brief the according values */
  SArray<Val> vals;
  /** \brief the according value lengths (could be empty) */
  SArray<int> lens;
  int priority;
};

/** \brief meta information about a kv request */
struct KVMeta {
  //static const int kEmpty;
  KVMeta():cmd(Meta::kEmpty), push(false), sender(Meta::kEmpty), timestamp(Meta::kEmpty),
    key(Meta::kEmpty), version(0), num_merge(1), app_id(Meta::kEmpty), priority(Meta::kEmpty) {}
  /** \brief the int cmd */
  int cmd;
  /** \brief whether or not this is a push request */
  bool push;
  /** \brief sender's node id */
  int sender;
  /** \brief the associated timestamp */
  int timestamp;
  /** \brief the customer id of worker */
  int customer_id;
  /** \brief unique_key */
  int key;
  /** \brief key version */
  int version;
  int num_merge;
  int app_id;
  int priority;
};

/**
 * \brief A worker node that can \ref Push (\ref Pull) key-value pairs to (from) server
 * nodes
 *
 * \tparam Val the type of value, which should be primitive types such as
 * int32_t and float
 */
template<typename Val>
class KVWorker: public SimpleApp {
 public:
  /** avoid too many this-> */
  using SimpleApp::obj_;
  /**
   * \brief callback function for \ref Push and \ref Pull
   *
   * It is called by the data receiving thread of this instance when the push or
   * pull is actually finished. Namely the kv pairs have already written into
   * servers' data structure or the kv pairs have already pulled back.
   */
  using Callback = std::function<void()>;
  using ReqHandle = std::function<void(const KVMeta& req_meta,
                                       const KVPairs<Val>& req_data,
                                       KVWorker* worker)>;

  void set_request_handle(const ReqHandle& request_handle) {
      CHECK(request_handle) << "invalid request handle";
      request_handle_ = request_handle;
  }

  /**
   * \brief constructor
   *
   * \param app_id the app id, should match with \ref KVServer's id
   * \param customer_id the customer id which is unique locally
   */
  explicit KVWorker(int app_id, int customer_id) : SimpleApp() {
    using namespace std::placeholders;
    slicer_ = std::bind(&KVWorker<Val>::DefaultSlicer, this, _1, _2, _3);

    enable_intra_ts = dmlc::GetEnv("ENABLE_INTRA_TS", 0);
    enable_p3 = dmlc::GetEnv("ENABLE_P3", 0);
    if(enable_intra_ts) {
        obj_ = new Customer(app_id, customer_id, std::bind(&KVWorker<Val>::TS_Process, this, _1), false);
    } else {
        obj_ = new Customer(app_id, customer_id, std::bind(&KVWorker<Val>::Process, this, _1), false);
    }
  }

  /** \brief deconstructor */
  virtual ~KVWorker() { delete obj_; obj_ = nullptr; }

  /**
   * \brief Pushes a list of key-value pairs to all server nodes.
   *
   * This function pushes a KV list specified by \a keys and \a vals to all
   * server nodes.
   *
   * Sample usage: the following codes push two KV pairs `{1, (1.1, 1.2)}` and `{3,
   * (3.1,3.2)}` to server nodes, where the value is a length-2 float vector
   * \code
   *   KVWorker<float> w;
   *   std::vector<Key> keys = {1, 3};
   *   std::vector<float> vals = {1.1, 1.2, 3.1, 3.2};
   *   w.Push(keys, vals);
   * \endcode
   *
   * If \a lens is given, then the value can be various length. See
   * \ref KVPairs for more information.
   *
   * The KV list is partitioned and sent based on the key range each server
   * maintaining. This function returns without waiting the data are sent
   * actually. Instead, use either \ref Wait or the callback to know when
   * finished. This function is thread-safe.
   *
   * @param keys a list of keys, must be unique and sorted in increasing order
   * @param vals the according values
   * @param lens optional, lens[i] stores the value length of the \a
   * i-th KV pair
   * @param cmd an optional command sent to the servers
   * @param cb the callback which is called when the push is finished.
   * @return the timestamp of this request
   */
  int Push(const std::vector<Key>& keys,
           const std::vector<Val>& vals,
           const std::vector<int>& lens = {},
           int cmd = 0,
           const Callback& cb = nullptr) {
    return ZPush(
      SArray<Key>(keys), SArray<Val>(vals), SArray<int>(lens), cmd, cb);
  }

  /**
   * \brief zero-copy Push
   *
   * This function is similar to \ref Push except that all data
   * will not be copied into system for better performance. It is the caller's
   * responsibility to keep the content to be not changed before actually
   * finished.
   */
  int ZPush(const SArray<Key>& keys,
            const SArray<Val>& vals,
            const SArray<int>& lens = {},
            int cmd = 0,
            const Callback& cb = nullptr,
            int uniq_key = Meta::kEmpty,
            int version = 0) {
    int ts = obj_->NewRequest(kServerGroup);
    AddCallback(ts, cb);
    KVPairs<Val> kvs;
    kvs.keys = keys;
    kvs.vals = vals;
    kvs.lens = lens;

    if(enable_intra_ts && kvs.keys.size()) {
      KVMeta meta;
      meta.cmd         = cmd;
      meta.push        = true;
      meta.sender      = Postoffice::Get()->van()->my_node_.id;
      meta.timestamp   = ts;
      meta.app_id      = obj_->app_id();
      meta.customer_id = obj_->customer_id();
      meta.key         = uniq_key;
      meta.version     = version;
      meta.num_merge   = 1;
      request_handle_(meta, kvs, this);
      Postoffice::Get()->van()->AskForReceiverPush(meta.app_id, meta.customer_id, ts);
    } else {
      Send(ts, true, cmd, kvs, uniq_key, version);
    }
    return ts;
  }

  int P3_ZPush(const SArray<Key>& keys,
               const SArray<Val>& vals,
               const SArray<int>& lens = {},
               int cmd = 0,
               const Callback& cb = nullptr,
               int priority = 0) {
    int ts = obj_->NewRequest(kServerGroup);
    AddCallback(ts, [this, ts, keys, vals, lens, cb]() mutable {
      if (recv_kvs_.find(ts) == recv_kvs_.end()) {
        if (cb) cb();
        return;
      }

      mu_.lock();
      auto& kvs = recv_kvs_[ts];
      mu_.unlock();

      // do check
      CHECK_EQ(kvs.size(), (size_t)1);
      CHECK_EQ(keys.size(), (size_t)1);
      CHECK_EQ(lens.size(), keys.size());

      auto kv = kvs[0];
      ps::Key key = keys[0];
      size_t len = lens[0];
      CHECK_EQ(kv.keys[0], key);
      CHECK_EQ(kv.lens[0], len);
      CHECK_EQ(vals.size(), len);
      CHECK_EQ(kv.vals.size(), len);

      Val* p_vals = vals.data();
      int* p_lens = lens.data();
      for (const auto& s : kvs) {
        memcpy(p_vals, s.vals.data(), s.vals.size() * sizeof(Val));
        p_vals += s.vals.size();
        if (p_lens) {
          memcpy(p_lens, s.lens.data(), s.lens.size() * sizeof(int));
          p_lens += s.lens.size();
        }
      }

      mu_.lock();
      recv_kvs_.erase(ts);
      mu_.unlock();

      if (cb) cb();
    });

    KVPairs<Val> kvs;
    kvs.keys = keys;
    kvs.vals = vals;
    kvs.lens = lens;
    kvs.priority = priority;
    Send(ts, true, cmd, kvs, Meta::kEmpty, Meta::kEmpty, Meta::kEmpty, Meta::kEmpty, 1, true);
    return ts;
  }

  /**
   * \brief Pulls the values associated with the keys from the server nodes
   *
   * This function pulls the values of the keys specified in \a keys from the
   * server nodes. The format is same to \ref KVPairs
   *
   * Sample usage: the following codes pull the values of keys \a 1 and \a 3
   * from the server nodes.
   * \code
   *   KVWorker<float> w;
   *   std::vector<Key> keys = {1, 3};
   *   std::vector<float> vals;
   *   ps.Pull(keys, &vals);
   * \endcode
   *
   * It's a non-blocking call. The actual pulling is finished,
   * namely \a vals (and \a lens) is filled with pulled values, only
   * if \ref Wait returns or the callback is called.
   *
   * @param keys a list of keys, must be unique and sorted in increasing order
   * @param vals the buffer for the pulled values. It can be 0 size.
   * @param lens optional buffer for the value length. If set, it can be 0 size.
   * @param cmd an optional command sent to the servers
   * @param cb the callback which is called when the pull is finished.
   * @return the timestamp of this request
   */
  int Pull(const std::vector<Key>& keys,
           std::vector<Val>* vals,
           std::vector<int>* lens = nullptr,
           int cmd = 0,
           const Callback& cb = nullptr) {
    return Pull_(SArray<Key>(keys), vals, lens, cmd, cb);
  }

  /**
   * \brief zero-copy Pull
   *
   * This function is similar to \ref Pull except that all data
   * will not be copied into system for better performance. It is the caller's
   * responsibility to keep the content to be not changed before actually
   * finished.
   */
  int ZPull(const SArray<Key>& keys,
            SArray<Val>* vals,
            SArray<int>* lens = nullptr,
            int cmd = 0,
            const Callback& cb = nullptr) {
    return Pull_(keys, vals, lens, cmd, cb);
  }

  /** \brief auto pull for tsengine*/
  int AutoPull(int uniq_key,
               const SArray<Key>& keys,
               SArray<Val>* vals,
               SArray<int>* lens = nullptr,
               int cmd = 0,
               const Callback& cb = nullptr);

  void Send(int timestamp, bool push, int cmd, const KVPairs<Val>& kvs,
            int uniq_key = Meta::kEmpty, int key_version = Meta::kEmpty,
            int app = Meta::kEmpty, int customer = Meta::kEmpty, int num_merge = 1,
            bool enable_priority = false);

  /**
   * \brief Waits until a push or pull has been finished
   *
   * Sample usage:
   * \code
   *   int ts = w.Pull(keys, &vals);
   *   Wait(ts);
   *   // now vals is ready for use
   * \endcode
   *
   * \param timestamp the timestamp returned by the push or pull
   */
  void Wait(int timestamp) { obj_->WaitRequest(timestamp); }

  void Response(const KVMeta& req);

  using SlicedKVs = std::vector<std::pair<bool, KVPairs<Val>>>;
  /**
   * \brief a slicer partitions a key-value list according to the key ranges
   * \param send the kv list for partitioning
   * \param ranges the key ranges, ranges[i] is the key range of server i
   * \param sliced the sliced lists. slices[i] should only contains keys in
   * ranges[i] and the according values
   */
  using Slicer = std::function<void(
    const KVPairs<Val>& send, const std::vector<Range>& ranges,
    SlicedKVs* sliced)>;

  /**
   * \brief set a user-defined slicer
   */
  void set_slicer(const Slicer& slicer) {
    CHECK(slicer); slicer_ = slicer;
  }

  int enable_intra_ts = 0;
  int enable_p3 = 0;

 private:
  ReqHandle request_handle_;
  /**
   * \brief internal pull, C/D can be either SArray or std::vector
   */
  template <typename C, typename D>
  int Pull_(const SArray<Key>& keys, C* vals, D* lens,
            int cmd, const Callback& cb);

  void AutoPullReply(const int sender);
  void AutoPullUpdate(const int version,const int iters, const int req, const KVPairs<Val>& kvs);

  /**
   * \brief add a callback for a request. threadsafe.
   * @param cb callback
   * @param timestamp the timestamp of the request
   */
  void AddCallback(int timestamp, const Callback& cb) {
    if (!cb) return;
    std::lock_guard<std::mutex> lk(mu_);
    callbacks_[timestamp] = cb;
  }

  /**
   * \brief run and delete the callback
   * \param timestamp the timestamp of the callback
   */
  void RunCallback(int timestamp);

  /** \brief internal receive handle */
  void Process(const Message& msg);
  void TS_Process(const Message& msg);

  /** \brief default kv slicer */
  void DefaultSlicer(const KVPairs<Val>& send,
                     const std::vector<Range>& ranges,
                     SlicedKVs* sliced);

  /** \brief data buffer for received kvs for each timestamp */
  std::unordered_map<int, std::vector<KVPairs<Val>>> recv_kvs_;
  /** \brief data buffer for received from auto pull */
  std::unordered_map<int, std::unordered_map<Key, KVPairs<Val>>> auto_pull_kvs_;
  /** \brief data version */
  std::unordered_map<int, int> data_version_;
  /** \brief callbacks for each timestamp */
  std::unordered_map<int, Callback> callbacks_;
  /** \brief lock */
  std::mutex mu_;
  std::mutex ts_mu_;
  std::mutex ts_cond;
  /** \brief condition value */
  std::condition_variable cond_;
  /** \brief kv list slicer */
  Slicer slicer_;
  int send_push=0;
};


/**
 * \brief A server node for maintaining key-value pairs
 */
template <typename Val>
class KVServer: public SimpleApp {
 public:
  /**
   * \brief constructor
   * \param app_id the app id, should match with \ref KVWorker's id
   */
  explicit KVServer(int app_id) : SimpleApp() {
    using namespace std::placeholders;
    slicer_ = std::bind(&KVServer<Val>::DefaultSlicer, this, _1, _2, _3);
    obj_ = new Customer(app_id, app_id, std::bind(&KVServer<Val>::Process, this, _1), true);
    enable_inter_ts = dmlc::GetEnv("ENABLE_INTER_TS", 0);
    enable_intra_ts = dmlc::GetEnv("ENABLE_INTRA_TS", 0);
    enable_dgt = dmlc::GetEnv("ENABLE_DGT", 0);
    enable_p3 = dmlc::GetEnv("ENABLE_P3", 0);
    if(enable_dgt) InitDGT();
  }

  /** \brief deconstructor */
  virtual ~KVServer() { delete obj_; obj_ = nullptr; }

  /** \brief send merged data to global server to perform aggregation. */
  void Send(int timestamp, bool push, int cmd, const KVPairs<Val>& kvs,
            int uniq_key = Meta::kEmpty);

  void TS_Send(int timestamp, bool push, int uniq_key, int cmd, const KVPairs<Val>& kvs,
               int key_version, int app, int customer, int merge);

  /**
   * \brief the handle to process a push/pull request from a worker
   * \param req_meta meta-info of this request
   * \param req_data kv pairs of this request
   * \param server this pointer
   */
  using ReqHandle = std::function<void(const KVMeta& req_meta,
                                       const KVPairs<Val>& req_data,
                                       KVServer* server)>;

  void set_request_handle(const ReqHandle& request_handle) {
    CHECK(request_handle) << "invalid request handle";
    request_handle_ = request_handle;
  }

  void set_request_global_handle(const ReqHandle& request_handle) {
    CHECK(request_handle) << "invalid request handle";
    request_handle_global = request_handle;
  }
  using Callback = std::function<void()>;

  void AddCallback(int timestamp, const Callback& cb) {
    if (!cb) return;
    std::lock_guard<std::mutex> lk(mu_);
    callbacks_[timestamp] = cb;
  }

  void RunCallback(int timestamp);

  /**
   * \brief push local aggregated data to global server to
   *        perform aggregation.
   * \param kvs the kv pairs that will send to the global server
   * \param cmd an optional command sent to the servers
   * \param cb the callback which is called when the push is finished.
   * */
  int Push(const KVPairs<Val>& kvs,
           const int cmd = 0,
           const Callback& cb = nullptr) {
    int ts = obj_->NewRequest(kServerGroupGlobal, true);
    AddCallback(ts, cb);
    Send(ts, true, cmd, kvs);
    return ts;
  }

  int Pull(const SArray<Key>& keys, int cmd = 0) {
    int ts = obj_->NewRequest(kServerGroupGlobal);
    KVPairs<Val> kvs;
    kvs.keys = keys;
    Send(ts, false, cmd, kvs);
    return ts;
  }

  int TS_Push(const KVPairs<Val>& kvs,
              int uniq_key = 0,
              const int cmd = 0,
              const Callback& cb = nullptr) {
    int ts = obj_->NewRequest(kServerGroupGlobal, true);
    AddCallback(ts, cb);
    if(kvs.keys.size() && enable_inter_ts){
      KVMeta meta;
      meta.cmd       = cmd;
      meta.push      = true;
      meta.sender    = Postoffice::Get()->van()->my_node_global_.id;
      meta.timestamp = ts;
      meta.app_id = obj_->app_id();
      meta.customer_id = obj_->customer_id();
      meta.key = uniq_key;
      meta.version = 0;
      meta.num_merge = 1;
      request_handle_global(meta,kvs,this);
      Postoffice::Get()->van()->AskForReceiverPush(meta.app_id , meta.customer_id, ts, true);
    } else {
      Send(ts, true, cmd, kvs, uniq_key);
    }
    return ts;
  }

  int TS_Pull(const SArray<Key>& keys, int uniq_key = 0, int cmd = 0) {
      int ts = obj_->NewRequest(kServerGroupGlobal);
      KVPairs<Val> kvs;
      kvs.keys = keys;
      Send(ts, false, cmd, kvs, uniq_key);
      return ts;
  }

  /**
   * \brief response to the push/pull request
   * \param req the meta-info of the request
   * \param res the kv pairs that will send back to the worker
   */
  void Response(const KVMeta& req, const KVPairs<Val>& res, bool is_global = false);
  void Response(const KVMeta& req, bool is_global = false) { Response(req, KVPairs<Val>(), is_global); }

  void AutoPullUpdate2(const int version, const int iters, const int key, const int cmd, const KVPairs<Val>& kvs) {
    int throughput = -1;
    int last_recv_id = -1;
    while(true) {
      int receiver = Postoffice::Get()->van()->GetGlobalReceiver(throughput, last_recv_id, iters);
      if(receiver == -1) break; // whether transmition is over
      if(kvs.keys.size()){
        Message msg;
        msg.meta.head = cmd;
        msg.meta.app_id = obj_->app_id();
        msg.meta.customer_id = obj_->customer_id();
        msg.meta.request = true;
        msg.meta.push = false;
        msg.meta.sender = Postoffice::Get()->van()->my_node_global_.id;
        msg.meta.recver = receiver;
        msg.meta.key = key;
        msg.meta.version = version;
        msg.meta.iters=iters;
        msg.meta.timestamp = -1;
        msg.AddData(kvs.keys);
        msg.AddData(kvs.vals);
        if (kvs.lens.size()) {
          msg.AddData(kvs.lens);
        }
        clock_t starts ,ends;
        starts = clock();
        Postoffice::Get()->van()->Send(msg,true);
        Postoffice::Get()->van()->WaitForGlobalFinish();
        ends = clock();
        throughput= (int) (1/((double)(ends - starts) / CLOCKS_PER_SEC));
        last_recv_id = receiver;
      }
    }
  }

  void AutoPullUpdate1(const int version, const KVMeta& req, const KVPairs<Val>& kvs = KVPairs<Val>()) {
    global_iter++;
    int throughput = -1;
    int last_recv_id = -1;
    while(true) {
      int receiver=Postoffice::Get()->van()->GetGlobalReceiver(throughput, last_recv_id, global_iter);
      if(receiver == -1) break; // whether transmition is over
      if(kvs.keys.size()) {
        Message msg;
        msg.meta.head = req.cmd;
        msg.meta.app_id = obj_->app_id();
        msg.meta.customer_id = obj_->customer_id();
        msg.meta.request = true;
        msg.meta.push = false;
        msg.meta.sender = Postoffice::Get()->van()->my_node_global_.id;
        msg.meta.recver = receiver;
        msg.meta.key = req.key;
        msg.meta.version = version;
        msg.meta.iters = global_iter;
        msg.meta.timestamp = -1;
        msg.AddData(kvs.keys);
        msg.AddData(kvs.vals);
        if (kvs.lens.size()) {
          msg.AddData(kvs.lens);
        }
        clock_t starts, ends;
        starts = clock();
        Postoffice::Get()->van()->Send(msg, true);
        Postoffice::Get()->van()->WaitForGlobalFinish();
        ends = clock();
        throughput = (int) (1/((double)(ends - starts) / CLOCKS_PER_SEC));
        last_recv_id = receiver;
      }
    }
  }

  /** \brief server auto load data to worker**/
  void AutoPullUpdate(const int version, const KVMeta& req, const KVPairs<Val>& kvs = KVPairs<Val>()) {
    iter++;
    int throughput = -1;
    int last_recv_id = -1;
    while(true) {
      int receiver = Postoffice::Get()->van()->GetReceiver(throughput, last_recv_id, iter);
      if(receiver == -1) break; // whether transmition is over
      if(kvs.keys.size()) {
        Message msg;
        msg.meta.app_id = obj_->app_id();
        msg.meta.customer_id = obj_->customer_id();
        msg.meta.request = true;
        msg.meta.push = false;
        msg.meta.sender = Postoffice::Get()->van()->my_node_.id;
        msg.meta.recver = receiver;
        msg.meta.key = req.key;
        msg.meta.version = version;
        msg.meta.iters = iter;
        msg.meta.timestamp = -1;
        msg.AddData(kvs.keys);
        msg.AddData(kvs.vals);
        if (kvs.lens.size()) {
          msg.AddData(kvs.lens);
        }
        clock_t starts, ends;
        starts = clock();
        Postoffice::Get()->van()->Send(msg);
        Postoffice::Get()->van()->WaitForFinish();
        ends = clock();
        throughput = (int) (1/((double)(ends - starts) / CLOCKS_PER_SEC));
        last_recv_id = receiver;
      }
    }
  }

  /**
   * \brief get the number of ACKs of push request
   * @param timestamp, the timestamp of the push request
   */
  int NumResponse(int timestamp) { return obj_->NumResponse(timestamp); }

  using SlicedKVs = std::vector<std::pair<bool, KVPairs<Val>>>;
  /**
   * \brief a slicer partitions a key-value list according to the key ranges
   * \param send the kv list for partitioning
   * \param ranges the key ranges, ranges[i] is the key range of server i
   * \param sliced the sliced lists. slices[i] should only contains keys in
   * ranges[i] and the according values
   */
  using Slicer = std::function<void(
    const KVPairs<Val>& send, const std::vector<Range>& ranges,
    SlicedKVs* sliced)>;

  /**
   * \brief set a user-defined slicer
   */
  void set_slicer(const Slicer& slicer) {
    CHECK(slicer); slicer_ = slicer;
  }

  int enable_intra_ts = 0;
  int enable_inter_ts = 0;
  int enable_p3 = 0;

 private:
  /** \brief internal receive handle */
  void Process(const Message& msg);

  void AutoPullReply(const int sender);
  /** \brief default kv slicer */
  void DefaultSlicer(const KVPairs<Val>& send,
                     const std::vector<Range>& ranges,
                     SlicedKVs* sliced);

  /** \brief request handle */
  ReqHandle request_handle_;
  ReqHandle request_handle_global;
  /** \brief callbacks for each timestamp */
  std::unordered_map<int, Callback> callbacks_;
  /** \brief lock */
  std::mutex mu_;
  /** \brief kv list slicer */
  Slicer slicer_;

  void InitDGT();

  float EvalMsgContribution(int key, Message& msg);

  int GetChannel(int index, int max_index, int C, float k);

  enum class RequestType {
      kDefaultPushPull, kRowSparsePushPull, kCompressedPushPull, kDGCompressedPushOnly
  };

  struct DataHandleType {
      RequestType requestType;
      int dtype;
  };

  static DataHandleType DepairDataHandleType(int cmd) {
    int w = std::floor((std::sqrt(8 * cmd + 1) - 1) / 2);
    int t = ((w * w) + w) / 2;
    int y = cmd - t;
    int x = w - y;
    CHECK_GE(x, 0);
    CHECK_GE(y, 0);
    DataHandleType type;
    type.requestType = static_cast<RequestType>(x);
    type.dtype = y;
    return type;
  }

  int enable_dgt = 0;
  float dmlc_k = 1.0;
  float contri_alpha = 0.0;
  int  dgt_info = 0;
  int  block_size = 0;
  float dmlc_k_init = 0.0;
  float dmlc_k_min = 0.0;
  int adaptive_k_flag = 0;
  int udp_channel_num = 0;
  std::vector<Message> msg_vector;
  std::unordered_map<int, std::unordered_map<int, float>> contri;
  int iter=-1;
  int global_iter=-1;
  int send_push=0;
};

/**
 * \brief an example handle adding pushed kv into store
 */
template <typename Val>
struct KVServerDefaultHandle {
  void operator()(
      const KVMeta& req_meta, const KVPairs<Val>& req_data, KVServer<Val>* server) {
    size_t n = req_data.keys.size();
    KVPairs<Val> res;
    if (req_meta.push) {
      CHECK_EQ(n, req_data.vals.size());
    } else {
      res.keys = req_data.keys; res.vals.resize(n);
    }
    for (size_t i = 0; i < n; ++i) {
      Key key = req_data.keys[i];
      if (req_meta.push) {
        store[key] += req_data.vals[i];
      } else {
        res.vals[i] = store[key];
      }
    }
    server->Response(req_meta, res);
  }
  std::unordered_map<Key, Val> store;
};

template <typename Val>
void KVServer<Val>::Response(const KVMeta& req, const KVPairs<Val>& res, bool is_global) {
  Message msg;
  msg.meta.app_id      = obj_->app_id();
  msg.meta.customer_id = req.customer_id;
  msg.meta.request     = false;
  msg.meta.push        = req.push;
  msg.meta.head        = req.cmd;
  msg.meta.timestamp   = req.timestamp;
  msg.meta.recver      = req.sender;
  msg.meta.key         = req.key;
  msg.meta.version     = 0;
  if (res.keys.size()) {
    msg.AddData(res.keys);
    msg.AddData(res.vals);
    if (res.lens.size()) {
      msg.AddData(res.lens);
    }
  }
  CHECK_NE(Postoffice::Get()->van()->Send(msg, is_global), -1);
}

template <typename Val>
void KVWorker<Val>::DefaultSlicer(
    const KVPairs<Val>& send, const std::vector<Range>& ranges,
    typename KVWorker<Val>::SlicedKVs* sliced) {
  sliced->resize(ranges.size());

  // find the positions in msg.key
  size_t n = ranges.size();
  std::vector<size_t> pos(n+1);
  const Key* begin = send.keys.begin();
  const Key* end = send.keys.end();
  for (size_t i = 0; i < n; ++i) {
    if (i == 0) {
      pos[0] = std::lower_bound(begin, end, ranges[0].begin()) - begin;
      begin += pos[0];
    } else {
      CHECK_EQ(ranges[i-1].end(), ranges[i].begin());
    }
    size_t len = std::lower_bound(begin, end, ranges[i].end()) - begin;
    begin += len;
    pos[i+1] = pos[i] + len;

    // don't send it to servers for empty kv
    sliced->at(i).first = (len != 0);
  }
  CHECK_EQ(pos[n], send.keys.size());
  if (send.keys.empty()) return;

  // the length of value
  size_t k = 0, val_begin = 0, val_end = 0;
  if (send.lens.empty()) {
    k = send.vals.size() / send.keys.size();
    CHECK_EQ(k * send.keys.size(), send.vals.size());
  } else {
    CHECK_EQ(send.keys.size(), send.lens.size());
  }

  // slice
  for (size_t i = 0; i < n; ++i) {
    if (pos[i + 1] == pos[i]) {
      sliced->at(i).first = false;
      continue;
    }
    sliced->at(i).first = true;
    auto& kv = sliced->at(i).second;
    kv.keys = send.keys.segment(pos[i], pos[i+1]);
    if (send.lens.size()) {
      kv.lens = send.lens.segment(pos[i], pos[i+1]);
      for (int l : kv.lens) val_end += l;
      kv.vals = send.vals.segment(val_begin, val_end);
      val_begin = val_end;
    } else {
      kv.vals = send.vals.segment(pos[i] * k, pos[i+1] * k);
    }
  }
}

template <typename Val>
void KVServer<Val>::DefaultSlicer(
        const KVPairs<Val>& send, const std::vector<Range>& ranges,
        typename KVServer<Val>::SlicedKVs* sliced) {
  sliced->resize(ranges.size());

  // find the positions in msg.key
  size_t n = ranges.size();
  std::vector<size_t> pos(n+1);
  const Key* begin = send.keys.begin();
  const Key* end = send.keys.end();
  for (size_t i = 0; i < n; ++i) {
    if (i == 0) {
      pos[0] = std::lower_bound(begin, end, ranges[0].begin()) - begin;
      begin += pos[0];
    } else {
      CHECK_EQ(ranges[i - 1].end(), ranges[i].begin());
    }
    size_t len = std::lower_bound(begin, end, ranges[i].end()) - begin;
    begin += len;
    pos[i+1] = pos[i] + len;

    // don't send it to servers for empty kv
    sliced->at(i).first = (len != 0);
  }
  CHECK_EQ(pos[n], send.keys.size());
  if (send.keys.empty()) return;

  // the length of value
  size_t k = 0, val_begin = 0, val_end = 0;
  if (send.lens.empty()) {
    k = send.vals.size() / send.keys.size();
    CHECK_EQ(k * send.keys.size(), send.vals.size());
  } else {
    CHECK_EQ(send.keys.size(), send.lens.size());
  }

  // slice
  for (size_t i = 0; i < n; ++i) {
    if (pos[i+1] == pos[i]) {
      sliced->at(i).first = false;
      continue;
    }
    sliced->at(i).first = true;
    auto& kv = sliced->at(i).second;
    kv.keys = send.keys.segment(pos[i], pos[i + 1]);
    if (send.lens.size()) {
      kv.lens = send.lens.segment(pos[i], pos[i + 1]);
      for(int l : kv.lens) val_end += l;
      kv.vals = send.vals.segment(val_begin, val_end);
      val_begin = val_end;
    } else {
      kv.vals = send.vals.segment(pos[i] * k, pos[i+1] * k);
    }
  }
}

template <typename Val>
void KVWorker<Val>::Send(int timestamp, bool push, int cmd, const KVPairs<Val>& kvs,
                         int uniq_key, int key_version, int app, int customer, int num_merge,
                         bool enable_priority) {
  // slice the message
  SlicedKVs sliced;
  slicer_(kvs, Postoffice::Get()->GetServerKeyRanges(), &sliced);

  // need to add response first, since it will not always trigger the callback
  int skipped = 0;
  for (size_t i = 0; i < sliced.size(); ++i) {
    if (!sliced[i].first) ++skipped;
  }
  obj_->AddResponse(timestamp, skipped);
  if ((size_t) skipped == sliced.size()) {
    RunCallback(timestamp);
  }

  for (size_t i = 0; i < sliced.size(); ++i) {
    const auto &s = sliced[i];
    if (!s.first) continue;
    Message msg;
    msg.meta.app_id      = (app != Meta::kEmpty) ? app : obj_->app_id();
    msg.meta.customer_id = (customer != Meta::kEmpty) ? customer : obj_->customer_id();
    msg.meta.request     = true;
    msg.meta.push        = push;
    msg.meta.head        = cmd;
    msg.meta.timestamp   = timestamp;
    msg.meta.sender      = (app != Meta::kEmpty) ? Postoffice::Get()->van()->my_node_.id : Meta::kEmpty;
    msg.meta.recver      = (app != Meta::kEmpty) ? send_push : Postoffice::Get()->ServerRankToID(i, false);
    msg.meta.key         = uniq_key;
    msg.meta.version     = key_version;
    msg.meta.iters       = (num_merge != 1) ? num_merge : 1;
    msg.meta.priority    = (enable_priority) ? kvs.priority : 0;

    const auto &kvs = s.second;
    if (kvs.keys.size()) {
      msg.AddData(kvs.keys);
      msg.AddData(kvs.vals);
      if (kvs.lens.size()) {
        msg.AddData(kvs.lens);
      }
    }

    if (enable_priority) {
      Postoffice::Get()->van()->PushToSenderQueue(msg);
    } else {
      CHECK_NE(Postoffice::Get()->van()->Send(msg), -1);
      if (app != Meta::kEmpty) send_push = 0;
    }
  }
}

template <typename Val>
void KVServer<Val>::InitDGT(){
  contri_alpha = dmlc::GetEnv("DGT_CONTRI_ALPHA", 0.3);
  dgt_info = dmlc::GetEnv("DGT_INFO", 0);
  block_size = dmlc::GetEnv("DGT_BLOCK_SIZE", 4096);
  dmlc_k_init = dmlc::GetEnv("DMLC_K", 0.5);
  dmlc_k_min = dmlc::GetEnv("DMLC_K_MIN", 0.2);
  adaptive_k_flag = dmlc::GetEnv("ADAPTIVE_K_FLAG", 0);
  udp_channel_num = dmlc::GetEnv("DMLC_UDP_CHANNEL_NUM", 1);
  return;
}

template <typename Val>
float KVServer<Val>::EvalMsgContribution(int key, Message& msg) {
  /*calculate p_N of a msg*/
  float *pd = (float*)msg.data[1].data();
  int nlen = msg.data[1].size() / sizeof(float);
  float N = 0.0;
  for(int i = 0; i < nlen; i++){
      N += fabs(*(pd + i));
  }

  /*calculate contri of a msg*/
  auto itt = contri.find(key);
  if(itt == contri.end())
    contri[key][msg.meta.seq] = 0.0;
  auto it = contri[key].find(msg.meta.seq);
  if(it == contri[key].end())
    contri[key][msg.meta.seq] = 0.0;

  contri[key][msg.meta.seq] = contri_alpha * contri[key][msg.meta.seq] + (1 - contri_alpha)*(N / nlen);
  return contri[key][msg.meta.seq];
}

template <typename Val>
int KVServer<Val>::GetChannel(int index, int max_index, int C, float k) {
  int min_index = std::round(k * (max_index + 1));
  if(index < min_index)  return 0;
  for(int i = 0; i < C; ++i){
    if(max_index - min_index > 0) {
      if(index >= min_index + (float)i * (max_index - min_index) / C && \
         index < min_index + (float)(i + 1) * (max_index - min_index) / C) {
        return i + 1;
      }
    } else {
      return i + 1;
    }
  }
  srand((unsigned)time(NULL));
  return rand() % C + 1;
}

template <typename Val>
void KVServer<Val>::Send(int timestamp, bool push, int cmd, const KVPairs<Val>& kvs,
                         int uniq_key) {
  // slice the message
  SlicedKVs sliced;
  slicer_(kvs, Postoffice::Get()->GetServerKeyRanges(true), &sliced);

  // need to add response first, since it will not always trigger the callback
  int skipped = 0;
  for (size_t i = 0; i < sliced.size(); ++i) {
    if (!sliced[i].first) ++skipped;
  }
  obj_->AddResponse(timestamp, skipped);
  if ((size_t)skipped == sliced.size()) {
    RunCallback(timestamp);
  }

  for (size_t i = 0; i < sliced.size(); ++i) {
    const auto& s = sliced[i];
    if (!s.first) continue;
    if(DepairDataHandleType(cmd).requestType == RequestType::kDefaultPushPull
        && push && enable_dgt) {
      int total_bytes = kvs.vals.size();
      int remain_bytes = total_bytes;
      int val_bytes = 0;
      int seq = 0;
      int seq_num = 0;

      if(total_bytes % block_size == 0) {
          seq_num = total_bytes / block_size;
      } else {
          seq_num = total_bytes / block_size + 1;
      }
      dmlc_k = dmlc_k_init;
      while(remain_bytes != 0) {
        Message msg;
        msg.meta.app_id      = obj_->app_id();
        msg.meta.customer_id = obj_->customer_id();
        msg.meta.request     = true;
        msg.meta.push        = push;
        msg.meta.head        = cmd;
        msg.meta.timestamp   = timestamp;
        msg.meta.recver      = Postoffice::Get()->ServerRankToID(i, true);
        msg.meta.msg_type    = 1;
        if (DepairDataHandleType(cmd).dtype == 0) { // kFloat32
            msg.meta.bits_num = 32;
        } else if (DepairDataHandleType(cmd).dtype == 2) { // kFloat16
            msg.meta.bits_num = 16;
        }

        msg.meta.total_bytes = total_bytes;
        int l = std::min(remain_bytes,block_size);
        SArray<Val> tmp_val = kvs.vals.segment(val_bytes, val_bytes+l);

        msg.meta.val_bytes = val_bytes;
        val_bytes += l;
        msg.meta.first_key = kvs.keys[0];
        msg.meta.seq = seq;
        msg.meta.seq_begin = 0;
        msg.meta.seq_end = seq_num - 1;
        if (kvs.keys.size()) {
          msg.AddData(kvs.keys);
          msg.meta.keys_len = msg.data.back().size();
          msg.AddData(tmp_val);
          msg.meta.vals_len = msg.data.back().size();
          if (kvs.lens.size()) {
            msg.AddData(kvs.lens);
            msg.meta.lens_len = msg.data.back().size();
          }
        }
        msg.contribution = EvalMsgContribution((int)kvs.keys[0], msg);

        if (msg.contribution != 0 || msg.meta.seq == msg.meta.seq_end) {
          msg_vector.push_back(msg);
        }
        remain_bytes -= l;
        seq++;
        msg.data.clear();
      }
      std::sort(msg_vector.begin(), msg_vector.end() - 1, [](const Message& msg1, const Message& msg2) {
        return msg1.contribution > msg2.contribution;
      });
      for (size_t j = 0; j < msg_vector.size(); ++j) {
        msg_vector[j].meta.channel = GetChannel(j, msg_vector.size() - 1, udp_channel_num, dmlc_k);
        if (msg_vector[j].meta.seq == msg_vector[j].meta.seq_end) {
          msg_vector[j].meta.channel = 0;
        }
        msg_vector[j].meta.tos = (udp_channel_num - msg_vector[j].meta.channel) * 32;
        Postoffice::Get()->van()->Classifier(msg_vector[j], msg_vector[j].meta.channel, 0);
      }
      msg_vector.clear();
    } else {
      Message msg;
      msg.meta.app_id      = obj_->app_id();
      msg.meta.customer_id = obj_->customer_id();
      msg.meta.request     = true;
      msg.meta.push        = push;
      msg.meta.head        = cmd;
      msg.meta.timestamp   = timestamp;
      msg.meta.recver      = Postoffice::Get()->ServerRankToID(i, true);
      msg.meta.iters       = 1;
      msg.meta.key         = uniq_key;
      msg.meta.version     = 0;
      const auto& kvs = s.second;
      if (kvs.keys.size()) {
        msg.AddData(kvs.keys);
        msg.AddData(kvs.vals);
        if (kvs.lens.size()) {
          msg.AddData(kvs.lens);
        }
      }
      CHECK_NE(Postoffice::Get()->van()->Send(msg, true), -1);
    }
  }
}

template <typename Val>
void KVServer<Val>::TS_Send(int timestamp, bool push, int uniq_key, int cmd,
                             const KVPairs<Val>& kvs, int key_version,int app, int customer, int merge) {
  // slice the message
  SlicedKVs sliced;
  slicer_(kvs, Postoffice::Get()->GetServerKeyRanges(true), &sliced);

  // need to add response first, since it will not always trigger the callback
  int skipped = 0;
  for (size_t i = 0; i < sliced.size(); ++i) {
    if (!sliced[i].first) ++skipped;
  }
  obj_->AddResponse(timestamp, skipped);
  if ((size_t)skipped == sliced.size()) {
    RunCallback(timestamp);
  }
  for (size_t i = 0; i < sliced.size(); ++i) {
    const auto& s = sliced[i];
    if (!s.first) continue;
    if(DepairDataHandleType(cmd).requestType == RequestType::kDefaultPushPull && push && enable_dgt){
      int total_bytes = kvs.vals.size();
      int remain_bytes = total_bytes;
      int val_bytes = 0;   // offset
      int seq = 0;
      int seq_num = 0;

      if(total_bytes % block_size == 0) {
          seq_num = total_bytes / block_size;
      } else {
          seq_num = total_bytes / block_size + 1;
      }
      dmlc_k = dmlc_k_init;
      while(remain_bytes != 0){
        Message msg;
        msg.meta.app_id = obj_->app_id();
        msg.meta.customer_id = obj_->customer_id();
        msg.meta.request     = true;
        msg.meta.push        = push;
        msg.meta.head        = cmd;
        msg.meta.timestamp   = timestamp;
        msg.meta.recver      = Postoffice::Get()->ServerRankToID(i, true);
        msg.meta.msg_type    = 1;
        if(DepairDataHandleType(cmd).dtype == 0) { //kFloat32
            msg.meta.bits_num = 32;
        } else if (DepairDataHandleType(cmd).dtype == 2) { //kFloat16
            msg.meta.bits_num = 16;
        }

        msg.meta.total_bytes = total_bytes;
        int l = std::min(remain_bytes,block_size);
        SArray<Val> tmp_val = kvs.vals.segment(val_bytes, val_bytes+l);

        msg.meta.val_bytes = val_bytes;
        val_bytes += l;
        msg.meta.first_key = kvs.keys[0];
        msg.meta.seq = seq;
        msg.meta.seq_begin = 0;
        msg.meta.seq_end = seq_num - 1;
        if (kvs.keys.size()) {
          msg.AddData(kvs.keys);
          msg.meta.keys_len = msg.data.back().size();
          msg.AddData(tmp_val);
          msg.meta.vals_len = msg.data.back().size();
          if (kvs.lens.size()) {
            msg.AddData(kvs.lens);
            msg.meta.lens_len = msg.data.back().size();
          }
        }
        msg.contribution = EvalMsgContribution((int)kvs.keys[0], msg);

        if(msg.contribution != 0 || msg.meta.seq == msg.meta.seq_end)
          msg_vector.push_back(msg);
        remain_bytes -= l;
        seq++;
        msg.data.clear();
      }
      std::sort(msg_vector.begin(), msg_vector.end() - 1, [](const Message& msg1, const Message& msg2){
        return msg1.contribution > msg2.contribution;
      });
      for(size_t j = 0; j < msg_vector.size(); ++j) {
        msg_vector[j].meta.channel = GetChannel(j, msg_vector.size() - 1, udp_channel_num, dmlc_k);
        if(msg_vector[j].meta.seq == msg_vector[j].meta.seq_end) {
          msg_vector[j].meta.channel = 0;
        }
        msg_vector[j].meta.tos = (udp_channel_num - msg_vector[j].meta.channel) * 32;
        Postoffice::Get()->van()->Classifier(msg_vector[j],msg_vector[j].meta.channel, 0);
      }
      msg_vector.clear();
    } else {
      Message msg;
      msg.meta.app_id      = app;
      msg.meta.customer_id = customer;
      msg.meta.request     = true;
      msg.meta.push        = push;
      msg.meta.head        = cmd;
      msg.meta.timestamp   = timestamp;
      msg.meta.sender      = Postoffice::Get()->van()->my_node_global_.id;
      msg.meta.iters       = merge;
      msg.meta.recver      = send_push;
      msg.meta.key         = uniq_key;
      msg.meta.version     = key_version;
      const auto& kvs = s.second;
      if (kvs.keys.size()) {
        msg.AddData(kvs.keys);
        msg.AddData(kvs.vals);
        if (kvs.lens.size()) {
          msg.AddData(kvs.lens);
        }
      }
      CHECK_NE(Postoffice::Get()->van()->Send(msg, true), -1);
      send_push = 0;
    }
  }
}

template <typename Val>
void KVWorker<Val>::Response(const KVMeta& req) {
  Message msg;
  msg.meta.app_id = obj_->app_id();
  msg.meta.customer_id = req.customer_id;
  msg.meta.request     = false;
  msg.meta.push        = req.push;
  msg.meta.head        = req.cmd;
  msg.meta.timestamp   = req.timestamp;
  msg.meta.recver      = req.sender;
  msg.meta.key         = req.key;
  msg.meta.version     = req.version;
  Postoffice::Get()->van()->Send(msg);
}

template <typename Val>
void KVWorker<Val>::AutoPullUpdate(const int version, const int iters, const int req, const KVPairs<Val>& kvs) {
  int throughput = -1;
  int last_recv_id = -1;
  auto* van = Postoffice::Get()->van();

  while(true) {
    int receiver = van->GetReceiver(throughput, last_recv_id, iters);
    if(receiver == -1) break;

    if(kvs.keys.size()) {
      Message msg;
      msg.meta.app_id      = obj_->app_id();
      msg.meta.customer_id = obj_->customer_id();
      msg.meta.request     = true;
      msg.meta.push        = false;
      msg.meta.sender      = van->my_node_.id;
      msg.meta.recver      = receiver;
      msg.meta.key         = req;
      msg.meta.version     = version;
      msg.meta.iters       = iters;
      msg.meta.timestamp   = -1;

      msg.AddData(kvs.keys);
      msg.AddData(kvs.vals);
      if (kvs.lens.size()) {
        msg.AddData(kvs.lens);
      }

      auto start_time = clock();
      van->Send(msg);
      van->WaitForFinish();
      auto duration = (double)(clock() - start_time);
      throughput = (int)(CLOCKS_PER_SEC / duration);
      last_recv_id = receiver;
    }
  }
}

template <typename Val>
void KVWorker<Val>::AutoPullReply(const int sender){
  Message reply;
  reply.meta.recver = sender;
  reply.meta.control.cmd = Control::AUTOPULLREPLY;
  Postoffice::Get()->van()->Send(reply);
}

template <typename Val>
void KVWorker<Val>::Process(const Message& msg) {
  if (msg.meta.simple_app) {
    SimpleApp::Process(msg); return;
  }
  // store the data for pulling
  int ts = msg.meta.timestamp;
  if ((!msg.meta.push && msg.data.size()) || (enable_p3 && msg.data.size())) {
    CHECK_GE(msg.data.size(), (size_t)2);
    KVPairs<Val> kvs;
    kvs.keys = msg.data[0];
    kvs.vals = msg.data[1];
    if (msg.data.size() > (size_t)2) {
      kvs.lens = msg.data[2];
    }
    mu_.lock();
    recv_kvs_[ts].push_back(kvs);
    mu_.unlock();
  }
  // finished, run callbacks
  if (obj_->NumResponse(ts) == Postoffice::Get()->num_servers() - 1)  {
    RunCallback(ts);
  }
}

template <typename Val>
void KVWorker<Val>::TS_Process(const Message& msg) {
  CHECK_EQ(enable_intra_ts, 1);
  if (msg.meta.simple_app) {
    SimpleApp::Process(msg);
    return;
  }

  // store the data for pulling
  int ts = msg.meta.timestamp;
  int key = msg.meta.key;

  if (msg.data.size()) {
    if(msg.meta.push && msg.meta.request){
      KVMeta meta;
      meta.cmd         = msg.meta.head;
      meta.push        = msg.meta.push;
      meta.sender      = msg.meta.sender;
      meta.timestamp   = msg.meta.timestamp;
      meta.customer_id = msg.meta.customer_id;
      meta.key         = msg.meta.key;
      meta.version     = msg.meta.version;
      meta.num_merge   = msg.meta.iters;

      KVPairs<Val> kvs;
      kvs.keys = msg.data[0];
      kvs.vals = msg.data[1];
      kvs.lens = msg.data[2];

      request_handle_(meta, kvs, this);
      Postoffice::Get()->van()->AskForReceiverPush(msg.meta.app_id, msg.meta.customer_id, msg.meta.timestamp);
    } else {
      CHECK_GE(msg.data.size(), (size_t)2);
      KVPairs<Val> kvs;
      kvs.keys = msg.data[0];
      kvs.vals = msg.data[1];
      if (msg.data.size() > (size_t)2) {
        kvs.lens = msg.data[2];
        CHECK_EQ(kvs.keys.size(), kvs.lens.size());
      }
      if (msg.meta.request) {
        CHECK_EQ(kvs.keys.size(), (size_t)1);
        if(enable_intra_ts) {
          AutoPullReply(msg.meta.sender);
          AutoPullUpdate(msg.meta.version, msg.meta.iters, msg.meta.key, kvs);
        }
        ts_mu_.lock();
        auto_pull_kvs_[key][kvs.keys[0]] = kvs;
        ts_mu_.unlock();
        cond_.notify_all();
        return;
      } else {
        ts_mu_.lock();
        recv_kvs_[ts].push_back(kvs);
        ts_mu_.unlock();
      }
    }
  } else if (msg.meta.push && msg.meta.request) {
    send_push = msg.meta.iters;
    KVMeta meta;
    meta.num_merge = -1;
    KVPairs<char> kvs;
    request_handle_(meta, kvs, this);
  }
  // finished, run callbacks
  if (!msg.meta.request && obj_->NumResponse(ts) == Postoffice::Get()->num_servers() - 1)  {
    RunCallback(ts);
  }
}

template <typename Val>
void KVServer<Val>::Process(const Message& msg) {
  if (msg.meta.simple_app) {
    SimpleApp::Process(msg);
    return;
  }
  KVMeta meta;
  meta.cmd         = msg.meta.head;
  meta.push        = msg.meta.push;
  meta.sender      = msg.meta.sender;
  meta.timestamp   = msg.meta.timestamp;
  meta.customer_id = msg.meta.customer_id;
  meta.key         = msg.meta.key;
  meta.version     = msg.meta.version;
  meta.num_merge   = (msg.meta.iters==Meta::kEmpty)?1:msg.meta.iters;
  meta.app_id      = msg.meta.request;
  KVPairs<Val> data;
  int n = msg.data.size();
  if (n) {
    CHECK_GE(n, 2);
    data.keys = msg.data[0];
    data.vals = msg.data[1];
    if (n > 2) {
      CHECK_EQ(n, 3);
      data.lens = msg.data[2];
      CHECK_EQ(data.lens.size(), data.keys.size());
    }
  }

  // notice here to normalize it later !!!warning!!! number of field should less than 50
  if (enable_intra_ts && msg.meta.sender > 100 && msg.meta.push && msg.meta.request) {
      Postoffice::Get()->van()->AskForReceiverPush(msg.meta.app_id, msg.meta.customer_id, msg.meta.timestamp);
  }

  if (enable_inter_ts && !Postoffice::Get()->is_global_server() && \
      msg.meta.sender < 100 && !msg.meta.push && msg.data.size() && \
      msg.meta.request) {
    AutoPullReply(msg.meta.sender);
    AutoPullUpdate2(msg.meta.version, msg.meta.iters, msg.meta.key,msg.meta.head, data);
  }

  if (enable_inter_ts && Postoffice::Get()->is_global_server() && msg.meta.sender < 100 && \
      msg.meta.push && msg.meta.request) {
    Postoffice::Get()->van()->AskForReceiverPush(msg.meta.app_id, msg.meta.customer_id, msg.meta.timestamp, true);
  }

  if (enable_inter_ts && !Postoffice::Get()->is_global_server() && msg.meta.sender < 100 &&  \
      msg.meta.push && msg.meta.request) {
    if(msg.data.size()){
      KVMeta meta;
      meta.cmd         = msg.meta.head;
      meta.push        = msg.meta.push;
      meta.sender      = msg.meta.sender;
      meta.timestamp   = msg.meta.timestamp;
      meta.customer_id = msg.meta.customer_id;
      meta.key         = msg.meta.key;
      meta.version     = msg.meta.version;
      meta.num_merge   = msg.meta.iters;

      KVPairs<Val> kvs;
      kvs.keys = msg.data[0];
      kvs.vals = msg.data[1];
      kvs.lens = msg.data[2];

      request_handle_global(meta, kvs, this);
      Postoffice::Get()->van()->AskForReceiverPush(msg.meta.app_id, msg.meta.customer_id, msg.meta.timestamp, true);
    } else {
      send_push = msg.meta.iters;
      KVMeta meta;
      meta.num_merge = -1;
      KVPairs<char> kvs;
      request_handle_global(meta,kvs,this);
    }
  }
  CHECK(request_handle_);
  if(!(enable_inter_ts && !Postoffice::Get()->is_global_server() && msg.meta.sender < 100 && \
     msg.meta.push && msg.meta.request)) {
    request_handle_(meta, data, this);
  }
}

template <typename Val>
void KVServer<Val>::AutoPullReply(const int sender){
  Message rpy;
  rpy.meta.recver = sender;
  rpy.meta.control.cmd = Control::AUTOPULLREPLY;
  Postoffice::Get()->van()->Send(rpy,true);
}

template <typename Val>
void KVWorker<Val>::RunCallback(int timestamp) {
  mu_.lock();
  auto it = callbacks_.find(timestamp);
  if (it != callbacks_.end()) {
    mu_.unlock();

    CHECK(it->second);
    it->second();

    mu_.lock();
    callbacks_.erase(it);
  }
  mu_.unlock();
}

template <typename Val>
void KVServer<Val>::RunCallback(int timestamp) {
  mu_.lock();
  auto it = callbacks_.find(timestamp);
  if (it != callbacks_.end()) {
    mu_.unlock();

    CHECK(it->second);
    it->second();

    mu_.lock();
    callbacks_.erase(it);
  }
  mu_.unlock();
}

template <typename Val>
template <typename C, typename D>
int KVWorker<Val>::Pull_(const SArray<Key>& keys, C* vals, D* lens, int cmd, const Callback& cb) {
  int ts = obj_->NewRequest(kServerGroup);
  AddCallback(ts, [this, ts, keys, vals, lens, cb]() mutable {
    mu_.lock();
    auto& kvs = recv_kvs_[ts];
    mu_.unlock();

    // do check
    size_t total_key = 0, total_val = 0;
    for (const auto& s : kvs) {
      Range range = FindRange(keys, s.keys.front(), s.keys.back() + 1);
      CHECK_EQ(range.size(), s.keys.size())
          << "unmatched keys size from one server";
      if (lens) CHECK_EQ(s.lens.size(), s.keys.size());
      total_key += s.keys.size();
      total_val += s.vals.size();
    }
    CHECK_EQ(total_key, keys.size()) << "lost some servers?";

    // fill vals and lens
    std::sort(kvs.begin(), kvs.end(), [](
        const KVPairs<Val>& a, const KVPairs<Val>& b) {
      return a.keys.front() < b.keys.front();
    });
    CHECK_NOTNULL(vals);
    if (vals->empty()) {
      vals->resize(total_val);
    } else {
      CHECK_EQ(vals->size(), total_val);
    }
    Val* p_vals = vals->data();
    int* p_lens = nullptr;
    if (lens) {
      if (lens->empty()) {
        lens->resize(keys.size());
      } else {
        CHECK_EQ(lens->size(), keys.size());
      }
      p_lens = lens->data();
    }
    for (const auto& s : kvs) {
      memcpy(p_vals, s.vals.data(), s.vals.size() * sizeof(Val));
      p_vals += s.vals.size();
      if (p_lens) {
        memcpy(p_lens, s.lens.data(), s.lens.size() * sizeof(int));
        p_lens += s.lens.size();
      }
    }

    mu_.lock();
    recv_kvs_.erase(ts);
    mu_.unlock();
    if (cb) cb();
  });

  KVPairs<Val> kvs;
  kvs.keys = keys;
  Send(ts, false, cmd, kvs);
  return ts;
}

template <typename Val>
int KVWorker<Val>::AutoPull(int uniq_key, const SArray <Key> &keys, SArray <Val> *vals, SArray<int> *lens,
                            int cmd, const std::function<void()> &cb) {
  // Wait until we have enough key-value pairs for the unique key.
  std::unique_lock<std::mutex> lk(ts_cond);
  while(auto_pull_kvs_[uniq_key].size() != keys.size()){
    cond_.wait(lk);
  }


  auto& autokvs = auto_pull_kvs_[uniq_key];
  Val* p_vals = vals->data();
  int* p_lens = nullptr;
  size_t total_vals = 0;

  // do checks
  for(auto& kvs : autokvs){
    total_vals += kvs.second.vals.size();
  }
  CHECK_NOTNULL(vals);
  if (vals->empty()) {
    vals->resize(total_vals);
  } else {
    CHECK_EQ(vals->size(), total_vals);
  }
  if (lens) {
    if (lens->empty()) {
        lens->resize(keys.size());
    } else {
        CHECK_EQ(lens->size(), keys.size());
    }
    p_lens = lens->data();
  }
  // fill vals and lens
  for (size_t i = 0; i < keys.size(); i++){
    memcpy(p_vals, autokvs[keys[i]].vals.data(), autokvs[keys[i]].vals.size() * sizeof(Val));
    p_vals += autokvs[keys[i]].vals.size();
    if (p_lens) {
      memcpy(p_lens, autokvs[keys[i]].lens.data(), autokvs[keys[i]].lens.size() * sizeof(int));
      p_lens += autokvs[keys[i]].lens.size();
    }
  }
  // erase used kvs
  auto_pull_kvs_.erase(uniq_key);
  // run callback
  if(cb) cb();
  // return data_version_[uniq_key];
  return 0;
}

}  // namespace ps
#endif  // PS_KV_APP_H_
