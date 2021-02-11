#ifndef _BUTTONTEXT_HH
#define _BUTTONTEXT_HH 1

#include <tuple>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include "Magick++.h"


struct render_to_image {
  render_to_image(const Magick::Color& background_, unsigned targetwidth_, unsigned targetheight_)
  : background(Magick::Geometry(targetwidth_, targetheight_), background_), targetwidth(targetwidth_ ?: UINT_MAX), targetheight(targetheight_ ?: UINT_MAX)
  {
  }

  render_to_image(const Magick::Image& background_, double widthfactor = 1.0, double heightfactor = 1.0)
  : background(background_), targetwidth(background.columns() * std::clamp(widthfactor, 0.0, 1.0)), targetheight(background.rows() * std::clamp(heightfactor, 0.0, 1.0))
  {
  }

  void operator()(FT_GlyphSlot slot, FT_Int x){ render(slot, x); }

  std::pair<double,FT_UInt> first_font_size();
  std::pair<bool,double> check_size();

  bool goodenough(unsigned w, unsigned h) const;

  Magick::Image& finish(Magick::Color foreground = Magick::Color("black"), double posx = 0.5, double posy = 0.5);

private:
  void render(FT_GlyphSlot slot, FT_Int x);

  void reset() {
    slices.clear();
    ymin = INT_MAX;
    ymax = INT_MIN;
  }

  // Magick::Color background;
  Magick::Image background;
  unsigned targetwidth;
  unsigned targetheight;
  Magick::Image image;

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
  std::vector<slice> slices;
  int ymin = INT_MAX;
  int ymax = INT_MIN;
};

#endif // buttontext.hh
