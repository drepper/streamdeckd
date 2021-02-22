#ifndef _BUTTONTEXT_HH
#define _BUTTONTEXT_HH 1

#include <tuple>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <Magick++.h>


struct render_to_image {
  render_to_image(const Magick::Color& background_, unsigned targetwidth_, unsigned targetheight_)
  : background(Magick::Geometry(targetwidth_, targetheight_), background_), targetwidth(targetwidth_ ?: UINT_MAX), targetheight(targetheight_ ?: UINT_MAX)
  {
  }

  render_to_image(const Magick::Image& background_, double widthfactor = 1.0, double heightfactor = 1.0)
  : background(background_), targetwidth(background.columns() * std::clamp(widthfactor, 0.0, 1.0)), targetheight(background.rows() * std::clamp(heightfactor, 0.0, 1.0))
  {
  }

  void start() { lines.emplace_back(); }

  void operator()(FT_GlyphSlot slot, FT_Int x){ render(slot, x); }

  std::pair<double,FT_UInt> first_font_size();
  void compute_dimensions();
  std::pair<bool,double> check_size();

  bool goodenough(unsigned w, unsigned h) const;

  Magick::Image finish(Magick::Color foreground = Magick::Color("black"), double posx = 0.5, double posy = 0.5);

  void reset() {
    lines.clear();
  }

private:
  void render(FT_GlyphSlot slot, FT_Int x);

  // Magick::Color background;
  Magick::Image background;
  unsigned targetwidth;
  unsigned targetheight;

  double current_fontsize = 0;
  using experiment_type = std::tuple<double,unsigned,unsigned>;
  std::vector<experiment_type> experiments;
  struct slice {
    slice(int x_, int y_, FT_GlyphSlot slot_);

    int x;
    int y;
    unsigned width;
    unsigned height;
    std::vector<uint8_t> bitmap;
  };
  struct line_type {
    std::vector<slice> slices;
    int ymin = INT_MAX;
    int ymax = INT_MIN;
  };
  std::vector<line_type> lines;
  static constexpr double frac_linesep = 0.15;
  unsigned maxwidth = 0;
  unsigned totalheight = 0;
  unsigned linesep = 0;
};

#endif // buttontext.hh
