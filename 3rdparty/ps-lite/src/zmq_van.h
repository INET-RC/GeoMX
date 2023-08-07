/**
 *  Copyright (c) 2015 by Contributors
 *  Modifications Copyright (c) 2021 by Contributors at INET-RC
 */
#ifndef PS_ZMQ_VAN_H_
#define PS_ZMQ_VAN_H_
#include <zmq.h>
#include <stdlib.h>
#include <thread>
#include <string>
#include "ps/internal/van.h"
#include <assert.h>
#include <stdlib.h>                   
#if _MSC_VER
#define rand_r(x) rand()
#endif

namespace ps {
/**
 * \brief be smart on freeing recved data
 */
inline void FreeData(void *data, void *hint) {
  if (hint == NULL) {
    delete [] static_cast<char*>(data);
  } else {
    delete static_cast<SArray<char>*>(hint);
  }
}

inline void FreeData_malloc(void *data, void *hint) {
  if (hint == NULL) {
    free(static_cast<char*>(data));
  } else {
    free(static_cast<SArray<char>*>(hint));
  }
}

/**
 * \brief ZMQ based implementation
 */
class ZMQVan : public Van {
 public:
  ZMQVan() { }
  virtual ~ZMQVan() { }

 protected:
  void Start(int customer_id) override {
    start_mu_.lock();
    if (context_ == nullptr) {
      context_ = zmq_ctx_new();
      CHECK(context_ != NULL) << "create 0mq context failed";
      zmq_ctx_set(context_, ZMQ_MAX_SOCKETS, 65536);
    }
    start_mu_.unlock();
    Van::Start(customer_id);
  }

  void Stop(const bool is_global = false) override {
    auto& my_node = is_global ? my_node_global_ : my_node_;
    PS_VLOG(1) << my_node.ShortDebugString() << " is stopping";
    if (is_global && Postoffice::Get()->is_server()) {
      PS_VLOG(1) << my_node_.ShortDebugString() << " is stopping";
    }

    Van::Stop(is_global);

    // close sockets
    int linger = 0;
    auto& receiver = is_global ? receiver_global_ : receiver_;
    auto& senders = is_global ? senders_global_ : senders_;
    int rc = zmq_setsockopt(receiver, ZMQ_LINGER, &linger, sizeof(linger));
    CHECK(rc == 0 || errno == ETERM);
    CHECK_EQ(zmq_close(receiver), 0);
    if (is_global && Postoffice::Get()->is_server()) {
      int rc = zmq_setsockopt(receiver_, ZMQ_LINGER, &linger, sizeof(linger));
      CHECK(rc == 0 || errno == ETERM);
      CHECK_EQ(zmq_close(receiver_), 0);
      for (auto &it : senders_) {
        int rc = zmq_setsockopt(it.second, ZMQ_LINGER, &linger, sizeof(linger));
        CHECK(rc == 0 || errno == ETERM);
        CHECK_EQ(zmq_close(it.second), 0);
      }
      senders_.clear();
    }
    for (auto &it : senders) {
      int rc = zmq_setsockopt(it.second, ZMQ_LINGER, &linger, sizeof(linger));
      CHECK(rc == 0 || errno == ETERM);
      CHECK_EQ(zmq_close(it.second), 0);
    }
    senders.clear();
    zmq_ctx_destroy(context_);
    context_ = nullptr;
  }

