#include <cstdlib>
#include <filesystem>

#include <pwd.h>
#include <unistd.h>

#include <libconfig.h++>
extern "C" {
  // libxdo is not prepared for C++ and the X headers define stray macros.
#include <xdo.h>
#undef BadRequest
}
#include <keylightpp.hh>
#include <streamdeckpp.hh>

#include "obs.hh"

// XYZ Debug
// #include <iostream>


using namespace std::string_literals;


namespace {

  struct action {
    action(unsigned k) : key(k) { }
    virtual ~action() { }

    unsigned key;

    virtual void call() = 0;

    virtual void show_icon(const libconfig::Setting& setting, streamdeck::device_type* d) {
      std::string iconname;
      if (setting.lookupValue("icon", iconname)) {
        auto path = std::filesystem::path(iconname);
        if (path.is_relative())
          path = std::filesystem::path(SHAREDIR) / path;
        d->set_key_image(key, path.c_str());
      }      
    }
  };


  struct keylight_toggle final : public action {
    using base_type = action;

    keylight_toggle(unsigned k, bool has_serial, std::string& serial, keylightpp::device_list_type& keylights_) : base_type(k), serial(has_serial ? serial : ""), keylights(keylights_) { }

    void call() override {
      for (auto& d : keylights)
        if (serial == "" || serial == d.serial)
          d.toggle();
    }
  private:
    const std::string serial;
    keylightpp::device_list_type& keylights;
  };


  struct keylight_color final : public action {
    using base_type = action;

    keylight_color(unsigned k, bool has_serial, std::string& serial, keylightpp::device_list_type& keylights_, int inc_) : base_type(k), serial(has_serial ? serial : ""), keylights(keylights_), inc(inc_) { }

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

    keylight_brightness(unsigned k, bool has_serial, std::string& serial, keylightpp::device_list_type& keylights_, int inc_) : base_type(k), serial(has_serial ? serial : ""), keylights(keylights_), inc(inc_) { }

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

    execute(unsigned k, std::string&& command_) : base_type(k), command(std::move(command_)) { }

    void call() override {
      auto _ = system(command.c_str());
      (void) _;
    }

  private:
    std::string command;
  };


  struct keypress final : public action {
    using base_type = action;

    keypress(unsigned k, std::string&& sequence, xdo_t* xdo_) : base_type(k), sequence_list(1, std::move(sequence)), xdo(xdo_) { }
    keypress(unsigned k, std::list<std::string>&& sequence_list_, xdo_t* xdo_) : base_type(k), sequence_list(std::move(sequence_list_)), xdo(xdo_) { }

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

    obsaction(unsigned k, obs::button* b_) : base_type(k), b(b_) { }

    void call() override {
      b->call();
    }

    void show_icon(const libconfig::Setting&, streamdeck::device_type*) override {
      b->show_icon(key);
    }

  private:
    obs::button* b;
  };


  struct deck_config {
    deck_config(const std::filesystem::path& conffile);

    void run();
  private:
    streamdeck::context ctx;
    streamdeck::device_type* dev = nullptr;

    bool has_keylights = false;
    keylightpp::device_list_type keylights;
    xdo_t* xdo = nullptr;
    std::map<unsigned,std::unique_ptr<action>> actions;
    std::unique_ptr<obs::info> obs;
  };


