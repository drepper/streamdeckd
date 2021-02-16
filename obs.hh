#ifndef _OBS_HH
#define _OBS_HH 1

#include <condition_variable>
#include <functional>
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
#include <Magick++.h>

#include "ftlibrary.hh"


namespace obs {

  // Forward declaration.
  struct info;


  enum struct keyop_type {
    live_scene,
    preview_scene,
    cut,
    auto_rate,
    ftb,
    transition,
    record,
    stream,
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


  using set_key_image_cb = std::function<void(unsigned,unsigned,unsigned,const Magick::Image&)>;


  struct button {
    button(unsigned nr_, set_key_image_cb setkey_, info* i_, unsigned page_, unsigned row_, unsigned column_, std::string& icon1_, std::string& icon2_, keyop_type keyop_);

    unsigned nr;
    set_key_image_cb setkey;
    info* i;
    const unsigned page;
    const unsigned row;
    const unsigned column;
    Magick::Image icon1;
    Magick::Image icon2;
    keyop_type keyop;

    void call();
    virtual void show_icon();
    bool visible() const { return true; }
    void initialize();
  };


  struct auto_button : button {
    using base_type = button;

    auto_button(unsigned nr_, set_key_image_cb setkey_, info* i_, unsigned page_, unsigned row_, unsigned column_, std::string& icon1_, keyop_type keyop_, ftlibrary& ftobj, const std::string& font_, const std::string& color_, std::pair<double,double>&& center_, int& duration_ms_)
    : base_type(nr_, setkey_, i_, page_, row_, column_, icon1_, icon1_, keyop_), fontobj(ftobj, std::move(font_)), duration_ms(duration_ms_), color(color_), center(std::move(center_))
    {
    }

    void show_icon() override ;

    ftface fontobj;
    int& duration_ms;
    Magick::Color color;
    std::pair<double,double> center;
  };


  struct scene_button : button {
    using base_type = button;

    scene_button(unsigned nr_, set_key_image_cb setkey_, info* i_, unsigned page_, unsigned row_, unsigned column_, std::string& icon1_, std::string& icon2_, keyop_type keyop_, ftlibrary& ftobj, const std::string& font_)
    : base_type(nr_, setkey_, i_, page_, row_, column_, icon1_, icon2_, keyop_), fontobj(ftobj, std::move(font_))
    {
    }

    void show_icon() override;

    ftface fontobj;
  };


  struct transition_button : button {
    using base_type = button;

    transition_button(unsigned nr_, set_key_image_cb setkey_, info* i_, unsigned page_, unsigned row_, unsigned column_, std::string& icon1_, std::string& icon2_, keyop_type keyop_, ftlibrary& ftobj, const std::string& font_)
    : base_type(nr_, setkey_, i_, page_, row_, column_, icon1_, icon2_, keyop_), fontobj(ftobj, std::move(font_))
    {
    }

    void show_icon() override;

    ftface fontobj;
  };


  struct work_request {
    enum struct work_type {
        none,
        new_session,
        buttons,
        scene,
        preview,
        transition,
        new_scene,
        delete_scene,
        recording,
        streaming,
        sceneschanged,
        studiomode,
    } type;
    unsigned nr = 0;
    std::vector<std::string> names;
  };


  struct info {
    info(const libconfig::Setting& config, ftlibrary& ftobj_);
    ~info();

    void get_session_data();
    button* parse_key(set_key_image_cb setkey, unsigned page, unsigned row, unsigned column, const libconfig::Setting& config);

    void add_scene(unsigned idx, const char* name);
    unsigned scene_count() const { return scenes.size(); }
    unsigned transition_count() const { return transitions.size(); }
    scene& get_current_scene();
    scene& get_current_preview();
    int get_current_duration() const { return current_duration_ms; }
    scene& get_scene(const std::string& s);
    scene& get_preview(const std::string& s);
    transition& get_current_transition();
    std::string& get_scene_name(unsigned nr) { for (auto& p : scenes) if (p.second.nr == nr) return p.second.name; throw std::runtime_error("invalid scene number"); }
    std::string& get_transition_name(unsigned nr) { for (auto& p : transitions) if (p.second.nr == nr) return p.second.name; throw std::runtime_error("invalid transition number"); }

    void worker_thread();
    void callback(const Json::Value& val);
    void connection_update(bool connected_);

    ftlibrary& ftobj;

    bool created_ws = false;
    bool connected = false;
    std::queue<work_request> worker_queue;
    work_request get_request();

    std::condition_variable worker_cv;
    std::mutex worker_m;
    std::atomic<bool> terminate = false;
    std::thread worker;

    bool log_unknown_events = false;

    bool studio_mode = false;
    bool is_recording = false;
    bool is_streaming = false;

    std::unordered_map<std::string,obs::scene> scenes;
    std::string current_scene;
    std::string current_preview;
    std::unordered_map<std::string,obs::transition> transitions;
    std::string current_transition;
    int current_duration_ms;
    std::atomic_flag handle_next_transition_change = true;

    std::unordered_multimap<unsigned,scene_button> scene_live_buttons;
    std::unordered_multimap<unsigned,scene_button> scene_preview_buttons;
    std::list<button> cut_buttons;
    std::list<auto_button> auto_buttons;
    std::list<button> ftb_buttons;
    std::unordered_multimap<unsigned,transition_button> transition_buttons;
    std::list<button> record_buttons;

    const Magick::Color im_black;
    const Magick::Color im_white;
    const Magick::Color im_darkgray;

    const Magick::Image obsicon;
    const Magick::Image live_unused_icon;
    const Magick::Image preview_unused_icon;

    const std::string obsfont;
  };

} // namespace obs

#endif // obs.hh
