#include "bmp_image.h"

static int failures = 0;

#define CHECK(cond)                                                           \
  do                                                                          \
    {                                                                         \
      if (!(cond))                                                            \
        {                                                                     \
          fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
          failures++;                                                         \
        }                                                                     \
    }                                                                         \
  while (0)

// writes a 24bit bmp where pixel (row, col) is blue = col * 20,
// green = row * 30 and red = 200 on the left half, 10 on the right half.
// rows are stored top-down when top_down is set
static void
_write_test_bmp (const char *path, int width, int height, int top_down)
{
  int row_size = (width * 3 + 3) & ~3;
  struct bmp_header header = { 0 };
  header.signature = 0x4D42;
  header.data_offset = sizeof (struct bmp_header);
  header.header_size = 40;
  header.width = width;
  header.height = top_down ? -height : height;
  header.planes = 1;
  header.bit_depth = 24;
  header.image_size = row_size * height;
  header.file_size = header.data_offset + header.image_size;
  FILE *file = fopen (path, "wb");
  fwrite (&header, sizeof (header), 1, file);
  for (int i = 0; i < height; i++)
    {
      int row = top_down ? height - 1 - i : i;
      for (int col = 0; col < width; col++)
        {
          uint8_t bgr[3] = { (uint8_t)(col * 20), (uint8_t)(row * 30),
                             col < width / 2 ? 200 : 10 };
          fwrite (bgr, 1, 3, file);
        }
      for (int pad = width * 3; pad < row_size; pad++)
        {
          fputc (0, file);
        }
    }
  fclose (file);
}

static int
_files_equal (const char *path1, const char *path2)
{
  FILE *file1 = fopen (path1, "rb");
  FILE *file2 = fopen (path2, "rb");
  int equal = file1 != NULL && file2 != NULL;
  while (equal)
    {
      int c1 = fgetc (file1);
      int c2 = fgetc (file2);
      equal = c1 == c2;
      if (c1 == EOF)
        {
          break;
        }
    }
  if (file1)
    fclose (file1);
  if (file2)
    fclose (file2);
  return equal;
}

static int
_pixels_equal (struct pixel a, struct pixel b)
{
  return a.red == b.red && a.green == b.green && a.blue == b.blue;
}

static void
_fill (struct bmp_image *image, struct pixel value)
{
  for (int i = 0; i < image->header.height; i++)
    for (int j = 0; j < image->header.width; j++)
      image->pixels[i][j] = value;
}

static int
_is_uniform (struct bmp_image *image, struct pixel value)
{
  for (int i = 0; i < image->header.height; i++)
    for (int j = 0; j < image->header.width; j++)
      if (!_pixels_equal (image->pixels[i][j], value))
        return 0;
  return 1;
}

static void
test_read_channels (void)
{
  struct bmp_image *image = read_img ("odd.bmp");
  CHECK (image != NULL);
  CHECK (image->header.width == 7 && image->header.height == 5);
  CHECK (image->pixels[0][0].red == 200 && image->pixels[0][6].red == 10);
  CHECK (image->pixels[3][2].green == 90 && image->pixels[3][2].blue == 40);
  free_img (image);
}

static void
test_round_trip_keeps_row_padding (void)
{
  struct bmp_image *image = read_img ("odd.bmp");
  CHECK (write_img (image, "odd_out.bmp") == 0);
  CHECK (_files_equal ("odd.bmp", "odd_out.bmp"));
  CHECK (write_img (image, "/nonexistent_dir/out.bmp") == -1);
  free_img (image);
}

static void
test_top_down (void)
{
  struct bmp_image *bottom_up = read_img ("odd.bmp");
  struct bmp_image *top_down = read_img ("top_down.bmp");
  CHECK (top_down != NULL && top_down->header.height == 5);
  for (int i = 0; i < 5; i++)
    for (int j = 0; j < 7; j++)
      CHECK (_pixels_equal (top_down->pixels[i][j], bottom_up->pixels[i][j]));
  free_img (bottom_up);
  free_img (top_down);
}

static void
test_read_errors (void)
{
  CHECK (read_img ("does_not_exist.bmp") == NULL);
  FILE *file = fopen ("garbage.bmp", "wb");
  for (int i = 0; i < 100; i++)
    fputc ('x', file);
  fclose (file);
  CHECK (read_img ("garbage.bmp") == NULL);
}

