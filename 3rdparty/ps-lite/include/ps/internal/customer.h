/**
 *  Copyright (c) 2015 by Contributors
 *  Modifications Copyright (c) 2021 by Contributors at INET-RC
 */
#ifndef PS_INTERNAL_CUSTOMER_H_
#define PS_INTERNAL_CUSTOMER_H_
#include <mutex>
#include <vector>
#include <utility>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <thread>
#include <memory>
#include <algorithm>
#include "ps/internal/message.h"
#include "ps/internal/threadsafe_queue.h"
namespace ps {

/**
 * \brief The object for communication.
 *
 * As a sender, a customer tracks the responses for each request sent.
 *
 * It has its own receiving thread which is able to process any message received
 * from a remote node with `msg.meta.customer_id` equal to this customer's id
 */
class Customer {
 public:
  /**
   * \brief the handle for a received message
   * \param recved the received message
   */
  using RecvHandle = std::function<void(const Message& recved)>;

  /**
   * \brief constructor
   * \param app_id the globally unique id indicating the application the postoffice
   *               serving for
   * \param customer_id the locally unique id indicating the customer of a postoffice
   * \param recv_handle the functino for processing a received message
   * \param is_server whether this node is a server / global server node
   */
  Customer(int app_id, int customer_id, const RecvHandle& recv_handle, bool is_server = false);

  /**
   * \brief desconstructor
   */
  ~Customer();

  /**
   * \brief return the globally unique application id
   */
  inline int app_id() { return app_id_; }


  /**
   * \brief return the locally unique customer id
   */
  inline int customer_id() { return customer_id_; }

  /**
   * \brief get a timestamp for a new request. threadsafe
   * \param recver the receive node id of this request
   * \return the timestamp of this request
   */
  int NewRequest(int recver, const bool is_global = false);


  /**
   * \brief wait until the request is finished. threadsafe
   * \param timestamp the timestamp of the request
   */
  void WaitRequest(int timestamp);

  /**
   * \brief return the number of responses received for the request. threadsafe
   * \param timestamp the timestamp of the request
   */
  int NumResponse(int timestamp);

  /**
   * \brief add a number of responses to timestamp
   */
  void AddResponse(int timestamp, int num = 1);

  /**
   * \brief accept a received message from \ref Van. threadsafe
   * \param recved the received the message
   */
  inline void Accept(const Message& recved) {
    static const std::vector<int> valid_heads = {0, 2, 3, 5, 6};
    bool is_head_valid = std::find(
      valid_heads.begin(), valid_heads.end(), recved.meta.head) != valid_heads.end();
    if (is_head_valid && recved.meta.sender % 2 == 1 && recved.meta.request == true
      && recved.meta.push == false && recved.meta.simple_app == false) {
      recv_pull_queue_.Push(recved);
    } else {
      recv_queue_.Push(recved);
    }
  }

 private:
  /**
   * \brief the thread functions
   */
  void Receiving();
  void ReceivingPull();

  int app_id_;
  int customer_id_;

  RecvHandle recv_handle_;
  ThreadsafeQueue recv_queue_, recv_pull_queue_;
  std::unique_ptr<std::thread> recv_thread_, recv_pull_thread_;

  std::mutex tracker_mu_;
  std::condition_variable tracker_cond_;
  std::vector<std::pair<int, int>> tracker_;

  DISALLOW_COPY_AND_ASSIGN(Customer);
};

}  // namespace ps
#endif  // PS_INTERNAL_CUSTOMER_H_
