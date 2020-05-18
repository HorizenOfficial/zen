#include <sc/TEMP_zendooError.h>

/*
* Get a short description of an error's category.
*/
const char* zendoo_get_category_name(uint32_t category) {
    switch(category) {
        case 0: return "General";
        case 1: return "Unable to read/write";
        case 2: return "Crypto error";
        default: return "Unknown";
    }
}

/*
* Clear the `LAST_ERROR` variable.
*/
void zendoo_clear_error(void) { }

/*
* Retrieve the most recent `Error` from the `LAST_ERROR` variable.
*
* # Safety
*
* The error message will be freed if another error occurs. It is the caller's
* responsibility to make sure they're no longer using the `Error` before
* calling any function which may set `LAST_ERROR`.
*/
Error zendoo_get_last_error(void) { 
    Error err;
    err.category = GENERAL_ERROR;
    err.msg = "";
    return err; 
}

void print_error(const char *msg) {
    Error e = zendoo_get_last_error();
        fprintf(stderr,
            "%s: %s [%d - %s]\n",
            msg,
            e.msg,
            e.category,
            zendoo_get_category_name(e.category));
}