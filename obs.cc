#include "obs.hh"

#include <cassert>
#include <iterator>
#include <filesystem>

#include <openssl/evp.h>
#include <openssl/sha.h>

#include "obsws.hh"
#include "buttontext.hh"

using namespace std::string_literals;
using namespace std::literals::chrono_literals;


static_assert(__cpp_static_assert >= 200410L, "extended static_assert missing");
static_assert(__cpp_lib_string_udls >= 201304);
static_assert(__cpp_lib_chrono_udls >= 201304);


// in main.cc
extern Magick::Image find_image(const std::filesystem::path& path);


namespace obs {

  namespace {

    std::string server("localhost");
    int port = 4444;
    std::string password("");
    std::string log("");

  } // anonymous namespace;


  button::button(unsigned nr_, set_key_image_cb setkey_, info* i_, unsigned page_, unsigned row_, unsigned column_, std::string& icon1_, std::string& icon2_, keyop_type keyop_)
  : nr(nr_), setkey(setkey_), i(i_), page(page_), row(row_), column(column_), icon1(find_image(icon1_)), icon2(find_image(icon2_)), keyop(keyop_)
  {
  }


  void button::show_icon()
  {
    auto icon = &i->obsicon;

    if (i->connected) {
      // std::cout << "update connected\n";
      if (i->studio_mode && (keyop == keyop_type::live_scene || keyop == keyop_type::preview_scene)) {
        if (nr <= i->scene_count()) {
          bool active;
          if (keyop == keyop_type::live_scene)
            active = i->get_current_scene().nr == nr;
          else
            active = i->get_current_preview().nr == nr;
          icon = active ? &icon1 : &icon2;
        }
        // else std::cout << "scene out of range\n";
      } else if (keyop == keyop_type::record)
        icon = i->is_recording ? &icon1 : &icon2;
      else if (keyop == keyop_type::stream)
        icon = i->is_streaming ? &icon1 : &icon2;
      else if (keyop == keyop_type::ftb && i->ftb.active())
        icon = &i->ftb.get();
      else if (i->studio_mode)
        icon = &icon1;
    }

    setkey(page, row, column, *icon);
  }


