#include "obsws.hh"

#include <atomic>
#if __has_include(<latch>)
# include <latch>
#else
# include <condition_variable>
#endif
#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

#include <json/json.h>
#include <libwebsockets.h>
#include <uuid.h>

#if __cpp_lib_atomic_wait == 0
# include <cerrno>
# include <sys/syscall.h>
# include <linux/futex.h>
#endif


// XYZ DEBUG
#include <iostream>


#if __has_include(<latch>) == 0
namespace {
  struct replacement_latch {
    explicit replacement_latch(std::ptrdiff_t expected_) : expected(expected_) { }
    ~replacement_latch() = default;

    replacement_latch(const replacement_latch&) = delete;
    replacement_latch& operator=(const replacement_latch&) = delete;

    void count_down(std::ptrdiff_t n = 1) {
      std::lock_guard<std::mutex> lk(m);
      if ((expected -= n) == 0)
        cv.notify_all();
    }

    void wait() {
      std::unique_lock<std::mutex> lock(m);
      cv.wait(lock, [this]{ return expected == 0; });
    }

    bool try_wait() const noexcept {
      return expected == 0;
    }
  private:
    std::atomic<std::ptrdiff_t> expected;
    std::mutex m;
    std::condition_variable cv;
  };
}

namespace std {
  using latch = replacement_latch;
}
#endif


namespace {

#if __cpp_lib_atomic_wait
  template<typename T>
  void atomic_wait(std::atomic<T>& obj, T old, std::memory_order m = std::memory_order_seq_cst) noexcept
  {
    obj.wait(old, m);
  }

  void atomic_notify_all(std::atomic<T>& obj) noexcept
  {
    obj.notify_all();
  }
#else
  template<typename T>
  void atomic_wait(std::atomic<T>& obj, T old, std::memory_order m = std::memory_order_seq_cst)
  {
    while (true) {
      auto e = syscall(SYS_futex, &obj, FUTEX_WAIT_PRIVATE, old, nullptr);
      if (e == 0 || errno == EAGAIN)
        break;
      else if (errno != EINTR)
        throw std::runtime_error("invalid futex result " + std::to_string(e) + "  errno=" + std::to_string(errno));
    }
  }

  template<typename T>
  void atomic_notify_all(std::atomic<T>& obj)
  {
    syscall(SYS_futex, &obj, FUTEX_WAKE_PRIVATE, INT_MAX, nullptr);
  }
#endif

  enum struct ws_status {
    idle,
    connecting,
    connected,
    running,
    writable,
    terminated
  };


  struct request {
    request(Json::Value&& d_, bool emit_, std::ptrdiff_t lc) : d(std::move(d_)), emit(emit_), l(lc) { }

    Json::Value d;
    bool emit;
    bool fail = false;
    std::latch l;
    Json::Value result;
  };


  struct client {
    client(obsws::event_cb_type event_cb_, obsws::update_cb_type update_cb_, const char* server_, unsigned port_, const char* log, int ssl_connection_, const char* ssl_ca_path, const uint32_t* backoff_ms, uint16_t nbackoff_ms, uint16_t secs_since_valid_ping, uint16_t secs_since_valid_hangup, uint8_t jitter_percent);
    ~client() { status = ws_status::terminated; atomic_notify_all(status); thread.join(); }

    static auto allocate(obsws::event_cb_type event_cb, obsws::update_cb_type update_cb_, const char* server, unsigned port, const char* log, int ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_INSECURE | LCCSCF_ALLOW_EXPIRED | LCCSCF_ALLOW_SELFSIGNED, const char* ssl_ca_path = nullptr, const uint32_t* backoff_ms = init_backoff_ms, uint16_t nbackoff_ms = LWS_ARRAY_SIZE(init_backoff_ms), uint16_t secs_since_valid_ping = 3, uint16_t secs_since_valid_hangup = 10, uint8_t jitter_percent = 20)
    { return std::make_unique<client>(event_cb, update_cb_, server, port, log, ssl_connection, ssl_ca_path, backoff_ms, nbackoff_ms, secs_since_valid_ping, secs_since_valid_hangup, jitter_percent); }

