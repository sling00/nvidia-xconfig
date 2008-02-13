/*
 * nvidia-xconfig: A tool for manipulating X config files,
 * specifically for use by the NVIDIA Linux graphics driver.
 *
 * Copyright (C) 2006 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *      Free Software Foundation, Inc.
 *      59 Temple Place - Suite 330
 *      Boston, MA 02111-1307, USA
 *
 *
 * extract-edids.c
 *
 * This source file gives us the means to extract EDIDs from verbose X
 * log files or from .txt files. A verbose log will contain a raw EDID 
 * byte dump like this:
 *
 * (--) NVIDIA(0): Raw EDID bytes:
 * (--) NVIDIA(0):
 * (--) NVIDIA(0):   00 ff ff ff ff ff ff 00  5a 63 47 4b fc 27 00 00
 * (--) NVIDIA(0):   0f 0a 01 02 9e 1e 17 64  ee 04 85 a0 57 4a 9b 26
 * (--) NVIDIA(0):   12 50 54 00 08 00 01 01  01 01 01 01 01 01 01 01
 * (--) NVIDIA(0):   01 01 01 01 01 01 64 19  00 40 41 00 26 30 18 88
 * (--) NVIDIA(0):   36 00 30 e4 10 00 00 18  00 00 00 ff 00 47 4b 30
 * (--) NVIDIA(0):   31 35 31 30 32 33 36 0a  20 20 00 00 00 fc 00 56
 * (--) NVIDIA(0):   69 65 77 53 6f 6e 69 63  20 56 50 44 00 00 00 fc
 * (--) NVIDIA(0):   00 31 35 30 0a 20 20 20  20 20 20 20 20 20 00 ce
 * (--) NVIDIA(0):
 * (--) NVIDIA(0): --- End of EDID for ViewSonic VPD150 (DFP-1) ---
 *
 * A .txt file will contain a raw EDID byte dump like this:
 *
 * 00 FF FF FF FF FF FF 00-06 10 F4 01 01 01 01 01    ................
 * 27 08 01 01 28 1F 17 96-E8 44 E4 A1 57 4A 97 23    '...(....D..WJ.#
 * 19 4F 57 BF EE 00 01 01-01 01 01 01 01 01 01 01    .OW.............
 * 01 01 01 01 01 01 64 19-00 40 41 00 26 30 18 88    ......d..@A.&0..
 * 36 00 33 E6 10 00 00 18-40 1F 00 30 41 00 24 30    6.3.....@..0A.$0
 * 20 60 33 00 33 E6 10 00-00 18 00 00 00 FD 00 38     `3.3..........8
 * 4C 1F 3D 08 00 0A 20 20-20 20 20 20 00 00 00 FC    L.=...      ....
 * 00 41 70 70 6C 65 53 74-75 64 69 6F 0A 20 00 88    .AppleStudio. ..
 *
 * EDID Version                : 1.1
 *
 * We read a log file or a .txt file, identify and read any EDID(s) contained 
 * in the file, and then write the EDID bytes to edid.bin files (just
 * like what nvidia-settings can capture for display devices running
 * on the current X server).
 *
 * This is useful for NVIDIA engineers to simulate users' display
 * environments, based on a verbose nvidia-bug-report.log or X log or a .txt
 * file. This utility is included in nvidia-xconfig, since maybe users will
 * find use for this, too.
 */

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/types.h>
#include <pwd.h>
#include <stdarg.h>

#include "nvidia-xconfig.h"


#define NIBBLE_TO_HEX(n) (((n) <= 9) ? ('0' + (n)) : ('a' - 0xa + (n)))

#define HEX_TO_NIBBLE(n)                                                \
    ((((n) >= '0') && ((n) <= '9')) ? ((n) - '0') :                     \
        ((((n) >= 'a') && ((n) <= 'f')) ? (((n) - 'a') + 10) :          \
            ((((n) >= 'A') && ((n) <= 'F')) ? (((n) - 'A') + 10) : 0))) \

#define IS_HEX(n) ((((n) >= '0') && ((n) <= '9')) || \
                   (((n) >= 'a') && ((n) <= 'f')) || \
                   (((n) >= 'A') && ((n) <= 'F')))

