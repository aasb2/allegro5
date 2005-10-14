/*         ______   ___    ___ 
 *        /\  _  \ /\_ \  /\_ \ 
 *        \ \ \L\ \\//\ \ \//\ \      __     __   _ __   ___ 
 *         \ \  __ \ \ \ \  \ \ \   /'__`\ /'_ `\/\`'__\/ __`\
 *          \ \ \/\ \ \_\ \_ \_\ \_/\  __//\ \L\ \ \ \//\ \L\ \
 *           \ \_\ \_\/\____\/\____\ \____\ \____ \ \_\\ \____/
 *            \/_/\/_/\/____/\/____/\/____/\/___L\ \/_/ \/___/
 *                                           /\____/
 *                                           \_/__/
 *
 *      Graphics mode set and bitmap creation routines.
 *
 *      By Shawn Hargreaves.
 *
 *      See readme.txt for copyright information.
 */


#include <string.h>

#include "allegro.h"
#include "allegro/internal/aintern.h"



int _sub_bitmap_id_count = 1;          /* hash value for sub-bitmaps */

int _gfx_mode_set_count = 0;           /* has the graphics mode changed? */

int _screen_split_position = 0;        /* has the screen been split? */

int _safe_gfx_mode_change = 0;         /* are we getting through GFX_SAFE? */

RGB_MAP *rgb_map = NULL;               /* RGB -> palette entry conversion */

COLOR_MAP *color_map = NULL;           /* translucency/lighting table */

int _color_depth = 8;                  /* how many bits per pixel? */

int _refresh_rate_request = 0;         /* requested refresh rate */
static int current_refresh_rate = 0;   /* refresh rate set by the driver */

int _wait_for_vsync = TRUE;            /* vsync when page-flipping? */

int _color_conv = COLORCONV_TOTAL;     /* which formats to auto convert? */

static int color_conv_set = FALSE;     /* has the user set conversion mode? */

int _palette_color8[256];               /* palette -> pixel mapping */
int _palette_color15[256];
int _palette_color16[256];
int _palette_color24[256];
int _palette_color32[256];

int *palette_color = _palette_color8; 

BLENDER_FUNC _blender_func15 = NULL;   /* truecolor pixel blender routines */
BLENDER_FUNC _blender_func16 = NULL;
BLENDER_FUNC _blender_func24 = NULL;
BLENDER_FUNC _blender_func32 = NULL;

BLENDER_FUNC _blender_func15x = NULL;
BLENDER_FUNC _blender_func16x = NULL;
BLENDER_FUNC _blender_func24x = NULL;

int _blender_col_15 = 0;               /* for truecolor lit sprites */
int _blender_col_16 = 0;
int _blender_col_24 = 0;
int _blender_col_32 = 0;

int _blender_alpha = 0;                /* for truecolor translucent drawing */

int _rgb_r_shift_15 = DEFAULT_RGB_R_SHIFT_15;     /* truecolor pixel format */
int _rgb_g_shift_15 = DEFAULT_RGB_G_SHIFT_15;
int _rgb_b_shift_15 = DEFAULT_RGB_B_SHIFT_15;
int _rgb_r_shift_16 = DEFAULT_RGB_R_SHIFT_16;
int _rgb_g_shift_16 = DEFAULT_RGB_G_SHIFT_16;
int _rgb_b_shift_16 = DEFAULT_RGB_B_SHIFT_16;
int _rgb_r_shift_24 = DEFAULT_RGB_R_SHIFT_24;
int _rgb_g_shift_24 = DEFAULT_RGB_G_SHIFT_24;
int _rgb_b_shift_24 = DEFAULT_RGB_B_SHIFT_24;
int _rgb_r_shift_32 = DEFAULT_RGB_R_SHIFT_32;
int _rgb_g_shift_32 = DEFAULT_RGB_G_SHIFT_32;
int _rgb_b_shift_32 = DEFAULT_RGB_B_SHIFT_32;
int _rgb_a_shift_32 = DEFAULT_RGB_A_SHIFT_32;