    void run();
    bool ensure_running() {
      bool started = false;
      for (auto s = status.load(); s != ws_status::running && s != ws_status::writable; s = status.load()) {
        if (s == ws_status::terminated)
          return false;
        if (s == ws_status::idle) {
          if (started)
            return false;
          started = true;
          connect();
        }
        atomic_wait(status, s);
      }
      return true;
    }
    bool ensure_mark_writable() {
      bool started = false;
      for (auto s = status.load(); s != ws_status::writable; s = status.load()) {
        if (s == ws_status::terminated)
          return false;
        if (s == ws_status::idle) {
          if (started)
            return false;
          started = true;
          connect();
        }
        atomic_wait(status, s);
      }
      return true;
    }

    int send(const std::string& s);
    request& send(Json::Value&& root, bool emit);

    void terminate() { status = ws_status::terminated; atomic_notify_all(status); }

    static int callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len)
    {
      return ((client*) user)->callback(wsi, reason, in, len);
    }

    template<bool emit>
    auto call_emit(const Json::Value& din)
    {
      Json::Value d(din);
      uuid_t uuid;
      char uuid_str[37];
      uuid_generate(uuid);
      uuid_unparse(uuid, uuid_str);
      d["message-id"] = uuid_str;
      auto& req(send(std::move(d), emit));

      if constexpr (emit) 
        return true;
      else {
        req.l.wait();
        Json::Value res = std::move(req.result);
        outstanding.remove_if([uuid_str](auto& e) { return e.d["message-id"].asString() == uuid_str; });
        return res;    
      }
    }

  protected:
    static const char protocol_name[];
    static const uint32_t init_backoff_ms[3];
    static const uint32_t subsequent_backoff_ms[4];
    const lws_protocols protocols[2] = {
      { "obsws", callback, 0, 0 },
      { nullptr, nullptr, 0, 0}
    };

    std::unique_ptr<lws_context, void(*)(lws_context*)> context{ nullptr, nullptr };
    lws_retry_bo_t retry;
    const char* remote_protocol;
    const int ssl_connection;

    const char* server;
    int port;
    bool log_events;

    struct sul_wrapper {
      client* self;
      lws_sorted_usec_list_t sul;  // schedule connection retry
    } wrap;
    lws* wsi = nullptr;            // related wsi if any
    std::atomic<ws_status> status = ws_status::idle;
    uint16_t retry_count = 0;      // count of consequetive retries

    std::thread thread;
    obsws::event_cb_type event_cb;
    obsws::update_cb_type update_cb;

    // Memory used to partial results.
    std::string chunks;

    static void connect(lws_sorted_usec_list_t* sul) {
      // Unfortunately the C interface of libwebsockets so far does not have any callbacks
      // with additional parameters passed in.  Resort to ugly pointer arithmetic.
      // We need a POD class to make this possible.  The 'client' class is not a POD so
      // an additional wrapper class is introduced with a pointer to the class.
      static_cast<sul_wrapper*>((void*)((char*) sul - offsetof(sul_wrapper, sul)))->self->connect();
    }

    int callback(struct lws* wsi, enum lws_callback_reasons reason, void* in, size_t len);

  private:
    void connect();
    void exhausted();

