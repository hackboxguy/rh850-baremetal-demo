/*
 * lib_buildinfo.h - Compile-time build date/time in BCD format
 *
 * Parses __DATE__ ("Mmm dd yyyy") and __TIME__ ("hh:mm:ss") into
 * BCD bytes at compile time. Zero runtime overhead.
 *
 * Usage:
 *   BUILD_YEAR_HI  = 0x20  (for year 2026)
 *   BUILD_YEAR_LO  = 0x26
 *   BUILD_MONTH    = 0x04  (April)
 *   BUILD_DAY      = 0x08
 *   BUILD_HOUR     = 0x17
 *   BUILD_MINUTE   = 0x30
 */

#ifndef LIB_BUILDINFO_H
#define LIB_BUILDINFO_H

/*
 * __DATE__ format: "Mmm dd yyyy" (e.g. "Apr  8 2026", "Nov 15 2026")
 *                   0123456789A
 *
 * __TIME__ format: "hh:mm:ss" (e.g. "17:30:45")
 *                   01234567
 */

/* ---- Year: positions 7-10 of __DATE__ ---- */
#define _BUILD_YEAR_CH0     (__DATE__[7])     /* '2' */
#define _BUILD_YEAR_CH1     (__DATE__[8])     /* '0' */
#define _BUILD_YEAR_CH2     (__DATE__[9])     /* '2' */
#define _BUILD_YEAR_CH3     (__DATE__[10])    /* '6' */

#define BUILD_YEAR_HI       ((uint8)(((_BUILD_YEAR_CH0 - '0') << 4) | \
                                      (_BUILD_YEAR_CH1 - '0')))
#define BUILD_YEAR_LO       ((uint8)(((_BUILD_YEAR_CH2 - '0') << 4) | \
                                      (_BUILD_YEAR_CH3 - '0')))

/* ---- Month: positions 0-2 of __DATE__ ---- */
#define _BUILD_MON_CH0      (__DATE__[0])
#define _BUILD_MON_CH1      (__DATE__[1])
#define _BUILD_MON_CH2      (__DATE__[2])

/* Convert 3-letter month abbreviation to BCD 01-12 */
#define BUILD_MONTH         ((uint8)(                                        \
    (_BUILD_MON_CH1 == 'a' && _BUILD_MON_CH2 == 'n') ? 0x01u :  /* Jan */   \
    (_BUILD_MON_CH1 == 'e' && _BUILD_MON_CH2 == 'b') ? 0x02u :  /* Feb */   \
    (_BUILD_MON_CH1 == 'a' && _BUILD_MON_CH2 == 'r') ? 0x03u :  /* Mar */   \
    (_BUILD_MON_CH1 == 'p' && _BUILD_MON_CH2 == 'r') ? 0x04u :  /* Apr */   \
    (_BUILD_MON_CH1 == 'a' && _BUILD_MON_CH2 == 'y') ? 0x05u :  /* May */   \
    (_BUILD_MON_CH1 == 'u' && _BUILD_MON_CH2 == 'n') ? 0x06u :  /* Jun */   \
    (_BUILD_MON_CH1 == 'u' && _BUILD_MON_CH2 == 'l') ? 0x07u :  /* Jul */   \
    (_BUILD_MON_CH1 == 'u' && _BUILD_MON_CH2 == 'g') ? 0x08u :  /* Aug */   \
    (_BUILD_MON_CH1 == 'e' && _BUILD_MON_CH2 == 'p') ? 0x09u :  /* Sep */   \
    (_BUILD_MON_CH0 == 'O')                           ? 0x10u :  /* Oct */   \
    (_BUILD_MON_CH0 == 'N')                           ? 0x11u :  /* Nov */   \
    (_BUILD_MON_CH0 == 'D')                           ? 0x12u :  /* Dec */   \
    0x00u))

/* ---- Day: positions 4-5 of __DATE__ (space-padded: " 8" or "15") ---- */
#define _BUILD_DAY_CH0      (__DATE__[4])     /* ' ' or '1'-'3' */
#define _BUILD_DAY_CH1      (__DATE__[5])     /* '0'-'9' */

#define BUILD_DAY           ((uint8)(                                        \
    (_BUILD_DAY_CH0 == ' ')                                                  \
        ? (_BUILD_DAY_CH1 - '0')                                             \
        : ((uint8)((_BUILD_DAY_CH0 - '0') << 4) | (_BUILD_DAY_CH1 - '0'))))

/* ---- Hour: positions 0-1 of __TIME__ ---- */
#define BUILD_HOUR          ((uint8)(((__TIME__[0] - '0') << 4) | \
                                      (__TIME__[1] - '0')))

/* ---- Minute: positions 3-4 of __TIME__ ---- */
#define BUILD_MINUTE        ((uint8)(((__TIME__[3] - '0') << 4) | \
                                      (__TIME__[4] - '0')))

#endif /* LIB_BUILDINFO_H */
