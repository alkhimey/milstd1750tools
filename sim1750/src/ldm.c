/***************************************************************************/
/*                                                                         */
/* Project   :        sim1750 -- Mil-Std-1750 Software Simulator           */
/*                                                                         */
/* Component :       ldm.c -- ASCII Load Module loading functions          */
/*                                                                         */
/* Copyright :         (C) Daimler-Benz Aerospace AG, 1994-97              */
/*                                                                         */
/* Author    :      Oliver M. Kellogg, Dornier Satellite Systems,          */
/*                     Dept. RST13, D-81663 Munich, Germany.               */
/* Contact   :            oliver.kellogg@space.otn.dasa.de                 */
/*                                                                         */
/* Disclaimer:                                                             */
/*                                                                         */
/*  This program is free software; you can redistribute it and/or modify   */
/*  it under the terms of the GNU General Public License as published by   */
/*  the Free Software Foundation; either version 2 of the License, or      */
/*  (at your option) any later version.                                    */
/*                                                                         */
/*  This program is distributed in the hope that it will be useful,        */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of         */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          */
/*  GNU General Public License for more details.                           */
/*                                                                         */
/*  You should have received a copy of the GNU General Public License      */
/*  along with this program; if not, write to the Free Software            */
/*  Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.   */
/*                                                                         */
/***************************************************************************/


#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "status.h"
#include "peekpoke.h"
#include "arch.h"
#include "utils.h"
#include "loadfile.h"


static int linecnt;

#define TLDADDR    2
#define XTCADDR    3
#define COUNT      7
#define CHECKSUM   8
#define DATASTART 12

#define get_word(str)   get_nibbles(str,4)

static int
rotl16 (int num, int n_shifts)	/* Rotate-left a 16 bit word */
{
  ulong buf;

  if (n_shifts < 0 || n_shifts > 15)
    return error ("ldm: rotl16 called with illegal n_shifts");
  buf = ((ulong) num & 0xFFFFL) << n_shifts;
  buf = (buf & 0xFFFFL) | (buf >> 16);
  return (int) buf;
}

/* Compute the checksum for a line from a TLD Load Module file */

static int
check_tldline (char *line)
{
  int i, code, datacnt;
  bool address_field_used = TRUE, cmd_m_c_t = FALSE;

  if (line[0] != '/')
    return error ("ldm: illegal start character for LDM line");

  switch (line[1])
    {
    case 'M':
      cmd_m_c_t = TRUE;		/* slides through to following case */
    case 'I':
    case 'O':
      code = 0x9;
      break;
    case 'N':
      code = 0xA;
      break;
    case 'L':
      code = 0xA + (*(line + 5) - '0');  /* Instruction => 0xA, Oprnd => 0xB */
      break;
    case 'Q':
      code = 0xB;
      break;
    case 'A':
      address_field_used = FALSE;
      code = 0x6;
      break;
    case 'T':
      code = 0x6;
      cmd_m_c_t = TRUE;
      break;
    case 'Z':
      address_field_used = FALSE;	/* slides through to following case */
    case 'G':
    case 'H':
      code = 0x8;
      break;
    case 'C':
      cmd_m_c_t = TRUE;		/* slides through to following case */
    case 'B':
    case 'P':
      code = 0xE;
      break;
    default:
      return error ("illegal Command type in TLD LDM line");
    }
  code <<= 1;
  if (address_field_used)
    {
      int addr_hinibble = xtoi (line[TLDADDR]);
      long addr16 = get_word (line + TLDADDR + 1);
      if (addr16 == -1L)
	return error ("ldm: illegal char in address field");
      code ^= (int) addr16;
      if (addr_hinibble == -1)
	return error ("ldm: illegal char in MS-nibble of address");
      if (cmd_m_c_t)
	code = rotl16 (code, 1) ^ addr_hinibble;
    }
  if ((datacnt = xtoi (line[COUNT])) == -1)
    return error ("ldm: illegal character in data count");
  for (i = 0; i < datacnt; i++)
    {
      long dataword = get_word (line + DATASTART + (4 * i));
      if (dataword == -1L)
	return error ("ldm: illegal char in data word");
      code = rotl16 (code, 1) ^ (int) dataword;
    }

  return code;
}