  std::vector<int> Bind_UDP(const Node& node, int max_retry) override {
    std::vector<int> tmp_udp_port;
    for(unsigned int i = 0; i < node.udp_port.size(); ++i) {
      udp_receiver_ = zmq_socket(context_, ZMQ_ROUTER);
      CHECK(udp_receiver_ != NULL) << "create udp_receiver ["
        << i << "] socket failed: " << zmq_strerror(errno);
      int udp_recv_buf_size = 4 * 1024 * 1024;
      int rc = zmq_setsockopt(udp_receiver_, ZMQ_RCVBUF, &udp_recv_buf_size, sizeof(udp_recv_buf_size));
      assert(rc == 0);
      int check_rcv_buff = 0;
      size_t check_len = sizeof(check_rcv_buff);
      rc = zmq_getsockopt(udp_receiver_, ZMQ_RCVBUF, &check_rcv_buff, &check_len);

      int local = GetEnv("DMLC_LOCAL", 0);
      std::string hostname = node.hostname.empty() ? "*" : node.hostname;
      int use_kubernetes = GetEnv("DMLC_USE_KUBERNETES", 0);
      if (use_kubernetes > 0 && node.role == Node::SCHEDULER) {
        hostname = "0.0.0.0";
      }
      std::string udp_addr = local ? "ipc:///tmp/" : "udp://" + hostname + ":";

      int udp_port = node.udp_port[i];
      unsigned seed = static_cast<unsigned>(time(NULL) + udp_port);
      for (int i = 0; i < max_retry + 1; ++i) {
        auto address = udp_addr + std::to_string(udp_port);
        if (zmq_bind(udp_receiver_, address.c_str()) == 0) break;
        if (i == max_retry) {
          udp_port = -1;
        } else {
          udp_port = 10000 + rand_r(&seed) % 40000;
        }
      }
      PS_VLOG(1) << "Bind udp channel [" << i + 1 << "] success." << std::endl;
      udp_receiver_vec.push_back(udp_receiver_);
      tmp_udp_port.push_back(udp_port);
    }
    return tmp_udp_port;
  }

  void Connect_UDP(const Node& node) override {
    CHECK_NE(node.id, node.kEmpty);
    CHECK(node.hostname.size());
    int id = node.id;
    auto it = udp_senders_.find(id);
    if (it != udp_senders_.end()) {
      for (unsigned int i = 0; i < it->second.size(); ++i)
        zmq_close(it->second[i]);
    }
    // worker doesn't need to connect to the other workers. same for server
    if ((node.role == my_node_global_.role) &&
        (node.id != my_node_global_.id)) return;

    for(unsigned int i = 0; i < node.udp_port.size(); ++i){
      PS_VLOG(1) << node.udp_port[i];
      void *udp_sender = zmq_socket(context_, ZMQ_DEALER);
      CHECK(udp_sender != NULL)
        << zmq_strerror(errno)
        << ". it often can be solved by \"sudo ulimit -n 65536\""
        << " or edit /etc/security/limits.conf";
      if (my_node_global_.id != Node::kEmpty) {
        std::string my_id = "ps" + std::to_string(my_node_global_.id);
        zmq_setsockopt(udp_sender, ZMQ_IDENTITY, my_id.data(), my_id.size());
        int tos = (node.udp_port.size() - i - 1) * 32;
        if(zmq_setsockopt(udp_sender, ZMQ_TOS, &tos, sizeof(tos)) == 0) {
          int dscp = (node.udp_port.size() - i - 1) * 8;
          std::string command = "iptables -t mangle -A OUTPUT -p udp --dst "
            + node.hostname + " --dport "+ std::to_string(node.udp_port[i])
            + " -j DSCP --set-dscp " + std::to_string(dscp);
          std::cout << "command = " << command << std::endl;
          CHECK_NE(system(command.c_str()), -1) << "Execute system command failed";
          std::cout << "Success to set " << "udp[" << i + 1 << "]:"
            << my_node_global_.id << "=>" << node.id << "("
            << node.hostname.c_str() << ":" << node.udp_port[i]
            << "):" << "tos=" << tos << std::endl;
        } else {
          std::cout << "Failed to set " << "udp[" << i + 1 << "]:"
            << my_node_global_.id << "=>" << node.id << "("
            << node.hostname.c_str() << ":" << node.udp_port[i]
            << "):" << "tos=" << tos << std::endl;
        }
      }

      int udp_send_buf_size = 4 * 1024 * 1024;
      int rc = zmq_setsockopt(udp_sender, ZMQ_SNDBUF, &udp_send_buf_size, sizeof(udp_send_buf_size));
      assert(rc == 0);

      // connect
      std::string addr = "udp://" + node.hostname + ":" + std::to_string(node.udp_port[i]);

      if (GetEnv("DMLC_LOCAL", 0)) {
        addr = "ipc:///tmp/" + std::to_string(node.udp_port[i]);
      }
      if (zmq_connect(udp_sender, addr.c_str()) != 0) {
        PS_VLOG(1) << "UDP[channel " << i + 1 << "]:connect to "
          + addr + " failed: " + zmq_strerror(errno);
      } else {
        PS_VLOG(1) << "UDP[channel " << i + 1 << "]:connect to "
          + addr + " success.";
      }
      udp_senders_[id].push_back(udp_sender);
    }
  }