static void
test_filters_keep_uniform_image (void)
{
  struct pixel grey = { 50, 100, 150 };
  struct pixel black = { 0, 0, 0 };
  struct bmp_image *image = read_img ("odd.bmp");
  _fill (image, grey);

  struct bmp_image *copy = copy_img (image);
  CHECK (blur (copy) == 0 && _is_uniform (copy, grey));
  free_img (copy);

  // kernel is larger than the image, so padding must cover it
  copy = copy_img (image);
  CHECK (g_blur (copy, 3) == 0 && _is_uniform (copy, grey));
  free_img (copy);

  copy = copy_img (image);
  CHECK (sharpen (copy) == 0 && _is_uniform (copy, grey));
  free_img (copy);

  // no edges anywhere, including the border
  copy = copy_img (image);
  CHECK (edge (copy) == 0 && _is_uniform (copy, black));
  free_img (copy);

  free_img (image);
}

static void
test_convolve (void)
{
  struct bmp_image *image = read_img ("odd.bmp");
  struct bmp_image *copy = copy_img (image);
  double identity[25] = { 0 };
  identity[12] = 1;
  CHECK (convolve (copy, identity, 5) == 0);
  for (int i = 0; i < 5; i++)
    for (int j = 0; j < 7; j++)
      CHECK (_pixels_equal (copy->pixels[i][j], image->pixels[i][j]));
  CHECK (convolve (copy, identity, 4) == -1);
  CHECK (g_blur (copy, 0) == -1);
  free_img (copy);
  free_img (image);
}

static void
test_edge_finds_boundary (void)
{
  struct bmp_image *image = read_img ("odd.bmp");
  CHECK (v_edge (image) == 0);
  // the red channel changes between columns 2 and 3
  CHECK (image->pixels[2][2].red > 100 && image->pixels[2][3].red > 100);
  CHECK (image->pixels[2][0].red < image->pixels[2][2].red);
  free_img (image);
}

static void
test_pixel_operations (void)
{
  struct bmp_image *image = read_img ("odd.bmp");
  struct bmp_image *copy = copy_img (image);

  invert (copy);
  CHECK (copy->pixels[0][0].red == 55);
  invert (copy);
  CHECK (_pixels_equal (copy->pixels[0][0], image->pixels[0][0]));

  adjust_brightness (copy, 100);
  CHECK (copy->pixels[0][0].red == 255 && copy->pixels[0][1].blue == 120);
  adjust_brightness (copy, -500);
  CHECK (_is_uniform (copy, (struct pixel){ 0, 0, 0 }));
  free_img (copy);

  copy = copy_img (image);
  flip_horizontal (copy);
  CHECK (_pixels_equal (copy->pixels[1][0], image->pixels[1][6]));
  flip_vertical (copy);
  CHECK (_pixels_equal (copy->pixels[0][0], image->pixels[4][6]));
  free_img (copy);

  copy = copy_img (image);
  convert_to_binary (copy, 200);
  CHECK (copy->pixels[0][0].red == 0);
  convert_to_bw (image);
  CHECK (image->pixels[0][0].red == image->pixels[0][0].green);
  free_img (copy);
  free_img (image);
}

static void
test_concat (void)
{
  struct bmp_image *image1 = read_img ("odd.bmp");
  struct bmp_image *image2 = read_img ("odd.bmp");
  CHECK (concat (image1, image2) == 0);
  CHECK (image1->header.width == 14);
  CHECK (_pixels_equal (image1->pixels[3][9], image2->pixels[3][2]));
  CHECK (write_img (image1, "concat.bmp") == 0);
  struct bmp_image *reread = read_img ("concat.bmp");
  CHECK (reread != NULL && reread->header.width == 14);
  free_img (reread);

  image2->header.height = 3;
  CHECK (concat (image1, image2) == -1);
  CHECK (image1->header.width == 14);
  image2->header.height = 5;
  free_img (image1);
  free_img (image2);
}

