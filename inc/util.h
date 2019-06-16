/********************************************************************
 * util.h
 *
 *  Utility and helper functions (debug print etc)
 *
 *  April 5, 2019
 *
 *******************************************************************/

#ifndef __util_h__
#define __util_h__

#define     DB_ERR      0
#define     DB_INFO     1
#define     DB_VERBOSE  2

/********************************************************************
 * Function prototypes
 *
 */

void debug_lvl(int);
int  debug(int, char *, ...);
void echo_reply(void);

#endif  /* __util_h__ */