  int SendMsg_UDP(int channel, const Message& msg, int tag) override {
    std::lock_guard<std::mutex> lk(mu_);
    // find the socket
    int id = msg.meta.recver;
    CHECK_NE(id, Meta::kEmpty);
    auto it = udp_senders_.find(id);
    if (it == udp_senders_.end()) {
      LOG(WARNING) << "Udp:there is no socket to node " << id;
      return -1;
    }
    void *socket = it->second[channel];
    int meta_size;
    char* meta_buf;
    int n = msg.data.size();

    PackMeta(msg.meta, &meta_buf, &meta_size,true);

    size_t tot_bytes = 0;
    size_t addr_offset = 0;
    int send_bytes = 0;
    tot_bytes += sizeof(meta_size);
    tot_bytes += meta_size;
    for(int i = 0; i < n; ++i){
      tot_bytes += msg.data[i].size();
    }
    char *send_buf = (char*) malloc(tot_bytes);

    memcpy(send_buf, (char*)&meta_size, sizeof(meta_size));
    addr_offset += sizeof(meta_size);
    memcpy(send_buf + addr_offset, meta_buf, meta_size);
    addr_offset += meta_size;
    for(int i = 0; i < n; ++i){
      memcpy(send_buf + addr_offset, msg.data[i].data(), msg.data[i].size());
      addr_offset += msg.data[i].size();
    }
    assert(tot_bytes == addr_offset);

    zmq_msg_t data_msg;
    zmq_msg_init_data(&data_msg, send_buf, tot_bytes, FreeData_malloc, NULL);

    while (true) {
      if (zmq_msg_send(&data_msg, socket, tag) == static_cast<ssize_t>(tot_bytes)) break;
      if (errno == EINTR) continue;
      LOG(WARNING) << "Udp:failed to send message to node [" << id
                   << "] errno: " << errno << " " << zmq_strerror(errno);
      return -1;
    }
    send_bytes = tot_bytes;
    return send_bytes;
  }

  int RecvMsg_UDP(int channel, Message* msg) override {
    msg->data.clear();
    size_t recv_bytes = 0;
    for (int i = 0; ; ++i) {
      zmq_msg_t* zmsg = new zmq_msg_t;
      CHECK(zmq_msg_init(zmsg) == 0) << zmq_strerror(errno);
      while (true) {
        if (zmq_msg_recv(zmsg, udp_receiver_vec[channel], 0) != -1) break;
        if (errno == EINTR) {
          std::cout << "interrupted";
          continue;
        }
        LOG(WARNING) << "failed to receive message. errno: "
                     << errno << " " << zmq_strerror(errno);
        return -1;
      }
      char* buf = CHECK_NOTNULL((char *)zmq_msg_data(zmsg));
      size_t size = zmq_msg_size(zmsg);
      recv_bytes += size;

      int meta_size;
      int addr_offset = 0;
      memcpy((void*)&meta_size, buf+addr_offset, sizeof(meta_size));
      addr_offset += sizeof(meta_size);

      // task
      UnpackMeta(buf + addr_offset, meta_size, &(msg->meta));
      addr_offset += meta_size;

      if(msg->meta.keys_len > 0) {
        SArray<char> data;
        data.reset(buf + addr_offset, msg->meta.keys_len, [zmsg, size](char* buf) {});
        msg->data.push_back(data);
        addr_offset += msg->meta.keys_len;
        if(msg->meta.lens_len > 0) {
          data.reset(buf + addr_offset, msg->meta.vals_len, [zmsg, size](char* buf) {});
          msg->data.push_back(data);
          addr_offset += msg->meta.vals_len;
          data.reset(buf + addr_offset, msg->meta.lens_len, [zmsg, size](char* buf) {
            zmq_msg_close(zmsg);
            delete zmsg;
          });
          msg->data.push_back(data);
          addr_offset += msg->meta.lens_len;
        } else {
          data.reset(buf + addr_offset, msg->meta.vals_len, [zmsg, size](char* buf) {
            zmq_msg_close(zmsg);
            delete zmsg;
          });
          msg->data.push_back(data);
          addr_offset += msg->meta.vals_len;
        }
      }
      break;
    }
    return recv_bytes;
  }

