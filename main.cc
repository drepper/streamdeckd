#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <regex>

#include <error.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/poll.h>

#include <libconfig.h++>
#include <giomm.h>
extern "C" {
  // libxdo is not prepared for C++ and the X headers define stray macros.
#include <xdo.h>
#undef BadRequest
}
#include <keylightpp.hh>
#include <streamdeckpp.hh>
#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>
#include <X11/extensions/scrnsaver.h>
#include <X11/extensions/XInput2.h>

#include "obs.hh"
#include "ftlibrary.hh"
extern "C" {
#include "resources.h"
}

// XYZ Debug
// #include <iostream>


using namespace std::string_literals;


static_assert(__cpp_static_assert >= 200410L, "extended static_assert missing");
static_assert(__cpp_lib_filesystem >= 201703);
static_assert(__cpp_lib_make_unique >= 201304L);
static_assert(__cpp_range_based_for >= 200907);


namespace {

  const std::filesystem::path resource_ns("/org/akkadia/streamdeckd");


  std::filesystem::path get_homedir()
  {
    auto homedir = getenv("HOME");
    if (homedir != nullptr && *homedir != '\0')
      return homedir;

    auto pwd = getpwuid(getuid());
    if (pwd != nullptr)
      return pwd->pw_dir;

    // Better than nothing
    return std::filesystem::current_path();
  }

} // anonymous namespace



Magick::Image find_image(const std::filesystem::path& path)
{
  if (! path.is_relative())
    return Magick::Image(path);

  try {
    auto data = Gio::Resource::lookup_data_global(resource_ns / path);
    gsize size;
    auto ptr = static_cast<const char*>(data->get_data(size));
    return Magick::Image(Magick::Blob(ptr, size));
  }
  catch (Glib::Error&) {
  }
  try {
    return Magick::Image(get_homedir() / "Pictures" / path);
  }
  catch (Magick::ErrorBlob&) {
  }
  return Magick::Image(std::filesystem::path("/usr/share/pixmaps") / path);
}


namespace {

  struct deck_config;


  struct action {
    action(unsigned k, const libconfig::Setting& setting, streamdeck::device_type& dev_, const char* default_icon = nullptr) : key(k), dev(dev_)
    {
      std::string iconname;
      if (! setting.lookupValue("icon", iconname)) {
        if (default_icon == nullptr)
          return;
        iconname = default_icon;
      }
      icon1 = dev.register_image(find_image(iconname));
    }
    virtual ~action() { }

    virtual void call() = 0;

    virtual void show_icon()
    {
      dev.set_key_image(key, icon1);
    }

  protected:
    unsigned key;
    streamdeck::device_type& dev;
    int icon1;
  };


  struct keylight_toggle final : public action {
    using base_type = action;

    keylight_toggle(unsigned k, const libconfig::Setting& setting, streamdeck::device_type& dev_, bool has_serial, std::string& serial_, keylightpp::device_list_type& keylights_)
    : base_type(k, setting, dev_), serial(has_serial ? serial_ : ""), keylights(keylights_)
    {
      nkeylights = 0;
      for (auto& d : keylights)
        if (serial.empty() || serial == d.serial)
          ++nkeylights;

      std::string icon1name;
      if (! setting.lookupValue("icon_on", icon1name))
        icon1name = "bulb_on.png";
      icon1 = dev.register_image(find_image(icon1name));

      if (nkeylights == 1) {
        std::string icon2name;
        if (! setting.lookupValue("icon_off", icon2name))
          icon2name = "bulb_off.png";
        icon2 = dev.register_image(find_image(icon2name));
      } else
        icon2 = icon1;
    }

    void call() override
    {
      bool any = false;
      for (auto& d : keylights)
        if (serial.empty() || serial == d.serial) {
          d.toggle();
          any = true;
        }
      if (any && nkeylights == 1)
        show_icon();
    }

    void show_icon() override
    {
      dev.set_key_image(key, nkeylights > 1 || ! keylights.front().state() ? icon1 : icon2);
    }
  private:
    const std::string serial;
    keylightpp::device_list_type& keylights;
    unsigned nkeylights;
    int icon2;
  };


  struct keylight_color final : public action {
    using base_type = action;

