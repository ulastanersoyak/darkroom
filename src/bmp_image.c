#include "include/bmp_image.h"
#include <math.h>

// bmp rows are padded to a multiple of 4 bytes
static uint32_t
_row_size (int32_t width)
{
  return ((uint32_t)width * sizeof (struct pixel) + 3) & ~3u;
}

static struct bmp_header
_read_header (FILE *bmp_file, char *file_path)
{
  struct bmp_header header;
  if (fread (&header, sizeof (struct bmp_header), 1, bmp_file) != 1)
    {
      fprintf (stderr,
               "error [bmp_image:_read_header]->couldn't read header from %s\n",
               file_path);
      fclose (bmp_file);
      exit (1);
    }
  if (header.signature != 0x4D42)
    {
      fprintf (stderr,
               "error [bmp_image:_read_header]->invalid signature from "
               "%s.expected 0x4D42 got 0x%04X\n",
               file_path, header.signature);
      fclose (bmp_file);
      exit (1);
    }
  if (header.bit_depth != 24 || header.compression != 0 || header.width <= 0
      || header.height == 0)
    {
      fprintf (stderr,
               "error [bmp_image:_read_header]->unsupported bitmap %s.only "
               "uncompressed 24bit bitmaps are supported\n",
               file_path);
      fclose (bmp_file);
      exit (1);
    }
  return header;
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
  struct pixel **pixels
      = (struct pixel **)malloc (header->height * sizeof (struct pixel *));
  if (pixels == NULL)
    {
      fprintf (stderr,
               "error [bmp_image:_read_pixels]->memory allocation failed for "
               "%s\n",
               file_path);
      exit (1);
    }
  for (int i = 0; i < header->height; i++)
    {
      // allocate the padded row size so the whole row can be read at once
      pixels[i] = (struct pixel *)malloc (row_size);
      if (pixels[i] == NULL)
        {
          fprintf (stderr,
                   "error [bmp_image:_read_pixels]->memory allocation failed "
                   "for %s\n",
                   file_path);
          exit (1);
        }
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
          exit (1);
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
      exit (1);
    }
  struct bmp_header header = _read_header (bmp_file, file_path);
  struct pixel **pixels = _read_pixels (bmp_file, file_path, &header);
  struct bmp_image *image
      = (struct bmp_image *)malloc (sizeof (struct bmp_image));
  if (image == NULL)
    {
      fprintf (stderr,
               "error [bmp_image:read_img]->memory allocation failed for %s\n",
               file_path);
      exit (1);
    }
  image->file_path = file_path;
  image->header = header;
  image->pixels = pixels;
  fclose (bmp_file);
  return image;
}