  deck_config::deck_config(const std::filesystem::path& conffile)
  {
    libconfig::Config config;
    config.readFile(conffile.c_str());

    std::string serial;
    if (! config.lookupValue("serial", serial))
      serial = "";

    if (config.exists("obs")) {
      auto& group = config.lookup("obs");
      if (group.isGroup())
        obs = std::make_unique<obs::info>(group);
    }

    for (auto& d : ctx) {
      if (! d->connected())
        continue;

      if (serial == "" || d->get_serial_number() == serial) {
        d->reset();

        try {
          auto& keys = config.lookup("keys");

          for (unsigned k = 0; k < d->key_count; ++k) {
            auto row = 1u + k / d->key_cols;
            auto column = 1u + k % d->key_cols;
            auto keyname = "r"s + std::to_string(row) + "c"s + std::to_string(column);
            if (keys.exists(keyname)) {
              auto& key = keys[keyname];
              if (key.exists("type")) {
                bool valid = false;

                if (std::string(key["type"]) == "keylight" && key.exists("function")) {
                  std::string serial;
                  bool has_serial = key.lookupValue("serial", serial);

                  if (! has_keylights) {
                    has_keylights = true;
                    keylights = keylightpp::discover();
                    if (keylights.begin() == keylights.end())
                      continue;
                  }

                  if (std::string(key["function"]) == "on/off") {
                    actions[k] = std::make_unique<keylight_toggle>(k, has_serial, serial, keylights);
                    valid = true;
                  } else if (std::string(key["function"]) == "brightness+") {
                    actions[k] = std::make_unique<keylight_brightness>(k, has_serial, serial, keylights, 5);
                    valid = true;
                  } else if (std::string(key["function"]) == "brightness-") {
                    actions[k] = std::make_unique<keylight_brightness>(k, has_serial, serial, keylights, -5);
                    valid = true;
                  } else if (std::string(key["function"]) == "color+") {
                    actions[k] = std::make_unique<keylight_color>(k, has_serial, serial, keylights, 250);
                    valid = true;
                  } else if (std::string(key["function"]) == "color-") {
                    actions[k] = std::make_unique<keylight_color>(k, has_serial, serial, keylights, -250);
                    valid = true;
                  }
                } else if (std::string(key["type"]) == "execute" && key.exists("command")) {
                  actions[k] = std::make_unique<execute>(k, std::string(key["command"]));
                  valid = true;
                } else if (std::string(key["type"]) == "key" && key.exists("sequence")) {
                  if (xdo == nullptr)
                    xdo = xdo_new(nullptr);
                  if (xdo != nullptr) {
                    auto& seq = key.lookup("sequence");
                    if (seq.isScalar()) {
                      actions[k] = std::make_unique<keypress>(k, std::string(seq), xdo);
                      valid = true;
                    } else if (seq.isList() && seq.getLength() > 0) {
                      std::list<std::string> l;
                      for (auto& sseq : seq) {
                        if (! sseq.isScalar()) {
                          l.clear();
                          break;
                        }
                        l.emplace_back(std::string(sseq));
                      }
                      if (l.size() > 0) {
                        actions[k] = std::make_unique<keypress>(k, std::move(l), xdo);
                        valid = true;
                      }
                    }
                  }
                } else if (obs && std::string(key["type"]) == "obs")
                  if (auto b = obs->parse_key(d.get(), row, column, key); b != nullptr) {
                    actions[k] = std::make_unique<obsaction>(k, b);
                    valid = true;
                  }

                if (valid)
                  actions[k]->show_icon(key, d.get());
              }
            }
          }
          dev = d.get();
        }
        catch (libconfig::SettingNotFoundException&) {
          // No key settings.
        }
        break;
      }
    }

    if (dev == nullptr)
      throw std::runtime_error("no device available");
  }


  void deck_config::run()
  {
    while (true) {
      auto ss = dev->read();
      unsigned k = 0;
      for (auto s : ss) {
        if (s != 0)
          if (auto found = actions.find(k); found != actions.end())
            found->second->call();
        ++k;
      }
    }
  }

} // anonymous namespace


int main(int argc, char* argv[])
{
  // XYZ configurable
  std::filesystem::path conffile;

  const char* homedir = getenv("HOME");
  if (homedir == nullptr || *homedir == '\0') {
    auto pwd = getpwuid(getuid());
    if (pwd != nullptr)
      homedir = pwd->pw_dir;
    else
      conffile = std::filesystem::current_path();
  } else
    conffile = std::filesystem::path(homedir);

  conffile /= ".config/streamdeckd.conf";

  deck_config deck(conffile);

  deck.run();
}
