#include <cstdlib>
#include <filesystem>

#include <pwd.h>
#include <unistd.h>

#include <libconfig.h++>
#include <giomm.h>
extern "C" {
  // libxdo is not prepared for C++ and the X headers define stray macros.
#include <xdo.h>
#undef BadRequest
}
#include <keylightpp.hh>
#include <streamdeckpp.hh>

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
      icon1 = find_image(iconname);
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
    Magick::Image icon1;
  };


  struct keylight_toggle final : public action {
    using base_type = action;

    keylight_toggle(unsigned k, const libconfig::Setting& setting, streamdeck::device_type& dev_, bool has_serial, std::string& serial_, keylightpp::device_list_type& keylights_)
    : base_type(k, setting, dev_), serial(has_serial ? serial_ : ""), keylights(keylights_)
    {
      nkeylights = 0;
      for (auto& d : keylights)
        if (serial == "" || serial == d.serial)
          ++nkeylights;

      std::string icon1name;
      if (! setting.lookupValue("icon_on", icon1name))
        icon1name = "bulb_on.png";
      icon1 = find_image(icon1name);

      if (nkeylights == 1) {
        std::string icon2name;
        if (! setting.lookupValue("icon_off", icon2name))
          icon2name = "bulb_off.png";
        icon2 = find_image(icon2name);
      } else
        icon2 = icon1;
    }

    void call() override
    {
      bool any = false;
      for (auto& d : keylights)
        if (serial == "" || serial == d.serial) {
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
    Magick::Image icon2;
  };


  struct keylight_color final : public action {
    using base_type = action;

    keylight_color(unsigned k, const libconfig::Setting& setting, streamdeck::device_type& dev_, bool has_serial, std::string& serial_, keylightpp::device_list_type& keylights_, int inc_)
    : base_type(k, setting, dev_, inc_ >= 0 ? "color+.png" : "color-.png"), serial(has_serial ? serial_ : ""), keylights(keylights_), inc(inc_)
    {
    }

    void call() override {
      for (auto& d : keylights)
        if (serial == "" || serial == d.serial) {
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
        if (serial == "" || serial == d.serial) {
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

    void setkey(unsigned page, unsigned row, unsigned column, const Magick::Image& image);

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
    Magick::Image blankimg;
  };


  deck_config::deck_config(const std::filesystem::path& conffile)
  : blankimg(find_image("blank.png"))
  {
    libconfig::Config config;
    config.readFile(conffile.c_str());

    std::string serial;
    if (! config.lookupValue("serial", serial))
      serial = "";

    if (! config.lookupValue("pages", nrpages))
      nrpages = 1;

    if (config.exists("obs")) {
      auto& group = config.lookup("obs");
      if (group.isGroup())
        obs = std::make_unique<obs::info>(group, ftobj);
    }

    unsigned brightness;
    if (! config.lookupValue("brightness", brightness))
      brightness = 100;

    for (auto& d : ctx) {
      if (! d->connected())
        continue;

      if (serial == "" || d->get_serial_number() == serial) {
        dev = d.get();
        d->reset();

        try {
          auto& keys = config.lookup("keys");

          nrpages = keys.getLength();
          for (const auto& page : keys) {
            unsigned pagenr = page.getIndex();

            for (unsigned k = 0; k < d->key_count; ++k) {
              auto row = 1u + k / d->key_cols;
              auto column = 1u + k % d->key_cols;
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
                    actions[kidx] = std::make_unique<keylight_toggle>(k, key, *d, has_serial, serial, keylights);
                  else if (std::string(key["function"]) == "brightness+")
                    actions[kidx] = std::make_unique<keylight_brightness>(k, key, *d, has_serial, serial, keylights, 5);
                  else if (std::string(key["function"]) == "brightness-")
                    actions[kidx] = std::make_unique<keylight_brightness>(k, key, *d, has_serial, serial, keylights, -5);
                  else if (std::string(key["function"]) == "color+")
                    actions[kidx] = std::make_unique<keylight_color>(k, key, *d, has_serial, serial, keylights, 250);
                  else if (std::string(key["function"]) == "color-")
                    actions[kidx] = std::make_unique<keylight_color>(k, key, *d, has_serial, serial, keylights, -250);
                } else if (std::string(key["type"]) == "execute" && key.exists("command"))
                  actions[kidx] = std::make_unique<execute>(k, key, *d, std::string(key["command"]));
                else if (std::string(key["type"]) == "key" && key.exists("sequence")) {
                  if (xdo == nullptr)
                    xdo = xdo_new(nullptr);
                  if (xdo != nullptr) {
                    auto& seq = key.lookup("sequence");
                    if (seq.isScalar())
                      actions[kidx] = std::make_unique<keypress>(k, key, *d, std::string(seq), xdo);
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
                        actions[kidx] = std::make_unique<keypress>(k, key, *d, std::move(l), xdo);
                    }
                  }
                } else if (obs && std::string(key["type"]) == "obs") {
                  if (auto b = obs->parse_key([this](unsigned page, unsigned row, unsigned column, const Magick::Image& image){ setkey(page, row, column, image); }, pagenr, row, column, key); b != nullptr)
                    actions[kidx] = std::make_unique<obsaction>(k, key, *d, b);
                } else if (std::string(key["type"]) == "nextpage")
                  actions[kidx] = std::make_unique<pageaction>(k, key, *d, (pagenr + 1) % nrpages, pageaction::direction::right, *this);
                else if (std::string(key["type"]) == "prevpage")
                  actions[kidx] = std::make_unique<pageaction>(k, key, *d, (pagenr - 1 + nrpages) % nrpages, pageaction::direction::left, *this);
              }
            }
          }
        }
        catch (libconfig::SettingNotFoundException&) {
          // No key settings.
        }

        d->set_brightness(brightness);
        break;
      }
    }

    if (dev == nullptr)
      throw std::runtime_error("no device available");
  }



  void deck_config::setkey(unsigned page, unsigned row, unsigned column, const Magick::Image& image)
  {
    if (page == current_page)
      dev->set_key_image((row - 1u) * dev->key_cols + (column - 1u), image);
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

    while (true) {
      auto ss = dev->read();
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
