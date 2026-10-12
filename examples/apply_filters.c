// usage: apply_filters <input.bmp> <output prefix>
// writes one bmp per filter, e.g. "out/lenna" -> out/lenna_blur.bmp, ...
#include "bmp_image.h"

typedef int (*filter_fn) (struct bmp_image *image);

static int
_gaussian (struct bmp_image *image)
{
  return g_blur (image, 2.0);
}

static int
_bw (struct bmp_image *image)
{
  convert_to_bw (image);
  return 0;
}

static int
_invert (struct bmp_image *image)
{
  invert (image);
  return 0;
}

static int
_mirror (struct bmp_image *image)
{
  flip_horizontal (image);
  return 0;
}

// the original image next to its edges
static int
_side_by_side (struct bmp_image *image)
{
  struct bmp_image *edges = copy_img (image);
  if (edges == NULL)
    {
      return -1;
    }
  int result = edge (edges) == 0 ? concat (image, edges) : -1;
  free_img (edges);
  return result;
}

struct filter
{
  const char *name;
  filter_fn apply;
};

int
main (int argc, char **argv)
{
  if (argc != 3)
    {
      fprintf (stderr, "usage: %s <input.bmp> <output prefix>\n", argv[0]);
      return 1;
    }
  struct bmp_image *original = read_img (argv[1]);
  if (original == NULL)
    {
      return 1;
    }
  const struct filter filters[] = {
    { "bw", _bw },         { "blur", blur },
    { "gblur", _gaussian }, { "sharpen", sharpen },
    { "edge", edge },       { "invert", _invert },
    { "mirror", _mirror },  { "side_by_side", _side_by_side },
  };
  int failed = 0;
  for (size_t i = 0; i < sizeof (filters) / sizeof (filters[0]); i++)
    {
      char out_path[4096];
      snprintf (out_path, sizeof (out_path), "%s_%s.bmp", argv[2],
                filters[i].name);
      struct bmp_image *image = copy_img (original);
      if (image == NULL || filters[i].apply (image) != 0
          || write_img (image, out_path) != 0)
        {
          fprintf (stderr, "%s failed\n", filters[i].name);
          failed = 1;
        }
      else
        {
          printf ("wrote %s\n", out_path);
        }
      free_img (image);
    }
  free_img (original);
  return failed;
}
