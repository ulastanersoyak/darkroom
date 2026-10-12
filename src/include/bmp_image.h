#ifndef BMP_IMG_H
#define BMP_IMG_H
#include "custom_math.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// https://stackoverflow.com/questions/65840161/write-bmp-image
/*this program will only be able to read uncompressed 24bit bitmaps (for
now-maybe for ever) so each colour channel gets 8 bits each. bmp stores the
channels in blue, green, red order. each row in the file is padded to a
multiple of 4 bytes; that padding is stripped on read and re-added on write*/
struct pixel
{
  uint8_t blue;
  uint8_t green;
  uint8_t red;
};
#pragma pack(push, 1)
/*each bitmap file is 54 byte and the data is in that particular order with
each block taking a constant amount of memory. since the order and memory is
constant, it allows the program to be able to read the bmp file directly as a
byte stream. the #pragma pack(push,1) guarentees that the elements of this
particular struck is adjacent in the computers memory. #pragma pack(pop)
restores the memory alignment to the default after this struct is initialized*/
struct bmp_header
{
  uint16_t signature; // size:2byte.identifies file.usefull when checking if
                      // file is a bitmap or not.0x4D42 and "BM" is valid.
  uint32_t file_size; // size:4byte.total size of the bitmap.54 bit header +
                      // (img_height*img_width).
  uint32_t
      reserved; // size:4byte.reserved memory. typically set to 0 and unused.
  uint32_t data_offset; // size:4byte.indicates where header info ends and
                        // pixels start.
  uint32_t header_size; // size:4byte:specifies the size of bmp header in
                        // bytes.typically has value of 40(0x28) for standard
                        // windows bitmap.
  int32_t width;        // size:4byte.width of bitmap image
  int32_t height;       // size:4byte.height of bitmap image.
  uint16_t planes; // size:2byte.indicates the number of colour planes.always
                   // set to 1.
  uint16_t
      bit_depth; // size:2byte.specifies the number of bits used to represent
                 // pixels.typically holds values of 1,4,8,16,24,32.this
                 // program will use 24 most of the time.
  uint32_t compression; // size:4byte.specifies the compression method used in
                        // the bitmap image.0 means no compression, 3 means
                        // BI_BITFIELDS.
  uint32_t image_size;  // size:4byte.indicates size of the raw bitmap image in
                        // bytes.0 for uncompressed images.
  int32_t x_resolution; // size:4byte.specifies the horizontal resolution for
                        // image.
  int32_t
      y_resolution; // size:4byte.specifies the vertical resolution for image.
  uint32_t num_colours; // size:4byte.indicates number of colours in colour
                        // palette of image.usually in range of 0-24.
  uint32_t important_colours; // size:4byte.indicates number of important
                              // colours.usually 0,indicating all colours have
                              // equal importance
};
#pragma pack(pop)
struct bmp_image
{
  char *file_path;
  struct bmp_header header;
  struct pixel **pixels;
};
/*functions that can fail print the reason to stderr and report it through
their return value: NULL for the ones returning an image, -1 for the ones
returning int (0 means success). a failed operation leaves the image
unchanged*/
struct bmp_image *read_img (char *file_path);
int write_img (struct bmp_image *img, char *file_name);
struct bmp_image *copy_img (const struct bmp_image *image);
void free_img (struct bmp_image *image);
void convert_to_bw (struct bmp_image *image);
void convert_to_binary (struct bmp_image *image, const uint8_t threshold);
// static void _normalize_img(struct bmp_image* image);
/*applies a size x size kernel (row-major, size must be odd) to every pixel.
pixels outside the image take the value of the nearest edge pixel. results
are stored as their absolute value clamped to 0-255*/
int convolve (struct bmp_image *image, const double *kernel, int size);
int blur (struct bmp_image *image);
int g_blur (struct bmp_image *image, double sigma);
int v_edge (struct bmp_image *image);
int h_edge (struct bmp_image *image);
int edge (struct bmp_image *image);
int sharpen (struct bmp_image *image);
int box_blur (struct bmp_image *image, int size);
int emboss (struct bmp_image *image);
int laplacian (struct bmp_image *image);
void invert (struct bmp_image *image);
void adjust_brightness (struct bmp_image *image, int delta);
void flip_horizontal (struct bmp_image *image);
void flip_vertical (struct bmp_image *image);
int concat (struct bmp_image *image1, struct bmp_image *image2);
int resize (struct bmp_image *image, int new_width, int new_height);
int crop (struct bmp_image *image, int x, int y, int width, int height);
#endif
