#include "doctest.h"

#include <iostream>
#include <thread>

#include <oink.hpp>

#include <boost/container/string.hpp>

template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

TEST_CASE("smoke test") {
  oink::bip::shared_memory_object::remove("oink_test");
  oink::bip::shared_memory_object::remove("oink_test_mq");
  oink::bip::remove_shared_memory_on_destroy _test("oink_test");
  oink::bip::remove_shared_memory_on_destroy _test_mq("oink_test_mq");

  struct mymsg {
    static constexpr const char *name() { return "msg"; }
    int i;
  };

  struct mymsg1 {
    static constexpr const char *name() { return "msg1"; }
    oink::bc::basic_string<char, std::char_traits<char>, oink::allocator<char>> message;

    mymsg1(const char *msg, const oink::allocator<char> &alloc) : message(msg, alloc) {}
  };

  oink::arena arena("oink_test", 65536);

  oink::sender endpoint(arena, "oink_test_mq", 1024);

  auto m = endpoint.send<mymsg>(123);
  CHECK(m->i == 123);
  mymsg1 m1 = endpoint.send<mymsg1>("allocator", endpoint.get_allocator<char>());

  CHECK(m1.message == "allocator");

  oink::receiver rendpoint(arena, "oink_test_mq", 1024);
  int received = 0;
  CHECK(rendpoint.receive<mymsg, mymsg1>(
      overloaded{[&](mymsg &msg) { received = msg.i; }, [](mymsg1 &msg) {}}));

  CHECK(received == m->i);

  std::string s;
  CHECK(rendpoint.receive<mymsg, mymsg1>(
      overloaded{[&](mymsg &msg) { received = msg.i; }, [&](mymsg1 &msg) { s = msg.message; }}));
  CHECK(s == "allocator");
}

TEST_CASE("smoke test (multithreading)") {
  oink::bip::shared_memory_object::remove("oink_test");
  oink::bip::shared_memory_object::remove("oink_test_mq");
  oink::bip::remove_shared_memory_on_destroy _test("oink_test");
  oink::bip::remove_shared_memory_on_destroy _test_mq("oink_test_mq");

  struct mymsg {
    static constexpr const char *name() { return "msg"; }
    int i;
  };

  struct stop {
    static constexpr const char *name() { return "stop"; }
  };

  oink::arena arena("oink_test", 65536 * 100);

  oink::sender endpoint(arena, "oink_test_mq", 1024);

  std::vector<int> values;
  std::thread rt([&]() {
    oink::receiver rendpoint(arena, "oink_test_mq", 1024);
    bool done = false;
    while (!done) {
      rendpoint.receive<mymsg, stop>(
          overloaded{[&](mymsg &msg) { values.push_back(msg.i); }, [&](stop &) { done = true; }});
    }
  });

  std::vector<std::thread> threads;
  for (int i = 0; i < 100; i++) {
    threads.emplace_back([&](int x) { endpoint.send<mymsg>(x); }, i);
  }

  for (auto &t : threads) {
    t.join();
  }
  endpoint.send<stop>();

  rt.join();
  CHECK(values.size() == 100);
  std::sort(values.begin(), values.end());
  for (int i = 0; auto &val : values) {
    CHECK(values[i] == i);
    i++;
  }
}

