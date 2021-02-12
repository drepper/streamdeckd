#include "obs.hh"

#include <cassert>
#include <filesystem>

#include <openssl/evp.h>
#include <openssl/sha.h>

#include "obsws.hh"
#include "buttontext.hh"


namespace obs {

  namespace {

    std::string server("localhost");
    int port = 4444;
    std::string password("");
    std::string log("");


    // XYZ Remove absolute path
    Magick::Image obsicon("/home/drepper/devel/streamdeckd/obs.png");

    auto make_absolute(std::filesystem::path&& path)
    {
      if (path.is_relative())
        path = std::filesystem::path(SHAREDIR) / path;
      return path;
    }

  } // anonymous namespace;


  button::button(unsigned nr_, streamdeck::device_type* d_, info* i_, unsigned row_, unsigned column_, std::string& icon1_, std::string& icon2_, keyop_type keyop_)

  : nr(nr_), d(d_), i(i_), row(row_), column(column_), icon1(make_absolute(icon1_)), icon2(make_absolute(icon2_)), keyop(keyop_)
  {
    update();
  }


  void button::update()
  {
    auto icon = &obsicon;
    auto k = (row - 1) * d->key_cols + column - 1;

    if (i->connected) {
      // std::cout << "update connected\n";
      if (keyop == keyop_type::live_scene || keyop == keyop_type::preview_scene) {
        if (nr <= i->scene_count()) {
          bool active;
          if (keyop == keyop_type::live_scene)
            active = i->get_current_scene().nr == nr;
          else
            active = i->get_current_preview().nr == nr;
          icon = active ? &icon1 : &icon2;
        }
        // else std::cout << "scene out of range\n";
      } else if (keyop == keyop_type::record) {
        icon = i->is_recording ? &icon1 : &icon2;
      } else if (keyop == keyop_type::stream) {
        icon = i->is_streaming ? &icon1 : &icon2;
      } else {
        icon = &icon1;
        // std::cout << "new single icon " << iconname << std::endl;
      }
    }
    // else std::cout << "not connected button type " << int(keyop) << std::endl;

    d->set_key_image(k, *icon);
  }


