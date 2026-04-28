/*---------------------------------------------------------------------------/
/  Configurations of FatFs Module — joypad-os
/---------------------------------------------------------------------------*/
/*
 * ChaN FatFs R0.15 patch1, vendored at src/lib/fatfs/.
 * This file overrides defaults for our use case:
 *   - read/write FAT16/FAT32 SD cards
 *   - long filenames so user-facing files stay readable on PC
 *   - single volume (one SD slot per device)
 *   - no chdir / no concurrent locking (single-task access)
 *   - mkfs enabled so device can format a fresh card
 */

#define FFCONF_DEF	80286

/*---------------------------------------------------------------------------/
/ Function Configurations
/---------------------------------------------------------------------------*/

#define FF_FS_READONLY	0       /* read+write */
#define FF_FS_MINIMIZE	0       /* full API surface */
#define FF_USE_FIND		0
#define FF_USE_MKFS		1       /* allow formatting */
#define FF_USE_FASTSEEK	0
#define FF_USE_EXPAND	0
#define FF_USE_CHMOD	0
#define FF_USE_LABEL	0
#define FF_USE_FORWARD	0
#define FF_USE_STRFUNC	1       /* gets/puts/printf */
#define FF_PRINT_LLI	0       /* no long-long printf */
#define FF_PRINT_FLOAT	0
#define FF_STRF_ENCODE	3       /* UTF-8 */

/*---------------------------------------------------------------------------/
/ Locale and Namespace Configurations
/---------------------------------------------------------------------------*/

#define FF_CODE_PAGE	437     /* US/Latin (small table; fine for ASCII filenames) */
#define FF_USE_LFN		1       /* LFN with stack buffer */
#define FF_MAX_LFN		255
#define FF_LFN_UNICODE	0       /* ANSI/OEM API for now (smallest) */
#define FF_LFN_BUF		255
#define FF_SFN_BUF		12
#define FF_FS_RPATH		0       /* no chdir */

/*---------------------------------------------------------------------------/
/ Drive/Volume Configurations
/---------------------------------------------------------------------------*/

#define FF_VOLUMES		1
#define FF_STR_VOLUME_ID	0
#define FF_VOLUME_STRS		"RAM","NAND","CF","SD","SD2","USB","USB2","USB3"
#define FF_MULTI_PARTITION	0
#define FF_MIN_SS		512
#define FF_MAX_SS		512
#define FF_LBA64		0
#define FF_MIN_GPT		0x10000000
#define FF_USE_TRIM		0

/*---------------------------------------------------------------------------/
/ System Configurations
/---------------------------------------------------------------------------*/

#define FF_FS_TINY		0       /* private sector buf per file (fine for one file at a time) */
#define FF_FS_EXFAT		0       /* exFAT off — keeps code small, FAT16/32 covers our use */
#define FF_FS_NORTC		1       /* no RTC — files get a fixed timestamp */
#define FF_NORTC_MON	1
#define FF_NORTC_MDAY	1
#define FF_NORTC_YEAR	2026
#define FF_FS_NOFSINFO	0
#define FF_FS_LOCK		0       /* no concurrent open lock — single-task access */
#define FF_FS_REENTRANT	0       /* no syscall-level reentrancy */
#define FF_FS_TIMEOUT	1000
#define FF_SYNC_t		HANDLE
