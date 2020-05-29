use std::cell::RefCell;
use std::ffi::CString;
use std::os::raw::c_char;
use std::ptr;

pub const GENERAL_ERROR: u32 = 0;
pub const IO_ERROR: u32 = 1;
pub const CRYPTO_ERROR: u32 = 2;

type StdError = Box<dyn std::error::Error>;

/// Defining an error to return when dereferencing null pointers
#[derive(Debug, Clone)]
pub struct NullPointerError(pub String);

use std::fmt::{Display, Formatter, Result};

impl Display for NullPointerError {
    fn fmt(&self, f: &mut Formatter) -> Result {
        write!(f, "{}", self.0)
    }
}

impl std::error::Error for NullPointerError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        // Generic error, underlying cause isn't tracked.
        None
    }
}

/// Get a short description of an error's category.
#[no_mangle]
pub extern "C" fn zendoo_get_category_name(category: u32) -> *const c_char {
    // NOTE: Update this every time a new category constant is added
    let s: &[u8] = match category {
        GENERAL_ERROR => b"General\0",
        IO_ERROR => b"Unable to read/write\0",
        CRYPTO_ERROR => b"Crypto error\0",
        _ => b"Unknown\0",
    };
    s.as_ptr() as *const c_char
}

thread_local! {
    /// An `errno`-like thread-local variable which keeps track of the most
    /// recent error to occur.
    static LAST_ERROR: RefCell<Option<LastError>> = RefCell::new(None);
}

#[derive(Debug)]
struct LastError {
    pub error: StdError,
    pub c_string: CString,
    pub category: u32,
}

/// Extra information about an error.
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(C)]
pub struct Error {
    /// A human-friendly error message (`null` if there wasn't one).
    pub msg: *const c_char,
    /// The general error category.
    pub category: u32,
}

impl Default for Error {
    fn default() -> Error {
        // A default `Error` for when no error has actually occurred
        Error {
            msg: ptr::null(),
            category: GENERAL_ERROR,
        }
    }
}

/// Call `set_last_error()`, with a default error category.
pub fn set_general_error(err: StdError) {
    set_last_error(err, GENERAL_ERROR);
}

pub fn set_last_error(err: StdError, category: u32) {
    LAST_ERROR.with(|l| {
        let c_string = CString::new(err.to_string()).unwrap_or_default();

        let new_error = LastError {
            error: err,
            c_string,
            category,
        };

        *l.borrow_mut() = Some(new_error);
    });
}

/// Retrieve the most recent `Error` from the `LAST_ERROR` variable.
///
/// # Safety
///
/// The error message will be freed if another error occurs. It is the caller's
/// responsibility to make sure they're no longer using the `Error` before
/// calling any function which may set `LAST_ERROR`.
#[no_mangle]
pub unsafe extern "C" fn zendoo_get_last_error() -> Error {
    LAST_ERROR.with(|l| match l.borrow().as_ref() {
        Some(err) => Error {
            msg: err.c_string.as_ptr(),
            category: err.category,
        },
        None => Error::default(),
    })
}

/// Clear the `LAST_ERROR` variable.
#[no_mangle]
pub extern "C" fn zendoo_clear_error() {
    LAST_ERROR.with(|l| l.borrow_mut().take());
}