#define TRUE 1
#define FALSE 0

#define EDID_OUTPUT_FILE_NAME "edid.bin"

#define LOG_FILE 10
#define TEXT_FILE 20
#define UNKNOWN_FILE 30

typedef struct {
    int size;
    unsigned char *bytes;
    char *name;
} EdidRec, *EdidPtr;

typedef struct {
    char *start;
    size_t length;
    char *current;
} FileRec, *FilePtr;


static int findFileType(FilePtr pFile);

static EdidPtr findEdidforLogFile(FilePtr pFile);
static EdidPtr findEdidforTextFile(FilePtr pFile);

static int findEdidHeaderforLogFile(FilePtr pFile);
static int readEdidDataforLogFile(FilePtr pFile, EdidPtr pEdid);
static int readEdidFooterforLogFile(FilePtr pFile, EdidPtr pEdid);

static int findEdidfooterforTextFile(FilePtr pFile);
static int readEdidDataforTextFile(FilePtr pFile, EdidPtr pEdid);
static int readMonitorNameforTextFile(FilePtr pFile, EdidPtr pEdid);

static char *findFileName(char *option);
static int writeEdidFile(EdidPtr pEdid, char *filename);

static void freeEdid(EdidPtr pEdid);



/*
 * extract_edids() - see description at the top of this file
 */

int extract_edids(Options *op)
{
    int fd = -1, ret, fileType, funcRet = FALSE;
    char *filename;
    
    struct stat stat_buf;
 
    FileRec file;
    EdidPtr pEdid, *pEdids;
    int nEdids, i;
    
    nEdids = 0;
    pEdid = NULL;
    pEdids = NULL;
    
    memset(&file, 0, sizeof(FileRec));
    file.start = (void *) -1;
    
    /* open the file and get its length */
    
    fd = open(op->extract_edids_from_file, O_RDONLY);
    
    if (fd == -1) {
        fmterr("Unable to open file \"%s\".", op->extract_edids_from_file);
        goto done;
    }
    
    ret = fstat(fd, &stat_buf);

    if (ret == -1) {
        fmterr("Unable to get length of file \"%s\".",
               op->extract_edids_from_file);
        goto done;
    }
    
    file.length = stat_buf.st_size;

    if (file.length == 0) {
        fmterr("File \"%s\" is empty.", op->extract_edids_from_file);
        goto done;
    }
    
    /* mmap the file */

    file.start = mmap(0, file.length, PROT_READ,
                      MAP_SHARED, fd, 0);

    if (file.start == (void *) -1) {
        fmterr("Unable to map file \"%s\".", op->extract_edids_from_file);
        goto done;
    }
    
    /* start parsing at the start of file */

    file.current = file.start;
    
    /* check for the file type(log or .txt) */

    fileType = findFileType(&file);
    
    /* if the file does not contain any edid information, goto done */
  
    if (fileType == UNKNOWN_FILE) {
        funcRet = TRUE;
        goto done;
    } 

    if (fileType == LOG_FILE) {
        file.current = file.start;
    }

    /* scan through the whole file, and build a list of pEdids */
    
    while(1) {
    
        if (fileType == LOG_FILE) {
    
            pEdid = findEdidforLogFile(&file);
    
        }
        
        if (fileType == TEXT_FILE) {
    
            pEdid = findEdidforTextFile(&file);
    
        }
   
        if (!pEdid) break;
        
        pEdids = nvrealloc(pEdids, sizeof(pEdids) * (nEdids + 1));
        
        pEdids[nEdids] = pEdid;
        nEdids++;

        /* Only one edid in a .txt file */

        if (fileType == TEXT_FILE) break;
    }
    
    /* fall through to the 'done' label */
    
    funcRet = TRUE;

 done:
    
    /* unmap and close the file */
    
    if (file.start != (void *) -1) {
        munmap(file.start, file.length);
    }

    if (fd != -1) {
        close(fd);
    }
    
    
    /* write the EDIDs to file */
    
    /*
     * determine the base filename; this is what we pass to
     * writeEdidFile; it will unique-ify from there
     */

    fmtout("");
    fmtout("Found %d EDID%s in \"%s\".",
           nEdids, (nEdids == 1) ? "": "s", op->extract_edids_from_file);

    filename = findFileName(op->extract_edids_output_file);
    
    for (i = 0; i < nEdids; i++) {
        
        pEdid = pEdids[i];

        funcRet = writeEdidFile(pEdid, filename);

        freeEdid(pEdid);
    }
    
    if (pEdids) nvfree(pEdids);

    nvfree(filename);
    
    fmtout("");

    return funcRet;

} // extract_edids()

