#include "include/bmp_image.h"
#include <math.h>

// bmp rows are padded to a multiple of 4 bytes
static uint32_t
_row_size (int32_t width)
{
  return ((uint32_t)width * sizeof (struct pixel) + 3) & ~3u;
}

// frees the first count rows of pixels and the row array itself
static void
_free_rows (struct pixel **pixels, int count)
{
  if (pixels == NULL)
    {
      return;
    }
  for (int i = 0; i < count; i++)
    {
      free (pixels[i]);
    }
  free (pixels);
}

// allocates height rows of row_bytes each. returns NULL if any allocation
// fails, nothing is leaked in that case
static struct pixel **
_alloc_rows (int height, size_t row_bytes)
{
  struct pixel **pixels
      = (struct pixel **)malloc (height * sizeof (struct pixel *));
  if (pixels == NULL)
    {
      return NULL;
    }
  for (int i = 0; i < height; i++)
    {
      pixels[i] = (struct pixel *)malloc (row_bytes);
      if (pixels[i] == NULL)
        {
          _free_rows (pixels, i);
          return NULL;
        }
    }
  return pixels;
}

static int
_read_header (FILE *bmp_file, char *file_path, struct bmp_header *header)
{
  if (fread (header, sizeof (struct bmp_header), 1, bmp_file) != 1)
    {
      fprintf (stderr,
               "error [bmp_image:_read_header]->couldn't read header from %s\n",
               file_path);
      return -1;
    }
  if (header->signature != 0x4D42)
    {
      fprintf (stderr,
               "error [bmp_image:_read_header]->invalid signature from "
               "%s.expected 0x4D42 got 0x%04X\n",
               file_path, header->signature);
      return -1;
    }
  if (header->bit_depth != 24 || header->compression != 0
      || header->width <= 0 || header->height == 0)
    {
      fprintf (stderr,
               "error [bmp_image:_read_header]->unsupported bitmap %s.only "
               "uncompressed 24bit bitmaps are supported\n",
               file_path);
      return -1;
    }
  return 0;
}

static struct pixel **
_read_pixels (FILE *bmp_file, char *file_path, struct bmp_header *header)
{
  // negative height means rows are stored top-down. store them bottom-up
  // like every other bitmap so the rest of the code only deals with one case
  int top_down = header->height < 0;
  if (top_down)
    {
      header->height = -header->height;
    }
  uint32_t row_size = _row_size (header->width);
  // allocate the padded row size so the whole row can be read at once
  struct pixel **pixels = _alloc_rows (header->height, row_size);
  if (pixels == NULL)
    {
      fprintf (stderr,
               "error [bmp_image:_read_pixels]->memory allocation failed for "
               "%s\n",
               file_path);
      return NULL;
    }
  fseek (bmp_file, header->data_offset, SEEK_SET);
  for (int i = 0; i < header->height; i++)
    {
      int row = top_down ? header->height - 1 - i : i;
      if (fread (pixels[row], 1, row_size, bmp_file) != row_size)
        {
          fprintf (stderr,
                   "error [bmp_image:_read_pixels]->unexpected end of file in "
                   "%s\n",
                   file_path);
          _free_rows (pixels, header->height);
          return NULL;
        }
    }
  return pixels;
}

struct bmp_image *
read_img (char *file_path)
{
  FILE *bmp_file = fopen (file_path, "rb");
  if (bmp_file == NULL)
    {
      fprintf (stderr, "error [bmp_image:read_img]->couldn't open file %s\n",
               file_path);
      return NULL;
    }
  struct bmp_header header;
  if (_read_header (bmp_file, file_path, &header) != 0)
    {
      fclose (bmp_file);
      return NULL;
    }
  struct pixel **pixels = _read_pixels (bmp_file, file_path, &header);
  fclose (bmp_file);
  if (pixels == NULL)
    {
      return NULL;
    }
  struct bmp_image *image
      = (struct bmp_image *)malloc (sizeof (struct bmp_image));
  if (image == NULL)
    {
      fprintf (stderr,
               "error [bmp_image:read_img]->memory allocation failed for %s\n",
               file_path);
      _free_rows (pixels, header.height);
      return NULL;
    }
  image->file_path = file_path;
  image->header = header;
  image->pixels = pixels;
  return image;
}

