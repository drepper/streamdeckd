#include <cassert>
#include <numeric>

#include "buttontext.hh"

// XYZ Debug
// #include <iostream>


// The ImageMagic headers are not really safe for C++, they assume that the enture
// namespaces are flattened.  This is in this code noticeable with the use of the
// Quantum type.  Hence include they type in the global namespace.
using Magick::Quantum;


render_to_image::slice::slice(int x_, int y_, FT_GlyphSlot slot)
: x(x_), y(y_), width(slot->bitmap.width), height(slot->bitmap.rows)
{
  bitmap.resize(width * height);
  std::copy_n(slot->bitmap.buffer, width * height, bitmap.begin());
}


void render_to_image::render(FT_GlyphSlot slot, FT_Int x)
{
  auto bitmap = &slot->bitmap;
  auto& line = lines.back();
  auto& slices = line.slices;

  assert(bitmap->pixel_mode == FT_PIXEL_MODE_GRAY);
  assert(bitmap->num_grays == 256);
  try {
    auto& ref = slices.emplace_back(x + slot->bitmap_left, -slot->bitmap_top, slot);
    line.ymin = std::min(line.ymin, int(slot->bitmap_top - ref.height));
    line.ymax = std::max(line.ymax, slot->bitmap_top);
  }
  catch (std::runtime_error&) {
    // Ignore.
  }
}


std::pair<double,FT_UInt> render_to_image::first_font_size()
{
  experiments.clear();
  return { current_fontsize = 24.0, 122 };
}


void render_to_image::compute_dimensions()
{
  std::vector<unsigned> widths;
  std::vector<unsigned> heights;
  for (auto& line : lines) {
    auto& slices = line.slices;
    widths.emplace_back(slices.back().x + slices.back().width - slices.front().x);
    heights.emplace_back(line.ymax - line.ymin + 1);
  }
  assert(widths.size() == heights.size());

  maxwidth = std::accumulate(widths.begin(), widths.end(), 0u, [](const auto l, const auto r){ return std::max(l, r); });
  totalheight = std::accumulate(heights.begin(), heights.end(), 0u);
  // Account for line separation.
  linesep = std::max(1u, unsigned(frac_linesep * totalheight / heights.size() + 0.5));
  totalheight += (heights.size() - 1) * linesep;

  // std::cout << "#lines = " << lines.size() << "  maxwidth = " << maxwidth << " (target: " << targetwidth << ")   totalheight = " << totalheight << " (target: " << targetheight << ")  linesep = " << linesep << "\n";
}


std::pair<bool,double> render_to_image::check_size()
{
  if (goodenough(maxwidth, totalheight))
    return { true, current_fontsize };

  if (experiments.empty()) {
    experiments.emplace_back(current_fontsize, maxwidth, totalheight);
    double f = std::min(targetwidth / double(maxwidth), targetheight / double(totalheight));
    current_fontsize *= f;
  } else {
    if (maxwidth < std::get<1>(experiments[0]) || (maxwidth == std::get<1>(experiments[0]) && totalheight <= std::get<2>(experiments[0])))
      experiments.emplace(experiments.begin(), std::make_tuple(current_fontsize, maxwidth, totalheight));
    else if (experiments.size() == 1)
      experiments.emplace_back(std::make_tuple(current_fontsize, maxwidth, totalheight));
    else {
      if (maxwidth < std::get<1>(experiments.back()) || (maxwidth == std::get<1>(experiments.back()) && totalheight < std::get<2>(experiments.back())))
        experiments[1] = experiment_type{ current_fontsize, maxwidth, totalheight };
      else
        return { true, std::get<0>(experiments.back()) };
    }

    auto ds = std::get<0>(experiments.back()) - std::get<0>(experiments[0]);
    auto dw = std::get<1>(experiments.back()) - std::get<1>(experiments[0]);
    auto dh = std::get<2>(experiments.back()) - std::get<2>(experiments[0]);

    auto ew = targetwidth - std::get<1>(experiments[0]);
    auto eh = targetheight - std::get<2>(experiments[0]);

    current_fontsize = std::min(ds / dw * (ew + dw / ds * std::get<0>(experiments[0])),
                                ds / dh * (eh + dh / ds * std::get<0>(experiments[0])));
  }

  // std::cout << "new font size " << current_fontsize << std::endl;

  return { false, current_fontsize };
}


bool render_to_image::goodenough(unsigned w, unsigned h) const
{
  return w <= targetwidth && h <= targetheight && (w >= unsigned(0.99 * targetwidth) || h >= unsigned(0.99 * targetheight));
}


