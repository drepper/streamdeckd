#ifndef _OBS_HH
#define _OBS_HH 1

#include <chrono>
#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <type_traits>
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
    source,
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


  using set_key_image_cb = std::function<void(unsigned,unsigned,unsigned,Magick::Image&&)>;
  using set_key_handle_cb = std::function<void(unsigned,unsigned,unsigned,int)>;


  struct button {
    button(unsigned nr_, set_key_image_cb setkey_image_, set_key_handle_cb set_key_handle_, info* i_, unsigned page_, unsigned row_, unsigned column_, int icon1_, int icon2_, keyop_type keyop_);

    unsigned nr;
    set_key_image_cb setkey_image;
    set_key_handle_cb setkey_handle;
    info* i;
    const unsigned page;
    const unsigned row;
    const unsigned column;
    int icon1;
    int icon2;
    keyop_type keyop;

    void call();
    virtual void show_icon();
    bool visible() const { return true; }
    void initialize();
  };


  struct auto_button : button {
    using base_type = button;

    auto_button(unsigned nr_, set_key_image_cb setkey_image_, set_key_handle_cb setkey_handle_, info* i_, unsigned page_, unsigned row_, unsigned column_, Magick::Image&& icon1_, keyop_type keyop_, ftlibrary& ftobj, const std::string& font_, const std::string& color_, std::pair<double,double>&& center_, unsigned& duration_ms_)
    : base_type(nr_, setkey_image_, setkey_handle_, i_, page_, row_, column_, -1, -1, keyop_), background(std::move(icon1_)), fontobj(ftobj, std::move(font_)), duration_ms(duration_ms_), color(color_), center(std::move(center_))
    {
    }

    void show_icon() override ;

    Magick::Image background;
    ftface fontobj;
    unsigned& duration_ms;
    Magick::Color color;
    std::pair<double,double> center;
  };


  struct scene_button : button {
    using base_type = button;

    scene_button(unsigned nr_, set_key_image_cb setkey_image_, set_key_handle_cb setkey_handle_, info* i_, unsigned page_, unsigned row_, unsigned column_, Magick::Image&& icon1_, Magick::Image&& icon2_, keyop_type keyop_, ftlibrary& ftobj, const std::string& font_)
    : base_type(nr_, setkey_image_, setkey_handle_, i_, page_, row_, column_, -1, -1, keyop_), background(std::move(icon1_)), background_off(std::move(icon2_)), fontobj(ftobj, std::move(font_))
    {
    }

    void show_icon() override;

    Magick::Image background;
    Magick::Image background_off;
    ftface fontobj;
  };


  struct transition_button : button {
    using base_type = button;

    transition_button(unsigned nr_, set_key_image_cb setkey_image_, set_key_handle_cb setkey_handle_, info* i_, unsigned page_, unsigned row_, unsigned column_, Magick::Image&& icon1_, Magick::Image&& icon2_, keyop_type keyop_, ftlibrary& ftobj, const std::string& font_)
    : base_type(nr_, setkey_image_, setkey_handle_, i_, page_, row_, column_, -1, -1, keyop_), background(std::move(icon1_)), background_off(std::move(icon2_)), fontobj(ftobj, std::move(font_))
    {
    }

    void show_icon() override;

    Magick::Image background;
    Magick::Image background_off;
    ftface fontobj;
  };


  struct source_button : button {
    using base_type = button;

    source_button(unsigned nr_, set_key_image_cb setkey_image_image_, set_key_handle_cb setkey_handle_, info* i_, unsigned page_, unsigned row_, unsigned column_, Magick::Image&& icon1_, Magick::Image&& icon2_, keyop_type keyop_, ftlibrary& ftobj, const std::string& font_)
    : base_type(nr_, setkey_image_image_, setkey_handle_, i_, page_, row_, column_, -1, -1, keyop_), background(std::move(icon1_)), background_off(std::move(icon2_)), fontobj(ftobj, std::move(font_))
    {
    }

    void show_icon() override;

    Magick::Image background;
    Magick::Image background_off;
    ftface fontobj;
  };


  struct work_request {
    enum struct work_type {
        none,
        new_session,
        buttons,
        scene,
        scenecontent,
        visible,
        preview,
        transition,
        new_scene,
        delete_scene,
        recording,
        streaming,
        sceneschanged,
        studiomode,
        sourcename,
        transitionend,
        duration,
        sourceorder,
    } type;
    unsigned nr = 0;
    std::vector<std::string> names;
  };


  struct info {
    using register_image_cb = std::function<int(Magick::Image&&)>;

    info(const libconfig::Setting& config, ftlibrary& ftobj_, register_image_cb register_image_);
    ~info();

    void get_session_data();
    button* parse_key(set_key_image_cb setkey_image, set_key_handle_cb setkey_handle,  unsigned page, unsigned row, unsigned column, const libconfig::Setting& config);

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

    bool prohibit_sleep() const { return is_recording || is_streaming; }

    enum struct button_class : unsigned {
      none = 0u,
      live = 1u << 0,
      preview = 1u << 1,
      cut = 1u << 2,
      auto_ = 1u << 3,
      ftb = 1u << 4,
      transition = 1u << 5,
      record = 1u << 6,
      sources = 1u << 7,

      all = live | preview | cut | auto_ | ftb | transition | record | sources
    };
    void button_update(button_class bc);

    const register_image_cb register_image;

    ftlibrary& ftobj;

    bool created_ws = false;
    bool connected = false;
    std::queue<work_request> worker_queue;
    work_request get_request();
    using timeout_clock = std::chrono::system_clock;
    std::optional<work_request> get_request(const std::chrono::time_point<timeout_clock>& to);

    std::condition_variable worker_cv;
    std::mutex worker_m;
    std::atomic<bool> terminate = false;
    std::thread worker;

    bool log_unknown_events = false;

    bool studio_mode = false;
    bool is_recording = false;
    bool is_streaming = false;

    bool ignore_next_transition_change = false;

    std::unordered_map<std::string,obs::scene> scenes;
    std::string current_scene;
    std::vector<std::string> current_sources;
    std::string current_preview;
    std::string saved_preview;
    std::unordered_map<std::string,obs::transition> transitions;
    std::string current_transition;
    unsigned current_duration_ms;
    std::atomic_flag handle_next_transition_change = true;

    std::unordered_multimap<unsigned,scene_button> scene_live_buttons;
    std::unordered_multimap<unsigned,source_button> source_buttons;
    std::unordered_multimap<unsigned,scene_button> scene_preview_buttons;
    std::list<button> cut_buttons;
    std::list<auto_button> auto_buttons;
    std::list<button> ftb_buttons;
    std::unordered_multimap<unsigned,transition_button> transition_buttons;
    std::list<button> record_buttons;
    std::string open;

    const Magick::Color im_black;
    const Magick::Color im_white;
    const Magick::Color im_darkgray;

    const int obsicon;
    const int live_unused_icon;
    const int preview_unused_icon;
    const int source_unused_icon;
    const int transition_unused_icon;

    struct ftb_handler {
      std::vector<int> icons;
      int cycle = -1;

      bool active() const { return cycle >= 0; }
      void operator++() { if (size_t(++cycle) == 2 * icons.size()) cycle = 0; }
      operator int() const { return cycle; }

      void start() { cycle = 0; }
      void stop() { cycle = -1; }

      int get() const { return icons[size_t(cycle) >= icons.size() ? (2 * icons.size() - 1 - cycle) : cycle]; }
    } ftb;

    const std::string obsfont;
  };


  inline constexpr info::button_class operator|(info::button_class l, info::button_class r)
  {
    using int_type = std::underlying_type_t<info::button_class>;
    return info::button_class(int_type(l) | int_type(r));
  }

  inline constexpr info::button_class operator&(info::button_class l, info::button_class r)
  {
    using int_type = std::underlying_type_t<info::button_class>;
    return info::button_class(int_type(l) & int_type(r));
  }

  inline consteval info::button_class operator^(info::button_class l, info::button_class r)
  {
    using int_type = std::underlying_type_t<info::button_class>;
    return info::button_class(int_type(l) ^ int_type(r));
  }

} // namespace obs

#endif // obs.hh