/* Analyze and load a line from a TLD Load Module file */

static int
load_tldline (char *line)
{
  int cmd, actual_chksum, datacnt;
  long claimed_chksum, address;

  if (line[0] != '/')
    return error ("TLD LDM: illegal format at line %d", linecnt);
  cmd = line[1];
  if (cmd == ';' || cmd == 'E' || cmd == 'H' || cmd == 'Z')
    return OKAY;
  if ((claimed_chksum = get_word (line + CHECKSUM)) == -1L)
    return error ("numeric syntax error in checksum at TLD LDM line %d",
		  linecnt);
  actual_chksum = check_tldline (line);
  if ((int) claimed_chksum != actual_chksum)
    warning ("incorrect checksum in TLD LDM line %d (computed: %04Xh)",
	     linecnt, actual_chksum);
  switch (cmd)
    {
    case 'A':
      simreg.sw &= 0xFFF0;
      simreg.sw |= (ushort) get_word (line + DATASTART) & 0x000F;
      break;
    case 'I':
    case 'O':
      {
	int i, as, logaddr_hinibble, bank = (cmd == 'I') ? CODE : DATA;
	if ((as = xtoi (line[TLDADDR])) == -1)
	  return error ("line %d: /%c error in load address state",
			linecnt, cmd);
	if ((logaddr_hinibble = xtoi (line[TLDADDR + 1])) == -1)
	  return error ("line %d: /%c error in logical address MS-nibble",
			linecnt, cmd);
	if ((address = get_nibbles (line + TLDADDR + 2, 3)) == -1L)
	  return error ("line %d: /%c error in logical address",
			linecnt, cmd);
	address |= (long) pagereg[bank][as][logaddr_hinibble].ppa << 12;
	if ((datacnt = xtoi (line[COUNT])) == -1)
	  return error ("line %d: /%c error in data count", linecnt, cmd);
	for (i = 0; i < datacnt; i++)
	  {
	    long word = get_word (line + DATASTART + (4 * i));
	    if (word == -1L)
	      return error ("line %d: /%c data error", linecnt, cmd);
	    poke (address++, (ushort) word);
	  }
      }
      break;
    case 'L':
      {
	int i, bank, as, pagereg_number;
	if ((as = xtoi (line[TLDADDR])) == -1)
	  return error ("line %d: /L error in load address state", linecnt);
	if ((bank = xtoi (line[TLDADDR + 3])) == -1)
	  return error ("line %d: /L error in load address bank", linecnt);
	if ((pagereg_number = xtoi (line[TLDADDR + 4])) == -1)
	  return error ("line %d: /L error in pagereg number", linecnt);
	if ((datacnt = xtoi (line[COUNT])) == -1)
	  return error ("line %d: /L error in data count", linecnt);
	for (i = 0; i < datacnt; i++)
	  {
	    const int allocation_type = xtoi (line[DATASTART + (4 * i)]);
	    long pagereg_contents;
	    if (allocation_type == -1)
	      return error ("line %d: /L error in allocation type", linecnt);
	    if (allocation_type != 2)
	      return error ("line %d: /L allocation type %d unimplemented",
			    linecnt, allocation_type);
	    pagereg_contents = get_nibbles (line + DATASTART + (4 * i) + 1, 3);
	    if (pagereg_contents == -1L || pagereg_contents > 0xFFL)
	      return error ("line %d: /L pagereg contents error", linecnt);
	    pagereg[bank][as][pagereg_number].ppa = (ushort) pagereg_contents;
	    if (++pagereg_number > 0xF)
	      break;
	  }
      }
      break;
    case 'M':
      {
	int i, physaddr_hinibble;
	if ((physaddr_hinibble = xtoi (line[TLDADDR])) == -1)
	  return error ("line %d: /M error in load address high nibble", linecnt);
	if ((address = get_word (line + TLDADDR + 1)) == -1L)
	  return error ("line %d: /M error in load address", linecnt);
	address |= physaddr_hinibble << 16;
	if ((datacnt = xtoi (line[COUNT])) == -1)
	  return error ("line %d: /M error in data count", linecnt);
	for (i = 0; i < datacnt; i++)
	  {
	    long word = get_word (line + DATASTART + (4 * i));
	    if (word == -1L)
	      return error ("line %d: /M data error", linecnt);
	    poke (address++, (ushort) word);
	  }
      }
      break;
    case 'N':
    case 'Q':
      {
	int i, bank = (cmd == 'N') ? CODE : DATA;
	if ((address = get_nibbles (line + TLDADDR, 5)) < 0L)
	  return error ("line %d: /%c data error in address field", linecnt, cmd);
	if (address > 0xFFL)
	  return error ("line %d: /%c starting pagereg number too large",
			linecnt, cmd);
	if ((datacnt = xtoi (line[COUNT])) == -1)
	  return error ("line %d: /%c error in data count", linecnt, cmd);
	for (i = 0; i < datacnt; i++)
	  {
	    const int as = (int) address >> 4;
	    const int pagenum = (int) address & 0xF;
	    ushort *entire_pagereg = (ushort *) & pagereg[bank][as][pagenum];
	    long word = get_word (line + DATASTART + (4 * i));
	    if (word == -1L)
	      return error ("line %d: /%c data error", linecnt, cmd);
	    *entire_pagereg = (ushort) word;
	    address++;
	  }
      }
      break;
    case 'T':
      if ((address = get_nibbles (line + TLDADDR, 5)) == -1L)
	return error ("TLD LDM: numeric syntax error in transfer address");
      simreg.ic = (ushort) address;
      if (address > 0xFFFFL)
	return error ("TLD LDM: cannot handle transfer address (too high)");
      break;
    default:
      warning ("TLD LDM: unimplemented command type '%c' (ignored)", cmd);
      break;
    }
  return OKAY;
}