Magick::Image render_to_image::finish(Magick::Color foreground, double posx, double posy)
{
  Magick::Image image(background);
  image.modifyImage();

#if MagickLibVersion >= 0x700
  assert(image.alpha());
  assert(image.channels() == 4);
#endif

  auto imwidth = image.columns();
  auto imheight = image.rows();
#if MagickLibVersion >= 0x700
  auto mem = image.getPixels(0, 0, imwidth, imheight);
  auto foreground_red = foreground.quantumRed();
  auto foreground_green = foreground.quantumGreen();
  auto foreground_blue = foreground.quantumBlue();
#else
  Magick::Pixels view(image);
  auto* mem = view.get(0, 0, imwidth, imheight);
  auto foregroud_red = foreground.redQuantum();
  auto foregroud_green = foreground.greenQuantum();
  auto foregroud_blue = foreground.blueQuantum();
#endif

  int offy = std::max(0, int((imheight - totalheight) * posy));

  for (auto& line : lines) {
    auto& slices = line.slices;
    auto& ymin = line.ymin;
    auto& ymax = line.ymax;

    unsigned width = slices.back().x + slices.back().width - slices.front().x;
    unsigned height = ymax - ymin + 1;

    int offx = std::max(0, int((imwidth - width) * posx));

    auto s0x = slices.front().x;
    for (const auto& s : slices) {
      for (unsigned y = 0; y < s.height; ++y) {
        assert(s.x - s0x + s.width <= width);

        if (offy + s.y + ymax + y > imheight)
          break;

        for (unsigned x = 0; x < s.width; ++x) {
          auto offset = y * s.width + x;

          auto memx = offx + s.x - s0x + x;
          auto memy = offy + s.y + ymax + y;
          if (memx >= imwidth)
            continue;


          auto foreground_alpha = QuantumRange * ~s.bitmap[offset] / 255;

#if MagickLibVersion >= 0x700
          auto memoffset = (memy * imwidth + memx) * 4;
          Quantum red = mem[memoffset];
          Quantum green = mem[memoffset + 1];
          Quantum blue = mem[memoffset + 2];
          Quantum opacity = mem[memoffset + 3];
#else
          auto memoffset = memy * imwidth + memx;
          Quantum red = mem[memoffset].red;
          Quantum green = mem[memoffset].green;
          Quantum blue = mem[memoffset].blue;
          Quantum opacity = mem[memoffset].opacity;
#endif
          if (opacity != QuantumRange && foreground_alpha != QuantumRange) {
            // This code is a mess.  It implements alpha-blending but with three different units
            // this gets complicated.  Opagueness in the alpha-blending formula assumes a range
            // of 0.0 (transparent) to 1.0 (completely opaque).  The ImageMagick library uses a
            // range from QuantumRange (transparent) to 0 (completely opaque).  Note the reverse
            // relationship of numeric value and meaning and the fact that QuantumRange depends in
            // value and type on the libraries configuration.  Finally, the freetype library uses
            // an opagueness value from 0 to an upper value based on the image format.  We use
            // 256 gray levels and therefore the range is 0 to 255.
            auto alphamem = 1.0 - opacity / double(QuantumRange);
            opacity = foreground_alpha * opacity / double(QuantumRange);

            auto alphares = 1.0 - opacity / double(QuantumRange);
            auto alphatext = s.bitmap[offset] / 255.0;

            red = 1.0 / alphares * (alphatext * foreground_red + (1.0 - alphatext) * alphamem * red);
            green = 1.0 / alphares * (alphatext * foreground_green + (1.0 - alphatext) * alphamem * green);
            blue = 1.0 / alphares * (alphatext * foreground_blue + (1.0 - alphatext) * alphamem * blue);
          } else if (opacity == 0) {
            red = foreground_red;
            green = foreground_green;
            blue = foreground_blue;
            opacity = foreground_alpha;
          }

#if MagickLibVersion >= 0x700
          mem[memoffset] = red;
          mem[memoffset + 1] = green;
          mem[memoffset + 2] = blue;
          mem[memoffset + 3] = opacity;
#else
          mem[memoffset].red = red;
          mem[memoffset].green = green;
          mem[memoffset].blue = blue;
          mem[memoffset].opacity = opacity;
#endif
        }
      }
    }

    offy += height + linesep;
  }

  // view.sync();
  image.syncPixels();

  return image;
}
