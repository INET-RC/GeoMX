/**
 *  Copyright (c) 2015 by Contributors
 *  Modifications Copyright (c) 2021 by Contributors at INET-RC
 */
#ifndef PS_INTERNAL_MESSAGE_H_
#define PS_INTERNAL_MESSAGE_H_
#include <vector>
#include <limits>
#include <string>
#include <sstream>
#include "ps/sarray.h"
namespace ps {
/** \brief data type */
    enum DataType {
        CHAR, INT8, INT16, INT32, INT64,
        UINT8, UINT16, UINT32, UINT64,
        FLOAT, DOUBLE, OTHER
    };
/** \brief data type name */
    static const char* DataTypeName[] = {
            "CHAR", "INT8", "INT16", "INT32", "INT64",
            "UINT8", "UINT16", "UINT32", "UINT64",
            "FLOAT", "DOUBLE", "OTHER"
    };
/**
 * \brief compare if V and W are the same type
 */
    template<typename V, typename W>
    inline bool SameType() {
      return std::is_same<typename std::remove_cv<V>::type, W>::value;
    }
/**
 * \brief return the DataType of V
 */
    template<typename V>
    DataType GetDataType() {
      if (SameType<V, int8_t>()) {
        return INT8;
      } else if (SameType<V, int16_t>()) {
        return INT16;
      } else if (SameType<V, int32_t>()) {
        return INT32;
      } else if (SameType<V, int64_t>()) {
        return INT64;
      } else if (SameType<V, uint8_t>()) {
        return UINT8;
      } else if (SameType<V, uint16_t>()) {
        return UINT16;
      } else if (SameType<V, uint32_t>()) {
        return UINT32;
      } else if (SameType<V, uint64_t>()) {
        return UINT64;
      } else if (SameType<V, float>()) {
        return FLOAT;
      } else if (SameType<V, double>()) {
        return DOUBLE;
      } else {
        return OTHER;
      }
    }
/**
 * \brief information about a node
 */
    struct Node {
        /** \brief the empty value */
        static const int kEmpty;
        /** \brief default constructor */
        Node() : id(kEmpty), port(kEmpty), is_recovery(false) {}
        /** \brief node roles */
        enum Role { SERVER, WORKER, SCHEDULER, GLOBAL_SERVER, GLOBAL_SCHEDULER };
        /** \brief get debug string */
        std::string DebugString() const {
          std::stringstream ss;
          ss << "role=" << (role == SERVER ? "server" : (role == WORKER ? "worker" : (role == SCHEDULER ? "scheduler" : \
                     (role == GLOBAL_SERVER ? "global server" : "global scheduler"))))
             << (id != kEmpty ? ", id=" + std::to_string(id) : "")
             << ", ip=" << hostname << ", port=" << port << ", is_recovery=" << is_recovery;
          for(int i = 0; i< udp_port.size(); ++i){
            ss << "udp[channel "<< i+1 << "] port = " << udp_port[i];
          }
          return ss.str();
        }
        /** \brief get short debug string */
        std::string ShortDebugString() const {
          std::string str = role == SERVER ? "S" : (role == WORKER ? "W" : (role == SCHEDULER ? "H" : \
                     (role == GLOBAL_SERVER ? "GS" : "GH")));
          if (id != kEmpty) str += "[" + std::to_string(id) + "]";
          return str;
        }
        /** \brief the role of this node */
        Role role;
        /** \brief node id */
        int id;
        /** \brief customer id */
        int customer_id;
        /** \brief hostname or ip */
        std::string hostname;
        /** \brief the port this node is binding */
        int port;
        /** \brief whether this node is created by failover */
//begin, added by huaman
        std::vector<int> udp_port;
//end, added by huaman
        bool is_recovery;
    };
/**
 * \brief meta info of a system control message
 */
    struct Control {
        /** \brief empty constructor */
        Control() : cmd(EMPTY) { }
        /** \brief return true is empty */
        inline bool empty() const { return cmd == EMPTY; }
        /** \brief get debug string */
        std::string DebugString() const {
          if (empty()) return "";
          std::vector<std::string> cmds = {
                  "EMPTY", "TERMINATE", "ADD_NODE", "ADD_GLOBAL_NODE", "BARRIER", "BARRIER_GLOBAL", "ACK", "HEARTBEAT","AUTOPULLRPY","ASK","REPLY", "ASK1"};
          std::stringstream ss;
          ss << "cmd=" << cmds[cmd];
          if (node.size()) {
            ss << ", node={";
            for (const Node& n : node) ss << " " << n.DebugString();
            ss << " }";
          }
          if (cmd == BARRIER) ss << ", barrier_group=" << barrier_group;
          if (cmd == ACK) ss << ", msg_sig=" << msg_sig;
          return ss.str();
        }
        /** \brief all commands */
        enum Command { EMPTY, TERMINATE, ADD_NODE, ADD_GLOBAL_NODE, BARRIER, BARRIER_GLOBAL, ACK, HEARTBEAT ,AUTOPULLRPY,ASK, REPLY, ASK1};
        /** \brief the command */
        Command cmd;
        /** \brief node infos */
        std::vector<Node> node;
        /** \brief the node group for a barrier, such as kWorkerGroup */
        int barrier_group;
        /** message signature */
        uint64_t msg_sig;
    };
/**
 * \brief meta info of a message
 */
    struct Meta {
        /** \brief the empty value */
        static const int kEmpty;
        /** \brief default constructor */
        Meta() : head(kEmpty), app_id(kEmpty), customer_id(kEmpty),
                 timestamp(kEmpty),\
           first_key(0),\
           seq(0),\
           seq_begin(0),\
           seq_end(0),\
           msg_type(0),\
           push_op_num(0),\
           val_bytes(0),\
           total_bytes(0),\
           channel(0),\
           keys_len(0),\
           vals_len(0),\
           lens_len(0),\
           tos(0),\
           bits_num(32),\
           priority(0),\
           sender(kEmpty),\
           recver(kEmpty),\
           request(false),\
           push(false),\
           key(kEmpty),\
           version(kEmpty),\
           iters(kEmpty),\
           simple_app(false){}//
        std::string DebugString() const {
          std::stringstream ss;
          if (sender == Node::kEmpty) {
            ss << "I";
          } else {
            ss << (sender == 1 ? "(scheduler)" : (sender % 2 == 0 ? "(server)" : "(worker)")) << sender;
          }
          ss <<  " => " << (recver == 1 ? "(scheduler)" : (recver % 2 == 0 ? "(server)" : "(worker)")) << recver;
          ss << ". Meta: request=" << request;


          if (timestamp != kEmpty) ss << ", timestamp=" << timestamp;
          ss << ", first_key = " << first_key;
          ss << ", seq = " << seq;
          ss << ", seq_begin = " << seq_begin;
          ss << ", seq_end = " << seq_end;
          ss << ", channel = " << channel;
          ss << ", msg_type = " << msg_type;
          ss << ", push_op_num = " << push_op_num;
          ss << ", val_bytes = " << val_bytes;
          ss << ", total_bytes = " << total_bytes;
          ss << ", keys_len = " << keys_len;
          ss << ", vals_len = " << vals_len;
          ss << ", lens_len = " << lens_len;
          ss << ", tos = " << tos;
          ss << ", key=" << key;
          ss << ", version=" << version;
          ss << ", iters=" <<iters;
          if(compr.size()){
            ss << ", compr = [";
            for(auto v : compr) ss << " " << v;
            ss << " ]";
          }
          ss << ", bits_num = " << bits_num;
          if (!control.empty()) {
            ss << ", control={ " << control.DebugString() << " }";
          } else {
            ss << ", simple_app=" << simple_app
               << ", push=" << push;
          }
          if (head != kEmpty) ss << ", head=" << head;
          if (body.size()) ss << ", body=" << body;
          if (data_type.size()) {
            ss << ", data_type={";
            for (auto d : data_type) ss << " " << DataTypeName[static_cast<int>(d)];
            ss << " }";
          }
          return ss.str();
        }
        /** \brief an int head */
        int head;
        /** \brief the unique id of the application of messsage is for*/
        int app_id;
        /** \brief customer id*/
        int customer_id;
        /** \brief the timestamp of this message */
        int timestamp;
//begin, added by huaman