static void
test_resize (void)
{
  struct pixel grey = { 50, 100, 150 };
  struct bmp_image *image = read_img ("odd.bmp");

  // same size is a no-op
  struct bmp_image *copy = copy_img (image);
  CHECK (resize (copy, 7, 5) == 0);
  for (int i = 0; i < 5; i++)
    for (int j = 0; j < 7; j++)
      CHECK (_pixels_equal (copy->pixels[i][j], image->pixels[i][j]));
  free_img (copy);

  // upscaling keeps the corners and stays between neighbouring values
  copy = copy_img (image);
  CHECK (resize (copy, 21, 15) == 0);
  CHECK (copy->header.width == 21 && copy->header.height == 15);
  CHECK (_pixels_equal (copy->pixels[0][0], image->pixels[0][0]));
  CHECK (_pixels_equal (copy->pixels[14][20], image->pixels[4][6]));
  CHECK (copy->pixels[7][10].green >= 60 && copy->pixels[7][10].green <= 90);
  // odd widths need row padding on write
  CHECK (write_img (copy, "resized.bmp") == 0);
  struct bmp_image *reread = read_img ("resized.bmp");
  CHECK (reread != NULL && reread->header.width == 21);
  free_img (reread);
  free_img (copy);

  copy = copy_img (image);
  _fill (copy, grey);
  CHECK (resize (copy, 3, 2) == 0 && _is_uniform (copy, grey));
  CHECK (resize (copy, 0, 5) == -1 && resize (copy, 5, -1) == -1);
  CHECK (copy->header.width == 3);
  free_img (copy);
  free_img (image);
}

static void
test_crop (void)
{
  struct bmp_image *image = read_img ("odd.bmp");
  struct bmp_image *copy = copy_img (image);
  // 3x2 rectangle starting at column 2, one row below the top. the top row
  // is the last stored row
  CHECK (crop (copy, 2, 1, 3, 2) == 0);
  CHECK (copy->header.width == 3 && copy->header.height == 2);
  CHECK (_pixels_equal (copy->pixels[1][0], image->pixels[3][2]));
  CHECK (_pixels_equal (copy->pixels[0][2], image->pixels[2][4]));

  CHECK (crop (copy, 0, 0, 4, 1) == -1);
  CHECK (crop (copy, -1, 0, 1, 1) == -1);
  CHECK (crop (copy, 0, 0, 0, 1) == -1);
  CHECK (copy->header.width == 3);
  free_img (copy);

  copy = copy_img (image);
  CHECK (crop (copy, 0, 0, 7, 5) == 0);
  for (int i = 0; i < 5; i++)
    for (int j = 0; j < 7; j++)
      CHECK (_pixels_equal (copy->pixels[i][j], image->pixels[i][j]));
  free_img (copy);
  free_img (image);
}

static int
_images_equal (struct bmp_image *a, struct bmp_image *b)
{
  if (a->header.width != b->header.width
      || a->header.height != b->header.height)
    return 0;
  for (int i = 0; i < a->header.height; i++)
    for (int j = 0; j < a->header.width; j++)
      if (!_pixels_equal (a->pixels[i][j], b->pixels[i][j]))
        return 0;
  return 1;
}

static void
test_rotate (void)
{
  struct bmp_image *image = read_img ("odd.bmp");
  struct bmp_image *copy = copy_img (image);

  // clockwise: the bottom left corner becomes the top left corner and the
  // top left corner becomes the top right corner
  CHECK (rotate (copy, 90) == 0);
  CHECK (copy->header.width == 5 && copy->header.height == 7);
  CHECK (_pixels_equal (copy->pixels[6][0], image->pixels[0][0]));
  CHECK (_pixels_equal (copy->pixels[6][4], image->pixels[4][0]));
  CHECK (rotate (copy, 90) == 0 && rotate (copy, 90) == 0
         && rotate (copy, 90) == 0);
  CHECK (_images_equal (copy, image));

  CHECK (rotate (copy, 90) == 0 && rotate (copy, -90) == 0);
  CHECK (_images_equal (copy, image));
  CHECK (rotate (copy, 270) == 0 && rotate (copy, 90) == 0);
  CHECK (_images_equal (copy, image));

  struct bmp_image *flipped = copy_img (image);
  flip_horizontal (flipped);
  flip_vertical (flipped);
  CHECK (rotate (copy, 180) == 0 && _images_equal (copy, flipped));
  CHECK (rotate (copy, 45) == -1 && _images_equal (copy, flipped));
  CHECK (rotate (copy, 720) == 0 && _images_equal (copy, flipped));

  free_img (flipped);
  free_img (copy);
  free_img (image);
}

static void
test_box_blur (void)
{
  struct pixel grey = { 50, 100, 150 };
  struct bmp_image *image = read_img ("odd.bmp");
  struct bmp_image *copy = copy_img (image);
  CHECK (box_blur (copy, 5) == 0);
  // the 5x5 average around column 1 mixes the 200 and 10 red halves
  CHECK (copy->pixels[2][1].red < 200 && copy->pixels[2][1].red > 10);
  free_img (copy);

  // a 3x3 box blur is the default blur
  copy = copy_img (image);
  struct bmp_image *expected = copy_img (image);
  CHECK (box_blur (copy, 3) == 0 && blur (expected) == 0);
  for (int i = 0; i < 5; i++)
    for (int j = 0; j < 7; j++)
      CHECK (_pixels_equal (copy->pixels[i][j], expected->pixels[i][j]));
  free_img (expected);

  _fill (copy, grey);
  CHECK (box_blur (copy, 7) == 0 && _is_uniform (copy, grey));
  CHECK (box_blur (copy, 4) == -1 && box_blur (copy, 0) == -1);
  free_img (copy);
  free_img (image);
}