void
write_img (struct bmp_image *image, char *file_name)
{
  FILE *bmp_file = fopen (file_name, "wb");
  if (bmp_file == NULL)
    {
      fprintf (stderr, "error [bmp_image:write_img]->couldn't open file %s\n",
               file_name);
      exit (1);
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
  fwrite (&header, sizeof (struct bmp_header), 1, bmp_file);
  const uint8_t padding[3] = { 0, 0, 0 };
  for (int i = 0; i < header.height; i++)
    {
      fwrite (image->pixels[i], 1, row_bytes, bmp_file);
      fwrite (padding, 1, row_size - row_bytes, bmp_file);
    }
  fclose (bmp_file);
}

struct bmp_image *
copy_img (const struct bmp_image *image)
{
  struct bmp_image *copy
      = (struct bmp_image *)malloc (sizeof (struct bmp_image));
  struct pixel **pixels = (struct pixel **)malloc (image->header.height
                                                   * sizeof (struct pixel *));
  if (copy == NULL || pixels == NULL)
    {
      fprintf (stderr,
               "error [bmp_image:copy_img]->memory allocation failed for %s\n",
               image->file_path);
      exit (1);
    }
  size_t row_bytes = image->header.width * sizeof (struct pixel);
  for (int i = 0; i < image->header.height; i++)
    {
      pixels[i] = (struct pixel *)malloc (row_bytes);
      if (pixels[i] == NULL)
        {
          fprintf (stderr,
                   "error [bmp_image:copy_img]->memory allocation failed for "
                   "%s\n",
                   image->file_path);
          exit (1);
        }
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
  for (int i = 0; i < image->header.height; i++)
    {
      free (image->pixels[i]);
    }
  free (image->pixels);
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

static void
_add_padding (struct bmp_image *image)
{
  int new_width = image->header.width + 2 * PADDING_OFFSET;
  int new_height = image->header.height + 2 * PADDING_OFFSET;
  struct pixel **output_pixels
      = (struct pixel **)malloc (new_height * sizeof (struct pixel *));
  for (int i = 0; i < new_height; i++)
    {
      output_pixels[i]
          = (struct pixel *)calloc (new_width, sizeof (struct pixel));
    }
  for (int i = PADDING_OFFSET; i < image->header.height + PADDING_OFFSET; i++)
    {
      for (int j = PADDING_OFFSET; j < image->header.width + PADDING_OFFSET;
           j++)
        {
          output_pixels[i][j]
              = image->pixels[i - PADDING_OFFSET][j - PADDING_OFFSET];
        }
    }
  for (int i = 0; i < image->header.height; i++)
    {
      free (image->pixels[i]);
    }
  free (image->pixels);
  image->pixels = output_pixels;
}
static void
convolution (struct bmp_image *image, const double kernel[3][3])
{
  struct pixel **output_pixels = (struct pixel **)malloc (
      image->header.height * sizeof (struct pixel *));
  for (int i = 0; i < image->header.height; i++)
    {
      output_pixels[i] = (struct pixel *)malloc (image->header.width
                                                 * sizeof (struct pixel));
    }
  _add_padding (image);
  double r_sum = 0;
  double g_sum = 0;
  double b_sum = 0;
  for (int i = PADDING_OFFSET; i < image->header.height + PADDING_OFFSET; i++)
    {
      for (int j = PADDING_OFFSET; j < image->header.width + PADDING_OFFSET;
           j++)
        {
          r_sum = 0;
          g_sum = 0;
          b_sum = 0;
          for (int ki = -1; ki < 2; ki++)
            {
              for (int kj = -1; kj < 2; kj++)
                {
                  int x_idx = i + ki;
                  int y_idx = j + kj;
                  r_sum += kernel[ki + 1][kj + 1]
                           * (image->pixels[x_idx][y_idx].red);
                  b_sum += kernel[ki + 1][kj + 1]
                           * (image->pixels[x_idx][y_idx].blue);
                  g_sum += kernel[ki + 1][kj + 1]
                           * (image->pixels[x_idx][y_idx].green);
                }
            }
          // edge kernels produce negative responses, take the magnitude and
          // clamp so the result fits in a byte instead of wrapping around
          output_pixels[i - PADDING_OFFSET][j - PADDING_OFFSET].red
              = min ((int)lround (fabs (r_sum)), 255);
          output_pixels[i - PADDING_OFFSET][j - PADDING_OFFSET].green
              = min ((int)lround (fabs (g_sum)), 255);
          output_pixels[i - PADDING_OFFSET][j - PADDING_OFFSET].blue
              = min ((int)lround (fabs (b_sum)), 255);
        }
    }
  // the padded image has 2*PADDING_OFFSET more rows than the header says
  for (int i = 0; i < image->header.height + 2 * PADDING_OFFSET; i++)
    {
      free (image->pixels[i]);
    }
  free (image->pixels);
  image->pixels = output_pixels;
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
void
blur (struct bmp_image *image)
{
  const double kernel[3][3] = { { 1.0 / 9, 1.0 / 9, 1.0 / 9 },
                                { 1.0 / 9, 1.0 / 9, 1.0 / 9 },
                                { 1.0 / 9, 1.0 / 9, 1.0 / 9 } };
  convolution (image, kernel);
}

void
g_blur (struct bmp_image *image, double sigma)
{
  double kernel[3][3];
  double sum = 0;
  for (int i = 0; i < 3; i++)
    {
      for (int j = 0; j < 3; j++)
        {
          int x = i - 1;
          int y = j - 1;
          kernel[i][j] = exp (-(x * x + y * y) / (2 * sigma * sigma));
          sum += kernel[i][j];
        }
    }
  for (int i = 0; i < 3; i++)
    {
      for (int j = 0; j < 3; j++)
        {
          kernel[i][j] /= sum;
        }
    }
  convolution (image, (const double(*)[3])kernel);
}

void
v_edge (struct bmp_image *image)
{
  convert_to_bw (image);
  const double kernel[3][3] = { { -1, 0, 1 }, { -2, 0, 2 }, { -1, 0, 1 } };
  convolution (image, kernel);
}

void
h_edge (struct bmp_image *image)
{
  convert_to_bw (image);
  const double kernel[3][3] = { { -1, -2, -1 }, { 0, 0, 0 }, { 1, 2, 1 } };
  convolution (image, kernel);
}

// sobel edge magnitude: sqrt(gx^2 + gy^2) of the vertical and horizontal
// edge responses
void
edge (struct bmp_image *image)
{
  struct bmp_image *gy = copy_img (image);
  v_edge (image);
  h_edge (gy);
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
}

void
sharpen (struct bmp_image *image)
{
  const double kernel[3][3] = { { 0, -1, 0 }, { -1, 5, -1 }, { 0, -1, 0 } };
  convolution (image, kernel);
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
// is left untouched. both images must have the same height
void
concat (struct bmp_image *image1, struct bmp_image *image2)
{
  if (image1->header.height != image2->header.height)
    {
      fprintf (stderr,
               "error [bmp_image:concat]->height mismatch between %s (%d) "
               "and %s (%d)\n",
               image1->file_path, image1->header.height, image2->file_path,
               image2->header.height);
      exit (1);
    }
  int new_width = image1->header.width + image2->header.width;
  size_t row1_bytes = image1->header.width * sizeof (struct pixel);
  size_t row2_bytes = image2->header.width * sizeof (struct pixel);
  for (int i = 0; i < image1->header.height; i++)
    {
      struct pixel *row = (struct pixel *)malloc (new_width
                                                  * sizeof (struct pixel));
      if (row == NULL)
        {
          fprintf (stderr,
                   "error [bmp_image:concat]->memory allocation failed for "
                   "%s\n",
                   image1->file_path);
          exit (1);
        }
      memcpy (row, image1->pixels[i], row1_bytes);
      memcpy (row + image1->header.width, image2->pixels[i], row2_bytes);
      free (image1->pixels[i]);
      image1->pixels[i] = row;
    }
  image1->header.width = new_width;
}