/*
 * findFileType() - scan through the pFile to determine the file type
 * file type can be LOG_FILE, TEXT_FILE, UNKNOWN_FILE(file with no EDID) 
 */

static int findFileType(FilePtr pFile)
{ 
    if (findEdidHeaderforLogFile(pFile)) return LOG_FILE;
   
    if (findEdidfooterforTextFile(pFile)) return TEXT_FILE;

    return UNKNOWN_FILE;

} // findFileType()

/*
 * findEdid() - scan through pFile for an EDID header, if we find one,
 * parse the EDID data and footer.  On success, return a newly
 * allocated pEdid data structure.  On failure, return NULL.
 */
 
static EdidPtr findEdidforLogFile(FilePtr pFile)
{
    EdidPtr pEdid = nvalloc(sizeof(EdidRec));
    
    if (!findEdidHeaderforLogFile(pFile)) goto fail;
    
    if (!readEdidDataforLogFile(pFile, pEdid)) goto fail;

    if (!readEdidFooterforLogFile(pFile, pEdid)) goto fail;
    
    return pEdid;
    
 fail:
    
    freeEdid(pEdid);
    
    return NULL;
    
} // findEdidforLogFile()

/*
 * scan through the pFile for EDID data and Monitor name.
 */

static EdidPtr findEdidforTextFile(FilePtr pFile)
{
    EdidPtr pEdid = nvalloc(sizeof(EdidRec));

    if (!readEdidDataforTextFile(pFile,pEdid)) goto fail;
    if (!readMonitorNameforTextFile(pFile, pEdid)) goto fail;

    return pEdid;

 fail:

    freeEdid(pEdid);

    return NULL;

} // findEdidforTextFile()


/*
 * findEdidHeader() - scan the mmapped file, starting at
 * 'pFile->current', for the string "Raw EDID bytes:".  If we find the
 * string, return TRUE, and leave pFile->current pointing to the first
 * character past the string.  If we reach the end of the mmapped
 * file, return FALSE.
 */

static int findEdidHeaderforLogFile(FilePtr pFile)
{
    while (((pFile->current - pFile->start) + 15) <= pFile->length) {
        
        if ((pFile->current[0]  == 'R') &&
            (pFile->current[1]  == 'a') &&
            (pFile->current[2]  == 'w') &&
            (pFile->current[3]  == ' ') &&
            (pFile->current[4]  == 'E') &&
            (pFile->current[5]  == 'D') &&
            (pFile->current[6]  == 'I') &&
            (pFile->current[7]  == 'D') &&
            (pFile->current[8]  == ' ') &&
            (pFile->current[9]  == 'b') &&
            (pFile->current[10] == 'y') &&
            (pFile->current[11] == 't') &&
            (pFile->current[12] == 'e') &&
            (pFile->current[13] == 's') &&
            (pFile->current[14] == ':')) {
            
            pFile->current += 15;
            return TRUE;
        }
        pFile->current++;
    }

    return FALSE;
    
} // findEdidHeaderforLogFile()



/*
 * readEdidData() - start parsing at pFile->current for the EDID
 * string; it is assumed that pFile was advanced to the correct
 * position by findEdidHeader() (i.e., we should be immediately after
 * "Raw EDID bytes:").  We use a state machine to look for the lower
 * and upper nibbles of each EDID byte, and to advance past the label,
 * that looks something like "(--) NVIDIA(0):".
 */

