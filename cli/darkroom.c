// command line front end for the library. operations run left to right:
//   darkroom in.bmp out.bmp --resize 50% --gblur 1.5 --edge
#include "bmp_image.h"
#include <math.h>

typedef int (*op_fn) (struct bmp_image *image, const char *arg);

struct operation
{
  const char *name;
  const char *arg_help; // NULL when the operation takes no argument
  op_fn run;
  const char *description;
};

// the whole string has to be a number, so "12abc" and "" are rejected
static int
_parse_int (const char *text, int *value)
{
  char *end;
  long parsed = strtol (text, &end, 10);
  if (end == text || *end != '\0' || parsed < -1000000 || parsed > 1000000)
    {
      return -1;
    }
  *value = (int)parsed;
  return 0;
}

static int
_parse_double (const char *text, double *value)
{
  char *end;
  *value = strtod (text, &end);
  return (end == text || *end != '\0') ? -1 : 0;
}

static int
_bad_arg (const char *name, const char *arg, const char *expected)
{
  fprintf (stderr, "darkroom: --%s expects %s, got '%s'\n", name, expected,
           arg);
  return -1;
}

static int
_op_bw (struct bmp_image *image, const char *arg)
{
  (void)arg;
  convert_to_bw (image);
  return 0;
}

static int
_op_binary (struct bmp_image *image, const char *arg)
{
  int threshold;
  if (_parse_int (arg, &threshold) != 0 || threshold < 0 || threshold > 255)
    {
      return _bad_arg ("binary", arg, "a threshold between 0 and 255");
    }
  convert_to_binary (image, (uint8_t)threshold);
  return 0;
}

static int
_op_invert (struct bmp_image *image, const char *arg)
{
  (void)arg;
  invert (image);
  return 0;
}

static int
_op_brightness (struct bmp_image *image, const char *arg)
{
  int delta;
  if (_parse_int (arg, &delta) != 0)
    {
      return _bad_arg ("brightness", arg, "an integer");
    }
  adjust_brightness (image, delta);
  return 0;
}

static int
_op_blur (struct bmp_image *image, const char *arg)
{
  int size;
  if (_parse_int (arg, &size) != 0)
    {
      return _bad_arg ("blur", arg, "an odd kernel size");
    }
  return box_blur (image, size);
}

static int
_op_gblur (struct bmp_image *image, const char *arg)
{
  double sigma;
  if (_parse_double (arg, &sigma) != 0)
    {
      return _bad_arg ("gblur", arg, "a sigma");
    }
  return g_blur (image, sigma);
}

static int
_op_sharpen (struct bmp_image *image, const char *arg)
{
  (void)arg;
  return sharpen (image);
}

static int
_op_unsharp (struct bmp_image *image, const char *arg)
{
  double sigma;
  double amount;
  int length = 0;
  if (sscanf (arg, "%lf,%lf%n", &sigma, &amount, &length) != 2
      || arg[length] != '\0')
    {
      return _bad_arg ("unsharp", arg, "<sigma>,<amount>");
    }
  return unsharp_mask (image, sigma, amount);
}

static int
_op_emboss (struct bmp_image *image, const char *arg)
{
  (void)arg;
  return emboss (image);
}

static int
_op_edge (struct bmp_image *image, const char *arg)
{
  (void)arg;
  return edge (image);
}

static int
_op_prewitt (struct bmp_image *image, const char *arg)
{
  (void)arg;
  return prewitt_edge (image);
}

static int
_op_scharr (struct bmp_image *image, const char *arg)
{
  (void)arg;
  return scharr_edge (image);
}

static int
_op_laplacian (struct bmp_image *image, const char *arg)
{
  (void)arg;
  return laplacian (image);
}

static int
_op_flip_h (struct bmp_image *image, const char *arg)
{
  (void)arg;
  flip_horizontal (image);
  return 0;
}

static int
_op_flip_v (struct bmp_image *image, const char *arg)
{
  (void)arg;
  flip_vertical (image);
  return 0;
}

static int
_op_resize (struct bmp_image *image, const char *arg)
{
  int width;
  int height;
  double percent;
  int length = 0;
  if (sscanf (arg, "%dx%d%n", &width, &height, &length) == 2
      && arg[length] == '\0')
    {
      return resize (image, width, height);
    }
  if (sscanf (arg, "%lf%%%n", &percent, &length) == 1 && length > 0
      && arg[length] == '\0' && percent > 0)
    {
      width = max ((int)lround (image->header.width * percent / 100), 1);
      height = max ((int)lround (image->header.height * percent / 100), 1);
      return resize (image, width, height);
    }
  return _bad_arg ("resize", arg, "<width>x<height> or <percent>%");
}

