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
 *      New keyboard API.
 *
 *      By Peter Wang.
 *
 *      See readme.txt for copyright information.
 */


#define ALLEGRO_NO_COMPATIBILITY

#include "allegro.h"
#include "allegro/internal/aintern.h"
#include ALLEGRO_INTERNAL_HEADER
#include "allegro/internal/aintern2.h"
#include "internal/system_new.h"


/* the active keyboard driver */
// FIXME: Clarify if we really can get away with a single driver. There will
// only be one source, yes. But we might have one SDL window and one GLFW
// window, both with a different driver (adding to the same event
// queue/source/whatever).
static AL_KEYBOARD_DRIVER *new_keyboard_driver = NULL;

/* mode flags */
/* TODO: use the config system for these */
bool _al_three_finger_flag = true;
bool _al_key_led_flag = true;



/* Provide a default naming for the most common keys. Keys whose
 * mapping changes dependind on the layout aren't listed - it's up to
 * the keyboard driver to do that.  All keyboard drivers should
 * provide their own implementation though, especially if they use
 * positional mapping.
 */
AL_CONST char *_al_keyboard_common_names[AL_KEY_MAX] =
{
   "(none)",     "A",          "B",          "C",
   "D",          "E",          "F",          "G",
   "H",          "I",          "J",          "K",
   "L",          "M",          "N",          "O",
   "P",          "Q",          "R",          "S",
   "T",          "U",          "V",          "W",
   "X",          "Y",          "Z",          "0",
   "1",          "2",          "3",          "4",
   "5",          "6",          "7",          "8",
   "9",          "PAD 0",      "PAD 1",      "PAD 2",
   "PAD 3",      "PAD 4",      "PAD 5",      "PAD 6",
   "PAD 7",      "PAD 8",      "PAD 9",      "F1",
   "F2",         "F3",         "F4",         "F5",
   "F6",         "F7",         "F8",         "F9",
   "F10",        "F11",        "F12",        "ESCAPE",
   "KEY60",      "KEY61",      "KEY62",      "BACKSPACE",
   "TAB",        "KEY65",      "KEY66",      "ENTER",
   "KEY68",      "KEY69",      "BACKSLASH",  "KEY71",
   "KEY72",      "KEY73",      "KEY74",      "SPACE",
   "INSERT",     "DELETE",     "HOME",       "END",
   "PGUP",       "PGDN",       "LEFT",       "RIGHT",
   "UP",         "DOWN",       "PAD /",      "PAD *",
   "PAD -",      "PAD +",      "PAD DELETE", "PAD ENTER",
   "PRINTSCREEN","PAUSE",      "KEY94",      "KEY95",
   "KEY96",      "KEY97",      "KEY98",      "KEY99",
   "KEY100",     "KEY101",     "KEY102",     "PAD =",
   "KEY104",     "KEY105",     "KEY106",     "KEY107",
   "KEY108",     "KEY109",     "KEY110",     "KEY111",
   "KEY112",     "KEY113",     "KEY114",     "LSHIFT",
   "RSHIFT",     "LCTRL",      "RCTRL",      "ALT",
   "ALTGR",      "LWIN",       "RWIN",       "MENU",
   "SCROLLLOCK", "NUMLOCK",    "CAPSLOCK"
};



/* al_install_keyboard: [primary thread]
 *  Install a keyboard driver. Returns true if successful. If a driver
 *  was already installed, nothing happens and true is returned.
 */
bool al_install_keyboard(void)
{
   _DRIVER_INFO *driver_list;
   int i;

   if (new_keyboard_driver)
      return true;

   // FIXME: of course, need to try all available drivers until one is found
   // (but please, not a global static array..) right now, only my dummy driver
   // is returned for testing..

#if 0
   if (system_driver->keyboard_drivers)
      driver_list = system_driver->keyboard_drivers();
   else
      driver_list = _al_keyboard_driver_list;

   for (i=0; driver_list[i].driver; i++) {
      new_keyboard_driver = driver_list[i].driver;
      new_keyboard_driver->name = new_keyboard_driver->desc = get_config_text(new_keyboard_driver->ascii_name);
      if (new_keyboard_driver->init())
	 break;
   }

   if (!driver_list[i].driver) {
      new_keyboard_driver = NULL;
      return false;
   }

   //set_leds(-1);
#endif

   AL_SYSTEM *system = al_system_driver();
   TRACE("al_install_keyboard: finding keyboard driver.\n");
   new_keyboard_driver = system->vt->get_keyboard_driver();
   TRACE("al_install_keyboard: Using %s.\n", new_keyboard_driver->name);

   // FIXME: check return value
   new_keyboard_driver->init();

   _add_exit_func(al_uninstall_keyboard, "al_uninstall_keyboard");

   return true;
}



/* al_uninstall_keyboard: [primary thread]
 *  Uninstalls the active keyboard driver, if any.  This will
 *  automatically unregister the keyboard event source with any event
 *  queues.
 *
 *  This function is automatically called when Allegro is shut down.
 */
void al_uninstall_keyboard(void)
{
   if (!new_keyboard_driver)
      return;

   //set_leds(-1);

   new_keyboard_driver->exit();
   new_keyboard_driver = NULL;

   _remove_exit_func(al_uninstall_keyboard);
}



/* al_get_keyboard:
 *  Return a pointer to an object representing the keyboard, that can
 *  be used as an event source.
 */
AL_KEYBOARD *al_get_keyboard(void)
{
   ASSERT(new_keyboard_driver);
   {
      AL_KEYBOARD *kbd = new_keyboard_driver->get_keyboard();
      ASSERT(kbd);

      return kbd;
   }
}



/* al_set_keyboard_leds:
 *  Overrides the state of the keyboard LED indicators.
 *  Set to -1 to return to default behavior.
 *  False is returned if the current keyboard driver cannot set LED indicators.
 */
bool al_set_keyboard_leds(int leds)
{
   ASSERT(new_keyboard_driver);

   if (new_keyboard_driver->set_leds)
      return new_keyboard_driver->set_leds(leds);

   return false;
}



/* al_keycode_to_name:
 *  Converts the given keycode to a description of the key.
 */
AL_CONST char *al_keycode_to_name(int keycode)
{
   AL_CONST char *name = NULL;

   ASSERT(new_keyboard_driver);
   ASSERT((keycode >= 0) && (keycode < AL_KEY_MAX));

   if (new_keyboard_driver->keycode_to_name)
      name = new_keyboard_driver->keycode_to_name(keycode);

   if (!name)
      name = _al_keyboard_common_names[keycode];

   ASSERT(name);

   return name;
}



/* al_get_keyboard_state: [primary thread]
 *  Save the state of the keyboard specified at the time the function
 *  is called into the structure pointed to by RET_STATE.
 */
void al_get_keyboard_state(AL_KBDSTATE *ret_state)
{
   ASSERT(new_keyboard_driver);
   ASSERT(ret_state);

   new_keyboard_driver->get_state(ret_state);
}



/* al_key_down:
 *  Return true if the key specified was held down in the state
 *  specified.
 */
bool al_key_down(AL_KBDSTATE *state, int keycode)
{
   return _AL_KBDSTATE_KEY_DOWN(*state, keycode);
}



/*
 * Local Variables:
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
