/* ######################### includes ############################################################################### */

#include <grm/dom_render/graphics_tree/Document.hxx>
#include <grm/dom_render/graphics_tree/Element.hxx>
#include <grm/dom_render/graphics_tree/Comment.hxx>
#include <grm/dom_render/graphics_tree/util.hxx>
#include <grm/dom_render/render.hxx>

extern "C" {

#include <grm/layout.h>
#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <cm.h>

#include "base64_int.h"
#include <grm/dump.h>
#include "event_int.h"
#include "gks.h"
#include "gr.h"
#include "gr3.h"
#include "logging_int.h"

#ifndef NO_EXPAT
#include <expat.h>
#endif
}

#include "plot_int.h"
#include "grm/layout.hxx"

extern "C" {

#include "util_int.h"

#include "datatype/double_map_int.h"
#include "datatype/string_map_int.h"
#include "datatype/string_array_map_int.h"
#include "datatype/template/map_int.h"
#include "datatype/template/set_int.h"


/* ######################### private interface ###################################################################### */

/* ========================= datatypes ============================================================================== */

/* ------------------------- args set ------------------------------------------------------------------------------- */

DECLARE_SET_TYPE(args, grm_args_t *)


/* ------------------------- string-to-plot_func map ---------------------------------------------------------------- */

DECLARE_MAP_TYPE(plot_func, plot_func_t)


/* ------------------------- string-to-args_set map ----------------------------------------------------------------- */

DECLARE_MAP_TYPE(args_set, args_set_t *)


#undef DECLARE_SET_TYPE
#undef DECLARE_MAP_TYPE


/* ========================= macros ================================================================================= */

/* ------------------------- math ----------------------------------------------------------------------------------- */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef isfinite
#define isfinite(x) ((x) - (x) == (x) - (x))
#endif

/* ------------------------- args get ------------------------------------------------------------------------------- */

#define ARGS_VALUE_ITERATOR_GET(value_it, length, array) \
  if (value_it->next(value_it) == NULL)                  \
    {                                                    \
      args_value_iterator_delete(value_it);              \
      return ERROR_INTERNAL;                             \
    }                                                    \
  length = value_it->array_length;                       \
  array = *(double **)value_it->value_ptr;

/* ------------------------- re-implementation of x_lin/x_log ------------------------------------------------------- */

#define X_FLIP_IF(x, scale_options, xmin, xmax) \
  (GR_OPTION_FLIP_X & scale_options ? xmin + xmax : 0) + (GR_OPTION_FLIP_X & scale_options ? -1 : 1) * x

#define X_LIN(x, scale_options, xmin, xmax, a, b) \
  X_FLIP_IF((GR_OPTION_X_LOG & scale_options ? (x > 0 ? a * log10(x) + b : -FLT_MAX) : x), scale_options, xmin, xmax)

#define X_LOG(x, scale_options, xmin, xmax, a, b)                                                             \
  (GR_OPTION_X_LOG & scale_options ? (pow(10.0, (double)((X_FLIP_IF(x, scale_options, xmin, xmax) - b) / a))) \
                                   : X_FLIP_IF(x, scale_options, xmin, xmax))

/* ========================= methods ================================================================================ */

/* ------------------------- args set ------------------------------------------------------------------------------- */

DECLARE_SET_METHODS(args)


/* ------------------------- string-to-plot_func map ---------------------------------------------------------------- */

DECLARE_MAP_METHODS(plot_func)


/* ------------------------- string-to-args_set map ----------------------------------------------------------------- */

DECLARE_MAP_METHODS(args_set)


#undef DECLARE_SET_METHODS
#undef DECLARE_MAP_METHODS


/* ######################### internal implementation ################################################################ */

/* ========================= static variables ======================================================================= */

/* ------------------------- plot ----------------------------------------------------------------------------------- */

/* ~~~~~~~~~~~~~~~~~~~~~~~~~ general ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

static int plot_static_variables_initialized = 0;
const char *plot_hierarchy_names[] = {"root", "plots", "subplots", "series", NULL};
static int plot_scatter_markertypes[] = {
    GKS_K_MARKERTYPE_SOLID_CIRCLE,   GKS_K_MARKERTYPE_SOLID_TRI_UP, GKS_K_MARKERTYPE_SOLID_TRI_DOWN,
    GKS_K_MARKERTYPE_SOLID_SQUARE,   GKS_K_MARKERTYPE_SOLID_BOWTIE, GKS_K_MARKERTYPE_SOLID_HGLASS,
    GKS_K_MARKERTYPE_SOLID_DIAMOND,  GKS_K_MARKERTYPE_SOLID_STAR,   GKS_K_MARKERTYPE_SOLID_TRI_RIGHT,
    GKS_K_MARKERTYPE_SOLID_TRI_LEFT, GKS_K_MARKERTYPE_SOLID_PLUS,   GKS_K_MARKERTYPE_PENTAGON,
    GKS_K_MARKERTYPE_HEXAGON,        GKS_K_MARKERTYPE_HEPTAGON,     GKS_K_MARKERTYPE_OCTAGON,
    GKS_K_MARKERTYPE_STAR_4,         GKS_K_MARKERTYPE_STAR_5,       GKS_K_MARKERTYPE_STAR_6,
    GKS_K_MARKERTYPE_STAR_7,         GKS_K_MARKERTYPE_STAR_8,       GKS_K_MARKERTYPE_VLINE,
    GKS_K_MARKERTYPE_HLINE,          GKS_K_MARKERTYPE_OMARK,        INT_MAX};


/* ~~~~~~~~~~~~~~~~~~~~~~~~~ args ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

static grm_args_t *global_root_args = NULL;
grm_args_t *active_plot_args = NULL;
static unsigned int active_plot_index = 0;
grid_t *global_grid = NULL;
static std::shared_ptr<GR::Render> global_render;
static std::shared_ptr<GR::Element> global_root;
static std::shared_ptr<GR::Element> currentDomElement;

/* ~~~~~~~~~~~~~~~~~~~~~~~~~ event handling ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

event_queue_t *event_queue = NULL;


/* ~~~~~~~~~~~~~~~~~~~~~~~~~ kind to fmt ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

static string_map_entry_t kind_to_fmt[] = {{"line", "xys"},          {"hexbin", "xys"},
                                           {"polar", "xys"},         {"shade", "xys"},
                                           {"stem", "xys"},          {"step", "xys"},
                                           {"contour", "xyzc"},      {"contourf", "xyzc"},
                                           {"tricont", "xyzc"},      {"trisurf", "xyzc"},
                                           {"surface", "xyzc"},      {"wireframe", "xyzc"},
                                           {"plot3", "xyzc"},        {"scatter", "xyzc"},
                                           {"scatter3", "xyzc"},     {"quiver", "xyuv"},
                                           {"heatmap", "xyzc"},      {"hist", "x"},
                                           {"barplot", "y"},         {"isosurface", "c"},
                                           {"imshow", "c"},          {"nonuniformheatmap", "xyzc"},
                                           {"polar_histogram", "x"}, {"pie", "x"},
                                           {"volume", "c"}};


/* ~~~~~~~~~~~~~~~~~~~~~~~~~ kind to func ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

static plot_func_map_entry_t kind_to_func[] = {{"line", plot_line},
                                               {"step", plot_step},
                                               {"scatter", plot_scatter},
                                               {"quiver", plot_quiver},
                                               {"stem", plot_stem},
                                               {"hist", plot_hist},
                                               {"barplot", plot_barplot},
                                               {"contour", plot_contour},
                                               {"contourf", plot_contourf},
                                               {"hexbin", plot_hexbin},
                                               {"heatmap", plot_heatmap},
                                               {"wireframe", plot_wireframe},
                                               {"surface", plot_surface},
                                               {"plot3", plot_plot3},
                                               {"scatter3", plot_scatter3},
                                               {"imshow", plot_imshow},
                                               {"isosurface", plot_isosurface},
                                               {"polar", plot_polar},
                                               {"trisurf", plot_trisurf},
                                               {"tricont", plot_tricont},
                                               {"shade", plot_shade},
                                               {"nonuniformheatmap", plot_heatmap},
                                               {"polar_histogram", plot_polar_histogram},
                                               {"pie", plot_pie},
                                               {"volume", plot_volume}};


/* ~~~~~~~~~~~~~~~~~~~~~~~~~ maps ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

static double_map_t *meters_per_unit_map = NULL;
static string_map_t *fmt_map = NULL;
static plot_func_map_t *plot_func_map = NULL;
static string_map_t *plot_valid_keys_map = NULL;
static string_array_map_t *type_map = NULL;


/* ~~~~~~~~~~~~~~~~~~~~~~~~~ plot clear ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

const char *plot_clear_exclude_keys[] = {"array_index", "in_use", NULL};


/* ~~~~~~~~~~~~~~~~~~~~~~~~~ plot merge ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

const char *plot_merge_ignore_keys[] = {"id", "series_id", "subplot_id", "plot_id", "array_index", "in_use", NULL};
const char *plot_merge_clear_keys[] = {"series", NULL};


/* ~~~~~~~~~~~~~~~~~~~~~~~~~ kind to func ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

static const double_map_entry_t symbol_to_meters_per_unit[] = {
    {"m", 1.0},     {"dm", 0.1},    {"cm", 0.01},  {"mm", 0.001},        {"in", 0.0254},
    {"\"", 0.0254}, {"ft", 0.3048}, {"'", 0.0254}, {"pc", 0.0254 / 6.0}, {"pt", 0.0254 / 72.0},
};


/* ~~~~~~~~~~~~~~~~~~~~~~~~~ text encoding ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

static int pre_plot_text_encoding = -1;


/* ~~~~~~~~~~~~~~~~~~~~~~~~~ valid keys ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* IMPORTANT: Every key should only be part of ONE array -> keys can be assigned to the right object, if a user sends a
 * flat object that mixes keys of different hierarchies */

const char *valid_root_keys[] = {"plots", "append_plots", "hold_plots", NULL};
const char *valid_plot_keys[] = {"clear", "figsize", "raw", "size", "subplots", "update", NULL};

const char *valid_subplot_keys[] = {"abs_height",
                                    "abs_width",
                                    "adjust_xlim",
                                    "adjust_ylim",
                                    "adjust_zlim",
                                    "alpha",
                                    "angle_ticks",
                                    "aspect_ratio",
                                    "backgroundcolor",
                                    "bar_color",
                                    "bar_width",
                                    "col",
                                    "colspan",
                                    "colormap",
                                    "fit_parents_height",
                                    "fit_parents_width",
                                    "font",
                                    "font_precision",
                                    "grid_element",
                                    "ind_bar_color",
                                    "ind_edge_color",
                                    "ind_edge_width",
                                    "keep_aspect_ratio",
                                    "kind",
                                    "labels",
                                    "levels",
                                    "location",
                                    "normalization",
                                    "panzoom",
                                    "phiflip",
                                    "rel_height",
                                    "rel_width",
                                    "resample_method",
                                    "reset_ranges",
                                    "rings",
                                    "rotation",
                                    "row",
                                    "rowspan",
                                    "series",
                                    "style",
                                    "subplot",
                                    "tilt",
                                    "title",
                                    "xbins",
                                    "xflip",
                                    "xform",
                                    "xgrid",
                                    "xlabel",
                                    "xlim",
                                    "xlog",
                                    "xticklabels",
                                    "ybins",
                                    "yflip",
                                    "ygrid",
                                    "ylabel",
                                    "ylim",
                                    "ylog",
                                    "zflip",
                                    "zgrid",
                                    "zlim",
                                    "zlog",
                                    NULL};
const char *valid_series_keys[] = {"a",          "algorithm",
                                   "bin_width",  "bin_edges",
                                   "bin_counts", "c",
                                   "c_dims",     "crange",
                                   "draw_edges", "dmin",
                                   "dmax",       "edge_color",
                                   "edge_width", "error",
                                   "face_color", "foreground_color",
                                   "indices",    "inner_series",
                                   "isovalue",   "markertype",
                                   "nbins",      "philim",
                                   "rgb",        "rlim",
                                   "s",          "spec",
                                   "step_where", "stairs",
                                   "u",          "v",
                                   "weights",    "x",
                                   "xcolormap",  "xrange",
                                   "y",          "ycolormap",
                                   "ylabels",    "yrange",
                                   "z",          "z_dims",
                                   "zrange",     NULL};

/* ~~~~~~~~~~~~~~~~~~~~~~~~~ valid types ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* If multiple formats are supported use `|` as separator
 * Example: "i|s" for supporting both integer and strings */
/* TODO: type for format "s"? */
static string_map_entry_t key_to_formats[] = {{"a", "A"},
                                              {"abs_height", "d"},
                                              {"abs_width", "d"},
                                              {"algorithm", "i|s"},
                                              {"adjust_xlim", "i"},
                                              {"adjust_ylim", "i"},
                                              {"adjust_zlim", "i"},
                                              {"alpha", "d"},
                                              {"aspect_ratio", "d"},
                                              {"append_plots", "i"},
                                              {"backgroundcolor", "i"},
                                              {"bar_color", "D|i"},
                                              {"c", "D|I"},
                                              {"c_dims", "I"},
                                              {"crange", "D"},
                                              {"col", "i|I"},
                                              {"colspan", "i|I"},
                                              {"colormap", "i"},
                                              {"dmin", "d"},
                                              {"dmax", "d"},
                                              {"edge_color", "D|i"},
                                              {"edge_width", "d"},
                                              {"error", "a"},
                                              {"fig_size", "D"},
                                              {"fit_parents_height", "i"},
                                              {"fit_parents_width", "i"},
                                              {"font", "i"},
                                              {"font_precision", "i"},
                                              {"foreground_color", "D"},
                                              {"grid_element", "s"},
                                              {"hold_plots", "i"},
                                              {"ind_bar_color", "A"},
                                              {"ind_edge_color", "A"},
                                              {"ind_edge_width", "A"},
                                              {"isovalue", "d"},
                                              {"keep_aspect_ratio", "i"},
                                              {"kind", "s"},
                                              {"labels", "S"},
                                              {"levels", "i"},
                                              {"location", "i"},
                                              {"markertype", "i|D"},
                                              {"nbins", "i"},
                                              {"panzoom", "D"},
                                              {"raw", "s"},
                                              {"rel_height", "d"},
                                              {"rel_width", "d"},
                                              {"resample_method", "s|i"},
                                              {"reset_ranges", "i"},
                                              {"rotation", "d"},
                                              {"row", "i|I"},
                                              {"rowspan", "i|I"},
                                              {"size", "D|A"},
                                              {"spec", "s"},
                                              {"step_where", "s"},
                                              {"style", "s"},
                                              {"subplot", "D"},
                                              {"tilt", "d"},
                                              {"title", "s"},
                                              {"u", "D"},
                                              {"update", "i"},
                                              {"v", "D"},
                                              {"x", "D|I"},
                                              {"xbins", "i"},
                                              {"xcolormap", "i"},
                                              {"xflip", "i"},
                                              {"xform", "i"},
                                              {"xgrid", "i"},
                                              {"xlabel", "s"},
                                              {"xlim", "D"},
                                              {"xrange", "D"},
                                              {"xlog", "i"},
                                              {"y", "D"},
                                              {"ybins", "i"},
                                              {"ycolormap", "i"},
                                              {"yflip", "i"},
                                              {"yform", "i"},
                                              {"ygrid", "i"},
                                              {"ylabel", "s"},
                                              {"ylim", "D"},
                                              {"yrange", "D"},
                                              {"ylog", "i"},
                                              {"z", "D"},
                                              {"z_dims", "I"},
                                              {"zflip", "i"},
                                              {"zgrid", "i"},
                                              {"zlim", "D"},
                                              {"zrange", "D"},
                                              {"zlog", "i"}};

/* ========================= functions ============================================================================== */

/* ------------------------- plot ----------------------------------------------------------------------------------- */

/* ~~~~~~~~~~~~~~~~~~~~~~~~~ general ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

err_t plot_init_static_variables(void)
{
  err_t error = ERROR_NONE;

  if (!plot_static_variables_initialized)
    {
      logger((stderr, "Initializing static plot variables\n"));
      event_queue = event_queue_new();
      global_root_args = grm_args_new();
      error_cleanup_and_set_error_if(global_root_args == NULL, ERROR_MALLOC);
      error = plot_init_args_structure(global_root_args, plot_hierarchy_names, 1);
      error_cleanup_if_error;
      plot_set_flag_defaults();
      error_cleanup_and_set_error_if(!grm_args_values(global_root_args, "plots", "a", &active_plot_args),
                                     ERROR_INTERNAL);
      active_plot_index = 1;
      global_render = GR::Render::createRender();
      meters_per_unit_map = double_map_new_with_data(array_size(symbol_to_meters_per_unit), symbol_to_meters_per_unit);
      error_cleanup_and_set_error_if(meters_per_unit_map == NULL, ERROR_MALLOC);
      fmt_map = string_map_new_with_data(array_size(kind_to_fmt), kind_to_fmt);
      error_cleanup_and_set_error_if(fmt_map == NULL, ERROR_MALLOC);
      plot_func_map = plot_func_map_new_with_data(array_size(kind_to_func), kind_to_func);
      error_cleanup_and_set_error_if(plot_func_map == NULL, ERROR_MALLOC);
      {
        const char **hierarchy_keys[] = {valid_root_keys, valid_plot_keys, valid_subplot_keys, valid_series_keys, NULL};
        const char **hierarchy_names_ptr, ***hierarchy_keys_ptr, **current_key_ptr;
        plot_valid_keys_map = string_map_new(array_size(valid_root_keys) + array_size(valid_plot_keys) +
                                             array_size(valid_subplot_keys) + array_size(valid_series_keys));
        error_cleanup_and_set_error_if(plot_valid_keys_map == NULL, ERROR_MALLOC);
        hierarchy_keys_ptr = hierarchy_keys;
        hierarchy_names_ptr = plot_hierarchy_names;
        while (*hierarchy_names_ptr != NULL && *hierarchy_keys_ptr != NULL)
          {
            current_key_ptr = *hierarchy_keys_ptr;
            while (*current_key_ptr != NULL)
              {
                string_map_insert(plot_valid_keys_map, *current_key_ptr, *hierarchy_names_ptr);
                ++current_key_ptr;
              }
            ++hierarchy_names_ptr;
            ++hierarchy_keys_ptr;
          }
      }
      type_map = string_array_map_new_from_string_split(array_size(key_to_formats), key_to_formats, '|');
      error_cleanup_and_set_error_if(type_map == NULL, ERROR_MALLOC);
      plot_static_variables_initialized = 1;
    }
  return ERROR_NONE;

error_cleanup:
  if (global_root_args != NULL)
    {
      grm_args_delete(global_root_args);
      global_root_args = NULL;
    }
  if (meters_per_unit_map != NULL)
    {
      double_map_delete(meters_per_unit_map);
      meters_per_unit_map = NULL;
    }
  if (fmt_map != NULL)
    {
      string_map_delete(fmt_map);
      fmt_map = NULL;
    }
  if (plot_func_map != NULL)
    {
      plot_func_map_delete(plot_func_map);
      plot_func_map = NULL;
    }
  if (plot_valid_keys_map != NULL)
    {
      string_map_delete(plot_valid_keys_map);
      plot_valid_keys_map = NULL;
    }
  if (type_map != NULL)
    {
      string_array_map_delete(type_map);
      type_map = NULL;
    }
  return error;
}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~ plot arguments ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

err_t plot_merge_args(grm_args_t *args, const grm_args_t *merge_args, const char **hierarchy_name_ptr,
                      uint_map_t *hierarchy_to_id, int hold_always)
{
  static args_set_map_t *key_to_cleared_args = NULL;
  static int recursion_level = -1;
  int plot_id, subplot_id, series_id;
  int append_plots;
  args_iterator_t *merge_it = NULL;
  arg_t *arg, *merge_arg;
  args_value_iterator_t *value_it = NULL, *merge_value_it = NULL;
  const char **current_hierarchy_name_ptr;
  grm_args_t **args_array, **merge_args_array, *current_args;
  unsigned int i;
  err_t error = ERROR_NONE;

  ++recursion_level;
  if (hierarchy_name_ptr == NULL)
    {
      hierarchy_name_ptr = plot_hierarchy_names;
    }
  if (hierarchy_to_id == NULL)
    {
      hierarchy_to_id = uint_map_new(array_size(plot_hierarchy_names));
      cleanup_and_set_error_if(hierarchy_to_id == NULL, ERROR_MALLOC);
    }
  else
    {
      hierarchy_to_id = uint_map_copy(hierarchy_to_id);
      cleanup_and_set_error_if(hierarchy_to_id == NULL, ERROR_MALLOC);
    }
  if (key_to_cleared_args == NULL)
    {
      key_to_cleared_args = args_set_map_new(array_size(plot_merge_clear_keys));
      cleanup_and_set_error_if(hierarchy_to_id == NULL, ERROR_MALLOC);
    }
  grm_args_values(global_root_args, "append_plots", "i", &append_plots);
  get_id_from_args(merge_args, &plot_id, &subplot_id, &series_id);
  if (plot_id > 0)
    {
      uint_map_insert(hierarchy_to_id, "plots", plot_id);
    }
  else
    {
      uint_map_insert_default(hierarchy_to_id, "plots", append_plots ? 0 : active_plot_index);
      uint_map_at(hierarchy_to_id, "plots", (unsigned int *)&plot_id);
      logger((stderr, "Using plot_id \"%u\"\n", plot_id));
    }
  if (subplot_id > 0)
    {
      uint_map_insert(hierarchy_to_id, "subplots", subplot_id);
    }
  else
    {
      uint_map_insert_default(hierarchy_to_id, "subplots", 1);
      uint_map_at(hierarchy_to_id, "subplots", (unsigned int *)&subplot_id);
    }
  if (series_id > 0)
    {
      uint_map_insert(hierarchy_to_id, "series", series_id);
    }
  else
    {
      uint_map_insert_default(hierarchy_to_id, "series", 1);
      uint_map_at(hierarchy_to_id, "series", (unsigned int *)&series_id);
    }
  /* special case: clear the plot container before usage if
   * - it is the first call of `plot_merge_args` AND `hold_always` is `0` AND
   *   - `plot_id` is `1` and `hold_plots` is not set OR
   *   - `hold_plots` is true and no plot will be appended (`plot_id` > 0)
   */
  if (strcmp(*hierarchy_name_ptr, "root") == 0 && plot_id > 0 && !hold_always)
    {
      int hold_plots_key_available, hold_plots;
      hold_plots_key_available = grm_args_values(args, "hold_plots", "i", &hold_plots);
      if (hold_plots_key_available)
        {
          logger((stderr, "Do%s hold plots\n", hold_plots ? "" : " not"));
        }
      if ((hold_plots_key_available && !hold_plots) || (!hold_plots_key_available && plot_id == 1))
        {
          cleanup_and_set_error_if(!grm_args_values(args, "plots", "A", &args_array), ERROR_INTERNAL);
          current_args = args_array[plot_id - 1];
          grm_args_clear(current_args);
          error = plot_init_args_structure(current_args, hierarchy_name_ptr + 1, 1);
          cleanup_if_error;
          logger((stderr, "Cleared current args\n"));
        }
      else
        {
          logger((stderr, "Held current args\n"));
        }
    }
#ifndef NDEBUG
  if (strcmp(*hierarchy_name_ptr, "root") == 0 && hold_always)
    {
      logger((stderr, "\"hold_always\" is set\n"));
    }
#endif /* ifndef  */
  merge_it = args_iter(merge_args);
  cleanup_and_set_error_if(merge_it == NULL, ERROR_MALLOC);
  while ((merge_arg = merge_it->next(merge_it)) != NULL)
    {
      if (str_equals_any_in_array(merge_arg->key, plot_merge_ignore_keys))
        {
          continue;
        }
      /* First, find the correct hierarchy level where the current merge value belongs to. */
      error = plot_get_args_in_hierarchy(args, hierarchy_name_ptr, merge_arg->key, hierarchy_to_id,
                                         (const grm_args_t **)&current_args, &current_hierarchy_name_ptr);
      if (error == ERROR_PLOT_UNKNOWN_KEY)
        {
          logger((stderr, "WARNING: The key \"%s\" is not assigned to any hierarchy level.\n", merge_arg->key));
        }
      cleanup_if_error;
      if (str_equals_any_in_array(*current_hierarchy_name_ptr, plot_merge_clear_keys))
        {
          int clear_args = 1;
          args_set_t *cleared_args = NULL;
          if (args_set_map_at(key_to_cleared_args, *current_hierarchy_name_ptr, &cleared_args))
            {
              clear_args = !args_set_contains(cleared_args, current_args);
            }
          if (clear_args)
            {
              logger((stderr, "Perform a clear on the current args container\n"));
              grm_args_clear(current_args);
              if (cleared_args == NULL)
                {
                  cleared_args = args_set_new(10); /* FIXME: do not use a magic number, use a growbable set instead! */
                  cleanup_and_set_error_if(cleared_args == NULL, ERROR_MALLOC);
                  cleanup_and_set_error_if(
                      !args_set_map_insert(key_to_cleared_args, *current_hierarchy_name_ptr, cleared_args),
                      ERROR_INTERNAL);
                }
              logger((stderr, "Add args container \"%p\" to cleared args with key \"%s\"\n", (void *)current_args,
                      *current_hierarchy_name_ptr));
              cleanup_and_set_error_if(!args_set_add(cleared_args, current_args), ERROR_INTERNAL);
            }
        }
      /* If the current key is a hierarchy key, perform a merge. Otherwise (else branch) put the value in without a
       * merge.
       */
      if (current_hierarchy_name_ptr[1] != NULL && strcmp(merge_arg->key, current_hierarchy_name_ptr[1]) == 0)
        {
          /* `args_at` cannot fail in this case because the `args` object was initialized with an empty structure
           * before. If `arg` is NULL, an internal error occurred. */
          arg = args_at(current_args, merge_arg->key);
          cleanup_and_set_error_if(arg == NULL, ERROR_INTERNAL);
          value_it = arg_value_iter(arg);
          merge_value_it = arg_value_iter(merge_arg);
          cleanup_and_set_error_if(value_it == NULL, ERROR_MALLOC);
          cleanup_and_set_error_if(merge_value_it == NULL, ERROR_MALLOC);
          /* Do not support two dimensional argument arrays like `nAnA`) -> a loop would be needed with more memory
           * management */
          cleanup_and_set_error_if(value_it->next(value_it) == NULL, ERROR_MALLOC);
          cleanup_and_set_error_if(merge_value_it->next(merge_value_it) == NULL, ERROR_MALLOC);
          /* Increase the array size of the internal args array if necessary */
          if (merge_value_it->array_length > value_it->array_length)
            {
              error = plot_init_arg_structure(arg, current_hierarchy_name_ptr, merge_value_it->array_length);
              cleanup_if_error;
              args_value_iterator_delete(value_it);
              value_it = arg_value_iter(arg);
              cleanup_and_set_error_if(value_it == NULL, ERROR_MALLOC);
              cleanup_and_set_error_if(value_it->next(value_it) == NULL, ERROR_MALLOC);
              args_array = *(grm_args_t ***)value_it->value_ptr;
            }
          else
            {
              /* The internal args container stores always an array for hierarchy levels */
              args_array = *(grm_args_t ***)value_it->value_ptr;
            }
          if (merge_value_it->is_array)
            {
              merge_args_array = *(grm_args_t ***)merge_value_it->value_ptr;
            }
          else
            {
              merge_args_array = (grm_args_t **)merge_value_it->value_ptr;
            }
          /* Merge array entries pairwise */
          for (i = 0; i < merge_value_it->array_length; ++i)
            {
              logger((stderr, "Perform a recursive merge on key \"%s\", array index \"%d\"\n", merge_arg->key, i));
              error = plot_merge_args(args_array[i], merge_args_array[i], current_hierarchy_name_ptr + 1,
                                      hierarchy_to_id, hold_always);
              cleanup_if_error;
            }
        }
      else
        {
          const char *compatible_format;
          /* Only accept the new value, if it has a compatible type. Convert if possible */
          if ((compatible_format = get_compatible_format(merge_arg->key, merge_arg->value_format)) != NULL)
            {
              logger((stderr, "Perform a replace on key \"%s\"\n", merge_arg->key));
              if (strlen(merge_arg->value_format) == 1 &&
                  tolower(*merge_arg->value_format) == *merge_arg->value_format &&
                  toupper(*compatible_format) == *compatible_format)
                {
                  /* A single value is given but an array is needed
                   * -> Push the given value as array instead
                   * **IMPORTANT**: In this case the existing `merge_arg` node cannot be shared with `current_args`
                   * (since the stored format does not fit), so the value must be copied (it is owned by the `merge_arg`
                   * node and would be freed twice otherwise). */
                  char array_format[3] = "n";
                  void *copy_buffer;
                  array_format[1] = *compatible_format;
                  logger((stderr, "Convert the given format \"%s\" to an array format \"%s\" \n",
                          merge_arg->value_format, array_format));
                  copy_buffer = copy_value(tolower(*compatible_format), merge_arg->value_ptr);
                  cleanup_and_set_error_if(copy_buffer == NULL, ERROR_MALLOC);
                  grm_args_push(current_args, merge_arg->key, array_format, 1, copy_buffer);
                  /* x -> copy_buffer -> value, value is now stored and the buffer is not needed any more */
                  free(copy_buffer);
                }
              else
                {
                  /* Otherwise, push without conversion (needed conversion is done later when extracting values with
                   * functions like `grm_args_values`) */
                  error = args_push_arg(current_args, merge_arg);
                  cleanup_if_error;
                }
            }
          else
            {
              logger((stderr, "The type \"%s\" of key \"%s\" was rejected and will not be merged.\n",
                      merge_arg->value_format, merge_arg->key));
            }
        }
    }

cleanup:
  if (recursion_level == 0)
    {
      if (key_to_cleared_args != NULL)
        {
          args_set_t *cleared_args = NULL;
          const char **current_key_ptr = plot_merge_clear_keys;
          while (*current_key_ptr != NULL)
            {
              if (args_set_map_at(key_to_cleared_args, *current_key_ptr, &cleared_args))
                {
                  args_set_delete(cleared_args);
                }
              ++current_key_ptr;
            }
          args_set_map_delete(key_to_cleared_args);
          key_to_cleared_args = NULL;
        }
    }
  if (hierarchy_to_id != NULL)
    {
      uint_map_delete(hierarchy_to_id);
      hierarchy_to_id = NULL;
    }
  if (merge_it != NULL)
    {
      args_iterator_delete(merge_it);
    }
  if (value_it != NULL)
    {
      args_value_iterator_delete(value_it);
    }
  if (merge_value_it != NULL)
    {
      args_value_iterator_delete(merge_value_it);
    }

  --recursion_level;

  return error;
}

err_t plot_init_arg_structure(arg_t *arg, const char **hierarchy_name_ptr, unsigned int next_hierarchy_level_max_id)
{
  grm_args_t **args_array = NULL;
  unsigned int args_old_array_length;
  unsigned int i;
  err_t error = ERROR_NONE;

  logger((stderr, "Init plot args structure for hierarchy: \"%s\"\n", *hierarchy_name_ptr));

  ++hierarchy_name_ptr;
  if (*hierarchy_name_ptr == NULL)
    {
      return ERROR_NONE;
    }
  arg_first_value(arg, "A", NULL, &args_old_array_length);
  if (next_hierarchy_level_max_id <= args_old_array_length)
    {
      return ERROR_NONE;
    }
  logger((stderr, "Increase array for key \"%s\" from %d to %d\n", *hierarchy_name_ptr, args_old_array_length,
          next_hierarchy_level_max_id));
  error = arg_increase_array(arg, next_hierarchy_level_max_id - args_old_array_length);
  return_if_error;
  arg_values(arg, "A", &args_array);
  for (i = args_old_array_length; i < next_hierarchy_level_max_id; ++i)
    {
      args_array[i] = grm_args_new();
      grm_args_push(args_array[i], "array_index", "i", i);
      return_error_if(args_array[i] == NULL, ERROR_MALLOC);
      error = plot_init_args_structure(args_array[i], hierarchy_name_ptr, 1);
      return_if_error;
      if (strcmp(*hierarchy_name_ptr, "plots") == 0)
        {
          grm_args_push(args_array[i], "in_use", "i", 0);
        }
    }

  return ERROR_NONE;
}

err_t plot_init_args_structure(grm_args_t *args, const char **hierarchy_name_ptr,
                               unsigned int next_hierarchy_level_max_id)
{
  arg_t *arg = NULL;
  grm_args_t **args_array = NULL;
  unsigned int i;
  err_t error = ERROR_NONE;

  logger((stderr, "Init plot args structure for hierarchy: \"%s\"\n", *hierarchy_name_ptr));

  ++hierarchy_name_ptr;
  if (*hierarchy_name_ptr == NULL)
    {
      return ERROR_NONE;
    }
  arg = args_at(args, *hierarchy_name_ptr);
  if (arg == NULL)
    {
      args_array = static_cast<grm_args_t **>(calloc(next_hierarchy_level_max_id, sizeof(grm_args_t *)));
      error_cleanup_and_set_error_if(args_array == NULL, ERROR_MALLOC);
      for (i = 0; i < next_hierarchy_level_max_id; ++i)
        {
          args_array[i] = grm_args_new();
          grm_args_push(args_array[i], "array_index", "i", i);
          error_cleanup_and_set_error_if(args_array[i] == NULL, ERROR_MALLOC);
          error = plot_init_args_structure(args_array[i], hierarchy_name_ptr, 1);
          error_cleanup_if_error;
          if (strcmp(*hierarchy_name_ptr, "plots") == 0)
            {
              grm_args_push(args_array[i], "in_use", "i", 0);
            }
        }
      error_cleanup_if(!grm_args_push(args, *hierarchy_name_ptr, "nA", next_hierarchy_level_max_id, args_array));
      free(args_array);
      args_array = NULL;
    }
  else
    {
      error = plot_init_arg_structure(arg, hierarchy_name_ptr - 1, next_hierarchy_level_max_id);
      error_cleanup_if_error;
    }

  return ERROR_NONE;

error_cleanup:
  if (args_array != NULL)
    {
      for (i = 0; i < next_hierarchy_level_max_id; ++i)
        {
          if (args_array[i] != NULL)
            {
              grm_args_delete(args_array[i]);
            }
        }
      free(args_array);
    }

  return error;
}

int plot_check_for_request(const grm_args_t *args, err_t *error)
{
  const char *request;
  int is_request = 0;

  *error = ERROR_NONE;
  if (grm_args_values(args, "request", "s", &request))
    {
      is_request = 1;
      *error = event_queue_enqueue_request_event(event_queue, request);
    }
  else
    {
      *error = ERROR_PLOT_INVALID_REQUEST;
    }

  return is_request;
}

void plot_set_flag_defaults(void)
{
  /* Use a standalone function for initializing flags instead of `plot_set_attribute_defaults` to guarantee the flags
   * are already set before `grm_plot` is called (important for `grm_merge`) */

  logger((stderr, "Set global flag defaults\n"));

  args_setdefault(global_root_args, "append_plots", "i", ROOT_DEFAULT_APPEND_PLOTS);
}

void plot_set_attribute_defaults(grm_args_t *plot_args)
{
  const char *kind;
  grm_args_t **current_subplot, **current_series;
  double garbage0, garbage1;

  logger((stderr, "Set plot attribute defaults\n"));

  args_setdefault(plot_args, "clear", "i", PLOT_DEFAULT_CLEAR);
  args_setdefault(plot_args, "update", "i", PLOT_DEFAULT_UPDATE);
  if (!grm_args_contains(plot_args, "figsize"))
    {
      args_setdefault(plot_args, "size", "dd", PLOT_DEFAULT_WIDTH, PLOT_DEFAULT_HEIGHT);
    }

  grm_args_values(plot_args, "subplots", "A", &current_subplot);
  while (*current_subplot != NULL)
    {
      args_setdefault(*current_subplot, "kind", "s", PLOT_DEFAULT_KIND);
      grm_args_values(*current_subplot, "kind", "s", &kind);
      if (grm_args_contains(*current_subplot, "labels"))
        {
          args_setdefault(*current_subplot, "location", "i", PLOT_DEFAULT_LOCATION);
        }
      args_setdefault(*current_subplot, "subplot", "dddd", PLOT_DEFAULT_SUBPLOT_MIN_X, PLOT_DEFAULT_SUBPLOT_MAX_X,
                      PLOT_DEFAULT_SUBPLOT_MIN_Y, PLOT_DEFAULT_SUBPLOT_MAX_Y);
      args_setdefault(*current_subplot, "xlog", "i", PLOT_DEFAULT_XLOG);
      args_setdefault(*current_subplot, "ylog", "i", PLOT_DEFAULT_YLOG);
      args_setdefault(*current_subplot, "zlog", "i", PLOT_DEFAULT_ZLOG);
      args_setdefault(*current_subplot, "xflip", "i", PLOT_DEFAULT_XFLIP);
      args_setdefault(*current_subplot, "yflip", "i", PLOT_DEFAULT_YFLIP);
      args_setdefault(*current_subplot, "zflip", "i", PLOT_DEFAULT_ZFLIP);
      args_setdefault(*current_subplot, "xgrid", "i", PLOT_DEFAULT_XGRID);
      args_setdefault(*current_subplot, "ygrid", "i", PLOT_DEFAULT_YGRID);
      args_setdefault(*current_subplot, "zgrid", "i", PLOT_DEFAULT_ZGRID);
      args_setdefault(*current_subplot, "resample_method", "i", PLOT_DEFAULT_RESAMPLE_METHOD);
      if (strcmp(kind, "heatmap") == 0)
        {
          args_setdefault(*current_subplot, "adjust_xlim", "i", 0);
          args_setdefault(*current_subplot, "adjust_ylim", "i", 0);
        }
      else
        {
          args_setdefault(
              *current_subplot, "adjust_xlim", "i",
              (grm_args_values(*current_subplot, "xlim", "dd", &garbage0, &garbage1) ? 0 : PLOT_DEFAULT_ADJUST_XLIM));
          args_setdefault(
              *current_subplot, "adjust_ylim", "i",
              (grm_args_values(*current_subplot, "ylim", "dd", &garbage0, &garbage1) ? 0 : PLOT_DEFAULT_ADJUST_YLIM));
          args_setdefault(
              *current_subplot, "adjust_zlim", "i",
              (grm_args_values(*current_subplot, "zlim", "dd", &garbage0, &garbage1) ? 0 : PLOT_DEFAULT_ADJUST_ZLIM));
        }
      args_setdefault(*current_subplot, "colormap", "i", PLOT_DEFAULT_COLORMAP);
      args_setdefault(*current_subplot, "font", "i", PLOT_DEFAULT_FONT);
      args_setdefault(*current_subplot, "font_precision", "i", PLOT_DEFAULT_FONT_PRECISION);
      args_setdefault(*current_subplot, "rotation", "d", PLOT_DEFAULT_ROTATION);
      args_setdefault(*current_subplot, "tilt", "d", PLOT_DEFAULT_TILT);
      args_setdefault(*current_subplot, "keep_aspect_ratio", "i", PLOT_DEFAULT_KEEP_ASPECT_RATIO);

      if (str_equals_any(kind, 2, "contour", "contourf"))
        {
          args_setdefault(*current_subplot, "levels", "i", PLOT_DEFAULT_CONTOUR_LEVELS);
        }
      else if (strcmp(kind, "tricont") == 0)
        {
          args_setdefault(*current_subplot, "levels", "i", PLOT_DEFAULT_TRICONT_LEVELS);
        }

      grm_args_values(*current_subplot, "series", "A", &current_series);
      while (*current_series != NULL)
        {
          args_setdefault(*current_series, "spec", "s", SERIES_DEFAULT_SPEC);
          if (strcmp(kind, "step") == 0)
            {
              args_setdefault(*current_series, "step_where", "s", PLOT_DEFAULT_STEP_WHERE);
            }
          else if (strcmp(kind, "hexbin") == 0)
            {
              args_setdefault(*current_series, "nbins", "i", PLOT_DEFAULT_HEXBIN_NBINS);
            }
          else if (strcmp(kind, "volume") == 0)
            {
              args_setdefault(*current_series, "algorithm", "i", PLOT_DEFAULT_VOLUME_ALGORITHM);
            }
          ++current_series;
        }
      ++current_subplot;
    }
}

void plot_pre_plot(grm_args_t *plot_args)
{
  int clear;

  logger((stderr, "Pre plot processing\n"));

  plot_set_text_encoding();
  grm_args_values(plot_args, "clear", "i", &clear);
  logger((stderr, "Got keyword \"clear\" with value %d\n", clear));
  if (clear)
    {
      global_root->append(global_render->createClearWS());
    }
  plot_process_wswindow_wsviewport(plot_args);
}

void plot_set_text_encoding(void)
{
  //  gr_inqtextencoding(&pre_plot_text_encoding);
  global_render->setTextEncoding(global_root, ENCODING_UTF8);
}

void plot_process_wswindow_wsviewport(grm_args_t *plot_args)
{
  int pixel_width, pixel_height;
  int previous_pixel_width, previous_pixel_height;
  double metric_width, metric_height;
  double aspect_ratio_ws_pixel, aspect_ratio_ws_metric;
  double wsviewport[4] = {0.0, 0.0, 0.0, 0.0};
  double wswindow[4] = {0.0, 0.0, 0.0, 0.0};

  get_figure_size(plot_args, &pixel_width, &pixel_height, &metric_width, &metric_height);

  if (!grm_args_values(plot_args, "previous_pixel_size", "ii", &previous_pixel_width, &previous_pixel_height) ||
      (previous_pixel_width != pixel_width || previous_pixel_height != pixel_height))
    {
      /* TODO: handle error return value? */
      event_queue_enqueue_size_event(event_queue, active_plot_index - 1, pixel_width, pixel_height);
    }

  aspect_ratio_ws_pixel = (double)pixel_width / pixel_height;
  aspect_ratio_ws_metric = metric_width / metric_height;
  if (aspect_ratio_ws_pixel > 1)
    {
      wsviewport[1] = metric_width;
      wsviewport[3] = metric_width / aspect_ratio_ws_metric;
      wswindow[1] = 1.0;
      wswindow[3] = 1.0 / (aspect_ratio_ws_pixel);
    }
  else
    {
      wsviewport[1] = metric_height * aspect_ratio_ws_metric;
      wsviewport[3] = metric_height;
      wswindow[1] = aspect_ratio_ws_pixel;
      wswindow[3] = 1.0;
    }

  gr_setwsviewport(wsviewport[0], wsviewport[1], wsviewport[2], wsviewport[3]);
  gr_setwswindow(wswindow[0], wswindow[1], wswindow[2], wswindow[3]);

  grm_args_push(plot_args, "wswindow", "dddd", wswindow[0], wswindow[1], wswindow[2], wswindow[3]);
  grm_args_push(plot_args, "wsviewport", "dddd", wsviewport[0], wsviewport[1], wsviewport[2], wsviewport[3]);
  grm_args_push(plot_args, "previous_pixel_size", "ii", pixel_width, pixel_height);

  logger((stderr, "Stored wswindow (%lf, %lf, %lf, %lf)\n", wswindow[0], wswindow[1], wswindow[2], wswindow[3]));
  logger(
      (stderr, "Stored wsviewport (%lf, %lf, %lf, %lf)\n", wsviewport[0], wsviewport[1], wsviewport[2], wsviewport[3]));
}

err_t plot_pre_subplot(grm_args_t *subplot_args)
{
  const char *kind;
  double alpha;
  err_t error = ERROR_NONE;

  logger((stderr, "Pre subplot processing\n"));

  grm_args_values(subplot_args, "kind", "s", &kind);
  logger((stderr, "Got keyword \"kind\" with value \"%s\"\n", kind));
  //  plot_process_viewport(subplot_args);
  error = plot_store_coordinate_ranges(subplot_args);
  return_if_error;
  plot_process_window(subplot_args);

  plot_process_colormap(subplot_args);
  plot_process_font(subplot_args);
  plot_process_resample_method(subplot_args);

  if (str_equals_any(kind, 2, "polar", "polar_histogram"))
    {
      plot_draw_polar_axes(subplot_args);
    }
  else if (!str_equals_any(kind, 3, "imshow", "isosurface", "pie"))
    {
      plot_draw_axes(subplot_args, 1);
    }

  gr_uselinespec(const_cast<char *>(" "));

  gr_savestate();
  if (grm_args_values(subplot_args, "alpha", "d", &alpha))
    {
      gr_settransparency(alpha);
    }

  return ERROR_NONE;
}

void plot_process_colormap(grm_args_t *subplot_args)
{
  int colormap;

  if (grm_args_values(subplot_args, "colormap", "i", &colormap))
    {
      gr_setcolormap(colormap);
    }
  /* TODO: Implement other datatypes for `colormap` */
}

void plot_process_font(grm_args_t *subplot_args)
{
  int font, font_precision;

  /* `font` and `font_precision` are always set */
  if (grm_args_values(subplot_args, "font", "i", &font) &&
      grm_args_values(subplot_args, "font_precision", "i", &font_precision))
    {
      logger((stderr, "Using font: %d with precision %d\n", font, font_precision));
      gr_settextfontprec(font, font_precision);
    }
  /* TODO: Implement other datatypes for `font` and `font_precision` */
}

err_t plot_process_grid_arguments(const grm_args_t *args)
{
  int current_nesting_degree, nesting_degree;
  int *rows, *cols;
  unsigned int rows_length, cols_length;
  int rowspan, colspan;
  int *rowspans, *colspans;
  unsigned int rowspans_length, colspans_length;
  int rowstart, rowstop, colstart, colstop;
  grm_args_t **current_subplot_args;
  grid_t *current_grid;
  element_t *current_element;

  double *rel_heights, *rel_widths, *abs_heights, *abs_widths, *aspect_ratios;
  int *fit_parents_heights, *fit_parents_widths;
  unsigned int rel_heights_length, rel_widths_length, abs_heights_length, abs_widths_length, aspect_ratios_length,
      fit_parents_heights_length, fit_parents_widths_length;
  err_t error = ERROR_NONE;

  if (global_grid != NULL)
    {
      grid_delete(global_grid);
    }
  error = grid_new(1, 1, &global_grid);
  return_if_error;
  grm_args_values(active_plot_args, "subplots", "A", &current_subplot_args);
  while (*current_subplot_args != NULL)
    {
      rows = NULL, cols = NULL;
      rowspans = NULL, colspans = NULL;
      rowspan = 1, colspan = 1;
      rel_heights = NULL, rel_widths = NULL;
      abs_heights = NULL, abs_widths = NULL;
      aspect_ratios = NULL;
      fit_parents_heights = NULL, fit_parents_widths = NULL;

      grm_args_first_value(*current_subplot_args, "row", "I", &rows, &rows_length);
      grm_args_first_value(*current_subplot_args, "col", "I", &cols, &cols_length);

      if (rows == NULL || cols == NULL)
        {
          rows_length = 0;
          cols_length = 0;
        }

      if (rows_length != cols_length)
        {
          return ERROR_LAYOUT_COMPONENT_LENGTH_MISMATCH;
        }

      grm_args_first_value(*current_subplot_args, "rowspan", "I", &rowspans, &rowspans_length);
      grm_args_first_value(*current_subplot_args, "colspan", "I", &colspans, &colspans_length);

      if (rowspans == NULL)
        {
          rowspans = &rowspan;
          rowspans_length = 1;
        }
      if (colspans == NULL)
        {
          colspans = &colspan;
          colspans_length = 1;
        }

      grm_args_first_value(*current_subplot_args, "rel_height", "D", &rel_heights, &rel_heights_length);
      grm_args_first_value(*current_subplot_args, "rel_width", "D", &rel_widths, &rel_widths_length);
      grm_args_first_value(*current_subplot_args, "abs_height", "D", &abs_heights, &abs_heights_length);
      grm_args_first_value(*current_subplot_args, "abs_width", "D", &abs_widths, &abs_widths_length);
      grm_args_first_value(*current_subplot_args, "aspect_ratio", "D", &aspect_ratios, &aspect_ratios_length);
      grm_args_first_value(*current_subplot_args, "fit_parents_height", "I", &fit_parents_heights,
                           &fit_parents_heights_length);
      grm_args_first_value(*current_subplot_args, "fit_parents_width", "I", &fit_parents_widths,
                           &fit_parents_widths_length);

      nesting_degree = rows_length - 1;
      current_grid = global_grid;
      for (current_nesting_degree = 0; current_nesting_degree <= nesting_degree; ++current_nesting_degree)
        {

          rowstart = rows[current_nesting_degree];
          rowstop =
              (current_nesting_degree >= rowspans_length) ? rowstart + 1 : rowstart + rowspans[current_nesting_degree];
          colstart = cols[current_nesting_degree];
          colstop =
              (current_nesting_degree >= colspans_length) ? colstart + 1 : colstart + colspans[current_nesting_degree];

          if (rowstart - rowstop == 0 || colstart - colstop == 0)
            {
              break;
            }

          if (nesting_degree == current_nesting_degree)
            {
              error =
                  grid_setElementArgsSlice(rowstart, rowstop, colstart, colstop, *current_subplot_args, current_grid);
              return_if_error;
              error = grid_getElement(rowstart, colstart, current_grid, &current_element);
              return_if_error;
            }
          else
            {
              error = grid_ensureCellsAreGrid(rowstart, rowstop, colstart, colstop, current_grid);
              return_if_error;
              error = grid_getElement(rowstart, colstart, current_grid, (element_t **)&current_grid);
              return_if_error;
              current_element = (element_t *)current_grid;
            }

          if (rel_heights != NULL && rel_heights_length > current_nesting_degree &&
              rel_heights[current_nesting_degree] != -1)
            {
              error = element_setRelativeHeight(current_element, rel_heights[current_nesting_degree]);
            }
          if (rel_widths != NULL && rel_widths_length > current_nesting_degree &&
              rel_widths[current_nesting_degree] != -1)
            {
              error = element_setRelativeWidth(current_element, rel_widths[current_nesting_degree]);
            }
          if (abs_heights != NULL && abs_heights_length > current_nesting_degree &&
              abs_heights[current_nesting_degree] != -1)
            {
              error = element_setAbsHeight(current_element, abs_heights[current_nesting_degree]);
            }
          if (abs_widths != NULL && abs_widths_length > current_nesting_degree &&
              abs_widths[current_nesting_degree] != -1)
            {
              error = element_setAbsWidth(current_element, abs_widths[current_nesting_degree]);
            }
          if (aspect_ratios != NULL && aspect_ratios_length > current_nesting_degree &&
              aspect_ratios[current_nesting_degree] != -1)
            {
              error = element_setAspectRatio(current_element, aspect_ratios[current_nesting_degree]);
            }
          if (fit_parents_heights != NULL && fit_parents_heights_length > current_nesting_degree &&
              fit_parents_heights[current_nesting_degree] != -1)
            {
              element_setFitParentsHeight(current_element, fit_parents_heights[current_nesting_degree]);
            }
          if (fit_parents_widths != NULL && fit_parents_widths_length > current_nesting_degree &&
              fit_parents_widths[current_nesting_degree] != -1)
            {
              element_setFitParentsWidth(current_element, fit_parents_widths[current_nesting_degree]);
            }
          return_if_error;
        }

      ++current_subplot_args;
    }

  return ERROR_NONE;
}

void plot_process_resample_method(grm_args_t *subplot_args)
{
  unsigned int resample_method_flag;
  if (!grm_args_values(subplot_args, "resample_method", "i", &resample_method_flag))
    {
      const char *resample_method_str;
      grm_args_values(subplot_args, "resample_method", "s", &resample_method_str);
      if (strcmp(resample_method_str, "nearest") == 0)
        {
          resample_method_flag = GKS_K_RESAMPLE_NEAREST;
        }
      else if (strcmp(resample_method_str, "linear") == 0)
        {
          resample_method_flag = GKS_K_RESAMPLE_LINEAR;
        }
      else if (strcmp(resample_method_str, "lanczos") == 0)
        {
          resample_method_flag = GKS_K_RESAMPLE_LANCZOS;
        }
      else
        {
          resample_method_flag = GKS_K_RESAMPLE_DEFAULT;
        }
    }
  gr_setresamplemethod(resample_method_flag);
}

static void legend_size(grm_args_t *subplot_args, double *w, double *h)
{
  double tbx[4], tby[4];
  const char **labels, **current_label;
  unsigned int num_labels;

  *w = 0;
  *h = 0;
  if (grm_args_first_value(subplot_args, "labels", "S", &labels, &num_labels))
    {
      for (current_label = labels; *current_label != NULL; ++current_label)
        {
          gr_inqtext(0, 0, *(char **)current_label, tbx, tby);
          *w = grm_max(*w, tbx[2] - tbx[0]);
          *h += grm_max(tby[2] - tby[0], 0.03);
        }
    }
}

void plot_process_viewport(grm_args_t *subplot_args)
{
  //! TODO: Make dom compatible
  const char *kind;
  const double *subplot;
  int keep_aspect_ratio;
  int pixel_width, pixel_height;
  double aspect_ratio_ws;
  double vp[4];
  double vp0, vp1, vp2, vp3;
  double left_margin, right_margin, bottom_margin, top_margin;
  char *x_label, *y_label, *title;
  double viewport[4] = {0.0, 0.0, 0.0, 0.0};
  int background_color_index;

  grm_args_values(subplot_args, "kind", "s", &kind);
  grm_args_values(subplot_args, "subplot", "D", &subplot);
  grm_args_values(subplot_args, "keep_aspect_ratio", "i", &keep_aspect_ratio);
  logger((stderr, "Using subplot: %lf, %lf, %lf, %lf\n", subplot[0], subplot[1], subplot[2], subplot[3]));

  get_figure_size(NULL, &pixel_width, &pixel_height, NULL, NULL);

  aspect_ratio_ws = (double)pixel_width / pixel_height;
  memcpy(vp, subplot, sizeof(vp));
  if (aspect_ratio_ws > 1)
    {
      vp[2] /= aspect_ratio_ws;
      vp[3] /= aspect_ratio_ws;
      if (keep_aspect_ratio)
        {
          double border = 0.5 * (vp[1] - vp[0]) * (1.0 - 1.0 / aspect_ratio_ws);
          vp[0] += border;
          vp[1] -= border;
        }
    }
  else
    {
      vp[0] *= aspect_ratio_ws;
      vp[1] *= aspect_ratio_ws;
      if (keep_aspect_ratio)
        {
          double border = 0.5 * (vp[3] - vp[2]) * (1.0 - aspect_ratio_ws);
          vp[2] += border;
          vp[3] -= border;
        }
    }

  if (str_equals_any(kind, 6, "wireframe", "surface", "plot3", "scatter3", "trisurf", "volume"))
    {
      double extent;

      extent = grm_min(vp[1] - vp[0], vp[3] - vp[2]);
      vp0 = 0.5 * (vp[0] + vp[1] - extent);
      vp1 = 0.5 * (vp[0] + vp[1] + extent);
      vp2 = 0.5 * (vp[2] + vp[3] - extent);
      vp3 = 0.5 * (vp[2] + vp[3] + extent);
    }
  else
    {
      vp0 = vp[0];
      vp1 = vp[1];
      vp2 = vp[2];
      vp3 = vp[3];
    }

  left_margin = grm_args_values(subplot_args, "ylabel", "s", &y_label) ? 0.05 : 0;
  if (str_equals_any(kind, 8, "contour", "contourf", "hexbin", "heatmap", "nonuniformheatmap", "surface", "trisurf",
                     "volume"))
    {
      right_margin = (vp1 - vp0) * 0.1;
    }
  else
    {
      right_margin = 0;
    }
  bottom_margin = grm_args_values(subplot_args, "xlabel", "s", &x_label) ? 0.05 : 0;
  top_margin = grm_args_values(subplot_args, "title", "s", &title) ? 0.075 : 0;

  viewport[0] = vp0 + (0.075 + left_margin) * (vp1 - vp0);
  viewport[1] = vp0 + (0.95 - right_margin) * (vp1 - vp0);
  viewport[2] = vp2 + (0.075 + bottom_margin) * (vp3 - vp2);
  viewport[3] = vp2 + (0.975 - top_margin) * (vp3 - vp2);

  if (str_equals_any(kind, 4, "line", "stairs", "scatter", "stem"))
    {
      int location;
      double w, h;

      if (grm_args_values(subplot_args, "location", "i", &location))
        {
          if (location == 11 || location == 12 || location == 13)
            {
              legend_size(subplot_args, &w, &h);
              viewport[1] -= w + 0.1;
            }
        }
    }

  if (grm_args_values(subplot_args, "backgroundcolor", "i", &background_color_index))
    {
      gr_savestate();
      gr_selntran(0);
      gr_setfillintstyle(GKS_K_INTSTYLE_SOLID);
      gr_setfillcolorind(background_color_index);
      if (aspect_ratio_ws > 1)
        {
          gr_fillrect(subplot[0], subplot[1], subplot[2] / aspect_ratio_ws, subplot[3] / aspect_ratio_ws);
        }
      else
        {
          gr_fillrect(subplot[0] * aspect_ratio_ws, subplot[1] * aspect_ratio_ws, subplot[2], subplot[3]);
        }
      gr_selntran(1);
      gr_restorestate();
    }

  if (str_equals_any(kind, 3, "pie", "polar", "polar_histogram"))
    {
      double x_center, y_center, r;

      x_center = 0.5 * (viewport[0] + viewport[1]);
      y_center = 0.5 * (viewport[2] + viewport[3]);
      r = 0.45 * grm_min(viewport[1] - viewport[0], viewport[3] - viewport[2]);
      if (grm_args_contains(subplot_args, "title"))
        {
          r *= 0.975;
          y_center -= 0.025 * r;
        }
      viewport[0] = x_center - r;
      viewport[1] = x_center + r;
      viewport[2] = y_center - r;
      viewport[3] = y_center + r;
    }

  //  if (!currentDomElement)
  //    {
  //      global_render->setViewport(global_root->lastChildElement(), viewport[0], viewport[1], viewport[2],
  //      viewport[3]);
  //    }
  //  else
  //    {
  //      global_render->setViewport(currentDomElement, viewport[0], viewport[1], viewport[2], viewport[3]);
  //    }
  //  gr_setviewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  //
  //  grm_args_push(subplot_args, "vp", "dddd", vp[0], vp[1], vp[2], vp[3]);
  //  grm_args_push(subplot_args, "viewport", "dddd", viewport[0], viewport[1], viewport[2], viewport[3]);
  //
  //  logger((stderr, "Stored vp (%lf, %lf, %lf, %lf)\n", vp[0], vp[1], vp[2], vp[3]));
  //  logger((stderr, "Stored viewport (%lf, %lf, %lf, %lf)\n", viewport[0], viewport[1], viewport[2], viewport[3]));
}

double auto_tick(double amin, double amax)
{
  double tick_size[] = {5.0, 2.0, 1.0, 0.5, 0.2, 0.1, 0.05, 0.02, 0.01};
  double scale, tick;
  int i, n;

  scale = pow(10.0, (int)(log10(amax - amin)));
  tick = 1.0;
  for (i = 0; i < 9; i++)
    {
      n = (amax - amin) / scale / tick_size[i];
      if (n > 7)
        {
          tick = tick_size[i - 1];
          break;
        }
    }
  tick *= scale;
  return tick;
}

void plot_process_window(grm_args_t *subplot_args)
{
  int scale = 0;
  const char *kind;
  int xlog, ylog, zlog;
  int xflip, yflip, zflip;
  int major_count = 0, x_major_count, y_major_count;
  const double *stored_window;
  double x_min, x_max, y_min, y_max, z_min, z_max;
  double x, y, xzoom, yzoom;
  int adjust_xlim, adjust_ylim, adjust_zlim;
  double x_tick, y_tick;
  double x_org_low, x_org_high, y_org_low, y_org_high;
  int reset_ranges = 0;

  auto group = global_root->lastChildElement();

  grm_args_values(subplot_args, "kind", "s", &kind);
  grm_args_values(subplot_args, "xlog", "i", &xlog);
  grm_args_values(subplot_args, "ylog", "i", &ylog);
  grm_args_values(subplot_args, "zlog", "i", &zlog);
  grm_args_values(subplot_args, "xflip", "i", &xflip);
  grm_args_values(subplot_args, "yflip", "i", &yflip);
  grm_args_values(subplot_args, "zflip", "i", &zflip);

  if (!str_equals_any(kind, 3, "pie", "polar", "polar_histogram"))
    {
      scale |= xlog ? GR_OPTION_X_LOG : 0;
      scale |= ylog ? GR_OPTION_Y_LOG : 0;
      scale |= zlog ? GR_OPTION_Z_LOG : 0;
      scale |= xflip ? GR_OPTION_FLIP_X : 0;
      scale |= yflip ? GR_OPTION_FLIP_Y : 0;
      scale |= zflip ? GR_OPTION_FLIP_Z : 0;
    }

  grm_args_values(subplot_args, "_xlim", "dd", &x_min, &x_max);
  grm_args_values(subplot_args, "_ylim", "dd", &y_min, &y_max);
  if (grm_args_values(subplot_args, "reset_ranges", "i", &reset_ranges) && reset_ranges)
    {
      if (grm_args_values(subplot_args, "_original_xlim", "dd", &x_min, &x_max) &&
          grm_args_values(subplot_args, "_original_ylim", "dd", &y_min, &y_max) &&
          grm_args_values(subplot_args, "_original_adjust_xlim", "i", &adjust_xlim) &&
          grm_args_values(subplot_args, "_original_adjust_ylim", "i", &adjust_ylim))
        {
          grm_args_push(subplot_args, "_xlim", "dd", x_min, x_max);
          grm_args_push(subplot_args, "_ylim", "dd", y_min, y_max);
          grm_args_push(subplot_args, "adjust_xlim", "i", adjust_xlim);
          grm_args_push(subplot_args, "adjust_ylim", "i", adjust_ylim);
          grm_args_remove(subplot_args, "_original_xlim");
          grm_args_remove(subplot_args, "_original_ylim");
          grm_args_remove(subplot_args, "_original_adjust_xlim");
          grm_args_remove(subplot_args, "_original_adjust_ylim");
        }
      grm_args_remove(subplot_args, "reset_ranges");
    }
  if (grm_args_contains(subplot_args, "panzoom"))
    {
      if (!grm_args_contains(subplot_args, "_original_xlim"))
        {
          grm_args_push(subplot_args, "_original_xlim", "dd", x_min, x_max);
          grm_args_values(subplot_args, "adjust_xlim", "i", &adjust_xlim);
          grm_args_push(subplot_args, "_original_adjust_xlim", "i", adjust_xlim);
          grm_args_push(subplot_args, "adjust_xlim", "i", 0);
        }
      if (!grm_args_contains(subplot_args, "_original_ylim"))
        {
          grm_args_push(subplot_args, "_original_ylim", "dd", y_min, y_max);
          grm_args_values(subplot_args, "adjust_ylim", "i", &adjust_ylim);
          grm_args_push(subplot_args, "_original_adjust_ylim", "i", adjust_ylim);
          grm_args_push(subplot_args, "adjust_ylim", "i", 0);
        }
      if (!grm_args_values(subplot_args, "panzoom", "dddd", &x, &y, &xzoom, &yzoom))
        {
          if (grm_args_values(subplot_args, "panzoom", "ddd", &x, &y, &xzoom))
            {
              yzoom = xzoom;
            }
          else
            {
              /* TODO: Add error handling for type mismatch (-> next statement would fail) */
              grm_args_values(subplot_args, "panzoom", "dd", &x, &y);
              yzoom = xzoom = 0.0;
            }
        }
      /* Ensure the correct window is set in GR */
      if (grm_args_values(subplot_args, "window", "D", &stored_window))
        {
          global_render->setWindow(group, stored_window[0], stored_window[1], stored_window[2], stored_window[3]);
          //! TODO: Write a new gr_panzoom method that uses the dom-tree and not the internal gr attributes
          gr_setwindow(stored_window[0], stored_window[1], stored_window[2], stored_window[3]);
          logger((stderr, "Window before `gr_panzoom` (%lf, %lf, %lf, %lf)\n", stored_window[0], stored_window[1],
                  stored_window[2], stored_window[3]));
        }
      //! TODO: dom render
      gr_panzoom(x, y, xzoom, yzoom, &x_min, &x_max, &y_min, &y_max);
      logger((stderr, "Window after `gr_panzoom` (%lf, %lf, %lf, %lf)\n", x_min, x_max, y_min, y_max));
      grm_args_push(subplot_args, "_xlim", "dd", x_min, x_max);
      grm_args_push(subplot_args, "_ylim", "dd", y_min, y_max);
      grm_args_remove(subplot_args, "panzoom");
    }

  if (str_equals_any(kind, 6, "wireframe", "surface", "plot3", "scatter3", "polar", "trisurf"))
    {
      major_count = 2;
    }
  else
    {
      major_count = 5;
    }

  if (!(scale & GR_OPTION_X_LOG))
    {
      grm_args_values(subplot_args, "adjust_xlim", "i", &adjust_xlim);
      if (adjust_xlim)
        {
          logger((stderr, "_xlim before \"gr_adjustlimits\": (%lf, %lf)\n", x_min, x_max));
          gr_adjustlimits(&x_min, &x_max);
          logger((stderr, "_xlim after \"gr_adjustlimits\": (%lf, %lf)\n", x_min, x_max));
        }
      if (strcmp(kind, "barplot") == 0)
        {
          const char *xticklabels[5];
          unsigned int xticklabels_length;
          x_tick = 1;
          if (grm_args_first_value(subplot_args, "xticklabels", "S", &xticklabels, &xticklabels_length))
            {
              x_major_count = 0;
            }
          else
            {
              x_major_count = 1;
            }
        }
      else
        {
          x_major_count = major_count;
          x_tick = auto_tick(x_min, x_max) / x_major_count;
        }
    }
  else
    {
      x_tick = x_major_count = 1;
    }
  if (!(scale & GR_OPTION_FLIP_X))
    {
      x_org_low = x_min;
      x_org_high = x_max;
    }
  else
    {
      x_org_low = x_max;
      x_org_high = x_min;
    }
  grm_args_push(subplot_args, "xtick", "d", x_tick);
  grm_args_push(subplot_args, "xorg", "dd", x_org_low, x_org_high);
  grm_args_push(subplot_args, "xmajor", "i", x_major_count);

  if (!(scale & GR_OPTION_Y_LOG))
    {
      grm_args_values(subplot_args, "adjust_ylim", "i", &adjust_ylim);
      if (adjust_ylim)
        {
          logger((stderr, "_ylim before \"gr_adjustlimits\": (%lf, %lf)\n", y_min, y_max));
          gr_adjustlimits(&y_min, &y_max);
          logger((stderr, "_ylim after \"gr_adjustlimits\": (%lf, %lf)\n", y_min, y_max));
        }
      y_major_count = major_count;
      y_tick = auto_tick(y_min, y_max) / y_major_count;
    }
  else
    {
      y_tick = y_major_count = 1;
    }
  if (!(scale & GR_OPTION_FLIP_Y))
    {
      y_org_low = y_min;
      y_org_high = y_max;
    }
  else
    {
      y_org_low = y_max;
      y_org_high = y_min;
    }
  grm_args_push(subplot_args, "ytick", "d", y_tick);
  grm_args_push(subplot_args, "yorg", "dd", y_org_low, y_org_high);
  grm_args_push(subplot_args, "ymajor", "i", y_major_count);

  logger((stderr, "Storing window (%lf, %lf, %lf, %lf)\n", x_min, x_max, y_min, y_max));
  grm_args_push(subplot_args, "window", "dddd", x_min, x_max, y_min, y_max);
  if (!str_equals_any(kind, 2, "polar", "polar_histogram"))
    {
      gr_setwindow(x_min, x_max, y_min, y_max);
      global_render->setWindow(group, x_min, x_max, y_min, y_max);
    }
  else
    {
      global_render->setWindow(group, -1, 1, -1, 1);
    }

  if (str_equals_any(kind, 7, "wireframe", "surface", "plot3", "scatter3", "trisurf", "volume", "isosurface"))
    {
      int z_major_count;
      double z_tick;
      double z_org_low, z_org_high;
      double rotation, tilt;

      grm_args_values(subplot_args, "_zlim", "dd", &z_min, &z_max);
      if (!(scale & GR_OPTION_Z_LOG))
        {
          grm_args_values(subplot_args, "adjust_zlim", "i", &adjust_zlim);
          if (adjust_zlim)
            {
              logger((stderr, "_zlim before \"gr_adjustlimits\": (%lf, %lf)\n", z_min, z_max));
              gr_adjustlimits(&z_min, &z_max);
              logger((stderr, "_zlim after \"gr_adjustlimits\": (%lf, %lf)\n", z_min, z_max));
            }
          z_major_count = major_count;
          z_tick = auto_tick(z_min, z_max) / z_major_count;
        }
      else
        {
          z_tick = z_major_count = 1;
        }
      if (!(scale & GR_OPTION_FLIP_Z))
        {
          z_org_low = z_min;
          z_org_high = z_max;
        }
      else
        {
          z_org_low = z_max;
          z_org_high = z_min;
        }
      grm_args_push(subplot_args, "ztick", "d", z_tick);
      grm_args_push(subplot_args, "zorg", "dd", z_org_low, z_org_high);
      grm_args_push(subplot_args, "zmajor", "i", z_major_count);

      grm_args_values(subplot_args, "rotation", "d", &rotation);
      grm_args_values(subplot_args, "tilt", "d", &tilt);

      global_render->setWindow3d(group, x_min, x_max, y_min, y_max, z_min, z_max);
      global_render->setSpace3d(group, -rotation, tilt, 30, 0);
    }

  grm_args_push(subplot_args, "scale", "i", scale);
  global_render->setScale(global_root->lastChildElement(), scale);
}

err_t plot_store_coordinate_ranges(grm_args_t *subplot_args)
{
  const char *kind;
  const char *style = "";
  const char *fmt;
  grm_args_t **current_series;
  unsigned int series_count;
  grm_args_t **inner_series;
  unsigned int inner_series_count;
  const char *data_component_names[] = {"x", "y", "z", "c", NULL};
  const char **current_component_name;
  double *current_component = NULL;
  unsigned int current_point_count = 0;
  struct
  {
    const char *subplot;
    const char *series;
  } * current_range_keys,
      range_keys[] = {{"xlim", "xrange"}, {"ylim", "yrange"}, {"zlim", "zrange"}, {"clim", "crange"}};
  double *bins = NULL;
  unsigned int i;
  err_t error = ERROR_NONE;

  logger((stderr, "Storing coordinate ranges\n"));

  /* If a pan and/or zoom was performed before, do not overwrite limits
   * -> the user fully controls limits by interaction */
  if (grm_args_contains(subplot_args, "_original_xlim"))
    {
      logger((stderr, "Panzoom active, do not modify limits...\n"));
      return ERROR_NONE;
    }

  grm_args_values(subplot_args, "kind", "s", &kind);
  grm_args_values(subplot_args, "style", "s", &style);
  cleanup_and_set_error_if(!string_map_at(fmt_map, kind, static_cast<const char **>(&fmt)), ERROR_PLOT_UNKNOWN_KIND);
  if (!str_equals_any(kind, 2, "pie", "polar_histogram"))
    {
      current_component_name = data_component_names;
      current_range_keys = range_keys;
      while (*current_component_name != NULL)
        {
          double min_component = DBL_MAX;
          double max_component = -DBL_MAX;
          double step = -DBL_MAX;
          if (strchr(fmt, **current_component_name) == NULL)
            {
              ++current_range_keys;
              ++current_component_name;
              continue;
            }
          /* Heatmaps need calculated range keys, so run the calculation even if limits are given */
          if (!grm_args_contains(subplot_args, current_range_keys->subplot) || strcmp(kind, "heatmap") == 0)
            {
              grm_args_first_value(subplot_args, "series", "A", &current_series, &series_count);
              while (*current_series != NULL)
                {
                  double current_min_component = DBL_MAX, current_max_component = -DBL_MAX;
                  if (!grm_args_values(*current_series, current_range_keys->series, "dd", &current_min_component,
                                       &current_max_component))
                    {
                      if (grm_args_first_value(*current_series, *current_component_name, "D", &current_component,
                                               &current_point_count))
                        {
                          if (strcmp(style, "stacked") == 0)
                            {
                              current_max_component = 0.0;
                              current_min_component = 0.0;
                              for (i = 0; i < current_point_count; i++)
                                {
                                  if (current_component[i] > 0)
                                    {
                                      current_max_component += current_component[i];
                                    }
                                  else
                                    {
                                      current_min_component += current_component[i];
                                    }
                                }
                            }
                          else
                            {
                              for (i = 0; i < current_point_count; i++)
                                {
                                  current_min_component = grm_min(current_component[i], current_min_component);
                                  current_max_component = grm_max(current_component[i], current_max_component);
                                }
                            }
                        }
                      else if (strcmp(kind, "heatmap") == 0 && str_equals_any(*current_component_name, 2, "x", "y"))
                        {
                          /* in this case `x` or `y` (or both) are missing
                           * -> set the current grm_min/max_component to the dimensions of `z`
                           *    (shifted by half a unit to center color blocks) */
                          const char *other_component_name = (strcmp(*current_component_name, "x") == 0) ? "y" : "x";
                          double *other_component;
                          unsigned int other_point_count;
                          if (grm_args_first_value(*current_series, other_component_name, "D", &other_component,
                                                   &other_point_count))
                            {
                              /* The other component is given -> the missing dimension can be calculated */
                              double *z;
                              unsigned int z_length;
                              cleanup_and_set_error_if(!grm_args_first_value(*current_series, "z", "D", &z, &z_length),
                                                       ERROR_PLOT_MISSING_DATA);
                              current_point_count = z_length / other_point_count;
                            }
                          else
                            {
                              /* A heatmap without `x` and `y` values
                               * -> dimensions can only be read from `z_dims` */
                              int rows, cols;
                              cleanup_and_set_error_if(!grm_args_values(*current_series, "z_dims", "ii", &rows, &cols),
                                                       ERROR_PLOT_MISSING_DIMENSIONS);
                              current_point_count = (strcmp(*current_component_name, "x") == 0) ? cols : rows;
                            }
                          current_min_component = -0.5;
                          current_max_component = current_point_count - 0.5;
                        }
                      else if (grm_args_first_value(*current_series, "inner_series", "nA", &inner_series,
                                                    &inner_series_count))
                        {
                          while (*inner_series != NULL)
                            {
                              if (grm_args_first_value(*inner_series, *current_component_name, "D", &current_component,
                                                       &current_point_count))
                                {
                                  current_max_component = 0;
                                  current_min_component = 0;
                                  for (i = 0; i < current_point_count; i++)
                                    {
                                      if (current_component[i] > 0)
                                        {
                                          current_max_component += current_component[i];
                                        }
                                      else
                                        {
                                          current_min_component += current_component[i];
                                        }
                                    }
                                  max_component = grm_max(current_max_component, max_component);
                                  min_component = grm_min(current_min_component, min_component);
                                }
                              inner_series++;
                            }
                        }
                    }
                  if (current_min_component != DBL_MAX && current_max_component != -DBL_MAX)
                    {
                      grm_args_push(*current_series, current_range_keys->series, "dd", current_min_component,
                                    current_max_component);
                    }
                  min_component = grm_min(current_min_component, min_component);
                  max_component = grm_max(current_max_component, max_component);
                  ++current_series;
                }
            }
          if (grm_args_values(subplot_args, current_range_keys->subplot, "dd", &min_component, &max_component))
            {
              grm_args_push(subplot_args, private_name(current_range_keys->subplot), "dd", min_component,
                            max_component);
            }
          else if (min_component != DBL_MAX && max_component != -DBL_MAX)
            {
              if ((strcmp(kind, "barplot")) == 0 && strcmp("y", *current_component_name) == 0)
                {
                  if (min_component > 0)
                    {
                      min_component = 0;
                    }
                }
              else if (strcmp(kind, "quiver") == 0)
                {
                  step = grm_max(find_max_step(current_point_count, current_component), step);
                  if (step > 0.0)
                    {
                      min_component -= step;
                      max_component += step;
                    }
                }
              grm_args_push(subplot_args, private_name(current_range_keys->subplot), "dd", min_component,
                            max_component);
            }
          ++current_range_keys;
          ++current_component_name;
        }
    }
  /* For quiver plots use u^2 + v^2 as z value */
  if (strcmp(kind, "quiver") == 0)
    {
      double min_component = DBL_MAX;
      double max_component = -DBL_MAX;
      if (!grm_args_values(subplot_args, "zlim", "dd", &min_component, &max_component))
        {
          grm_args_values(subplot_args, "series", "A", &current_series);
          while (*current_series != NULL)
            {
              double current_min_component = DBL_MAX;
              double current_max_component = -DBL_MAX;
              if (!grm_args_values(*current_series, "zrange", "dd", &current_min_component, &current_max_component))
                {
                  double *u, *v;
                  unsigned int u_length, v_length;
                  cleanup_and_set_error_if(!grm_args_first_value(*current_series, "u", "D", &u, &u_length),
                                           ERROR_PLOT_MISSING_DATA);
                  cleanup_and_set_error_if(!grm_args_first_value(*current_series, "v", "D", &v, &v_length),
                                           ERROR_PLOT_MISSING_DATA);
                  cleanup_and_set_error_if(u_length != v_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
                  for (i = 0; i < u_length; i++)
                    {
                      double z = u[i] * u[i] + v[i] * v[i];
                      current_min_component = grm_min(z, current_min_component);
                      current_max_component = grm_max(z, current_max_component);
                    }
                  current_min_component = sqrt(current_min_component);
                  current_max_component = sqrt(current_max_component);
                }
              min_component = grm_min(current_min_component, min_component);
              max_component = grm_max(current_max_component, max_component);
              ++current_series;
            }
        }
      grm_args_push(subplot_args, "_zlim", "dd", min_component, max_component);
    }
  else if (str_equals_any(kind, 3, "imshow", "isosurface", "volume"))
    {
      /* Iterate over `x` and `y` range keys (and `z` depending on `kind`) */
      current_range_keys = range_keys;
      for (i = 0; i < (strcmp(kind, "volume") != 0 ? 2 : 3); i++)
        {
          double min_component = (strcmp(kind, "volume") != 0 ? 0.0 : -1.0);
          double max_component = 1.0;
          grm_args_values(subplot_args, current_range_keys->subplot, "dd", &min_component, &max_component);
          grm_args_push(subplot_args, private_name(current_range_keys->subplot), "dd", min_component, max_component);
          ++current_range_keys;
        }
    }
  else if (strcmp(kind, "barplot") == 0)
    {
      double x_min = 0.0, x_max = -DBL_MAX;
      if (!grm_args_values(subplot_args, "xlim", "dd", &x_min, &x_max))
        {
          if (str_equals_any(style, 2, "lined", "stacked"))
            {
              x_max = series_count + 1;
            }
          else
            {
              grm_args_values(subplot_args, "series", "A", &current_series);
              while (*current_series != NULL)
                {
                  double *y;
                  grm_args_first_value(*current_series, "y", "D", &y, &current_point_count);
                  x_max = grm_max(current_point_count + 1, x_max);
                  ++current_series;
                }
            }
        }
      grm_args_push(subplot_args, "_xlim", "dd", x_min, x_max);
    }
  else if (strcmp(kind, "hist") == 0)
    {
      double y_min = 0.0, y_max = 0.0;
      if (!grm_args_values(subplot_args, "ylim", "dd", &y_min, &y_max))
        {
          grm_args_values(subplot_args, "series", "A", &current_series);
          while (*current_series != NULL)
            {
              double current_y_min = DBL_MAX, current_y_max = -DBL_MAX;
              if (!grm_args_values(*current_series, "yrange", "dd", &current_y_min, &current_y_max))
                {
                  double *x = NULL, *weights = NULL;
                  unsigned int num_bins = 0, num_weights;
                  cleanup_and_set_error_if(!grm_args_first_value(*current_series, "x", "D", &x, &current_point_count),
                                           ERROR_PLOT_MISSING_DATA);
                  grm_args_values(*current_series, "nbins", "i", &num_bins);
                  grm_args_first_value(*current_series, "weights", "D", &weights, &num_weights);
                  if (weights != NULL)
                    {
                      cleanup_and_set_error_if(current_point_count != num_weights,
                                               ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
                    }
                  if (num_bins <= 1)
                    {
                      num_bins = (int)(3.3 * log10(current_point_count) + 0.5) + 1;
                    }
                  bins = static_cast<double *>(malloc(num_bins * sizeof(double)));
                  cleanup_and_set_error_if(bins == NULL, ERROR_MALLOC);
                  bin_data(current_point_count, x, num_bins, bins, weights);
                  for (i = 0; i < num_bins; i++)
                    {
                      current_y_min = grm_min(current_y_min, bins[i]);
                      current_y_max = grm_max(current_y_max, bins[i]);
                    }
                  grm_args_push(*current_series, "bins", "nD", num_bins, bins);
                  free(bins);
                  bins = NULL;
                }
              y_min = grm_min(current_y_min, y_min);
              y_max = grm_max(current_y_max, y_max);
              current_series++;
            }
          grm_args_push(subplot_args, "_ylim", "dd", y_min, y_max);
        }
    }
  else if (strcmp(kind, "polar_histogram") == 0)
    {
      double r_max;
      error = classes_polar_histogram(subplot_args, &r_max);
      cleanup_if_error;
      grm_args_push(subplot_args, "r_max", "d", r_max);
    }

cleanup:
  free(bins);

  return error;
}

void plot_post_plot(grm_args_t *plot_args)
{
  int update;

  logger((stderr, "Post plot processing\n"));

  grm_args_values(plot_args, "update", "i", &update);
  logger((stderr, "Got keyword \"update\" with value %d\n", update));
  if (update)
    {
      gr_updatews();
    }
  plot_restore_text_encoding();
}

void plot_restore_text_encoding(void)
{
  gr_inqtextencoding(&pre_plot_text_encoding);
  if (pre_plot_text_encoding >= 0)
    {
      gr_settextencoding(pre_plot_text_encoding);
      pre_plot_text_encoding = -1;
    }
}

void plot_post_subplot(grm_args_t *subplot_args)
{
  const char *kind;

  logger((stderr, "Post subplot processing\n"));

  gr_restorestate();
  grm_args_values(subplot_args, "kind", "s", &kind);
  grm_args_values(subplot_args, "kind", "s", &kind);
  logger((stderr, "Got keyword \"kind\" with value \"%s\"\n", kind));
  if (grm_args_contains(subplot_args, "labels"))
    {
      if (str_equals_any(kind, 4, "line", "step", "scatter", "stem"))
        {
          plot_draw_legend(subplot_args);
        }
      else if (strcmp(kind, "pie") == 0)
        {
          plot_draw_pie_legend(subplot_args);
        }
    }
  if (strcmp(kind, "barplot") == 0)
    {
      plot_draw_axes(subplot_args, 2);
    }
}

err_t plot_get_args_in_hierarchy(grm_args_t *args, const char **hierarchy_name_start_ptr, const char *key,
                                 uint_map_t *hierarchy_to_id, const grm_args_t **found_args,
                                 const char ***found_hierarchy_name_ptr)
{
  const char *key_hierarchy_name, **current_hierarchy_name_ptr;
  grm_args_t *current_args, **args_array;
  arg_t *current_arg;
  unsigned int args_array_length, current_id;

  logger((stderr, "Check hierarchy level for key \"%s\"...\n", key));
  return_error_if(!string_map_at(plot_valid_keys_map, key, static_cast<const char **>(&key_hierarchy_name)),
                  ERROR_PLOT_UNKNOWN_KEY);
  logger((stderr, "... got hierarchy \"%s\"\n", key_hierarchy_name));
  current_hierarchy_name_ptr = hierarchy_name_start_ptr;
  current_args = args;
  if (strcmp(*hierarchy_name_start_ptr, key_hierarchy_name) != 0)
    {
      while (*++current_hierarchy_name_ptr != NULL)
        {
          current_arg = args_at(current_args, *current_hierarchy_name_ptr);
          return_error_if(current_arg == NULL, ERROR_INTERNAL);
          arg_first_value(current_arg, "A", &args_array, &args_array_length);
          uint_map_at(hierarchy_to_id, *current_hierarchy_name_ptr, &current_id);
          /* Check for the invalid id 0 because id 0 is set for append mode */
          if (current_id == 0)
            {
              current_id = args_array_length + 1;
              if (strcmp(*current_hierarchy_name_ptr, "plots") == 0)
                {
                  int last_plot_in_use = 0;
                  if (grm_args_values(args_array[args_array_length - 1], "in_use", "i", &last_plot_in_use) &&
                      !last_plot_in_use)
                    {
                      /* Use the last already existing plot if it is still empty */
                      --current_id;
                    }
                }
              logger((stderr, "Append mode, set id to \"%u\"\n", current_id));
              uint_map_insert(hierarchy_to_id, *current_hierarchy_name_ptr, current_id);
            }
          if (current_id > args_array_length)
            {
              plot_init_args_structure(current_args, current_hierarchy_name_ptr - 1, current_id);
              arg_first_value(current_arg, "A", &args_array, &args_array_length);
            }
          current_args = args_array[current_id - 1];
          if (strcmp(*current_hierarchy_name_ptr, "plots") == 0)
            {
              int in_use;
              err_t error = ERROR_NONE;
              grm_args_values(current_args, "in_use", "i", &in_use);
              if (in_use)
                {
                  error = event_queue_enqueue_update_plot_event(event_queue, current_id - 1);
                }
              else
                {
                  error = event_queue_enqueue_new_plot_event(event_queue, current_id - 1);
                }
              return_if_error;
              grm_args_push(current_args, "in_use", "i", 1);
            }
          if (strcmp(*current_hierarchy_name_ptr, key_hierarchy_name) == 0)
            {
              break;
            }
        }
      return_error_if(*current_hierarchy_name_ptr == NULL, ERROR_INTERNAL);
    }
  if (found_args != NULL)
    {
      *found_args = current_args;
    }
  if (found_hierarchy_name_ptr != NULL)
    {
      *found_hierarchy_name_ptr = current_hierarchy_name_ptr;
    }

  return ERROR_NONE;
}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~ plotting ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

err_t plot_line(grm_args_t *subplot_args)
{
  grm_args_t **current_series;
  err_t error;
  const char *kind;

  grm_args_values(subplot_args, "series", "A", &current_series);
  grm_args_values(subplot_args, "kind", "s", &kind);
  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  group->setAttribute("name", "line");

  while (*current_series != NULL)
    {
      double *x, *y;
      unsigned int x_length, y_length;
      char *spec;
      int mask;
      auto subGroup = global_render->createGroup("line_series");
      group->append(subGroup);
      return_error_if(!grm_args_first_value(*current_series, "x", "D", &x, &x_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(!grm_args_first_value(*current_series, "y", "D", &y, &y_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(x_length != y_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
      std::vector<double> x_vec(x, x + x_length);
      std::vector<double> y_vec(y, y + y_length);
      grm_args_values(*current_series, "spec", "s", &spec); /* `spec` is always set */
      mask = gr_uselinespec(spec);
      if (int_equals_any(mask, 5, 0, 1, 3, 4, 5))
        {
          int current_line_colorind;
          gr_inqlinecolorind(&current_line_colorind);
          int id = static_cast<int>(global_root->getAttribute("id"));
          std::string str = std::to_string(id);
          auto element = global_render->createPolyline(str + "x", x_vec, str + "y", y_vec);
          global_root->setAttribute("id", ++id);
          element->setAttribute("linecolorind", current_line_colorind);
          subGroup->append(element);
        }
      if (mask & 2)
        {
          int current_marker_colorind;
          gr_inqmarkercolorind(&current_marker_colorind);
          int id = static_cast<int>(global_root->getAttribute("id"));
          std::string str = std::to_string(id);
          auto element = global_render->createPolymarker(str + "x", x_vec, str + "y", y_vec);
          global_root->setAttribute("id", ++id);
          element->setAttribute("markercolorind", current_marker_colorind);
          subGroup->append(element);
        }

      error = plot_draw_errorbars(*current_series, x, x_length, y, kind);
      return_if_error;

      ++current_series;
    }

  return ERROR_NONE;
}

err_t plot_step(grm_args_t *subplot_args)
{
  /*
   * Parameters:
   * `x` as double array
   * `y` as double array
   * optional step position `step_where` as string, modes: `pre`, `mid`, `post`, Default: `mid`
   * optional `spec`
   */
  grm_args_t **current_series;

  grm_args_values(subplot_args, "series", "A", &current_series);
  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  group->setAttribute("name", "step");

  while (*current_series != NULL)
    {
      double *x, *y, *x_step_boundaries = NULL, *y_step_values = NULL;
      unsigned int x_length, y_length, mask, i;
      char *spec;
      auto subGroup = global_render->createGroup("step_series");
      group->append(subGroup);

      return_error_if(!grm_args_first_value(*current_series, "x", "D", &x, &x_length) && x_length < 1,
                      ERROR_PLOT_MISSING_DATA);
      return_error_if(!grm_args_first_value(*current_series, "y", "D", &y, &y_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(x_length != y_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
      grm_args_values(*current_series, "spec", "s", &spec); /* `spec` is always set */
      mask = gr_uselinespec(spec);
      if (int_equals_any(mask, 5, 0, 1, 3, 4, 5))
        {
          const char *where;
          grm_args_values(*current_series, "step_where", "s", &where); /* `spec` is always set */
          if (strcmp(where, "pre") == 0)
            {
              x_step_boundaries = static_cast<double *>(calloc(2 * x_length - 1, sizeof(double)));
              y_step_values = static_cast<double *>(calloc(2 * x_length - 1, sizeof(double)));
              x_step_boundaries[0] = x[0];
              for (i = 1; i < 2 * x_length - 2; i += 2)
                {
                  x_step_boundaries[i] = x[i / 2];
                  x_step_boundaries[i + 1] = x[i / 2 + 1];
                }
              y_step_values[0] = y[0];
              for (i = 1; i < 2 * x_length - 1; i += 2)
                {
                  y_step_values[i] = y_step_values[i + 1] = y[i / 2 + 1];
                }
              int id = static_cast<int>(global_root->getAttribute("id"));
              std::string str = std::to_string(id);
              std::vector<double> x_vec(x_step_boundaries, x_step_boundaries + 2 * x_length - 1);
              std::vector<double> y_vec(y_step_values, y_step_values + 2 * x_length - 1);
              auto element = global_render->createPolyline(str + "x", x_vec, str + "y", y_vec);
              global_root->setAttribute("id", ++id);
              subGroup->append(element);
            }
          else if (strcmp(where, "post") == 0)
            {
              x_step_boundaries = static_cast<double *>(calloc(2 * x_length - 1, sizeof(double)));
              y_step_values = static_cast<double *>(calloc(2 * x_length - 1, sizeof(double)));
              for (i = 0; i < 2 * x_length - 2; i += 2)
                {
                  x_step_boundaries[i] = x[i / 2];
                  x_step_boundaries[i + 1] = x[i / 2 + 1];
                }
              x_step_boundaries[2 * x_length - 2] = x[x_length - 1];
              for (i = 0; i < 2 * x_length - 2; i += 2)
                {
                  y_step_values[i] = y_step_values[i + 1] = y[i / 2];
                }
              y_step_values[2 * x_length - 2] = y[x_length - 1];
              int id = static_cast<int>(global_root->getAttribute("id"));
              std::string str = std::to_string(id);
              std::vector<double> x_vec(x_step_boundaries, x_step_boundaries + 2 * x_length - 1);
              std::vector<double> y_vec(y_step_values, y_step_values + 2 * x_length - 1);
              auto element = global_render->createPolyline(str + "x", x_vec, str + "y", y_vec);
              global_root->setAttribute("id", ++id);
              subGroup->append(element);
            }
          else if (strcmp(where, "mid") == 0)
            {
              x_step_boundaries = static_cast<double *>(calloc(2 * x_length, sizeof(double)));
              y_step_values = static_cast<double *>(calloc(2 * x_length, sizeof(double)));
              x_step_boundaries[0] = x[0];
              for (i = 1; i < 2 * x_length - 2; i += 2)
                {
                  x_step_boundaries[i] = x_step_boundaries[i + 1] = (x[i / 2] + x[i / 2 + 1]) / 2.0;
                }
              x_step_boundaries[2 * x_length - 1] = x[x_length - 1];
              for (i = 0; i < 2 * x_length - 1; i += 2)
                {
                  y_step_values[i] = y_step_values[i + 1] = y[i / 2];
                }
              int id = static_cast<int>(global_root->getAttribute("id"));
              std::string str = std::to_string(id);
              std::vector<double> x_vec(x_step_boundaries, x_step_boundaries + 2 * x_length - 1);
              std::vector<double> y_vec(y_step_values, y_step_values + 2 * x_length - 1);
              auto element = global_render->createPolyline(str + "x", x_vec, str + "y", y_vec);
              global_root->setAttribute("id", ++id);
              subGroup->append(element);
            }
          free(x_step_boundaries);
          free(y_step_values);
        }
      if (mask & 2)
        {
          std::vector<double> x_vec(x, x + x_length);
          std::vector<double> y_vec(y, y + y_length);

          int id = static_cast<int>(global_root->getAttribute("id"));
          std::string str = std::to_string(id);
          auto element = global_render->createPolymarker(str + "x", x_vec, str + "y", y_vec);
          global_root->setAttribute("id", ++id);
          subGroup->append(element);
        }
      ++current_series;
    }

  return ERROR_NONE;
}

err_t plot_scatter(grm_args_t *subplot_args)
{
  /*
   * Parameters:
   * `x` as double array
   * `y` as double array
   * optional marker size `z` as double array
   * optional marker color `c` as double array for each marker or as single integer for all markers
   * optional `markertype` as integer (see: [Marker types](https://gr-framework.org/markertypes.html?highlight=marker))
   */
  grm_args_t **current_series;
  err_t error;
  char *kind;
  int *previous_marker_type = plot_scatter_markertypes;
  grm_args_values(subplot_args, "series", "A", &current_series);
  grm_args_values(subplot_args, "kind", "s", &kind);
  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  group->setAttribute("name", "scatter");

  while (*current_series != NULL)
    {
      auto subGroup = global_render->createGroup("scatter_series");
      group->append(subGroup);

      auto parent_element = global_render->createElement("polymarker");
      double *x = NULL, *y = NULL, *z = NULL, *c = NULL, c_min, c_max;
      double *markertypes = NULL;
      unsigned int x_length, y_length, z_length, c_length, markertypes_length;
      int i, c_index = -1, markertype;
      std::vector<int> markerTypesVec, markerColorIndsVec;
      std::vector<double> markerSizesVec;

      return_error_if(!grm_args_first_value(*current_series, "x", "D", &x, &x_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(!grm_args_first_value(*current_series, "y", "D", &y, &y_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(x_length != y_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
      if (grm_args_first_value(*current_series, "z", "D", &z, &z_length))
        {
          return_error_if(x_length != z_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
        }
      if (grm_args_values(*current_series, "markertype", "i", &markertype))
        {
          global_render->setMarkerType(subGroup, markertype);
        }
      else if (!grm_args_first_value(*current_series, "markertype", "D", &markertypes, &markertypes_length))
        {
          global_render->setMarkerType(subGroup, *previous_marker_type++);
          if (*previous_marker_type == INT_MAX)
            {
              previous_marker_type = plot_scatter_markertypes;
            }
        }
      if (!grm_args_first_value(*current_series, "c", "D", &c, &c_length) &&
          grm_args_values(*current_series, "c", "i", &c_index))
        {
          if (c_index < 0)
            {
              logger((stderr, "Invalid scatter color %d, using 0 instead\n", c_index));
              c_index = 0;
            }
          else if (c_index > 255)
            {
              logger((stderr, "Invalid scatter color %d, using 255 instead\n", c_index));
              c_index = 255;
            }
        }
      if (z != NULL || c != NULL || markertypes != NULL)
        {
          grm_args_values(subplot_args, "_clim", "dd", &c_min, &c_max);

          for (i = 0; i < x_length; i++)
            {
              if (z != NULL)
                {
                  if (i < z_length)
                    {
                      markerSizesVec.push_back(z[i] / 100.0);
                    }
                  else
                    {
                      markerSizesVec.push_back(2.0);
                    }
                }
              if (c != NULL)
                {
                  if (i < c_length)
                    {
                      c_index = 1000 + (int)(255 * (c[i] - c_min) / (c_max - c_min));
                      if (c_index < 1000 || c_index > 1255)
                        {
                          markerColorIndsVec.push_back(0);
                          // ToDo: Originally this marker should be skipped...
                          continue;
                        }
                    }
                  else
                    {
                      c_index = 989;
                    }
                  markerColorIndsVec.push_back(c_index);
                }
              else if (c_index != -1)
                {
                  markerColorIndsVec.push_back(1000 + c_index);
                }

              if (markertypes != NULL)
                {
                  if (i < markertypes_length)
                    {
                      markerTypesVec.push_back((int)markertypes[i]);
                    }
                  else
                    {
                      markerTypesVec.push_back((int)markertypes[markertypes_length - 1]);
                    }
                }
              else
                {
                  markerTypesVec.push_back(*previous_marker_type);
                }
            }

          std::vector<double> x_vec(x, x + x_length);
          std::vector<double> y_vec(y, y + y_length);

          int id = static_cast<int>(global_root->getAttribute("id"));
          std::string str = std::to_string(id);
          global_root->setAttribute("id", ++id);

          auto element = global_render->createPolymarker(str + "x", x_vec, str + "y", y_vec);
          subGroup->append(element);
          if (!markerTypesVec.empty())
            {
              global_render->setMarkerType(element, "markertypes" + str, markerTypesVec);
            }
          if (!markerSizesVec.empty())
            {
              global_render->setMarkerSize(element, "markersizes" + str, markerSizesVec);
            }
          if (!markerColorIndsVec.empty())
            {
              global_render->setMarkerColorInd(element, "markercolorinds" + str, markerColorIndsVec);
            }


          if (markertypes == NULL)
            {
              previous_marker_type++;
            }
        }
      else
        {
          std::vector<double> x_vec(x, x + x_length);
          std::vector<double> y_vec(y, y + y_length);

          int id = static_cast<int>(global_root->getAttribute("id"));
          std::string str = std::to_string(id);
          auto element = global_render->createPolymarker(str + "x", x_vec, str + "y", y_vec);
          subGroup->append(element);
          global_root->setAttribute("id", ++id);
        }
      error = plot_draw_errorbars(*current_series, x, x_length, y, kind);
      return_if_error;
      ++current_series;
    }

  return ERROR_NONE;
}

err_t plot_quiver(grm_args_t *subplot_args)
{
  grm_args_t **current_series;
  err_t error = ERROR_NONE;

  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  group->setAttribute("name", "quiver");

  grm_args_values(subplot_args, "series", "A", &current_series);
  while (*current_series != NULL)
    {
      auto subGroup = global_render->createGroup("quiver_series");
      group->append(subGroup);

      double *x = NULL, *y = NULL, *u = NULL, *v = NULL;
      unsigned int x_length, y_length, u_length, v_length;
      return_error_if(!grm_args_first_value(*current_series, "x", "D", &x, &x_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(!grm_args_first_value(*current_series, "y", "D", &y, &y_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(!grm_args_first_value(*current_series, "u", "D", &u, &u_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(!grm_args_first_value(*current_series, "v", "D", &v, &v_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(x_length * y_length != u_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
      return_error_if(x_length * y_length != v_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);

      std::vector<double> x_vec(x, x + x_length);
      std::vector<double> y_vec(y, y + y_length);
      std::vector<double> u_vec(u, u + x_length * y_length);
      std::vector<double> v_vec(v, v + x_length * y_length);

      int id = (int)global_root->getAttribute("id");
      std::string str = std::to_string(id);
      global_root->setAttribute("id", id + 1);
      subGroup->append(
          global_render->createQuiver("x" + str, x_vec, "y" + str, y_vec, "u" + str, u_vec, "v" + str, v_vec, 1));

      ++current_series;
    }
  error = plot_draw_colorbar(subplot_args, 0.05, 256);

  return error;
}

err_t plot_stem(grm_args_t *subplot_args)
{
  const double *window;
  double base_line_y[2] = {0.0, 0.0};
  double stem_x[2], stem_y[2] = {0.0};
  grm_args_t **current_series;
  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();

  group->setAttribute("name", "stem");

  grm_args_values(subplot_args, "window", "D", &window);
  grm_args_values(subplot_args, "series", "A", &current_series);
  while (*current_series != NULL)
    {
      double *x, *y;
      unsigned int x_length, y_length;
      char *spec;
      unsigned int i;

      auto subGroup = global_render->createGroup("stem_series");
      group->append(subGroup);

      return_error_if(!grm_args_first_value(*current_series, "x", "D", &x, &x_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(!grm_args_first_value(*current_series, "y", "D", &y, &y_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(x_length != y_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);

      subGroup->append(global_render->createPolyline(window[0], window[1], base_line_y[0], base_line_y[1]));
      grm_args_values(*current_series, "spec", "s", &spec);

      global_render->setLineSpec(subGroup, spec);
      for (i = 0; i < x_length; ++i)
        {
          stem_x[0] = stem_x[1] = x[i];
          stem_y[1] = y[i];
          subGroup->append(global_render->createPolyline(stem_x[0], stem_x[1], stem_y[0], stem_y[1]));
        }
      std::vector<double> x_vec(x, x + x_length);
      std::vector<double> y_vec(y, y + x_length);
      int id = static_cast<int>(global_root->getAttribute("id"));
      std::string str = std::to_string(id);
      subGroup->append(
          global_render->createPolymarker("x" + str, x_vec, "y" + str, y_vec, nullptr, GKS_K_MARKERTYPE_SOLID_CIRCLE));
      global_root->setAttribute("id", ++id);
      ++current_series;
    }

  return ERROR_NONE;
}

err_t plot_hist(grm_args_t *subplot_args)
{
  char *kind;
  grm_args_t **current_series;
  double *bar_centers = NULL;
  int bar_color_index = 989, i;
  double bar_color_rgb[3] = {-1};
  err_t error = ERROR_NONE;

  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  global_root->lastChildElement()->setAttribute("name", "hist");

  grm_args_values(subplot_args, "kind", "s", &kind);
  grm_args_values(subplot_args, "series", "A", &current_series);
  grm_args_values(subplot_args, "bar_color", "ddd", &bar_color_rgb[0], &bar_color_rgb[1], &bar_color_rgb[2]);
  grm_args_values(subplot_args, "bar_color", "i", &bar_color_index);
  if (bar_color_rgb[0] != -1)
    {
      for (i = 0; i < 3; i++)
        {
          cleanup_and_set_error_if((bar_color_rgb[i] > 1 || bar_color_rgb[i] < 0), ERROR_PLOT_OUT_OF_RANGE);
        }
      bar_color_index = 1000;
      global_render->setColorRep(group, bar_color_index, bar_color_rgb[0], bar_color_rgb[1], bar_color_rgb[2]);
    }

  while (*current_series != NULL)
    {
      double edge_color_index = 1;
      double edge_color_rgb[3] = {-1};
      double x_min, x_max, bar_width;
      double *bins;
      unsigned int num_bins;

      auto subGroup = global_render->createGroup("hist_series");
      group->append(subGroup);

      grm_args_values(*current_series, "edge_color", "ddd", &edge_color_rgb[0], &edge_color_rgb[1], &edge_color_rgb[2]);
      grm_args_values(*current_series, "edge_color", "i", &edge_color_index);
      if (edge_color_rgb[0] != -1)
        {
          for (i = 0; i < 3; i++)
            {
              cleanup_and_set_error_if((edge_color_rgb[i] > 1 || edge_color_rgb[i] < 0), ERROR_PLOT_OUT_OF_RANGE);
            }
          edge_color_index = 1001;
          global_render->setColorRep(subGroup, edge_color_index, edge_color_rgb[0], edge_color_rgb[1],
                                     edge_color_rgb[2]);
        }

      grm_args_first_value(*current_series, "bins", "D", &bins, &num_bins);
      grm_args_values(*current_series, "xrange", "dd", &x_min, &x_max);

      bar_width = (x_max - x_min) / num_bins;
      for (i = 1; i < num_bins + 1; ++i)
        {
          double x = x_min + (i - 1) * bar_width;
          auto fillRect1 = global_render->createFillRect(x, x + bar_width, 0.0, bins[i - 1]);
          global_render->setFillColorInd(fillRect1, bar_color_index);
          global_render->setFillIntStyle(fillRect1, GKS_K_INTSTYLE_SOLID);
          subGroup->append(fillRect1);

          auto fillRect2 = global_render->createFillRect(x, x + bar_width, 0.0, bins[i - 1]);
          global_render->setFillColorInd(fillRect2, edge_color_index);
          global_render->setFillIntStyle(fillRect2, GKS_K_INTSTYLE_HOLLOW);
          subGroup->append(fillRect2);
        }
      if (grm_args_contains(*current_series, "error"))
        {
          bar_centers = static_cast<double *>(malloc(num_bins * sizeof(double)));
          cleanup_and_set_error_if(bar_centers == NULL, ERROR_MALLOC);
          linspace(x_min + 0.5 * bar_width, x_max - 0.5 * bar_width, num_bins, bar_centers);
          error = plot_draw_errorbars(*current_series, bar_centers, num_bins, bins, kind);
          cleanup_if_error;
          free(bar_centers);
          bar_centers = NULL;
        }
      ++current_series;
    }

cleanup:
  free(bar_centers);

  return error;
}

err_t plot_barplot(grm_args_t *subplot_args)
{
  grm_args_t **current_series;
  grm_args_t **inner_series;
  unsigned int inner_series_length;

  /* Default */
  int bar_color = 989, edge_color = 1;
  double bar_color_rgb[3] = {-1};
  double edge_color_rgb[3] = {-1};
  double bar_width = 1, edge_width = 1;
  const char *style = "default";
  double *y;
  unsigned int y_length;
  unsigned int fixed_y_length = 0;
  grm_args_t **ind_bar_color = NULL;
  double(*pos_ind_bar_color)[3] = NULL;
  grm_args_t **ind_edge_color = NULL;
  double(*pos_ind_edge_color)[3] = NULL;
  grm_args_t **ind_edge_width = NULL;
  double *pos_ind_edge_width = NULL;
  int series_index = 0;
  double wfac;
  int len_std_colors = 20;
  int std_colors[20] = {989, 982, 980, 981, 996, 983, 995, 988, 986, 990,
                        991, 984, 992, 993, 994, 987, 985, 997, 998, 999};
  int color_save_spot = 1000;
  int change_bar_color = 0;
  int change_edge_color = 0;
  int change_edge_width = 0;
  unsigned int i;
  err_t error = ERROR_NONE;
  double *y_lightness = NULL;

  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  gr_savestate();
  group->setAttribute("name", "barplot");

  grm_args_values(subplot_args, "series", "A", &current_series);
  grm_args_values(subplot_args, "bar_color", "ddd", &bar_color_rgb[0], &bar_color_rgb[1], &bar_color_rgb[2]);
  grm_args_values(subplot_args, "bar_color", "i", &bar_color);
  grm_args_values(subplot_args, "bar_width", "d", &bar_width);
  grm_args_values(subplot_args, "style", "s", &style);

  if (bar_color_rgb[0] != -1)
    {
      for (i = 0; i < 3; i++)
        {
          cleanup_and_set_error_if((bar_color_rgb[i] > 1 || bar_color_rgb[i] < 0), ERROR_PLOT_OUT_OF_RANGE);
        }
    }

  /* ind_parameter */
  /* determine the length of y */
  while (*current_series != NULL)
    {
      if (!grm_args_first_value(*current_series, "y", "D", &y, &y_length))
        {
          cleanup_and_set_error_if(
              !grm_args_first_value(*current_series, "inner_series", "A", &inner_series, &inner_series_length),
              ERROR_PLOT_MISSING_DATA);
          y_length = inner_series_length;
        }
      fixed_y_length = grm_max(y_length, fixed_y_length);
      ++current_series;
    }

  /* ind_bar_color */
  if (grm_args_values(subplot_args, "ind_bar_color", "A", &ind_bar_color))
    {
      pos_ind_bar_color = static_cast<double(*)[3]>(malloc(3 * fixed_y_length * sizeof(double)));
      cleanup_and_set_error_if(pos_ind_bar_color == NULL, ERROR_MALLOC);
      change_bar_color = 1;
      for (i = 0; i < fixed_y_length; ++i)
        {
          pos_ind_bar_color[i][0] = -1;
        }
      while (*ind_bar_color != NULL)
        {
          int *indices = NULL;
          unsigned int indices_length;
          int index;
          double rgb[3];
          unsigned int j;
          cleanup_and_set_error_if(!(grm_args_first_value(*ind_bar_color, "indices", "I", &indices, &indices_length) ||
                                     grm_args_values(*ind_bar_color, "indices", "i", &index)),
                                   ERROR_PLOT_MISSING_DATA);
          cleanup_and_set_error_if(!grm_args_values(*ind_bar_color, "rgb", "ddd", &rgb[0], &rgb[1], &rgb[2]),
                                   ERROR_PLOT_MISSING_DATA);
          for (j = 0; j < 3; j++)
            {
              cleanup_and_set_error_if((rgb[j] > 1 || rgb[j] < 0), ERROR_PLOT_OUT_OF_RANGE);
            }

          if (indices != NULL)
            {
              for (j = 0; j < indices_length; ++j)
                {
                  cleanup_and_set_error_if(indices[j] - 1 >= (int)fixed_y_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
                  pos_ind_bar_color[indices[j] - 1][0] = rgb[0];
                  pos_ind_bar_color[indices[j] - 1][1] = rgb[1];
                  pos_ind_bar_color[indices[j] - 1][2] = rgb[2];
                }
            }
          else
            {
              cleanup_and_set_error_if(index - 1 >= (int)fixed_y_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
              pos_ind_bar_color[index - 1][0] = rgb[0];
              pos_ind_bar_color[index - 1][1] = rgb[1];
              pos_ind_bar_color[index - 1][2] = rgb[2];
            }
          ind_bar_color++;
        }
    }

  /* ind_edge_color */
  if (grm_args_values(subplot_args, "ind_edge_color", "A", &ind_edge_color))
    {
      pos_ind_edge_color = static_cast<double(*)[3]>(malloc(3 * fixed_y_length * sizeof(double)));
      cleanup_and_set_error_if(pos_ind_edge_color == NULL, ERROR_MALLOC);
      change_edge_color = 1;
      for (i = 0; i < fixed_y_length; ++i)
        {
          pos_ind_edge_color[i][0] = -1;
        }
      while (*ind_edge_color != NULL)
        {
          int *indices = NULL;
          unsigned int indices_length;
          int index;
          double rgb[3];
          unsigned int j;
          cleanup_and_set_error_if(!(grm_args_first_value(*ind_edge_color, "indices", "I", &indices, &indices_length) ||
                                     grm_args_values(*ind_edge_color, "indices", "i", &index)),
                                   ERROR_PLOT_MISSING_DATA);
          cleanup_and_set_error_if(!grm_args_values(*ind_edge_color, "rgb", "ddd", &rgb[0], &rgb[1], &rgb[2]),
                                   ERROR_PLOT_MISSING_DATA);
          for (j = 0; j < 3; j++)
            {
              cleanup_and_set_error_if((rgb[j] > 1 || rgb[j] < 0), ERROR_PLOT_OUT_OF_RANGE);
            }

          if (indices != NULL)
            {
              for (j = 0; j < indices_length; ++j)
                {
                  cleanup_and_set_error_if(indices[j] - 1 >= (int)fixed_y_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
                  pos_ind_edge_color[indices[j] - 1][0] = rgb[0];
                  pos_ind_edge_color[indices[j] - 1][1] = rgb[1];
                  pos_ind_edge_color[indices[j] - 1][2] = rgb[2];
                }
            }
          else
            {
              cleanup_and_set_error_if(index - 1 >= (int)fixed_y_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
              pos_ind_edge_color[index - 1][0] = rgb[0];
              pos_ind_edge_color[index - 1][1] = rgb[1];
              pos_ind_edge_color[index - 1][2] = rgb[2];
            }
          ind_edge_color++;
        }
    }

  /* ind_edge_width */
  if (grm_args_values(subplot_args, "ind_edge_width", "A", &ind_edge_width))
    {
      pos_ind_edge_width = static_cast<double *>(malloc(sizeof(double) * fixed_y_length));
      cleanup_and_set_error_if(pos_ind_edge_width == NULL, ERROR_MALLOC);
      for (i = 0; i < fixed_y_length; ++i)
        {
          pos_ind_edge_width[i] = -1;
        }
      change_edge_width = 1;
      while (*ind_edge_width != NULL)
        {
          int *indices = NULL;
          unsigned int indices_length;
          int index;
          double width;
          unsigned int j;
          cleanup_and_set_error_if(!(grm_args_first_value(*ind_edge_width, "indices", "I", &indices, &indices_length) ||
                                     grm_args_values(*ind_edge_width, "indices", "i", &index)),
                                   ERROR_PLOT_MISSING_DATA);
          cleanup_and_set_error_if(!grm_args_values(*ind_edge_width, "width", "d", &width), ERROR_PLOT_MISSING_DATA);
          cleanup_and_set_error_if(width < 0, ERROR_PLOT_OUT_OF_RANGE);

          if (indices != NULL)
            {
              for (j = 0; j < indices_length; ++j)
                {
                  cleanup_and_set_error_if(indices[j] - 1 >= (int)fixed_y_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
                  pos_ind_edge_width[indices[j] - 1] = width;
                }
            }
          else
            {
              cleanup_and_set_error_if(index - 1 >= (int)fixed_y_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
              pos_ind_edge_width[index - 1] = width;
            }
          ind_edge_width++;
        }
    }

  grm_args_values(subplot_args, "series", "A", &current_series);
  wfac = 0.9 * bar_width;
  while (*current_series != NULL)
    {
      /* Init */
      int inner_series_index;
      double *y = NULL;
      unsigned int y_length = 0;
      grm_args_t **inner_series = NULL;
      unsigned int inner_series_length = 0;
      int *c = NULL;
      unsigned int c_length;
      double *c_rgb = NULL;
      unsigned int c_rgb_length;
      char **ylabels = NULL;
      unsigned int ylabels_left = 0;
      unsigned int ylabels_length = 0;
      unsigned int y_lightness_to_get = 0;
      unsigned char rgb[sizeof(int)];
      int use_y_notations_from_inner_series = 1;
      double Y;
      int color;
      /* Style Varianz */
      double pos_vertical_change = 0;
      double neg_vertical_change = 0;
      double x1, x2, y1, y2;

      auto subGroup = global_render->createGroup("bar_series");
      group->append(subGroup);

      grm_args_values(*current_series, "edge_color", "ddd", &edge_color_rgb[0], &edge_color_rgb[1], &edge_color_rgb[2]);
      grm_args_values(*current_series, "edge_color", "i", &edge_color);
      global_render->setTextAlign(subGroup, 2, 3);
      global_render->selectClipXForm(subGroup, 1);

      if (edge_color_rgb[0] != -1)
        {
          for (i = 0; i < 3; i++)
            {
              cleanup_and_set_error_if((edge_color_rgb[i] > 1 || edge_color_rgb[i] < 0), ERROR_PLOT_OUT_OF_RANGE);
            }
        }
      grm_args_values(*current_series, "edge_width", "d", &edge_width);

      grm_args_first_value(*current_series, "c", "I", &c, &c_length);
      grm_args_first_value(*current_series, "c", "D", &c_rgb, &c_rgb_length);
      grm_args_first_value(*current_series, "ylabels", "S", &ylabels, &ylabels_length);
      ylabels_left = ylabels_length;
      y_lightness_to_get = ylabels_length;

      cleanup_and_set_error_if(
          !(grm_args_first_value(*current_series, "y", "D", &y, &y_length) ||
            (grm_args_first_value(*current_series, "inner_series", "A", &inner_series, &inner_series_length))),
          ERROR_PLOT_MISSING_DATA);
      cleanup_and_set_error_if(strcmp(style, "lined") && inner_series != NULL, ERROR_UNSUPPORTED_OPERATION);
      cleanup_and_set_error_if(y != NULL && inner_series != NULL, ERROR_PLOT_INCOMPATIBLE_ARGUMENTS);
      if (c != NULL)
        {
          cleanup_and_set_error_if((c_length < y_length) && (c_length < inner_series_length),
                                   ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
        }
      if (c_rgb != NULL)
        {
          cleanup_and_set_error_if((c_rgb_length < y_length * 3) && (c_rgb_length < inner_series_length * 3),
                                   ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
          for (i = 0; i < y_length * 3; i++)
            {
              cleanup_and_set_error_if((c_rgb[i] > 1 || c_rgb[i] < 0), ERROR_PLOT_OUT_OF_RANGE);
            }
        }
      if (ylabels != NULL)
        {
          y_lightness = static_cast<double *>(malloc(sizeof(double) * y_lightness_to_get));
          use_y_notations_from_inner_series = 0;
        }

      global_render->setFillIntStyle(subGroup, 1);
      global_render->setFillColorInd(subGroup, bar_color);

      if (bar_color_rgb[0] != -1)
        {
          global_render->setColorRep(subGroup, color_save_spot, bar_color_rgb[0], bar_color_rgb[1], bar_color_rgb[2]);
          bar_color = color_save_spot;
          global_render->setFillColorInd(subGroup, bar_color);
        }
      /* Draw Bar */
      for (i = 0; i < y_length; i++)
        {
          y1 = 0;
          y2 = y[i];

          if (strcmp(style, "default") == 0)
            {
              x1 = i + 1 - 0.5 * bar_width;
              x2 = i + 1 + 0.5 * bar_width;
            }
          else if (strcmp(style, "stacked") == 0)
            {
              x1 = series_index + 1 - 0.5 * bar_width;
              x2 = series_index + 1 + 0.5 * bar_width;
              if (y[i] > 0)
                {
                  y1 = 0 + pos_vertical_change;
                  y2 = y[i] + pos_vertical_change;
                  pos_vertical_change += y[i];
                }
              else
                {
                  y1 = 0 + neg_vertical_change;
                  y2 = y[i] + neg_vertical_change;
                  neg_vertical_change += y[i];
                }
            }
          else if (strcmp(style, "lined") == 0)
            {
              bar_width = wfac / y_length;
              x1 = series_index + 1 - 0.5 * wfac + bar_width * i;
              x2 = series_index + 1 - 0.5 * wfac + bar_width + bar_width * i;
            }


          auto temp = global_render->createFillRect(x1, x2, y1, y2);
          subGroup->append(temp);

          if (strcmp(style, "default") != 0)
            {
              int color_index = i % len_std_colors;
              global_render->setFillColorInd(temp, std_colors[color_index]);
            }
          if (c != NULL)
            {
              global_render->setFillColorInd(temp, c[i]);
            }
          else if (c_rgb != NULL)
            {
              global_render->setColorRep(temp, color_save_spot, c_rgb[i * 3], c_rgb[i * 3 + 1], c_rgb[i * 3 + 2]);
              global_render->setFillColorInd(temp, color_save_spot);
            }
          else if (change_bar_color)
            {
              if (*pos_ind_bar_color[i] != -1)
                {
                  global_render->setColorRep(temp, color_save_spot, pos_ind_bar_color[i][0], pos_ind_bar_color[i][1],
                                             pos_ind_bar_color[i][2]);
                  global_render->setFillColorInd(temp, color_save_spot);
                }
            }
          if (y_lightness_to_get > 0 && y_lightness != NULL)
            {
              if (temp->hasAttribute("fillcolorind"))
                {
                  color = (int)temp->getAttribute("fillcolorind");
                }
              else if (subGroup->hasAttribute("fillcolorind"))
                {
                  color = (int)subGroup->getAttribute("fillcolorind");
                }

              gr_inqcolor(color, (int *)rgb);
              Y = (0.2126729 * rgb[0] / 255 + 0.7151522 * rgb[1] / 255 + 0.0721750 * rgb[2] / 255);
              y_lightness[i] = 116 * pow(Y / 100, 1.0 / 3) - 16;
              --y_lightness_to_get;
            }
        }

      pos_vertical_change = 0;
      neg_vertical_change = 0;
      /* Draw Edge */
      for (i = 0; i < y_length; i++)
        {
          if (strcmp(style, "default") == 0)
            {
              x1 = i + 1 - 0.5 * bar_width;
              x2 = i + 1 + 0.5 * bar_width;
              y1 = 0;
              y2 = y[i];
            }
          if (strcmp(style, "stacked") == 0)
            {
              x1 = series_index + 1 - 0.5 * bar_width;
              x2 = series_index + 1 + 0.5 * bar_width;
              if (y[i] > 0)
                {
                  y1 = 0 + pos_vertical_change;
                  y2 = y[i] + pos_vertical_change;
                  pos_vertical_change += y[i];
                }
              else
                {
                  y1 = 0 + neg_vertical_change;
                  y2 = y[i] + neg_vertical_change;
                  neg_vertical_change += y[i];
                }
            }
          if (strcmp(style, "lined") == 0)
            {
              bar_width = wfac / y_length;
              x1 = series_index + 1 - 0.5 * wfac + bar_width * i;
              x2 = series_index + 1 - 0.5 * wfac + bar_width + bar_width * i;
              y1 = 0;
              y2 = y[i];
            }

          auto temp = global_render->createDrawRect(x1, x2, y1, y2);
          subGroup->append(temp);

          global_render->setLineWidth(temp, edge_width);
          if (change_edge_width)
            {
              if (pos_ind_edge_width[i] != -1)
                {
                  double width = pos_ind_edge_width[i];
                  global_render->setLineWidth(temp, width);
                }
            }
          if (edge_color_rgb[0] != -1)
            {
              global_render->setColorRep(temp, color_save_spot, edge_color_rgb[0], edge_color_rgb[1],
                                         edge_color_rgb[2]);
              edge_color = color_save_spot;
            }
          global_render->setLineColorInd(temp, edge_color);
          if (change_edge_color)
            {
              if (*pos_ind_edge_color[i] != -1)
                {
                  global_render->setColorRep(temp, color_save_spot, pos_ind_edge_color[i][0], pos_ind_edge_color[i][1],
                                             pos_ind_edge_color[i][2]);
                  global_render->setLineColorInd(temp, color_save_spot);
                }
            }
        }

      pos_vertical_change = 0;
      neg_vertical_change = 0;
      double width, height, available_width, available_height, x_text, y_text;
      double tbx[4], tby[4];
      /* Draw ylabels */

      if (ylabels != NULL)
        {
          for (i = 0; i < y_length; i++)
            {
              if (strcmp(style, "default") == 0)
                {
                  x1 = i + 1 - 0.5 * bar_width;
                  x2 = i + 1 + 0.5 * bar_width;
                  y1 = 0;
                  y2 = y[i];
                }
              if (strcmp(style, "stacked") == 0)
                {
                  x1 = series_index + 1 - 0.5 * bar_width;
                  x2 = series_index + 1 + 0.5 * bar_width;
                  if (y[i] > 0)
                    {
                      y1 = 0 + pos_vertical_change;
                      y2 = y[i] + pos_vertical_change;
                      pos_vertical_change += y[i];
                    }
                  else
                    {
                      y1 = 0 + neg_vertical_change;
                      y2 = y[i] + neg_vertical_change;
                      neg_vertical_change += y[i];
                    }
                }
              if (strcmp(style, "lined") == 0)
                {
                  bar_width = wfac / y_length;
                  x1 = series_index + 1 - 0.5 * wfac + bar_width * i;
                  x2 = series_index + 1 - 0.5 * wfac + bar_width + bar_width * i;
                  y1 = 0;
                  y2 = y[i];
                }

              if (ylabels_left > 0)
                {
                  available_width = x2 - x1;
                  available_height = y2 - y1;
                  x_text = (x1 + x2) / 2;
                  y_text = (y1 + y2) / 2;
                  gr_settextalign(2, 3);
                  gr_setcharup(0.0, 1.0);

                  std::shared_ptr<GR::Element> temp = global_render->createText(x_text, y_text, ylabels[i], WC);
                  global_render->setTextWidthAndHeight(temp, available_width, available_height);
                  if (y_lightness[i] < 0.4)
                    {
                      global_render->setTextColorInd(temp, 0);
                    }
                  else
                    {
                      global_render->setTextColorInd(temp, 1);
                    }
                  subGroup->append(temp);
                  --ylabels_left;
                }
            }
        }

      /* Draw inner_series */
      for (inner_series_index = 0; inner_series_index < inner_series_length; inner_series_index++)
        {
          /* Draw bars from inner_series */
          int *inner_c = NULL;
          unsigned int inner_c_length;
          double *inner_c_rgb = NULL;
          unsigned int inner_c_rgb_length;
          auto inner_group = global_render->createGroup("bar_inner_series");
          subGroup->append(inner_group);

          global_render->setFillColorInd(inner_group, std_colors[inner_series_index % len_std_colors]);
          grm_args_first_value(inner_series[inner_series_index], "y", "D", &y, &y_length);
          bar_width = wfac / fixed_y_length;
          if (c != NULL)
            {
              global_render->setFillColorInd(inner_group, c[inner_series_index]);
            }
          else if (c_rgb != NULL)
            {
              global_render->setColorRep(inner_group, color_save_spot, c_rgb[inner_series_index * 3],
                                         c_rgb[inner_series_index * 3 + 1], c_rgb[inner_series_index * 3 + 2]);
              global_render->setFillColorInd(inner_group, color_save_spot);
            }
          else if (change_bar_color)
            {
              if (*pos_ind_bar_color[inner_series_index] != -1)
                {
                  global_render->setColorRep(inner_group, color_save_spot, pos_ind_bar_color[inner_series_index][0],
                                             pos_ind_bar_color[inner_series_index][1],
                                             pos_ind_bar_color[inner_series_index][2]);
                  global_render->setFillColorInd(inner_group, color_save_spot);
                }
            }
          if (grm_args_first_value(inner_series[inner_series_index], "c", "I", &inner_c, &inner_c_length))
            {
              cleanup_and_set_error_if(inner_c_length != y_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
            }
          if (grm_args_first_value(inner_series[inner_series_index], "c", "D", &inner_c_rgb, &inner_c_rgb_length))
            {
              cleanup_and_set_error_if((inner_c_rgb_length < y_length * 3), ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
              for (i = 0; i < y_length * 3; i++)
                {
                  cleanup_and_set_error_if((inner_c_rgb[i] > 1 || inner_c_rgb[i] < 0), ERROR_PLOT_OUT_OF_RANGE);
                }
            }
          if (ylabels == NULL)
            {
              grm_args_first_value(inner_series[inner_series_index], "ylabels", "nS", &ylabels, &ylabels_length);
              ylabels_left = ylabels_length;
              y_lightness_to_get = ylabels_length;
              y_lightness = static_cast<double *>(malloc(sizeof(double) * y_lightness_to_get));
            }
          for (i = 0; i < y_length; i++)
            {
              x1 = series_index + 1 - 0.5 * wfac + bar_width * inner_series_index;
              x2 = series_index + 1 - 0.5 * wfac + bar_width + bar_width * inner_series_index;
              if (y[i] > 0)
                {
                  y1 = 0 + pos_vertical_change;
                  y2 = y[i] + pos_vertical_change;
                  pos_vertical_change += y[i];
                }
              else
                {
                  y1 = 0 + neg_vertical_change;
                  y2 = y[i] + neg_vertical_change;
                  neg_vertical_change += y[i];
                }
              auto temp = global_render->createFillRect(x1, x2, y1, y2);
              inner_group->append(temp);

              if (inner_c != NULL)
                {
                  global_render->setFillColorInd(temp, inner_c[i]);
                }
              if (inner_c_rgb != NULL)
                {
                  global_render->setColorRep(temp, color_save_spot, inner_c_rgb[i * 3], inner_c_rgb[i * 3 + 1],
                                             inner_c_rgb[i * 3 + 2]);
                  global_render->setFillColorInd(temp, color_save_spot);
                }

              if (y_lightness_to_get > 0 && y_lightness != NULL)
                {
                  if (temp->hasAttribute("fillcolorind"))
                    {
                      color = (int)temp->getAttribute("fillcolorind");
                    }
                  else if (subGroup->hasAttribute("fillcolorind"))
                    {
                      color = (int)inner_group->getAttribute("fillcolorind");
                    }
                  gr_inqcolor(color, (int *)rgb);
                  Y = (0.2126729 * rgb[0] / 255 + 0.7151522 * rgb[1] / 255 + 0.0721750 * rgb[2] / 255);
                  y_lightness[ylabels_length - y_lightness_to_get] = 116 * pow(Y / 100, 1.0 / 3) - 16;
                  --y_lightness_to_get;
                }
            }
          pos_vertical_change = 0;
          neg_vertical_change = 0;


          auto inner_edges = global_render->createGroup("bar_inner_edges");
          inner_group->append(inner_edges);

          /* Draw edges from inner_series */
          global_render->setLineWidth(inner_edges, edge_width);
          if (change_edge_width)
            {
              if (pos_ind_edge_width[inner_series_index] != -1)
                {
                  double width = pos_ind_edge_width[inner_series_index];
                  global_render->setLineWidth(inner_edges, width);
                }
            }
          if (edge_color_rgb[0] != -1)
            {
              global_render->setColorRep(inner_edges, color_save_spot, edge_color_rgb[0], edge_color_rgb[1],
                                         edge_color_rgb[2]);
              edge_color = color_save_spot;
            }
          global_render->setLineColorInd(inner_edges, edge_color);
          if (change_edge_color)
            {
              if (*pos_ind_edge_color[inner_series_index] != -1)
                {
                  global_render->setColorRep(inner_edges, color_save_spot, pos_ind_edge_color[inner_series_index][0],
                                             pos_ind_edge_color[inner_series_index][1],
                                             pos_ind_edge_color[inner_series_index][2]);
                  global_render->setLineColorInd(inner_edges, color_save_spot);
                }
            }

          for (i = 0; i < y_length; i++)
            {
              x1 = series_index + 1 - 0.5 * wfac + bar_width * inner_series_index;
              x2 = series_index + 1 - 0.5 * wfac + bar_width + bar_width * inner_series_index;
              if (y[i] > 0)
                {
                  y1 = 0 + pos_vertical_change;
                  y2 = y[i] + pos_vertical_change;
                  pos_vertical_change += y[i];
                }
              else
                {
                  y1 = 0 + neg_vertical_change;
                  y2 = y[i] + neg_vertical_change;
                  neg_vertical_change += y[i];
                }
              auto temp = global_render->createDrawRect(x1, x2, y1, y2);
              inner_edges->append(temp);
              global_render->setFillColorInd(temp, std_colors[inner_series_index % len_std_colors]);
            }
          pos_vertical_change = 0;
          neg_vertical_change = 0;

          /* Draw ynotations from inner_series */
          if (ylabels != NULL)
            {
              for (i = 0; i < y_length; i++)
                {
                  x1 = series_index + 1 - 0.5 * wfac + bar_width * inner_series_index;
                  x2 = series_index + 1 - 0.5 * wfac + bar_width + bar_width * inner_series_index;
                  if (y[i] > 0)
                    {
                      y1 = 0 + pos_vertical_change;
                      y2 = y[i] + pos_vertical_change;
                      pos_vertical_change += y[i];
                    }
                  else
                    {
                      y1 = 0 + neg_vertical_change;
                      y2 = y[i] + neg_vertical_change;
                      neg_vertical_change += y[i];
                    }

                  if (ylabels_left > 0)
                    {
                      gr_wctondc(&x1, &y1);
                      gr_wctondc(&x2, &y2);
                      available_width = x2 - x1;
                      available_height = y2 - y1;
                      x_text = (x1 + x2) / 2;
                      y_text = (y1 + y2) / 2;
                      gr_wctondc(&x1, &x2);
                      auto temp = global_render->createText(x1, x2, ylabels[ylabels_length - ylabels_left]);
                      inner_group->append(temp);

                      if (y_lightness[ylabels_length - ylabels_left] < 0.4)
                        {

                          global_render->setTextColorInd(temp, 0);
                        }
                      else
                        {
                          global_render->setTextColorInd(temp, 1);
                        }
                      // TODO: Respect `textalign` and `charup` in the graphics tree
                      gr_settextalign(2, 3);
                      gr_setcharup(0.0, 1.0);
                      gr_inqtextext(x_text, y_text, ylabels[ylabels_length - ylabels_left], tbx, tby);
                      logger((stderr,
                              "ylabel: \"%s\", textext_x: (%lf, %lf, %lf, %lf), textext_y: (%lf, %lf, %lf, %lf)\n",
                              ylabels[ylabels_length - ylabels_left], tbx[0], tbx[1], tbx[2], tbx[3], tby[0], tby[1],
                              tby[2], tby[3]));
                      gr_wctondc(&tbx[0], &tby[0]);
                      gr_wctondc(&tbx[2], &tby[2]);
                      width = tbx[2] - tbx[0];
                      height = tby[2] - tby[0];
                      logger((stderr, "width: %lf, available_width: %lf\n", width, available_width));
                      logger((stderr, "height: %lf, available_height: %lf\n", height, available_height));
                      if (width < available_width && height < available_height)
                        {
                          /*
                           * TODO: Respect `charup` in the graphics tree
                           * gr_setcharup(0.0, 1.0);
                           */
                          auto temp = global_render->createText(x_text, y_text, ylabels[ylabels_length - ylabels_left]);
                          subGroup->append(temp);
                        }
                      else if (height < available_width && width < available_height)
                        {
                          /*
                           * TODO: Respect `charup` in the graphics tree
                           * gr_setcharup(-1.0, 0.0);
                           */
                          auto temp = global_render->createText(x_text, y_text, ylabels[ylabels_length - ylabels_left]);
                          subGroup->append(temp);
                          // TODO: Remove the following line when `charup` is implemented in the graphics tree
                          gr_setcharup(0.0, 1.0);
                        }
                      --ylabels_left;
                    }
                }
            }
          y_length = 0;
          pos_vertical_change = 0;
          neg_vertical_change = 0;

          if (use_y_notations_from_inner_series)
            {
              free(y_lightness);
              y_lightness = NULL;
              ylabels = NULL;
            }
        }

      if (y_lightness != NULL)
        {
          free(y_lightness);
          y_lightness = NULL;
        }
      series_index++;
      ++current_series;
    }
  gr_restorestate();


cleanup:
  if (pos_ind_bar_color != NULL)
    {
      free(pos_ind_bar_color);
    }
  if (pos_ind_edge_color != NULL)
    {
      free(pos_ind_edge_color);
    }
  if (pos_ind_edge_width != NULL)
    {
      free(pos_ind_edge_width);
    }
  if (y_lightness != NULL)
    {
      free(y_lightness);
    }
  return error;
}

err_t plot_contour(grm_args_t *subplot_args)
{
  double z_min, z_max;
  int num_levels;
  double *h;
  double *gridit_x = NULL, *gridit_y = NULL, *gridit_z = NULL;
  grm_args_t **current_series;
  int i;
  err_t error = ERROR_NONE;

  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  group->setAttribute("name", "contour");

  grm_args_values(subplot_args, "_zlim", "dd", &z_min, &z_max);
  gr_setprojectiontype(0);
  gr_setspace(z_min, z_max, 0, 90);
  grm_args_values(subplot_args, "levels", "i", &num_levels);
  h = static_cast<double *>(malloc(num_levels * sizeof(double)));
  if (h == NULL)
    {
      debug_print_malloc_error();
      error = ERROR_MALLOC;
      goto cleanup;
    }
  grm_args_values(subplot_args, "series", "A", &current_series);
  while (*current_series != NULL)
    {
      double *x, *y, *z;
      unsigned int x_length, y_length, z_length;
      grm_args_first_value(*current_series, "x", "D", &x, &x_length);
      grm_args_first_value(*current_series, "y", "D", &y, &y_length);
      grm_args_first_value(*current_series, "z", "D", &z, &z_length);
      auto subGroup = global_render->createGroup("contour_series");
      group->append(subGroup);
      if (x_length == y_length && x_length == z_length)
        {
          if (gridit_x == NULL)
            {
              gridit_x = static_cast<double *>(malloc(PLOT_CONTOUR_GRIDIT_N * sizeof(double)));
              gridit_y = static_cast<double *>(malloc(PLOT_CONTOUR_GRIDIT_N * sizeof(double)));
              gridit_z = static_cast<double *>(malloc(PLOT_CONTOUR_GRIDIT_N * PLOT_CONTOUR_GRIDIT_N * sizeof(double)));
              if (gridit_x == NULL || gridit_y == NULL || gridit_z == NULL)
                {
                  debug_print_malloc_error();
                  error = ERROR_MALLOC;
                  goto cleanup;
                }
            }
          gr_gridit(x_length, x, y, z, PLOT_CONTOUR_GRIDIT_N, PLOT_CONTOUR_GRIDIT_N, gridit_x, gridit_y, gridit_z);
          for (i = 0; i < PLOT_CONTOUR_GRIDIT_N * PLOT_CONTOUR_GRIDIT_N; i++)
            {
              z_min = grm_min(gridit_z[i], z_min);
              z_max = grm_max(gridit_z[i], z_max);
            }
          for (i = 0; i < num_levels; ++i)
            {
              h[i] = z_min + (1.0 * i) / num_levels * (z_max - z_min);
            }

          int id = (int)global_root->getAttribute("id");
          global_root->setAttribute("id", id + 1);
          std::string str = std::to_string(id);

          std::vector<double> px_vec = std::vector<double>(gridit_x, gridit_x + PLOT_CONTOUR_GRIDIT_N);
          std::vector<double> py_vec = std::vector<double>(gridit_y, gridit_y + PLOT_CONTOUR_GRIDIT_N);
          std::vector<double> h_vec = std::vector<double>(h, h + num_levels);
          std::vector<double> pz_vec =
              std::vector<double>(gridit_z, gridit_z + PLOT_CONTOUR_GRIDIT_N * PLOT_CONTOUR_GRIDIT_N);

          auto contour = global_render->createContour("px" + str, px_vec, "py" + str, py_vec, "h" + str, h_vec,
                                                      "pz" + str, pz_vec, 1000);
          subGroup->append(contour);
        }
      else
        {
          if (x_length * y_length != z_length)
            {
              error = ERROR_PLOT_COMPONENT_LENGTH_MISMATCH;
              goto cleanup;
            }
          for (i = 0; i < num_levels; ++i)
            {
              h[i] = z_min + (1.0 * i) / num_levels * (z_max - z_min);
            }

          int id = (int)global_root->getAttribute("id");
          global_root->setAttribute("id", id + 1);
          std::string str = std::to_string(id);

          std::vector<double> px_vec = std::vector<double>(x, x + x_length);
          std::vector<double> py_vec = std::vector<double>(y, y + y_length);
          std::vector<double> h_vec = std::vector<double>(h, h + num_levels);
          std::vector<double> pz_vec = std::vector<double>(z, z + x_length * y_length);

          auto contour = global_render->createContour("px" + str, px_vec, "py" + str, py_vec, "h" + str, h_vec,
                                                      "pz" + str, pz_vec, 1000);
          subGroup->append(contour);
        }
      ++current_series;
    }
  if ((error = plot_draw_colorbar(subplot_args, 0.0, num_levels)) != ERROR_NONE)
    {
      goto cleanup;
    }

cleanup:
  free(h);
  free(gridit_x);
  free(gridit_y);
  free(gridit_z);

  return error;
}

err_t plot_contourf(grm_args_t *subplot_args)
{
  double z_min, z_max;
  int num_levels, scale;
  double *h;
  double *gridit_x = NULL, *gridit_y = NULL, *gridit_z = NULL;
  grm_args_t **current_series;
  int i;
  err_t error = ERROR_NONE;

  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  group->setAttribute("name", "contourf");

  grm_args_values(subplot_args, "_zlim", "dd", &z_min, &z_max);
  global_render->setProjectionType(group, 0);
  global_render->setSpace(group, z_min, z_max, 0, 90);
  grm_args_values(subplot_args, "levels", "i", &num_levels);
  h = static_cast<double *>(malloc(num_levels * sizeof(double)));
  if (h == NULL)
    {
      debug_print_malloc_error();
      error = ERROR_MALLOC;
      goto cleanup;
    }
  grm_args_values(subplot_args, "scale", "i", &scale);
  global_render->setScale(group, scale);
  grm_args_values(subplot_args, "series", "A", &current_series);
  while (*current_series != NULL)
    {
      double *x, *y, *z;
      unsigned int x_length, y_length, z_length;
      auto subGroup = global_render->createGroup("contourf_series");
      group->append(subGroup);
      grm_args_first_value(*current_series, "x", "D", &x, &x_length);
      grm_args_first_value(*current_series, "y", "D", &y, &y_length);
      grm_args_first_value(*current_series, "z", "D", &z, &z_length);
      if ((error = plot_draw_colorbar(subplot_args, 0.0, num_levels)) != ERROR_NONE)
        {
          goto cleanup;
        }
      global_render->setLineColorInd(subGroup, 1);
      if (x_length == y_length && x_length == z_length)
        {
          if (gridit_x == NULL)
            {
              gridit_x = static_cast<double *>(malloc(PLOT_CONTOUR_GRIDIT_N * sizeof(double)));
              gridit_y = static_cast<double *>(malloc(PLOT_CONTOUR_GRIDIT_N * sizeof(double)));
              gridit_z = static_cast<double *>(malloc(PLOT_CONTOUR_GRIDIT_N * PLOT_CONTOUR_GRIDIT_N * sizeof(double)));
              if (gridit_x == NULL || gridit_y == NULL || gridit_z == NULL)
                {
                  debug_print_malloc_error();
                  error = ERROR_MALLOC;
                  goto cleanup;
                }
            }
          gr_gridit(x_length, x, y, z, PLOT_CONTOUR_GRIDIT_N, PLOT_CONTOUR_GRIDIT_N, gridit_x, gridit_y, gridit_z);
          for (i = 0; i < PLOT_CONTOUR_GRIDIT_N * PLOT_CONTOUR_GRIDIT_N; i++)
            {
              z_min = grm_min(gridit_z[i], z_min);
              z_max = grm_max(gridit_z[i], z_max);
            }
          for (i = 0; i < num_levels; ++i)
            {
              h[i] = z_min + (1.0 * i) / num_levels * (z_max - z_min);
            }
          int id = (int)global_root->getAttribute("id");
          global_root->setAttribute("id", id + 1);
          std::string str = std::to_string(id);

          std::vector<double> px_vec = std::vector<double>(gridit_x, gridit_x + PLOT_CONTOUR_GRIDIT_N);
          std::vector<double> py_vec = std::vector<double>(gridit_y, gridit_y + PLOT_CONTOUR_GRIDIT_N);
          std::vector<double> h_vec = std::vector<double>(h, h + num_levels);
          std::vector<double> pz_vec =
              std::vector<double>(gridit_z, gridit_z + PLOT_CONTOUR_GRIDIT_N * PLOT_CONTOUR_GRIDIT_N);

          auto contour = global_render->createContourf("px" + str, px_vec, "py" + str, py_vec, "h" + str, h_vec,
                                                       "pz" + str, pz_vec, 1000);
          subGroup->append(contour);
        }
      else
        {
          if (x_length * y_length != z_length)
            {
              error = ERROR_PLOT_COMPONENT_LENGTH_MISMATCH;
              goto cleanup;
            }
          for (i = 0; i < num_levels; ++i)
            {
              h[i] = z_min + (1.0 * i) / num_levels * (z_max - z_min);
            }
          int id = (int)global_root->getAttribute("id");
          global_root->setAttribute("id", id + 1);
          std::string str = std::to_string(id);

          std::vector<double> px_vec = std::vector<double>(x, x + x_length);
          std::vector<double> py_vec = std::vector<double>(y, y + y_length);
          std::vector<double> h_vec = std::vector<double>(h, h + num_levels);
          std::vector<double> pz_vec = std::vector<double>(z, z + x_length * y_length);

          auto contour = global_render->createContourf("px" + str, px_vec, "py" + str, py_vec, "h" + str, h_vec,
                                                       "pz" + str, pz_vec, 1000);
          subGroup->append(contour);
        }
      ++current_series;
    }

cleanup:
  free(h);
  free(gridit_x);
  free(gridit_y);
  free(gridit_z);

  return error;
}

err_t plot_hexbin(grm_args_t *subplot_args)
{
  grm_args_t **current_series;

  grm_args_values(subplot_args, "series", "A", &current_series);
  while (*current_series != NULL)
    {
      double *x, *y;
      unsigned int x_length, y_length;
      int cntmax, nbins;
      return_error_if(!grm_args_first_value(*current_series, "x", "D", &x, &x_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(!grm_args_first_value(*current_series, "y", "D", &y, &y_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(x_length != y_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
      grm_args_values(*current_series, "nbins", "i", &nbins);
      cntmax = gr_hexbin(x_length, x, y, nbins);
      // TODO: how to get cntmax with dom render?

      /* TODO: return an error in the else case? */
      if (cntmax > 0)
        {
          grm_args_push(subplot_args, "_zlim", "dd", 0.0, 1.0 * cntmax);
          plot_draw_colorbar(subplot_args, 0.0, 256);
        }
      ++current_series;
    }

  return ERROR_NONE;
}

err_t plot_heatmap(grm_args_t *subplot_args)
{
  const char *kind = NULL;
  grm_args_t **current_series;
  int icmap[256], *rgba = NULL, *data = NULL, zlog = 0;
  unsigned int i, cols, rows, z_length;
  double *x = NULL, *y = NULL, *z, x_min, x_max, y_min, y_max, z_min, z_max, c_min, c_max, zv;
  err_t error = ERROR_NONE;

  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  group->setAttribute("name", "heatmap");

  grm_args_values(subplot_args, "series", "A", &current_series);
  grm_args_values(subplot_args, "kind", "s", &kind);
  grm_args_values(subplot_args, "zlog", "i", &zlog);
  while (*current_series != NULL)
    {
      int is_uniform_heatmap;
      auto subGroup = global_render->createGroup("heatmap_series");
      group->append(subGroup);
      grm_args_first_value(*current_series, "x", "D", &x, &cols);
      grm_args_first_value(*current_series, "y", "D", &y, &rows);
      is_uniform_heatmap = is_equidistant_array(cols, x) && is_equidistant_array(rows, y);
      cleanup_and_set_error_if(!is_uniform_heatmap && (x == NULL || y == NULL), ERROR_PLOT_MISSING_DATA);
      cleanup_and_set_error_if(!grm_args_first_value(*current_series, "z", "D", &z, &z_length),
                               ERROR_PLOT_MISSING_DATA);
      if (x == NULL && y == NULL)
        {
          /* If neither `x` nor `y` are given, we need more information about the shape of `z` */
          cleanup_and_set_error_if(!grm_args_values(*current_series, "z_dims", "ii", &rows, &cols),
                                   ERROR_PLOT_MISSING_DIMENSIONS);
        }
      else if (x == NULL)
        {
          cols = z_length / rows;
        }
      else if (y == NULL)
        {
          rows = z_length / cols;
        }
      if (x == NULL)
        {
          grm_args_values(*current_series, "xrange", "dd", &x_min, &x_max);
        }
      else
        {
          x_min = x[0];
          x_max = x[cols - 1];
        }
      if (x == NULL)
        {
          grm_args_values(*current_series, "yrange", "dd", &y_min, &y_max);
        }
      else
        {
          y_min = y[0];
          y_max = y[rows - 1];
        }
      grm_args_values(*current_series, "zrange", "dd", &z_min, &z_max);
      if (!grm_args_values(*current_series, "crange", "dd", &c_min, &c_max))
        {
          c_min = z_min;
          c_max = z_max;
        }

      if (zlog)
        {
          z_min = log(z_min);
          z_max = log(z_max);
          c_min = log(c_min);
          c_max = log(c_max);
        }

      if (!is_uniform_heatmap)
        {
          --cols;
          --rows;
        }
      for (i = 0; i < 256; i++)
        {
          gr_inqcolor(1000 + i, icmap + i);
        }

      data = static_cast<int *>(malloc(rows * cols * sizeof(int)));
      cleanup_and_set_error_if(data == NULL, ERROR_MALLOC);
      if (z_max > z_min)
        {
          for (i = 0; i < cols * rows; i++)
            {
              if (zlog)
                {
                  zv = log(z[i]);
                }
              else
                {
                  zv = z[i];
                }

              if (zv > z_max || zv < z_min || grm_isnan(zv))
                {
                  data[i] = -1;
                }
              else
                {
                  data[i] = (int)((zv - c_min) / (c_max - c_min) * 255);
                  if (data[i] >= 255)
                    {
                      data[i] = 255;
                    }
                  else if (data[i] < 0)
                    {
                      data[i] = 0;
                    }
                }
            }
        }
      else
        {
          for (i = 0; i < cols * rows; i++)
            {
              data[i] = 0;
            }
        }
      rgba = static_cast<int *>(malloc(rows * cols * sizeof(int)));
      cleanup_and_set_error_if(rgba == NULL, ERROR_MALLOC);
      if (is_uniform_heatmap)
        {
          for (i = 0; i < rows * cols; i++)
            {
              if (data[i] == -1)
                {
                  rgba[i] = 0;
                }
              else
                {
                  rgba[i] = (255 << 24) + icmap[data[i]];
                }
            }
          int id = (int)global_root->getAttribute("id");
          global_root->setAttribute("id", id + 1);
          std::string str = std::to_string(id);

          std::vector<int> rgba_vec = std::vector<int>(rgba, rgba + cols * rows);

          auto drawImage =
              global_render->createDrawImage(x_min, x_max, y_min, y_max, cols, rows, "rgba" + str, rgba_vec, 0);
          subGroup->append(drawImage);
        }
      else
        {
          for (i = 0; i < rows * cols; i++)
            {
              if (data[i] == -1)
                {
                  rgba[i] = 1256 + 1; /* Invalid color index -> gr_nonuniformcellarray draws a transparent rectangle */
                }
              else
                {
                  rgba[i] = data[i] + 1000;
                }
            }
          int id = (int)global_root->getAttribute("id");
          global_root->setAttribute("id", id + 1);
          std::string str = std::to_string(id);

          std::vector<int> rgba_vec = std::vector<int>(rgba, rgba + cols * rows);
          std::vector<double> x_vec = std::vector<double>(x, x + cols + 1);
          std::vector<double> y_vec = std::vector<double>(y, y + rows + 1);
          std::vector<int> color_vec = std::vector<int>(rgba, rgba + cols * rows);

          auto nuca = global_render->createNonUniformCellArray("x" + str, x_vec, "y" + str, y_vec, cols, rows, 1, 1,
                                                               cols, rows, "color" + str, color_vec);
          subGroup->append(nuca);
        }

      free(rgba);
      free(data);
      rgba = NULL;
      data = NULL;

      ++current_series;
    }

  plot_draw_colorbar(subplot_args, 0.0, 256);

cleanup:
  free(rgba);
  free(data);

  return error;
}

err_t plot_wireframe(grm_args_t *subplot_args)
{
  double *gridit_x = NULL, *gridit_y = NULL, *gridit_z = NULL;
  grm_args_t **current_series;
  err_t error = ERROR_NONE;

  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  group->setAttribute("name", "wireframe");

  grm_args_values(subplot_args, "series", "A", &current_series);
  while (*current_series != NULL)
    {
      double *x, *y, *z;
      unsigned int x_length, y_length, z_length;

      auto subGroup = global_render->createGroup("wireframe_series");
      group->append(subGroup);

      grm_args_first_value(*current_series, "x", "D", &x, &x_length);
      grm_args_first_value(*current_series, "y", "D", &y, &y_length);
      grm_args_first_value(*current_series, "z", "D", &z, &z_length);

      global_render->setFillColorInd(subGroup, 0);
      if (x_length == y_length && x_length == z_length)
        {
          if (gridit_x == NULL)
            {
              gridit_x = static_cast<double *>(malloc(PLOT_WIREFRAME_GRIDIT_N * sizeof(double)));
              gridit_y = static_cast<double *>(malloc(PLOT_WIREFRAME_GRIDIT_N * sizeof(double)));
              gridit_z =
                  static_cast<double *>(malloc(PLOT_WIREFRAME_GRIDIT_N * PLOT_WIREFRAME_GRIDIT_N * sizeof(double)));
              if (gridit_x == NULL || gridit_y == NULL || gridit_z == NULL)
                {
                  debug_print_malloc_error();
                  error = ERROR_MALLOC;
                  goto cleanup;
                }
            }
          gr_gridit(x_length, x, y, z, PLOT_WIREFRAME_GRIDIT_N, PLOT_WIREFRAME_GRIDIT_N, gridit_x, gridit_y, gridit_z);
          std::vector<double> grid_x_vec = std::vector<double>(gridit_x, gridit_x + PLOT_WIREFRAME_GRIDIT_N);
          std::vector<double> grid_y_vec = std::vector<double>(gridit_y, gridit_y + PLOT_WIREFRAME_GRIDIT_N);
          std::vector<double> grid_z_vec = std::vector<double>(gridit_z, gridit_z + PLOT_WIREFRAME_GRIDIT_N);
          int id_int = static_cast<int>(global_root->getAttribute("id"));
          global_root->setAttribute("id", ++id_int);
          std::string id = std::to_string(id_int);

          auto temp = global_render->createSurface("x" + id, grid_x_vec, "y" + id, grid_y_vec, "z" + id, grid_z_vec,
                                                   GR_OPTION_FILLED_MESH);
          subGroup->append(temp);
        }
      else
        {
          if (x_length * y_length != z_length)
            {
              error = ERROR_PLOT_COMPONENT_LENGTH_MISMATCH;
              goto cleanup;
            }
          std::vector<double> x_vec = std::vector<double>(x, x + x_length);
          std::vector<double> y_vec = std::vector<double>(y, y + y_length);
          std::vector<double> z_vec = std::vector<double>(z, z + z_length);
          int id_int = static_cast<int>(global_root->getAttribute("id"));
          global_root->setAttribute("id", ++id_int);
          std::string id = std::to_string(id_int);

          auto temp =
              global_render->createSurface("x" + id, x_vec, "y" + id, y_vec, "z" + id, z_vec, GR_OPTION_FILLED_MESH);
          subGroup->append(temp);
        }
      ++current_series;
    }
  plot_draw_axes(subplot_args, 2);

cleanup:
  free(gridit_x);
  free(gridit_y);
  free(gridit_z);

  return error;
}

err_t plot_surface(grm_args_t *subplot_args)
{
  double *gridit_x = NULL, *gridit_y = NULL, *gridit_z = NULL;
  grm_args_t **current_series;
  err_t error = ERROR_NONE;

  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  group->setAttribute("name", "surface");

  grm_args_values(subplot_args, "series", "A", &current_series);
  while (*current_series != NULL)
    {
      double *x, *y, *z;
      unsigned int x_length, y_length, z_length;
      auto subGroup = global_render->createGroup("surface_series");
      group->append(subGroup);
      grm_args_first_value(*current_series, "x", "D", &x, &x_length);
      grm_args_first_value(*current_series, "y", "D", &y, &y_length);
      grm_args_first_value(*current_series, "z", "D", &z, &z_length);
      /* TODO: add support for GR3 */
      if (x_length == y_length && x_length == z_length)
        {
          if (gridit_x == NULL)
            {
              gridit_x = static_cast<double *>(malloc(PLOT_SURFACE_GRIDIT_N * sizeof(double)));
              gridit_y = static_cast<double *>(malloc(PLOT_SURFACE_GRIDIT_N * sizeof(double)));
              gridit_z = static_cast<double *>(malloc(PLOT_SURFACE_GRIDIT_N * PLOT_SURFACE_GRIDIT_N * sizeof(double)));
              if (gridit_x == NULL || gridit_y == NULL || gridit_z == NULL)
                {
                  debug_print_malloc_error();
                  error = ERROR_MALLOC;
                  goto cleanup;
                }
            }
          gr_gridit(x_length, x, y, z, PLOT_SURFACE_GRIDIT_N, PLOT_SURFACE_GRIDIT_N, gridit_x, gridit_y, gridit_z);

          std::vector<double> grid_x_vec = std::vector<double>(gridit_x, gridit_x + PLOT_WIREFRAME_GRIDIT_N);
          std::vector<double> grid_y_vec = std::vector<double>(gridit_y, gridit_y + PLOT_WIREFRAME_GRIDIT_N);
          std::vector<double> grid_z_vec = std::vector<double>(gridit_z, gridit_z + PLOT_WIREFRAME_GRIDIT_N);
          int id_int = static_cast<int>(global_root->getAttribute("id"));
          global_root->setAttribute("id", ++id_int);
          std::string id = std::to_string(id_int);

          auto temp = global_render->createSurface("x" + id, grid_x_vec, "y" + id, grid_y_vec, "z" + id, grid_z_vec,
                                                   GR_OPTION_COLORED_MESH);

          subGroup->append(temp);
        }
      else
        {
          if (x_length * y_length != z_length)
            {
              error = ERROR_PLOT_COMPONENT_LENGTH_MISMATCH;
              goto cleanup;
            }
          std::vector<double> x_vec = std::vector<double>(x, x + x_length);
          std::vector<double> y_vec = std::vector<double>(y, y + y_length);
          std::vector<double> z_vec = std::vector<double>(z, z + z_length);
          int id_int = static_cast<int>(global_root->getAttribute("id"));
          global_root->setAttribute("id", ++id_int);
          std::string id = std::to_string(id_int);

          auto temp =
              global_render->createSurface("x" + id, x_vec, "y" + id, y_vec, "z" + id, z_vec, GR_OPTION_COLORED_MESH);
          subGroup->append(temp);
        }
      ++current_series;
    }
  plot_draw_axes(subplot_args, 2);
  plot_draw_colorbar(subplot_args, 0.05, 256);

cleanup:
  free(gridit_x);
  free(gridit_y);
  free(gridit_z);

  return error;
}

err_t plot_plot3(grm_args_t *subplot_args)
{
  grm_args_t **current_series;

  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  group->setAttribute("name", "plot3");
  grm_args_values(subplot_args, "series", "A", &current_series);
  while (*current_series != NULL)
    {
      double *x, *y, *z;
      unsigned int x_length, y_length, z_length;
      auto subGroup = global_render->createGroup("plot3_series");
      group->append(subGroup);
      return_error_if(!grm_args_first_value(*current_series, "x", "D", &x, &x_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(!grm_args_first_value(*current_series, "y", "D", &y, &y_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(!grm_args_first_value(*current_series, "z", "D", &z, &z_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(x_length != y_length || x_length != z_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);

      std::vector<double> x_vec = std::vector<double>(x, x + x_length);
      std::vector<double> y_vec = std::vector<double>(y, y + y_length);
      std::vector<double> z_vec = std::vector<double>(z, z + z_length);
      int id_int = static_cast<int>(global_root->getAttribute("id"));
      global_root->setAttribute("id", ++id_int);
      std::string id = std::to_string(id_int);

      auto temp = global_render->createPolyline3d("x" + id, x_vec, "y" + id, y_vec, "z" + id, z_vec);

      subGroup->append(temp);
      ++current_series;
    }
  plot_draw_axes(subplot_args, 2);

  return ERROR_NONE;
}

err_t plot_scatter3(grm_args_t *subplot_args)
{
  grm_args_t **current_series;
  double c_min, c_max;
  unsigned int x_length, y_length, z_length, c_length, i, c_index;
  double *x, *y, *z, *c;

  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  group->setAttribute("name", "scatter3");
  grm_args_values(subplot_args, "series", "A", &current_series);
  while (*current_series != NULL)
    {
      auto subGroup = global_render->createGroup("scatter3_series");
      group->append(subGroup);
      return_error_if(!grm_args_first_value(*current_series, "x", "D", &x, &x_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(!grm_args_first_value(*current_series, "y", "D", &y, &y_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(!grm_args_first_value(*current_series, "z", "D", &z, &z_length), ERROR_PLOT_MISSING_DATA);
      std::vector<int> markerCVec;
      return_error_if(x_length != y_length || x_length != z_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
      global_render->setMarkerType(subGroup, GKS_K_MARKERTYPE_SOLID_CIRCLE);
      if (grm_args_first_value(*current_series, "c", "D", &c, &c_length))
        {
          grm_args_values(subplot_args, "_clim", "dd", &c_min, &c_max);
          for (i = 0; i < x_length; i++)
            {
              if (i < c_length)
                {
                  c_index = 1000 + (int)(255 * (c[i] - c_min) / c_max);
                }
              else
                {
                  c_index = 989;
                }
              markerCVec.push_back(c_index);
            }
        }
      else
        {
          std::vector<double> x_vec = std::vector<double>(x, x + x_length);
          std::vector<double> y_vec = std::vector<double>(y, y + y_length);
          std::vector<double> z_vec = std::vector<double>(z, z + z_length);
          int id_int = static_cast<int>(global_root->getAttribute("id"));
          global_root->setAttribute("id", ++id_int);
          std::string id = std::to_string(id_int);

          if (markerCVec.size() > 0)
            {
              global_render->setMarkerColorInd(subGroup, "markercolorinds" + id, markerCVec);
            }
          else if (grm_args_values(*current_series, "c", "i", &c_index))
            {
              global_render->setMarkerColorInd(subGroup, c_index);
            }

          auto temp = global_render->createPolymarker3d("x" + id, x_vec, "y" + id, y_vec, "z" + id, z_vec);
          subGroup->append(temp);
        }
      ++current_series;
    }
  plot_draw_axes(subplot_args, 2);

  return ERROR_NONE;
}

err_t plot_imshow(grm_args_t *subplot_args)
{
  grm_args_t **current_series;
  double *c_data;
  double c_min, c_max;
  double *vp;
  unsigned int c_data_length, i, j, k, rows, cols;
  int *img_data;
  unsigned int *shape;
  int xflip, yflip;
  double x_min, x_max, y_min, y_max, w, h, tmp;

  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  group->setAttribute("name", "imshow");

  grm_args_values(subplot_args, "series", "A", &current_series);
  return_error_if(!grm_args_values(subplot_args, "_clim", "dd", &c_min, &c_max), ERROR_PLOT_MISSING_DATA);
  return_error_if(!grm_args_values(subplot_args, "vp", "D", &vp), ERROR_PLOT_MISSING_DATA);
  while (*current_series != NULL)
    {
      auto subGroup = global_render->createGroup("imshow_series");
      group->append(subGroup);

      return_error_if(!grm_args_first_value(*current_series, "c", "D", &c_data, &c_data_length),
                      ERROR_PLOT_MISSING_DATA);
      return_error_if(!grm_args_first_value(*current_series, "c_dims", "I", &shape, &i), ERROR_PLOT_MISSING_DATA);
      return_error_if(i != 2, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
      return_error_if(shape[0] * shape[1] != c_data_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
      rows = shape[0];
      cols = shape[1];

      img_data = static_cast<int *>(malloc(sizeof(int) * c_data_length));
      if (img_data == NULL)
        {
          debug_print_malloc_error();
          free(img_data);
          return ERROR_MALLOC;
        }

      logger((stderr, "Got min, max %lf %lf\n", c_min, c_max));
      k = 0;
      for (j = 0; j < rows; ++j)
        for (i = 0; i < cols; ++i)
          {
            img_data[k++] = 1000 + (int)grm_round((1.0 * c_data[i * rows + j] - c_min) / (c_max - c_min) * 255);
          }

      if (cols * (vp[3] - vp[2]) < rows * (vp[1] - vp[0]))
        {
          w = (double)cols / (double)rows * (vp[3] - vp[2]);
          x_min = grm_max(0.5 * (vp[0] + vp[1] - w), vp[0]);
          x_max = grm_min(0.5 * (vp[0] + vp[1] + w), vp[1]);
          y_min = vp[2];
          y_max = vp[3];
        }
      else
        {
          h = (double)rows / (double)cols * (vp[1] - vp[0]);
          x_min = vp[0];
          x_max = vp[1];
          y_min = grm_max(0.5 * (vp[3] + vp[2] - h), vp[2]);
          y_max = grm_min(0.5 * (vp[3] + vp[2] + h), vp[3]);
        }

      global_render->setSelntran(subGroup, 0);
      global_render->setScale(subGroup, 0);
      grm_args_values(subplot_args, "xflip", "i", &xflip);
      if (xflip)
        {
          tmp = x_max;
          x_max = x_min;
          x_min = tmp;
        }
      grm_args_values(subplot_args, "yflip", "i", &yflip);
      if (yflip)
        {
          tmp = y_max;
          y_max = y_min;
          y_min = tmp;
        }

      std::vector<int> img_vec = std::vector<int>(img_data, img_data + cols * rows);
      int id_int = static_cast<int>(global_root->getAttribute("id"));
      global_root->setAttribute("id", ++id_int);
      std::string id = std::to_string(id_int);

      auto temp = global_render->createCellArray(x_min, x_max, y_min, y_max, cols, rows, 1, 1, cols, rows,
                                                 "img_data" + id, img_vec);
      subGroup->append(temp);


      free(img_data);

      ++current_series;
    }

  return ERROR_NONE;
}

/*
 * Checks if the GR3_MC_DTYPE macro is set to unsigned short, as we only support this dtype in plot_isosurface.
 */
void gr3_mc_dtype_test_(void)
{
  switch (0)
    {
    case ((sizeof(GR3_MC_DTYPE) == 2) && (((GR3_MC_DTYPE)-1) > 0)):
    case 0:
      break;
    }
}

err_t plot_isosurface(grm_args_t *subplot_args)
{
  /*
   * Possible arguments to pass:
   * double[] `c` with the isodata to be drawn
   * int[3] `c_dims` shape of the c-array
   * optional double `isovalue` of the surface. All values higher or equal to isovalue are seen as inside the object,
   * all values below the isovalue are seen as outside the object.
   * optional double `rotation` in degrees
   * optional double `tilt` of the camera in degrees
   * optional double[3] `foreground_color`: color of the surface
   */
  grm_args_t **current_series;
  double *orig_data, *viewport, *temp_colors;
  unsigned int i, data_length, dims;
  unsigned int *shape;
  double c_min, c_max, isovalue, x_min, x_max, y_min, y_max, rotation, tilt;
  float foreground_colors[3], positions[3], directions[3], ups[3], scales[3];
  float r;
  int fig_width, fig_height;
  int subplot_width, subplot_height;

  unsigned short isovalue_int, *conv_data;

  int mesh; /* The mesh id of the drawn surface */
  int error;

  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  group->setAttribute("name", "isosurface");

  grm_args_values(subplot_args, "series", "A", &current_series);
  return_error_if(!grm_args_values(subplot_args, "viewport", "D", &viewport), ERROR_PLOT_MISSING_DATA);

  grm_args_values(subplot_args, "rotation", "d", &rotation);
  grm_args_values(subplot_args, "tilt", "d", &tilt);

  /* Convert to radians */
  tilt = fmod(tilt, 360.0) / 180.0 * M_PI;
  rotation = fmod(rotation, 360.0) / 180.0 * M_PI;
  logger((stderr, "tilt %lf rotation %lf\n", tilt, rotation));

  /* Calculate subplot pixel size */
  x_min = viewport[0];
  x_max = viewport[1];
  y_min = viewport[2];
  y_max = viewport[3];

  get_figure_size(NULL, &fig_width, &fig_height, NULL, NULL);
  subplot_width = (int)(grm_max(fig_width, fig_height) * (x_max - x_min));
  subplot_height = (int)(grm_max(fig_width, fig_height) * (y_max - y_min));

  logger((stderr, "viewport: (%lf, %lf, %lf, %lf)\n", x_min, x_max, y_min, y_max));
  logger((stderr, "viewport ratio: %lf\n", (x_min - x_max) / (y_min - y_max)));
  logger((stderr, "subplot size: (%d, %d)\n", subplot_width, subplot_height));
  logger((stderr, "subplot ratio: %lf\n", ((double)subplot_width / (double)subplot_height)));
  while (*current_series != NULL)
    {

      auto subGroup = global_render->createGroup("isosurface_series");
      group->append(subGroup);
      return_error_if(!grm_args_first_value(*current_series, "c", "D", &orig_data, &data_length),
                      ERROR_PLOT_MISSING_DATA);
      return_error_if(!grm_args_first_value(*current_series, "c_dims", "I", &shape, &dims), ERROR_PLOT_MISSING_DATA);
      return_error_if(dims != 3, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
      return_error_if(shape[0] * shape[1] * shape[2] != data_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
      return_error_if(data_length <= 0, ERROR_PLOT_MISSING_DATA);

      /* Capture isovalue, rotation and tilt, also default values */
      isovalue = 0.5;
      foreground_colors[0] = 0.0;
      foreground_colors[1] = 0.5;
      foreground_colors[2] = 0.8;

      grm_args_values(*current_series, "isovalue", "d", &isovalue);
      /* We need to convert the double values to floats, as the gr3_drawmesh expects floats, but an argument can only
       * contain doubles. */
      if (grm_args_first_value(*current_series, "foreground_color", "D", &temp_colors, &i))
        {
          return_error_if(i != 3, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
          while (i-- > 0)
            {
              foreground_colors[i] = (float)temp_colors[i];
            }
        }
      logger((stderr, "Colors; %f %f %f\n", foreground_colors[0], foreground_colors[1], foreground_colors[2]));

      /* Check if any value is finite in array, also calculation of real min and max */
      c_min = c_max = *orig_data;
      for (i = 0; i < data_length; ++i)
        {
          if (isfinite(orig_data[i]))
            {
              if (grm_isnan(c_min) || c_min > orig_data[i])
                {
                  c_min = orig_data[i];
                }
              if (grm_isnan(c_max) || c_max < orig_data[i])
                {
                  c_max = orig_data[i];
                }
            }
        }
      return_error_if(c_min == c_max || !isfinite(c_min) || !isfinite(c_max), ERROR_PLOT_MISSING_DATA);

      /* Calculate isovalue in ushort, and translate the original data into ushorts */
      isovalue_int = (unsigned short)((isovalue - c_min) / (c_max - c_min) * USHRT_MAX);

      logger((stderr, "c_min %lf c_max %lf isovalue_int %hu\n ", c_min, c_max, isovalue_int));
      conv_data = static_cast<unsigned short *>(malloc(sizeof(unsigned short) * data_length));
      if (conv_data == NULL)
        {
          debug_print_malloc_error();
          free(conv_data);
          return ERROR_MALLOC;
        }

      for (i = 0; i < data_length; ++i)
        {
          if (grm_isnan(orig_data[i]) || orig_data[i] < c_min)
            {
              conv_data[i] = 0;
            }
          else if (orig_data[i] > c_max)
            {
              conv_data[i] = USHRT_MAX;
            }
          else
            {
              conv_data[i] = (unsigned short)((orig_data[i] - c_min) / (c_max - c_min) * USHRT_MAX);
            }
        }

      global_render->setSelntran(subGroup, 0);
      subGroup->append(global_render->createGR3Clear());

      error = gr3_createisosurfacemesh(&mesh, conv_data, isovalue_int,

                                       /* dim_x */ shape[0],
                                       /* dim_y */ shape[1],
                                       /* dim_z */ shape[2],

                                       /* stride_x */ shape[1] * shape[2],
                                       /* stride_y */ shape[2],
                                       /* stride_z */ 1,

                                       /* step_x */ 2.f / (shape[0] - 1),
                                       /* step_y */ 2.f / (shape[1] - 1),
                                       /* step_z */ 2.f / (shape[2] - 1),

                                       /* offset_x */ -1.f,
                                       /* offset_y */ -1.f,
                                       /* offset_z */ -1.f);

      return_error_if(error == GR3_ERROR_OUT_OF_MEM, ERROR_MALLOC);
      return_error_if(error != GR3_ERROR_NONE, ERROR_INTERNAL);

      global_render->setGR3BackgroundColor(subGroup, 1.0f, 1.0f, 1.0f, 0.0f);

      positions[0] = 0.0f;
      positions[1] = 0.0f;
      positions[2] = 0.0f;
      directions[0] = 0.0f;
      directions[1] = 0.0f;
      directions[2] = 1.0f;
      ups[0] = 0.0f;
      ups[1] = 1.0f;
      ups[2] = 0.0f;
      scales[0] = 1.0f;
      scales[1] = 1.0f;
      scales[2] = 1.0f;

      auto p_vec = std::vector<double>(positions, positions + data_length);
      auto d_vec = std::vector<double>(directions, directions + data_length);
      auto u_vec = std::vector<double>(ups, ups + data_length);
      auto c_vec = std::vector<double>(foreground_colors, foreground_colors + data_length);
      auto s_vec = std::vector<double>(scales, scales + data_length);

      int id = static_cast<int>(global_root->getAttribute("id"));
      global_root->setAttribute("id", id + 1);
      std::string str = std::to_string(id);


      auto drawMesh =
          global_render->createGR3DrawMesh(mesh, 1, "positions" + str, p_vec, "directions" + str, d_vec, "ups" + str,
                                           u_vec, "foregroundcolors" + str, c_vec, "scales" + str, s_vec);
      subGroup->append(drawMesh);

      r = 2.5;
      ups[0] = 0.0f;
      ups[1] = (tilt == 0 ? 0.0f : 1.0f);
      ups[2] = (tilt == 0 ? 1.0f : 0.0f);

      auto drawImage = global_render->createGR3DrawImage(x_min, x_max, y_min, y_max, subplot_width, subplot_height,
                                                         GR3_DRAWABLE_GKS);
      subGroup->append(drawImage);

      global_render->setGR3CameraLookAt(drawImage, (float)(r * sin(tilt) * sin(rotation)), (float)(r * cos(tilt)),
                                        (float)(r * sin(tilt) * cos(rotation)), 0.0f, 0.0f, 0.0f, ups[0], ups[1],
                                        ups[2]);


      logger((stderr, "gr3_drawimage returned %i\n",
              gr3_drawimage(x_min, x_max, y_min, y_max, subplot_width, subplot_height, GR3_DRAWABLE_GKS)));


      auto deleteMesh = global_render->createGR3DeleteMesh(mesh);
      subGroup->append(deleteMesh);

      free(conv_data);
      ++current_series;
    }

  return ERROR_NONE;
}

err_t plot_volume(grm_args_t *subplot_args)
{
  grm_args_t **current_series;
  const char *kind;
  double dlim[2] = {INFINITY, -INFINITY};
  err_t error;

  grm_args_values(subplot_args, "series", "A", &current_series);
  grm_args_values(subplot_args, "kind", "s", &kind);
  while (*current_series != NULL)
    {
      const double *c;
      unsigned int data_length, dims;
      unsigned int *shape;
      int algorithm;
      const char *algorithm_str;
      double dmin, dmax;
      int width, height;
      double device_pixel_ratio;

      return_error_if(!grm_args_first_value(*current_series, "c", "D", &c, &data_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(!grm_args_first_value(*current_series, "c_dims", "I", &shape, &dims), ERROR_PLOT_MISSING_DATA);
      return_error_if(dims != 3, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
      return_error_if(shape[0] * shape[1] * shape[2] != data_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
      return_error_if(data_length <= 0, ERROR_PLOT_MISSING_DATA);

      if (!grm_args_values(*current_series, "algorithm", "i", &algorithm))
        {
          if (grm_args_values(*current_series, "algorithm", "s", &algorithm_str))
            {
              if (strcmp(algorithm_str, "emission") == 0)
                {
                  algorithm = GR_VOLUME_EMISSION;
                }
              else if (strcmp(algorithm_str, "absorption") == 0)
                {
                  algorithm = GR_VOLUME_ABSORPTION;
                }
              else if (str_equals_any(algorithm_str, 2, "mip", "maximum"))
                {
                  algorithm = GR_VOLUME_MIP;
                }
              else
                {
                  logger((stderr, "Got unknown volume algorithm \"%s\"\n", algorithm_str));
                  return ERROR_PLOT_UNKNOWN_ALGORITHM;
                }
            }
          else
            {
              logger((stderr, "No volume algorithm given! Aborting the volume routine\n"));
              return ERROR_PLOT_MISSING_ALGORITHM;
            }
        }
      if (algorithm != GR_VOLUME_ABSORPTION && algorithm != GR_VOLUME_EMISSION && algorithm != GR_VOLUME_MIP)
        {
          logger((stderr, "Got unknown volume algorithm \"%d\"\n", algorithm));
          return ERROR_PLOT_UNKNOWN_ALGORITHM;
        }

      dmin = dmax = -1.0;
      grm_args_values(*current_series, "dmin", "d", &dmin);
      grm_args_values(*current_series, "dmax", "d", &dmax);

      gr_inqvpsize(&width, &height, &device_pixel_ratio);
      gr_setpicturesizeforvolume((int)width * device_pixel_ratio, (int)height * device_pixel_ratio);


      // TODO: In & out parameters dmin and dmax... Use Context to save dmin and dmax as vectors. The next loop should
      // use a reference to these vectors and use the last dmin and dmax
      // TODO: Question: what about dlim... Other functions use dlims from subplot_args...
      gr_volume(shape[0], shape[1], shape[2], (double *)c, algorithm, &dmin, &dmax);

      dlim[0] = grm_min(dlim[0], dmin);
      dlim[1] = grm_max(dlim[1], dmax);

      ++current_series;
    }

  logger((stderr, "dmin, dmax: (%lf, %lf)\n", dlim[0], dlim[1]));
  grm_args_push(subplot_args, "_clim", "dd", dlim[0], dlim[1]);

  error = plot_draw_axes(subplot_args, 2);
  return_if_error;
  error = plot_draw_colorbar(subplot_args, 0.0, 256);
  return_if_error;

  return ERROR_NONE;
}

err_t plot_polar(grm_args_t *subplot_args)
{
  const double *window;
  double r_min, r_max, tick;
  int n;
  grm_args_t **current_series;

  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  group->setAttribute("name", "polar");

  grm_args_values(subplot_args, "window", "D", &window);
  r_min = window[2];
  r_max = window[3];
  tick = 0.5 * auto_tick(r_min, r_max);
  n = (int)ceil((r_max - r_min) / tick);
  r_max = r_min + n * tick;
  grm_args_values(subplot_args, "series", "A", &current_series);
  while (*current_series != NULL)
    {
      double *rho, *theta, *x, *y;
      unsigned int rho_length, theta_length;
      char *spec;
      unsigned int i;
      auto subGroup = global_render->createGroup("polar_series");
      group->append(subGroup);

      return_error_if(!grm_args_first_value(*current_series, "x", "D", &theta, &theta_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(!grm_args_first_value(*current_series, "y", "D", &rho, &rho_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(rho_length != theta_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
      x = static_cast<double *>(malloc(rho_length * sizeof(double)));
      y = static_cast<double *>(malloc(rho_length * sizeof(double)));
      if (x == NULL || y == NULL)
        {
          debug_print_malloc_error();
          free(x);
          free(y);
          return ERROR_MALLOC;
        }
      for (i = 0; i < rho_length; ++i)
        {
          double current_rho = rho[i] / r_max;
          x[i] = current_rho * cos(theta[i]);
          y[i] = current_rho * sin(theta[i]);
        }
      grm_args_values(*current_series, "spec", "s", &spec); /* `spec` is always set */
      global_render->setLineSpec(subGroup, spec);

      int id = (int)global_root->getAttribute("id");
      global_root->setAttribute("id", id + 1);

      std::vector<double> x_vec(x, x + rho_length), y_vec(y, y + rho_length);

      auto temp = global_render->createPolyline("x" + std::to_string(id), x_vec, "y" + std::to_string(id), y_vec);
      subGroup->append(temp);
      free(x);
      free(y);
      ++current_series;
    }

  return ERROR_NONE;
}


/*!
 * Plot a polar histogram.
 *
 * \param[in] x (series)
 *            Either a list containing doubles representing angles or ints for bin_counts.
 * \param[in] normalization a string specifying the normalization (subplot)
 *            The default value is "count".
 *          + count: the default normalization.  The height of each bin is the number of observations in each bin.
 *          + probability: The height of each bin is the relative number of observations,
 *            (number of observations in bin/total number of observations).
 *            The sum of the bin heights is 1.
 *          + countdensity: The height of each bin is the number of observations in bin / (width of bin).
 *          + pdf: Probability density function estimate. The height of each bin is
 *            (number of observations in the bin)/(total number of observations * width of bin).
 *            The area of each bin is the relative number of observations. The sum of the bin areas is 1
 *          + cumcount: The height of each bin is the cumulative number of observations in each bin and all
 *            previous bins. The height of the last bin is numel (theta).
 *          + cdf: Cumulative density function estimate. The height of each bin is equal to the cumulative relative
 *            number of observations in the bin and all previous bins. The height of the last bin is 1.
 * \param[in] bin_counts an int which behaves like a boolean (series)
 *            0: the series contains angles.
 *            else: the given series contains the direct values for each bin.
 *            The default value is 0.
 * \param[in] bin_edges a list containing angles that represent the edges of the bins (series)
 *            It is not compatible with nbins or bin_width.
 *            When used with bin_counts: length of bin_edges must equal the length of bin_counts + 1.
 * \param[in] stairs an int which behaves like a boolean (series)
 *            0: everything will be drawn.
 *            else: only the plot outlines will be drawn.
 *            The default value is 0 and stairs with a value unequal 0 is not compatible with colormap.
 * \param[in] colormap a list with two ints which represent a gr colormap (series)
 *            The first int sets the angle colormap and the second one the radius colormap.
 *            Must be between (inclusive) -1 and 47.
 *            -1: no colormap
 * \param[in] draw_edges an int that acts like a boolean (series)
 *            0: bin edges will not be drawn.
 *            else: bin edges will be drawn.
 *            It is only compatible with colormap.
 *            The default value is 0.
 * \param[in] philim a list containing two double values representing start and end angle (series)
 *            The plot will only use the values in the given range and and it will plot bins between those two values.
 * \param[in] phiflip an integer that represents a boolean (subplot)
 *            0: the angles will be displayed anti clockwise.
 *            else: the angles will be displayed clockwise.
 *            If philim[0] is greater than philim[1], phiflip will be negated.
 *            The default value is 0.
 * \param[in] rlim a list containing two double values between (inclusive) 0.0 and 1.0 (series)
 *            0.0 is the center and 1.0 is the outer edge of the plot.
 *            rlim will limit the bins' start and end height.
 * \param[in] bin_width a double value between (exclusive) 0.0 and 2 * pi setting the bins' width (series)
 *            It is not compatible nbins or bin_edges.
 * \param[in] nbins an int setting the number of bins (series)
 *            It is not compatible with bin_edges, nbins or bin_counts.
 * \param[in] face_alpha double value between 0.0 and 1.0 inclusive (series)
 *            Sets the opacity of bins.
 *            A value of 1.0 means fully opaque and 0.0 means completely transparent (invisible).
 *            The default value is 0.75.
 * \param[in] face_color sets the fill color of bins (series)
 *            face_color expects an integer between 0 and 1008.
 *            The default value is 989.
 * \param[in] edge_color sets the color of the bin edges (series)
 *            edge_color expects an integer between 0 and 1008.
 *            The default value is 1.
 *
 * */
err_t plot_polar_histogram(grm_args_t *subplot_args)
{
  unsigned int num_bins;
  double *classes = NULL;
  unsigned int length;
  double max;
  double *inner = NULL, *outer = NULL;
  double r;
  double rect;
  double *liste = NULL;
  double liste0;
  double liste1;
  double *liste2 = NULL;
  double *mlist = NULL;
  double *rectlist = NULL;
  const char *norm = NULL;
  double bin_width = -1.0;
  double *bin_edges = NULL;
  unsigned int num_bin_edges;
  double *bin_widths = NULL;
  double *philim = NULL;
  double *rlim = NULL;
  unsigned int dummy;
  double *r_min_list = NULL;
  double *r_min_list2 = NULL;
  int stairs;
  double r_min = 0.0;
  double r_max = 1.0;
  double *phi_array = NULL;
  double *arc_2_x = NULL;
  double *arc_2_y = NULL;
  int xcolormap;
  int ycolormap;
  int *colormap = NULL;
  double *angles = NULL;
  int draw_edges = 0;
  int phiflip = 0;
  int x;
  const double convert = 180 / M_PI;
  int edge_color = 1;
  int face_color = 989;
  double face_alpha = 0.75;
  grm_args_t **series;
  unsigned int resample = 0;
  int *lineardata = NULL;
  int *bin_counts = NULL;
  double *f1 = NULL;
  double *f2 = NULL;
  int freeable_bin_widths = 0;
  int freeable_bin_edges = 0;
  int freeable_angles = 0;
  err_t error = ERROR_NONE;

  // TODO: Move viewport dependent calculations to render?
  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  group->setAttribute("name", "polarhistogram");

  gr_savestate();
  std::shared_ptr<GR::Element> temp_elem;
  std::string str;

  gr_inqresamplemethod(&resample);

  global_render->setResampleMethod(group, 0x2020202);

  grm_args_values(subplot_args, "series", "A", &series);

  grm_args_first_value(*series, "classes", "D", &classes, &length);

  /* edge_color */
  if (grm_args_values(*series, "edge_color", "i", &edge_color) == 0)
    {
      edge_color = 1;
    }

  /* face_color */
  if (grm_args_values(*series, "face_color", "i", &face_color) == 0)
    {
      face_color = 989;
    }

  /* face_alpha */
  if (grm_args_values(*series, "face_alpha", "d", &face_alpha) == 0)
    {
      face_alpha = 0.75;
    }

  global_render->setTransparency(group, face_alpha);

  grm_args_values(*series, "nbins", "i", &num_bins);

  grm_args_values(subplot_args, "r_max", "d", &max);

  if (grm_args_values(subplot_args, "phiflip", "i", &phiflip) == 0)
    {
      phiflip = 0;
    }

  if (grm_args_values(subplot_args, "normalization", "s", &norm) == 0)
    {
      norm = "count";
    }

  if (grm_args_values(*series, "draw_edges", "i", &draw_edges) == 0)
    {
      draw_edges = 0;
    }

  if (grm_args_first_value(*series, "bin_edges", "D", &bin_edges, &num_bin_edges) == 0)
    {
      bin_edges = NULL;
      num_bin_edges = 0;
      grm_args_values(*series, "bin_width", "d", &bin_width);
    }
  else
    {
      grm_args_first_value(*series, "bin_widths", "D", &bin_widths, &num_bins);
    }


  if (grm_args_values(*series, "stairs", "i", &stairs) == 0)
    {
      stairs = 0;
    }
  else
    {
      if (draw_edges != 0)
        {
          logger((stderr, "stairs is not compatible with draw_edges / colormap\n"));
          cleanup_and_set_error(ERROR_PLOT_INCOMPATIBLE_ARGUMENTS);
        }
      /* no bin_edges */
      else if (num_bin_edges == 0)
        {
          mlist = static_cast<double *>(malloc(num_bins * 4 * sizeof(double)));
          cleanup_and_set_error_if(mlist == NULL, ERROR_MALLOC);
          if (stairs != 0)
            {
              stairs = 1;
            }
        }
      else
        {
          rectlist = static_cast<double *>(malloc(num_bins * sizeof(double)));
          cleanup_and_set_error_if(rectlist == NULL, ERROR_MALLOC);
        }
    }

  if (grm_args_first_value(*series, "rlim", "D", &rlim, &dummy) == 0)
    {
      rlim = NULL;
    }
  else
    {
      /* TODO: Potential memory leak, s. `malloc` in line 3788 */
      mlist = static_cast<double *>(malloc((num_bins + 1) * 4 * sizeof(double)));
      cleanup_and_set_error_if(mlist == NULL, ERROR_MALLOC);
      if (rlim[0] > rlim[1])
        {
          r_min = rlim[1];
          r_max = rlim[0];
          rlim[0] = r_min;
          rlim[1] = r_max;
        }
      else
        {
          r_min = rlim[0];
          r_max = rlim[1];
        }

      if (r_max > 1.0)
        {
          r_max = 1.0;
          logger((stderr, "the max value of rlim can not exceed 1.0\n"));
          cleanup_and_set_error(ERROR_PLOT_OUT_OF_RANGE);
        }
      if (r_min < 0.0) r_min = 0.0;
    }

  length /= num_bins;
  outer = classes;
  if (phiflip != 0)
    {
      outer += (num_bins - 1) * length;
    }

  if (!grm_args_values(*series, "xcolormap", "i", &xcolormap) ||
      !grm_args_values(*series, "ycolormap", "i", &ycolormap))
    {
      colormap = NULL;
      if (draw_edges != 0)
        {
          logger((stderr, "draw_edges can only be used with colormap\n"));
          cleanup_and_set_error(ERROR_PLOT_INCOMPATIBLE_ARGUMENTS);
        }
    }
  else
    {
      if (-1 > xcolormap || xcolormap > 47 || ycolormap < -1 || ycolormap > 47)
        {
          logger((stderr, "the value for keyword \"colormap\" must contain two integer between -1 and 47\n"));
          cleanup_and_set_error(ERROR_PLOT_OUT_OF_RANGE);
        }
      else
        {
          int colormap_size = 500;
          int image_size = 2000;
          int y, center, center_x, center_y;
          double radius, angle;
          int temp1, temp2;
          int temp = 0;
          int total = 0;
          double norm_factor = 1;
          int count = 0;
          double max_radius;


          lineardata = static_cast<int *>(calloc(image_size * image_size, sizeof(int)));
          cleanup_and_set_error_if(lineardata == NULL, ERROR_MALLOC);

          bin_counts = static_cast<int *>(malloc(num_bins * sizeof(int)));
          cleanup_and_set_error_if(bin_counts == NULL, ERROR_MALLOC);

          colormap = create_colormap(xcolormap, ycolormap, colormap_size);
          cleanup_and_set_error_if(colormap == NULL, ERROR_PLOT_COLORMAP);

          if (num_bin_edges == 0)
            {
              angles = static_cast<double *>(malloc((num_bins + 1) * sizeof(double)));
              cleanup_and_set_error_if(angles == NULL, ERROR_MALLOC);
              freeable_angles = 1;
              linspace(0, M_PI * 2, num_bins + 1, angles);
            }
          else
            {
              angles = bin_edges;
            }

          outer = classes;

          center_x = image_size / 2;
          center_y = image_size / 2;
          center = image_size / 2;

          max_radius = center;

          for (temp1 = 0; temp1 < num_bins; temp1++)
            {
              temp = 0;
              for (temp2 = 0; temp2 < length; temp2++)
                {
                  if (classes[temp1 * length + temp2] == -1) break;
                  ++temp;
                }
              bin_counts[temp1] = temp;
            }

          if (str_equals_any(norm, 2, "probability", "pdf"))
            {
              for (temp1 = 0; temp1 < num_bins; ++temp1)
                {
                  total += bin_counts[temp1];
                }
            }
          else if (strcmp(norm, "cdf") == 0)
            total = bin_counts[num_bins - 1];


          if (str_equals_any(norm, 2, "probability", "cdf"))
            norm_factor = total;
          else if (num_bin_edges == 0 && strcmp(norm, "pdf") == 0)
            norm_factor = total * bin_width;
          else if (num_bin_edges == 0 && strcmp(norm, "countdensity") == 0)
            norm_factor = bin_width;

          if (rlim != NULL)
            {
              r_min *= max_radius;
              r_max *= max_radius;
            }
          else
            {
              r_min = 0.0;
              r_max = max_radius;
            }

          for (y = 0; y < image_size; y++)
            {
              for (x = 0; x < image_size; x++)
                {
                  int q;
                  radius = sqrt(pow(x - center_x, 2) + pow(y - center_y, 2));
                  angle = atan2(y - center_y, x - center_x);

                  if (angle < 0) angle += M_PI * 2;
                  if (phiflip == 0) angle = 2 * M_PI - angle;

                  for (q = 0; q < num_bins; ++q)
                    {
                      if (angle > angles[q] && angle <= angles[q + 1])
                        {
                          count = bin_counts[q];
                          if (strcmp(norm, "pdf") == 0 && num_bin_edges > 0)
                            {
                              norm_factor = total * bin_widths[q];
                            }
                          else if (strcmp(norm, "countdensity") == 0 && num_bin_edges > 0)
                            {
                              norm_factor = bin_widths[q];
                            }

                          if ((grm_round(radius * 100) / 100) <=
                                  (grm_round((count * 1.0 / norm_factor / max * center) * 100) / 100) &&
                              radius <= r_max && radius > r_min)
                            {
                              lineardata[y * image_size + x] = colormap
                                  [(int)(radius / (center * pow(2, 0.5)) * (colormap_size - 1)) * colormap_size +
                                   grm_max(grm_min((int)(angle / (2 * M_PI) * colormap_size), colormap_size - 1), 0)];
                            }

                        } /* end angle check */
                    }     /* end for q loop: bin check*/
                }         /* end x loop*/
            }             /* end y loop */
          if (rlim != NULL)
            {
              r_min = rlim[0];
              r_max = rlim[1];
            }

          int id = (int)global_root->getAttribute("id");
          global_root->setAttribute("id", id + 1);
          str = std::to_string(id);
          std::vector<int> l_vec(lineardata, lineardata + image_size * image_size);

          temp_elem =
              global_render->createDrawImage(-1.0, 1.0, -1.0, 1.0, image_size, image_size, "data" + str, l_vec, 0);
          group->append(temp_elem);
          free(lineardata);
          free(bin_counts);
          lineardata = NULL;
          bin_counts = NULL;
        } /* end colormap calculation*/

    } /* end colormap condition */

  outer = classes;
  if (phiflip != 0) outer += (num_bins - 1) * length;

  if (phiflip != 0 && num_bin_edges > 0)
    {
      double *temp = NULL;
      double *temp2 = NULL;
      temp = static_cast<double *>(malloc(num_bin_edges * sizeof(double)));
      cleanup_and_set_error_if(temp == NULL, ERROR_MALLOC);
      temp2 = static_cast<double *>(malloc(num_bins * sizeof(double)));
      cleanup_and_set_error_if(temp2 == NULL, ERROR_MALLOC);
      int u;
      for (u = 0; u < num_bin_edges; u++)
        {
          temp[u] = 2 * M_PI - bin_edges[num_bin_edges - 1 - u];
        }
      for (u = num_bins - 1; u >= 0; --u)
        {
          temp2[u] = bin_widths[num_bins - 1 - u];
        }
      bin_widths = temp2;
      freeable_bin_widths = 1;
      bin_edges = temp;
      temp = temp2 = NULL;
      freeable_bin_edges = 1;
    }

  /* no colormap or colormap combined with draw_edges */
  if (colormap == NULL || draw_edges == 1)
    {
      for (x = 0; x < num_bins; ++x)
        {
          double count;
          int y;

          /*
           * free memory from the previous iteration
           * (end of loop is not possible because of `continue` statements)
           * last iteration memory is freed in the cleanup block
           */
          free(liste);
          liste = NULL;
          free(liste2);
          liste2 = NULL;
          free(r_min_list);
          r_min_list = NULL;
          free(r_min_list2);
          r_min_list2 = NULL;

          count = 0.0;
          inner = outer;

          if (*inner == -1)
            {
              /* stairs bin_edges / philim  */
              if (rectlist != NULL && philim != NULL)
                rectlist[x] = r_min;
              else if (rectlist != NULL)
                rectlist[x] = 0.0;
            }


          for (y = 0; y < length; ++y)
            {
              if (*inner != -1)
                {
                  count++;
                  inner++;
                }
            }

          if (str_equals_any(norm, 2, "probability", "cdf"))
            {
              count /= length;
            }
          else if (strcmp(norm, "pdf") == 0)
            {
              if (num_bin_edges == 0)
                {
                  count /= length * bin_width;
                }
              else
                {
                  count /= (length * *(bin_widths + x));
                }
            }
          else if (strcmp(norm, "countdensity") == 0)
            {
              if (num_bin_edges == 0)
                {
                  count /= bin_width;
                }
              else
                {
                  count /= *(bin_widths + x);
                }
            }

          /* no stairs*/
          if (stairs == 0)
            {
              r = pow((count / max), num_bins * 2);
              liste = moivre(r, 2 * x, num_bins * 2);
              cleanup_and_set_error_if(liste == NULL, ERROR_MALLOC);
              liste0 = liste[0];
              liste1 = liste[1];
              rect = sqrt(liste0 * liste0 + liste1 * liste1);

              if (rlim != NULL)
                {
                  double temporary;
                  int i;
                  liste2 = moivre(r, (2 * x + 2), (num_bins * 2));
                  cleanup_and_set_error_if(liste2 == NULL, ERROR_MALLOC);

                  *(mlist + x * 4) = liste0;
                  *(mlist + x * 4 + 1) = liste1;
                  *(mlist + x * 4 + 2) = *(liste2);
                  *(mlist + x * 4 + 3) = *(liste2 + 1);

                  r_min_list = moivre(pow((r_min), (num_bins * 2)), (x * 2), num_bins * 2);
                  cleanup_and_set_error_if(r_min_list == NULL, ERROR_MALLOC);
                  r_min_list2 = moivre(pow((r_min), (num_bins * 2)), (x * 2 + 2), num_bins * 2);
                  cleanup_and_set_error_if(r_min_list2 == NULL, ERROR_MALLOC);

                  for (i = 0; i < 2; ++i)
                    {
                      temporary = fabs(sqrt(pow(mlist[x * 4 + 2 - i * 2], 2) + pow(mlist[x * 4 + 3 - i * 2], 2)));
                      if (temporary > r_max)
                        {
                          double factor = fabs(r_max / temporary);
                          mlist[x * 4 + 2 - i * 2] *= factor;
                          mlist[x * 4 + 3 - i * 2] *= factor;
                        }
                    }
                  r = count / max;
                  if (r > r_max)
                    {
                      r = r_max;
                    }
                  free(liste2);
                  liste2 = NULL;
                }

              /*  no binedges */
              if (num_bin_edges == 0)
                {
                  if (rlim != NULL)
                    {
                      double start_angle;
                      double end_angle;

                      double diff_angle;
                      int num_angle;

                      int i;

                      if (r <= r_min)
                        {
                          if (phiflip == 1)
                            outer -= length;
                          else
                            outer += length;
                          continue;
                        }

                      start_angle = x * (360.0 / num_bins) / convert;
                      end_angle = (x + 1) * (360.0 / num_bins) / convert;

                      diff_angle = end_angle - start_angle;
                      num_angle = (int)(diff_angle / (0.2 / convert));

                      phi_array = static_cast<double *>(malloc(num_angle * sizeof(double)));
                      cleanup_and_set_error_if(phi_array == NULL, ERROR_MALLOC);
                      linspace(start_angle, end_angle, num_angle, phi_array);

                      f1 = static_cast<double *>(malloc((4 + 2 * num_angle) * sizeof(double)));
                      cleanup_and_set_error_if(f1 == NULL, ERROR_MALLOC);
                      /* line_1_x[0] and [1]*/
                      f1[0] = r_min_list[0];
                      f1[1] = mlist[4 * x];
                      /* arc_1_x */
                      listcomprehension(r, cos, phi_array, num_angle, 2, f1);
                      /* reversed line_2_x [0] and [1] */
                      f1[2 + num_angle + 1] = r_min_list2[0];
                      f1[2 + num_angle] = mlist[4 * x + 2];
                      /* reversed arc_2_x */
                      arc_2_x = listcomprehension(r_min, cos, phi_array, num_angle, 0, NULL);
                      cleanup_and_set_error_if(arc_2_x == NULL, ERROR_MALLOC);
                      for (i = 0; i < num_angle; ++i)
                        {
                          f1[2 + num_angle + 2 + i] = arc_2_x[num_angle - 1 - i];
                        }
                      free(arc_2_x);
                      arc_2_x = NULL;

                      f2 = static_cast<double *>(malloc((4 + 2 * num_angle) * sizeof(double)));
                      cleanup_and_set_error_if(f2 == NULL, ERROR_MALLOC);
                      /* line_1_y[0] and [1] */
                      f2[0] = r_min_list[1];
                      f2[1] = mlist[4 * x + 1];
                      /*arc_1_y */
                      listcomprehension(r, sin, phi_array, num_angle, 2, f2);
                      /* reversed line_2_y [0] and [1] */
                      f2[2 + num_angle + 1] = r_min_list2[1];
                      f2[2 + num_angle] = mlist[4 * x + 3];
                      /* reversed arc_2_y */
                      arc_2_y = listcomprehension(r_min, sin, phi_array, num_angle, 0, NULL);
                      cleanup_and_set_error_if(arc_2_y == NULL, ERROR_MALLOC);
                      for (i = 0; i < num_angle; ++i)
                        {
                          f2[2 + num_angle + 2 + i] = arc_2_y[num_angle - 1 - i];
                        }
                      free(arc_2_y);
                      arc_2_y = NULL;

                      if (draw_edges == 0)
                        {
                          int id = (int)global_root->getAttribute("id");
                          global_root->setAttribute("id", id + 1);
                          std::vector<double> f1_vec(f1, f1 + 4 + 2 * num_angle), f2_vec(f2, f2 + 4 + 2 * num_angle);
                          str = std::to_string(id);

                          temp_elem = global_render->createFillArea("x" + str, f1_vec, "y" + str, f2_vec);
                          group->append(temp_elem);
                          global_render->setFillColorInd(temp_elem, face_color);
                          global_render->setFillIntStyle(temp_elem, 1);

                          temp_elem = global_render->createFillArea("x" + str, std::nullopt, "y" + str, std::nullopt);
                          group->append(temp_elem);
                          global_render->setFillColorInd(temp_elem, edge_color);
                          global_render->setFillIntStyle(temp_elem, 0);
                        }
                      else
                        {
                          int id = (int)global_root->getAttribute("id");
                          global_root->setAttribute("id", id + 1);
                          std::vector<double> f1_vec(f1, f1 + 4 + 2 * num_angle), f2_vec(f2, f2 + 4 + 2 * num_angle);
                          str = std::to_string(id);

                          temp_elem = global_render->createFillArea("x" + str, std::nullopt, "y" + str, std::nullopt);
                          group->append(temp_elem);
                          global_render->setFillColorInd(temp_elem, edge_color);
                          global_render->setFillIntStyle(temp_elem, 0);
                        }

                      free(f1);
                      f1 = NULL;
                      free(f2);
                      f2 = NULL;
                      free(phi_array);
                      phi_array = NULL;
                    } /* end rlim condition */
                  /* no rlim */
                  else
                    {
                      if (draw_edges == 0)
                        {
                          temp_elem = global_render->createFillArc(-rect, rect, -rect, rect, x * (360.0 / num_bins),
                                                                   (x + 1) * (360.0 / num_bins));
                          group->append(temp_elem);

                          global_render->setFillIntStyle(temp_elem, 1);
                          global_render->setFillColorInd(temp_elem, face_color);
                        }

                      temp_elem = global_render->createFillArc(-rect, rect, -rect, rect, x * (360.0 / num_bins),
                                                               (x + 1) * (360.0 / num_bins));
                      group->append(temp_elem);

                      global_render->setFillIntStyle(temp_elem, 0);
                      global_render->setFillColorInd(temp_elem, edge_color);
                    }
                }
              /* bin_egdes */
              else
                {
                  if (rlim != NULL)
                    {
                      double start_angle;
                      double end_angle;

                      double diff_angle;
                      int num_angle;

                      int i;

                      if (r <= r_min)
                        {
                          if (phiflip != 0)
                            outer -= length;
                          else
                            outer += length;
                          continue;
                        }

                      start_angle = bin_edges[x];
                      end_angle = bin_edges[x + 1];

                      diff_angle = end_angle - start_angle;
                      num_angle = (int)(diff_angle / (0.2 / convert));
                      phi_array = static_cast<double *>(malloc(num_angle * sizeof(double)));
                      cleanup_and_set_error_if(phi_array == NULL, ERROR_MALLOC);
                      linspace(start_angle, end_angle, num_angle, phi_array);

                      f1 = static_cast<double *>(malloc((4 + 2 * num_angle) * sizeof(double)));
                      cleanup_and_set_error_if(f1 == NULL, ERROR_MALLOC);
                      /* line_1_x[0] and [1]*/
                      f1[0] = cos(bin_edges[x]) * r_min;
                      f1[1] = grm_min(rect, r_max) * cos(bin_edges[x]);
                      /*arc_1_x */
                      listcomprehension(r, cos, phi_array, num_angle, 2, f1);
                      /* reversed line_2_x [0] and [1] */
                      f1[2 + num_angle + 1] = cos(bin_edges[x + 1]) * r_min;
                      f1[2 + num_angle] = grm_min(rect, r_max) * cos(bin_edges[x + 1]);
                      /* reversed arc_2_x */
                      arc_2_x = listcomprehension(r_min, cos, phi_array, num_angle, 0, NULL);
                      cleanup_and_set_error_if(arc_2_x == NULL, ERROR_MALLOC);
                      for (i = 0; i < num_angle; ++i)
                        {
                          f1[2 + num_angle + 2 + i] = arc_2_x[num_angle - 1 - i];
                        }
                      free(arc_2_x);
                      arc_2_x = NULL;

                      f2 = static_cast<double *>(malloc((4 + 2 * num_angle) * sizeof(double)));
                      cleanup_and_set_error_if(f2 == NULL, ERROR_MALLOC);
                      /* line_1_y[0] and [1] */
                      f2[0] = r_min * sin(bin_edges[x]);
                      f2[1] = grm_min(rect, r_max) * sin(bin_edges[x]);
                      /*arc_1_y */
                      listcomprehension(r, sin, phi_array, num_angle, 2, f2);
                      /* reversed line_2_y [0] and [1] */
                      f2[2 + num_angle + 1] = r_min * sin(bin_edges[x + 1]);
                      f2[2 + num_angle] = grm_min(rect, r_max) * sin(bin_edges[x + 1]);
                      /* reversed arc_2_y */
                      arc_2_y = listcomprehension(r_min, sin, phi_array, num_angle, 0, NULL);
                      cleanup_and_set_error_if(arc_2_y == NULL, ERROR_MALLOC);
                      for (i = 0; i < num_angle; ++i)
                        {
                          f2[2 + num_angle + 2 + i] = arc_2_y[num_angle - 1 - i];
                        }
                      free(arc_2_y);
                      arc_2_y = NULL;

                      if (draw_edges == 0)
                        {
                          int id = (int)global_root->getAttribute("id");
                          global_root->setAttribute("id", id + 1);
                          std::vector<double> f1_vec(f1, f1 + 4 + 2 * num_angle), f2_vec(f2, f2 + 4 + 2 * num_angle);
                          str = std::to_string(id);

                          temp_elem = global_render->createFillArea("x" + str, f1_vec, "y" + str, f2_vec);
                          group->append(temp_elem);
                          global_render->setFillColorInd(temp_elem, face_color);
                          global_render->setFillIntStyle(temp_elem, 1);

                          temp_elem = global_render->createFillArea("x" + str, std::nullopt, "y" + str, std::nullopt);
                          group->append(temp_elem);
                          global_render->setFillColorInd(temp_elem, edge_color);
                          global_render->setFillIntStyle(temp_elem, 0);
                        }
                      else
                        {
                          int id = (int)global_root->getAttribute("id");
                          global_root->setAttribute("id", id + 1);
                          std::vector<double> f1_vec(f1, f1 + 4 + 2 * num_angle), f2_vec(f2, f2 + 4 + 2 * num_angle);
                          str = std::to_string(id);

                          temp_elem = global_render->createFillArea("x" + str, std::nullopt, "y" + str, std::nullopt);
                          group->append(temp_elem);
                          global_render->setFillColorInd(temp_elem, edge_color);
                          global_render->setFillIntStyle(temp_elem, 0);
                        }


                      //                      temp_elem = global_render->createFillArea("x" + str, std::nullopt, "y" +
                      //                      str, std::nullopt); group->append(temp_elem);
                      //                      global_render->setFillColorInd(temp_elem, edge_color);
                      //                      global_render->setFillIntStyle(temp_elem, 0);


                      free(f1);
                      f1 = NULL;
                      free(f2);
                      f2 = NULL;
                      free(phi_array);
                      phi_array = NULL;
                    }
                  /* no rlim */
                  else
                    {
                      if (draw_edges == 0)
                        {
                          temp_elem = global_render->createFillArc(-rect, rect, -rect, rect, bin_edges[x] * convert,
                                                                   bin_edges[x + 1] * convert);
                          group->append(temp_elem);

                          global_render->setFillIntStyle(temp_elem, 1);
                          global_render->setFillColorInd(temp_elem, face_color);
                        }
                      temp_elem = global_render->createFillArc(-rect, rect, -rect, rect, bin_edges[x] * convert,
                                                               bin_edges[x + 1] * convert);
                      group->append(temp_elem);

                      global_render->setFillIntStyle(temp_elem, 0);
                      global_render->setFillColorInd(temp_elem, edge_color);
                    }
                }
            } /* end no stairs condition */
          /* stairs without draw_edges (not compatible) */
          else if (draw_edges == 0 && colormap == NULL)
            {
              global_render->setFillColorInd(group, 0);
              global_render->setLineColorInd(group, edge_color);
              global_render->setLineWidth(group, 2.3);

              r = pow((count / max), (num_bins * 2));
              liste = moivre(r, (2 * x), num_bins * 2);
              cleanup_and_set_error_if(liste == NULL, ERROR_MALLOC);
              liste2 = moivre(r, (2 * x + 2), (num_bins * 2));
              cleanup_and_set_error_if(liste2 == NULL, ERROR_MALLOC);
              rect = sqrt(*liste * *liste + *(liste + 1) * *(liste + 1));

              /*  no bin_edges */
              if (num_bin_edges == 0)
                {
                  *(mlist + x * 4) = *liste;
                  *(mlist + x * 4 + 1) = *(liste + 1);
                  *(mlist + x * 4 + 2) = *(liste2);
                  *(mlist + x * 4 + 3) = *(liste2 + 1);

                  if (rlim != NULL)
                    {
                      double temporary;
                      int i;
                      for (i = 0; i < 2; ++i)
                        {
                          temporary = fabs(sqrt(pow(mlist[x * 4 + 2 - i * 2], 2) + pow(mlist[x * 4 + 3 - i * 2], 2)));
                          if (temporary > r_max)
                            {
                              double factor = fabs(r_max / temporary);
                              mlist[x * 4 + 2 - i * 2] *= factor;
                              mlist[x * 4 + 3 - i * 2] *= factor;
                            }
                        }

                      if (rect > r_min)
                        {
                          group->append(global_render->createDrawArc(
                              -grm_min(rect, r_max), grm_min(rect, r_max), -grm_min(rect, r_max), grm_min(rect, r_max),
                              x * (360.0 / num_bins), (x + 1) * 360.0 / num_bins));

                          group->append(global_render->createDrawArc(
                              -r_min, r_min, -r_min, r_min, x * (360.0 / num_bins), (x + 1) * (360.0 / num_bins)));
                        }
                    }
                  /* no rlim */
                  else
                    {
                      group->append(global_render->createDrawArc(-rect, rect, -rect, rect, x * (360.0 / num_bins),
                                                                 (x + 1) * (360.0 / num_bins)));
                    }
                }
              /* with bin_edges */
              else
                {
                  /* rlim and bin_edges*/
                  if (rlim != NULL)
                    {
                      if (rect < r_min)
                        {
                          rectlist[x] = r_min;
                        }
                      else if (rect > r_max)
                        {
                          rectlist[x] = r_max;
                        }
                      else
                        {
                          rectlist[x] = rect;
                        }

                      if (rect > r_min)
                        {

                          group->append(global_render->createDrawArc(
                              -grm_min(rect, r_max), grm_min(rect, r_max), -grm_min(rect, r_max), grm_min(rect, r_max),
                              bin_edges[x] * convert, bin_edges[x + 1] * convert));

                          group->append(global_render->createDrawArc(
                              -r_min, r_min, -r_min, r_min, bin_edges[x] * convert, bin_edges[x + 1] * convert));
                        }
                    }
                  /* no rlim */
                  else
                    {
                      *(rectlist + x) = rect;
                      if (x == num_bin_edges - 1)
                        {
                          break;
                        }
                      group->append(global_render->createDrawArc(-rect, rect, -rect, rect, *(bin_edges + x) * convert,
                                                                 *(bin_edges + x + 1) * convert));
                    }
                }
            }

          if (phiflip == 0)
            outer += length;
          else
            {
              outer -= length;
            }
        } /* end of classes for loop */

      if (stairs != 0 && draw_edges == 0)
        {
          /* stairs without binedges, rlim */
          if (mlist != NULL && rlim == NULL && rectlist == NULL)
            {
              int s;
              double line_x[2];
              double line_y[2];
              for (s = 0; s < num_bins * 4; s += 2)
                {
                  if (s > 2 && s % 4 == 0)
                    {
                      line_x[0] = *(mlist + s);
                      line_x[1] = *(mlist + s - 2);
                      line_y[0] = *(mlist + s + 1);
                      line_y[1] = *(mlist + s - 1);
                      group->append(global_render->createPolyline(line_x[0], line_x[1], line_y[0], line_y[1]));
                    }
                }
              line_x[0] = *(mlist);
              line_x[1] = *(mlist + (num_bins - 1) * 4 + 2);
              line_y[0] = *(mlist + 1);
              line_y[1] = *(mlist + (num_bins - 1) * 4 + 3);
              group->append(global_render->createPolyline(line_x[0], line_x[1], line_y[0], line_y[1]));
            }

          /* stairs without bin_edges with rlim*/
          else if (mlist != NULL && rlim != NULL && rectlist == NULL)
            {
              double line_x[2], line_y[2];
              double rect1, rect2;
              for (x = 0; x < num_bins; ++x)
                {
                  if (x > 0)
                    {
                      rect1 = sqrt(pow(mlist[x * 4], 2) + pow(mlist[x * 4 + 1], 2));
                      rect2 = sqrt(pow(mlist[(x - 1) * 4 + 2], 2) + pow(mlist[(x - 1) * 4 + 3], 2));

                      if (rect1 < r_min && rect2 < r_min) continue;
                      if (rect1 < r_min)
                        {
                          mlist[4 * x] = r_min * cos(2 * M_PI / num_bins * x);
                          mlist[4 * x + 1] = r_min * sin(2 * M_PI / num_bins * x);
                        }
                      else if (rect2 < r_min)
                        {
                          mlist[(x - 1) * 4 + 2] = r_min * cos(2 * M_PI / num_bins * x);
                          mlist[(x - 1) * 4 + 3] = r_min * sin(2 * M_PI / num_bins * x);
                        }
                      line_x[0] = mlist[x * 4];
                      line_x[1] = mlist[(x - 1) * 4 + 2];
                      line_y[0] = mlist[x * 4 + 1];
                      line_y[1] = mlist[(x - 1) * 4 + 3];
                      group->append(global_render->createPolyline(line_x[0], line_x[1], line_y[0], line_y[1]));
                    }
                }
              line_x[0] = mlist[(num_bins - 1) * 4 + 2] = grm_max(mlist[(num_bins - 1) * 4 + 2], r_min * cos(0));
              line_y[0] = mlist[(num_bins - 1) * 4 + 3] = grm_max(mlist[(num_bins - 1) * 4 + 3], r_min * sin(0));
              line_x[1] = mlist[0] = grm_max(mlist[0], r_min * cos(0));
              line_y[1] = mlist[1] = grm_max(mlist[1], r_min * sin(0));

              group->append(global_render->createPolyline(line_x[0], line_x[1], line_y[0], line_y[1]));
            }

          /* stairs with binedges without rlim */
          else if (rectlist != NULL && rlim == NULL)
            {
              double startx = 0.0, starty = 0.0;
              double line_x[2], line_y[2];

              for (x = 0; x < num_bin_edges - 1; ++x)
                {
                  line_x[0] = startx;
                  line_x[1] = *(rectlist + x) * cos(*(bin_edges + x));
                  line_y[0] = starty;
                  line_y[1] = *(rectlist + x) * sin(*(bin_edges + x));

                  startx = *(rectlist + x) * cos(*(bin_edges + x + 1));
                  starty = *(rectlist + x) * sin(*(bin_edges + x + 1));

                  if (!(*bin_edges == 0.0 && *(bin_edges + num_bin_edges - 1) > 1.96 * M_PI) || x > 0)
                    {
                      group->append(global_render->createPolyline(line_x[0], line_x[1], line_y[0], line_y[1]));
                    }
                }

              if (*bin_edges == 0.0 && *(bin_edges + num_bin_edges - 1) > 1.96 * M_PI)
                {
                  line_x[0] = *rectlist * cos(*bin_edges);
                  line_x[1] = startx;
                  line_y[0] = *rectlist * sin(*bin_edges);
                  line_y[1] = starty;
                  group->append(global_render->createPolyline(line_x[0], line_x[1], line_y[0], line_y[1]));
                }
              else
                {
                  line_x[0] = *(rectlist + num_bin_edges - 2) * cos(*(bin_edges + num_bin_edges - 1));
                  line_x[1] = 0.0;
                  line_y[0] = *(rectlist + num_bin_edges - 2) * sin(*(bin_edges + num_bin_edges - 1));
                  line_y[1] = 0.0;
                  group->append(global_render->createPolyline(line_x[0], line_x[1], line_y[0], line_y[1]));
                }
            }

          /* stairs with bin_edges and rlim */
          else if (rectlist != NULL && rlim != NULL)
            {
              double startx = grm_max(rectlist[0] * cos(bin_edges[0]), r_min * cos(bin_edges[0]));
              double starty = grm_max(rectlist[0] * sin(bin_edges[0]), r_min * sin(bin_edges[0]));

              double line_x[2];
              double line_y[2];

              for (x = 0; x < num_bin_edges - 1; ++x)
                {
                  *line_x = startx;
                  *(line_x + 1) = *(rectlist + x) * cos(*(bin_edges + x));
                  *line_y = starty;
                  *(line_y + 1) = *(rectlist + x) * sin(*(bin_edges + x));


                  startx = *(rectlist + x) * cos(*(bin_edges + x + 1));
                  starty = *(rectlist + x) * sin(*(bin_edges + x + 1));

                  if (((phiflip == 0) &&
                       (!((*bin_edges > 0.0 && *bin_edges < 0.001) && *(bin_edges + num_bin_edges - 1) > 1.96 * M_PI) ||
                        x > 0)) ||
                      ((*bin_edges > 1.96 * M_PI &&
                        !(*(bin_edges + num_bin_edges - 1) > 0.0 && *(bin_edges + num_bin_edges - 1) < 0.001)) ||
                       x > 0))
                    {
                      group->append(global_render->createPolyline(line_x[0], line_x[1], line_y[0], line_y[1]));
                    }
                }

              if (*bin_edges == 0.0 && *(bin_edges + num_bin_edges - 1) > 1.96 * M_PI)
                {
                  *line_x = *rectlist * cos(*bin_edges);
                  *(line_x + 1) = rectlist[num_bin_edges - 2] * cos(bin_edges[num_bin_edges - 1]);
                  *line_y = *rectlist * sin(*bin_edges);
                  *(line_y + 1) = rectlist[num_bin_edges - 2] * sin(bin_edges[num_bin_edges - 1]);
                  group->append(global_render->createPolyline(line_x[0], line_x[1], line_y[0], line_y[1]));
                }
              else
                {
                  *line_x = *(rectlist + num_bin_edges - 2) * cos(*(bin_edges + num_bin_edges - 1));
                  *(line_x + 1) = r_min * cos(bin_edges[num_bin_edges - 1]);
                  *line_y = *(rectlist + num_bin_edges - 2) * sin(*(bin_edges + num_bin_edges - 1));
                  *(line_y + 1) = r_min * sin(bin_edges[num_bin_edges - 1]);

                  group->append(global_render->createPolyline(line_x[0], line_x[1], line_y[0], line_y[1]));


                  line_x[0] = r_min * cos(bin_edges[0]);
                  line_x[1] = rectlist[0] * cos(bin_edges[0]);
                  line_y[0] = r_min * sin(bin_edges[0]);
                  line_y[1] = rectlist[0] * sin(bin_edges[0]);

                  group->append(global_render->createPolyline(line_x[0], line_x[1], line_y[0], line_y[1]));
                }
            }
        }
    }


  gr_updatews();
  gr_updategks();
  gr_restorestate();

  // TODO: gr_restorestate() restores resamplemethod?
  gr_setresamplemethod(resample);

cleanup:
  free(mlist);
  free(rectlist);
  free(lineardata);
  free(bin_counts);
  if (freeable_bin_widths == 1)
    {
      free(bin_widths);
    }
  if (freeable_bin_edges == 1)
    {
      free(bin_edges);
    }
  if (freeable_angles == 1)
    {
      free(angles);
    }
  free(phi_array);
  free(r_min_list);
  free(r_min_list2);
  free(f1);
  free(f2);
  free(arc_2_x);
  free(arc_2_y);
  free(liste);
  free(liste2);
  free(colormap);

  return error;
}

err_t plot_pie(grm_args_t *subplot_args)
{
  grm_args_t *series;
  double *x;
  double *normalized_x = NULL;
  unsigned int *normalized_x_int = NULL;
  unsigned int x_length;
  int color_index;
  int inq_color;
  unsigned char color_rgb[4];
  double start_angle, middle_angle, end_angle;
  double text_pos[2];
  char text[80];
  const char *title;
  unsigned int i;
  err_t error = ERROR_NONE;

  static unsigned int color_array_length = -1;
  const int *color_indices = NULL;
  const double *color_rgb_values = NULL;

  std::string color_indices_key;
  std::string color_rgb_values_key;

  std::vector<int> color_indices_vec;
  std::vector<double> color_rgb_values_vec;

  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  group->setAttribute("name", "pie");

  grm_args_values(subplot_args, "series", "a", &series); /* series exists always */

  gr_savestate();
  global_render->setFillIntStyle(group, GKS_K_INTSTYLE_SOLID);

  global_render->setTextAlign(group, GKS_K_TEXT_HALIGN_CENTER, GKS_K_TEXT_VALIGN_HALF);

  int id = static_cast<int>(global_root->getAttribute("id"));
  global_root->setAttribute("id", id + 1);

  cleanup_and_set_error_if(!grm_args_first_value(series, "x", "D", &x, &x_length), ERROR_PLOT_MISSING_DATA);
  normalized_x = normalize(x_length, x);
  cleanup_and_set_error_if(normalized_x == NULL, ERROR_MALLOC);
  normalized_x_int = normalize_int(x_length, x, 1000);
  cleanup_and_set_error_if(normalized_x_int == NULL, ERROR_MALLOC);


  if (grm_args_first_value(series, "c", "I", &color_indices, &color_array_length))
    {
      color_indices_key = "color_indices" + std::to_string(id);
      color_indices_vec = std::vector<int>(color_indices, color_indices + color_array_length);
      global_render->setNextColor(group, color_indices_key, color_indices_vec);
    }
  else if (grm_args_first_value(series, "c", "D", &color_rgb_values, &color_array_length))
    {
      color_rgb_values_key = "color_rgb_values" + std::to_string(id);
      color_rgb_values_vec = std::vector<double>(color_rgb_values, color_rgb_values + color_array_length);
      global_render->setNextColor(group, color_rgb_values_key, color_rgb_values_vec);
    }
  else
    {
      // fallback case of setNextColor
      global_render->setNextColor(group);
    }

  color_index = set_next_color(series, "c", GR_COLOR_FILL, group);
  start_angle = 90;
  for (i = 0; i < x_length; ++i)
    {
      end_angle = start_angle - normalized_x[i] * 360.0;
      auto temp = global_render->createFillArc(0.05, 0.95, 0.05, 0.95, start_angle, end_angle);
      group->append(temp);

      if (i > 0)
        {
          color_index = set_next_color(NULL, NULL, GR_COLOR_FILL, temp);
        }

      middle_angle = (start_angle + end_angle) / 2.0;

      text_pos[0] = 0.5 + 0.25 * cos(middle_angle * M_PI / 180.0);
      text_pos[1] = 0.5 + 0.25 * sin(middle_angle * M_PI / 180.0);

      snprintf(text, 80, "%.2lf\n%.1lf %%", x[i], normalized_x_int[i] / 10.0);

      auto text_elem = global_render->createText(text_pos[0], text_pos[1], text, WC);
      text_elem->setAttribute("color_index", color_index);
      group->append(text_elem);

      text_elem->setAttribute("set-text-color-for-background", true);

      start_angle = end_angle;
      if (start_angle < 0)
        {
          start_angle += 360.0;
        }
    }
  set_next_color(NULL, NULL, GR_COLOR_RESET, group);

  if (grm_args_values(subplot_args, "title", "s", &title))
    {
      auto titleRenderElem = global_render->createPiePlotTitleRenderElement(title);
      group->append(titleRenderElem);
    }

cleanup:
  free(normalized_x);
  free(normalized_x_int);

  return error;
}

err_t plot_trisurf(grm_args_t *subplot_args)
{
  grm_args_t **current_series;

  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  group->setAttribute("name", "trisurf");
  grm_args_values(subplot_args, "series", "A", &current_series);
  while (*current_series != NULL)
    {
      double *x, *y, *z;
      unsigned int x_length, y_length, z_length;
      auto subGroup = global_render->createGroup("trisurf_series");
      group->append(subGroup);
      return_error_if(!grm_args_first_value(*current_series, "x", "D", &x, &x_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(!grm_args_first_value(*current_series, "y", "D", &y, &y_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(!grm_args_first_value(*current_series, "z", "D", &z, &z_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(x_length != y_length || x_length != z_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);

      int id = (int)global_root->getAttribute("id");
      global_root->setAttribute("id", id + 1);
      std::string str = std::to_string(id);

      std::vector<double> x_vec(x, x + x_length), y_vec(y, y + x_length), z_vec(z, z + x_length);
      auto temp = global_render->createTriSurface("px" + str, x_vec, "py" + str, y_vec, "pz" + str, z_vec);
      subGroup->append(temp);
      ++current_series;
    }
  plot_draw_axes(subplot_args, 2);
  plot_draw_colorbar(subplot_args, 0.05, 256);

  return ERROR_NONE;
}

err_t plot_tricont(grm_args_t *subplot_args)
{
  double z_min, z_max;
  double *levels;
  int num_levels;
  grm_args_t **current_series;
  int i;

  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  group->setAttribute("name", "tricont");

  grm_args_values(subplot_args, "_zlim", "dd", &z_min, &z_max);
  grm_args_values(subplot_args, "levels", "i", &num_levels);
  levels = static_cast<double *>(malloc(num_levels * sizeof(double)));
  if (levels == NULL)
    {
      debug_print_malloc_error();
      return ERROR_MALLOC;
    }
  for (i = 0; i < num_levels; ++i)
    {
      levels[i] = z_min + ((1.0 * i) / (num_levels - 1)) * (z_max - z_min);
    }
  grm_args_values(subplot_args, "series", "A", &current_series);
  while (*current_series != NULL)
    {
      double *x, *y, *z;
      unsigned int x_length, y_length, z_length;
      auto subGroup = global_render->createGroup("tricont_series");
      group->append(subGroup);
      return_error_if(!grm_args_first_value(*current_series, "x", "D", &x, &x_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(!grm_args_first_value(*current_series, "y", "D", &y, &y_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(!grm_args_first_value(*current_series, "z", "D", &z, &z_length), ERROR_PLOT_MISSING_DATA);
      return_error_if(x_length != y_length || x_length != z_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);

      int id = (int)global_root->getAttribute("id");
      global_root->setAttribute("id", id + 1);
      std::string str = std::to_string(id);

      std::vector<double> x_vec(x, x + x_length), y_vec(y, y + x_length), z_vec(z, z + x_length),
          l_vec(levels, levels + num_levels);
      auto temp = global_render->createTriContour("px" + str, x_vec, "py" + str, y_vec, "pz" + str, z_vec,
                                                  "levels" + str, l_vec);

      subGroup->append(temp);
      ++current_series;
    }
  plot_draw_axes(subplot_args, 2);
  plot_draw_colorbar(subplot_args, 0.05, 256);
  free(levels);

  return ERROR_NONE;
}

err_t plot_shade(grm_args_t *subplot_args)
{
  grm_args_t **current_shader;
  const char *data_component_names[] = {"x", "y", NULL};
  double *components[2];
  /* char *spec = ""; TODO: read spec from data! */
  int xform, xbins, ybins;
  double **current_component = components;
  const char **current_component_name = data_component_names;
  unsigned int point_count;

  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  group->setAttribute("name", "shade");

  grm_args_values(subplot_args, "series", "A", &current_shader);
  while (*current_component_name != NULL)
    {
      grm_args_first_value(*current_shader, *current_component_name, "D", current_component, &point_count);
      ++current_component_name;
      ++current_component;
    }
  if (!grm_args_values(subplot_args, "xform", "i", &xform))
    {
      xform = 1;
    }
  if (!grm_args_values(subplot_args, "xbins", "i", &xbins))
    {
      xbins = 100;
    }
  if (!grm_args_values(subplot_args, "ybins", "i", &ybins))
    {
      ybins = 100;
    }

  int id = (int)global_root->getAttribute("id");
  global_root->setAttribute("id", id + 1);
  auto str = std::to_string(id);

  std::vector<double> c0(components[0], components[0] + point_count);
  std::vector<double> c1(components[1], components[1] + point_count);

  auto temp = global_render->createShadePoints("x" + str, c0, "y" + str, c1, xform, xbins, ybins);
  group->append(temp);
  return ERROR_NONE;
}

err_t plot_raw(grm_args_t *plot_args)
{
  const char *base64_data = NULL;
  char *graphics_data = NULL;
  err_t error = ERROR_NONE;
  std::vector<int> data_vec;

  cleanup_and_set_error_if(!grm_args_values(plot_args, "raw", "s", &base64_data), ERROR_PLOT_MISSING_DATA);
  graphics_data = base64_decode(NULL, base64_data, NULL, &error);
  cleanup_if_error;

  global_root->append(global_render->createClearWS());
  data_vec = std::vector<int>(graphics_data, graphics_data + strlen(graphics_data));
  global_root->append(global_render->createDrawGraphics("graphics", data_vec));
  global_root->append(global_render->createUpdateWS());

cleanup:
  if (graphics_data != NULL)
    {
      free(graphics_data);
    }

  return error;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~ auxiliary drawing functions ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

err_t plot_draw_axes(grm_args_t *args, unsigned int pass)
{
  // TODO: GR3 dom render integration and barplot
  // TODO: Move calculations to dom render for char height etc. (charheight and ticksize is now calced in the renderer
  // however some textpositions depend on charheight, vp and viewport)

  const char *kind = NULL;
  const double *viewport, *vp;
  double x_tick;
  double x_org_low, x_org_high;
  int x_major_count;
  int x_grid;
  double y_tick;
  double y_org_low, y_org_high;
  int y_major_count;
  int y_grid;
  double z_tick;
  double z_org_low, z_org_high;
  int z_major_count;
  int z_grid;
  double charheight;
  char *title;
  char *x_label, *y_label, *z_label;
  int tick_orientation = 1;


  auto group = global_render->createGroup("draw_axes");
  if (!currentDomElement)
    {
      global_root->lastChildElement()->append(group);
    }
  else
    {
      currentDomElement->append(group);
    }
  grm_args_values(args, "kind", "s", &kind);
  grm_args_values(args, "xtick", "d", &x_tick);
  grm_args_values(args, "xorg", "dd", &x_org_low, &x_org_high);
  grm_args_values(args, "xmajor", "i", &x_major_count);
  grm_args_values(args, "xgrid", "i", &x_grid);
  grm_args_values(args, "ytick", "d", &y_tick);
  grm_args_values(args, "yorg", "dd", &y_org_low, &y_org_high);
  grm_args_values(args, "ymajor", "i", &y_major_count);
  grm_args_values(args, "ygrid", "i", &y_grid);

  global_render->setLineColorInd(group, 1);
  global_render->setLineWidth(group, 1);

  if (str_equals_any(kind, 6, "wireframe", "surface", "plot3", "scatter3", "trisurf", "volume"))
    {
      grm_args_values(args, "ztick", "d", &z_tick);
      grm_args_values(args, "zorg", "dd", &z_org_low, &z_org_high);
      grm_args_values(args, "zmajor", "i", &z_major_count);
      grm_args_values(args, "zgrid", "i", &z_grid);
      if (pass == 1)
        {
          auto temp = global_render->createGrid3d(x_grid ? x_tick : 0, 0, z_grid ? z_tick : 0, x_org_low, y_org_high,
                                                  z_org_low, 2, 0, 2);
          group->append(temp);
          temp = global_render->createGrid3d(0, y_grid ? y_tick : 0, 0, x_org_low, y_org_high, z_org_low, 0, 2, 0);
          group->append(temp);
        }
      else
        {
          auto temp = global_render->createAxes3d(x_tick, 0, z_tick, x_org_low, y_org_low, z_org_low, x_major_count, 0,
                                                  z_major_count, -tick_orientation);
          group->append(temp);
          temp = global_render->createAxes3d(0, y_tick, 0, x_org_high, y_org_low, z_org_low, 0, y_major_count, 0,
                                             tick_orientation);
          group->append(temp);
        }
    }
  else
    {
      if (str_equals_any(kind, 2, "heatmap", "shade"))
        {
          tick_orientation = -1;
        }
      if (!str_equals_any(kind, 1, "shade"))
        {
          if (pass == 1 || strcmp(kind, "barplot") != 0)
            {
              group->append(global_render->createGrid(x_grid ? x_tick : 0, y_grid ? y_tick : 0, 0, 0, x_major_count,
                                                      y_major_count));
            }
        }
      group->append(global_render->createAxes(x_tick, y_tick, x_org_low, y_org_low, x_major_count, y_major_count,
                                              tick_orientation));
      group->append(global_render->createAxes(x_tick, y_tick, x_org_high, y_org_high, -x_major_count, -y_major_count,
                                              -tick_orientation));
    }

  if (grm_args_values(args, "title", "s", &title))
    {
      group->setAttribute("title", title);
    }

  if (str_equals_any(kind, 6, "wireframe", "surface", "plot3", "scatter3", "trisurf", "volume"))
    {
      if (grm_args_values(args, "xlabel", "s", &x_label) && grm_args_values(args, "ylabel", "s", &y_label) &&
          grm_args_values(args, "zlabel", "s", &z_label))
        {
          group->append(global_render->createTitles3d(x_label, y_label, z_label));
        }
    }
  else
    {
      if (grm_args_values(args, "xlabel", "s", &x_label))
        {
          group->setAttribute("xlabel", x_label);
        }
      if (grm_args_values(args, "ylabel", "s", &y_label))
        {
          group->setAttribute("ylabel", y_label);
        }
    }

  if (strcmp("barplot", kind) == 0 && pass == 2)
    {
      /* xticklabels */
      char **xticklabels = NULL;
      unsigned int xticklabels_length;
      int i;
      double x[2] = {x_org_low, x_org_high};
      double y[2] = {0, 0};

      if (grm_args_first_value(args, "xticklabels", "S", &xticklabels, &xticklabels_length))
        {
          std::vector<std::string> xticklabels_vec(xticklabels, xticklabels + xticklabels_length);
          int id = static_cast<int>(global_root->getAttribute("id"));
          std::string key = "xticklabels" + std::to_string(id);
          global_root->setAttribute("id", ++id);
          global_render->setXTickLabels(group, key, xticklabels_vec);
        }

      /* negative values */
      if (y_org_low < 0)
        {
          group->append(global_render->createPolyline(x[0], x[1], y[0], y[1]));
        }
    }

  return ERROR_NONE;
}

err_t plot_draw_polar_axes(grm_args_t *args)
{
  const double *window, *viewport, *vp;
  double diag;
  double charheight;
  double r_min, r_max = -1.0;
  double tick;
  double x[2], y[2];
  int i, n;
  double alpha;
  char text_buffer[PLOT_POLAR_AXES_TEXT_BUFFER];
  char *kind;
  int angle_ticks, rings;
  int phiflip = 0;
  double interval;
  char *title;
  char *norm;

  /* TODO: Move calculations containing viewport to the renderer */
  // TODO: Solve r_max issues. render order: polar axes then polar histogram. polar axes calcs r_max and stores it in
  // TODO: the dom, so that polar histogram can access it.

  grm_args_values(args, "vp", "D", &vp);


  if (grm_args_values(args, "angle_ticks", "i", &angle_ticks) == 0)
    {
      angle_ticks = 8;
    }
  if (grm_args_values(args, "rings", "i", &rings) == 0)
    {
      rings = 4;
    }

  grm_args_values(args, "kind", "s", &kind);

  if (strcmp(kind, "polar_histogram") == 0)
    {
      r_min = 0.0;
      if (grm_args_values(args, "normalization", "s", &norm) == 0)
        {
          norm = "count";
        }

      grm_args_values(args, "r_max", "d", &r_max);
    }

  if (grm_args_values(args, "phiflip", "i", &phiflip) == 0) phiflip = 0;


  if (!grm_args_values(args, "title", "s", &title))
    {
      title = "";
    }

  auto group = global_render->createDrawPolarAxes(angle_ticks, rings, kind, phiflip, vp[0], vp[1], vp[2], vp[3], title,
                                                  norm, r_max);

  if (!currentDomElement)
    {
      global_root->lastChildElement()->append(group);
    }
  else
    {
      currentDomElement->append(group);
    }
  return ERROR_NONE;
}

err_t plot_draw_legend(grm_args_t *subplot_args)
{
  const char **labels, **current_label;
  unsigned int num_labels, num_series;
  grm_args_t **current_series;
  const double *viewport;
  int location;
  double px, py, w, h;
  double tbx[4], tby[4];
  double legend_symbol_x[2], legend_symbol_y[2];
  int i;

  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();

  return_error_if(!grm_args_first_value(subplot_args, "labels", "S", &labels, &num_labels), ERROR_PLOT_MISSING_LABELS);
  logger((stderr, "Draw a legend with %d labels\n", num_labels));
  grm_args_values(subplot_args, "location", "i", &location);
  grm_args_first_value(subplot_args, "series", "A", &current_series, &num_series);

  int id = static_cast<int>(global_root->getAttribute("id"));
  global_root->setAttribute("id", ++id);

  std::string labels_key = std::to_string(id) + "labels";
  std::string specs_key = std::to_string(id) + "specs";
  std::vector<std::string> labels_vec(labels, labels + num_labels);

  std::vector<std::string> specs_vec;
  while (*current_series != NULL)
    {
      char *spec;
      grm_args_values(*current_series, "spec", "s", &spec); /* `spec` is always set */
      specs_vec.push_back(std::string(spec));
      ++current_series;
    }

  auto subGroup = global_render->createDrawLegend(labels_key, labels_vec, location, specs_key, specs_vec);
  group->append(subGroup);

  return ERROR_NONE;
}

err_t plot_draw_pie_legend(grm_args_t *subplot_args)
{
  const char **labels;
  unsigned int num_labels;
  grm_args_t *series;

  static const int *color_indices = NULL;
  static const double *color_rgb_values = NULL;
  static unsigned int color_array_length = -1;
  int useFallback = 0;

  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();

  return_error_if(!grm_args_first_value(subplot_args, "labels", "S", &labels, &num_labels), ERROR_PLOT_MISSING_LABELS);
  grm_args_values(subplot_args, "series", "a", &series); /* series exists always */

  int id = static_cast<int>(global_root->getAttribute("id"));
  global_root->setAttribute("id", id + 1);
  std::string labels_key = "labels" + std::to_string(id);
  std::vector<std::string> labels_vec(labels, labels + num_labels);

  auto drawPieLegendElement = global_render->createDrawPieLegend(labels_key, labels_vec);
  group->append(drawPieLegendElement);

  return ERROR_NONE;
}

err_t plot_draw_colorbar(grm_args_t *subplot_args, double off, unsigned int colors)
{
  // TODO: move viewport dependent calcs to render
  double c_min, c_max;
  int *data;
  int scale, flip, options;
  unsigned int i;

  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  auto colorbar_group = global_render->createGroup("colorbar");
  group->append(colorbar_group);

  gr_savestate();
  /* TODO: What to do, if there is a `_clim` and a `_zlim`? Merge both together? */
  if (!grm_args_values(subplot_args, "_clim", "dd", &c_min, &c_max))
    {
      if (!grm_args_values(subplot_args, "_zlim", "dd", &c_min, &c_max))
        {
          return ERROR_PLOT_MISSING_DATA;
        }
    }
  data = static_cast<int *>(malloc(colors * sizeof(int)));
  if (data == NULL)
    {
      debug_print_malloc_error();
      return ERROR_MALLOC;
    }
  for (i = 0; i < colors; ++i)
    {
      data[i] = 1000 + 255 * i / (colors - 1);
    }
  gr_inqscale(&options);
  if (grm_args_values(subplot_args, "xflip", "i", &flip) && flip)
    {
      options = (options | GR_OPTION_FLIP_Y) & ~GR_OPTION_FLIP_X;
      global_render->setScale(colorbar_group, options);
    }
  else if (grm_args_values(subplot_args, "yflip", "i", &flip) && flip)
    {
      options = options & ~GR_OPTION_FLIP_Y & ~GR_OPTION_FLIP_X;
      global_render->setScale(colorbar_group, options);
    }
  else
    {
      options = options & ~GR_OPTION_FLIP_X;
      global_render->setScale(colorbar_group, options);
    }
  global_render->setWindow(colorbar_group, 0.0, 1.0, c_min, c_max);
  colorbar_group->setAttribute("offset", off + 0.02);
  colorbar_group->setAttribute("width", 0.03);
  colorbar_group->setAttribute("colorbar-position", true);

  std::vector<int> data_vec = std::vector<int>(data, data + colors);
  int id = (int)global_root->getAttribute("id");
  std::string str = std::to_string(id);
  global_root->setAttribute("id", id + 1);

  auto cellArray =
      global_render->createCellArray(0, 1, c_max, c_min, 1, colors, 1, 1, 1, colors, "data" + str, data_vec);
  colorbar_group->append(cellArray);

  colorbar_group->setAttribute("diag_factor", 0.016);
  colorbar_group->setAttribute("max_charheight", 0.012);
  colorbar_group->setAttribute("relative-charheight", true);
  grm_args_values(subplot_args, "scale", "i", &scale);
  if (scale & GR_OPTION_Z_LOG)
    {
      global_render->setScale(colorbar_group, GR_OPTION_Y_LOG);
      auto axes = global_render->createAxes(0, 2, 1, c_min, 0, 1, 1);
      axes->setAttribute("tick_size", 0.005);
      colorbar_group->append(axes);
    }
  else
    {
      double c_tick = auto_tick(c_min, c_max);
      auto axes = global_render->createAxes(0, c_tick, 1, c_min, 0, 1, 1);
      axes->setAttribute("tick_size", 0.005);
      colorbar_group->append(axes);
    }
  free(data);
  gr_restorestate();

  return ERROR_NONE;
}

err_t extract_multi_type_argument(grm_args_t *error_container, const char *key, unsigned int x_length,
                                  unsigned int *downwards_length, unsigned int *upwards_length, double **downwards,
                                  double **upwards, double *downwards_flt, double *upwards_flt)
{
  arg_t *arg_ptr;
  args_value_iterator_t *value_it;
  unsigned int length;
  int i, *ii;

  arg_ptr = args_at(error_container, key);
  if (!arg_ptr)
    {
      return ERROR_NONE;
    }
  if (strcmp(arg_ptr->value_format, "nDnD") == 0)
    {
      value_it = arg_value_iter(arg_ptr);
      ARGS_VALUE_ITERATOR_GET(value_it, *downwards_length, *downwards);
      ARGS_VALUE_ITERATOR_GET(value_it, *upwards_length, *upwards);
      args_value_iterator_delete(value_it);

      return_error_if(*downwards_length != *upwards_length || *downwards_length != x_length,
                      ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
    }
  else if (strcmp(arg_ptr->value_format, "nD") == 0)
    {
      return_error_if(!grm_args_first_value(error_container, key, "D", downwards, downwards_length), ERROR_INTERNAL);
      /* Python encapsules all single elements into an array */
      if (*downwards_length == 1)
        {
          *downwards_flt = *upwards_flt = **downwards;
          *downwards = NULL;
          *downwards_length = 0;
          return ERROR_NONE;
        }
      return_error_if(*downwards_length != x_length, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
      *upwards = *downwards;
      *upwards_length = *downwards_length;
    }
  else if (strcmp(arg_ptr->value_format, "d") == 0)
    {
      return_error_if(!grm_args_values(error_container, key, "d", downwards_flt), ERROR_INTERNAL);
      *upwards_flt = *downwards_flt;
    }
  else if (strcmp(arg_ptr->value_format, "nI") == 0)
    {
      return_error_if(!grm_args_first_value(error_container, key, "nI", &ii, &length), ERROR_INTERNAL);
      return_error_if(length != 1, ERROR_PLOT_COMPONENT_LENGTH_MISMATCH);
      *upwards_flt = *downwards_flt = (double)ii[0];
    }
  else if (strcmp(arg_ptr->value_format, "i") == 0)
    {
      return_error_if(!grm_args_values(error_container, key, "i", &i), ERROR_INTERNAL);
      *upwards_flt = *downwards_flt = (double)i;
    }
  return ERROR_NONE;
}

err_t plot_draw_errorbars(grm_args_t *series_args, double *x, unsigned int x_length, double *y, const char *kind)
{
  grm_args_t *error_container;
  arg_t *arg_ptr;
  err_t error;

  double *absolute_upwards, *absolute_downwards, *relative_upwards, *relative_downwards;
  double absolute_upwards_flt, relative_upwards_flt, absolute_downwards_flt, relative_downwards_flt;
  unsigned int upwards_length, downwards_length, i;
  int scale_options, color_upwardscap, color_downwardscap, color_errorbar;
  int is_barplot;

  double marker_size, xmin, xmax, ymin, ymax, tick, a, b, e_upwards, e_downwards, x_value;
  double line_x[2], line_y[2];
  absolute_upwards = absolute_downwards = relative_upwards = relative_downwards = NULL;
  absolute_upwards_flt = absolute_downwards_flt = relative_upwards_flt = relative_downwards_flt = FLT_MAX;
  is_barplot = strcmp(kind, "barplot") == 0 ? 1 : 0;

  auto group = global_render->createGroup("draw_errorbars");
  if (currentDomElement)
    {
      currentDomElement->append(group);
    }
  else
    {
      global_root->lastChildElement()->append(group);
    }

  arg_ptr = args_at(series_args, "error");
  if (!arg_ptr)
    {
      return ERROR_NONE;
    }
  error_container = NULL;
  if (strcmp(arg_ptr->value_format, "a") == 0 || strcmp(arg_ptr->value_format, "nA") == 0)
    {
      return_error_if(!grm_args_values(series_args, "error", "a", &error_container), ERROR_INTERNAL);

      error = extract_multi_type_argument(error_container, "absolute", x_length - is_barplot, &downwards_length,
                                          &upwards_length, &absolute_downwards, &absolute_upwards,
                                          &absolute_downwards_flt, &absolute_upwards_flt);
      return_if_error;
      error = extract_multi_type_argument(error_container, "relative", x_length - is_barplot, &downwards_length,
                                          &upwards_length, &relative_downwards, &relative_upwards,
                                          &relative_downwards_flt, &relative_upwards_flt);
      return_if_error;
    }
  else
    {
      error = extract_multi_type_argument(series_args, "error", x_length - is_barplot, &downwards_length,
                                          &upwards_length, &absolute_downwards, &absolute_upwards,
                                          &absolute_downwards_flt, &absolute_upwards_flt);
      return_if_error;
    }

  if (absolute_upwards == NULL && relative_upwards == NULL && absolute_upwards_flt == FLT_MAX &&
      relative_upwards_flt == FLT_MAX && absolute_downwards == NULL && relative_downwards == NULL &&
      absolute_downwards_flt == FLT_MAX && relative_downwards_flt == FLT_MAX)
    {
      return ERROR_PLOT_MISSING_DATA;
    }

  /* Getting GR options and sizes. See gr_verrorbars. */
  gr_savestate();
  gr_inqmarkersize(&marker_size);
  gr_inqwindow(&xmin, &xmax, &ymin, &ymax);
  gr_inqscale(&scale_options);
  tick = marker_size * 0.0075 * (xmax - xmin);
  a = (xmax - xmin) / log10(xmax / xmin);
  b = xmin - a * log10(xmin);

  gr_inqlinecolorind(&color_errorbar);
  color_upwardscap = color_downwardscap = color_errorbar;
  if (error_container != NULL)
    {
      grm_args_values(error_container, "upwardscap_color", "i", &color_upwardscap);
      grm_args_values(error_container, "downwardscap_color", "i", &color_downwardscap);
      grm_args_values(error_container, "errorbar_color", "i", &color_errorbar);
    }

  /* Actual drawing of bars */
  e_upwards = e_downwards = FLT_MAX;
  for (i = 0; i < x_length - is_barplot; i++)
    {
      if (absolute_upwards != NULL || relative_upwards != NULL || absolute_upwards_flt != FLT_MAX ||
          relative_upwards_flt != FLT_MAX)
        {
          e_upwards =
              y[i] * (1. + (relative_upwards != NULL ? relative_upwards[i]
                                                     : (relative_upwards_flt != FLT_MAX ? relative_upwards_flt : 0))) +
              (absolute_upwards != NULL ? absolute_upwards[i]
                                        : (absolute_upwards_flt != FLT_MAX ? absolute_upwards_flt : 0.));
        }

      if (absolute_downwards != NULL || relative_downwards != NULL || absolute_downwards_flt != FLT_MAX ||
          relative_downwards_flt != FLT_MAX)
        {
          e_downwards =
              y[i] * (1. - (relative_downwards != NULL
                                ? relative_downwards[i]
                                : (relative_downwards_flt != FLT_MAX ? relative_downwards_flt : 0))) -
              (absolute_downwards != NULL ? absolute_downwards[i]
                                          : (absolute_downwards_flt != FLT_MAX ? absolute_downwards_flt : 0.));
        }

      /* See gr_verrorbars for reference */
      x_value = is_barplot ? (x[i] + x[i + 1]) / 2 : x[i];
      line_x[0] = X_LOG(X_LIN(x_value - tick, scale_options, xmin, xmax, a, b), scale_options, xmin, xmax, a, b);
      line_x[1] = X_LOG(X_LIN(x_value + tick, scale_options, xmin, xmax, a, b), scale_options, xmin, xmax, a, b);
      if (e_upwards != FLT_MAX && color_upwardscap >= 0)
        {
          line_y[0] = e_upwards;
          line_y[1] = e_upwards;
          group->append(
              global_render->createPolyline(line_x[0], line_x[1], line_y[0], line_y[1], 0, 0.0, color_upwardscap));
        }

      if (e_downwards != FLT_MAX && color_downwardscap >= 0)
        {
          line_y[0] = e_downwards;
          line_y[1] = e_downwards;
          group->append(
              global_render->createPolyline(line_x[0], line_x[1], line_y[0], line_y[1], 0, 0.0, color_downwardscap));
        }

      if (color_errorbar >= 0)
        {
          line_x[0] = x_value;
          line_x[1] = x_value;
          line_y[0] = e_upwards != FLT_MAX ? e_upwards : y[i];
          line_y[1] = e_downwards != FLT_MAX ? e_downwards : y[i];
          group->append(
              global_render->createPolyline(line_x[0], line_x[1], line_y[0], line_y[1], 0, 0.0, color_errorbar));
        }
    }
  gr_restorestate();

  return ERROR_NONE;
}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~ util ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

double find_max_step(unsigned int n, const double *x)
{
  double max_step = 0.0;
  unsigned int i;

  if (n < 2)
    {
      return 0.0;
    }
  for (i = 1; i < n; ++i)
    {
      max_step = grm_max(x[i] - x[i - 1], max_step);
    }

  return max_step;
}

/*!
 * Normalize a given array of doubles so all values sum up to 1.0
 *
 * \param[in] n The number of array elements.
 * \param[in] x A pointer to the array elements.
 * \return A pointer to newly alloced heap memory with the normalized values.
 */
double *normalize(unsigned int n, const double *x)
{
  double sum;
  double *normalized_x;
  unsigned int i;

  sum = 0.0;
  for (i = 0; i < n; ++i)
    {
      sum += x[i];
    }

  normalized_x = static_cast<double *>(malloc(n * sizeof(double)));
  if (normalized_x == NULL)
    {
      debug_print_malloc_error();
      return NULL;
    }

  for (i = 0; i < n; ++i)
    {
      normalized_x[i] = x[i] / sum;
    }

  return normalized_x;
}

/*!
 * Normalize a given array of doubles so all values sum up to `sum`.
 * All values are converted to unsigned integers. It is guaranteed that
 * the sum of all values is always `sum` (rounding errors are handled).
 *
 * \param[in] n The number of array elements.
 * \param[in] x A pointer to the array elements.
 * \return A pointer to newly alloced heap memory with the normalized values.
 */
unsigned int *normalize_int(unsigned int n, const double *x, unsigned int sum)
{
  double sum_x;
  unsigned int *normalized_x;
  unsigned int actual_sum;
  int rounding_error;
  double normalized_x_without_rounding;
  double current_relative_error;
  double min_relative_error;
  unsigned int min_relative_error_index;
  unsigned int i;

  sum_x = 0.0;
  for (i = 0; i < n; ++i)
    {
      sum_x += x[i];
    }

  normalized_x = static_cast<unsigned int *>(malloc(n * sizeof(unsigned int)));
  if (normalized_x == NULL)
    {
      debug_print_malloc_error();
      return NULL;
    }

  for (i = 0; i < n; ++i)
    {
      normalized_x[i] = (int)((x[i] * sum / sum_x) + 0.5);
    }

  actual_sum = 0;
  for (i = 0; i < n; ++i)
    {
      actual_sum += normalized_x[i];
    }
  rounding_error = sum - actual_sum;

  if (rounding_error != 0)
    {
      /*
       * Find the data value which gets the lowest relative error
       * when the rounding error is added.
       */
      min_relative_error = INFINITY;
      min_relative_error_index = 0;
      for (i = 0; i < n; ++i)
        {
          normalized_x_without_rounding = x[i] * sum / sum_x;
          current_relative_error =
              fabs(normalized_x[i] + rounding_error - normalized_x_without_rounding) / normalized_x_without_rounding;
          if (current_relative_error < min_relative_error)
            {
              min_relative_error = current_relative_error;
              min_relative_error_index = i;
            }
        }
      /* Apply the rounding error to the previously found data value */
      normalized_x[min_relative_error_index] += rounding_error;
    }

  return normalized_x;
}

const char *next_fmt_key(const char *kind)
{
  static const char *saved_fmt = NULL;
  static char fmt_key[2] = {0, 0};

  if (kind != NULL)
    {
      string_map_at(fmt_map, kind, static_cast<const char **>(&saved_fmt));
    }
  if (saved_fmt == NULL)
    {
      return NULL;
    }
  fmt_key[0] = *saved_fmt;
  if (*saved_fmt != '\0')
    {
      ++saved_fmt;
    }

  return fmt_key;
}

const char *get_compatible_format(const char *key, const char *given_format)
{
  const char **valid_formats;
  char *reduced_given_format;
  const char **current_format_ptr;
  const char *compatible_format = NULL;
  /* First, get all valid formats */
  if (!string_array_map_at(type_map, key, (char ***)&valid_formats))
    {
      /* If the given key does not exist, there is no type constraint
       * -> simply return the same type that was given */
      return given_format;
    }
  /* Second, filter the given format -> remove `n` chars because they are irrelevant for the following tests */
  reduced_given_format = str_filter(given_format, "n");
  if (reduced_given_format == NULL)
    {
      debug_print_malloc_error();
      goto cleanup;
    }
  /* Third, iterate over all valid formats and check if
   * - there is an exact match
   * - a single format char is valid,
   *   the type matches (e.g. `d` and `D`) and
   *   a single value / array or multiple single values of the same type ar given (e.g. `dddd`)
   */
  current_format_ptr = valid_formats;
  while (*current_format_ptr != NULL)
    {
      if (strcmp(*current_format_ptr, reduced_given_format) == 0)
        {
          /* Found an exact match */
          compatible_format = *current_format_ptr;
          break;
        }
      else if ((strlen(*current_format_ptr) == 1) && (tolower(**current_format_ptr) == tolower(*reduced_given_format)))
        {
          /* The current format is a single format of the same base type as the given format */
          if (strlen(reduced_given_format) == 1 ||
              is_homogenous_string_of_char(reduced_given_format, tolower(*reduced_given_format)))
            {
              /* Either a single format of the same type or
               * a homogenous format string of single values of the same type
               * is given */
              compatible_format = *current_format_ptr;
              break;
            }
        }
      ++current_format_ptr;
    }

cleanup:
  free(reduced_given_format);

  /* The previous tests were not successful -> `NULL` indicates no compatible format */
  return compatible_format;
}

int get_id_from_args(const grm_args_t *args, int *plot_id, int *subplot_id, int *series_id)
{
  const char *combined_id;
  int _plot_id = -1, _subplot_id = 0, _series_id = 0;

  if (grm_args_values(args, "id", "s", &combined_id))
    {
      const char *valid_id_delims = ":.";
      int *id_ptrs[4], **current_id_ptr;
      char *copied_id_str, *current_id_str;
      size_t segment_length;
      int is_last_segment;

      id_ptrs[0] = &_plot_id;
      id_ptrs[1] = &_subplot_id;
      id_ptrs[2] = &_series_id;
      id_ptrs[3] = NULL;
      if ((copied_id_str = gks_strdup(combined_id)) == NULL)
        {
          debug_print_malloc_error();
          return 0;
        }

      current_id_ptr = id_ptrs;
      current_id_str = copied_id_str;
      is_last_segment = 0;
      while (*current_id_ptr != NULL && !is_last_segment)
        {
          segment_length = strcspn(current_id_str, valid_id_delims);
          if (current_id_str[segment_length] == '\0')
            {
              is_last_segment = 1;
            }
          else
            {
              current_id_str[segment_length] = '\0';
            }
          if (*current_id_str != '\0')
            {
              if (!str_to_uint(current_id_str, (unsigned int *)*current_id_ptr))
                {
                  logger((stderr, "Got an invalid id \"%s\"\n", current_id_str));
                }
              else
                {
                  logger((stderr, "Read id: %d\n", **current_id_ptr));
                }
            }
          ++current_id_ptr;
          ++current_id_str;
        }

      free(copied_id_str);
    }
  else
    {
      grm_args_values(args, "plot_id", "i", &_plot_id);
      grm_args_values(args, "subplot_id", "i", &_subplot_id);
      grm_args_values(args, "series_id", "i", &_series_id);
    }
  /* plot id `0` references the first plot object (implicit object) -> handle it as the first plot and shift all ids by
   * one */
  *plot_id = _plot_id + 1;
  *subplot_id = _subplot_id;
  *series_id = _series_id;

  return _plot_id > 0 || _subplot_id > 0 || _series_id > 0;
}

int get_figure_size(const grm_args_t *plot_args, int *pixel_width, int *pixel_height, double *metric_width,
                    double *metric_height)
{
  double display_metric_width, display_metric_height;
  int display_pixel_width, display_pixel_height;
  double dpm[2], dpi[2];
  int tmp_size_i[2], pixel_size[2];
  double tmp_size_d[2], metric_size[2];
  grm_args_ptr_t tmp_size_a[2];
  const char *tmp_size_s[2];
  int i;

  if (plot_args == NULL)
    {
      plot_args = active_plot_args;
    }

#ifdef __EMSCRIPTEN__
  display_metric_width = 0.16384;
  display_metric_height = 0.12288;
  display_pixel_width = 640;
  display_pixel_height = 480;
#else
  gr_inqdspsize(&display_metric_width, &display_metric_height, &display_pixel_width, &display_pixel_height);
#endif
  dpm[0] = display_pixel_width / display_metric_width;
  dpm[1] = display_pixel_height / display_metric_height;
  dpi[0] = dpm[0] * 0.0254;
  dpi[1] = dpm[1] * 0.0254;

  /* TODO: Overwork this calculation */
  if (grm_args_values(plot_args, "figsize", "dd", &tmp_size_d[0], &tmp_size_d[1]))
    {
      for (i = 0; i < 2; ++i)
        {
          pixel_size[i] = (int)grm_round(tmp_size_d[i] * dpi[i]);
          metric_size[i] = tmp_size_d[i] / 0.0254;
        }
    }
  else if (grm_args_values(plot_args, "size", "dd", &tmp_size_d[0], &tmp_size_d[1]))
    {
      for (i = 0; i < 2; ++i)
        {
          pixel_size[i] = (int)grm_round(tmp_size_d[i]);
          metric_size[i] = tmp_size_d[i] / dpm[i];
        }
    }
  else if (grm_args_values(plot_args, "size", "ii", &tmp_size_i[0], &tmp_size_i[1]))
    {
      for (i = 0; i < 2; ++i)
        {
          pixel_size[i] = tmp_size_i[i];
          metric_size[i] = tmp_size_i[i] / dpm[i];
        }
    }
  else if (grm_args_values(plot_args, "size", "aa", &tmp_size_a[0], &tmp_size_a[1]))
    {
      for (i = 0; i < 2; ++i)
        {
          double pixels_per_unit = 1;
          if (grm_args_values(tmp_size_a[i], "unit", "s", &tmp_size_s[i]))
            {
              if (strcmp(tmp_size_s[i], "px") != 0)
                {
                  double meters_per_unit;
                  if (double_map_at(meters_per_unit_map, tmp_size_s[i], &meters_per_unit))
                    {
                      pixels_per_unit = meters_per_unit * dpm[i];
                    }
                  else
                    {
                      debug_print_error(("The unit %s is unknown.\n", tmp_size_s[i]));
                    }
                }
            }
          if (grm_args_values(tmp_size_a[i], "value", "i", &tmp_size_i[i]))
            {
              tmp_size_d[i] = tmp_size_i[i] * pixels_per_unit;
            }
          else if (grm_args_values(tmp_size_a[i], "value", "d", &tmp_size_d[i]))
            {
              tmp_size_d[i] = tmp_size_d[i] * pixels_per_unit;
            }
          else
            {
              /* If no value is given, fall back to default value */
              return 0;
            }
          pixel_size[i] = (int)grm_round(tmp_size_d[i]);
          metric_size[i] = tmp_size_d[i] / dpm[i];
        }
    }
  else
    {
      /* If this branch is executed, there is an internal error (size has a default value if not set by the user) */
      return 0;
    }

  logger((stderr, "figure pixel size: (%d, %d)\n", pixel_size[0], pixel_size[1]));
  logger((stderr, "figure metric size: (%f, %f)\n", metric_size[0], metric_size[1]));
  logger((stderr, "device dpi: (%lf, %lf)\n", dpi[0], dpi[1]));

  if (pixel_width != NULL)
    {
      *pixel_width = pixel_size[0];
    }
  if (pixel_height != NULL)
    {
      *pixel_height = pixel_size[1];
    }
  if (metric_width != NULL)
    {
      *metric_width = metric_size[0];
    }
  if (metric_height != NULL)
    {
      *metric_height = metric_size[1];
    }

  return 1;
}

int get_focus_and_factor(const int x1, const int y1, const int x2, const int y2, const int keep_aspect_ratio,
                         double *factor_x, double *factor_y, double *focus_x, double *focus_y,
                         grm_args_t **subplot_args)
{
  double ndc_box_x[4], ndc_box_y[4];
  double ndc_left, ndc_top, ndc_right, ndc_bottom;
  const double *wswindow, *viewport;
  int width, height, max_width_height;

  get_figure_size(NULL, &width, &height, NULL, NULL);
  max_width_height = grm_max(width, height);

  if (x1 <= x2)
    {
      ndc_left = (double)x1 / max_width_height;
      ndc_right = (double)x2 / max_width_height;
    }
  else
    {
      ndc_left = (double)x2 / max_width_height;
      ndc_right = (double)x1 / max_width_height;
    }
  if (y1 <= y2)
    {
      ndc_top = (double)(height - y1) / max_width_height;
      ndc_bottom = (double)(height - y2) / max_width_height;
    }
  else
    {
      ndc_top = (double)(height - y2) / max_width_height;
      ndc_bottom = (double)(height - y1) / max_width_height;
    }

  ndc_box_x[0] = ndc_left;
  ndc_box_y[0] = ndc_bottom;
  ndc_box_x[1] = ndc_right;
  ndc_box_y[1] = ndc_bottom;
  ndc_box_x[2] = ndc_left;
  ndc_box_y[2] = ndc_top;
  ndc_box_x[3] = ndc_right;
  ndc_box_y[3] = ndc_top;
  *subplot_args = get_subplot_from_ndc_points(array_size(ndc_box_x), ndc_box_x, ndc_box_y);
  if (*subplot_args == NULL)
    {
      return 0;
    }
  grm_args_values(*subplot_args, "viewport", "D", &viewport);
  grm_args_values(active_plot_args, "wswindow", "D", &wswindow);

  *factor_x = abs(x1 - x2) / (width * (viewport[1] - viewport[0]) / (wswindow[1] - wswindow[0]));
  *factor_y = abs(y1 - y2) / (height * (viewport[3] - viewport[2]) / (wswindow[3] - wswindow[2]));
  if (keep_aspect_ratio)
    {
      if (*factor_x <= *factor_y)
        {
          *factor_x = *factor_y;
          if (x1 > x2)
            {
              ndc_left = ndc_right - *factor_x * (viewport[1] - viewport[0]);
            }
        }
      else
        {
          *factor_y = *factor_x;
          if (y1 > y2)
            {
              ndc_top = ndc_bottom + *factor_y * (viewport[3] - viewport[2]);
            }
        }
    }
  *focus_x = (ndc_left - *factor_x * viewport[0]) / (1 - *factor_x) - (viewport[0] + viewport[1]) / 2.0;
  *focus_y = (ndc_top - *factor_y * viewport[3]) / (1 - *factor_y) - (viewport[2] + viewport[3]) / 2.0;
  return 1;
}
grm_args_t *get_subplot_from_ndc_point(double x, double y)
{
  grm_args_t **subplot_args;
  const double *viewport;

  grm_args_values(active_plot_args, "subplots", "A", &subplot_args);
  while (*subplot_args != NULL)
    {
      if (grm_args_values(*subplot_args, "viewport", "D", &viewport))
        {
          if (viewport[0] <= x && x <= viewport[1] && viewport[2] <= y && y <= viewport[3])
            {
              unsigned int array_index;
              grm_args_values(*subplot_args, "array_index", "i", &array_index);
              logger((stderr, "Found subplot id \"%u\" for ndc point (%lf, %lf)\n", array_index + 1, x, y));

              return *subplot_args;
            }
        }
      ++subplot_args;
    }

  return NULL;
}

grm_args_t *get_subplot_from_ndc_points(unsigned int n, const double *x, const double *y)
{
  grm_args_t *subplot_args;
  unsigned int i;

  for (i = 0, subplot_args = NULL; i < n && subplot_args == NULL; ++i)
    {
      subplot_args = get_subplot_from_ndc_point(x[i], y[i]);
    }

  return subplot_args;
}

double *moivre(double r, int x, int n)
{
  double *result = static_cast<double *>(malloc(2 * sizeof(double)));
  if (result != NULL)
    {
      if (n != 0)
        {
          *result = pow(r, (1.0 / n)) * (cos(2.0 * x * M_PI / n));
          *(result + 1) = pow(r, (1.0 / n)) * (sin(2.0 * x * M_PI / n));
        }
      else
        {
          *result = 1.0;
          *(result + 1) = 0.0;
        }
    }
  return result;
}


/* like python list comprehension [factor * func(element) for element in list] saves values in result starting at start
 * index */

double *listcomprehension(double factor, double (*pFunction)(double), double *list, int num, int start, double *result)
{
  int i;
  if (result == NULL)
    {
      result = static_cast<double *>(malloc(num * sizeof(double)));
      if (result == NULL)
        {
          return NULL;
        }
    }

  for (i = 0; i < num; ++i)
    {
      result[start] = factor * (*pFunction)(list[i]);
      start++;
    }

  return result;
}

/*
 * mixes gr colormaps with size = size * size. If x and or y < 0
 * */
int *create_colormap(int x, int y, int size)
{
  int r, g, b, a;
  int outer, inner;
  int r1, g1, b1;
  int r2, g2, b2;
  int *colormap = NULL;
  if (x > 47 || y > 47)
    {
      logger((stderr, "values for the keyword \"colormap\" can not be greater than 47\n"));
      return NULL;
    }

  colormap = static_cast<int *>(malloc(size * size * sizeof(int)));
  if (colormap == NULL)
    {
      debug_print_malloc_error();
      return NULL;
    }
  if (x >= 0 && y < 0)
    {
      for (outer = 0; outer < size; outer++)
        {
          for (inner = 0; inner < size; inner++)
            {
              a = 255;
              r = ((cmap_h[x][(int)(inner * 255.0 / size)] >> 16) & 0xff);
              g = ((cmap_h[x][(int)(inner * 255.0 / size)] >> 8) & 0xff);
              b = (cmap_h[x][(int)(inner * 255.0 / size)] & 0xff);

              colormap[outer * size + inner] = (a << 24) + (b << 16) + (g << 8) + (r);
            }
        }
      return colormap;
    }

  if (x < 0 && y >= 0)
    {
      gr_setcolormap(y);
      for (outer = 0; outer < size; outer++)
        {
          for (inner = 0; inner < size; inner++)
            {
              a = 255;
              r = ((cmap_h[y][(int)(inner * 255.0 / size)] >> 16) & 0xff);
              g = ((cmap_h[y][(int)(inner * 255.0 / size)] >> 8) & 0xff);
              b = (cmap_h[y][(int)(inner * 255.0 / size)] & 0xff);

              colormap[inner * size + outer] = (a << 24) + (b << 16) + (g << 8) + (r);
            }
        }


      return colormap;
    }

  if ((x >= 0 && y >= 0) || (x < 0 && y < 0))
    {
      if (x < 0 && y < 0)
        {
          x = y = 0;
        }
      gr_setcolormap(x);
      for (outer = 0; outer < size; outer++)
        {
          for (inner = 0; inner < size; inner++)
            {
              a = 255;
              r1 = ((cmap_h[x][(int)(inner * 255.0 / size)] >> 16) & 0xff);
              g1 = ((cmap_h[x][(int)(inner * 255.0 / size)] >> 8) & 0xff);
              b1 = (cmap_h[x][(int)(inner * 255.0 / size)] & 0xff);

              r2 = ((cmap_h[y][(int)(outer * 255.0 / size)] >> 16) & 0xff);
              g2 = ((cmap_h[y][(int)(outer * 255.0 / size)] >> 8) & 0xff);
              b2 = (cmap_h[y][(int)(outer * 255.0 / size)] & 0xff);

              colormap[outer * size + inner] =
                  (a << 24) + (((b1 + b2) / 2) << 16) + (((g1 + g2) / 2) << 8) + ((r1 + r2) / 2);
            }
        }
      return colormap;
    }
  return NULL;
}


/*
 * Calculates the classes for polar histogram
 * */
err_t classes_polar_histogram(grm_args_t *subplot_args, double *r_max)
{
  unsigned int num_bins;
  double *theta = NULL;
  unsigned int length;
  const char *norm;
  double *classes = NULL;

  double interval;
  double start;
  double *p;
  double max;
  double temp_max;

  double *bin_edges = NULL;
  double *bin_edges_buf = NULL;
  unsigned int num_bin_edges;

  double bin_width;

  double *bin_widths = NULL;

  int is_bin_counts;
  int *bin_counts = NULL;

  double false_ = -1;

  double *philim = NULL;
  unsigned int dummy;

  double *new_theta = NULL;
  double *new_edges = NULL;

  grm_args_t **series;

  err_t error = ERROR_NONE;


  grm_args_values(subplot_args, "series", "A", &series);


  /* get theta or bin_counts */
  if (grm_args_values(*series, "bin_counts", "i", &is_bin_counts) == 0)
    {
      is_bin_counts = 0;
      grm_args_first_value(*series, "x", "D", &theta, &length);
    }
  else
    {
      grm_args_first_value(*series, "x", "I", &bin_counts, &length);
      is_bin_counts = 1;
      num_bins = length;
      grm_args_push(*series, "nbins", "i", num_bins);
    }

  if (grm_args_first_value(*series, "philim", "D", &philim, &dummy) == 0)
    {
      philim = &false_;
    }
  else
    {
      if (philim[0] < 0.0 || philim[1] > 2 * M_PI)
        {
          logger((stderr, "philim must be between 0 and 2 * pi\n"));
          cleanup_and_set_error(ERROR_PLOT_OUT_OF_RANGE);
        }
      if (philim[1] < philim[0])
        {
          double temp = philim[1];
          int phiflip;
          philim[1] = philim[0];
          philim[0] = temp;
          if (grm_args_values(subplot_args, "phiflip", "i", &phiflip) == 0)
            {
              phiflip = 1;
              grm_args_push(subplot_args, "phiflip", "i", phiflip);
            }
          else
            {
              grm_args_push(subplot_args, "phiflip", "i", 0);
            }
        }
    }


  /* bin_edges and nbins */
  if (grm_args_first_value(*series, "bin_edges", "D", &bin_edges, &num_bin_edges) == 0)
    {
      if (grm_args_values(*series, "nbins", "i", &num_bins) == 0)
        {
          num_bins = grm_min(12, (int)(length * 1.0 / 2) - 1);
          grm_args_push(*series, "nbins", "i", num_bins);
        }
      else
        {
          if (num_bins <= 0 || num_bins > 200)
            {
              num_bins = grm_min(12, (int)(length * 1.0 / 2) - 1);
              grm_args_push(*series, "nbins", "i", num_bins);
            }
        }
      if (*philim == -1.0)
        num_bin_edges = 0;
      else
        {
          bin_edges = bin_edges_buf = static_cast<double *>(malloc((num_bins + 1) * sizeof(double)));
          cleanup_and_set_error_if(bin_edges == NULL, ERROR_MALLOC);
          linspace(philim[0], philim[1], num_bins + 1, bin_edges);
          num_bin_edges = num_bins + 1;
          grm_args_push(*series, "bin_edges", "nD", num_bin_edges, bin_edges);
        }
    }
  /* with bin_edges */
  else
    {
      /* no philim */
      if (*philim == -1)
        {

          /* filter bin_edges */
          int temp = 0;
          int i;
          new_edges = static_cast<double *>(malloc(num_bin_edges * sizeof(double)));
          cleanup_and_set_error_if(new_edges == NULL, ERROR_MALLOC);

          for (i = 0; i < num_bin_edges; ++i)
            {
              if (0.0 <= bin_edges[i] && bin_edges[i] <= 2 * M_PI)
                {
                  new_edges[temp] = bin_edges[i];
                  temp++;
                }
              else
                {
                  logger((stderr, "Only values between 0 and 2 * pi allowed\n"));
                  cleanup_and_set_error(ERROR_PLOT_OUT_OF_RANGE);
                }
            }
          if (num_bin_edges > temp)
            {
              num_bin_edges = temp;
              bin_edges = bin_edges_buf = (double *)realloc(new_edges, temp * sizeof(double));
              cleanup_and_set_error_if(bin_edges == NULL, ERROR_MALLOC);
            }
          else
            {
              bin_edges = new_edges;
            }
          num_bins = num_bin_edges - 1;
          grm_args_push(*series, "nbins", "i", num_bins);
        }
      /* with philim and binedges */
      else
        {
          /* filter bin_edges */
          int temp = 0;
          new_edges = static_cast<double *>(malloc(num_bin_edges * sizeof(double)));
          cleanup_and_set_error_if(new_edges == NULL, ERROR_MALLOC);

          int i;
          for (i = 0; i < num_bin_edges; ++i)
            {
              if (philim[0] <= bin_edges[i] && bin_edges[i] <= philim[1])
                {
                  new_edges[temp] = bin_edges[i];
                  temp++;
                }
            }
          if (temp > 1)
            {
              if (num_bin_edges > temp)
                {
                  num_bin_edges = temp;
                  bin_edges = bin_edges_buf = (double *)realloc(new_edges, temp * sizeof(double));
                }
              else
                {
                  bin_edges = new_edges;
                }
            }
          if (num_bin_edges == 1)
            {
              logger(
                  (stderr, "given philim and given bin_edges are not compatible --> filtered len(bin_edges) == 1\n"));
              cleanup_and_set_error(ERROR_PLOT_INCOMPATIBLE_ARGUMENTS);
            }
          else
            {
              num_bins = num_bin_edges - 1;
              grm_args_push(*series, "nbins", "i", num_bins);
              grm_args_push(*series, "bin_edges", "nD", num_bin_edges, bin_edges);
            }
        }
    }


  if (grm_args_values(subplot_args, "normalization", "s", &norm) == 0)
    {
      norm = "count";
    }
  else
    {
      if (!str_equals_any(norm, 6, "count", "countdensity", "pdf", "probability", "cumcount", "cdf"))
        {
          logger((stderr, "Got keyword \"norm\"  with invalid value \"%s\"\n", norm));
          cleanup_and_set_error(ERROR_PLOT_NORMALIZATION);
        }
    }

  if (grm_args_values(*series, "bin_width", "d", &bin_width) == 0)
    {
      if (num_bin_edges > 0)
        {
          int i;
          bin_widths = static_cast<double *>(malloc((num_bins + 1) * sizeof(double)));
          cleanup_and_set_error_if(bin_widths == NULL, ERROR_MALLOC);

          for (i = 1; i <= num_bin_edges - 1; ++i)
            {
              *(bin_widths + i - 1) = *(bin_edges + i) - *(bin_edges + i - 1);
            }
          grm_args_push(*series, "bin_widths", "nD", num_bins, bin_widths);
        }
      else
        {
          bin_width = 2 * M_PI / num_bins;
          grm_args_push(*series, "bin_width", "d", bin_width);
        }
    }
  /* bin_width is given*/
  else
    {
      int n = 0;
      int temp;

      if (num_bin_edges > 0 && *philim == -1.0)
        {
          int i;
          logger((stderr, "bin_width is not compatible with bin_edges\n"));
          cleanup_and_set_error(ERROR_PLOT_INCOMPATIBLE_ARGUMENTS);

          bin_widths = static_cast<double *>(malloc(num_bins * sizeof(double)));
          cleanup_and_set_error_if(bin_widths == NULL, ERROR_MALLOC);

          for (i = 1; i <= num_bin_edges - 1; ++i)
            {
              *(bin_widths + i - 1) = *(bin_edges + i) - *(bin_edges + i - 1);
            }
          grm_args_push(*series, "bin_widths", "nD", num_bins, bin_widths);
        }

      /* with philim */
      if (*philim != -1)
        {
          if (bin_width < 0 || bin_width > 2 * M_PI)
            {
              logger((stderr, "bin_width must be between 0 and 2 * Pi\n"));
              cleanup_and_set_error(ERROR_PLOT_OUT_OF_RANGE);
            }
          if (philim[1] - philim[0] < bin_width)
            {
              logger((stderr, "the given philim range does not work with the given bin_width\n"));
              cleanup_and_set_error(ERROR_PLOT_INCOMPATIBLE_ARGUMENTS);
            }
          else
            {
              n = (int)((philim[1] - philim[0]) / bin_width);
              if (is_bin_counts == 1)
                {
                  if (num_bins > n)
                    {
                      logger((stderr,
                              "bin_width does not work with this specific bin_count. Nbins do not fit bin_width\n"));
                      cleanup_and_set_error(ERROR_PLOT_INCOMPATIBLE_ARGUMENTS);
                    }
                  n = num_bins;
                }
              bin_edges = bin_edges_buf = static_cast<double *>(malloc((n + 1) * sizeof(double)));
              cleanup_and_set_error_if(bin_edges == NULL, ERROR_MALLOC);

              linspace(philim[0], n * bin_width, n + 1, bin_edges);
            }
        }
      /* without philim */
      else
        {
          if (bin_width <= 0 || bin_width > 2 * M_PI)
            {
              logger((stderr, "bin_width must be between 0 (exclusive) and 2 * Pi\n"));
              cleanup_and_set_error(ERROR_PLOT_OUT_OF_RANGE);
            }
          else if ((int)(2 * M_PI / bin_width) > 200)
            {
              n = 200;
              bin_width = 2 * M_PI / n;
            }
          n = (int)(2 * M_PI / bin_width);
          if (is_bin_counts == 1)
            {
              if (num_bins > n)
                {
                  logger(
                      (stderr, "bin_width does not work with this specific bin_count. Nbins do not fit bin_width\n"));
                  cleanup_and_set_error(ERROR_PLOT_INCOMPATIBLE_ARGUMENTS);
                }
              n = num_bins;
            }
          bin_edges = bin_edges_buf = static_cast<double *>(malloc((n + 1) * sizeof(double)));
          cleanup_and_set_error_if(bin_edges == NULL, ERROR_MALLOC);

          linspace(0, n * bin_width, n + 1, bin_edges);
        }
      grm_args_push(*series, "nbins", "i", n);
      num_bin_edges = n + 1;
      num_bins = n;
      grm_args_push(*series, "bin_edges", "nD", num_bin_edges, bin_edges);
      grm_args_push(*series, "bin_width", "d", bin_width);
      bin_widths = static_cast<double *>(malloc(num_bins * sizeof(double)));
      cleanup_and_set_error_if(bin_widths == NULL, ERROR_MALLOC);

      for (temp = 0; temp < num_bins; ++temp)
        {
          bin_widths[temp] = bin_width;
        }
      grm_args_push(*series, "bin_widths", "nD", num_bins, bin_widths);
    }


  /* is_bin_counts */
  if (is_bin_counts == 1)
    {
      double temp_max_bc = 0.0;
      int i;
      int total = 0;
      int j;
      int prev = 0;

      if (num_bin_edges > 0 && num_bins != num_bin_edges - 1)
        {
          logger((stderr, "Number of bin_edges must be number of bin_counts + 1\n"));
          cleanup_and_set_error(ERROR_PLOT_INCOMPATIBLE_ARGUMENTS);
        }

      /* total number of observations */
      for (i = 0; i < num_bins; ++i)
        {
          total += bin_counts[i];
        }
      for (i = 0; i < num_bins; ++i)
        {
          if (num_bin_edges > 0) bin_width = bin_widths[i];

          if (strcmp(norm, "pdf") == 0)
            {
              if (bin_counts[i] * 1.0 / (total * bin_width) > temp_max_bc)
                {
                  temp_max_bc = bin_counts[i] * 1.0 / (total * bin_width);
                }
            }
          else if (strcmp(norm, "countdensity") == 0)
            {
              if (bin_counts[i] * 1.0 / (bin_width) > temp_max_bc)
                {
                  temp_max_bc = bin_counts[i] * 1.0 / (bin_width);
                }
            }
          else
            {
              if (bin_counts[i] > temp_max_bc) temp_max_bc = bin_counts[i];
            }
        }

      /* double classes[num_bins][total]; */
      classes = static_cast<double *>(malloc((num_bins * total) * sizeof(double)));
      cleanup_and_set_error_if(classes == NULL, ERROR_MALLOC);

      length = (int)temp_max_bc;
      p = classes;

      for (i = 0; i < (num_bins * total); ++i)
        {
          *p = -1.0;
          p++;
        }

      /* fill classes with bin counts */
      for (i = 0; i < num_bins; ++i)
        {
          p = classes + i * total;
          if (str_equals_any(norm, 2, "cdf", "cumcount"))
            {
              prev += bin_counts[i];
              for (j = 0; j < prev; ++j)
                {
                  *p = 1.0;
                  ++p;
                }
            }

          else
            {
              for (j = 0; j < bin_counts[i]; ++j)
                {
                  *p = 1.0;
                  ++p;
                }
            }
        }

      grm_args_push(*series, "classes", "nD", total * num_bins, classes);
      if (strcmp(norm, "probability") == 0)
        max = temp_max_bc * 1.0 / total;
      else if (strcmp(norm, "cdf") == 0)
        max = 1.0;
      else if (strcmp(norm, "cumcount") == 0)
        max = total * 1.0;
      else
        max = temp_max_bc;
    }

  /* no is_bin_counts */
  else
    {
      int x;

      /* no bin_edges */
      if (num_bin_edges == 0)
        {
          max = 0.0;
          start = 0;
          interval = 2 * M_PI / num_bins;
          if (str_equals_any(norm, 4, "count", "pdf", "countdensity", "probability"))
            {
              /*double classes[num_bins][length];*/
              classes = static_cast<double *>(malloc(num_bins * length * sizeof(double)));
              cleanup_and_set_error_if(classes == NULL, ERROR_MALLOC);
              p = classes;
              int i;
              for (i = 0; i < (num_bins * length); ++i)
                {
                  *p = -1.0;
                  p++;
                }
              for (x = 0; x < num_bins; ++x)
                {
                  int y;
                  p = classes + x * length;
                  temp_max = 0.0;
                  for (y = 0; y < length; ++y)
                    {
                      if (start <= theta[y] && theta[y] < (start + interval))
                        {
                          temp_max++;
                          *p = theta[y];
                          p++;
                          if (max < temp_max) max = temp_max;
                        }
                    }
                  start += interval;
                }
              grm_args_push(*series, "classes", "nD", num_bins * length, classes);
            }
          else if (str_equals_any(norm, 2, "cdf", "cumcount"))
            {
              /*double classes[num_bins][length];*/
              classes = static_cast<double *>(malloc(num_bins * length * sizeof(double)));
              cleanup_and_set_error_if(classes == NULL, ERROR_MALLOC);
              int i;
              int prev;
              p = classes;

              for (i = 0; i < (num_bins * length); ++i)
                {
                  *p = -1;
                  p++;
                }

              prev = 0;
              for (x = 0; x < num_bins; ++x)
                {
                  p = classes + x * length;

                  if (x > 0)
                    {
                      double *prev_list;
                      int y;
                      /*prev_list = classes[x - 1];*/
                      prev_list = classes + (x - 1) * length;
                      for (y = 0; y < prev; ++y)
                        {
                          *p = *prev_list;
                          p++;
                          prev_list++;
                        }
                    }
                  for (i = 0; i < length; ++i)
                    {
                      if (start <= theta[i] && theta[i] < (start + interval))
                        {
                          *p = theta[i];
                          p++;
                          prev++;
                        }
                    }
                  start += interval;
                }
              max = length;
              grm_args_push(*series, "classes", "nD", num_bins * length, classes);
            }

          if (str_equals_any(norm, 2, "probability", "cdf"))
            {
              max = max / length;
            }
          else if (strcmp(norm, "pdf") == 0)
            {
              max = max / (length * bin_width);
            }
        }
      /* bin_edges */
      else
        {
          /* filter theta list */
          int filter;
          int temp = 0;
          double bin_min = *bin_edges;
          double bin_max = bin_edges[num_bin_edges - 1];
          new_theta = static_cast<double *>(malloc(length * sizeof(double)));
          cleanup_and_set_error_if(new_theta == NULL, ERROR_MALLOC);
          for (filter = 0; filter < length; ++filter)
            {
              if (theta[filter] >= bin_min && theta[filter] < bin_max)
                {
                  new_theta[temp] = theta[filter];
                  temp++;
                }
            }
          theta = new_theta;
          length = temp;

          max = 0.0;
          if (str_equals_any(norm, 4, "count", "pdf", "countdensity", "probability"))
            {
              int a;
              /* double classes[num_bins][length]; */
              classes = static_cast<double *>(malloc(num_bins * length * sizeof(double)));
              cleanup_and_set_error_if(classes == NULL, ERROR_MALLOC);
              p = classes;
              for (a = 0; a < (num_bins * length); ++a)
                {
                  *p = -1;
                  p++;
                }
              for (x = 0; x < num_bins; ++x)
                {
                  int b;
                  p = classes + x * length;
                  temp_max = 0.0;
                  for (b = 0; b < length; ++b)
                    {
                      if (x == num_bin_edges - 1) break;
                      if (*bin_edges <= theta[b] && theta[b] < *(bin_edges + 1))
                        {
                          temp_max++;
                          *p = theta[b];
                          p++;
                        }
                    }
                  if (strcmp(norm, "pdf") == 0)
                    {
                      temp_max /= length * *(bin_widths + x);
                    }
                  else if (strcmp(norm, "countdensity") == 0)
                    {
                      temp_max /= *(bin_widths + x);
                    }

                  if (max < temp_max) max = temp_max;

                  bin_edges++;
                }

              grm_args_push(*series, "classes", "nD", num_bins * length, classes);
            }
          else if (str_equals_any(norm, 2, "cdf", "cumcount"))
            {
              int c;
              int prev;
              int d;
              classes = static_cast<double *>(malloc(num_bins * length * sizeof(double)));
              cleanup_and_set_error_if(classes == NULL, ERROR_MALLOC);
              p = classes;
              for (c = 0; c < (num_bins * length); ++c)
                {
                  *p = -1;
                  p++;
                }

              prev = 0;
              for (d = 0; d < num_bins; ++d)
                {
                  int i;
                  p = classes + d * num_bins;

                  if (d > 0)
                    {
                      double *prev_list;
                      int y;
                      prev_list = classes + (d - 1) * num_bins;

                      for (y = 0; y < prev; ++y)
                        {
                          *p = *prev_list;
                          p++;
                          prev_list++;
                        }
                    }
                  for (i = 0; i < length; ++i)
                    {
                      if (*bin_edges <= theta[i] && theta[i] < *(bin_edges + 1))
                        {
                          *p = theta[i];
                          p++;
                          prev++;
                        }
                    }
                  bin_edges++;
                }
              max = length;
              grm_args_push(*series, "classes", "nD", num_bins * length, classes);
            }

          if (str_equals_any(norm, 2, "probability", "cdf"))
            {
              max = max / length;
            }
        }
    }

  if (r_max != NULL)
    {
      *r_max = max;
    }

cleanup:
  free(bin_edges_buf);
  free(bin_widths);
  free(classes);
  free(new_theta);
  free(new_edges);

  return error;
}

/*!
 * Convert an RGB triple to a luminance value following the CCIR 601 format.
 *
 * \param[in] r The red component of the RGB triple in the range [0.0, 1.0].
 * \param[in] g The green component of the RGB triple in the range [0.0, 1.0].
 * \param[in] b The blue component of the RGB triple in the range [0.0, 1.0].
 * \return The luminance of the given RGB triple in the range [0.0, 1.0].
 */
double get_lightness_from_rbg(double r, double g, double b)
{
  return 0.299 * r + 0.587 * g + 0.114 * b;
}

/*!
 * Set the text color either to black or white depending on the given background color.
 *
 * \param[in] r The red component of the RGB triple in the range [0.0, 1.0].
 * \param[in] g The green component of the RGB triple in the range [0.0, 1.0].
 * \param[in] b The blue component of the RGB triple in the range [0.0, 1.0].
 * \return The luminance of the given RGB triple.
 */
void set_text_color_for_background(double r, double g, double b)
{
  double color_lightness;

  color_lightness = get_lightness_from_rbg(r, g, b);
  if (color_lightness < 0.4)
    {
      gr_settextcolorind(0);
    }
  else
    {
      gr_settextcolorind(1);
    }
}


/*!
 * Draw an xticklabel at the specified position while trying to stay in the available space.
 * Every space character (' ') is seen as a possible position to break the label into the next line.
 * The label will not break into the next line when enough space is available.
 * If a label or a part of it does not fit in the available space but doesnt have a space character to break it up
 * it will get drawn anyway.
 *
 * \param[in] x1 The X coordinate of starting position of the label.
 * \param[in] x2 The Y coordinate of starting position of the label.
 * \param[in] label The label to be drawn.
 * \param[in] available_width The available space in X direction around the starting position.
 */
void draw_xticklabel(double x1, double x2, const char *label, double available_width)
{
  char new_label[256];
  int breakpoint_positions[128];
  int cur_num_breakpoints = 0;
  int i = 0;
  int cur_start = 0;
  double tbx[4], tby[4];
  double width;
  double charheight;

  gr_inqcharheight(&charheight);


  for (i = 0; i == 0 || label[i - 1] != '\0'; ++i)
    {
      if (label[i] == ' ' || label[i] == '\0')
        {
          /* calculate width of the next part of the label to be drawn */
          new_label[i] = '\0';
          gr_inqtext(x1, x2, new_label + cur_start, tbx, tby);
          gr_wctondc(&tbx[0], &tby[0]);
          gr_wctondc(&tbx[2], &tby[2]);
          width = tbx[2] - tbx[0];
          new_label[i] = ' ';

          /* add possible breakpoint */
          breakpoint_positions[cur_num_breakpoints++] = i;

          if (width > available_width)
            {
              /* part is too big but doesnt have a breakpoint in it */
              if (cur_num_breakpoints == 1)
                {
                  new_label[i] = '\0';
                  gr_text(x1, x2, new_label + cur_start);

                  cur_start = i + 1;
                  cur_num_breakpoints = 0;
                }
              /* part is too big and has breakpoints in it */
              else
                {
                  /* break label at last breakpoint that still allowed the text to fit */
                  new_label[breakpoint_positions[cur_num_breakpoints - 2]] = '\0';
                  gr_text(x1, x2, new_label + cur_start);

                  cur_start = breakpoint_positions[cur_num_breakpoints - 2] + 1;
                  breakpoint_positions[0] = breakpoint_positions[cur_num_breakpoints - 1];
                  cur_num_breakpoints = 1;
                }
              x2 -= charheight * 1.5;
            }
        }
      else
        {
          new_label[i] = label[i];
        }
    }

  /* 0-terminate the new label */
  new_label[i] = '\0';

  /* draw the rest */
  gr_text(x1, x2, new_label + cur_start);
}

/*!
 * \brief Set colors from color index or rgb arrays.
 *
 * Call the function first with an argument container and a key. Afterwards, call the `set_next_color` with `NULL`
 * pointers to iterate through the color arrays. If `key` does not exist in `args`, the function falls back to default
 * colors.
 *
 * \param args The argument container which stores the color values.
 * \param key The key of the colors in the argument container. The key may reference integer or double arrays.
 *            Integer arrays describe colors of the GKS color table (0 - 1255). Double arrays contain RGB tuples in the
 *            range [0.0, 1.0]. If key does not exist, the routine falls back to default colors (taken from
 *            `gr_uselinespec`).
 * \param color_type The color type to set. Can be one of `GR_COLOR_LINE`, `GR_COLOR_MARKER`, `GR_COLOR_FILL`,
 *                   `GR_COLOR_TEXT`, `GR_COLOR_BORDER` or any combination of them (combined with OR). The special value
 *                   `GR_COLOR_RESET` resets all color modifications.
 */
void set_next_color(const grm_args_t *args, const char *key, gr_color_type_t color_type)
{
  const static int fallback_color_indices[] = {989, 982, 980, 981, 996, 983, 995, 988, 986, 990,
                                               991, 984, 992, 993, 994, 987, 985, 997, 998, 999};
  static double saved_color[3];
  static int last_array_index = -1;
  static const int *color_indices = NULL;
  static const double *color_rgb_values = NULL;
  static unsigned int color_array_length = -1;
  int current_array_index = last_array_index + 1;
  int color_index = 0;
  int reset = (color_type == GR_COLOR_RESET);
  int gks_errind = GKS_K_NO_ERROR;

  if (reset || (args != NULL && key != NULL))
    {
      if (last_array_index >= 0 && color_rgb_values != NULL)
        {
          gr_setcolorrep(PLOT_CUSTOM_COLOR_INDEX, saved_color[0], saved_color[1], saved_color[2]);
        }
      last_array_index = -1;
      if (!reset && args != NULL && key != NULL)
        {
          if (!grm_args_first_value(args, key, "I", &color_indices, &color_array_length) &&
              !grm_args_first_value(args, key, "D", &color_rgb_values, &color_array_length))
            {
              /* use fallback colors if `key` cannot be read from `args` */
              logger((stderr, "Cannot read \"%s\" from args, falling back to default colors\n", key));
              color_indices = fallback_color_indices;
              color_array_length = array_size(fallback_color_indices);
            }
        }
      else
        {
          color_indices = NULL;
          color_rgb_values = NULL;
          color_array_length = -1;
        }

      if (reset)
        {
          return;
        }
    }

  if (last_array_index < 0 && color_rgb_values != NULL)
    {
      gks_inq_color_rep(1, PLOT_CUSTOM_COLOR_INDEX, GKS_K_VALUE_SET, &gks_errind, &saved_color[0], &saved_color[1],
                        &saved_color[2]);
    }

  current_array_index %= color_array_length;

  if (color_indices != NULL)
    {
      color_index = color_indices[current_array_index];
      last_array_index = current_array_index;
    }
  else if (color_rgb_values != NULL)
    {
      gr_setcolorrep(PLOT_CUSTOM_COLOR_INDEX, color_rgb_values[current_array_index],
                     color_rgb_values[current_array_index + 1], color_rgb_values[current_array_index + 2]);
      color_index = PLOT_CUSTOM_COLOR_INDEX;
      last_array_index = current_array_index + 2;
    }

  if (color_type & GR_COLOR_LINE)
    {
      gr_setlinecolorind(color_index);
    }
  if (color_type & GR_COLOR_MARKER)
    {
      gr_setmarkercolorind(color_index);
    }
  if (color_type & GR_COLOR_FILL)
    {
      gr_setfillcolorind(color_index);
    }
  if (color_type & GR_COLOR_TEXT)
    {
      gr_settextcolorind(color_index);
    }
  if (color_type & GR_COLOR_BORDER)
    {
      gr_setbordercolorind(color_index);
    }
}


/* ========================= methods ================================================================================ */

/* ------------------------- args set ------------------------------------------------------------------------------- */

DEFINE_SET_METHODS(args)

int args_set_entry_copy(args_set_entry_t *copy, args_set_const_entry_t entry)
{
  /* discard const because it is necessary to work on the object itself */
  /* TODO create two set types: copy and pointer version */
  *copy = (args_set_entry_t)entry;
  return 1;
}

void args_set_entry_delete(args_set_entry_t entry UNUSED) {}

size_t args_set_entry_hash(args_set_const_entry_t entry)
{
  return (size_t)entry;
}

int args_set_entry_equals(args_set_const_entry_t entry1, args_set_const_entry_t entry2)
{
  return entry1 == entry2;
}


/* ------------------------- string-to-plot_func map ---------------------------------------------------------------- */

DEFINE_MAP_METHODS(plot_func)

int plot_func_map_value_copy(plot_func_t *copy, const plot_func_t value)
{
  *copy = value;

  return 1;
}

void plot_func_map_value_delete(plot_func_t value UNUSED) {}


/* ------------------------- string-to-args_set map ----------------------------------------------------------------- */

DEFINE_MAP_METHODS(args_set)

int args_set_map_value_copy(args_set_t **copy, const args_set_t *value)
{
  /* discard const because it is necessary to work on the object itself */
  /* TODO create two map types: copy and pointer version */
  *copy = (args_set_t *)value;

  return 1;
}

void args_set_map_value_delete(args_set_t *value UNUSED) {}


#undef DEFINE_SET_METHODS
#undef DEFINE_MAP_METHODS


/* ######################### public implementation ################################################################## */

/* ========================= functions ============================================================================== */

/* ------------------------- plot ----------------------------------------------------------------------------------- */

void grm_finalize(void)
{
  if (plot_static_variables_initialized)
    {
      grm_args_delete(global_root_args);
      global_root_args = NULL;
      active_plot_args = NULL;
      active_plot_index = 0;
      event_queue_delete(event_queue);
      event_queue = NULL;
      double_map_delete(meters_per_unit_map);
      meters_per_unit_map = NULL;
      string_map_delete(fmt_map);
      fmt_map = NULL;
      plot_func_map_delete(plot_func_map);
      plot_func_map = NULL;
      string_map_delete(plot_valid_keys_map);
      plot_valid_keys_map = NULL;
      string_array_map_delete(type_map);
      type_map = NULL;
      plot_static_variables_initialized = 0;
      grid_delete(global_grid);
      global_grid = NULL;
    }
}

int grm_clear(void)
{
  if (plot_init_static_variables() != ERROR_NONE)
    {
      return 0;
    }
  grm_args_clear(active_plot_args);
  if (plot_init_args_structure(active_plot_args, plot_hierarchy_names + 1, 1) != ERROR_NONE)
    {
      return 0;
    }

  return 1;
}

void grm_dump_graphics_tree(FILE *f)
{
  const unsigned int indent = 2;
  fprintf(f, "%s\n", toXML(global_root, GR::SerializerOptions{std::string(indent, ' ')}).c_str());
}

unsigned int grm_max_plotid(void)
{
  unsigned int args_array_length = 0;

  if (grm_args_first_value(global_root_args, "plots", "A", NULL, &args_array_length))
    {
      --args_array_length;
    }

  return args_array_length;
}

char *grm_dump_graphics_tree_str(void)
{
  std::string graphics_tree_str = toXML(global_root);
  char *graphics_tree_cstr = new char[graphics_tree_str.length() + 1];
  strcpy(graphics_tree_cstr, graphics_tree_str.c_str());
  return graphics_tree_cstr;
}

#ifndef NO_EXPAT
static void xml_parse_start_handler(void *data, const XML_Char *tagName, const XML_Char **attr)
{
  std::shared_ptr<GR::Element> *insertionParent = (std::shared_ptr<GR::Element> *)data;
  if (strcmp(tagName, "root") == 0)
    {
      global_root = global_render->createElement("root");
      global_render->replaceChildren(global_root);
      if (attr[0])
        {
          global_root->setAttribute(attr[0], attr[1]);
        }
      (*insertionParent) = global_root;
    }
  else
    {
      std::shared_ptr<GR::Element> child = global_render->createElement(tagName);
      for (int i = 0; attr[i]; i += 2)
        {
          child->setAttribute(attr[i], attr[i + 1]);
        }

      (*insertionParent)->appendChild(child);
      *insertionParent = child;
    }
}

static void xml_parse_end_handler(void *data, const char *tagName)
{
  auto currentNode = (std::shared_ptr<GR::Element> *)data;
  *((std::shared_ptr<GR::Element> *)data) = (*currentNode)->parentElement();
}

void grm_load_graphics_tree(FILE *file)
{
  std::string xmlstring;
  XML_Parser parser = XML_ParserCreate(NULL);
  std::shared_ptr<GR::Element> parentNode;

  std::fseek(file, 0, SEEK_END);
  xmlstring.resize(std::ftell(file));
  std::rewind(file);
  std::fread(&xmlstring[0], 1, xmlstring.size(), file);

  plot_init_static_variables();

  XML_SetUserData(parser, &parentNode);
  XML_SetElementHandler(parser, xml_parse_start_handler, xml_parse_end_handler);

  if (XML_Parse(parser, xmlstring.c_str(), xmlstring.length(), XML_TRUE) == XML_STATUS_ERROR)
    {
      logger((stderr, "Cannot parse XML-String\n"));
      return;
    }

  XML_ParserFree(parser);
}
#endif

int grm_merge(const grm_args_t *args)
{
  return grm_merge_extended(args, 0, NULL);
}

int grm_merge_extended(const grm_args_t *args, int hold, const char *identificator)
{
  err_t error = ERROR_NONE;

  if (plot_init_static_variables() != ERROR_NONE)
    {
      return 0;
    }
  if (args != NULL)
    {
      if (plot_check_for_request(args, &error))
        {
          // If this is a request, do not process the argument container further
          process_events();
          return error == ERROR_NONE;
        }
      if (plot_merge_args(global_root_args, args, NULL, NULL, hold) != ERROR_NONE)
        {
          return 0;
        }
    }

  process_events();
  event_queue_enqueue_merge_end_event(event_queue, identificator);
  process_events();

  return 1;
}

int grm_merge_hold(const grm_args_t *args)
{
  return grm_merge_extended(args, 1, NULL);
}

int grm_merge_named(const grm_args_t *args, const char *identificator)
{
  return grm_merge_extended(args, 0, identificator);
}

int plot_process_subplot_args(grm_args_t *subplot_args)
{
  plot_func_t plot_func;
  char *ylabel, *xlabel, *title, *kind;
  int keep_aspect_ratio, location;
  double *subplot;

  if (plot_pre_subplot(subplot_args) != ERROR_NONE)
    {
      return 0;
    }

  grm_args_values(subplot_args, "kind", "s", &kind);
  std::shared_ptr<GR::Element> group = (currentDomElement) ? currentDomElement : global_root->lastChildElement();
  group->setAttribute("kind", kind);
  logger((stderr, "Got keyword \"kind\" with value \"%s\"\n", kind));

  if (grm_args_values(subplot_args, "ylabel", "s", &ylabel))
    {
      //      group->setAttribute("ylabel", ylabel);
      group->setAttribute("ylabel_margin", 1);
    }
  if (grm_args_values(subplot_args, "xlabel", "s", &xlabel))
    {
      //      group->setAttribute("xlabel", xlabel);
      group->setAttribute("xlabel_margin", 1);
    }
  if (grm_args_values(subplot_args, "title", "s", &title))
    {
      //      group->setAttribute("title", title);
      group->setAttribute("title_margin", 1);
    }
  if (grm_args_values(subplot_args, "keep_aspec_ratio", "i", &keep_aspect_ratio))
    {
      group->setAttribute("keep_aspect_ratio", keep_aspect_ratio);
    }
  if (grm_args_values(subplot_args, "location", "i", &location))
    {
      group->setAttribute("location", location);
    }
  if (grm_args_values(subplot_args, "subplot", "D", &subplot))
    {
      group->setAttribute("subplot", true);
      group->setAttribute("subplot_xmin", subplot[0]);
      group->setAttribute("subplot_xmax", subplot[1]);
      group->setAttribute("subplot_ymin", subplot[2]);
      group->setAttribute("subplot_ymax", subplot[3]);
    }

  if (!plot_func_map_at(plot_func_map, kind, &plot_func))
    {
      return 0;
    }
  if (plot_func(subplot_args) != ERROR_NONE)
    {
      return 0;
    };
  plot_post_subplot(subplot_args);
  return 1;
}

int grm_plot(const grm_args_t *args)
{
  grm_args_t **current_subplot_args;
  grm::Grid *currentGrid;
  plot_func_t plot_func;
  const char *kind = NULL;
  int figsize_x, figsize_y, tmp_size_i[2];
  double tmp_size_d[2];
  grm_args_ptr_t tmp_size_a[2];
  const char *tmp_size_s[2];
  std::string vars[2] = {"x", "y"};
  double default_size[2] = {PLOT_DEFAULT_WIDTH, PLOT_DEFAULT_HEIGHT};
  if (!grm_merge(args))
    {
      return 0;
    }

  global_root = global_render->createElement("root");
  global_render->replaceChildren(global_root);
  global_root->setAttribute("id", 0);

  if (grm_args_values(active_plot_args, "raw", "s", &current_subplot_args))
    {
      plot_raw(active_plot_args);
      global_render->render();
    }
  else
    {
      plot_set_attribute_defaults(active_plot_args);

      if (grm_args_values(active_plot_args, "size", "dd", &tmp_size_d[0], &tmp_size_d[1]))
        {
          global_root->setAttribute("size", true);
          for (int i = 0; i < 2; ++i)
            {
              global_root->setAttribute("size_" + vars[i], tmp_size_d[i]);
              global_root->setAttribute("size_" + vars[i] + "_type", "double");
              global_root->setAttribute("size_" + vars[i] + "_unit", "m");
            }
        }
      if (grm_args_values(active_plot_args, "size", "ii", &tmp_size_i[0], &tmp_size_i[1]))
        {
          global_root->setAttribute("size", true);
          for (int i = 0; i < 2; ++i)
            {
              global_root->setAttribute("size_" + vars[i], tmp_size_i[i]);
              global_root->setAttribute("size_" + vars[i] + "_type", "int");
              global_root->setAttribute("size_" + vars[i] + "_unit", "m");
            }
        }
      if (grm_args_values(active_plot_args, "size", "aa", &tmp_size_a[0], &tmp_size_a[1]))
        {
          global_root->setAttribute("size", true);
          for (int i = 0; i < 2; ++i)
            {
              if (grm_args_values(tmp_size_a[i], "unit", "s", &tmp_size_s[i]))
                {
                  global_root->setAttribute("size_" + vars[i] + "_unit", tmp_size_s[i]);
                }
              if (grm_args_values(tmp_size_a[i], "value", "i", &tmp_size_i[i]))
                {
                  global_root->setAttribute("size_" + vars[i] + "_type", "int");
                  global_root->setAttribute("size_" + vars[i], tmp_size_i[i]);
                }
              else if (grm_args_values(tmp_size_a[i], "value", "d", &tmp_size_d[i]))
                {
                  global_root->setAttribute("size_" + vars[i] + "_type", "double");
                  global_root->setAttribute("size_" + vars[i], tmp_size_d[i]);
                }
              else
                {
                  /* If no value is given, fall back to default value */
                  for (int i = 0; i < 2; ++i)
                    {
                      global_root->setAttribute("size_" + vars[i], default_size[i]);
                      global_root->setAttribute("size_" + vars[i] + "_type", "double");
                      global_root->setAttribute("size_" + vars[i] + "_unit", "m");
                    }
                  return 0;
                }
            }
        }
      if (grm_args_values(active_plot_args, "figsize", "dd", &figsize_x, &figsize_y))
        {
          global_root->setAttribute("figsize", true);
          global_root->setAttribute("figsize_x", figsize_x);
          global_root->setAttribute("figsize_y", figsize_y);
        }
      if (plot_process_grid_arguments(active_plot_args) != ERROR_NONE)
        {
          return 0;
        }

      currentGrid = reinterpret_cast<grm::Grid *>(global_grid);
      int nrows = currentGrid->getNRows();
      int ncols = currentGrid->getNCols();

      plot_pre_plot(active_plot_args);
      grm_args_values(active_plot_args, "subplots", "A", &current_subplot_args);

      if (!(nrows == 1 && ncols == 1 &&
            currentGrid->getElement(0, 0) == nullptr)) // Check if Grid arguments in container
        {
          auto gridDomElement = global_render->createLayoutGrid(*currentGrid);
          global_root->append(gridDomElement);

          for (auto const &elementToSlice : currentGrid->getElementToPosition())
            {
              grm_plot_helper(elementToSlice.first, elementToSlice.second, gridDomElement);
            }
        }
      else
        {
          std::cout << "no grid elements\n";
          while (*current_subplot_args != NULL)
            {
              /* TODO: is the group name important? */
              auto group = global_render->createGroup("");
              global_root->append(group);
              if (!plot_process_subplot_args(*current_subplot_args))
                {
                  return 0;
                }
              ++current_subplot_args;
            }
        }

      global_render->render();
      plot_post_plot(active_plot_args);
    }

  process_events();

#ifndef NDEBUG
  logger((stderr, "root args after \"grm_plot\" (active_plot_index: %d):\n", active_plot_index - 1));
  if (logger_enabled())
    {
      grm_dump(global_root_args, stderr);
      grm_dump_graphics_tree(stderr);
    }
#endif

  return 1;
}

void grm_render(void)
{
  global_render->render();
}

int grm_switch(unsigned int id)
{
  grm_args_t **args_array = NULL;
  unsigned int args_array_length = 0;

  if (plot_init_static_variables() != ERROR_NONE)
    {
      return 0;
    }

  if (plot_init_args_structure(global_root_args, plot_hierarchy_names, id + 1) != ERROR_NONE)
    {
      return 0;
    }
  if (!grm_args_first_value(global_root_args, "plots", "A", &args_array, &args_array_length))
    {
      return 0;
    }
  if (id + 1 > args_array_length)
    {
      return 0;
    }

  active_plot_index = id + 1;
  active_plot_args = args_array[id];

  return 1;
}
}

int grm_plot_helper(grm::GridElement *gridElement, grm::Slice *slice,
                    const std::shared_ptr<GR::Element> &parentDomElement)
{
  plot_func_t plot_func;
  const char *kind = NULL;

  if (gridElement == NULL)
    {
      std::cout << "Error: gridElement is NULL\n";
      return 0;
    }

  if (!gridElement->isGrid())
    {
      grm_args_t **current_subplot_args = &gridElement->subplot_args;
      auto subplotGroup = global_render->createLayoutGridElement(*gridElement, *slice);
      currentDomElement = subplotGroup;
      parentDomElement->append(subplotGroup);

      if (!plot_process_subplot_args(*current_subplot_args))
        {
          return 0;
        }
    }
  else
    {
      grm::Grid *currentGrid = reinterpret_cast<grm::Grid *>(gridElement);

      auto gridDomElement = global_render->createLayoutGrid(*currentGrid);
      gridDomElement->setAttribute("rowStart", slice->rowStart);
      gridDomElement->setAttribute("rowStop", slice->rowStop);
      gridDomElement->setAttribute("colStart", slice->colStart);
      gridDomElement->setAttribute("colStop", slice->colStop);
      parentDomElement->append(gridDomElement);

      for (auto const &elementToSlice : currentGrid->getElementToPosition())
        {
          grm_plot_helper(elementToSlice.first, elementToSlice.second, gridDomElement);
        }
    }
}

std::shared_ptr<GR::Element> grm_get_document_root(void)
{
  return global_root;
}

std::shared_ptr<GR::Render> grm_get_render(void)
{
  return global_render;
}


/*!
 * Set the text color either to black or white depending on the given background color.
 *
 * \param[in] r The red component of the RGB triple in the range [0.0, 1.0].
 * \param[in] g The green component of the RGB triple in the range [0.0, 1.0].
 * \param[in] b The blue component of the RGB triple in the range [0.0, 1.0].
 * \return The luminance of the given RGB triple.
 */
void set_text_color_for_background(double r, double g, double b, const std::shared_ptr<GR::Element> &element)
{
  double color_lightness;

  color_lightness = get_lightness_from_rbg(r, g, b);
  if (color_lightness < 0.4)
    {
      global_render->setTextColorInd(element, 0);
    }
  else
    {
      global_render->setTextColorInd(element, 1);
    }
}


/*!
 * \brief Set colors from color index or rgb arrays. The render version
 *
 * Call the function first with an argument container and a key. Afterwards, call the `set_next_color` with `NULL`
 * pointers to iterate through the color arrays. If `key` does not exist in `args`, the function falls back to default
 * colors.
 *
 * \param args The argument container which stores the color values.
 * \param key The key of the colors in the argument container. The key may reference integer or double arrays.
 *            Integer arrays describe colors of the GKS color table (0 - 1255). Double arrays contain RGB tuples in the
 *            range [0.0, 1.0]. If key does not exist, the routine falls back to default colors (taken from
 *            `gr_uselinespec`).
 * \param color_type The color type to set. Can be one of `GR_COLOR_LINE`, `GR_COLOR_MARKER`, `GR_COLOR_FILL`,
 *                   `GR_COLOR_TEXT`, `GR_COLOR_BORDER` or any combination of them (combined with OR). The special value
 *                   `GR_COLOR_RESET` resets all color modifications.
 */
int set_next_color(const grm_args_t *args, const char *key, gr_color_type_t color_type,
                   const std::shared_ptr<GR::Element> &element)
{
  const static int fallback_color_indices[] = {989, 982, 980, 981, 996, 983, 995, 988, 986, 990,
                                               991, 984, 992, 993, 994, 987, 985, 997, 998, 999};
  static double saved_color[3];
  static int last_array_index = -1;
  static const int *color_indices = NULL;
  static const double *color_rgb_values = NULL;
  static unsigned int color_array_length = -1;
  int current_array_index = last_array_index + 1;
  int color_index = 0;
  int reset = (color_type == GR_COLOR_RESET);
  int gks_errind = GKS_K_NO_ERROR;

  if (reset || (args != NULL && key != NULL))
    {
      if (last_array_index >= 0 && color_rgb_values != NULL)
        {
          gr_setcolorrep(PLOT_CUSTOM_COLOR_INDEX, saved_color[0], saved_color[1], saved_color[2]);
        }
      last_array_index = -1;
      if (!reset && args != NULL && key != NULL)
        {
          if (!grm_args_first_value(args, key, "I", &color_indices, &color_array_length) &&
              !grm_args_first_value(args, key, "D", &color_rgb_values, &color_array_length))
            {
              /* use fallback colors if `key` cannot be read from `args` */
              logger((stderr, "Cannot read \"%s\" from args, falling back to default colors\n", key));
              color_indices = fallback_color_indices;
              color_array_length = array_size(fallback_color_indices);
            }
        }
      else
        {
          color_indices = NULL;
          color_rgb_values = NULL;
          color_array_length = -1;
        }

      if (reset)
        {
          return 0;
        }
    }

  if (last_array_index < 0 && color_rgb_values != NULL)
    {
      gks_inq_color_rep(1, PLOT_CUSTOM_COLOR_INDEX, GKS_K_VALUE_SET, &gks_errind, &saved_color[0], &saved_color[1],
                        &saved_color[2]);
    }

  current_array_index %= color_array_length;

  if (color_indices != NULL)
    {
      color_index = color_indices[current_array_index];
      last_array_index = current_array_index;
    }
  else if (color_rgb_values != NULL)
    {
      color_index = PLOT_CUSTOM_COLOR_INDEX;
      last_array_index = current_array_index + 2;
      global_render->setColorRep(element, PLOT_CUSTOM_COLOR_INDEX, color_rgb_values[current_array_index],
                                 color_rgb_values[current_array_index + 1], color_rgb_values[current_array_index + 2]);
    }

  if (color_type & GR_COLOR_LINE)
    {
      global_render->setLineColorInd(element, color_index);
    }
  if (color_type & GR_COLOR_MARKER)
    {
      global_render->setMarkerColorInd(element, color_index);
    }
  if (color_type & GR_COLOR_FILL)
    {
      global_render->setFillColorInd(element, color_index);
    }
  if (color_type & GR_COLOR_TEXT)
    {
      global_render->setTextColorInd(element, color_index);
    }
  if (color_type & GR_COLOR_BORDER)
    {
      global_render->setBorderColorInd(element, color_index);
    }
  return color_index;
}