int
write_img (struct bmp_image *image, char *file_name)
{
  FILE *bmp_file = fopen (file_name, "wb");
  if (bmp_file == NULL)
    {
      fprintf (stderr, "error [bmp_image:write_img]->couldn't open file %s\n",
               file_name);
      return -1;
    }
  // only the 40 byte info header and the pixels are written, so the offsets
  // and sizes from the source file have to be recomputed
  uint32_t row_size = _row_size (image->header.width);
  uint32_t row_bytes = image->header.width * sizeof (struct pixel);
  struct bmp_header header = image->header;
  header.data_offset = sizeof (struct bmp_header);
  header.header_size = 40;
  header.image_size = row_size * header.height;
  header.file_size = header.data_offset + header.image_size;
  header.num_colours = 0;
  header.important_colours = 0;
  int ok = fwrite (&header, sizeof (struct bmp_header), 1, bmp_file) == 1;
  const uint8_t padding[3] = { 0, 0, 0 };
  for (int i = 0; ok && i < header.height; i++)
    {
      ok = fwrite (image->pixels[i], 1, row_bytes, bmp_file) == row_bytes
           && fwrite (padding, 1, row_size - row_bytes, bmp_file)
                  == row_size - row_bytes;
    }
  if (fclose (bmp_file) != 0)
    {
      ok = 0;
    }
  if (!ok)
    {
      fprintf (stderr, "error [bmp_image:write_img]->couldn't write file %s\n",
               file_name);
      return -1;
    }
  return 0;
}

struct bmp_image *
copy_img (const struct bmp_image *image)
{
  struct bmp_image *copy
      = (struct bmp_image *)malloc (sizeof (struct bmp_image));
  size_t row_bytes = image->header.width * sizeof (struct pixel);
  struct pixel **pixels = _alloc_rows (image->header.height, row_bytes);
  if (copy == NULL || pixels == NULL)
    {
      fprintf (stderr,
               "error [bmp_image:copy_img]->memory allocation failed for %s\n",
               image->file_path);
      free (copy);
      _free_rows (pixels, image->header.height);
      return NULL;
    }
  for (int i = 0; i < image->header.height; i++)
    {
      memcpy (pixels[i], image->pixels[i], row_bytes);
    }
  copy->file_path = image->file_path;
  copy->header = image->header;
  copy->pixels = pixels;
  return copy;
}

void
free_img (struct bmp_image *image)
{
  if (image == NULL)
    {
      return;
    }
  _free_rows (image->pixels, image->header.height);
  free (image);
}

void
convert_to_bw (struct bmp_image *image)
{
  for (int i = 0; i < image->header.height; i++)
    {
      for (int j = 0; j < image->header.width; j++)
        {
          int luminosity = 0.21 * image->pixels[i][j].red
                           + 0.72 * image->pixels[i][j].green
                           + 0.07 * image->pixels[i][j].blue;
          luminosity = (luminosity < 0)     ? 0
                       : (luminosity > 255) ? 255
                                            : luminosity;
          image->pixels[i][j].red = luminosity;
          image->pixels[i][j].green = luminosity;
          image->pixels[i][j].blue = luminosity;
        }
    }
}

void
convert_to_binary (struct bmp_image *image, const uint8_t threshold)
{
  for (int i = 0; i < image->header.height; i++)
    {
      for (int j = 0; j < image->header.width; j++)
        {
          uint8_t avg = (image->pixels[i][j].red + image->pixels[i][j].green
                         + image->pixels[i][j].blue)
                        / 3;
          if (avg < threshold)
            {
              image->pixels[i][j].red = 0;
              image->pixels[i][j].green = 0;
              image->pixels[i][j].blue = 0;
            }
          else
            {
              image->pixels[i][j].red = 255;
              image->pixels[i][j].green = 255;
              image->pixels[i][j].blue = 255;
            }
        }
    }
}

// returns a copy of the image's pixels surrounded by radius pixels on every
// side, or NULL if allocation fails. border pixels copy the nearest edge
// pixel so filters don't see a black frame around the image
static struct pixel **
_add_padding (const struct bmp_image *image, int radius)
{
  int width = image->header.width;
  int height = image->header.height;
  int new_width = width + 2 * radius;
  int new_height = height + 2 * radius;
  struct pixel **padded
      = _alloc_rows (new_height, new_width * sizeof (struct pixel));
  if (padded == NULL)
    {
      return NULL;
    }
  for (int i = 0; i < new_height; i++)
    {
      int src_i = min (max (i - radius, 0), height - 1);
      for (int j = 0; j < new_width; j++)
        {
          int src_j = min (max (j - radius, 0), width - 1);
          padded[i][j] = image->pixels[src_i][src_j];
        }
    }
  return padded;
}