    keylight_color(unsigned k, const libconfig::Setting& setting, streamdeck::device_type& dev_, bool has_serial, std::string& serial_, keylightpp::device_list_type& keylights_, int inc_)
    : base_type(k, setting, dev_, inc_ >= 0 ? "color+.png" : "color-.png"), serial(has_serial ? serial_ : ""), keylights(keylights_), inc(inc_)
    {
    }

    void call() override {
      for (auto& d : keylights)
        if (serial.empty() || serial == d.serial) {
          if (inc < 0)
            d.color_dec(unsigned(-inc));
          else
            d.color_inc(unsigned(inc));
        }
    }
  private:
    const std::string serial;
    keylightpp::device_list_type& keylights;
    const int inc;
  };


  struct keylight_brightness final : public action {
    using base_type = action;

    keylight_brightness(unsigned k, const libconfig::Setting& setting, streamdeck::device_type& dev_, bool has_serial, std::string& serial_, keylightpp::device_list_type& keylights_, int inc_)
    : base_type(k, setting, dev_, inc_ >= 0 ? "brightness+.png" : "brightness-.png"), serial(has_serial ? serial_ : ""), keylights(keylights_), inc(inc_)
    {
    }

    void call() override {
      for (auto& d : keylights)
        if (serial.empty() || serial == d.serial) {
          if (inc < 0)
            d.brightness_dec(unsigned(-inc));
          else
            d.brightness_inc(unsigned(inc));
        }
    }
  private:
    const std::string serial;
    keylightpp::device_list_type& keylights;
    const int inc;
  };


  struct execute final : public action {
    using base_type = action;

    execute(unsigned k, const libconfig::Setting& setting, streamdeck::device_type& dev_, std::string&& command_) : base_type(k, setting, dev_), command(std::move(command_)) { }

    void call() override {
      auto _ = system(command.c_str());
      (void) _;
    }

  private:
    std::string command;
  };


  struct keypress final : public action {
    using base_type = action;

    keypress(unsigned k, const libconfig::Setting& setting, streamdeck::device_type& dev_, std::string&& sequence, xdo_t* xdo_) : base_type(k, setting, dev_), sequence_list(1, std::move(sequence)), xdo(xdo_) { }
    keypress(unsigned k, const libconfig::Setting& setting, streamdeck::device_type& dev_, std::list<std::string>&& sequence_list_, xdo_t* xdo_) : base_type(k, setting, dev_), sequence_list(std::move(sequence_list_)), xdo(xdo_) { }

    void call() override {
      for (const auto& sequence : sequence_list)
        xdo_send_keysequence_window(xdo, CURRENTWINDOW, sequence.c_str(), 100000);
    }
  private:
    std::list<std::string> sequence_list;
    xdo_t* xdo;
  };


  struct obsaction final : public action {
    using base_type = action;

    obsaction(unsigned k, const libconfig::Setting& setting, streamdeck::device_type& dev_, obs::button* b_) : base_type(k, setting, dev_), b(b_) { }

    void call() override {
      b->call();
    }

    void show_icon() override {
      b->show_icon();
    }

  private:
    obs::button* b;
  };


  struct pageaction final : public action {
    using base_type = action;

    enum struct direction {
      left,
      right,
    };

    pageaction(unsigned k, const libconfig::Setting& setting, streamdeck::device_type& dev_, unsigned to_page_, direction dir, deck_config& deck_)
    : base_type(k, setting, dev_, dir == direction::left ? "left-arrow.png" : "right-arrow.png"), to_page(to_page_), deck(deck_) {}

    void call() override;

  private:
    unsigned to_page;
    deck_config& deck;
  };


  struct deck_config {
    deck_config(const std::filesystem::path& conffile);

    void show_icons();
    void run();

    void nextpage(unsigned to_page);
  private:
    static unsigned keyidx(unsigned page, unsigned k) { return page * 256 + k; }

    void setkey(unsigned page, unsigned row, unsigned column, Magick::Image&& image);
    void setkey(unsigned page, unsigned row, unsigned column, int handle);

    int register_image(Magick::Image&& image);

    void handle_idle();
    bool prohibit_sleep() const {
      return obs && obs->prohibit_sleep();
    }

    enum struct idle {
      running,
      temp,
      full
    };
    idle idle_state = idle::running;
    void idle_dim(idle i);
    unsigned brightness;
    std::chrono::seconds idle_temp_time = std::chrono::seconds{0};
    std::chrono::seconds idle_full_time = std::chrono::seconds{0};
    unsigned brightness_idle;
    std::thread idle_thread;