TEST_SUITE("receiver") {
  TEST_CASE("unknown message") {
    oink::bip::shared_memory_object::remove("oink_test");
    oink::bip::shared_memory_object::remove("oink_test_mq");
    oink::bip::remove_shared_memory_on_destroy _test("oink_test");
    oink::bip::remove_shared_memory_on_destroy _test_mq("oink_test_mq");

    struct mymsg {
      static constexpr const char *name() { return "msg"; }
      int i;
    };

    struct mymsg1 {
      static constexpr const char *name() { return "msg1"; }
      oink::bc::basic_string<char, std::char_traits<char>, oink::allocator<char>> message;

      mymsg1(const char *msg, const oink::allocator<char> &alloc) : message(msg, alloc) {}
    };

    oink::arena arena("oink_test", 65536);

    oink::sender endpoint(arena, "oink_test_mq", 1024);
    endpoint.send<mymsg1>("allocator", endpoint.get_allocator<char>());

    oink::receiver rendpoint(arena, "oink_test_mq", 1024);

    CHECK_THROWS_WITH_AS(
        rendpoint.receive<mymsg>(overloaded{[&](mymsg &msg) {}}),
        (std::string("unknown message ") + std::to_string(oink::message_tag<mymsg1>())).c_str(),
        oink::receiver::unknown_message);
  }

  TEST_CASE("catch all") {
    oink::bip::shared_memory_object::remove("oink_test");
    oink::bip::shared_memory_object::remove("oink_test_mq");
    oink::bip::remove_shared_memory_on_destroy _test("oink_test");
    oink::bip::remove_shared_memory_on_destroy _test_mq("oink_test_mq");

    struct mymsg {
      static constexpr const char *name() { return "msg"; }
      int i;
    };

    struct mymsg1 {
      static constexpr const char *name() { return "msg1"; }
      oink::bc::basic_string<char, std::char_traits<char>, oink::allocator<char>> message;

      mymsg1(const char *msg, const oink::allocator<char> &alloc) : message(msg, alloc) {}
    };

    oink::arena arena("oink_test", 65536);

    oink::sender endpoint(arena, "oink_test_mq", 1024);
    endpoint.send<mymsg1>("allocator", endpoint.get_allocator<char>());

    oink::receiver rendpoint(arena, "oink_test_mq", 1024);

    std::optional<std::size_t> received_hash = std::nullopt;
    rendpoint.receive(overloaded{[&](oink::receiver::msg &msg) { received_hash = msg.hash; }});
    CHECK(received_hash.has_value());
    CHECK(received_hash.value() == oink::message_tag<mymsg1>());
  }

  TEST_CASE("rescheduling") {
    oink::bip::shared_memory_object::remove("oink_test");
    oink::bip::shared_memory_object::remove("oink_test_mq");
    oink::bip::remove_shared_memory_on_destroy _test("oink_test");
    oink::bip::remove_shared_memory_on_destroy _test_mq("oink_test_mq");

    struct mymsg {
      static constexpr const char *name() { return "msg"; }
      int i;
    };

    oink::arena arena("oink_test", 65536);

    oink::sender endpoint(arena, "oink_test_mq", 1024);
    // endpoint.send<mymsg1>("allocator", endpoint.get_allocator<char>());

    oink::receiver rendpoint(arena, "oink_test_mq", 1024);

    endpoint.send<mymsg>(10);

    // let's not consume it
    CHECK(!rendpoint.receive<mymsg>(overloaded{[&](mymsg &) { return false; }}));
    // let's consume it now
    {
      CHECK(rendpoint.receive<mymsg>(overloaded{[&](mymsg &) { return true; }}));
    }
    CHECK(!rendpoint.receive<mymsg>(overloaded{[&](mymsg &) {}}));
  }

  TEST_CASE("rescheduling catch-all") {
    oink::bip::shared_memory_object::remove("oink_test");
    oink::bip::shared_memory_object::remove("oink_test_mq");
    oink::bip::remove_shared_memory_on_destroy _test("oink_test");
    oink::bip::remove_shared_memory_on_destroy _test_mq("oink_test_mq");

    struct mymsg {
      static constexpr const char *name() { return "msg"; }
      int i;
    };

    oink::arena arena("oink_test", 65536);

    oink::sender endpoint(arena, "oink_test_mq", 1024);

    oink::receiver rendpoint(arena, "oink_test_mq", 1024);

    // Typed receivers
    endpoint.send<mymsg>(10);

    // let's not consume it
    CHECK(!rendpoint.receive<mymsg>(overloaded{[&](oink::receiver::msg &) { return false; }}));
    // let's consume it now
    {
      CHECK(rendpoint.receive<mymsg>(overloaded{[&](oink::receiver::msg &) { return true; }}));
    }
    CHECK(!rendpoint.receive<mymsg>(overloaded{[&](oink::receiver::msg &) {}}));
  }
}

TEST_CASE("message deallocation") {
  oink::bip::shared_memory_object::remove("oink_test");
  oink::bip::shared_memory_object::remove("oink_test_mq");
  oink::bip::remove_shared_memory_on_destroy _test("oink_test");
  oink::bip::remove_shared_memory_on_destroy _test_mq("oink_test_mq");

  struct mymsg {
    static constexpr const char *name() { return "msg"; }
    int i;
  };

  oink::arena arena("oink_test", 65536);

  oink::sender endpoint(arena, "oink_test_mq", 1024);

  auto initial_free_memory = arena.get_segment_manager()->get_free_memory();

  {
    auto m = endpoint.send<mymsg>(123);
    CHECK(initial_free_memory != arena.get_segment_manager()->get_free_memory());

    oink::receiver rendpoint(arena, "oink_test_mq", 1024);
    int received = 0;
    CHECK(rendpoint.receive<mymsg>(overloaded{[&](mymsg &msg) { received = msg.i; }}));

    CHECK(initial_free_memory != arena.get_segment_manager()->get_free_memory());
  }

  CHECK(initial_free_memory == arena.get_segment_manager()->get_free_memory());
}
