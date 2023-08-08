/**
 *  Copyright (c) 2015 by Contributors
 *  Modifications Copyright (c) 2021 by Contributors at INET-RC
 */
#ifndef PS_INTERNAL_VAN_H_
#define PS_INTERNAL_VAN_H_
#include <unordered_map>
#include <mutex>
#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <memory>
#include <atomic>
#include <ctime>
#include <unordered_set>
#include <condition_variable>
#include "ps/base.h"
#include "ps/internal/message.h"
#include "ps/internal/threadsafe_queue.h"
#include "customer.h"
#include <cmath>
#include <iostream>
namespace ps {
class Resender;

/**
 * \brief Van sends messages to remote nodes
 *
 * If environment variable PS_RESEND is set to be 1, then van will resend a
 * message if it no ACK messsage is received within PS_RESEND_TIMEOUT millisecond
 */
class Van {
 public:
  /**
   * \brief create Van
   * \param type zmq, socket, ...
   */
  static Van *Create(const std::string &type);

  /** \brief constructer, do nothing. use \ref Start for real start */
  Van() {}

  /**\brief deconstructer, do nothing. use \ref Stop for real stop */
  virtual ~Van() {}

  /**
   * \brief start van
   *
   * must call it before calling Send
   *
   * it initalizes all connections to other nodes.  start the receiving
   * threads, which keeps receiving messages. if it is a system
   * control message, give it to postoffice::manager, otherwise, give it to the
   * according app.
   */
  virtual void Start(int customer_id);
  virtual void StartGlobal(int customer_id);
  void Push(const Message& msg);
  /**
   * \brief send a message, It is thread-safe
   * \return the number of bytes sent. -1 if failed
   */
  int Send(const Message &msg, bool is_global = false);
  int DGT_Send(const Message& msg, int channel = 0, int tag = 0);
  /**
   * \brief return my node
   */
  inline const Node& my_node(bool is_global = false) const {
    const auto& ready = is_global ? ready_global_ : ready_;
    auto& my_node = is_global ? my_node_global_ : my_node_;
    CHECK(ready) << "Call Start() and StartGlobal() first";
    return my_node;
  }//

  /**
   * \brief stop van
   * stop receiving threads
   */
  virtual void Stop(bool is_global = false);

  /**
   * \brief get next available timestamp. thread safe
   */
  inline int GetTimestamp() { return timestamp_++; }

  /**
   * \brief whether it is ready for sending. thread safe
   */
  inline bool IsReady() { return ready_; }

  void Classifier(Message& msg, int channel, int tag);
  void Wait_for_finished();
  void Wait_for_global_finished();
  int GetReceiver(int throughput, int last_recv_id, int version);
  int GetGlobalReceiver(int throughput, int last_recv_id, int version);
  void AskForReceiverPush(int app, int customer1, int timestamp, bool is_global = false);
  Node my_node_, my_node_global_;

 protected:
  /**
   * \brief connect to a node
   */
  virtual void Connect(const Node &node, bool is_global = false) = 0;

  /**
   * \brief bind to my node
   * do multiple retries on binding the port. since it's possible that
   * different nodes on the same machine picked the same port
   * \return return the port binded, -1 if failed.
   */
  virtual int Bind(const Node &node, int max_retry, bool is_global = false) = 0;
  virtual std::vector<int> Bind_UDP(const Node &node, int max_retry) = 0;
  virtual void Connect_UDP(const Node &node) = 0;
  virtual int SendMsg_UDP(int channel, const Message &msg, int tag = 0) = 0;
  virtual int RecvMsg_UDP(int channel, Message *msg) = 0;

  /**
   * \brief block until received a message
   * \return the number of bytes received. -1 if failed or timeout
   */
  virtual int RecvMsg(Message *msg, bool is_global = false) = 0;

  /**
   * \brief send a mesage
   * \return the number of bytes sent
   */
  virtual int SendMsg(const Message &msg, bool is_global = false) = 0;

  /**
   * \brief pack meta into a string
   */
  void PackMeta(const Meta &meta, char **meta_buf, int *buf_size, bool is_global);

  /**
   * \brief unpack meta from a string
   */
  void UnpackMeta(const char *meta_buf, int buf_size, Meta *meta);

  Node scheduler_, global_scheduler_;
  bool is_scheduler_, is_global_scheduler_;
  std::mutex start_mu_;
  std::mutex ask_mu;
  std::mutex ask_global_mu;
  std::mutex ver_mu;
  std::mutex ver_global_mu;
  std::condition_variable ask_cond;
  std::condition_variable ask_global_cond;
  std::condition_variable ver_cond;
  std::condition_variable ver_global_cond;
  std::mutex sched;
  std::mutex sched1;
  int receiver_ = -2;
  int receiver_global = -2;
  bool ver_flag = false;
  bool ver_global_flag = false;

 private:
  void Sending();
  std::unique_ptr<std::thread> sender_thread_;