  void button::call()
  {
    if (! i->connected)
      return;

    Json::Value batch;
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
      obsws::emit(d);
      break;
    case keyop_type::auto_rate:
      d["request-type"] = "TransitionToProgram";
      d["with-transition"]["name"] = i->get_current_transition().name;
      d["with-transition"]["duration"] = i->get_current_duration();
      obsws::emit(d);
      break;
    case keyop_type::ftb:
      batch["request-type"] = "ExecuteBatch";
      i->handle_next_transition_change.clear();
      d.clear();
      d["request-type"] = "SetPreviewScene";
      d["scene-name"] = "Black";
      batch["requests"].append(d);
      d.clear();
      d["request-type"] = "TransitionToProgram";
      d["with-transition"]["name"] = "Fade";
      d["with-transition"]["duration"] = 1000;
      batch["requests"].append(d);
      if (i->get_current_duration() != 1000) {
        d.clear();
        d["request-type"] = "SetTransitionDuration";
        d["duration"] = i->get_current_duration();
        batch["requests"].append(d);
      }
      obsws::emit(batch);
      break;
    case keyop_type::transition:
      d["request-type"] = "SetCurrentTransition";
      d["transition-name"] = i->get_transition_name(nr);
      obsws::emit(d);
      break;
    case keyop_type::record:
      d["request-type"] = "StartStopRecording";
      obsws::emit(d);
      break;
    case keyop_type::stream:
      d["request-type"] = "StartStopStreaming";
      obsws::emit(d);
      break;
    default:
      break;
    }
  }


  void auto_button::update()
  {
    auto k = (row - 1) * d->key_cols + column - 1;

    if (i->connected) {
      font_render<render_to_image> renderobj(fontobj, icon1, 0.8, 0.3);
      auto s = std::to_string(duration_ms / 1000.0);
      if (s.size() == 1)
        s += ".0";
      else if (s.size() > 3)
        s.erase(3);
      d->set_key_image(k, renderobj.draw(s.c_str(), color, std::get<0>(center), std::get<1>(center)));
    } else
      d->set_key_image(k, obsicon);
  }


  void transition_button::update()
  {
    auto k = (row - 1) * d->key_cols + column - 1;

    if (i->connected) {
      auto it = std::find_if(i->transitions.begin(), i->transitions.end(), [nr = base_type::nr](const auto& e){ return nr == e.second.nr; });
      if (it != i->transitions.end()) {
        auto name = it->second.name;
        // XYZ Hack
        if (name.size() > 5) name.erase(5);

        if (i->get_current_transition().nr == nr) {
          font_render<render_to_image> renderobj(fontobj, icon1, 0.8, 0.8);
          d->set_key_image(k, renderobj.draw(name.c_str(), Magick::Color("black"), 0.5, 0.5));
        } else {
          font_render<render_to_image> renderobj(fontobj, icon2, 0.8, 0.8);
          d->set_key_image(k, renderobj.draw(name.c_str(), Magick::Color("darkgray"), 0.5, 0.5));
        }
        return;
      }
    }
    d->set_key_image(k, obsicon);
  }


  info::info(const libconfig::Setting& config, ftlibrary& ftobj_)
  : ftobj(ftobj_)
  {
    if (config.exists("server"))
      server = std::string(config["server"]);
    else
      server = "localhost";
    if (config.exists("port"))
      port = int(config["port"]);
    else
      port = 4444;
    if (config.exists("password"))
      password = std::string(config["password"]);
    else
      password = "";
    if (config.exists("log")) {
      log = std::string(config["log"]);

      log_unknown_events = log.find("unknown") != std::string::npos;
    } else {
      log = "";
      log_unknown_events = false;
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
        for (auto& b : ftb_buttons)
          b.update();
        for (auto& b : transition_buttons)
          std::get<1>(b).update();
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
        {
          auto& old_transition = get_current_transition();
          current_transition = std::get<0>(req.names);
          auto& new_transition = get_current_transition();
          for (auto& p : transition_buttons)
            if (p.second.nr == old_transition.nr || p.second.nr == new_transition.nr)
              p.second.update();
        }
        break;
      case work_request::work_type::new_scene:
        {
          auto& name = std::get<0>(req.names);
          unsigned nr = 1 + scenes.size();
          scenes.emplace(std::piecewise_construct, std::forward_as_tuple(name), std::forward_as_tuple(nr, name));
          auto rlive = scene_live_buttons.equal_range(nr);
          for (auto it = rlive.first; it != rlive.second; ++it)
            it->second.update();
          auto rpreview = scene_preview_buttons.equal_range(nr);
          for (auto it = rpreview.first; it != rpreview.second; ++it)
            it->second.update();
        }
        break;
      case work_request::work_type::delete_scene:
        {
          auto& name = std::get<0>(req.names);
          if (auto it = scenes.find(name); it != scenes.end()) {
            auto nr = it->second.nr;
            scenes.erase(it);
            for (auto& s : scenes)
              if (s.second.nr >= nr)
                --s.second.nr;
            for (auto& b : scene_live_buttons)
              if (b.second.nr >= nr)
                b.second.update();
            for (auto& b : scene_preview_buttons)
              if (b.second.nr >= nr)
                b.second.update();
          }
        }
        break;
      case work_request::work_type::recording:
        is_recording = req.nr != 0;
        for (auto& b : record_buttons)
          b.update();
        break;
      case work_request::work_type::streaming:
        is_streaming = req.nr != 0;
        for (auto& b : record_buttons)
          b.update();
        break;
      }
    }
  }


  void info::get_session_data()
  {
    Json::Value d;
    d["request-type"] = "GetVersion";
    auto resp = obsws::call(d);
    if (! resp.isMember("status") || resp["status"] != "ok" || strverscmp("4.9", resp["obs-websocket-version"].asCString()) > 0)
      return;

    d["request-type"] = "GetAuthRequired";
    resp = obsws::call(d);
    if (! resp.isMember("status") || resp["status"] != "ok")
      return;
    if (resp["authRequired"].asBool()) {
      auto salt = resp["salt"].asCString();
      SHA256_CTX shactx;
      SHA256_Init(&shactx);
      SHA256_Update(&shactx, password.c_str(), password.size());
      SHA256_Update(&shactx, salt, strlen(salt));
      unsigned char hashbuf[SHA256_DIGEST_LENGTH];
      SHA256_Final(hashbuf, &shactx);

      unsigned char enchashbuf[(sizeof(hashbuf) + 2) / 3 * 4 + 1];
      auto enclen = EVP_EncodeBlock(enchashbuf, hashbuf, SHA256_DIGEST_LENGTH);

      auto challenge = resp["challenge"].asCString();
      SHA256_Init(&shactx);
      SHA256_Update(&shactx, enchashbuf, enclen);
      SHA256_Update(&shactx, challenge, strlen(challenge));
      SHA256_Final(hashbuf, &shactx);

      EVP_EncodeBlock(enchashbuf, hashbuf, SHA256_DIGEST_LENGTH);

      d.clear();
      d["request-type"] = "Authenticate";
      d["auth"] = (char*) enchashbuf;
      obsws::emit(d);
    }

    Json::Value batch;
    batch["request-type"] = "ExecuteBatch";

    d.clear();
    d["request-type"] = "EnableStudioMode";
    batch["requests"].append(d);

    scenes.clear();
    d.clear();
    d["request-type"] = "GetSceneList";
    batch["requests"].append(d);

    transitions.clear();
    d.clear();
    d["request-type"] = "GetTransitionList";
    batch["requests"].append(d);

    d.clear();
    d["request-type"] = "GetCurrentTransition";
    batch["requests"].append(d);

    d.clear();
    d["request-type"] = "GetPreviewScene";
    batch["requests"].append(d);

    d.clear();
    d["request-type"] = "GetRecordingStatus";
    batch["requests"].append(d);

    d.clear();
    d["request-type"] = "GetStreamingStatus";
    batch["requests"].append(d);

    resp = obsws::call(batch);
    if (! resp.isMember("status") || resp["status"] != "ok")
      return;

    auto& scenelist = resp["results"][1];
    if (scenelist.isMember("status") && scenelist["status"] == "ok") {
      bool has_Black = false;
      current_scene = scenelist["current-scene"].asString();
      auto& escenes = scenelist["scenes"];
      for (auto& s : escenes) {
        auto name = s["name"].asString();
        has_Black |= name == "Black";
        scenes.emplace(std::piecewise_construct, std::forward_as_tuple(name), std::forward_as_tuple(1 + scenes.size(), name)); 
      }

      if (! has_Black) {
        d.clear();
        d["request-type"] = "CreateScene";
        d["sceneName"] = "Black";
        obsws::emit(d);
      }
    }

    auto& transitionlist = resp["results"][2];
    if (transitionlist.isMember("status") && transitionlist["status"] == "ok")
      for (auto& t : transitionlist["transitions"])
        if (auto name = t["name"].asString(); name != "Cut")
          transitions.emplace(std::piecewise_construct, std::forward_as_tuple(name), std::forward_as_tuple(1 + transitions.size(), name));

    auto& ctransition = resp["results"][3];
    if (ctransition.isMember("status") && ctransition["status"] == "ok") {
      current_transition = ctransition["name"].asString();
      if (ctransition.isMember("duration"))
        current_duration_ms = ctransition["duration"].asInt();
      else
        current_duration_ms = -1;
    }

    auto previewscene = resp["results"][4];
    if (previewscene.isMember("status") && previewscene["status"] == "ok")
      current_preview = previewscene["name"].asString();

    auto recordingstatus = resp["results"][5];
    if (recordingstatus.isMember("status") && recordingstatus["status"] == "ok")
      is_recording = recordingstatus["isRecording"].asBool() && ! recordingstatus["isRecordingPaused"].asBool();

    auto streamingstatus = resp["results"][6];
    if (streamingstatus.isMember("status") && streamingstatus["status"] == "ok") {
      is_streaming = streamingstatus["streaming"].asBool();
      is_recording = streamingstatus["recording"].asBool() && ! streamingstatus["recording-paused"].asBool();
    }
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
      std::string font("Arial");
      std::string color("red");
      std::pair<double,double> center{ 0.5, 0.7 };
      if (config.exists("transition")) {
        auto& transition = config.lookup("transition");
        if (transition.exists("font"))
          transition.lookupValue("font", font);
        if (transition.exists("color"))
          transition.lookupValue("color", color);
        if (transition.exists("center")) {
          auto& centerlist = transition.lookup("center");
          if (centerlist.isList() && centerlist.getLength() == 2) {
            if (centerlist[0].isScalar())
              std::get<0>(center) = centerlist[0];
            if (centerlist[1].isScalar())
              std::get<1>(center) = centerlist[1];
          }
        }
      }
      return &auto_buttons.emplace_back(0, d, this, row, column, icon1, keyop_type::auto_rate, ftobj, font, color, std::move(center), current_duration_ms);
    } else if (function == "scene-ftb") {
      return &ftb_buttons.emplace_back(0, d, this, row, column, icon1, icon1, keyop_type::ftb);
    } else if (function == "transition") {
      std::string font("Arial");
      if (config.exists("font"))
        config.lookupValue("font", font);
      unsigned nr = unsigned(config["nr"]);
      return &transition_buttons.emplace(nr, transition_button(nr, d, this, row, column, icon1, icon2, keyop_type::transition, ftobj, font))->second;
    } else if (function == "toggle-record") {
      return &record_buttons.emplace_back(0, d, this, row, column, icon1, icon2, keyop_type::record);
    } else if (function == "toggle-stream") {
      return &record_buttons.emplace_back(0, d, this, row, column, icon1, icon2, keyop_type::stream);
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


  transition& info::get_current_transition()
  {
    return transitions[current_transition];
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
    else if (update_type == "TransitionDurationChanged") {
      current_duration_ms = val["new-duration"].asInt();
      for (auto& b : auto_buttons)
        b.update();
    } else if (update_type == "SourceCreated" || update_type == "SourceDestroyed") {
      if (val["sourceType"] == "scene") {
        std::lock_guard<std::mutex> guard(worker_m);
        worker_queue.emplace(update_type == "SourceCreated" ? work_request::work_type::new_scene : work_request::work_type::delete_scene, 0, std::make_pair(val["sourceName"].asString(), std::string()));
        worker_cv.notify_all();
      }
    } else if (update_type == "RecordingStarted" || update_type == "RecordingStopped") {
      std::lock_guard<std::mutex> guard(worker_m);
      worker_queue.emplace(work_request::work_type::recording, update_type == "RecordingStarted", std::make_pair(std::string(), std::string()));
      worker_cv.notify_all();
    } else if (update_type == "StreamStarted" || update_type == "StreamStopped") {
      std::lock_guard<std::mutex> guard(worker_m);
      worker_queue.emplace(work_request::work_type::streaming, update_type == "StreamStarted", std::make_pair(std::string(), std::string()));
      worker_cv.notify_all();
    } else if (log_unknown_events)
      std::cout << "info::callback unhandled event = " << val << std::endl;
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