int
convolve (struct bmp_image *image, const double *kernel, int size)
{
  if (size < 1 || size % 2 == 0)
    {
      fprintf (stderr,
               "error [bmp_image:convolve]->kernel size must be odd and "
               "positive, got %d\n",
               size);
      return -1;
    }
  int radius = size / 2;
  struct pixel **output_pixels = _alloc_rows (
      image->header.height, image->header.width * sizeof (struct pixel));
  struct pixel **padded = _add_padding (image, radius);
  if (output_pixels == NULL || padded == NULL)
    {
      fprintf (stderr,
               "error [bmp_image:convolve]->memory allocation failed for %s\n",
               image->file_path);
      _free_rows (output_pixels, image->header.height);
      _free_rows (padded, image->header.height + 2 * radius);
      return -1;
    }
  for (int i = 0; i < image->header.height; i++)
    {
      for (int j = 0; j < image->header.width; j++)
        {
          double r_sum = 0;
          double g_sum = 0;
          double b_sum = 0;
          // (i, j) in the image is (i + radius, j + radius) in padded, so
          // the window around it starts at (i, j)
          for (int ki = 0; ki < size; ki++)
            {
              for (int kj = 0; kj < size; kj++)
                {
                  double weight = kernel[ki * size + kj];
                  struct pixel p = padded[i + ki][j + kj];
                  r_sum += weight * p.red;
                  g_sum += weight * p.green;
                  b_sum += weight * p.blue;
                }
            }
          // edge kernels produce negative responses, take the magnitude and
          // clamp so the result fits in a byte instead of wrapping around
          output_pixels[i][j].red = min ((int)lround (fabs (r_sum)), 255);
          output_pixels[i][j].green = min ((int)lround (fabs (g_sum)), 255);
          output_pixels[i][j].blue = min ((int)lround (fabs (b_sum)), 255);
        }
    }
  _free_rows (padded, image->header.height + 2 * radius);
  _free_rows (image->pixels, image->header.height);
  image->pixels = output_pixels;
  return 0;
}
// static void _normalize_img(struct bmp_image* image){
//     int r_max_value=0;
//     int g_max_value=0;
//     int b_max_value=0;
//     int r_min_value=255;
//     int g_min_value=255;
//     int b_min_value=255;
//     for (int i=0;i<image->header.height;i++) {
//         for (int j=0;j<image->header.width;j++) {
//             r_max_value=max(r_max_value,image->pixels[i][j].red);
//             g_max_value=max(g_max_value,image->pixels[i][j].green);
//             b_max_value=max(b_max_value,image->pixels[i][j].blue);
//             r_min_value=min(r_min_value,image->pixels[i][j].red);
//             g_min_value=min(g_min_value,image->pixels[i][j].green);
//             b_min_value=min(b_min_value,image->pixels[i][j].blue);
//         }
//     }
//     for (int i=0;i<image->header.height;i++) {
//         for (int j=0;j<image->header.width;j++) {
//             image->pixels[i][j].red=(int)(((double)(image->pixels[i][j].red-r_min_value)/(r_max_value-r_min_value))*255);
//             image->pixels[i][j].green=(int)(((double)(image->pixels[i][j].green-g_min_value)/(g_max_value-g_min_value))*255);
//             image->pixels[i][j].blue=(int)(((double)(image->pixels[i][j].blue-b_min_value)/(b_max_value-b_min_value))*255);
//         }
//     }
// }

// default 3x3 blur filter
int
blur (struct bmp_image *image)
{
  const double kernel[3][3] = { { 1.0 / 9, 1.0 / 9, 1.0 / 9 },
                                { 1.0 / 9, 1.0 / 9, 1.0 / 9 },
                                { 1.0 / 9, 1.0 / 9, 1.0 / 9 } };
  return convolve (image, &kernel[0][0], 3);
}

// the kernel covers 3 sigma on each side, which holds over 99% of the
// gaussian's weight
int
g_blur (struct bmp_image *image, double sigma)
{
  if (sigma <= 0)
    {
      fprintf (stderr,
               "error [bmp_image:g_blur]->sigma must be positive, got %f\n",
               sigma);
      return -1;
    }
  int radius = max ((int)ceil (3 * sigma), 1);
  int size = 2 * radius + 1;
  double *kernel = (double *)malloc (size * size * sizeof (double));
  if (kernel == NULL)
    {
      fprintf (stderr,
               "error [bmp_image:g_blur]->memory allocation failed for %s\n",
               image->file_path);
      return -1;
    }
  double sum = 0;
  for (int i = 0; i < size; i++)
    {
      for (int j = 0; j < size; j++)
        {
          int x = i - radius;
          int y = j - radius;
          kernel[i * size + j] = exp (-(x * x + y * y) / (2 * sigma * sigma));
          sum += kernel[i * size + j];
        }
    }
  for (int i = 0; i < size * size; i++)
    {
      kernel[i] /= sum;
    }
  int result = convolve (image, kernel, size);
  free (kernel);
  return result;
}

