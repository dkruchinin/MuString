/*
 * Standard POSIX error codes.
 */

#ifndef __ERRNO_H__
#define __ERRNO_H__ 

enum {
    EPERM = 1,       /**< Operation not permitted */
    ENOENT,          /**< No such file or directory */
    ESRCH,           /**< No such process */
    EINTR,           /**< Interrupted system call */
    EIO,             /**< I/O error */
    ENXIO,           /**< No such device or address */
    E2BIG,           /**< Argument list too long */
    ENOEXEC,         /**< Exec format error */
    EBADF,           /**< Bad file number */
    ECHILD,          /**< No child processes */
    EAGAIN,          /**< Try again */
    ENOMEM,          /**< Out of memory */
    EACCES,          /**< Permission denied */
    EFAULT,          /**< Bad address */
    ENOTBLK,         /**< Block device required */
    EBUSY,           /**< Device or resource busy */
    EEXIST,          /**< File exists */
    EXDEV,           /**< Cross-device link */
    ENODEV,          /**< No such device */
    ENOTDIR,         /**< Not a directory */
    EISDIR,          /**< Is a directory */
    EINVAL,          /**< Invalid argument */
    ENFILE,          /**< File table overflow */
    EMFILE,          /**< Too many open files */
    ENOTTY,          /**< Not a typewriter */
    ETXTBSY,         /**< Text file busy */
    EFBIG,           /**< File too large */
    ENOSPC,          /**< No space left on device */
    ESPIPE,          /**< Illegal seek */
    EROFS,           /**< Read-only file system */
    EMLINK,          /**< Too many links */
    EPIPE,           /**< Broken pipe */
    EDOM,            /**< Math argument out of domain of func */
    ERANGE,          /**< Math result not representable */
    ENOSYS,          /**< Not implemented system functionality. */
    EWOULDBLOCK,     /**< Call would block. */
    EDEADLOCK,       /**< Deadlock detected. */
    EALREADY,        /**< Connection already in progress. */
    EADDRNOTAVAIL,   /**< Address not available. */
};
    
#endif