/* Compute the checksum for a line from an XTC Load Module file */

static int
check_xtcline (char *line)
{
  int i, code, datacnt;
  bool address_field_used = TRUE;

  if (line[0] != '/')
    return error ("illegal start character for XTC LDM line");

  switch (line[1])
    {
    case 'I':
      code = 0;
    elsecase 'O':
      code = 1;
    elsecase 'E':
      code = 2;
      address_field_used = FALSE;
    elsecase 'W':
      code = 3;
      address_field_used = FALSE;
    elsecase 'D':
      code = 4;
      address_field_used = FALSE;
    elsecase 'P':
      code = 5;
      address_field_used = FALSE;
    elsecase 'T':
      code = 6;
    elsecase 'C':
      code = 7;
    elsecase 'Z':
      code = 8;
      address_field_used = FALSE;
      break;
    default:
      return error ("illegal record type in XTC LDM line");
    }
  if (address_field_used)
    {
      long addr16 = get_word (line + XTCADDR);
      if (addr16 == -1L)
	return error ("xtcldm: illegal char in address field");
      code = (code << 1) ^ (int) addr16;
    }
  if ((datacnt = xtoi (line[COUNT])) == -1)
    return error ("xtcldm: illegal character in data count");
  for (i = 0; i < datacnt; i++)
    {
      long dataword = get_word (line + DATASTART + (4 * i));
      if (dataword == -1L)
	return error ("xtcldm: illegal char in data word");
      code = rotl16 (code, 1) ^ (int) dataword;
    }

  return code;
}

/* Analyze and load a line from an XTC Load Module file */

