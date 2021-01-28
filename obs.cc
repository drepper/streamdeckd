#include "obs.hh"

#include <cassert>
#include <filesystem>

#include "obsws.hh"


namespace obs {

  namespace {

    std::string server("localhost");
    int port = 4444;
    std::string log("");

  } // anonymous namespace;


  void button::show_icon(unsigned key)
  {
    // std::string iconname("com.obsproject.Studio.png");
    std::string iconname("/home/drepper/devel/streamdeckd/obs.png");

    if (keyop == keyop_type::live_scene || keyop == keyop_type::preview_scene) {
      if (nr <= i->scene_count()) {
        bool active;
        if (keyop == keyop_type::live_scene)
          active = i->get_current_scene().nr == nr;
        else
          active = i->get_current_preview().nr == nr;
        iconname = active ? icon1 : icon2;
      }
    } else
      iconname = icon1;

    auto path = std::filesystem::path(iconname);
    if (path.is_relative())
      path = std::filesystem::path(SHAREDIR) / path;
    d->set_key_image(key, path.c_str());    
  }


  void button::call()
  {
    if (nr > i->scene_count())
      return;

    Json::Value d;
    std::string old;

    switch(keyop) {
    case keyop_type::live_scene:
      d["request-type"] = "SetCurrentScene";
      d["scene-name"] = i->get_scene_name(nr);
      obsws::emit(d);
      break;
    case keyop_type::preview_scene:
      d["request-type"] = "SetPreviewScene";
      d["scene-name"] = i->get_scene_name(nr);
      obsws::emit(d);
      break;
    case keyop_type::cut:
      i->ignore_next_transition_change = true;
      d["request-type"] = "TransitionToProgram";
      d["with-transition"]["name"] = "Cut";
      obsws::call(d);
      break;
    case keyop_type::auto_rate:
      d["request-type"] = "TransitionToProgram";
      d["with-transition"]["name"] = i->get_current_transition();
      obsws::emit(d);
      break;
    default:
      break;
    }
  }


  info::info(const libconfig::Setting& config)
  {
    if (config.exists("server"))
      server = std::string(config["server"]);
    if (config.exists("port"))
      port = int(config["port"]);
    if (config.exists("log")) {
      log = std::string(config["log"]);

      log_unknown_events = log.find("unknown") != std::string::npos;
    }

    obsws::config([this](const Json::Value& val){ callback(val); }, server.c_str(), port, log.c_str());

    Json::Value d;
    d["request-type"] = "GetSceneList";
    auto scenelist = obsws::call(d);
    if (scenelist.isMember("current-scene")) {
      current_scene = scenelist["current-scene"].asString();
      auto& escenes = scenelist["scenes"];
      for (auto& s : escenes) {
        auto name = s["name"].asString();
        scenes.emplace(std::piecewise_construct, std::forward_as_tuple(name), std::forward_as_tuple(1 + scenes.size(), name)); 
      }
    }

    d["request-type"] = "GetCurrentTransition";
    current_transition = obsws::call(d)["name"].asString();

    d["request-type"] = "GetPreviewScene";
    auto previewscene = obsws::call(d);
    if (previewscene.isMember("name"))
      current_preview = previewscene["name"].asString();
  }


  void info::add_scene(unsigned idx, const char* name)
  {
    assert(! scenes.contains(name));
    auto sname = std::string(name);
    scenes.emplace(std::piecewise_construct, std::forward_as_tuple(name), std::forward_as_tuple(idx, name));
  }


  button* info::parse_key(streamdeck::device_type* d, unsigned row, unsigned column, const libconfig::Setting& config)
  {
    if (! config.exists("function"))
      return nullptr;

    auto function = std::string(config["function"]);
    std::string icon1;
    std::string icon2;
    config.lookupValue("icon1", icon1);
    if (config.exists("icon2"))
      config.lookupValue("icon2", icon2);
    else
      icon2 = icon1;
    if (function == "scene-live" && config.exists("nr")){
      unsigned nr = unsigned(config["nr"]);
      return &scene_live_buttons.insert(std::make_pair(nr, button{ nr, d, this, row, column, icon1, icon2, keyop_type::live_scene }))->second;
    } else if (function == "scene-preview" && config.exists("nr")) {
      unsigned nr = unsigned(config["nr"]);
      return &scene_preview_buttons.insert(std::make_pair(nr, button{ nr, d, this, row, column, icon1, icon2, keyop_type::preview_scene }))->second;
    } else if (function == "scene-cut") {
      return &cut_buttons.emplace_back(0, d, this, row, column, icon1, icon1, keyop_type::cut);
    } else if (function == "scene-auto") {
      return &auto_buttons.emplace_back(0, d, this, row, column, icon1, icon1, keyop_type::auto_rate);
    }

    return nullptr;
  }


  scene& info::get_current_scene()
  {
    return scenes[current_scene];
  }


  scene& info::get_current_preview()
  {
    return scenes[current_preview];
  }


  void info::callback(const Json::Value& val)
  {
    auto update_type = val["update-type"]; 
    if (update_type == "TransitionVideoEnd") {
      auto& old_live = get_current_scene();
      auto& old_preview = get_current_preview();

      current_scene = val["to-scene"].asString();
      current_preview = val["from-scene"].asString();
      auto& new_live = get_current_scene();
      auto& new_preview = get_current_preview();

      if (old_live.nr != new_live.nr)
        for (auto p : scene_live_buttons)
          if (p.second.nr == old_live.nr || p.second.nr == new_live.nr)
            p.second.show_icon((p.second.row - 1u)* p.second.d->key_cols + p.second.column - 1u);


      if (old_preview.nr != new_preview.nr)
        for (auto p : scene_preview_buttons)
          if (p.second.nr == old_preview.nr || p.second.nr == new_preview.nr)
            p.second.show_icon((p.second.row - 1u)* p.second.d->key_cols + p.second.column - 1u);
    } else if (update_type == "PreviewSceneChanged") {
      auto& old_preview = get_current_preview();

      current_preview = val["scene-name"].asString();
      auto& new_preview = get_current_preview();

      if (old_preview.nr != new_preview.nr)
        for (auto p : scene_preview_buttons)
          if (p.second.nr == old_preview.nr || p.second.nr == new_preview.nr)
            p.second.show_icon((p.second.row - 1u)* p.second.d->key_cols + p.second.column - 1u);
    } else if (update_type == "SwitchTransition") {
      if (ignore_next_transition_change)
        ignore_next_transition_change = false;
      else
        current_transition = val["transition-name"].asString();
    } else if (log_unknown_events)
      std::cout << "info::callback " << val << std::endl;
  }

} // namespace obs