int
v_edge (struct bmp_image *image)
{
  convert_to_bw (image);
  const double kernel[3][3] = { { -1, 0, 1 }, { -2, 0, 2 }, { -1, 0, 1 } };
  return convolve (image, &kernel[0][0], 3);
}

int
h_edge (struct bmp_image *image)
{
  convert_to_bw (image);
  const double kernel[3][3] = { { -1, -2, -1 }, { 0, 0, 0 }, { 1, 2, 1 } };
  return convolve (image, &kernel[0][0], 3);
}

// sobel edge magnitude: sqrt(gx^2 + gy^2) of the vertical and horizontal
// edge responses
int
edge (struct bmp_image *image)
{
  struct bmp_image *gy = copy_img (image);
  if (gy == NULL)
    {
      return -1;
    }
  if (v_edge (image) != 0 || h_edge (gy) != 0)
    {
      free_img (gy);
      return -1;
    }
  for (int i = 0; i < image->header.height; i++)
    {
      for (int j = 0; j < image->header.width; j++)
        {
          int gx_val = image->pixels[i][j].red;
          int gy_val = gy->pixels[i][j].red;
          int magnitude
              = min ((int)lround (sqrt (gx_val * gx_val + gy_val * gy_val)),
                     255);
          image->pixels[i][j].red = magnitude;
          image->pixels[i][j].green = magnitude;
          image->pixels[i][j].blue = magnitude;
        }
    }
  free_img (gy);
  return 0;
}

int
sharpen (struct bmp_image *image)
{
  const double kernel[3][3] = { { 0, -1, 0 }, { -1, 5, -1 }, { 0, -1, 0 } };
  return convolve (image, &kernel[0][0], 3);
}

// averages every pixel with its size x size neighbourhood. size must be odd
int
box_blur (struct bmp_image *image, int size)
{
  if (size < 1 || size % 2 == 0)
    {
      fprintf (stderr,
               "error [bmp_image:box_blur]->size must be odd and positive, "
               "got %d\n",
               size);
      return -1;
    }
  double *kernel = (double *)malloc (size * size * sizeof (double));
  if (kernel == NULL)
    {
      fprintf (stderr,
               "error [bmp_image:box_blur]->memory allocation failed for %s\n",
               image->file_path);
      return -1;
    }
  for (int i = 0; i < size * size; i++)
    {
      kernel[i] = 1.0 / (size * size);
    }
  int result = convolve (image, kernel, size);
  free (kernel);
  return result;
}

// lights the image from the bottom right so edges look raised. the kernel
// sums to 1, so flat areas keep their colour
int
emboss (struct bmp_image *image)
{
  const double kernel[3][3] = { { -2, -1, 0 }, { -1, 1, 1 }, { 0, 1, 2 } };
  return convolve (image, &kernel[0][0], 3);
}

// second derivative edge detector. unlike sobel it responds to edges in
// every direction with a single kernel
int
laplacian (struct bmp_image *image)
{
  convert_to_bw (image);
  const double kernel[3][3] = { { 0, 1, 0 }, { 1, -4, 1 }, { 0, 1, 0 } };
  return convolve (image, &kernel[0][0], 3);
}

void
invert (struct bmp_image *image)
{
  for (int i = 0; i < image->header.height; i++)
    {
      for (int j = 0; j < image->header.width; j++)
        {
          image->pixels[i][j].red = 255 - image->pixels[i][j].red;
          image->pixels[i][j].green = 255 - image->pixels[i][j].green;
          image->pixels[i][j].blue = 255 - image->pixels[i][j].blue;
        }
    }
}

static uint8_t
_clamp_channel (int value)
{
  return (uint8_t)max (0, min (value, 255));
}

void
adjust_brightness (struct bmp_image *image, int delta)
{
  for (int i = 0; i < image->header.height; i++)
    {
      for (int j = 0; j < image->header.width; j++)
        {
          image->pixels[i][j].red
              = _clamp_channel (image->pixels[i][j].red + delta);
          image->pixels[i][j].green
              = _clamp_channel (image->pixels[i][j].green + delta);
          image->pixels[i][j].blue
              = _clamp_channel (image->pixels[i][j].blue + delta);
        }
    }
}

