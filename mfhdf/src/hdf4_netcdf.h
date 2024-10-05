/*
 *    Copyright 1993, University Corporation for Atmospheric Research
 *
 *  Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose without fee is hereby granted, provided
 * that the above copyright notice appear in all copies, that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of UCAR/Unidata not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  UCAR makes no
 * representations about the suitability of this software for any purpose.
 * It is provided "as is" without express or implied warranty.  It is
 * provided with no support and without obligation on the part of UCAR
 * Unidata, to assist in its use, correction, modification, or enhancement.
 *
 */

#ifndef MFH4_HDF4_NETCDF_H
#define MFH4_HDF4_NETCDF_H

#include <inttypes.h>

#include "H4api_adpt.h"

/*
 * The following macro is provided for backward compatibility only.  If you
 * are a new user of netCDF, then you may safely ignore it.  If, however,
 * you have an existing archive of netCDF files that use default
 * floating-point fill values, then you should know that the definition of
 * the default floating-point fill values changed with version 2.3 of the
 * netCDF package.  Prior to this release, the default floating-point fill
 * values were not very portable:  their correct behavior depended not only
 * upon the particular platform, but also upon the compilation
 * environment.  This led to the definition of new, default floating-point
 * fill values that are portable across all platforms and compilation
 * environments.  If you wish, however, to obtain the old, non-portable
 * floating-point fill values, then the following macro should have a true
 * value PRIOR TO BUILDING THE netCDF LIBRARY.
 *
 * Implementation details are contained in the section below on fill values.
 */
#define NC_OLD_FILLVALUES 0

/* Fill values
 *
 * These values are stuffed into newly allocated space as appropriate.
 * The hope is that one might use these to notice that a particular datum
 * has not been set.
 */

#define FILL_BYTE  ((char)-127) /* Largest Negative value */
#define FILL_CHAR  ((char)0)
#define FILL_SHORT ((short)-32767)
#define FILL_LONG  ((long)-2147483647)

#if !NC_OLD_FILLVALUES

#define FILL_FLOAT  9.9692099683868690e+36F /* near 15 * 2^119 */
#define FILL_DOUBLE 9.9692099683868690e+36

#else /* NC_OLD_FILLVALUES below */

/*
 * This section is provided for backward compatibility only.  Using
 * XDR infinities for floating-point fill values has caused more problems
 * than it has solved.  We encourage you to define your own data-specific
 * fill values rather than use default ones (see `_FillValue' below).
 * If, however, you *must* use default fill values, then you should use
 * the above fill values rather than the ones in this section.
 */

/*
 * XDR_F_INFINITY is a float value whose EXTERNAL (xdr)
 * representation is ieee floating infinity.
 * XDR_D_INFINITY is a double value whose EXTERNAL (xdr)
 * representation is ieee double floating point infinity.
 * These are used as default fill values below.
 *
 * This section shows three techniques for setting these:
 *  Direct assignment (vax, cray) - works for non IEEE machines
 *        Doesn't work when IEEE machines don't allow
 *      float or double constants whose values are infinity.
 *  Use of a union (preferred portable method) - should work on
 *      any ANSI compiler with IEEE floating point representations,
 *      modulo byte order and sizeof() considerations.
 *  Use of pointer puns - may work with many older compilers
 *      which don't allow initialization of unions.
 *      Often doesn't work with compilers which have strict
 *      alignment rules.
 */

/* Direct assignment. All cases should be mutually exclusive */

/* Use of a union, assumes IEEE representation and 1 byte unsigned char */
#ifndef XDR_D_INFINITY
#define USE_D_UNION
union xdr_d_union {
    unsigned char bb[8];
    double        dd;
};
extern union xdr_d_union xdr_d_infs; /* instantiated in array.c */
#define XDR_D_INFINITY (xdr_d_infs.dd)
#endif /* !XDR_D_INFINITY */

#ifndef XDR_F_INFINITY
#define USE_F_UNION
union xdr_f_union {
    unsigned char bb[4];
    float         ff;
};
extern union xdr_f_union xdr_f_infs; /* instantiated in array.c */
#define XDR_F_INFINITY (xdr_f_infs.ff)
#endif /* !XDR_F_INFINITY */

/* End of INFINITY section */

#define FILL_FLOAT  XDR_F_INFINITY /* IEEE Infinity */
#define FILL_DOUBLE XDR_D_INFINITY

#endif /* NC_OLD_FILLVALUES above */

/*
 *  masks for the struct NC flags field; passed in as 'mode' arg to
 * nccreate and ncopen.
 *
 */
#define NC_RDWR   1      /* read/write, 0 => readonly */
#define NC_CREAT  2      /* in create phase, cleared by ncendef */
#define NC_EXCL   4      /* on create, don't destroy existing file */
#define NC_INDEF  8      /* in define mode, cleared by ncendef */
#define NC_NSYNC  0x10   /* synchronise numrecs on change */
#define NC_HSYNC  0x20   /* synchronise whole header on change */
#define NC_NDIRTY 0x40   /* numrecs has changed */
#define NC_HDIRTY 0x80   /* header info has changed */
#define NC_NOFILL 0x100  /* Don't fill vars on endef and increase of record */
#define NC_LINK   0x8000 /* isa link */

#define NC_FILL 0 /* argument to ncsetfill to clear NC_NOFILL */

/*
 * 'mode' arguments for nccreate and ncopen
 */
#define NC_NOWRITE   0
#define NC_WRITE     NC_RDWR
#define NC_CLOBBER   (NC_INDEF | NC_CREAT | NC_RDWR)
#define NC_NOCLOBBER (NC_INDEF | NC_EXCL | NC_CREAT | NC_RDWR)