/* lookup table for scaling 5 bit colors up to 8 bits */
int _rgb_scale_5[32] =
{
   0,   8,   16,  24,  32,  41,  49,  57,
   65,  74,  82,  90,  98,  106, 115, 123,
   131, 139, 148, 156, 164, 172, 180, 189,
   197, 205, 213, 222, 230, 238, 246, 255
};


/* lookup table for scaling 6 bit colors up to 8 bits */
int _rgb_scale_6[64] =
{
   0,   4,   8,   12,  16,  20,  24,  28,
   32,  36,  40,  44,  48,  52,  56,  60,
   64,  68,  72,  76,  80,  85,  89,  93,
   97,  101, 105, 109, 113, 117, 121, 125,
   129, 133, 137, 141, 145, 149, 153, 157,
   161, 165, 170, 174, 178, 182, 186, 190,
   194, 198, 202, 206, 210, 214, 218, 222,
   226, 230, 234, 238, 242, 246, 250, 255
};


GFX_VTABLE _screen_vtable;             /* accelerated drivers change this */


/* lock_bitmap:
 *  Locks all the memory used by a bitmap structure.
 */
void lock_bitmap(BITMAP *bmp)
{
   LOCK_DATA(bmp, sizeof(BITMAP) + sizeof(char *) * bmp->h);

   if (bmp->dat) {
      LOCK_DATA(bmp->dat, bmp->w * bmp->h * BYTES_PER_PIXEL(bitmap_color_depth(bmp)));
   }
}



/* request_refresh_rate:
 *  Requests that the next call to set_gfx_mode() use the specified refresh
 *  rate.
 */
void request_refresh_rate(int rate)
{
   _refresh_rate_request = rate;
}



/* get_refresh_rate:
 *  Returns the refresh rate set by the most recent call to set_gfx_mode().
 */
int get_refresh_rate(void)
{
   return current_refresh_rate;
}



/* _set_current_refresh_rate:
 *  Sets the current refresh rate.
 *  (This function must be called by the gfx drivers)
 */
void _set_current_refresh_rate(int rate)
{
   /* sanity check to discard bogus values */
   if ((rate<40) || (rate>200))
      rate = 0;

   current_refresh_rate = rate;

   /* adjust retrace speed */
   _vsync_speed = rate ? BPS_TO_TIMER(rate) : BPS_TO_TIMER(70);
}



/* sort_gfx_mode_list:
 *  Callback for quick-sorting a mode-list.
 */
static int sort_gfx_mode_list(GFX_MODE *entry1, GFX_MODE *entry2)
{
   if (entry1->width > entry2->width) {
      return +1;
   }
   else if (entry1->width < entry2->width) {
      return -1;
   }
   else {
      if (entry1->height > entry2->height) {
         return +1;
      }
      else if (entry1->height < entry2->height) {
         return -1;
      }
      else {
         if (entry1->bpp > entry2->bpp) {
            return +1;
         }
         else if (entry1->bpp < entry2->bpp) {
            return -1;
         }
         else {
            return 0;
         }
      }
   }
}



/* get_gfx_mode_list:
 *  Attempts to create a list of all the supported video modes for a certain
 *  GFX driver.
 */
GFX_MODE_LIST *get_gfx_mode_list(int card)
{
   _DRIVER_INFO *list_entry;
   GFX_DRIVER *drv = NULL;
   GFX_MODE_LIST *gfx_mode_list = NULL;

   ASSERT(system_driver);

   /* ask the system driver for a list of graphics hardware drivers */
   if (system_driver->gfx_drivers)
      list_entry = system_driver->gfx_drivers();
   else
      list_entry = _gfx_driver_list;

   /* find the graphics driver, and if it can fetch mode lists, do so */
   while (list_entry->driver) {
      if (list_entry->id == card) {
         drv = list_entry->driver;
         if (!drv->fetch_mode_list)
            return NULL;

         gfx_mode_list = drv->fetch_mode_list();
         if (!gfx_mode_list)
            return NULL;

         break;
      }

      list_entry++;
   }

   if (!drv)
      return NULL;

   /* sort the list and finish */
   qsort(gfx_mode_list->mode, gfx_mode_list->num_modes, sizeof(GFX_MODE),
         (int (*) (AL_CONST void *, AL_CONST void *))sort_gfx_mode_list);

   return gfx_mode_list;
}