    streamdeck::context ctx;
    streamdeck::device_type* dev = nullptr;

    bool has_keylights = false;
    keylightpp::device_list_type keylights;
    xdo_t* xdo = nullptr;
    unsigned nrpages = 1;
    unsigned current_page = 0;
    std::map<unsigned,std::unique_ptr<action>> actions;
    std::unique_ptr<obs::info> obs;
    ftlibrary ftobj;
    int blankimg;
  };


  deck_config::deck_config(const std::filesystem::path& conffile)
  {
    libconfig::Config config;
    config.readFile(conffile.c_str());

    std::string serial;
    if (! config.lookupValue("serial", serial))
      serial.clear();

    for (auto& d : ctx) {
      if (! d->connected())
        continue;

      if (serial.empty() || d->get_serial_number() == serial) {
        dev = d.get();
        d->reset();
      }
    }

    if (dev == nullptr)
      throw std::runtime_error("no device available");

    if (! config.lookupValue("pages", nrpages))
      nrpages = 1;

    if (config.exists("obs")) {
      auto& group = config.lookup("obs");
      if (group.isGroup())
        obs = std::make_unique<obs::info>(group, ftobj, [this](Magick::Image&& image) { return register_image(std::move(image)); });
    }

    if (! config.lookupValue("brightness", brightness))
      brightness = 100;
    brightness_idle = brightness;

    if (config.exists("idle"))
      if (auto& idle = config.lookup("idle"); idle.isGroup()) {
        unsigned val;
        if (! idle.lookupValue("away", val))
          idle_temp_time = std::chrono::seconds{0};
        else
          idle_temp_time = std::chrono::seconds{val};
        if (! idle.lookupValue("off", val))
          // The user never wants to totally turn off the display.
          idle_full_time = std::chrono::seconds{100000};
        else
          idle_full_time = std::chrono::seconds{val};

        if (idle_temp_time != std::chrono::seconds{0} || idle_full_time != std::chrono::seconds{0}) {
          if (! idle.lookupValue("brightness", brightness_idle))
            idle_temp_time = std::chrono::seconds{0};

          if (idle_temp_time != std::chrono::seconds{0} || idle_full_time != std::chrono::seconds{0})
            idle_thread = std::thread([this]{ handle_idle(); });
        }
      }

    try {
      auto& keys = config.lookup("keys");

      nrpages = keys.getLength();
      for (const auto& page : keys) {
        unsigned pagenr = page.getIndex();

        for (unsigned k = 0; k < dev->key_count; ++k) {
          auto row = 1u + k / dev->key_cols;
          auto column = 1u + k % dev->key_cols;
          auto keyname = "r"s + std::to_string(row) + "c"s + std::to_string(column);
          if (! page.exists(keyname))
            continue;

          unsigned kidx = keyidx(pagenr, k);

          auto& key = page[keyname];
          if (key.exists("type")) {

            if (std::string(key["type"]) == "keylight" && key.exists("function")) {
              std::string serial;
              bool has_serial = key.lookupValue("serial", serial);

              if (! has_keylights) {
                has_keylights = true;
                for (unsigned t = 0; t < 3; ++t) {
                  keylights = keylightpp::discover();
                  if (keylights.begin() != keylights.end())
                    break;
                  sleep(1);
                }
                if (keylights.begin() == keylights.end()) {
                  has_keylights = false;
                  continue;
                }
              }

              if (std::string(key["function"]) == "on/off")
                actions[kidx] = std::make_unique<keylight_toggle>(k, key, *dev, has_serial, serial, keylights);
              else if (std::string(key["function"]) == "brightness+")
                actions[kidx] = std::make_unique<keylight_brightness>(k, key, *dev, has_serial, serial, keylights, 5);
              else if (std::string(key["function"]) == "brightness-")
                actions[kidx] = std::make_unique<keylight_brightness>(k, key, *dev, has_serial, serial, keylights, -5);
              else if (std::string(key["function"]) == "color+")
                actions[kidx] = std::make_unique<keylight_color>(k, key, *dev, has_serial, serial, keylights, 250);
              else if (std::string(key["function"]) == "color-")
                actions[kidx] = std::make_unique<keylight_color>(k, key, *dev, has_serial, serial, keylights, -250);
            } else if (std::string(key["type"]) == "execute" && key.exists("command"))
              actions[kidx] = std::make_unique<execute>(k, key, *dev, std::string(key["command"]));
            else if (std::string(key["type"]) == "key" && key.exists("sequence")) {
              if (xdo == nullptr)
                xdo = xdo_new(nullptr);
              if (xdo != nullptr) {
                auto& seq = key.lookup("sequence");
                if (seq.isScalar())
                  actions[kidx] = std::make_unique<keypress>(k, key, *dev, std::string(seq), xdo);
                else if (seq.isList() && seq.getLength() > 0) {
                  std::list<std::string> l;
                  for (auto& sseq : seq) {
                    if (! sseq.isScalar()) {
                      l.clear();
                      break;
                    }
                    l.emplace_back(std::string(sseq));
                  }
                  if (l.size() > 0)
                    actions[kidx] = std::make_unique<keypress>(k, key, *dev, std::move(l), xdo);
                }
              }
            } else if (obs && std::string(key["type"]) == "obs") {
              if (auto b = obs->parse_key([this](unsigned page, unsigned row, unsigned column, Magick::Image&& image){ setkey(page, row, column, std::move(image)); }, [this](unsigned page, unsigned row, unsigned column, int handle){ setkey(page, row, column, handle); }, pagenr, row, column, key); b != nullptr)
                actions[kidx] = std::make_unique<obsaction>(k, key, *dev, b);
            } else if (std::string(key["type"]) == "nextpage")
              actions[kidx] = std::make_unique<pageaction>(k, key, *dev, (pagenr + 1) % nrpages, pageaction::direction::right, *this);
            else if (std::string(key["type"]) == "prevpage")
              actions[kidx] = std::make_unique<pageaction>(k, key, *dev, (pagenr - 1 + nrpages) % nrpages, pageaction::direction::left, *this);
          }
        }
      }
    }
    catch (libconfig::SettingNotFoundException&) {
      // No key settings.
    }

    dev->set_brightness(brightness);
    blankimg = dev->register_image(find_image("blank.png"));
  }