  void button::call()
  {
    if (! i->connected)
      return;

    if (! i->studio_mode && keyop != keyop_type::live_scene && keyop != keyop_type::record && keyop != keyop_type::stream && keyop != keyop_type::source && keyop != keyop_type::transition)
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
      i->ignore_next_transition_change = true;
      d["request-type"] = "TransitionToProgram";
      d["with-transition"]["name"] = "Cut";
      d["with-transition"]["duration"] = 0u;
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
    case keyop_type::source:
      if (2 * (nr - 1) < i->current_sources.size()) {
        d["request-type"] = "SetSceneItemProperties";
        if (i->studio_mode)
          d["scene-name"] = i->current_preview;
        d["item"] = i->current_sources[2 * (nr - 1)];
        d["visible"] = i->current_sources[2 * (nr - 1) + 1] == "false";
        obsws::emit(d);
      }
      break;
    default:
      break;
    }
  }


  void auto_button::show_icon()
  {
    if (i->connected && i->studio_mode) {
      font_render<render_to_image> renderobj(fontobj, icon1, 0.8, 0.3);
      auto s = std::to_string(duration_ms / 1000.0);
      if (s.size() == 1)
        s += ".0";
      else if (s.size() > 3)
        s.erase(3);
      setkey(page, row, column, renderobj.draw(s, color, std::get<0>(center), std::get<1>(center)));
    } else
      setkey(page, row, column, i->obsicon);
  }


  void scene_button::show_icon()
  {
    if (i->connected && (keyop != keyop_type::preview_scene || i->studio_mode)) {
      auto it = std::find_if(i->scenes.begin(), i->scenes.end(), [nr = base_type::nr](const auto& e){ return nr == e.second.nr; });
      if (it != i->scenes.end()) {
        auto name = it->second.name;
        std::vector<std::string> vs;
        if (name.size() <= 5)
          vs.emplace_back(name);
        else {
          std::istringstream iss(name);
          vs = std::vector(std::istream_iterator<std::string>{iss}, std::istream_iterator<std::string>());
        }

        if ((keyop == keyop_type::live_scene && i->get_current_scene().nr == nr) || (keyop == keyop_type::preview_scene && i->get_current_preview().nr == nr)) {
          font_render<render_to_image> renderobj(fontobj, icon1, 0.8, 0.8);
          setkey(page, row, column, renderobj.draw(vs, keyop == keyop_type::live_scene ? i->im_white : i->im_black, 0.5, 0.5));
        } else {
          font_render<render_to_image> renderobj(fontobj, icon2, 0.8, 0.8);
          setkey(page, row, column, renderobj.draw(vs, i->im_darkgray, 0.5, 0.5));
        }
        return;
      }
    }
    setkey(page, row, column, keyop == keyop_type::live_scene ? i->live_unused_icon : (! i->connected || i->studio_mode ? i->preview_unused_icon : i->obsicon));
  }


  void transition_button::show_icon()
  {
    if (i->connected) {
      auto it = std::find_if(i->transitions.begin(), i->transitions.end(), [nr = base_type::nr](const auto& e){ return nr == e.second.nr; });
      if (it != i->transitions.end()) {
        auto name = it->second.name;
        std::vector<std::string> vs;
        if (name.size() <= 5)
          vs.emplace_back(name);
        else {
          std::istringstream iss(name);
          vs = std::vector(std::istream_iterator<std::string>{iss}, std::istream_iterator<std::string>());
        }

        if (i->get_current_transition().nr == nr) {
          font_render<render_to_image> renderobj(fontobj, icon1, 0.8, 0.8);
          setkey(page, row, column, renderobj.draw(vs, i->im_black, 0.5, 0.5));
        } else {
          font_render<render_to_image> renderobj(fontobj, icon2, 0.8, 0.8);
          setkey(page, row, column, renderobj.draw(vs, i->im_darkgray, 0.5, 0.5));
        }
        return;
      }
      setkey(page, row, column, icon2);
    } else
      setkey(page, row, column, i->transition_unused_icon);
  }


  void source_button::show_icon()
  {
    if (i->connected) {
      unsigned idx = 2 * (base_type::nr - 1u);
      if (idx < i->current_sources.size()) {
        auto str = i->current_sources[idx];
        std::vector<std::string> vs;
        if (str.size() <= 5)
          vs.emplace_back(str);
        else {
          std::istringstream iss(str);
          vs = std::vector(std::istream_iterator<std::string>{iss}, std::istream_iterator<std::string>());
        }

        if (i->current_sources[idx + 1] == "true") {
          font_render<render_to_image> renderobj(fontobj, icon1, 0.8, 0.8);
          setkey(page, row, column, renderobj.draw(vs, i->im_black, 0.5, 0.5));
        } else {
          font_render<render_to_image> renderobj(fontobj, icon2, 0.8, 0.8);
          setkey(page, row, column, renderobj.draw(vs, i->im_darkgray, 0.5, 0.5));
        }
        return;
      }
    }
    setkey(page, row, column, i->source_unused_icon);
  }


  info::info(const libconfig::Setting& config, ftlibrary& ftobj_)
  : ftobj(ftobj_), im_black("black"), im_white("white"), im_darkgray("darkgray"),
    obsicon(find_image("obs.png")), live_unused_icon(find_image("scene_live_unused.png")), preview_unused_icon(find_image("scene_preview_unused.png")),
    source_unused_icon(find_image("source_unused.png")), transition_unused_icon(find_image("transition_unused.png")),
    ftb { .icons = { find_image("ftb-0.png"), find_image("ftb-12.png"), find_image("ftb-25.png"), find_image("ftb-37.png"),
                     find_image("ftb-50.png"), find_image("ftb-62.png"), find_image("ftb-75.png"), find_image("ftb-87.png"),
                     find_image("ftb-100.png") } },
    obsfont(config.exists("font") ? std::string(config["font"]) : "Arial"s)
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
    if (config.exists("open"))
      open = std::string(config["open"]);
    else
      open = "";

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


  std::optional<work_request> info::get_request(const std::chrono::time_point<timeout_clock>& to)
  {
    std::unique_lock<std::mutex> m(worker_m);
    while (worker_queue.empty())
      if (worker_cv.wait_until(m, to) == std::cv_status::timeout) {
        if (worker_queue.empty())
          return std::nullopt;
        break;
      }
    auto req = std::move(worker_queue.front());
    worker_queue.pop();
    return req;
  }


  void info::button_update(button_class cb)
  {
    if ((cb & button_class::live) != button_class::none)
      for (auto& b : scene_live_buttons)
        std::get<1>(b).show_icon();

    if ((cb & button_class::preview) != button_class::none)
      for (auto& b : scene_preview_buttons)
        std::get<1>(b).show_icon();

    if ((cb & button_class::cut) != button_class::none)
      for (auto& b : cut_buttons)
        b.show_icon();

    if ((cb & button_class::auto_) != button_class::none)
      for (auto& b : auto_buttons)
        b.show_icon();

    if ((cb & button_class::ftb) != button_class::none)
      for (auto& b : ftb_buttons)
        b.show_icon();

    if ((cb & button_class::transition) != button_class::none)
      for (auto& b : transition_buttons)
        std::get<1>(b).show_icon();

    if ((cb & button_class::record) != button_class::none)
      for (auto& b : record_buttons)
        b.show_icon();

    if ((cb & button_class::sources) != button_class::none)
      for (auto& b : source_buttons)
        std::get<1>(b).show_icon();
  }


  void info::worker_thread()
  {
    static constexpr auto cycle_time = 200ms;

    get_session_data();

    auto now = timeout_clock::now();
    auto to = now + cycle_time;

    Json::Value batch;
    while (! terminate) {
      work_request req;
      if (ftb.active()) {
        auto oreq = get_request(to);

        now = timeout_clock::now();
        if (now >= to) {
          ++ftb;
          button_update(button_class::ftb);
          to += cycle_time;
        }

        if (! oreq)
          continue;

        req = std::move(*oreq);
      } else
        req = get_request();

      Json::Value d;
      switch(req.type) {
      case work_request::work_type::none:
        break;
      case work_request::work_type::new_session:
        get_session_data();
        break;
      case work_request::work_type::buttons:
        button_update(button_class::all);
        break;
      case work_request::work_type::scene:
        {
          auto& old_live = get_current_scene();
          auto& old_preview = get_current_preview();

          current_scene = req.names[0];
          current_preview = req.names[1];
          auto& new_live = get_current_scene();
          auto& new_preview = get_current_preview();

          if (old_live.nr != new_live.nr) {
            for (auto& p : scene_live_buttons)
              if (p.second.nr == old_live.nr || p.second.nr == new_live.nr)
                p.second.show_icon();
            for (auto& p : scene_preview_buttons)
              if (p.second.nr == old_preview.nr || p.second.nr == new_preview.nr)
                p.second.show_icon();
          }
          if (old_preview.nr != new_preview.nr) {
            for (auto& p : scene_live_buttons)
              if ((p.second.nr == old_live.nr || p.second.nr == new_live.nr) && p.second.nr != old_live.nr && p.second.nr != new_live.nr)
                p.second.show_icon();
            for (auto& p : scene_preview_buttons)
              if ((p.second.nr == old_preview.nr || p.second.nr == new_preview.nr) && p.second.nr != old_live.nr && p.second.nr != new_live.nr)
                p.second.show_icon();
          }
        }
        break;
      case work_request::work_type::scenecontent:
        if (! studio_mode) {
          if (req.names[0] != current_scene)
            // XYZ To do.  Should not happen but can be rectified by requesting scene content
            abort();
          else
            req.names.erase(req.names.begin());
          current_sources = std::move(req.names);
          button_update(button_class::sources);
        }
        break;
      case work_request::work_type::visible:
        assert(req.names.size() == 3);
        if (req.names[0] == (studio_mode ? current_preview : current_scene)) {
          for (size_t j = 0; j < current_sources.size(); j += 2)
            if (current_sources[j] == req.names[1]) {
              current_sources[j + 1] = std::move(req.names[2]);
              for (auto& p : source_buttons)
                if (p.second.nr == j / 2 + 1u)
                  p.second.show_icon();
              break;
            }
        }
        break;
      case work_request::work_type::preview:
        {
          auto& old_preview = get_current_preview();

          current_preview = req.names[0];
          auto& new_preview = get_current_preview();

          if (old_preview.nr != new_preview.nr)
            for (auto& p : scene_preview_buttons)
              if (p.second.nr == old_preview.nr || p.second.nr == new_preview.nr)
                p.second.show_icon();

          if (studio_mode) {
            req.names.erase(req.names.begin());
            current_sources = std::move(req.names);
            button_update(button_class::sources);
          }
        }
        break;
      case work_request::work_type::transition:
        if (! ignore_next_transition_change) {
          auto& old_transition = get_current_transition();
          current_transition = req.names[0];
          auto& new_transition = get_current_transition();
          for (auto& p : transition_buttons)
            if (p.second.nr == old_transition.nr || p.second.nr == new_transition.nr)
              p.second.show_icon();
        }
        break;
      case work_request::work_type::new_scene:
        {
          auto& name = req.names[0];
          unsigned nr = 1 + scenes.size();
          scenes.emplace(std::piecewise_construct, std::forward_as_tuple(name), std::forward_as_tuple(nr, name));
          auto rlive = scene_live_buttons.equal_range(nr);
          for (auto it = rlive.first; it != rlive.second; ++it)
            it->second.show_icon();
          auto rpreview = scene_preview_buttons.equal_range(nr);
          for (auto it = rpreview.first; it != rpreview.second; ++it)
            it->second.show_icon();
        }
        break;
      case work_request::work_type::delete_scene:
        {
          auto& name = req.names[0];
          if (auto it = scenes.find(name); it != scenes.end()) {
            auto nr = it->second.nr;
            scenes.erase(it);
            for (auto& s : scenes)
              if (s.second.nr >= nr)
                --s.second.nr;
            for (auto& b : scene_live_buttons)
              if (b.second.nr >= nr)
                b.second.show_icon();
            for (auto& b : scene_preview_buttons)
              if (b.second.nr >= nr)
                b.second.show_icon();
          }
        }
        break;
      case work_request::work_type::recording:
        is_recording = req.nr != 0;
        button_update(button_class::record);
        if (! is_recording && ! open.empty()) {
          std::filesystem::path fname(req.names[0]);
          static const char pattern[] = "%URL%";
          auto cmd = open;
          if (auto n = cmd.find(pattern); n != std::string::npos)
            cmd.replace(n, sizeof(pattern), "file://"s + fname.parent_path().string());
          system(cmd.c_str());
        }
        break;
      case work_request::work_type::streaming:
        is_streaming = req.nr != 0;
        button_update(button_class::record);
        break;
      case work_request::work_type::sceneschanged:
        scenes.clear();
        for (auto& s : req.names)
          scenes.emplace(std::piecewise_construct, std::forward_as_tuple(s), std::forward_as_tuple(1 + scenes.size(), s));
        batch.clear();
        batch["request-type"] = "ExecuteBatch";
        d["request-type"] = "GetCurrentScene";
        batch["requests"].append(d);
        d.clear();
        d["request-type"] = "GetPreviewScene";
        batch["requests"].append(d);
        std::cout << "batch = " << batch << std::endl;
        if (auto res = obsws::call(batch); res["status"] == "ok") {
          if (res["results"][0]["status"] == "ok")
            current_scene = res["results"][0]["name"].asString();
          if (res["results"][1]["status"] == "ok")
            current_preview = res["results"][1]["name"].asString();
        }
        button_update(button_class::live | button_class::preview);
        break;
      case work_request::work_type::studiomode:
        studio_mode = req.nr;
        d["request-type"] = studio_mode ? "GetPreviewScene" : "GetCurrentScene";
        if (auto res = obsws::call(d); res["status"] == "ok") {
          if (studio_mode)
            current_preview = res["name"].asString();
          current_sources.clear();
          for (const auto& s : res["sources"]) {
            current_sources.emplace_back(s["name"].asString());
            current_sources.emplace_back(s["render"].asBool() ? "true"s : "false"s);
          }
        }
        button_update(button_class::all ^ button_class::live ^ button_class::record ^ button_class::transition);
        break;
      case work_request::work_type::sourcename:
        for (size_t i = 0; 2 * i < current_sources.size(); ++i)
          if (current_sources[i] == req.names[0]) {
            current_sources[i] = req.names[1];
            for (auto& e : source_buttons)
              if (e.second.nr == 1 + i)
                e.second.show_icon();
            break;
          }
        break;
      case work_request::work_type::transitionend:
        if (ignore_next_transition_change && req.names[1] == "Cut") {
          ignore_next_transition_change = false;
          batch.clear();
          batch["request-type"] = "ExecuteBatch";
          d["request-type"] = "SetCurrentTransition";
          d["transition-name"] = current_transition;
          batch["requests"].append(d);
          d.clear();
          d["request-type"] = "SetTransitionDuration";
          d["duration"] = current_duration_ms;
          batch["requests"].append(d);
          obsws::emit(batch);
        }
        break;
      case work_request::work_type::duration:
        if (! ignore_next_transition_change) {
          current_duration_ms = req.nr;
          for (auto& b : auto_buttons)
            b.show_icon();
        }
        break;
      case work_request::work_type::sourceorder:
        if ((studio_mode && req.names[0] == current_preview) || (!studio_mode && req.names[0] == current_scene)) {
          req.names.erase(req.names.begin());
          auto it = req.names.begin();
          while (it != req.names.end()) {
            auto it2 = std::find(current_sources.begin(), current_sources.end(), *it);
            assert(it2 != current_sources.end());
            it = req.names.emplace(it + 1, std::move(*(it2 + 1)));
            ++it;
          }
          current_sources = std::move(req.names);
          button_update(button_class::sources);
        }
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

      auto challenge = resp["challenge"].asString();
      SHA256_Init(&shactx);
      SHA256_Update(&shactx, enchashbuf, enclen);
      SHA256_Update(&shactx, challenge.c_str(), challenge.size());
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
    d["request-type"] = "GetStudioModeStatus";
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
    d["request-type"] = "GetTransitionDuration";
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

    studio_mode = resp["results"][0]["status"] == "ok" && resp["results"][0]["studio-mode"].asBool();

    auto& scenelist = resp["results"][1];
    if (scenelist.isMember("status") && scenelist["status"] == "ok") {
      bool has_Black = false;
      current_scene = scenelist["current-scene"].asString();
      auto& escenes = scenelist["scenes"];
      for (const auto& s : escenes) {
        auto name = s["name"].asString();
        if (name == "Black")
          has_Black = true;
        else
          scenes.emplace(std::piecewise_construct, std::forward_as_tuple(name), std::forward_as_tuple(1 + scenes.size(), name)); 

        if (name == (studio_mode ? current_preview : current_scene)) {
          current_sources.clear();
          for (const auto& src : s["sources"]) {
            current_sources.emplace_back(src["name"].asString());
            current_sources.emplace_back(src["render"].asBool() ? "true" : "false");
          }
        }
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
    if (ctransition.isMember("status") && ctransition["status"] == "ok")
      current_transition = ctransition["name"].asString();

    auto& dtransition = resp["results"][4];
    if (dtransition.isMember("status") && dtransition["status"] == "ok")
      current_duration_ms = dtransition["transition-duration"].asUInt();

    auto previewscene = resp["results"][5];
    if (previewscene.isMember("status") && previewscene["status"] == "ok")
      current_preview = previewscene["name"].asString();

    auto recordingstatus = resp["results"][6];
    if (recordingstatus.isMember("status") && recordingstatus["status"] == "ok")
      is_recording = recordingstatus["isRecording"].asBool() && ! recordingstatus["isRecordingPaused"].asBool();

    auto streamingstatus = resp["results"][7];
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


  button* info::parse_key(set_key_image_cb setkey, unsigned page, unsigned row, unsigned column, const libconfig::Setting& config)
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
    if (function == "scene-live"){
      if (icon1.empty()) {
        icon1 = "scene_live.png";
        if (icon2.empty())
          icon2 = "scene_live_off.png";
      }
      std::string font;
      if (config.exists("font"))
        config.lookupValue("font", font);
      else
      	font = obsfont;
      unsigned nr = 1u + scene_live_buttons.size();
      return &scene_live_buttons.emplace(nr, scene_button(nr, setkey, this, page, row, column, icon1, icon2, keyop_type::live_scene, ftobj, font))->second;
    } else if (function == "scene-preview") {
      if (icon1.empty()) {
        icon1 = "scene_preview.png";
        if (icon2.empty())
          icon2 = "scene_preview_off.png";
      }
      std::string font;
      if (config.exists("font"))
        config.lookupValue("font", font);
      else
      	font = obsfont;
      unsigned nr = 1u + scene_preview_buttons.size();
      return &scene_preview_buttons.emplace(nr, scene_button(nr, setkey, this, page, row, column, icon1, icon2, keyop_type::preview_scene, ftobj, font))->second;
    } else if (function == "scene-cut") {
      if (icon1.empty())
        icon1 = "cut.png";
      return &cut_buttons.emplace_back(0, setkey, this, page, row, column, icon1, icon1, keyop_type::cut);
    } else if (function == "scene-auto") {
      if (icon1.empty())
        icon1 = "auto.png";
      std::string font;
      if (config.exists("font"))
      	config.lookupValue("font", font);
      else
      	font = obsfont;
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
      return &auto_buttons.emplace_back(0, setkey, this, page, row, column, icon1, keyop_type::auto_rate, ftobj, font, color, std::move(center), current_duration_ms);
    } else if (function == "scene-ftb") {
      if (icon1.empty())
        icon1 = icon2 = "ftb.png";
      return &ftb_buttons.emplace_back(0, setkey, this, page, row, column, icon1, icon1, keyop_type::ftb);
    } else if (function == "transition") {
      if (icon1.empty()) {
        icon1 = "transition.png";
        if (icon2.empty())
          icon2 = "transition_off.png";
      }
      std::string font;
      if (config.exists("font"))
        config.lookupValue("font", font);
      else
      	font = obsfont;
      unsigned nr = 1u + transition_buttons.size();
      return &transition_buttons.emplace(nr, transition_button(nr, setkey, this, page, row, column, icon1, icon2, keyop_type::transition, ftobj, font))->second;
    } else if (function == "source") {
      if (icon1.empty()) {
        icon1 = "source.png";
        if (icon2.empty())
          icon2 = "source_off.png";
      }
      std::string font;
      if (config.exists("font"))
        config.lookupValue("font", font);
      else
        font = obsfont;
      unsigned nr = 1u + source_buttons.size();
      return &source_buttons.emplace(nr, source_button(nr, setkey, this, page, row, column, icon1, icon2, keyop_type::source, ftobj, font))->second;
    } else if (function == "toggle-record") {
      if (icon1.empty()) {
        icon1 = "record.png";
        if (icon2.empty())
          icon2 = "record_off.png";
      }
      return &record_buttons.emplace_back(0, setkey, this, page, row, column, icon1, icon2, keyop_type::record);
    } else if (function == "toggle-stream") {
      if (icon1.empty()) {
        icon1 = "stream.png";
        if (icon2.empty())
          icon2 = "stream_off.png";
      }
      return &record_buttons.emplace_back(0, setkey, this, page, row, column, icon1, icon2, keyop_type::stream);
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
      worker_queue.emplace(work_request::work_type::scene, 0, std::vector{ val["to-scene"].asString(), val["from-scene"].asString() });
      worker_cv.notify_all();
    } else if (update_type == "PreviewSceneChanged") {
      std::vector<std::string> vs{ val["scene-name"].asString() };
      for (const auto& s : val["sources"]) {
        vs.emplace_back(s["name"].asString());
        vs.emplace_back(s["render"].asBool() ? "true" : "false");
      }
      std::lock_guard<std::mutex> guard(worker_m);
      worker_queue.emplace(work_request::work_type::preview, 0, vs);
      worker_cv.notify_all();
    } else if (update_type == "SwitchTransition") {
      if (handle_next_transition_change.test_and_set()) {
        std::lock_guard<std::mutex> guard(worker_m);
        worker_queue.emplace(work_request::work_type::transition, 0, std::vector{ val["transition-name"].asString() });
        worker_cv.notify_all();
      }
    } else if (update_type == "Exiting")
      connection_update(false);
    else if (update_type == "TransitionDurationChanged") {
      std::lock_guard<std::mutex> guard(worker_m);
      worker_queue.emplace(work_request::work_type::duration, val["new-duration"].asUInt());
      worker_cv.notify_all();
    } else if (update_type == "SourceCreated" || update_type == "SourceDestroyed") {
      if (val["sourceType"] == "scene") {
        std::lock_guard<std::mutex> guard(worker_m);
        worker_queue.emplace(update_type == "SourceCreated" ? work_request::work_type::new_scene : work_request::work_type::delete_scene, 0, std::vector{ val["sourceName"].asString() });
        worker_cv.notify_all();
      }
    } else if (update_type == "RecordingStarted" || update_type == "RecordingStopped") {
      std::lock_guard<std::mutex> guard(worker_m);
      worker_queue.emplace(work_request::work_type::recording, update_type == "RecordingStarted", std::vector{ val["recordingFilename"].asString() });
      worker_cv.notify_all();
    } else if (update_type == "StreamStarted" || update_type == "StreamStopped") {
      std::lock_guard<std::mutex> guard(worker_m);
      worker_queue.emplace(work_request::work_type::streaming, update_type == "StreamStarted");
      worker_cv.notify_all();
    } else if (update_type == "ScenesChanged") {
      std::vector<std::string> newscenes;
      for (auto& s : val["scenes"])
        newscenes.emplace_back(s["name"].asString());
      std::lock_guard<std::mutex> guard(worker_m);
      worker_queue.emplace(work_request::work_type::sceneschanged, 0, newscenes);
      worker_cv.notify_all();
    } else if (update_type == "StudioModeSwitched") {
      std::lock_guard<std::mutex> guard(worker_m);
      worker_queue.emplace(work_request::work_type::studiomode, val["new-state"].asBool());
      worker_cv.notify_all();
    } else if (update_type == "SwitchScenes") {
      std::vector<std::string> vs{ val["scene-name"].asString() };
      for (const auto& s : val["sources"]) {
        vs.emplace_back(s["name"].asString());
        vs.emplace_back(s["render"].asBool() ? "true" : "false");
      }
      std::lock_guard<std::mutex> guard(worker_m);
      worker_queue.emplace(work_request::work_type::scenecontent, 0, std::move(vs));
      worker_cv.notify_all();
    } else if (update_type == "SceneItemVisibilityChanged") {
      std::vector<std::string> vs{ val["scene-name"].asString(), val["item-name"].asString(), val["item-visible"].asString() };
      std::lock_guard<std::mutex> guard(worker_m);
      worker_queue.emplace(work_request::work_type::visible, 0, std::move(vs));
      worker_cv.notify_all();
    } else if (update_type == "SceneItemTransformChanged") {
      std::vector<std::string> vs{ val["scene-name"].asString(), val["item-name"].asString(), val["transform"]["visible"].asString() };
      std::lock_guard<std::mutex> guard(worker_m);
      worker_queue.emplace(work_request::work_type::visible, 0, std::move(vs));
      worker_cv.notify_all();
    } else if (update_type == "SourceRenamed") {
      std::vector<std::string> vs{ val["previousName"].asString(), val["newName"].asString() };
      std::lock_guard<std::mutex> guard(worker_m);
      worker_queue.emplace(work_request::work_type::sourcename, 0, std::move(vs));
      worker_cv.notify_all();
    } else if (update_type == "TransitionEnd") {
      std::vector<std::string> vs{ val["to-scene"].asString(), val["name"].asString() };
      std::lock_guard<std::mutex> guard(worker_m);
      worker_queue.emplace(work_request::work_type::transitionend, 0, std::move(vs));
      worker_cv.notify_all();
    } else if (update_type == "SourceOrderChanged") {
      std::vector<std::string> vs{ val["scene-name"].asString() };
      for (const auto& s : val["scene-items"])
        vs.emplace(vs.begin() + 1, s["source-name"].asString());
      std::lock_guard<std::mutex> guard(worker_m);
      worker_queue.emplace(work_request::work_type::sourceorder, 0, std::move(vs));
      worker_cv.notify_all();
    } else if (log_unknown_events)
      std::cout << "info::callback unhandled event = " << val << std::endl;
  }


  // This function is executed by the obsws thread.  It should only use the worker_queue to
  // affect the state of the object.
  void info::connection_update(bool connected_)
  {
    if (connected == connected_)
      return;

    connected = connected_;

    std::lock_guard<std::mutex> guard(worker_m);
    worker_queue.emplace(work_request::work_type::new_session);
    worker_queue.emplace(work_request::work_type::buttons);
    worker_cv.notify_all();
  }

} // namespace obs