static void
test_emboss (void)
{
  struct pixel grey = { 50, 100, 150 };
  struct bmp_image *image = read_img ("odd.bmp");
  struct bmp_image *copy = copy_img (image);
  CHECK (emboss (copy) == 0);
  // pixels next to the red boundary change, the ones far from it don't
  CHECK (copy->pixels[2][3].red != image->pixels[2][3].red);
  CHECK (copy->pixels[2][0].red == image->pixels[2][0].red);
  _fill (copy, grey);
  CHECK (emboss (copy) == 0 && _is_uniform (copy, grey));
  free_img (copy);
  free_img (image);
}

static void
test_laplacian (void)
{
  struct pixel grey = { 50, 100, 150 };
  struct pixel black = { 0, 0, 0 };
  struct bmp_image *image = read_img ("odd.bmp");
  struct bmp_image *copy = copy_img (image);
  CHECK (laplacian (copy) == 0);
  CHECK (copy->pixels[2][2].red > 20 && copy->pixels[2][3].red > 20);
  CHECK (copy->pixels[2][2].red == copy->pixels[2][2].green);
  _fill (copy, grey);
  CHECK (laplacian (copy) == 0 && _is_uniform (copy, black));
  free_img (copy);
  free_img (image);
}

static void
test_gradient_edges (void)
{
  struct pixel grey = { 50, 100, 150 };
  struct pixel black = { 0, 0, 0 };
  int (*detectors[]) (struct bmp_image *)
      = { edge, prewitt_edge, scharr_edge };
  struct bmp_image *image = read_img ("odd.bmp");
  for (int k = 0; k < 3; k++)
    {
      struct bmp_image *copy = copy_img (image);
      CHECK (detectors[k] (copy) == 0);
      CHECK (copy->pixels[2][2].red > 100 && copy->pixels[2][3].red > 100);
      CHECK (copy->pixels[2][2].red == copy->pixels[2][2].blue);
      _fill (copy, grey);
      CHECK (detectors[k] (copy) == 0 && _is_uniform (copy, black));
      free_img (copy);
    }
  free_img (image);
}

static void
test_unsharp_mask (void)
{
  struct pixel grey = { 50, 100, 150 };
  struct bmp_image *image = read_img ("odd.bmp");
  struct bmp_image *copy = copy_img (image);
  // the boundary gets more contrast: the bright side brighter, the dark side
  // darker
  CHECK (unsharp_mask (copy, 1.0, 1.5) == 0);
  CHECK (copy->pixels[2][2].red > image->pixels[2][2].red);
  CHECK (copy->pixels[2][3].red < image->pixels[2][3].red);
  free_img (copy);

  copy = copy_img (image);
  CHECK (unsharp_mask (copy, 1.0, 0) == 0);
  for (int i = 0; i < 5; i++)
    for (int j = 0; j < 7; j++)
      CHECK (_pixels_equal (copy->pixels[i][j], image->pixels[i][j]));

  _fill (copy, grey);
  CHECK (unsharp_mask (copy, 2.0, 3.0) == 0 && _is_uniform (copy, grey));
  CHECK (unsharp_mask (copy, 0, 1.0) == -1);
  CHECK (unsharp_mask (copy, 1.0, -1.0) == -1);
  free_img (copy);
  free_img (image);
}

int
main (void)
{
  _write_test_bmp ("odd.bmp", 7, 5, 0);
  _write_test_bmp ("top_down.bmp", 7, 5, 1);

  test_read_channels ();
  test_round_trip_keeps_row_padding ();
  test_top_down ();
  test_read_errors ();
  test_filters_keep_uniform_image ();
  test_convolve ();
  test_edge_finds_boundary ();
  test_pixel_operations ();
  test_concat ();
  test_resize ();
  test_crop ();
  test_rotate ();
  test_box_blur ();
  test_emboss ();
  test_laplacian ();
  test_gradient_edges ();
  test_unsharp_mask ();

  if (failures == 0)
    printf ("all tests passed\n");
  return failures == 0 ? 0 : 1;
}
