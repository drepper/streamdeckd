#ifndef _OBSWS_HH
#define _OBSWS_HH 1

#include <functional>

#include <json/json.h>


namespace obsws {

  using event_cb_type = std::function<void(const Json::Value&)>;
  using update_cb_type = std::function<void(bool)>;


  void config(event_cb_type event_cb = nullptr, update_cb_type update_cb = nullptr, const char* server = "localhost", int port = 4444, const char* log = "");


  bool emit(const Json::Value& req);


  Json::Value call(const Json::Value& req);

} // namespace obsws

#endif // obsws.hh
