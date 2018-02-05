#include "texture.h"
#include "CGL/color.h"

namespace CGL {

Color Texture::sample(const SampleParams &sp) {
  // Part 5: Fill this in.
  Vector2D uv = sp.p_uv;
  float level = 0;
  switch (sp.lsm) {
    case LevelSampleMethod::L_LINEAR:
      level = get_level(sp);
      return sample_trilinear(uv, sp.psm, level, level);
      break;
    case LevelSampleMethod::L_NEAREST:
      level = round(get_level(sp));
    default:
      if (sp.psm == PixelSampleMethod::P_NEAREST) {
        return sample_nearest(uv, level);
      } else {
        return sample_bilinear(uv, level);
      }
  }
}

float Texture::get_level(const SampleParams &sp) {
  // Part 6: Fill this in.
  float dxx = width * sp.p_dx_uv.x;
  float dxy = height * sp.p_dx_uv.y;
  float dyy = height * sp.p_dy_uv.y;
  float dyx = width * sp.p_dy_uv.x;
  float level = max(dxx*dxx + dxy*dxy, dyx*dyx + dyy*dyy);
  return clamp(0.5*log2(level), 0.0, mipmap.size()-1.0);
}

Color Texture::sample_nearest(Vector2D uv, int level) {
  // Part 5: Fill this in.
  MipLevel* texmap = &mipmap[level];
  return Color(&(*texmap).texels[4*(static_cast<int>(max(floor(uv.y*(*texmap).height),0.0))*(*texmap).width 
                                  + static_cast<int>(max(floor(uv.x*(*texmap).width),0.0)))]);
}

Color Texture::sample_bilinear(Vector2D uv, int level) {
  // Part 5: Fill this in.
  MipLevel* texmap = &mipmap[level];
  float vr = uv.y*(*texmap).height - 0.5;
  float ur = uv.x*(*texmap).width - 0.5;
  float flvr = max(floor(vr), 0.0f);
  float flur = max(floor(ur), 0.0f);
  float cevr = min(ceil(vr), (*texmap).height - 1.0f);
  float ceur = min(ceil(ur), (*texmap).width - 1.0f);
  Color tl = &mipmap[level].texels[4*(static_cast<int>(flvr)*(*texmap).width + static_cast<int>(flur))];
  Color tr = &mipmap[level].texels[4*(static_cast<int>(flvr)*(*texmap).width + static_cast<int>(ceur))]; 
  Color bl = &mipmap[level].texels[4*(static_cast<int>(cevr)*(*texmap).width + static_cast<int>(flur))];
  Color br = &mipmap[level].texels[4*(static_cast<int>(cevr)*(*texmap).width + static_cast<int>(ceur))];
  vr = vr - floor(vr);
  ur = ur - floor(ur);
  
  return lerp(lerp(tl, tr, ur),lerp(bl, br, ur), vr);
}

Color Texture::sample_trilinear(Vector2D uv, PixelSampleMethod psm, float levelx, float levely) {
  // Part 6: Fill this in.
  int lower = floor(levelx);
  int upper = ceil(levelx);
  float ratio = levelx - lower;
  if (psm == PixelSampleMethod::P_NEAREST) {
    return lerp(sample_nearest(uv, lower), sample_nearest(uv, upper), ratio);
  } else {
    return lerp(sample_bilinear(uv, lower), sample_bilinear(uv, upper), ratio);
  }
}

Color Texture::lerp(Color a, Color b, float r) {
  return (1-r)*a + r*b;
}



/****************************************************************************/



inline void uint8_to_float(float dst[4], unsigned char *src) {
  uint8_t *src_uint8 = (uint8_t *)src;
  dst[0] = src_uint8[0] / 255.f;
  dst[1] = src_uint8[1] / 255.f;
  dst[2] = src_uint8[2] / 255.f;
  dst[3] = src_uint8[3] / 255.f;
}

inline void float_to_uint8(unsigned char *dst, float src[4]) {
  uint8_t *dst_uint8 = (uint8_t *)dst;
  dst_uint8[0] = (uint8_t)(255.f * max(0.0f, min(1.0f, src[0])));
  dst_uint8[1] = (uint8_t)(255.f * max(0.0f, min(1.0f, src[1])));
  dst_uint8[2] = (uint8_t)(255.f * max(0.0f, min(1.0f, src[2])));
  dst_uint8[3] = (uint8_t)(255.f * max(0.0f, min(1.0f, src[3])));
}

void Texture::generate_mips(int startLevel) {

  // make sure there's a valid texture
  if (startLevel >= mipmap.size()) {
    std::cerr << "Invalid start level";
  }

  // allocate sublevels
  int baseWidth = mipmap[startLevel].width;
  int baseHeight = mipmap[startLevel].height;
  int numSubLevels = (int)(log2f((float)max(baseWidth, baseHeight)));

  numSubLevels = min(numSubLevels, kMaxMipLevels - startLevel - 1);
  mipmap.resize(startLevel + numSubLevels + 1);

  int width = baseWidth;
  int height = baseHeight;
  for (int i = 1; i <= numSubLevels; i++) {

    MipLevel &level = mipmap[startLevel + i];

    // handle odd size texture by rounding down
    width = max(1, width / 2);
    //assert (width > 0);
    height = max(1, height / 2);
    //assert (height > 0);

    level.width = width;
    level.height = height;
    level.texels = vector<unsigned char>(4 * width * height);
  }

  // create mips
  int subLevels = numSubLevels - (startLevel + 1);
  for (int mipLevel = startLevel + 1; mipLevel < startLevel + subLevels + 1;
       mipLevel++) {

    MipLevel &prevLevel = mipmap[mipLevel - 1];
    MipLevel &currLevel = mipmap[mipLevel];

    int prevLevelPitch = prevLevel.width * 4; // 32 bit RGBA
    int currLevelPitch = currLevel.width * 4; // 32 bit RGBA

    unsigned char *prevLevelMem;
    unsigned char *currLevelMem;

    currLevelMem = (unsigned char *)&currLevel.texels[0];
    prevLevelMem = (unsigned char *)&prevLevel.texels[0];

    float wDecimal, wNorm, wWeight[3];
    int wSupport;
    float hDecimal, hNorm, hWeight[3];
    int hSupport;

    float result[4];
    float input[4];

    // conditional differentiates no rounding case from round down case
    if (prevLevel.width & 1) {
      wSupport = 3;
      wDecimal = 1.0f / (float)currLevel.width;
    } else {
      wSupport = 2;
      wDecimal = 0.0f;
    }

    // conditional differentiates no rounding case from round down case
    if (prevLevel.height & 1) {
      hSupport = 3;
      hDecimal = 1.0f / (float)currLevel.height;
    } else {
      hSupport = 2;
      hDecimal = 0.0f;
    }

    wNorm = 1.0f / (2.0f + wDecimal);
    hNorm = 1.0f / (2.0f + hDecimal);

    // case 1: reduction only in horizontal size (vertical size is 1)
    if (currLevel.height == prevLevel.height) {
      //assert (currLevel.height == 1);

      for (int i = 0; i < currLevel.width; i++) {
        wWeight[0] = wNorm * (1.0f - wDecimal * i);
        wWeight[1] = wNorm * 1.0f;
        wWeight[2] = wNorm * wDecimal * (i + 1);

        result[0] = result[1] = result[2] = result[3] = 0.0f;

        for (int ii = 0; ii < wSupport; ii++) {
          uint8_to_float(input, prevLevelMem + 4 * (2 * i + ii));
          result[0] += wWeight[ii] * input[0];
          result[1] += wWeight[ii] * input[1];
          result[2] += wWeight[ii] * input[2];
          result[3] += wWeight[ii] * input[3];
        }

        // convert back to format of the texture
        float_to_uint8(currLevelMem + (4 * i), result);
      }

      // case 2: reduction only in vertical size (horizontal size is 1)
    } else if (currLevel.width == prevLevel.width) {
      //assert (currLevel.width == 1);

      for (int j = 0; j < currLevel.height; j++) {
        hWeight[0] = hNorm * (1.0f - hDecimal * j);
        hWeight[1] = hNorm;
        hWeight[2] = hNorm * hDecimal * (j + 1);

        result[0] = result[1] = result[2] = result[3] = 0.0f;
        for (int jj = 0; jj < hSupport; jj++) {
          uint8_to_float(input, prevLevelMem + prevLevelPitch * (2 * j + jj));
          result[0] += hWeight[jj] * input[0];
          result[1] += hWeight[jj] * input[1];
          result[2] += hWeight[jj] * input[2];
          result[3] += hWeight[jj] * input[3];
        }

        // convert back to format of the texture
        float_to_uint8(currLevelMem + (currLevelPitch * j), result);
      }

      // case 3: reduction in both horizontal and vertical size
    } else {

      for (int j = 0; j < currLevel.height; j++) {
        hWeight[0] = hNorm * (1.0f - hDecimal * j);
        hWeight[1] = hNorm;
        hWeight[2] = hNorm * hDecimal * (j + 1);

        for (int i = 0; i < currLevel.width; i++) {
          wWeight[0] = wNorm * (1.0f - wDecimal * i);
          wWeight[1] = wNorm * 1.0f;
          wWeight[2] = wNorm * wDecimal * (i + 1);

          result[0] = result[1] = result[2] = result[3] = 0.0f;

          // convolve source image with a trapezoidal filter.
          // in the case of no rounding this is just a box filter of width 2.
          // in the general case, the support region is 3x3.
          for (int jj = 0; jj < hSupport; jj++)
            for (int ii = 0; ii < wSupport; ii++) {
              float weight = hWeight[jj] * wWeight[ii];
              uint8_to_float(input, prevLevelMem +
                                        prevLevelPitch * (2 * j + jj) +
                                        4 * (2 * i + ii));
              result[0] += weight * input[0];
              result[1] += weight * input[1];
              result[2] += weight * input[2];
              result[3] += weight * input[3];
            }

          // convert back to format of the texture
          float_to_uint8(currLevelMem + currLevelPitch * j + 4 * i, result);
        }
      }
    }
  }
}

}