  int Bind(const Node& node, int max_retry, bool is_global = false) override {
    auto& receiver = is_global ? receiver_global_ : receiver_;
    if (receiver == nullptr) {
      receiver = zmq_socket(context_, ZMQ_ROUTER);
      CHECK(receiver != NULL) << "create receiver socket failed: " << zmq_strerror(errno);
    }
    int local = GetEnv("DMLC_LOCAL", 0);
    std::string hostname = node.hostname.empty() ? "*" : node.hostname;
    int use_kubernetes = GetEnv("DMLC_USE_KUBERNETES", 0);
    if (use_kubernetes > 0 && node.role == Node::SCHEDULER) {
      hostname = "0.0.0.0";
    }
    std::string addr = local ? "ipc:///tmp/" : "tcp://" + hostname + ":";
    int port = node.port;
    unsigned seed = static_cast<unsigned>(time(NULL)+port);
    for (int i = 0; i < max_retry+1; ++i) {
      auto address = addr + std::to_string(port);
      if (zmq_bind(receiver, address.c_str()) == 0) break;
      if (i == max_retry) {
        port = -1;
      } else {
        port = 10000 + rand_r(&seed) % 40000;
      }
    }
    return port;
  }

  void Connect(const Node& node, bool is_global = false) override {
    CHECK_NE(node.id, node.kEmpty);
    CHECK_NE(node.port, node.kEmpty);
    CHECK(node.hostname.size());
    int id = node.id;
    const auto& my_node = is_global ? my_node_global_ : my_node_;
    auto& senders = is_global ? senders_global_ : senders_;
    auto it = senders.find(id);
    if (it != senders.end()) {
      zmq_close(it->second);
    }
    void *sender = zmq_socket(context_, ZMQ_DEALER);
    CHECK(sender != NULL)
            << zmq_strerror(errno)
            << ". it often can be solved by \"sudo ulimit -n 65536\""
            << " or edit /etc/security/limits.conf";
    if (my_node.id != Node::kEmpty) {
      std::string my_id = "ps" + std::to_string(my_node.id);
      zmq_setsockopt(sender, ZMQ_IDENTITY, my_id.data(), my_id.size());
      int hwm = 1;
      zmq_setsockopt(sender, ZMQ_SNDHWM, &hwm, sizeof(hwm));
      if(is_global && node.udp_port.size() != 0) {
        int tos = node.udp_port.size() * 32;
        if (zmq_setsockopt(sender, ZMQ_TOS, &tos, sizeof(tos)) == 0) {
          std::cout << "Success to set " << "tcp[" << 0 << "]:"
            << my_node_.id << "=>" << node.id << "(" << node.hostname.c_str()
            << ":" << node.port << "):" << "tos=" << tos << std::endl;
        } else {
          std::cout << "Failed to set " << "tcp[" << 0 << "]:"
            << my_node_.id << "=>" << node.id << "(" << node.hostname.c_str()
            << ":" << node.port << "):" << "tos=" << tos << std::endl;
        }
      }
    }
    // connect
    std::string addr = "tcp://" + node.hostname + ":" + std::to_string(node.port);
    if (GetEnv("DMLC_LOCAL", 0)) {
      addr = "ipc:///tmp/" + std::to_string(node.port);
    }
    if (zmq_connect(sender, addr.c_str()) != 0) {
      LOG(FATAL) <<  "connect to " + addr + " failed: " + zmq_strerror(errno);
    }
    senders[id] = sender;
  }

