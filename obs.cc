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


  button::button(unsigned nr_, set_key_image_cb setkey_image_, set_key_handle_cb setkey_handle_, info* i_, unsigned page_, unsigned row_, unsigned column_, int icon1_, int icon2_, keyop_type keyop_)
  : nr(nr_), setkey_image(setkey_image_), setkey_handle(setkey_handle_), i(i_), page(page_), row(row_), column(column_), icon1(icon1_), icon2(icon2_), keyop(keyop_)
  {
  }


  void button::show_icon()
  {
    auto icon = i->obsicon;

    if (i->connected) {
      // std::cout << "update connected\n";
      if (i->studio_mode && (keyop == keyop_type::live_scene || keyop == keyop_type::preview_scene)) {
        if (nr <= i->scene_count()) {
          bool active;
          if (keyop == keyop_type::live_scene)
            active = i->get_current_scene().nr == nr;
          else
            active = i->get_current_preview().nr == nr;
          icon = active ? icon1 : icon2;
        }
        // else std::cout << "scene out of range\n";
      } else if (keyop == keyop_type::record)
        icon = i->is_recording ? icon1 : icon2;
      else if (keyop == keyop_type::stream)
        icon = i->is_streaming ? icon1 : icon2;
      else if (keyop == keyop_type::virtualcam)
        icon = i->provide_virtualcam ? icon1 : icon2;
      else if (keyop == keyop_type::ftb)
        icon = i->ftb.active() ? i->ftb.get() : icon1;
      else if (! i->ftb.active() && i->studio_mode)
        icon = icon1;
    }

    setkey_handle(page, row, column, icon);
  }


  void button::call()
  {
    if (! i->connected)
      return;

    if (! i->studio_mode && keyop != keyop_type::live_scene && keyop != keyop_type::record && keyop != keyop_type::stream && keyop != keyop_type::source && keyop != keyop_type::transition && keyop != keyop_type::ftb)
      return;

    Json::Value batch;
    Json::Value d;
    std::string old;

    switch(keyop) {
    case keyop_type::live_scene:
      if (nr <= i->scene_count()) {
        if (i->ftb.active()) {
          if (! i->studio_mode && i->saved_scene != i->get_scene_name(nr)) {
            auto& oldscene = i->scenes[i->saved_scene];
            i->saved_scene = i->get_scene_name(nr);
            for (auto& b : i->scene_live_buttons)
              if (b.second.nr == oldscene.nr)
                b.second.show_icon();
            show_icon();
          }
        } else {
          d["request-type"] = "SetCurrentScene";
          d["scene-name"] = i->get_scene_name(nr);
          obsws::emit(d);
        }
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
      if (! i->ftb.active()) {
        i->ignore_next_transition_change = true;
        d["request-type"] = "TransitionToProgram";
        d["with-transition"]["name"] = "Cut";
        d["with-transition"]["duration"] = 0u;
        obsws::emit(d);
      }
      break;
    case keyop_type::auto_rate:
      if (! i->ftb.active()) {
        d["request-type"] = "TransitionToProgram";
        d["with-transition"]["name"] = i->get_current_transition().name;
        d["with-transition"]["duration"] = i->get_current_duration();
        obsws::emit(d);
      }
      break;
    case keyop_type::ftb:
      i->ignore_next_transition_change = true;
      if (! i->ftb.active()) {
        i->saved_preview = i->current_preview;
        i->saved_scene = i->current_scene;
        if (i->studio_mode) {
          batch["request-type"] = "ExecuteBatch";
          d.clear();
          d["request-type"] = "SetPreviewScene";
          d["scene-name"] = "Black";
          batch["requests"].append(d);
          d.clear();
          d["request-type"] = "TransitionToProgram";
          d["with-transition"]["name"] = "Fade";
          d["with-transition"]["duration"] = 1000;
          batch["requests"].append(d);
          obsws::emit(batch);
        } else {
          d.clear();
          d["request-type"] = "SetCurrentScene";
          d["scene-name"] = "Black";
          obsws::emit(d);
        }
        i->ftb.start();
      } else {
        i->ftb.stop();
        if (i->studio_mode) {
          d.clear();
          d["request-type"] = "TransitionToProgram";
          d["with-transition"]["name"] = "Fade";
          d["with-transition"]["duration"] = 1000;
          obsws::emit(d);
        } else {
          batch["request-type"] = "ExecuteBatch";
          d.clear();
          d["request-type"] = "SetCurrentTransition";
          d["transition-name"] = "Fade";
          batch["requests"].append(d);
          d.clear();
          d["request-type"] = "SetTransitionDuration";
          d["duration"] = 1000;
          batch["requests"].append(d);
          d.clear();
          d["request-type"] = "SetCurrentScene";
          d["scene-name"] = i->saved_scene;
          i->saved_scene.clear();
          batch["requests"].append(d);
          obsws::emit(batch);
        }
      }
      break;
    case keyop_type::transition:
      if (! i->ftb.active()) {
        d["request-type"] = "SetCurrentTransition";
        d["transition-name"] = i->get_transition_name(nr);
        obsws::emit(d);
      }
      break;
    case keyop_type::record:
      d["request-type"] = "StartStopRecording";
      obsws::emit(d);
      break;
    case keyop_type::stream:
      d["request-type"] = "StartStopStreaming";
      obsws::emit(d);
      break;
    case keyop_type::virtualcam:
      d["request-type"] = "StartStopVirtualCam";
      obsws::emit(d);
      break;
    case keyop_type::source:
      if (2 * (nr - 1) < i->current_sources.size() && (! i->ftb.active() || i->studio_mode)) {
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
    if (i->connected && i->studio_mode && ! i->ftb.active()) {
      font_render<render_to_image> renderobj(fontobj, background, 0.8, 0.3);
      auto s = std::to_string(duration_ms / 1000.0);
      if (s.size() == 1)
        s += ".0";
      else if (s.size() > 3)
        s.erase(3);
      setkey_image(page, row, column, renderobj.draw(s, color, std::get<0>(center), std::get<1>(center)));
    } else
      setkey_handle(page, row, column, i->obsicon);
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
          font_render<render_to_image> renderobj(fontobj, background, 0.8, 0.8);
          setkey_image(page, row, column, renderobj.draw(vs, keyop == keyop_type::live_scene ? i->im_white : i->im_black, 0.5, 0.5));
        } else {
          font_render<render_to_image> renderobj(fontobj, background_off, 0.8, 0.8);
          setkey_image(page, row, column, renderobj.draw(vs, i->im_darkgray, 0.5, 0.5));
        }
        return;
      }
    }
    setkey_handle(page, row, column, keyop == keyop_type::live_scene ? i->live_unused_icon : (! i->connected || i->studio_mode ? i->preview_unused_icon : i->obsicon));
  }


  void transition_button::show_icon()
  {
    if (i->connected && ! i->ftb.active()) {
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
          font_render<render_to_image> renderobj(fontobj, background, 0.8, 0.8);
          setkey_image(page, row, column, renderobj.draw(vs, i->im_black, 0.5, 0.5));
        } else {
          font_render<render_to_image> renderobj(fontobj, background_off, 0.8, 0.8);
          setkey_image(page, row, column, renderobj.draw(vs, i->im_darkgray, 0.5, 0.5));
        }
        return;
      }
    }
    setkey_handle(page, row, column, i->transition_unused_icon);
  }


  void source_button::show_icon()
  {
    if (i->connected && (! i->ftb.active() || i->studio_mode)) {
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
          font_render<render_to_image> renderobj(fontobj, background, 0.8, 0.8);
          setkey_image(page, row, column, renderobj.draw(vs, i->im_black, 0.5, 0.5));
        } else {
          font_render<render_to_image> renderobj(fontobj, background_off, 0.8, 0.8);
          setkey_image(page, row, column, renderobj.draw(vs, i->im_darkgray, 0.5, 0.5));
        }
        return;
      }
    }
    setkey_handle(page, row, column, i->source_unused_icon);
  }


  info::info(const libconfig::Setting& config, ftlibrary& ftobj_, register_image_cb register_image_)
  : register_image(register_image_), ftobj(ftobj_), im_black("black"), im_white("white"), im_darkgray("darkgray"),
    obsicon(register_image(find_image("obs.png"))),
    live_unused_icon(register_image(find_image("scene_live_unused.png"))),
    preview_unused_icon(register_image(find_image("scene_preview_unused.png"))),
    source_unused_icon(register_image(find_image("source_unused.png"))),
    transition_unused_icon(register_image(find_image("transition_unused.png"))),
    ftb { .icons = { register_image(find_image("ftb-0.png")), register_image(find_image("ftb-12.png")),
                     register_image(find_image("ftb-25.png")), register_image(find_image("ftb-37.png")),
                     register_image(find_image("ftb-50.png")), register_image(find_image("ftb-62.png")),
                     register_image(find_image("ftb-75.png")), register_image(find_image("ftb-87.png")),
                     register_image(find_image("ftb-100.png")) } },
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
    static constexpr auto cycle_time = 75ms;

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
          if (to < now)
            to = now + cycle_time;
        } else
          // We are likely in a middle of a cycle, redraw the inactive icon
          button_update(button_class::ftb);

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
        [[ fallthrough ]];
      case work_request::work_type::buttons:
        button_update(button_class::all);
        break;
      case work_request::work_type::scene:
        {
          if (ftb.active())
            ftb.stop();

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
          auto _ = system(cmd.c_str());
          (void)_;
        }
        break;
      case work_request::work_type::streaming:
        is_streaming = req.nr != 0;
        button_update(button_class::record);
        break;
      case work_request::work_type::virtualcam:
        provide_virtualcam = req.nr != 0;
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
        } else if (ignore_next_transition_change && req.names[1] == "Fade") {
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
          if (req.names[2] != "Black" && studio_mode) {
            d.clear();
            d["request-type"] = "SetPreviewScene";
            d["scene-name"] = saved_preview;
            saved_preview.clear();
            batch["requests"].append(d);
          }
          obsws::emit(batch);
          button_update(button_class::ftb | button_class::live | button_class::preview | button_class::cut | button_class::auto_ | button_class::transition);
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
      std::unique_ptr<EVP_MD_CTX, void(*)(EVP_MD_CTX*)> ctx { EVP_MD_CTX_create(), &EVP_MD_CTX_free };
      if (ctx == nullptr)
        return;

      if (EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1)
        return;
      auto salt = resp["salt"].asCString();
      if (EVP_DigestUpdate(ctx.get(), password.c_str(), password.size()) != 1 ||
          EVP_DigestUpdate(ctx.get(), salt, strlen(salt)) != 1)
        return;
      unsigned char hashbuf[EVP_MD_size(EVP_sha256())];
      unsigned hashbuflen = sizeof(hashbuf);
      if (EVP_DigestFinal_ex(ctx.get(), hashbuf, &hashbuflen) != 1)
        return;

      unsigned char enchashbuf[(sizeof(hashbuf) + 2) / 3 * 4 + 1];
      auto enclen = EVP_EncodeBlock(enchashbuf, hashbuf, hashbuflen);

      if (EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1)
        return;
      auto challenge = resp["challenge"].asString();
      if (EVP_DigestUpdate(ctx.get(), enchashbuf, enclen) != 1 ||
          EVP_DigestUpdate(ctx.get(), challenge.c_str(), challenge.size()) != 1)
        return;
      hashbuflen = sizeof(hashbuf);
      if (EVP_DigestFinal_ex(ctx.get(), hashbuf, &hashbuflen) != 1)
        return;

      EVP_EncodeBlock(enchashbuf, hashbuf, hashbuflen);

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

    d.clear();
    d["request-type"] = "GetPreviewScene";
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
    d["request-type"] = "GetStreamingStatus";
    batch["requests"].append(d);

    resp = obsws::call(batch);
    if (! resp.isMember("status") || resp["status"] != "ok")
      return;

    studio_mode = resp["results"][0]["status"] == "ok" && resp["results"][0]["studio-mode"].asBool();

    auto& previewscene = resp["results"][1];
    if (previewscene.isMember("status") && previewscene["status"] == "ok")
      current_preview = previewscene["name"].asString();

    auto& scenelist = resp["results"][2];
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

      d.clear();
      d["request-type"] = "SetSceneTransitionOverride";
      d["sceneName"] = "Black";
      d["transitionName"] = "Fade";
      d["transitionDuration"] = 1000;
      obsws::emit(d);
    }

    auto& transitionlist = resp["results"][3];
    if (transitionlist.isMember("status") && transitionlist["status"] == "ok")
      for (auto& t : transitionlist["transitions"])
        if (auto name = t["name"].asString(); name != "Cut")
          transitions.emplace(std::piecewise_construct, std::forward_as_tuple(name), std::forward_as_tuple(1 + transitions.size(), name));

    auto& ctransition = resp["results"][4];
    if (ctransition.isMember("status") && ctransition["status"] == "ok")
      current_transition = ctransition["name"].asString();

    auto& dtransition = resp["results"][5];
    if (dtransition.isMember("status") && dtransition["status"] == "ok")
      current_duration_ms = dtransition["transition-duration"].asUInt();

    auto& streamingstatus = resp["results"][6];
    if (streamingstatus.isMember("status") && streamingstatus["status"] == "ok") {
      is_streaming = streamingstatus["streaming"].asBool();
      is_recording = streamingstatus["recording"].asBool() && ! streamingstatus["recording-paused"].asBool();
      provide_virtualcam = streamingstatus["virtualcam"].asBool();
    }

    connected = true;
  }


  void info::add_scene(unsigned idx, const char* name)
  {
    assert(! scenes.contains(name));
    auto sname = std::string(name);
    scenes.emplace(std::piecewise_construct, std::forward_as_tuple(name), std::forward_as_tuple(idx, name));
  }


  button* info::parse_key(set_key_image_cb setkey_image, set_key_handle_cb setkey_handle, unsigned page, unsigned row, unsigned column, const libconfig::Setting& config)
  {
    if (! config.exists("function"))
      return nullptr;

    auto function = std::string(config["function"]);
    std::string icon1name;
    std::string icon2name;
    int icon1;
    int icon2;
    config.lookupValue("icon1", icon1name);
    if (config.exists("icon2"))
      config.lookupValue("icon2", icon2name);
    else
      icon2name = icon1name;
    if (function == "scene-live"){
      if (icon1name.empty()) {
        icon1name = "scene_live.png";
        if (icon2name.empty())
          icon2name = "scene_live_off.png";
      }
      std::string font;
      if (config.exists("font"))
        config.lookupValue("font", font);
      else
      	font = obsfont;
      unsigned nr = 1u + scene_live_buttons.size();
      return &scene_live_buttons.emplace(nr, scene_button(nr, setkey_image, setkey_handle, this, page, row, column, find_image(icon1name), find_image(icon2name), keyop_type::live_scene, ftobj, font))->second;
    } else if (function == "scene-preview") {
      if (icon1name.empty()) {
        icon1name = "scene_preview.png";
        if (icon2name.empty())
          icon2name = "scene_preview_off.png";
      }
      std::string font;
      if (config.exists("font"))
        config.lookupValue("font", font);
      else
      	font = obsfont;
      unsigned nr = 1u + scene_preview_buttons.size();
      return &scene_preview_buttons.emplace(nr, scene_button(nr, setkey_image, setkey_handle, this, page, row, column, find_image(icon1name), find_image(icon2name), keyop_type::preview_scene, ftobj, font))->second;
    } else if (function == "scene-cut") {
      if (icon1name.empty())
        icon1name = "cut.png";
      icon1 = register_image(find_image(icon1name));
      return &cut_buttons.emplace_back(0, setkey_image, setkey_handle, this, page, row, column, icon1, icon1, keyop_type::cut);
    } else if (function == "scene-auto") {
      if (icon1name.empty())
        icon1name = "auto.png";
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
      return &auto_buttons.emplace_back(0, setkey_image, setkey_handle, this, page, row, column, find_image(icon1name), keyop_type::auto_rate, ftobj, font, color, std::move(center), current_duration_ms);
    } else if (function == "scene-ftb") {
      if (icon1name.empty())
        icon1name = "ftb.png";
      icon1 = register_image(find_image(icon1name));
      return &ftb_buttons.emplace_back(0, setkey_image, setkey_handle, this, page, row, column, icon1, icon1, keyop_type::ftb);
    } else if (function == "transition") {
      if (icon1name.empty()) {
        icon1name = "transition.png";
        if (icon2name.empty())
          icon2name = "transition_off.png";
      }
      std::string font;
      if (config.exists("font"))
        config.lookupValue("font", font);
      else
      	font = obsfont;
      unsigned nr = 1u + transition_buttons.size();
      return &transition_buttons.emplace(nr, transition_button(nr, setkey_image, setkey_handle, this, page, row, column, find_image(icon1name), find_image(icon2name), keyop_type::transition, ftobj, font))->second;
    } else if (function == "source") {
      if (icon1name.empty()) {
        icon1name = "source.png";
        if (icon2name.empty())
          icon2name = "source_off.png";
      }
      std::string font;
      if (config.exists("font"))
        config.lookupValue("font", font);
      else
        font = obsfont;
      unsigned nr = 1u + source_buttons.size();
      icon1 = register_image(find_image(icon1name));
      return &source_buttons.emplace(nr, source_button(nr, setkey_image, setkey_handle, this, page, row, column, find_image(icon1name), find_image(icon2name), keyop_type::source, ftobj, font))->second;
    } else if (function == "toggle-record") {
      if (icon1name.empty()) {
        icon1name = "record.png";
        if (icon2name.empty())
          icon2name = "record_off.png";
      }
      icon1 = register_image(find_image(icon1name));
      if (icon1name == icon2name)
        icon2 = icon1;
      else
        icon2 = register_image(find_image(icon2name));
      return &record_buttons.emplace_back(0, setkey_image, setkey_handle, this, page, row, column, icon1, icon2, keyop_type::record);
    } else if (function == "toggle-stream") {
      if (icon1name.empty()) {
        icon1name = "stream.png";
        if (icon2name.empty())
          icon2name = "stream_off.png";
      }
      icon1 = register_image(find_image(icon1name));
      if (icon1name == icon2name)
        icon2 = icon1;
      else
        icon2 = register_image(find_image(icon2name));
      return &record_buttons.emplace_back(0, setkey_image, setkey_handle, this, page, row, column, icon1, icon2, keyop_type::stream);
    } else if (function == "toggle-virtual-cam") {
      if (icon1name.empty()) {
        icon1name = "virtualcam.png";
        if (icon2name.empty())
          icon2name = "virtualcam_off.png";
      }
      icon1 = register_image(find_image(icon1name));
      if (icon1name == icon2name)
        icon2 = icon1;
      else
        icon2 = register_image(find_image(icon2name));
      return &record_buttons.emplace_back(0, setkey_image, setkey_handle, this, page, row, column, icon1, icon2, keyop_type::virtualcam);
    }

    return nullptr;
  }


  scene& info::get_current_scene()
  {
    return scenes[(! studio_mode && ftb.active()) ? saved_scene : current_scene];
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
    std::vector<std::string> vs;
    decltype(work_request::nr) nr = 0;
    work_request::work_type type(work_request::work_type::none);


    auto update_type = val["update-type"]; 
    if (update_type == "TransitionVideoEnd") {
      vs.emplace_back(val["to-scene"].asString());
      vs.emplace_back(val["from-scene"].asString());
      type = work_request::work_type::scene;
    } else if (update_type == "PreviewSceneChanged") {
      vs.emplace_back(val["scene-name"].asString());
      for (const auto& s : val["sources"]) {
        vs.emplace_back(s["name"].asString());
        vs.emplace_back(s["render"].asBool() ? "true" : "false");
      }
      type = work_request::work_type::preview;
    } else if (update_type == "SwitchTransition") {
      if (! handle_next_transition_change.test_and_set())
        return;

      vs.emplace_back(val["transition-name"].asString());
      type = work_request::work_type::transition;
    } else if (update_type == "Exiting")
      connection_update(false);
    else if (update_type == "TransitionDurationChanged") {
      nr = val["new-duration"].asUInt();
      type = work_request::work_type::duration;
    } else if (update_type == "SourceCreated" || update_type == "SourceDestroyed") {
      if (val["sourceType"] == "scene") {
        vs.emplace_back(val["sourceName"].asString());
        type = update_type == "SourceCreated" ? work_request::work_type::new_scene : work_request::work_type::delete_scene;
      } else
        return;
    } else if (update_type == "RecordingStarted" || update_type == "RecordingStopped") {
      vs.emplace_back(val["recordingFilename"].asString());
      nr = update_type == "RecordingStarted";
      type = work_request::work_type::recording;
    } else if (update_type == "StreamStarted" || update_type == "StreamStopped") {
      nr = update_type == "StreamStarted";
      type = work_request::work_type::streaming;
    } else if (update_type == "VirtualCamStarted" || update_type == "VirtualCamStopped") {
      nr = update_type == "VirtualCamStarted";
      type = work_request::work_type::virtualcam;
    } else if (update_type == "ScenesChanged") {
      for (auto& s : val["scenes"])
        if (s["name"] != "Black")
          vs.emplace_back(s["name"].asString());
      type = work_request::work_type::sceneschanged;
    } else if (update_type == "StudioModeSwitched") {
      nr = val["new-state"].asBool();
      type = work_request::work_type::studiomode;
    } else if (update_type == "SwitchScenes") {
      vs.emplace_back(val["scene-name"].asString());
      for (const auto& s : val["sources"]) {
        vs.emplace_back(s["name"].asString());
        vs.emplace_back(s["render"].asBool() ? "true" : "false");
      }
      type = work_request::work_type::scenecontent;
    } else if (update_type == "SceneItemVisibilityChanged") {
      vs.emplace_back(val["scene-name"].asString());
      vs.emplace_back(val["item-name"].asString());
      vs.emplace_back(val["item-visible"].asString());
      type = work_request::work_type::visible;
    } else if (update_type == "SceneItemTransformChanged") {
      vs.emplace_back(val["scene-name"].asString());
      vs.emplace_back(val["item-name"].asString());
      vs.emplace_back(val["transform"]["visible"].asString());
      type = work_request::work_type::visible;
    } else if (update_type == "SourceRenamed") {
      vs.emplace_back(val["previousName"].asString());
      vs.emplace_back(val["newName"].asString());
      type = work_request::work_type::sourcename;
    } else if (update_type == "TransitionEnd") {
      vs.emplace_back(val["to-scene"].asString());
      vs.emplace_back(val["name"].asString());
      vs.emplace_back(val["to-scene"].asString());
      type = work_request::work_type::transitionend;
    } else if (update_type == "SourceOrderChanged") {
      vs.emplace_back(val["scene-name"].asString());
      for (const auto& s : val["scene-items"])
        vs.emplace(vs.begin() + 1, s["source-name"].asString());
      type = work_request::work_type::sourceorder;
    } else {
      if (log_unknown_events)
        std::cout << "info::callback unhandled event = " << val << std::endl;
      return;
    }

    std::lock_guard<std::mutex> guard(worker_m);
    worker_queue.emplace(type, nr, std::move(vs));
    worker_cv.notify_all();
  }


  // This function is executed by the obsws thread.  It should only use the worker_queue to
  // affect the state of the object.
  void info::connection_update(bool connected_)
  {
    if (connected == connected_)
      return;

    std::lock_guard<std::mutex> guard(worker_m);
    if (connected_)
      worker_queue.emplace(work_request::work_type::new_session);
    else {
      connected = false;
      worker_queue.emplace(work_request::work_type::buttons);
    }
    worker_cv.notify_all();
  }

} // namespace obs
