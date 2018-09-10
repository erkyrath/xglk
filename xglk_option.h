#ifndef XGLK_OPTION_H
#define XGLK_OPTION_H

/* This is the only XGlk source file you should need to edit
    in order to compile the library. 
*/

/* Options: */

#define OPT_ALL_KEYS_TYPABLE

/* OPT_ALL_KEYS_TYPABLE should be defined if your keyboard is capable
   of composing the entire Latin-1 character set. If it is not defined,
   XGlk will assume that you can only type the standard 7-bit ASCII
   set. This information is only used by the gestalt selector, but
   it just might be important to the game, so don't lie.
*/

#endif /* XGLK_OPTION_H */