#define STATE_LOOKING_FOR_TOP_NIBBLE    0
#define STATE_LOOKING_FOR_BOTTOM_NIBBLE 1
#define STATE_LOOKING_FOR_END_OF_LABEL 2

#define MAX_EDID_SIZE 4096

static int readEdidDataforLogFile(FilePtr pFile, EdidPtr pEdid)
{
    int state;
    
    unsigned char pData[MAX_EDID_SIZE];
    int k;

    char c;

    /* clear the scratch EDID data */

    bzero(pData, MAX_EDID_SIZE);

    /*
     * start the parsing state machine by looking for the upper nibble
     * of the first byte in the EDID
     */
    
    state = STATE_LOOKING_FOR_TOP_NIBBLE;
    k = 0;
    
    while(1) {
        
        c = pFile->current[0];

        switch (state) {
            
        case STATE_LOOKING_FOR_TOP_NIBBLE:
            
            /* if we hit a newline, transition to label parsing */
            
            if (c == '\n') {
                state = STATE_LOOKING_FOR_END_OF_LABEL;
                goto nextChar;
            }
            
            /* skip white space; keep looking for top nibble */
            
            if (isspace(c)) {
                state = STATE_LOOKING_FOR_TOP_NIBBLE;
                goto nextChar;
            }

            /*
             * if we found a hex value, treat it as upper nibble, then
             * look for lower nibble
             */

            if (IS_HEX(c)) {
                pData[k] |= ((HEX_TO_NIBBLE(c)) << 4);
                state = STATE_LOOKING_FOR_BOTTOM_NIBBLE;
                goto nextChar;
            }
            
            /*
             * if we find the text "--- End of EDID for ... ---", then
             * we want to parse that to find out the name of the
             * display device whose EDID we are reading; this is also
             * our exit condition for the state machine
             */
            
            if (c == '-') {
                goto done;
            }
            
            goto fail; /* anything else is an error */
            
            break;
            
        case STATE_LOOKING_FOR_BOTTOM_NIBBLE:

            /*
             * if we found a hex value, treat it as the lower nibble,
             * then look for the upper nibble of the next byte
             */
            
            if (IS_HEX(c)) {
                pData[k] |= (HEX_TO_NIBBLE(c));
                state = STATE_LOOKING_FOR_TOP_NIBBLE;
                k++;
                if (k >= MAX_EDID_SIZE) goto fail;
                goto nextChar;
            }
            
            goto fail; /* anything else is an error */
            
            break;
            
        case STATE_LOOKING_FOR_END_OF_LABEL:

            /*
             * if we find a colon, then we are at the end of the
             * label; transition to looking for the upper nibble of
             * the next EDID byte
             */
            
            if (c == ':') {
                state = STATE_LOOKING_FOR_TOP_NIBBLE;
                goto nextChar;
            }
            
            /*
             * anything else is assumed to be text within the label,
             * so just ignore it
             */
            
            break;

        default:
            
            goto fail; /* should never get here */
            
            break;
        }
   
    nextChar:
        
        /*
         * if we are at the end of the mapping without hitting our
         * exit condition, fail
         */
        
        if ((pFile->current - pFile->start) >= pFile->length) goto fail;
        
        /* move to the next character, and run the state machine again */

        pFile->current++;
        
    } /* while(1) */
    
 done:
    
    /* we are done parsing the EDID, save what we have into pEdid */
    
    if (k <= 0) goto fail;
    
    pEdid->size = k;
    pEdid->bytes = nvalloc(k);
    
    memcpy(pEdid->bytes, pData, k);
    
    return TRUE;

 fail:
    
    return FALSE;

} // readEdidDataforLogFile()

/*
 * read EDID data for the .txt file; pFile->current gives the starting 
 * position of the EDID bytes, which is same as file starting position.
 * We use a state machine to look for the lower and upper nibbles of each
 * EDID byte, and to advance past the label.
 */

