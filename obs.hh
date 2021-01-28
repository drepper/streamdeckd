#ifndef _OBS_HH
#define _OBS_HH 1

#include <list>
#include <string>
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
  };

  struct scene {
    scene() = default;
    scene(unsigned nr_, const std::string& name_) : nr(nr_), name(name_) { }
    unsigned nr = 0;
    std::string name;
  };


  struct button {
    unsigned nr;
    streamdeck::device_type* d;
    info* i;
    unsigned row;
    unsigned column;
    std::string icon1;
    std::string icon2;
    keyop_type keyop;

    void call();
    void show_icon(unsigned key);
  };


  struct info {
    info(const libconfig::Setting& config);

    button* parse_key(streamdeck::device_type* d, unsigned row, unsigned column, const libconfig::Setting& config);

    void add_scene(unsigned idx, const char* name);
    unsigned scene_count() const { return scenes.size(); }
    scene& get_current_scene();
    scene& get_current_preview();
    const std::string& get_current_transition() const { return current_transition; }
    std::string& get_scene_name(unsigned nr) { for (auto& p : scenes) if (p.second.nr == nr) return p.second.name; throw std::runtime_error("invalid scene number"); }

    void callback(const Json::Value& val);

    bool log_unknown_events = false;

    std::unordered_map<std::string,obs::scene> scenes;
    std::string current_scene;
    std::string current_preview;
    std::string current_transition;
    bool ignore_next_transition_change = false;

    std::unordered_multimap<unsigned,button> scene_live_buttons;
    std::unordered_multimap<unsigned,button> scene_preview_buttons;
    std::list<button> cut_buttons;
    std::list<button> auto_buttons;
  };

} // namespace obs

#endif // obs.hh
