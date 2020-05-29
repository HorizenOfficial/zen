#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define GENERAL_ERROR 0

#define IO_ERROR 1

#define CRYPTO_ERROR 2

extern "C" {
    typedef struct {
      /*
       * A human-friendly error message (`null` if there wasn't one).
       */
      const char *msg;
      /*
       * The general error category.
       */
      uint32_t category;
    } Error;


    /*
     * Get a short description of an error's category.
     */
    const char *zendoo_get_category_name(uint32_t category);

    /*
     * Clear the `LAST_ERROR` variable.
     */
    void zendoo_clear_error(void);

    /*
     * Retrieve the most recent `Error` from the `LAST_ERROR` variable.
     *
     * # Safety
     *
     * The error message will be freed if another error occurs. It is the caller's
     * responsibility to make sure they're no longer using the `Error` before
     * calling any function which may set `LAST_ERROR`.
     */
    Error zendoo_get_last_error(void);
}

void print_error(const char *msg) {
    Error err = zendoo_get_last_error();

    fprintf(stderr,
            "%s: %s [%d - %s]\n",
            msg,
            err.msg,
            err.category,
            zendoo_get_category_name(err.category));
}