static int readEdidDataforTextFile(FilePtr pFile, EdidPtr pEdid)
{
    int state;

    unsigned char pData[MAX_EDID_SIZE];
    int k;

    char c;

    /* clear the scratch EDID data */

    bzero(pData, MAX_EDID_SIZE);

    /*
     * start the parsing state machine by looking for the upper nibble
     * of the first byte in the EDID
     */

    state = STATE_LOOKING_FOR_TOP_NIBBLE;
    k = 0;

    while(1) {

    c = pFile->current[0];

        switch (state) {

        case STATE_LOOKING_FOR_TOP_NIBBLE:
            
            /*
             * if we found a hex value, treat it as upper nibble, then
             * look for lower nibble
             */

            if (IS_HEX(c)) {
                pData[k] |= ((HEX_TO_NIBBLE(c)) << 4);
                state = STATE_LOOKING_FOR_BOTTOM_NIBBLE;
                goto nextChar;
            }
            
            /* skip '-' and keep looking for top nibble */

            if (c == '-') {
                state = STATE_LOOKING_FOR_TOP_NIBBLE;
                goto nextChar;
            }
           
            /* 
             * if two consecutive white space, change lebel.
             * if one white space, skip it.
             */
  
            if (isspace(c)) {
                
                if (isspace(pFile->current[1])) {
                    state = STATE_LOOKING_FOR_END_OF_LABEL;
                    goto nextChar;
                } else {
                    state = STATE_LOOKING_FOR_TOP_NIBBLE;
                    goto nextChar;
                }
            }

            goto fail; /* anything else is an error */

            break;

        case STATE_LOOKING_FOR_BOTTOM_NIBBLE:

            /*
             * if we found a hex value, treat it as the lower nibble,
             * then look for the upper nibble of the next byte
             */
  
            if (IS_HEX(c)) {
                pData[k] |= (HEX_TO_NIBBLE(c));
                state = STATE_LOOKING_FOR_TOP_NIBBLE;
                k++;
                if (k >= MAX_EDID_SIZE) goto fail;
                goto nextChar;
            }

            goto fail; /* anything else is an error */

            break;

        case STATE_LOOKING_FOR_END_OF_LABEL:
            
            /* if we found two consecutive '\r\n', then the reding of EDID
             * information is complete. if only one '\r\n', then change the
             * state.
             */

            if (c == '\r' && pFile->current[1] == '\n') {
                
                if (pFile->current[2] == '\r' && pFile->current[3] == '\n') {
                   goto done;
                } else {
                    state = STATE_LOOKING_FOR_TOP_NIBBLE;
                    goto nextChar;
                }
            }
            
            /* skip the white space */
 
            if (isspace(c)) {
            
                state = STATE_LOOKING_FOR_END_OF_LABEL;
                goto nextChar;
 
            }

            break;

        default:

            goto fail;

            break;
        }

    nextChar:

        /*
         * if we are at the end of the mapping without hitting our
         * exit condition, fail
         */

        if ((pFile->current - pFile->start) >= pFile->length) goto fail;

        pFile->current++;
    } /* while(1) */

 done:
    
    /* we are done parsing the EDID, save what we have into pEdid */
   
    if (k <= 0) goto fail;

    pEdid->size = k;
    pEdid->bytes = nvalloc(k);

    memcpy(pEdid->bytes, pData, k);

    return TRUE;
     
 fail:

    return FALSE;

} // readEdidDataforTextFile()

/*
 * readEdidFooter() - the EDID footer is in the form:
 *
 * --- End of EDID for [dpy name] ---
 *
 * Parse the footer to get the dpy name.  pFile->current is expected
 * to point at the start of the footer.  On success, pEdid->name is
 * assigned and TRUE is returned.  On failure, FALSE is returned.
 */