  int SendMsg(const Message& msg, bool is_global = false) override {
    std::lock_guard<std::mutex> lk(mu_);
    // find the socket
    int id = msg.meta.recver;
    CHECK_NE(id, Meta::kEmpty);
    void *socket = nullptr;
    auto& senders = is_global ? senders_global_ : senders_;
    auto it = senders.find(id);
    if (it == senders.end()) {
      LOG(WARNING) << "there is no socket to node " << id;
      return -1;
    }
    socket = it->second;
    // for tcp-dgt
    int tos = msg.meta.tos;
    zmq_setsockopt(socket, ZMQ_TOS, &tos, sizeof(tos));
    // send meta
    int meta_size;
    char* meta_buf;
    PackMeta(msg.meta, &meta_buf, &meta_size, is_global);
    int tag = ZMQ_SNDMORE;
    int n = msg.data.size();
    if (n == 0) tag = 0;
    zmq_msg_t meta_msg;
    zmq_msg_init_data(&meta_msg, meta_buf, meta_size, FreeData, NULL);
    while (true) {
      if (zmq_msg_send(&meta_msg, socket, tag) == meta_size) break;
      if (errno == EINTR) continue;
      return -1;
    }
    int send_bytes = meta_size;
    // send data
    for (int i = 0; i < n; ++i) {
      zmq_msg_t data_msg;
      SArray<char>* data = new SArray<char>(msg.data[i]);
      int data_size = data->size();
      zmq_msg_init_data(&data_msg, data->data(), data->size(), FreeData, data);
      if (i == n - 1) tag = 0;
      while (true) {
        if (zmq_msg_send(&data_msg, socket, tag) == data_size) break;
        if (errno == EINTR) continue;
        LOG(WARNING) << "failed to send message to node [" << id
                     << "] errno: " << errno << " " << zmq_strerror(errno)
                     << ". " << i << "/" << n;
        return -1;
      }
      send_bytes += data_size;
    }
    return send_bytes;
  }

  int RecvMsg(Message* msg, bool is_global = false) override {
    auto& receiver = is_global ? receiver_global_ : receiver_;
    const auto& my_node = is_global ? my_node_global_ : my_node_;
    msg->data.clear();
    size_t recv_bytes = 0;
    for (int i = 0; ; ++i) {
      zmq_msg_t* zmsg = new zmq_msg_t;
      CHECK(zmq_msg_init(zmsg) == 0) << zmq_strerror(errno);
      while (true) {
        if (zmq_msg_recv(zmsg, receiver, 0) != -1) break;
        if (errno == EINTR) {
          std::cout << "interrupted";
          continue;
        }
        LOG(WARNING) << "failed to receive message. errno: "
                     << errno << " " << zmq_strerror(errno);
        return -1;
      }
      char* buf = CHECK_NOTNULL((char *)zmq_msg_data(zmsg));
      size_t size = zmq_msg_size(zmsg);
      recv_bytes += size;

      if (i == 0) {
        // identify
        msg->meta.sender = GetNodeID(buf, size);
        msg->meta.recver = my_node.id;
        CHECK(zmq_msg_more(zmsg));
        zmq_msg_close(zmsg);
        delete zmsg;
      } else if (i == 1) {
        // task
        UnpackMeta(buf, size, &(msg->meta));
        zmq_msg_close(zmsg);
        bool more = zmq_msg_more(zmsg);
        delete zmsg;
        if (!more) break;
      } else {
        // zero-copy
        SArray<char> data;
        data.reset(buf, size, [zmsg, size](char* buf) {
          zmq_msg_close(zmsg);
          delete zmsg;
        });
        msg->data.push_back(data);
        if (!zmq_msg_more(zmsg)) { break; }
      }
    }
    return recv_bytes;
  }

 private:
  /**
   * return the node id given the received identity
   * \return -1 if not find
   */
  int GetNodeID(const char* buf, size_t size) {
    if (size > 2 && buf[0] == 'p' && buf[1] == 's') {
      int id = 0;
      size_t i = 2;
      for (; i < size; ++i) {
        if (buf[i] >= '0' && buf[i] <= '9') {
          id = id * 10 + buf[i] - '0';
        } else {
          break;
        }
      }
      if (i == size) return id;
    }
    return Meta::kEmpty;
  }

  void *context_ = nullptr;
  /**
   * \brief node_id to the socket for sending data to this node
   */
  std::unordered_map<int, void*> senders_;
  std::unordered_map<int, void*> senders_global_;
  std::mutex mu_;
  void *receiver_ = nullptr;
  void *receiver_global_ = nullptr;
  std::unordered_map<int, std::vector<void*>> udp_senders_;
  std::vector<void *> udp_receiver_vec;
  void *udp_receiver_ = nullptr;
};
}  // namespace ps

#endif  // PS_ZMQ_VAN_H_