// mirror the image left to right
void
flip_horizontal (struct bmp_image *image)
{
  for (int i = 0; i < image->header.height; i++)
    {
      for (int j = 0; j < image->header.width / 2; j++)
        {
          struct pixel tmp = image->pixels[i][j];
          image->pixels[i][j] = image->pixels[i][image->header.width - 1 - j];
          image->pixels[i][image->header.width - 1 - j] = tmp;
        }
    }
}

// mirror the image top to bottom. only the row pointers need swapping
void
flip_vertical (struct bmp_image *image)
{
  for (int i = 0; i < image->header.height / 2; i++)
    {
      struct pixel *tmp = image->pixels[i];
      image->pixels[i] = image->pixels[image->header.height - 1 - i];
      image->pixels[image->header.height - 1 - i] = tmp;
    }
}

// places image2 to the right of image1. image1 is modified in place, image2
// is left untouched. both images must have the same height. on failure
// image1 is left unchanged
int
concat (struct bmp_image *image1, struct bmp_image *image2)
{
  if (image1->header.height != image2->header.height)
    {
      fprintf (stderr,
               "error [bmp_image:concat]->height mismatch between %s (%d) "
               "and %s (%d)\n",
               image1->file_path, image1->header.height, image2->file_path,
               image2->header.height);
      return -1;
    }
  int new_width = image1->header.width + image2->header.width;
  size_t row1_bytes = image1->header.width * sizeof (struct pixel);
  size_t row2_bytes = image2->header.width * sizeof (struct pixel);
  struct pixel **rows
      = _alloc_rows (image1->header.height, new_width * sizeof (struct pixel));
  if (rows == NULL)
    {
      fprintf (stderr,
               "error [bmp_image:concat]->memory allocation failed for %s\n",
               image1->file_path);
      return -1;
    }
  for (int i = 0; i < image1->header.height; i++)
    {
      memcpy (rows[i], image1->pixels[i], row1_bytes);
      memcpy (rows[i] + image1->header.width, image2->pixels[i], row2_bytes);
    }
  _free_rows (image1->pixels, image1->header.height);
  image1->pixels = rows;
  image1->header.width = new_width;
  return 0;
}

// scales the image to new_width x new_height with bilinear interpolation:
// every output pixel blends the 4 source pixels closest to where it lands
int
resize (struct bmp_image *image, int new_width, int new_height)
{
  if (new_width < 1 || new_height < 1)
    {
      fprintf (stderr,
               "error [bmp_image:resize]->invalid size %dx%d for %s\n",
               new_width, new_height, image->file_path);
      return -1;
    }
  struct pixel **output_pixels
      = _alloc_rows (new_height, new_width * sizeof (struct pixel));
  if (output_pixels == NULL)
    {
      fprintf (stderr,
               "error [bmp_image:resize]->memory allocation failed for %s\n",
               image->file_path);
      return -1;
    }
  int width = image->header.width;
  int height = image->header.height;
  double scale_x = (double)width / new_width;
  double scale_y = (double)height / new_height;
  for (int i = 0; i < new_height; i++)
    {
      // map pixel centres, so the corners of both images line up
      double src_y = fmin (fmax ((i + 0.5) * scale_y - 0.5, 0), height - 1);
      int y0 = (int)src_y;
      int y1 = min (y0 + 1, height - 1);
      double fy = src_y - y0;
      for (int j = 0; j < new_width; j++)
        {
          double src_x
              = fmin (fmax ((j + 0.5) * scale_x - 0.5, 0), width - 1);
          int x0 = (int)src_x;
          int x1 = min (x0 + 1, width - 1);
          double fx = src_x - x0;
          struct pixel p00 = image->pixels[y0][x0];
          struct pixel p01 = image->pixels[y0][x1];
          struct pixel p10 = image->pixels[y1][x0];
          struct pixel p11 = image->pixels[y1][x1];
          double w00 = (1 - fx) * (1 - fy);
          double w01 = fx * (1 - fy);
          double w10 = (1 - fx) * fy;
          double w11 = fx * fy;
          output_pixels[i][j].red
              = lround (w00 * p00.red + w01 * p01.red + w10 * p10.red
                        + w11 * p11.red);
          output_pixels[i][j].green
              = lround (w00 * p00.green + w01 * p01.green + w10 * p10.green
                        + w11 * p11.green);
          output_pixels[i][j].blue
              = lround (w00 * p00.blue + w01 * p01.blue + w10 * p10.blue
                        + w11 * p11.blue);
        }
    }
  _free_rows (image->pixels, height);
  image->pixels = output_pixels;
  image->header.width = new_width;
  image->header.height = new_height;
  return 0;
}