/* destroy_gfx_mode_list:
 *  Removes the mode list created by get_gfx_mode_list() from memory.
 */
void destroy_gfx_mode_list(GFX_MODE_LIST *gfx_mode_list)
{
   if (gfx_mode_list) {
      if (gfx_mode_list->mode)
         free(gfx_mode_list->mode);

      free(gfx_mode_list);
   }
}



/* set_color_depth:
 *  Sets the pixel size (in bits) which will be used by subsequent calls to 
 *  set_gfx_mode() and create_bitmap(). Valid depths are 8, 15, 16, 24 and 32.
 */
void set_color_depth(int depth)
{
   _color_depth = depth;

   switch (depth) {
      case 8:  palette_color = _palette_color8;  break;
      case 15: palette_color = _palette_color15; break;
      case 16: palette_color = _palette_color16; break;
      case 24: palette_color = _palette_color24; break;
      case 32: palette_color = _palette_color32; break;
      default: ASSERT(FALSE);
   }
}



/* get_color_depth:
 *  Returns the current color depth.
 */
int get_color_depth(void)
{
   return _color_depth;
}



/* set_color_conversion:
 *  Sets a bit mask specifying which types of color format conversions are
 *  valid when loading data from disk.
 */
void set_color_conversion(int mode)
{
   _color_conv = mode;

   color_conv_set = TRUE;
}



/* get_color_conversion:
 *  Returns the bitmask specifying which types of color format
 *  conversion are valid when loading data from disk.
 */
int get_color_conversion(void)
{
   return _color_conv;
}



/* _color_load_depth:
 *  Works out which color depth an image should be loaded as, given the
 *  current conversion mode.
 */
int _color_load_depth(int depth, int hasalpha)
{
   typedef struct CONVERSION_FLAGS
   {
      int flag;
      int in_depth;
      int out_depth;
      int hasalpha;
   } CONVERSION_FLAGS;

   static CONVERSION_FLAGS conversion_flags[] =
   {
      { COLORCONV_8_TO_15,   8,  15, FALSE },
      { COLORCONV_8_TO_16,   8,  16, FALSE },
      { COLORCONV_8_TO_24,   8,  24, FALSE },
      { COLORCONV_8_TO_32,   8,  32, FALSE },
      { COLORCONV_15_TO_8,   15, 8,  FALSE },
      { COLORCONV_15_TO_16,  15, 16, FALSE },
      { COLORCONV_15_TO_24,  15, 24, FALSE },
      { COLORCONV_15_TO_32,  15, 32, FALSE },
      { COLORCONV_16_TO_8,   16, 8,  FALSE },
      { COLORCONV_16_TO_15,  16, 15, FALSE },
      { COLORCONV_16_TO_24,  16, 24, FALSE },
      { COLORCONV_16_TO_32,  16, 32, FALSE },
      { COLORCONV_24_TO_8,   24, 8,  FALSE },
      { COLORCONV_24_TO_15,  24, 15, FALSE },
      { COLORCONV_24_TO_16,  24, 16, FALSE },
      { COLORCONV_24_TO_32,  24, 32, FALSE },
      { COLORCONV_32_TO_8,   32, 8,  FALSE },
      { COLORCONV_32_TO_15,  32, 15, FALSE },
      { COLORCONV_32_TO_16,  32, 16, FALSE },
      { COLORCONV_32_TO_24,  32, 24, FALSE },
      { COLORCONV_32A_TO_8,  32, 8 , TRUE  },
      { COLORCONV_32A_TO_15, 32, 15, TRUE  },
      { COLORCONV_32A_TO_16, 32, 16, TRUE  },
      { COLORCONV_32A_TO_24, 32, 24, TRUE  }
   };

   int i;

   ASSERT((_gfx_mode_set_count > 0) || (color_conv_set));

   if (depth == _color_depth)
      return depth;

   for (i=0; i < (int)(sizeof(conversion_flags)/sizeof(CONVERSION_FLAGS)); i++) {
      if ((conversion_flags[i].in_depth == depth) &&
	  (conversion_flags[i].out_depth == _color_depth) &&
	  ((conversion_flags[i].hasalpha != 0) == (hasalpha != 0))) {
	 if (_color_conv & conversion_flags[i].flag)
	    return _color_depth;
	 else
	    return depth;
      }
   }

   ASSERT(FALSE);
   return 0;
}