    std::list<request> outstanding;
    std::mutex lock;
  };


  const char client::protocol_name[] = "obsws";

  const uint32_t client::init_backoff_ms[3] = { 250, 500, 750 }; // XYZ Last number should be 2 minutes or so...
  static constexpr uint32_t connect_timeout = 10000;  // XYZ Number should be 2 minutes or so...
  const uint32_t client::subsequent_backoff_ms[4] = { connect_timeout, 250, 500, 750 };


  client::client(obsws::event_cb_type event_cb_, obsws::update_cb_type update_cb_, const char* server_, unsigned port_, const char* log, int ssl_connection_, const char* ssl_ca_path, const uint32_t* backoff_ms, uint16_t nbackoff_ms, uint16_t secs_since_valid_ping, uint16_t secs_since_valid_hangup, uint8_t jitter_percent)
  : retry{ .retry_ms_table = backoff_ms, .retry_ms_table_count = nbackoff_ms, .conceal_count = nbackoff_ms, .secs_since_valid_ping = secs_since_valid_ping, .secs_since_valid_hangup = secs_since_valid_hangup, .jitter_percent = jitter_percent },
    ssl_connection(ssl_connection_), server(server_), port(port_), log_events(strstr(log, "events") != nullptr), wrap{ this }, status(ws_status::connecting), event_cb(event_cb_), update_cb(update_cb_)
  {
    std::cout << "client::client\n";
    lws_context_creation_info info;
    memset(&info, '\0', sizeof info);
    info.options = ssl_connection ? LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT : 0;
    info.port = CONTEXT_PORT_NO_LISTEN; /* we do not run any server */
    info.protocols = protocols;
    info.fd_limit_per_thread = 1 + 1 + 1;
    info.gid = -1;
    info.uid = -1;
    info.client_ssl_ca_filepath = ssl_ca_path;

    context = std::unique_ptr<lws_context, void(*)(lws_context*)>{ lws_create_context(&info), &lws_context_destroy };
    if (context == nullptr)
      throw std::runtime_error("cannot create lws context");

    std::cout << "created context\n";
    /* schedule the first client connection attempt to happen immediately */
    lws_sul_schedule(context.get(), 0, &wrap.sul, client::connect, 1);

    std::cout << "client::client scheduled\n";
    thread = std::thread(&client::run, this);
    std::cout << "client::client started thread\n";
  }


  void client::exhausted()
  {
    lwsl_info("%s: connection attempts exhausted\n", __func__);
    status = ws_status::idle;
    atomic_notify_all(status);
    update_cb(false);
    while (! outstanding.empty()) {
      outstanding.front().fail = true;
      outstanding.front().l.count_down();
      outstanding.pop_front();
    }

    // Change to the table with a large initial timeout.
    retry_count = 0;
    retry.retry_ms_table = subsequent_backoff_ms;
    retry.retry_ms_table_count = LWS_ARRAY_SIZE(subsequent_backoff_ms);
    retry.conceal_count = LWS_ARRAY_SIZE(subsequent_backoff_ms);
    if (lws_retry_sul_schedule(context.get(), 0, &wrap.sul, &retry, client::connect, &retry_count)) {
      lwsl_err("%s: rescheduling after connection timeout failed", __func__);
    }
    else std::cout << "reschedule in exhausted worked\n";
  }


  void client::connect()
  {
    lws_client_connect_info info;

    memset(&info, 0, sizeof(info));
    info.context = context.get();
    info.port = port;
    info.address = server;
    info.path = "/";
    info.host = lws_canonical_hostname(context.get());
    info.ssl_connection = ssl_connection;
    info.protocol = protocols[0].name;
    // info.local_protocol_name = "obsws";   // Does not matter.
    info.pwsi = &wsi;
    info.retry_and_idle_policy = &retry;
    info.userdata = this;

    status = ws_status::connecting;
    // atomic_notify_all(status);

    if (! lws_client_connect_via_info(&info)) {
      lwsl_user("%s: retry connecting\n", __func__);
      if (lws_retry_sul_schedule(context.get(), 0, &wrap.sul, &retry, client::connect, &retry_count)) {
        exhausted();
      }
    }

  }


  void client::run()
  {
    // std::cout << "thread loop reached\n";
    while (status != ws_status::terminated) {
      // std::cout << "run service\n";
      if (lws_service(context.get(), 50) < 0) {
        status = ws_status::idle;
        break;
      }
      // std::cout << "service done\n";
      auto s = status.load();
      if (s == ws_status::connected) {
        if (status.compare_exchange_strong(s, ws_status::running))
          atomic_notify_all(status);
      }
    }
    // std::cout << "run terminated\n";
  }


  int client::callback(struct lws* wsi, enum lws_callback_reasons reason, void* in, size_t len)
  {
    switch (reason) {
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      lwsl_err("CLIENT_CONNECTION_ERROR: %s\n", in ? (char *)in : "(null)");
      std::cout << "going to retry\n";
      goto do_retry;

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
      std::cout << "connected!\n";
      update_cb(true);
      status = ws_status::connected;
      atomic_notify_all(status);
      lwsl_user("%s: established\n", __func__);
      break;

    case LWS_CALLBACK_CLIENT_CLOSED:
      lwsl_user("%s: closed\n", __func__);
      update_cb(false);
      status = ws_status::connecting;
      goto do_retry;

    case LWS_CALLBACK_CLIENT_WRITEABLE:
      status = ws_status::writable;
      atomic_notify_all(status);
      break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
      if (log_events)
        lwsl_hexdump_notice(in, len);
      {
        chunks.append(static_cast<char*>(in), len);
        
        Json::CharReaderBuilder builder;
        const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        Json::Value root;
        Json::String err;
        if (reader->parse(chunks.data(), chunks.data() + chunks.size(), &root, &err)) {
          if (root.isMember("message-id")) {
            auto queued = std::find_if(outstanding.begin(), outstanding.end(), [s=root["message-id"].asString()](const auto& e){ return s == e.d["message-id"]; });
            if (queued->emit)
              outstanding.erase(queued);
            else {
              queued->result = std::move(root);
              queued->l.count_down();
            }
          } else if (event_cb && root.isMember("update-type"))
            event_cb(root);

          chunks.clear();
        } else {
          // There is no error code.  For incomlete messages we see an error string containing
          //    Missing '}' or object member name
          if (err.find("Missing '}'") == std::string::npos) {
            chunks.clear();
            lwsl_err("%s: invalid JSON: %s\n", __func__, err.c_str());
          }
        }
      }
      break;

    default:
      break;
    }

    return lws_callback_http_dummy(wsi, reason, this, in, len);

  do_retry:
    status = ws_status::connecting;
    if (lws_retry_sul_schedule_retry_wsi(wsi, &wrap.sul, client::connect, &retry_count))
      exhausted();

    return 0;
  }


  int client::send(const std::string& in)
  {
    if (! ensure_mark_writable())
      return -1;

    std::string s = std::string(LWS_SEND_BUFFER_PRE_PADDING, '\0') + in + std::string(LWS_SEND_BUFFER_POST_PADDING, '\0');

    return lws_write(wsi, reinterpret_cast<unsigned char*>(s.data()) + LWS_SEND_BUFFER_PRE_PADDING, in.size(), LWS_WRITE_TEXT);
  }


  request& client::send(Json::Value&& root, bool emit)
  {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";

    auto& ref = outstanding.emplace_back(std::move(root), emit, 1);

    if (send(Json::writeString(builder, ref.d)) < 0)
      throw std::runtime_error("cannot send");

    return ref;
  }


  // Configuration.
  obsws::event_cb_type event_cb = nullptr;
  obsws::update_cb_type update_cb = nullptr;
  std::string server("localhost");
  int port = 4444;
  std::string log("");

  // Session.
  std::unique_ptr<client> wsobj;


  bool setup()
  {
    if (! wsobj) {
      std::cout << "starting obsws thread\n";
      wsobj = client::allocate(event_cb, update_cb, server.c_str(), port, log.c_str(), 0);

      std::cout << "started obsws thread\n";
    }
    return bool(wsobj);
  }

} // anonymous namespace