static int
_op_crop (struct bmp_image *image, const char *arg)
{
  int x;
  int y;
  int width;
  int height;
  int length = 0;
  if (sscanf (arg, "%d,%d,%d,%d%n", &x, &y, &width, &height, &length) != 4
      || arg[length] != '\0')
    {
      return _bad_arg ("crop", arg, "<x>,<y>,<width>,<height>");
    }
  return crop (image, x, y, width, height);
}

static int
_op_rotate (struct bmp_image *image, const char *arg)
{
  int degrees;
  if (_parse_int (arg, &degrees) != 0)
    {
      return _bad_arg ("rotate", arg, "an angle in degrees");
    }
  return rotate (image, degrees);
}

static const struct operation operations[] = {
  { "bw", NULL, _op_bw, "convert to black and white" },
  { "binary", "<threshold>", _op_binary,
    "pure black and white around a 0-255 threshold" },
  { "invert", NULL, _op_invert, "colour negative" },
  { "brightness", "<delta>", _op_brightness,
    "add delta to every channel, can be negative" },
  { "blur", "<size>", _op_blur, "box blur with an odd kernel size" },
  { "gblur", "<sigma>", _op_gblur, "gaussian blur" },
  { "sharpen", NULL, _op_sharpen, "3x3 sharpen kernel" },
  { "unsharp", "<sigma>,<amount>", _op_unsharp, "unsharp mask" },
  { "emboss", NULL, _op_emboss, "raised relief look" },
  { "edge", NULL, _op_edge, "sobel edge detection" },
  { "prewitt", NULL, _op_prewitt, "prewitt edge detection" },
  { "scharr", NULL, _op_scharr, "scharr edge detection" },
  { "laplacian", NULL, _op_laplacian, "laplacian edge detection" },
  { "flip-h", NULL, _op_flip_h, "mirror left to right" },
  { "flip-v", NULL, _op_flip_v, "mirror top to bottom" },
  { "resize", "<w>x<h>|<percent>%", _op_resize, "bilinear resize" },
  { "crop", "<x>,<y>,<w>,<h>", _op_crop,
    "keep a rectangle, x and y measured from the top left" },
  { "rotate", "<degrees>", _op_rotate,
    "rotate clockwise by a multiple of 90" },
};

static const int operation_count
    = sizeof (operations) / sizeof (operations[0]);

static void
_usage (FILE *out)
{
  fprintf (out, "usage: darkroom <input.bmp> <output.bmp> [operation...]\n\n"
                "operations run in the order they are given:\n");
  for (int i = 0; i < operation_count; i++)
    {
      char flag[64];
      snprintf (flag, sizeof (flag), "--%s %s", operations[i].name,
                operations[i].arg_help ? operations[i].arg_help : "");
      fprintf (out, "  %-32s %s\n", flag, operations[i].description);
    }
}

static const struct operation *
_find_operation (const char *flag)
{
  if (strncmp (flag, "--", 2) != 0)
    {
      return NULL;
    }
  for (int i = 0; i < operation_count; i++)
    {
      if (strcmp (flag + 2, operations[i].name) == 0)
        {
          return &operations[i];
        }
    }
  return NULL;
}

int
main (int argc, char **argv)
{
  for (int i = 1; i < argc; i++)
    {
      if (strcmp (argv[i], "-h") == 0 || strcmp (argv[i], "--help") == 0)
        {
          _usage (stdout);
          return 0;
        }
    }
  if (argc < 3)
    {
      _usage (stderr);
      return 1;
    }
  // check the whole command line before touching any file, so a typo in the
  // last operation doesn't waste the work of the first ones
  for (int i = 3; i < argc; i++)
    {
      const struct operation *op = _find_operation (argv[i]);
      if (op == NULL)
        {
          fprintf (stderr, "darkroom: unknown operation '%s', see --help\n",
                   argv[i]);
          return 1;
        }
      if (op->arg_help != NULL && ++i >= argc)
        {
          fprintf (stderr, "darkroom: --%s needs an argument %s\n", op->name,
                   op->arg_help);
          return 1;
        }
    }
  struct bmp_image *image = read_img (argv[1]);
  if (image == NULL)
    {
      return 1;
    }
  for (int i = 3; i < argc; i++)
    {
      const struct operation *op = _find_operation (argv[i]);
      const char *arg = op->arg_help != NULL ? argv[++i] : NULL;
      if (op->run (image, arg) != 0)
        {
          fprintf (stderr, "darkroom: --%s failed, nothing was written\n",
                   op->name);
          free_img (image);
          return 1;
        }
    }
  int result = write_img (image, argv[2]);
  free_img (image);
  return result == 0 ? 0 : 1;
}