/*
 * 'size' argument to ncdimdef for an unlimited dimension
 */
#define NC_UNLIMITED 0L

/*
 * attribute id to put/get a global attribute
 */
#define NC_GLOBAL -1

#include "hlimits.h" /* Hard coded constants for HDF library */

/* This type used to be defined as an enum, but the C standard is a bit
 * vague as to which integer type you actually get to represent your enum
 * and passing that through a pointer caused failures on MacOS.
 */
typedef int nc_type;
#define NC_UNSPECIFIED 0 /* private */
#define NC_BYTE        1
#define NC_CHAR        2
#define NC_SHORT       3
#define NC_LONG        4
#define NC_FLOAT       5
#define NC_DOUBLE      6
/* private */
#define NC_BITFIELD  7
#define NC_STRING    8
#define NC_IARRAY    9
#define NC_DIMENSION 10
#define NC_VARIABLE  11
#define NC_ATTRIBUTE 12

/*
 * C data types corresponding to netCDF data types:
 */
/* Don't use these or the C++ interface gets confused
typedef char  ncchar;
typedef char  ncbyte;
typedef short ncshort;
typedef float ncfloat;
typedef double        ncdouble;
*/

/*
 * Variables/attributes of type NC_LONG should use the C type 'nclong',
 * which should map to a 32-bit integer.
 */
typedef int32_t nclong;

/*
 * Global netcdf error status variable
 *  Initialized in error.c
 */
#define NC_NOERR        0           /* No Error */
#define NC_EBADID       1           /* Not a netcdf id */
#define NC_ENFILE       2           /* Too many netcdfs open */
#define NC_EEXIST       3           /* netcdf file exists && NC_NOCLOBBER */
#define NC_EINVAL       4           /* Invalid Argument */
#define NC_EPERM        5           /* Write to read only */
#define NC_ENOTINDEFINE 6           /* Operation not allowed in data mode */
#define NC_EINDEFINE    7           /* Operation not allowed in define mode */
#define NC_EINVALCOORDS 8           /* Coordinates out of Domain */
#define NC_EMAXDIMS     9           /* MAX_NC_DIMS exceeded */
#define NC_ENAMEINUSE   10          /* String match to name in use */
#define NC_ENOTATT      11          /* Attribute not found */
#define NC_EMAXATTS     12          /* MAX_NC_ATTRS exceeded */
#define NC_EBADTYPE     13          /* Not a netcdf data type */
#define NC_EBADDIM      14          /* Invalid dimension id */
#define NC_EUNLIMPOS    15          /* NC_UNLIMITED in the wrong index */
#define NC_EMAXVARS     16          /* MAX_NC_VARS exceeded */
#define NC_ENOTVAR      17          /* Variable not found */
#define NC_EGLOBAL      18          /* Action prohibited on NC_GLOBAL varid */
#define NC_ENOTNC       19          /* Not a netcdf file */
#define NC_ESTS         20          /* In Fortran, string too short */
#define NC_EMAXNAME     21          /* MAX_NC_NAME exceeded */
#define NC_ENTOOL       NC_EMAXNAME /* Backward compatibility */
#define NC_EUNLIMIT     22          /* NC_UNLIMITED size already in use */

#define NC_EXDR   32 /* */
#define NC_SYSERR -1

#include "hdf2netcdf.h"
HDFLIBAPI int ncerr;

/*
 * Global options variable. Used to determine behavior of error handler.
 *  Initialized in lerror.c
 */
#define NC_FATAL   1
#define NC_VERBOSE 2

HDFLIBAPI int ncopts; /* default is (NC_FATAL | NC_VERBOSE) */

#ifdef __cplusplus
extern "C" {
#endif

HDFLIBAPI int nccreate(const char *path, int cmode);
HDFLIBAPI int ncopen(const char *path, int mode);
HDFLIBAPI int ncendef(int cdfid);
HDFLIBAPI int ncclose(int cdfid);
HDFLIBAPI int ncabort(int cdfid);
HDFLIBAPI int ncdimdef(int cdfid, const char *name, long length);
HDFLIBAPI int ncdiminq(int cdfid, int dimid, char *name, long *length);
HDFLIBAPI int ncvardef(int cdfid, const char *name, nc_type datatype, int ndims, const int *dim);
HDFLIBAPI int ncvarid(int cdfid, const char *name);
HDFLIBAPI int ncvarinq(int cdfid, int varid, char *name, nc_type *datatype, int *ndims, int *dim, int *natts);
HDFLIBAPI int ncvarput1(int cdfid, int varid, const long *coords, const void *value);
HDFLIBAPI int ncvarget1(int cdfid, int varid, const long *coords, void *value);
HDFLIBAPI int ncvarput(int cdfid, int varid, const long *start, const long *count, void *value);
HDFLIBAPI int ncvarget(int cdfid, int varid, const long *start, const long *count, void *value);
HDFLIBAPI int ncvarputs(int cdfid, int varid, const long *start, const long *count, const long *stride,
                        void *values);
HDFLIBAPI int ncvargets(int cdfid, int varid, const long *start, const long *count, const long *stride,
                        void *values);
HDFLIBAPI int ncvarputg(int cdfid, int varid, const long *start, const long *count, const long *stride,
                        const long *imap, void *values);
HDFLIBAPI int ncvargetg(int cdfid, int varid, const long *start, const long *count, const long *stride,
                        const long *imap, void *values);
HDFLIBAPI int ncattput(int cdfid, int varid, const char *name, nc_type datatype, int len, const void *value);
HDFLIBAPI int nctypelen(nc_type datatype);
HDFLIBAPI int ncsetfill(int cdfid, int fillmode);
#ifdef __cplusplus
}
#endif

#endif /* MFH4_HDF4_NETCDF_H */