static int readEdidFooterforLogFile(FilePtr pFile, EdidPtr pEdid)
{
    char *begin;
    int len;
    
    /* check that the mapping is large enough */

    if (((pFile->current - pFile->start) + 20) > pFile->length) {
        return FALSE;
    }

    /* make sure that the expected text is there */

    if ((pFile->current[0]  != '-') ||
        (pFile->current[1]  != '-') ||
        (pFile->current[2]  != '-') ||
        (pFile->current[3]  != ' ') ||
        (pFile->current[4]  != 'E') ||
        (pFile->current[5]  != 'n') ||
        (pFile->current[6]  != 'd') ||
        (pFile->current[7]  != ' ') ||
        (pFile->current[8]  != 'o') ||
        (pFile->current[9]  != 'f') ||
        (pFile->current[10] != ' ') ||
        (pFile->current[11] != 'E') ||
        (pFile->current[12] != 'D') ||
        (pFile->current[13] != 'I') ||
        (pFile->current[14] != 'D') ||
        (pFile->current[15] != ' ') ||
        (pFile->current[16] != 'f') ||
        (pFile->current[17] != 'o') ||
        (pFile->current[18] != 'r') ||
        (pFile->current[19] != ' ')) {
        
        return FALSE;
    }
    
    /* skip past the start */

    pFile->current += 20;
    
    begin = pFile->current;
    
    /* search for the end of the expected text */

    while (((pFile->current - pFile->start) + 5) <= pFile->length) {

        if ((pFile->current[0] == ' ') &&
            (pFile->current[1] == '-') &&
            (pFile->current[2] == '-') &&
            (pFile->current[3] == '-')) {
            
            len = pFile->current - begin;

            /* make sure the name length seems reasonable */

            if ((len > 512) || (len < 1)) {
                return FALSE;
            }
            
            pEdid->name = nvalloc(len + 1);
                
            strncpy(pEdid->name, begin, len);
            pEdid->name[len] = '\0';
            
            return TRUE;
        }

        pFile->current++;
    }

    return FALSE;
    
} // readEdidFooterforLogFile()

/* 
 * read Edid Footer i.e. "EDID Version".
 * this information is used to check whether the .txt file contains
 * any edid information or not
 */
 
static int findEdidfooterforTextFile(FilePtr pFile)
{   
    pFile->current = pFile->start;
    while (((pFile->current - pFile->start) + 12) <= pFile->length) {

        if ((pFile->current[0]  == 'E') &&
            (pFile->current[1]  == 'D') &&
            (pFile->current[2]  == 'I') &&
            (pFile->current[3]  == 'D') && 
            (pFile->current[4]  == ' ') &&
            (pFile->current[5]  == 'V') &&
            (pFile->current[6]  == 'e') &&
            (pFile->current[7]  == 'r') &&
            (pFile->current[8]  == 's') &&
            (pFile->current[9]  == 'i') &&
            (pFile->current[10] == 'o') &&
            (pFile->current[11] == 'n')) {
            
            pFile->current = pFile->start;
            return TRUE;
        }
        pFile->current++;
    }
    
return FALSE;

} // findEdidfooterforTextFile()

/* read the monitor information */

static int readMonitorNameforTextFile(FilePtr pFile, EdidPtr pEdid)
{
    char *begin;
    int len;

    while (((pFile->current - pFile->start) + 12) <= pFile->length) {

        if ((pFile->current[0]  == 'M') &&
            (pFile->current[1]  == 'o') &&
            (pFile->current[2]  == 'n') &&
            (pFile->current[3]  == 'i') &&
            (pFile->current[4]  == 't') &&
            (pFile->current[5]  == 'o') &&
            (pFile->current[6]  == 'r') &&
            (pFile->current[7]  == ' ') &&
            (pFile->current[8]  == 'N') &&
            (pFile->current[9]  == 'a') &&
            (pFile->current[10] == 'm') &&
            (pFile->current[11] == 'e')) {
        
            pFile->current += 12;
            goto next;
        }
     
        pFile->current++;

    }
    return FALSE;

    next:

       /* search for start of the expected text */

       while (pFile->current[0] != ':') pFile->current++;
       pFile->current += 2;

       begin = pFile->current;

       /* search for the end of expected text */
  
       while (((pFile->current - pFile->start) + 2) <= pFile->length) {

           if ((pFile->current[0] == '\r') && (pFile->current[1] == '\n')) {

               len = pFile->current - begin;
               
               if ((len > 512) || (len < 1)) {
                   return FALSE;
               }
   
               pEdid->name = nvalloc(len + 1);

               strncpy(pEdid->name, begin, len);
               pEdid->name[len] = '\0';

               return TRUE;
           }
           pFile->current++;
       }

     return FALSE;

} // readMonitorNameforTextFile() 

