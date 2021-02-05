#ifndef _OBS_HH
#define _OBS_HH 1

#include <condition_variable>
#include <list>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <libconfig.h++>
#include <streamdeckpp.hh>
#include <json/json.h>


namespace obs {

  // Forward declaration.
  struct info;


  enum struct keyop_type {
    live_scene,
    preview_scene,
    cut,
    auto_rate,
    ftb,
  };


  struct scene {
    scene() = default;
    scene(unsigned nr_, const std::string& name_) : nr(nr_), name(name_) { }
    unsigned nr = 0;
    std::string name;
  };


  struct transition {
    transition() = default;
    transition(unsigned nr_, const std::string& name_) : nr(nr_), name(name_) { }
    unsigned nr = 0;
    std::string name;
  };


  struct button {
    button(unsigned nr_, streamdeck::device_type* d_, info* i_, unsigned row_, unsigned column_, std::string& icon1_, std::string& icon2_, keyop_type keyop_)
    : nr(nr_), d(d_), i(i_), row(row_), column(column_), icon1(icon1_), icon2(icon2_), keyop(keyop_)
    {
      update();
    }

    unsigned nr;
    streamdeck::device_type* d;
    info* i;
    unsigned row;
    unsigned column;
    std::string icon1;
    std::string icon2;
    keyop_type keyop;

    void call();
    void show_icon(unsigned key) {}
    void update();
    void initialize();
  };


  struct work_request {
    enum struct work_type { none, new_session, buttons, scene, preview, transition } type;
    unsigned nr = 0;
    std::pair<std::string,std::string> names{ "", ""};
  };


  struct info {
    info(const libconfig::Setting& config);
    ~info();

    void get_session_data();
    button* parse_key(streamdeck::device_type* d, unsigned row, unsigned column, const libconfig::Setting& config);

    void add_scene(unsigned idx, const char* name);
    unsigned scene_count() const { return scenes.size(); }
    scene& get_current_scene();
    scene& get_current_preview();
    int get_current_duration() const { return current_duration_ms; }
    scene& get_scene(const std::string& s);
    scene& get_preview(const std::string& s);
    const std::string& get_current_transition() const { return current_transition; }
    std::string& get_scene_name(unsigned nr) { for (auto& p : scenes) if (p.second.nr == nr) return p.second.name; throw std::runtime_error("invalid scene number"); }

    void worker_thread();
    void callback(const Json::Value& val);
    void connection_update(bool connected_);

    bool created_ws = false;
    bool connected = false;
    std::queue<work_request> worker_queue;
    work_request get_request();

    std::condition_variable worker_cv;
    std::mutex worker_m;
    std::atomic<bool> terminate = false;
    std::thread worker;

    bool log_unknown_events = false;

    std::unordered_map<std::string,obs::scene> scenes;
    std::string current_scene;
    std::string current_preview;
    std::unordered_map<std::string,obs::transition> transitions;
    std::string current_transition;
    int current_duration_ms;
    std::atomic_flag handle_next_transition_change = true;

    std::unordered_multimap<unsigned,button> scene_live_buttons;
    std::unordered_multimap<unsigned,button> scene_preview_buttons;
    std::list<button> cut_buttons;
    std::list<button> auto_buttons;
  };

} // namespace obs

#endif // obs.hh