namespace obsws {

  void config(obsws::event_cb_type event_cb_, obsws::update_cb_type update_cb_, const char* server_, int port_, const char* log_)
  {
    event_cb = event_cb_;
    update_cb = update_cb_;
    server = server_;
    port = port_;
    log = log_;
  }


  bool emit(const Json::Value& req)
  {
    if (! setup())
      throw std::runtime_error("no connection");

    try {
      return wsobj->call_emit<true>(req);
    }
    catch (std::runtime_error&) {
      return false;
    }
  }


  Json::Value call(const Json::Value& req)
  {
    if (! setup())
      throw std::runtime_error("no connection");

    try {
      return wsobj->call_emit<false>(req);
    }
    catch (std::runtime_error&) {
      return Json::Value();
    }
  }

} // namespace obsws


#if 0
int main()
{
  Json::Value d;
  d["request-type"] = "GetVersion";
  auto res = obsws::call(d);
  std::cout << "res = " << res << std::endl;


  d["request-type"] = "GetSceneList";
  res = obsws::call(d);
  std::cout << "res = " << res << std::endl;

  d["request-type"] = "SetCurrentScene";
  d["scene-name"] = "Before";
  res = obsws::call(d);
  std::cout << "res = " << res << std::endl;

  sleep(2);
  // wsobj.release();
}
#endif