/*
 * findFileName() - determine the filename to use for writing out the
 * EDID
 */

static char *findFileName(char *option)
{
    char *tmp;
    struct passwd *pw;
    
    /* if the user gave an option, start by expanding '~' */
    
    if (option) {
        return nvstrdup(tilde_expansion(option));
    }

    /* if we can write to the current directory, then use that */
    
    if (access(".", R_OK|W_OK|X_OK|F_OK) == 0) {
        return nvstrcat("./", EDID_OUTPUT_FILE_NAME, NULL);
    }

    /*
     * otherwise, if we can get the user's home directory, and have
     * access to it, then use it
     */

    tmp = getenv("HOME");
    
    if (!tmp) {
        pw = getpwuid(getuid());
        if (pw) tmp = pw->pw_dir;
    }
    
    if (tmp && (access(tmp, R_OK|W_OK|X_OK|F_OK) == 0)) {
        return nvstrcat(tmp, "/", EDID_OUTPUT_FILE_NAME, NULL);
    }
    
    /* finally, just give them /tmp/edid.bin */
    
    return nvstrcat("/tmp/", EDID_OUTPUT_FILE_NAME, NULL);
    
} // findFileName()



/*
 * writeEdidFile() - write the EDID to file
 */

static int writeEdidFile(EdidPtr pEdid, char *filename)
{
    int fd = -1, ret = FALSE;
    char *dst = (void *) -1;
    char *msg = "?";
    char *working_filename;
    char scratch[64];
    int n;
    
    /*
     * create a unique filename; if the given filename isn't already
     * unique, append ".#" until it is unique.
     *
     * XXX there is a race between checking the existence of the file,
     * here, and opening the file below
     */
    
    n = 0;
    working_filename = nvstrdup(filename);
    
    while (access(working_filename, F_OK) == 0) {
        snprintf(scratch, 64, "%d", n++);
        nvfree(working_filename);
        working_filename = nvstrcat(filename, ".", scratch, NULL);
    }

    /* open the file */
    
    fd = open(working_filename, O_RDWR | O_CREAT | O_TRUNC,
              S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    
    if (fd == -1) {
        msg = "Unable to open file for writing";
        goto done;
    }
    
    /* set the size of the file */

    if (lseek(fd, pEdid->size - 1, SEEK_SET) == -1) {
        msg = "Unable to set file size";
        goto done;
    }
    
    if (write(fd, "", 1) != 1) {
        msg = "Unable to write output file size";
        goto done;
    }
    
    /* mmap the file */

    if ((dst = mmap(0, pEdid->size, PROT_READ | PROT_WRITE,
                    MAP_SHARED, fd, 0)) == (void *) -1) {
        msg = "Unable to map file for copying";
        goto done;
    }
    
    /* copy the data into the file */

    memcpy(dst, pEdid->bytes, pEdid->size);
    
    /* record success and fall through into done */
    
    ret = TRUE;
    
 done:

    /* unmap the file */
    
    if (dst != (void *) -1) {
        if (munmap(dst, pEdid->size) != 0) {
            msg = "Unable to unmap file";
            ret = FALSE;
        }
    }

    /* close the file */
    
    if (fd != -1) {
        if (close(fd) != 0) {
            msg = "Unable to close file";
            ret = FALSE;
        }
    }
    
    /* report what happened */

    if (ret) {
        fmtout("  Wrote EDID for \"%s\" to \"%s\" (%d bytes).",
               pEdid->name, working_filename, pEdid->size);
    } else {
        fmterr("Failed to write EDID for \"%s\" to \"%s\" (%s)",
               pEdid->name, working_filename, msg);
    }
    
    nvfree(working_filename);
    
    return ret;
    
} /* writeEdidFile() */



/*
 * freeEdid() - free the EDID data structure
 */

static void freeEdid(EdidPtr pEdid)
{
    if (pEdid->bytes) nvfree(pEdid->bytes);
    if (pEdid->name) nvfree(pEdid->name);
    
    nvfree(pEdid);
    
} /* freeEdid() */