  /** thread function for receving */
  void Receiving();
  void ReceivingGlobal();
  // ask for scheduler to get receiver_id
  void AskForReceiverPull(int throughput, int last_recv_id, int version, bool is_global=false);
  /** thread function for heartbeat */
  void Heartbeat();

  // node's address string (i.e. ip:port) -> node id
  // this map is updated when ip:port is received for the first time
  std::unordered_map<std::string, int> connected_nodes_;
  // maps the id of node which is added later to the id of node
  // which is with the same ip:port and added first
  std::unordered_map<int, int> shared_node_mapping_;

  /** whether it is ready for sending */
  std::atomic<bool> ready_{false}, ready_global_{false};

  std::atomic<size_t> send_bytes_{0};
  size_t recv_bytes_ = 0;
  int num_servers_ = 0;
  int num_workers_ = 0;
  int num_global_servers_ = 0;
  int num_global_workers_ = 0;
  /** the thread for receiving messages */
  std::unique_ptr<std::thread> receiver_thread_;
  std::unique_ptr<std::thread> receiver_global_thread_;
  /** the thread for sending heartbeat */
  std::unique_ptr<std::thread> heartbeat_thread_;
  std::vector<int> barrier_count_;
  /** msg resender */
  Resender *resender_ = nullptr;
  int drop_rate_ = 0;
  std::atomic<int> timestamp_{0};
  int init_stage = 0;

  std::vector<std::vector<int>> A;
  std::vector<int> B;
  std::vector<int> B1;
  std::queue<int> ask_q;
  std::vector<std::vector<int>> lifetime;
  int iters = -1;
  double max_greed_rate;

  int enable_p3 = 0;
  void ProcessAutoPullReply();
  void ProcessAutoPullReplyGlobal();
  void ProcessAskPullCommand(Message* msg);
  void ProcessAskPullGlobalCommand(Message* msg);
  void ProcessAskPushCommand(Message* msg);
  void ProcessAskPushGlobalCommand(Message* msg);
  void ProcessReplyCommand(Message* reply);
  void ProcessReplyGlobalCommand(Message* reply);
  void Important_scheduler();
  void Unimportant_scheduler();
  int Important_send(Message& msg);
  int Unimportant_send(Message& msg);
  void Receiving_UDP(int channel);
  void MergeMsg(Message* msg1, Message* msg2);
  void MergeMsg_HALF(Message* msg1, Message* msg2);
  void encode(Message& msg, int bits_num);
  void decode(Message& msg);
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
  std::unique_ptr<std::thread> udp_receiver_thread_[8];
  std::unique_ptr<std::thread> important_scheduler_thread_;
  std::unique_ptr<std::thread> unimportant_scheduler_thread_;
  std::unordered_map<int, std::unordered_map<int, std::unordered_map<int, Message>>> msg_map;
  std::mutex merge_mu_;
  std::mutex encode_mu_;
  std::mutex decode_mu_;
  std::vector<std::unique_ptr<std::thread>> udp_receiver_thread_vec;
  ThreadsafeQueue important_queue_;
  ThreadsafeQueue unimportant_queue_;
  ThreadsafeQueue send_queue_;
  std::unordered_map<int, std::unordered_map<int, SArray<char>>> residual;

  /**
   * \brief processing logic of AddNode message for scheduler and global scheduler
   */
  void ProcessAddNodeCommandAtScheduler(Message* msg, Meta* nodes, Meta* recovery_nodes);
  void ProcessAddGlobalNodeCommandAtScheduler(Message* msg, Meta* nodes);

  /**
   * \brief processing logic of Terminate message
   */
  void ProcessTerminateCommand(bool is_global = false);

  /**
   * \brief processing logic of AddNode message (run on each node)
   */
  void ProcessAddNodeCommand(Message* msg, Meta* nodes, Meta* recovery_nodes);
  void ProcessAddGlobalNodeCommand(Message* msg, Meta* nodes);

  /**
   * \brief processing logic of Barrier message (run on each node)
   */
  void ProcessBarrierCommand(Message* msg, bool is_global = false);

  /**
   * \brief processing logic of AddNode message (run on each node)
   */
  void ProcessHeartbeat(Message* msg);

  /**
   * \brief processing logic of Data message
   */
  void ProcessDataMsg(Message* msg);

  /**
   * \brief called by ProcessAddNodeCommand, in scheduler it assigns an id to the
   *        newly added node; in other nodes, it updates the node id with what is received
   *        from scheduler
   */
  void UpdateLocalID(Message* msg, std::unordered_set<int>* deadnodes_set, Meta* nodes,
                     Meta* recovery_nodes);
  void UpdateServerID(Message* msg, Meta* nodes);

  const char *heartbeat_timeout_val = Environment::Get()->find("PS_HEARTBEAT_TIMEOUT");
  int heartbeat_timeout_ = heartbeat_timeout_val ? atoi(heartbeat_timeout_val) : 0;

  DISALLOW_COPY_AND_ASSIGN(Van);
 };
}  // namespace ps
#endif  // PS_INTERNAL_VAN_H_