static int
load_xtcline (char *line)
{
  int cmd, actual_chksum, datacnt;
  long claimed_chksum, address;

  if (line[0] != '/')
    return error ("XTC LDM: illegal format at line %d", linecnt);
  cmd = line[1];
  if (cmd == 'C' || cmd == 'Z')		/* Comment or end-of-file */
    return OKAY;
  if ((claimed_chksum = get_word (line + CHECKSUM)) == -1L)
    return error ("numeric syntax error in checksum at XTC LDM line %d",
		  linecnt);
  actual_chksum = check_xtcline (line);
  if ((int) claimed_chksum != actual_chksum)
    warning ("incorrect checksum in XTC LDM line %d (computed: %04Xh)",
	     linecnt, actual_chksum);
  switch (cmd)
    {
    case 'D':		/* DMA/Processor write protect (TBD) */
    case 'P':
      break;
    case 'E':		/* Execution/operand-Write protect (TBD) */
    case 'W':
      break;
    case 'I':		/* Instruction/Operand memory */
    case 'O':
      {
        int bank = (cmd == 'I') ? CODE : DATA, as = simreg.sw & 0xF;
	int logaddr_hinibble = xtoi (line[XTCADDR]), i;
	if (logaddr_hinibble == -1)
	  return error ("line %d: /%c error in logical address MS-nibble",
			linecnt, cmd);
	if ((address = get_nibbles (line + XTCADDR + 1, 3)) == -1L)
	  return error ("line %d: /%c error in logical address",
			linecnt, cmd);
	address |= (long) pagereg[bank][as][logaddr_hinibble].ppa << 12;
	if ((datacnt = xtoi (line[COUNT])) == -1)
	  return error ("line %d: /%c error in data count", linecnt, cmd);
	for (i = 0; i < datacnt; i++)
	  {
	    long word = get_word (line + DATASTART + (4 * i));
	    if (word == -1L)
	      return error ("line %d: /%c data error", linecnt, cmd);
	    poke (address++, (ushort) word);
	  }
      }
      break;
    case 'T':		/* Transfer address */
      if ((address = get_nibbles (line + XTCADDR, 4)) == -1L)
	return error ("XTC LDM: numeric syntax error in transfer address");
      simreg.ic = (ushort) address;
      break;
    default:
      warning ("XTC LDM: unimplemented command type '%c' (ignored)", cmd);
      break;
    }
  return OKAY;
}


/* load file in TLD or XTC Load Module format */

static int (*load_ldmline[]) (char *) = { load_tldline, load_xtcline };


static int
load_ldm (char *filename)
{
  FILE *loadfile;
  char lline[128];
  bool verbose_save = verbose;
  int retval;

  if ((loadfile = fopen (filename, "r")) == (FILE *) 0)
    {
      strcat (strcpy (lline, filename), ".ldm");
      if ((loadfile = fopen (lline, "r")) == (FILE *) 0)
	return error ("cannot open load file '%s'", filename);
    }
  verbose = FALSE;		/* avoid info messages from poke() */
  linecnt = 0;
  while (fgets (lline, sizeof (lline), loadfile) != NULL)
    {
      ++linecnt;
      if (strlen (lline) < 2)
	continue;
      if ((retval = (*load_ldmline[(int) loadfile_type]) (lline)) != OKAY)
	break;
    }
  fclose (loadfile);
  verbose = verbose_save;
  return retval;
}


int
si_tld (int argc, char *argv[])
{
  char *filename = argv[1];

  if (argc <= 1)
    return error ("filename missing");
  loadfile_type = TLD_LDM;
  if (*filename == '"')
    {
      *filename++ = '\0';
      *(filename + strlen (filename) - 1) = '\0';
    }

  return load_ldm (filename);
}


int
si_xtc (int argc, char *argv[])
{
  char *filename = argv[1];

  if (argc <= 1)
    return error ("filename missing");
  loadfile_type = XTC_LDM;
  if (*filename == '"')
    {
      *filename++ = '\0';
      *(filename + strlen (filename) - 1) = '\0';
    }

  return load_ldm (filename);
}