/* create_bitmap_ex
 *  Creates a new memory bitmap in the specified color_depth
 */
BITMAP *create_bitmap_ex(int color_depth, int width, int height)
{
   GFX_VTABLE *vtable;
   BITMAP *bitmap;
   int i;
   ASSERT(width >= 0);
   ASSERT(height > 0);
   ASSERT(system_driver);

   if (system_driver->create_bitmap)
      return system_driver->create_bitmap(color_depth, width, height);

   vtable = _get_vtable(color_depth);
   if (!vtable)
      return NULL;

   bitmap = malloc(sizeof(BITMAP) + (sizeof(char *) * height));
   if (!bitmap)
      return NULL;

   bitmap->dat = malloc(width * height * BYTES_PER_PIXEL(color_depth));
   if (!bitmap->dat) {
      free(bitmap);
      return NULL;
   }

   bitmap->w = bitmap->cr = width;
   bitmap->h = bitmap->cb = height;
   bitmap->clip = TRUE;
   bitmap->cl = bitmap->ct = 0;
   bitmap->vtable = vtable;
   bitmap->write_bank = bitmap->read_bank = _stub_bank_switch;
   bitmap->id = 0;
   bitmap->extra = NULL;
   bitmap->x_ofs = 0;
   bitmap->y_ofs = 0;
   bitmap->seg = _default_ds();

   if (height > 0) {
      bitmap->line[0] = bitmap->dat;
      for (i=1; i<height; i++)
         bitmap->line[i] = bitmap->line[i-1] + width * BYTES_PER_PIXEL(color_depth);
   }

   if (system_driver->created_bitmap)
      system_driver->created_bitmap(bitmap);

   return bitmap;
}



/* create_bitmap:
 *  Creates a new memory bitmap.
 */
BITMAP *create_bitmap(int width, int height)
{
   ASSERT(width >= 0);
   ASSERT(height > 0);
   return create_bitmap_ex(_color_depth, width, height);
}



/* create_sub_bitmap:
 *  Creates a sub bitmap, ie. a bitmap sharing drawing memory with a
 *  pre-existing bitmap, but possibly with different clipping settings.
 *  Usually will be smaller, and positioned at some arbitrary point.
 *
 *  Mark Wodrich is the owner of the brain responsible this hugely useful 
 *  and beautiful function.
 */
BITMAP *create_sub_bitmap(BITMAP *parent, int x, int y, int width, int height)
{
   BITMAP *bitmap;
   int i;

   ASSERT(parent);
   ASSERT((x >= 0) && (y >= 0) && (x < parent->w) && (y < parent->h));
   ASSERT((width > 0) && (height > 0));
   ASSERT(system_driver);

   if (x+width > parent->w) 
      width = parent->w-x;

   if (y+height > parent->h) 
      height = parent->h-y;

   if (parent->vtable->create_sub_bitmap)
      return parent->vtable->create_sub_bitmap(parent, x, y, width, height);

   if (system_driver->create_sub_bitmap)
      return system_driver->create_sub_bitmap(parent, x, y, width, height);

   /* get memory for structure and line pointers */
   bitmap = malloc(sizeof(BITMAP) + (sizeof(char *) * height));
   if (!bitmap)
      return NULL;

   acquire_bitmap(parent);

   bitmap->w = bitmap->cr = width;
   bitmap->h = bitmap->cb = height;
   bitmap->clip = TRUE;
   bitmap->cl = bitmap->ct = 0;
   bitmap->vtable = parent->vtable;
   bitmap->write_bank = parent->write_bank;
   bitmap->read_bank = parent->read_bank;
   bitmap->dat = NULL;
   bitmap->extra = NULL;
   bitmap->x_ofs = x + parent->x_ofs;
   bitmap->y_ofs = y + parent->y_ofs;
   bitmap->seg = parent->seg;

   /* All bitmaps are created with zero ID's. When a sub-bitmap is created,
    * a unique ID is needed to identify the relationship when blitting from
    * one to the other. This is obtained from the global variable
    * _sub_bitmap_id_count, which provides a sequence of integers (yes I
    * know it will wrap eventually, but not for a long time :-) If the
    * parent already has an ID the sub-bitmap adopts it, otherwise a new
    * ID is given to both the parent and the child.
    */
   if (!(parent->id & BMP_ID_MASK)) {
      parent->id |= _sub_bitmap_id_count;
      _sub_bitmap_id_count = (_sub_bitmap_id_count+1) & BMP_ID_MASK;
   }

   bitmap->id = parent->id | BMP_ID_SUB;
   bitmap->id &= ~BMP_ID_LOCKED;

   if (is_planar_bitmap(bitmap))
      x /= 4;

   x *= BYTES_PER_PIXEL(bitmap_color_depth(bitmap));

   /* setup line pointers: each line points to a line in the parent bitmap */
   for (i=0; i<height; i++)
      bitmap->line[i] = parent->line[y+i] + x;

   if (bitmap->vtable->set_clip)
      bitmap->vtable->set_clip(bitmap);

   if (parent->vtable->created_sub_bitmap)
      parent->vtable->created_sub_bitmap(bitmap, parent);

   if (system_driver->created_sub_bitmap)
      system_driver->created_sub_bitmap(bitmap, parent);

   if (parent->id & BMP_ID_VIDEO)
      _register_switch_bitmap(bitmap, parent);

   release_bitmap(parent);

   return bitmap;
}



