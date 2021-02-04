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


  void button::update()
  {
    // std::string iconname("com.obsproject.Studio.png");
    std::string iconname("/home/drepper/devel/streamdeckd/obs.png");

    if (i->connected) {
      if (keyop == keyop_type::live_scene || keyop == keyop_type::preview_scene) {
        if (nr <= i->scene_count()) {
          bool active;
          if (keyop == keyop_type::live_scene)
            active = i->get_current_scene().nr == nr;
          else
            active = i->get_current_preview().nr == nr;
          iconname = active ? icon1 : icon2;
        }
      } else {
        iconname = icon1;
        // std::cout << "new single icon " << iconname << std::endl;
      }
    }
    // else std::cout << "not connected button type " << int(keyop) << std::endl;

    auto path = std::filesystem::path(iconname);
    if (path.is_relative())
      path = std::filesystem::path(SHAREDIR) / path;
    d->set_key_image((row - 1) * d->key_cols + column - 1, path.c_str());
  }


  void button::call()
  {
    if (! i->connected)
      return;

    Json::Value d;
    std::string old;

    switch(keyop) {
    case keyop_type::live_scene:
      if (nr <= i->scene_count()) {
        d["request-type"] = "SetCurrentScene";
        d["scene-name"] = i->get_scene_name(nr);
        obsws::emit(d);
      }
      break;
    case keyop_type::preview_scene:
      if (nr <= i->scene_count()) {
        d["request-type"] = "SetPreviewScene";
        d["scene-name"] = i->get_scene_name(nr);
        obsws::emit(d);
      }
      break;
    case keyop_type::cut:
      i->handle_next_transition_change.clear();
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

    // std::cout << "calling obsws::config\n";
    obsws::config([this](const Json::Value& val){ callback(val); }, [this](bool connected){ connection_update(connected); }, server.c_str(), port, log.c_str());

    worker = std::thread([this]{ worker_thread(); });
  }


  info::~info()
  {
    terminate = true;
    worker_cv.notify_all();
    worker.join();
  }


  work_request info::get_request()
  {
    std::unique_lock<std::mutex> m(worker_m);
    worker_cv.wait(m, [this]{ return ! worker_queue.empty(); });
    auto req = std::move(worker_queue.front());
    worker_queue.pop();
    return req;
  }


  void info::worker_thread()
  {
    get_session_data();

    while (! terminate) {
      // std::cout << "waiting for work request\n";
      auto req = get_request();
      // std::cout << "worker request = " << int(req.type) << std::endl;

      switch(req.type) {
      case work_request::work_type::none:
        break;
      case work_request::work_type::new_session:
        get_session_data();
        break;
      case work_request::work_type::buttons:
        for (auto& b : scene_live_buttons)
          std::get<1>(b).update();
        for (auto& b : scene_preview_buttons)
          std::get<1>(b).update();
        for (auto& b : cut_buttons)
          b.update();
        for (auto& b : auto_buttons)
          b.update();
        break;
      case work_request::work_type::scene:
        {
          auto& old_live = get_current_scene();
          auto& old_preview = get_current_preview();

          current_scene = std::get<0>(req.names);
          current_preview = std::get<1>(req.names);
          auto& new_live = get_current_scene();
          auto& new_preview = get_current_preview();

          if (old_live.nr != new_live.nr) {
            for (auto& p : scene_live_buttons)
              if (p.second.nr == old_live.nr || p.second.nr == new_live.nr)
                p.second.update();
            for (auto& p : scene_preview_buttons)
              if (p.second.nr == old_preview.nr || p.second.nr == new_preview.nr)
                p.second.update();
          }
          if (old_preview.nr != new_preview.nr) {
            for (auto& p : scene_live_buttons)
              if ((p.second.nr == old_live.nr || p.second.nr == new_live.nr) && p.second.nr != old_live.nr && p.second.nr != new_live.nr)
                p.second.update();
            for (auto& p : scene_preview_buttons)
              if ((p.second.nr == old_preview.nr || p.second.nr == new_preview.nr) && p.second.nr != old_live.nr && p.second.nr != new_live.nr)
                p.second.update();
          }
        }
        break;
      case work_request::work_type::preview:
        {
          auto& old_preview = get_current_preview();

          current_preview = std::get<0>(req.names);
          auto& new_preview = get_current_preview();

          if (old_preview.nr != new_preview.nr)
            for (auto& p : scene_preview_buttons)
              if (p.second.nr == old_preview.nr || p.second.nr == new_preview.nr)
                p.second.update();
        }
        break;
      case work_request::work_type::transition:
        current_transition = std::get<0>(req.names);
        break;
      }
    }
  }


  void info::get_session_data()
  {
    scenes.clear();
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
      return &scene_live_buttons.emplace(nr, button(nr, d, this, row, column, icon1, icon2, keyop_type::live_scene))->second;
    } else if (function == "scene-preview" && config.exists("nr")) {
      unsigned nr = unsigned(config["nr"]);
      return &scene_preview_buttons.emplace(nr, button(nr, d, this, row, column, icon1, icon2, keyop_type::preview_scene))->second;
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


  // This function is executed by the obsws thread.  It should only use the worker_queue to
  // affect the state of the object.
  void info::callback(const Json::Value& val)
  {
    auto update_type = val["update-type"]; 
    if (update_type == "TransitionVideoEnd") {
      std::lock_guard<std::mutex> guard(worker_m);
      worker_queue.emplace(work_request::work_type::scene, 0, std::make_pair(val["to-scene"].asString(), val["from-scene"].asString()));
      worker_cv.notify_all();
    } else if (update_type == "PreviewSceneChanged") {
      std::lock_guard<std::mutex> guard(worker_m);
      worker_queue.emplace(work_request::work_type::preview, 0, std::make_pair(val["scene-name"].asString(), std::string()));
      worker_cv.notify_all();
    } else if (update_type == "SwitchTransition") {
      if (handle_next_transition_change.test_and_set()) {
        std::lock_guard<std::mutex> guard(worker_m);
        worker_queue.emplace(work_request::work_type::transition, 0, std::make_pair(val["transition-name"].asString(), std::string()));
        worker_cv.notify_all();
      }
    } else if (update_type == "Exiting")
      connection_update(false);
    else if (log_unknown_events)
      std::cout << "info::callback " << val << std::endl;
  }


  // This function is executed by the obsws thread.  It should only use the worker_queue to
  // affect the state of the object.
  void info::connection_update(bool connected_)
  {
    // std::cout << "connection_update " << std::boolalpha << connected << " to " << connected_ << std::endl;
    if (connected == connected_)
      return;

    connected = connected_;

    std::lock_guard<std::mutex> guard(worker_m);
    worker_queue.emplace(work_request::work_type::new_session);
    worker_queue.emplace(work_request::work_type::buttons);
    worker_cv.notify_all();
  }

} // namespace obs
