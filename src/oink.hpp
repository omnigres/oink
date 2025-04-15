#pragma once

#include <map>
#include <typeindex>

#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include <boost/interprocess/sync/interprocess_upgradable_mutex.hpp>

#include <boost/container/vector.hpp>

namespace oink {
namespace bip = boost::interprocess;
namespace bc = boost::container;

template <typename T>
using allocator = bip::allocator<T, bip::managed_shared_memory::segment_manager>;

template <typename Container, typename Mutex> struct shared_container {
  using container_type = Container;
  using mutex_type = Mutex;
  Container container;
  Mutex mutex;

  template <typename... Args>
  explicit shared_container(Args &&...args) : container(std::forward<Args>(args)...) {}

  operator container_type &() { return container; }
  operator mutex_type &() { return mutex; }
};

template <typename T>
concept message = requires(T t) {
  { T::name() } -> std::same_as<const char *>;
};

struct arena {

  struct header {};

  using header_t = shared_container<header, bip::interprocess_upgradable_mutex>;

  arena(const char *segment_name, size_t segment_size)
      : segment(bip::open_or_create, segment_name, segment_size),
        header_(segment.find_or_construct<header_t>("__header")()) {}

  auto get_segment_manager() { return segment.get_segment_manager(); }

  template <typename T> auto get_allocator() {
    return bip::allocator<T, bip::managed_shared_memory::segment_manager>(
        segment.get_segment_manager());
  }

protected:
  bip::managed_shared_memory segment;

  header_t *header_;
};

struct endpoint {

  endpoint(arena &arena, const char *mq_segment_name, size_t mq_max_messages)
      : arena(arena),
        mq(bip::open_or_create, mq_segment_name, mq_max_messages, sizeof(std::size_t)),
        msgs_(arena.get_segment_manager()->find_or_construct<msg_vec>("__msgs")(
            arena.get_segment_manager())) {}

  template <typename T> auto get_allocator() { return arena.get_allocator<T>(); }

protected:
  struct msg {
    std::size_t hash;
    bip::offset_ptr<void> message_;
  };
  using msg_allocator_t = allocator<msg>;
  using msg_vec =
      shared_container<bc::vector<msg, msg_allocator_t>, bip::interprocess_upgradable_mutex>;

  arena &arena;
  bip::message_queue mq;

  msg_vec *msgs_;
};

struct sender : endpoint {
  using endpoint::endpoint;

  template <message M, typename... Args> M &send(Args &&...args) {
    auto msg_ = get_allocator<M>().allocate(1);
    std::construct_at(msg_.get(), std::forward<Args>(args)...);
    bip::scoped_lock lock(msgs_->mutex);
    auto m = msgs_->container.emplace(msgs_->container.end(),
                                      msg{std::hash<std::string>{}(M::name()), msg_});
    size_t i = msgs_->container.index_of(m);
    mq.send(&i, sizeof(i), 0);
    return *msg_;
  }
};

struct receiver : endpoint {
  using endpoint::endpoint;

  template <message... Msg> bool receive(auto visitor) {
    bip::message_queue::size_type recvd_size;
    unsigned int priority;

    std::size_t i;
    if (mq.timed_receive(&i, sizeof(i), recvd_size, priority,
                         std::chrono::system_clock::now() + std::chrono::milliseconds(500))) {

      bool matched = false;

      bip::scoped_lock lock(msgs_->mutex);
      auto &j = msgs_->container.at(i);

      (try_handle<Msg>(j, matched, visitor), ...);

      if (!matched) {
        // handle unknown message type if necessary
      } else {
      }

      if (mq.get_num_msg() == 0) {
        msgs_->container.clear();
      }

      return true;
    } else {
      return false;
    }
  }

private:
  template <message T> void try_handle(msg &j, bool &matched, auto visitor) {
    if (j.hash == std::hash<std::string>{}(T::name())) {
      visitor(*(static_cast<T *>(j.message_.get())));
      get_allocator<T>().deallocate(bip::offset_ptr<T>(static_cast<T *>(j.message_.get())), 1);
      matched = true;
    }
  }
};

} // namespace oink