  int deck_config::register_image(Magick::Image&& image)
  {
    return dev->register_image(std::move(image));
  }


  void deck_config::setkey(unsigned page, unsigned row, unsigned column, Magick::Image&& image)
  {
    if (page == current_page)
      dev->set_key_image(row - 1u, column - 1u, std::move(image));
  }


  void deck_config::setkey(unsigned page, unsigned row, unsigned column, int handle)
  {
    if (page == current_page)
      dev->set_key_image(row - 1u, column - 1u, handle);
  }


  void deck_config::show_icons()
  {
    for (unsigned k = 0; k < dev->key_count; ++k) {
      unsigned kidx = keyidx(current_page, k);

      if (actions.contains(kidx))
        actions[kidx]->show_icon();
      else
        dev->set_key_image(k, blankimg);
    }
  }


  void deck_config::run()
  {
    show_icons();

    signal(SIGTERM, SIG_DFL);

    while (true) {
      auto ss = dev->read();
      if (idle_state == idle::full)
        continue;
      unsigned k = 0;
      for (auto s : ss) {
        if (s != 0)
          if (auto found = actions.find(keyidx(current_page, k)); found != actions.end())
            found->second->call();
        ++k;
      }
    }
  }


  void deck_config::nextpage(unsigned to_page) {
    current_page = to_page;
    show_icons();
  }


  namespace {

    bool is_wayland_session()
    {
      auto env = getenv("XDG_SESSION_TYPE");
      if (env != nullptr) {
        if (strcmp(env, "x11") == 0)
          return false;
        if (strcmp(env, "wayland") == 0)
          return true;
      }
      env = getenv("WAYLAND_DISPLAY");
      if (env != nullptr && strncmp(env, "wayland-", 8) == 0)
        return true;
      // XYZ use something else?
      return false;
    }

    guint64 get_idle_time_dbus(Glib::RefPtr<Gio::DBus::Proxy>& proxy)
    {

      auto ret = proxy->call_sync("GetIdletime");
      if (! ret) [[ unlikely ]]
        return 0;

      auto t = ret.get_type_string();
      guint64 user_idle_time;
      if (t == "(t)")
        user_idle_time = ret.cast_dynamic<Glib::Variant<std::tuple<guint64>>>(ret).get_child<guint64>(0);
      else if (t == "t")
        user_idle_time = ret.cast_dynamic<Glib::Variant<guint64>>(ret).get();
      else
        user_idle_time = 0;

      return user_idle_time;
    }


