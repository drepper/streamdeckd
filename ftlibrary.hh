#ifndef _FTLIBRARY_HH
#define _FTLIBRARY_HH 1

#include <filesystem>
#include <map>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>
#include <Magick++.h>
#include <utf8proc.h>

static_assert(__cpp_static_assert >= 200410, "extended static_assert missing");
static_assert(__cpp_lib_filesystem >= 201703);
static_assert(__cpp_range_based_for >= 200907);
static_assert(__cpp_structured_bindings >= 201606);
static_assert(__cpp_variadic_templates >= 200704);


// Forward declarations.
struct ftlibrary;


struct ftface {
  ftface(ftlibrary& library_, const std::string& facename);
  ~ftface();

  void set_size(double s, unsigned hdpi, unsigned vdpi = 0) { FT_Set_Char_Size(face, 0, FT_UInt(s * 64), hdpi, vdpi); }

private:
  FT_Face face;
  ftlibrary& library;

  std::filesystem::path find_face_path(const std::string& facename);

  template<typename T>
  friend struct font_render;
};


struct ftlibrary {

  ftlibrary();
  ~ftlibrary();

  ftface& find_font(const std::string& fontface);

private:
  FT_Library library;
  FcConfig* fcconfig;

  std::map<std::string,ftface> faces;

  friend struct ftface;
};


template<typename T>
struct font_render {
  using render_type = T;

  template<typename... Args>
  font_render(ftface& fontface_, Args... args);

  template<typename... Args>
  Magick::Image& draw(const char* s, Args... args);
private:
  void call_render(double fontsize, FT_UInt dpi, std::vector<utf8proc_int32_t>& wch);

  ftface& fontface;
  render_type renderer;
};


template<typename T>
template<typename... Args>
font_render<T>::font_render(ftface& fontface_, Args... args)
: fontface(fontface_), renderer(args...)
{
}


template<typename T>
void font_render<T>::call_render(double fontsize, FT_UInt dpi, std::vector<utf8proc_int32_t>& wbuf)
{
  fontface.set_size(fontsize, dpi);

  FT_Vector pen{ 0, 0 };

  bool use_kerning = FT_HAS_KERNING(fontface.face);
  FT_UInt prevglyphidx = 0;

  for (auto wch : wbuf) {
    auto glyphidx = FT_Get_Char_Index(fontface.face, wch);

    if (use_kerning && prevglyphidx != 0 && glyphidx != 0) {
      FT_Vector kern;
      FT_Get_Kerning(fontface.face, prevglyphidx, glyphidx, FT_KERNING_DEFAULT, &kern);
      pen.x += kern.x;
    }

    if (auto error = FT_Load_Glyph(fontface.face, glyphidx, FT_LOAD_RENDER); error)
      continue;

    renderer(fontface.face->glyph, (pen.x + 0x20) >> 6);

    pen.x += fontface.face->glyph->advance.x;
    prevglyphidx = glyphidx;
  }
}


template<typename T>
template<typename... Args>
Magick::Image& font_render<T>::draw(const char* s, Args... args)
{
  auto slen = strlen(s);
  std::vector<utf8proc_int32_t> wbuf;
  wbuf.resize(slen + 1);
  auto wlen = utf8proc_decompose(reinterpret_cast<const utf8proc_uint8_t*>(s), slen, wbuf.data(), wbuf.size(), UTF8PROC_NULLTERM);
  if (wlen < 0)
    throw std::runtime_error("invalid character");
  wbuf.resize(wlen);

  auto [fontsize, dpi] = renderer.first_font_size();
  while (true) {
    call_render(fontsize, dpi, wbuf);
    
    auto [finished, new_fontsize] = renderer.check_size();
    if (finished) {
      if (fontsize != new_fontsize)
        call_render(new_fontsize, dpi, wbuf);
      break;
    }
    fontsize = new_fontsize;
  }

  return renderer.finish(std::forward<Args>(args)...);
}

#endif // ftlibrary.hh