        int first_key;   //used for calculate resender_key
        int seq;
        int seq_begin;
        int seq_end;
        int msg_type;     //point that the type of msg, global_push: 1 global_pull:0 default:0
        int push_op_num;
        int val_bytes;
        int total_bytes;
        int channel;
        int keys_len;
        int vals_len;
        int lens_len;
        int tos;
        std::vector<float> compr;
        int bits_num;
        int priority;
//end, added by huaman
        /** \brief the node id of the sender of this message */
        int sender;
        /** \brief the node id of the receiver of this message */
        int recver;
        /** \brief whether or not this is a request message*/
        bool request;
        /** \brief whether or not a push message */
        bool push;
        /** \brief add by cqq, unique key of the kvs */
        int key;
        /** \brief add by cqq, version of data */
        int version;
        /** \brief added by vbc, iteration of training */
        int iters;
        /** \brief whether or not it's for SimpleApp */
        bool simple_app;
        /** \brief an string body */
        std::string body;
        /** \brief data type of message.data[i] */
        std::vector<DataType> data_type;
        /** \brief system control message */
        Control control;
    };
/**
 * \brief messages that communicated amaong nodes.
 */
    struct Message {
//begin, added by huaman
        float contri;
        float p_loss;
        int rank;
//end, added by huaman
        /** \brief the meta info of this message */
        Meta meta;
        /** \brief the large chunk of data of this message */
        std::vector<SArray<char> > data;
        /**
         * \brief push array into data, and add the data type
         */
        template <typename V>
        void AddData(const SArray<V>& val) {
          CHECK_EQ(data.size(), meta.data_type.size());
          meta.data_type.push_back(GetDataType<V>());
          data.push_back(SArray<char>(val));
        }
        std::string DebugString() const {
          std::stringstream ss;
          ss << meta.DebugString();
          if (data.size()) {
            ss << " Body:";
            for (const auto& d : data) ss << " data_size=" << d.size();
          }
          return ss.str();
        }
    };
}  // namespace ps
#endif  // PS_INTERNAL_MESSAGE_H_