    guint64 get_idle_time_x11(Display* dpy, XScreenSaverInfo* ssi, int vendrel)
    {
      if (! XScreenSaverQueryInfo(dpy, DefaultRootWindow(dpy), ssi))
        return 0ul;

      auto idle = ssi->idle;
      if (vendrel >= 12000000) [[ likely ]]
        return idle;

      if (int dummy; DPMSQueryExtension(dpy, &dummy, &dummy) && DPMSCapable(dpy)) {
        CARD16 standby;
        CARD16 suspend;
        CARD16 off;
        DPMSGetTimeouts(dpy, &standby, &suspend, &off);

        CARD16 state;
        BOOL onoff;
        DPMSInfo(dpy, &state, &onoff);
        if (onoff)
          switch (state) {
          case DPMSModeStandby:
            // this check is a little bit paranoid, but be sure
            if (idle < unsigned(standby * 1000))
              idle += (standby * 1000);
            break;
          case DPMSModeSuspend:
            if (idle < unsigned((suspend + standby) * 1000))
              idle += ((suspend + standby) * 1000);
            break;
          case DPMSModeOff:
            if (idle < unsigned((off + suspend + standby) * 1000))
              idle += ((off + suspend + standby) * 1000);
            break;
          case DPMSModeOn:
          default:
            break;
          }
      }

      return idle;
    }

  } // anonymous namespace


  void deck_config::handle_idle()
  {
    // Prefer to use dbus which also works with Wayland.
    bool use_dbus = is_wayland_session();
    Glib::RefPtr<Gio::DBus::Proxy> proxy;

    if (use_dbus) {
      proxy = Gio::DBus::Proxy::create_for_bus_sync(Gio::DBus::BUS_TYPE_SESSION, "org.gnome.Mutter.IdleMonitor",
                                                    "/org/gnome/Mutter/IdleMonitor/Core", "org.gnome.Mutter.IdleMonitor");
      try {
        (void) get_idle_time_dbus(proxy);
      } catch (Glib::Error&) {
        use_dbus = false;
      }
    }

    Display* dpy = nullptr;
    XScreenSaverInfo* ssi = nullptr;
    int vendrel = -1;
    if (! use_dbus) {
      // Use X11 extensions.  The logic of the non-Wayland/non-DBus code is partially from:
      // https://github.com/g0hl1n/xprintidle/blob/master/xprintidle.c
      dpy = ::XOpenDisplay(nullptr);
      if (dpy == nullptr)
        return;

      int event_base;
      int error_base;
      if (! ::XScreenSaverQueryExtension(dpy, &event_base, &error_base)) {
        ::XCloseDisplay(dpy);
        return;
      }

      ssi = ::XScreenSaverAllocInfo();
      if (ssi == nullptr) {
        ::XCloseDisplay(dpy);
        return;
      }

      vendrel = VendorRelease(dpy);
    }

    std::vector<int> fds;
    bool use_input_devices = use_dbus;

    std::regex re(".*event([0-9]+).*");
    std::array<epoll_event,10> evs;


    // Used when /proc/bus/input/devices is not usable.
    XIEventMask m;
    m.deviceid = XIAllDevices;
    m.mask_len = XIMaskLen(XI_LASTEVENT);
    m.mask = static_cast<unsigned char*>(calloc(m.mask_len, sizeof(unsigned char)));
    if (m.mask == nullptr) {
      if (! use_dbus) {
        ::XFree(ssi);
        ::XCloseDisplay(dpy);
      }
      return;
    }

    XISetMask(m.mask, XI_ButtonPress);
    XISetMask(m.mask, XI_ButtonRelease);
    XISetMask(m.mask, XI_KeyPress);
    XISetMask(m.mask, XI_KeyRelease);
    XISetMask(m.mask, XI_Motion);
    XISetMask(m.mask, XI_TouchBegin);
    XISetMask(m.mask, XI_TouchUpdate);
    XISetMask(m.mask, XI_TouchEnd);


    auto dim_time = idle_temp_time == std::chrono::seconds{0} ? idle_full_time : idle_temp_time;

    bool first_sleep = true;
    auto now = std::chrono::system_clock::now();
    auto timeout_time = now + dim_time;
    auto to_sleep = timeout_time - now;
    while (true) {
      std::this_thread::sleep_for(to_sleep);
      auto idle_now = std::chrono::milliseconds(use_dbus ? get_idle_time_dbus(proxy) : get_idle_time_x11(dpy, ssi, vendrel));
      if (idle_now < dim_time) {
        to_sleep = dim_time - idle_now;
        continue;
      }

      fds.clear();

      int efd = ::epoll_create1(EPOLL_CLOEXEC);
      if (efd == -1) [[ unlikely ]]
        break;
      epoll_event ev;
      ev.events = EPOLLIN;

      if (use_input_devices) {
        std::ifstream ifs("/proc/bus/input/devices");
        if (! ifs.is_open()) [[ unlikely ]]
          use_input_devices = false;
        else {
          std::smatch match;
          for (std::string line; std::getline(ifs, line); )
            if (line.starts_with("H: Handlers=") && (line.contains("kbd") || line.contains("mouse"))) {
              if (std::regex_match(line, match, re) && match.size() == 2) {
                auto devname = "/dev/input/event"s + match[1].str();
                if (int fd = ::open(devname.c_str(), O_RDONLY | O_CLOEXEC | O_NOCTTY); fd >= 0) [[ likely ]] {
                  ev.data.fd = fd;
                  if (::epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev) != -1) [[ likely ]]
                    fds.push_back(fd);
                  else
                    ::close(fd);
                } else if (first_sleep && errno == EACCES)
                  std::cerr << "insufficient permissions to open " << devname << std::endl;
              }
            }

          if (fds.empty()) [[ unlikely ]]
            use_input_devices = false;
        }

        first_sleep = false;
      }

      Display* dpy2 = nullptr;
      Window win = 0;
      if (! use_input_devices) {
        // The system is idle.  Switch into event tracking mode.
        dpy2 = ::XOpenDisplay(nullptr);
        if (dpy2 == nullptr) {
          ::close(efd);
          break;
        }
        win = DefaultRootWindow(dpy2);

        ::XISelectEvents(dpy2, win, &m, 1);
        ::XSync(dpy2, False);

        int fd = ConnectionNumber(dpy2);
        ev.data.fd = fd;
        if (::epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev) == -1) [[ unlikely ]] {
          ::close(efd);
          break;
        }
      }

