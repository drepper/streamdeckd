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

// XYZ Debug
// #include <iostream>


using namespace std::string_literals;


namespace {

  struct action {
    virtual void call() = 0;
  };


  struct keylight_toggle final : public action {
    keylight_toggle(bool has_serial, std::string& serial, keylightpp::device_list_type& keylights_) : serial(has_serial ? serial : ""), keylights(keylights_) { }

    void call() final override {
      for (auto& d : keylights)
        if (serial == "" || serial == d.serial)
          d.toggle();
    }
  private:
    const std::string serial;
    keylightpp::device_list_type& keylights;
  };


  struct keylight_color final : public action {
    keylight_color(bool has_serial, std::string& serial, keylightpp::device_list_type& keylights_, int inc_) : serial(has_serial ? serial : ""), keylights(keylights_), inc(inc_) { }

    void call() final override {
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
    keylight_brightness(bool has_serial, std::string& serial, keylightpp::device_list_type& keylights_, int inc_) : serial(has_serial ? serial : ""), keylights(keylights_), inc(inc_) { }

    void call() final override {
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
    execute(std::string&& command_) : command(std::move(command_)) { }

    void call() final override {
      auto _ = system(command.c_str());
      (void) _;
    }

  private:
    std::string command;
  };


  struct keypress final : public action {
    keypress(std::string&& sequence_, xdo_t* xdo_) : sequence(std::move(sequence_)), xdo(xdo_) { }

    void call() final override {
      xdo_send_keysequence_window(xdo, CURRENTWINDOW, sequence.c_str(), 100000);
    }
  private:
    std::string sequence;
    xdo_t* xdo;
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
  };


  deck_config::deck_config(const std::filesystem::path& conffile)
  {
    libconfig::Config config;
    config.readFile(conffile.c_str());

    std::string serial;
    if (! config.lookupValue("serial", serial))
      serial = "";

    for (auto& d : ctx) {
      if (! d->connected())
        continue;

      if (serial == "" || d->get_serial_number() == serial) {
        d->reset();

        try {
          auto& keys = config.lookup("keys");

          for (unsigned k = 0; k < d->key_count; ++k) {
            auto keyname = "r"s + std::to_string(1u + k / d->key_cols) + "c"s + std::to_string(1 + k % d->key_cols);
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
                    actions[k] = std::make_unique<keylight_toggle>(has_serial, serial, keylights);
                    valid = true;
                  } else if (std::string(key["function"]) == "brightness+") {
                    actions[k] = std::make_unique<keylight_brightness>(has_serial, serial, keylights, 5);
                    valid = true;
                  } else if (std::string(key["function"]) == "brightness-") {
                    actions[k] = std::make_unique<keylight_brightness>(has_serial, serial, keylights, -5);
                    valid = true;
                  } else if (std::string(key["function"]) == "color+") {
                    actions[k] = std::make_unique<keylight_color>(has_serial, serial, keylights, 250);
                    valid = true;
                  } else if (std::string(key["function"]) == "color-") {
                    actions[k] = std::make_unique<keylight_color>(has_serial, serial, keylights, -250);
                    valid = true;
                  }
                } else if (std::string(key["type"]) == "execute" && key.exists("command")) {
                  actions[k] = std::make_unique<execute>(std::string(key["command"]));
                  valid = true;
                } else if (std::string(key["type"]) == "key" && key.exists("sequence")) {
                  if (xdo == nullptr)
                    xdo = xdo_new(nullptr);
                  if (xdo != nullptr) {
                    actions[k] = std::make_unique<keypress>(std::string(key["sequence"]), xdo);
                    valid = true;
                  }
                }

                if (valid) {
                  std::string iconname;
                  if (key.lookupValue("icon", iconname)) {
                    auto path = std::filesystem::path(iconname);
                    if (path.is_relative())
                      path = std::filesystem::path(SHAREDIR) / path;
                    d->set_key_image(k, path.c_str());
                  }
                }
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