/* set_clip_rect:
 *  Sets the two opposite corners of the clipping rectangle to be used when
 *  drawing to the bitmap. Nothing will be drawn to positions outside of this 
 *  rectangle. When a new bitmap is created the clipping rectangle will be 
 *  set to the full area of the bitmap.
 */
void set_clip_rect(BITMAP *bitmap, int x1, int y1, int x2, int y2)
{
   ASSERT(bitmap);

   /* internal clipping is inclusive-exclusive */
   x2++;
   y2++;

   bitmap->cl = MID(0, x1, bitmap->w-1);
   bitmap->ct = MID(0, y1, bitmap->h-1);
   bitmap->cr = MID(0, x2, bitmap->w);
   bitmap->cb = MID(0, y2, bitmap->h);

   if (bitmap->vtable->set_clip)
      bitmap->vtable->set_clip(bitmap);
}



/* add_clip_rect:
 *  Makes the new clipping rectangle the intersection between the given
 *  rectangle and the current one.
 */
void add_clip_rect(BITMAP *bitmap, int x1, int y1, int x2, int y2)
{
   int cx1, cy1, cx2, cy2;

   ASSERT(bitmap);

   get_clip_rect(bitmap, &cx1, &cy1, &cx2, &cy2);

   x1 = MAX(x1, cx1);
   y1 = MAX(y1, cy1);
   x2 = MIN(x2, cx2);
   y2 = MIN(y2, cy2);

   set_clip_rect(bitmap, x1, y1, x2, y2);
}



/* set_clip:
 *  Sets the two opposite corners of the clipping rectangle to be used when
 *  drawing to the bitmap. Nothing will be drawn to positions outside of this 
 *  rectangle. When a new bitmap is created the clipping rectangle will be 
 *  set to the full area of the bitmap. If x1, y1, x2 and y2 are all zero 
 *  clipping will be turned off, which will slightly speed up drawing 
 *  operations but will allow memory to be corrupted if you attempt to draw 
 *  off the edge of the bitmap.
 */
void set_clip(BITMAP *bitmap, int x1, int y1, int x2, int y2)
{
   int t;

   ASSERT(bitmap);

   if ((!x1) && (!y1) && (!x2) && (!y2)) {
      set_clip_rect(bitmap, 0, 0, bitmap->w-1, bitmap->h-1);
      set_clip_state(bitmap, FALSE);
      return;
   }

   if (x2 < x1) {
      t = x1;
      x1 = x2;
      x2 = t;
   }

   if (y2 < y1) {
      t = y1;
      y1 = y2;
      y2 = t;
   }

   set_clip_rect(bitmap, x1, y1, x2, y2);
   set_clip_state(bitmap, TRUE);
}