      idle_dim(idle_now >= idle_full_time ? idle::full : idle::temp);

      while (true) {
        int epoll_timeout = idle_now >= idle_full_time ? -1 : std::chrono::duration_cast<std::chrono::milliseconds>(idle_full_time - idle_now).count();
        auto nfds = ::epoll_wait(efd, evs.data(), evs.size(), epoll_timeout);
        if (nfds > 0) {
          // System event.  Resume normal operation, reset brightness.
          idle_dim(idle::running);
          break;
        }

        idle_now = std::chrono::milliseconds(use_dbus ? get_idle_time_dbus(proxy) : get_idle_time_x11(dpy, ssi, vendrel));
        if (idle_now >= idle_full_time)
          idle_dim(idle::full);
      }

      if (use_input_devices) {
        for (auto fd : fds)
          ::close(fd);
      } else {
        ::XDestroyWindow(dpy2, win);
        ::XCloseDisplay(dpy2);
      }
      ::close(efd);

      to_sleep = dim_time;
    }

    if (! use_dbus) {
      ::XFree(ssi);
      ::XCloseDisplay(dpy);
    }

    free(m.mask);
  }


  void deck_config::idle_dim(idle i)
  {
    if (i != idle_state)
      switch (idle_state = i) {
      case idle::running:
        dev->set_brightness(brightness);
        break;
      case idle::temp:
        dev->set_brightness(brightness_idle);
        break;
      case idle::full:
        dev->set_brightness(0);
        break;
      }
  }


  void pageaction::call() {
    deck.nextpage(to_page);
  }

} // anonymous namespace


int main(int argc, char* argv[])
{
  auto resource_bundle = Glib::wrap(resources_get_resource());
  resource_bundle->register_global();

  auto conffile = argc == 2 ? std::filesystem::path(argv[1]) : (get_homedir() / ".config/streamdeckd.conf");
  deck_config deck(conffile);

  deck.run();
}
