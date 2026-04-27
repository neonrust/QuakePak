// requires C11 (or C++11)

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <assert.h>

#ifdef _WIN64
#pragma comment(lib,"bufferoverflowu")
#else
#include <linux/limits.h>
#include <sys/stat.h>
#define __cdecl

#define _MAX_DRIVE 2
#define _MAX_DIR PATH_MAX
#define _MAX_FNAME PATH_MAX
#define _MAX_EXT PATH_MAX
void _splitpath(char *path, char *drive, char *dir, char *fname, char *ext)
{
	*drive = *path == '/'? '/': '\0';  // but actually n/a in linux
	drive[1] = '\0';
	char *last_sep = strrchr(path, '/');
	if(last_sep == NULL)
		*dir = '\0';
	else
	{
		int dir_len = last_sep - path;
 		strncpy(dir, path, dir_len);
   		dir[dir_len] = '\0';
	}
	char *dot = strrchr(path, '.');
	if(dot == NULL)
	{
		*ext = '\0';
		strcpy(fname, last_sep + 1);
	}
	else
	{
		strcpy(ext, dot);
		int name_len = dot - last_sep - 1;
		strncpy(fname, last_sep + 1, name_len);
		fname[name_len] = '\0';
	}
}
int _mkdir(char *dir) { return mkdir(dir, 0775); }
#endif

#define PATTERN_VALID  0 // valid pattern
#define PATTERN_ESC   -1 // literal escape at end of pattern
#define PATTERN_RANGE -2 // malformed range in [..] construct
#define PATTERN_CLOSE -3 // no end bracket in [..] construct
#define PATTERN_EMPTY -4 // [..] construct is empty

#define MATCH_VALID    1 // valid match
#define MATCH_END      2 // premature end of pattern string
#define MATCH_ABORT    3 // premature end of text string
#define MATCH_RANGE    4 // match failure on [..] construct
#define MATCH_LITERAL  5 // match failure on literal match
#define MATCH_PATTERN  6 // bad pattern

int is_pattern (char *p)
{
  // While character is not NULL
  while(*p)
  {
    // Compare character
    switch(*p++)
    {
      // Is wildcard?
      case '?':
      case '*':
      case '[':
      case '\\':
        // Return success
        return 1;
    }
  }
  // Return Failed
  return 0;
}

int is_valid_pattern (char *p, int *error_type)
{
  // p
  //   Pattern string
  // error_type
  //   PATTERN_VALID - pattern is well formed
  //   PATTERN_ESC   - pattern has invalid escape ('\' at end of pattern)
  //   PATTERN_RANGE - [..] construct has a no end range in a '-' pair (ie [a-])
  //   PATTERN_CLOSE - [..] construct has no end bracket (ie [abc-g )
  //   PATTERN_EMPTY - [..] construct is empty (ie [])
  // return
  //   1          - pattern is well formed
  //   0         - pattern is malformed

  // init error_type
  *error_type = PATTERN_VALID;

  // Until end of string, loop through pattern
  while(*p)
  {
    // Compare character, determining pattern type
    switch(*p)
    {
      // Check literal escape, it cannot be at end of pattern
      case '\\':
        if (*++p == '\0')
        {
          *error_type = PATTERN_ESC;
          return 0;
        }
        p++;
        break;
      // the [..] construct must be well formed
      case '[':
        p++;
        // If the next character is ']' then bad pattern
        if(*p == ']')
        {
          *error_type = PATTERN_EMPTY;
          return 0;
        }
        /* if end of pattern here then bad pattern */
        if(*p == '\0')
        {
          *error_type = PATTERN_CLOSE;
          return 0;
        }
        // Loop to end of [..] construct
        while(*p != ']')
        {
          // Check for literal escape
          if(*p == '\\')
          {
            p++;
            // if end of pattern here then bad pattern
            if(*p++ == '\0')
            {
              *error_type = PATTERN_ESC;
              return 0;
            }
          }
          else p++;
          // if end of pattern here then bad pattern
          if(*p == '\0')
          {
            *error_type = PATTERN_CLOSE;
            return 0;
          }
          // if this a range
          if(*p == '-')
          {
            // We must have an end of range
            if(*++p == '\0' || *p == ']')
            {
              *error_type = PATTERN_RANGE;
              return 0;
            }
            else
            {
              // Check for literal escape
              if(*p == '\\')
                p++;
              // if end of pattern here then bad pattern
              if (*p++ == '\0')
              {
                *error_type = PATTERN_ESC;
                return 0;
              }
            }
          }
        }
        break;

      // All other characters are valid pattern elements
      case '*':
      case '?':
      // "Normal" character
      default:
        p++;
        break;
    }
  }
  // Success
  return 1;
}

/*----------------------------------------------------------------------------
*
*  Match the pattern PATTERN against the string TEXT;
*
*  returns MATCH_VALID if pattern matches, or an errorcode as follows
*  otherwise:
*
*    MATCH_PATTERN  - bad pattern
*    MATCH_LITERAL  - match failure on literal mismatch
*    MATCH_RANGE    - match failure on [..] construct
*    MATCH_ABORT    - premature end of text string
*    MATCH_END  - premature end of pattern string
*    MATCH_VALID    - valid match
*
*
*  A match means the entire string TEXT is used up in matching.
*
*  In the pattern string:
*   `*' matches any sequence of characters (zero or more)
*   `?' matches any character
*   [SET] matches any character in the specified set,
*   [!SET] or [^SET] matches any character not in the specified set.
*
*  A set is composed of characters or ranges; a range looks like
*  character hyphen character (as in 0-9 or A-Z).  [0-9a-zA-Z_] is the
*  minimal set of characters allowed in the [..] pattern construct.
*  Other characters are allowed (ie. 8 bit characters) if your system
*  will support them.
*
*  To suppress the special syntactic significance of any of `[]*?!^-\',
*  and match the character exactly, precede it with a `\'.
*
----------------------------------------------------------------------------*/

int matche_after_star (char *pattern, char *text);

int matche (char *p, char *t)
{
  char range_start, range_end;  /* start and end in range */

  char invert;     /* is this [..] or [!..] */
  char member_match;   /* have I matched the [..] construct? */
  char loop;       /* should I terminate? */

  for ( ; *p; p++, t++)
  {
    /* if this is the end of the text
       then this is the end of the match */

    if (!*t)
    {
      return ( *p == '*' && *++p == '\0' ) ?
        MATCH_VALID : MATCH_ABORT;
    }

    /* determine and react to pattern type */

    switch (*p)
    {
    case '?':         /* single any character match */
      break;

    case '*':         /* multiple any character match */
      return matche_after_star (p, t);

    /* [..] construct, single member/exclusion character match */
    case '[':
    {
      /* move to beginning of range */

      p++;

      /* check if this is a member match or exclusion match */

      invert = 0;
      if (*p == '!' || *p == '^')
      {
        invert = 1;
        p++;
      }

      /* if closing bracket here or at range start then we have a
        malformed pattern */

      if (*p == ']')
      {
        return MATCH_PATTERN;
      }

      member_match = 0;
      loop = 1;

      while (loop)
      {
        /* if end of construct then loop is done */

        if (*p == ']')
        {
          loop = 0;
          continue;
        }

        /* matching a '!', '^', '-', '\' or a ']' */

        if (*p == '\\')
        {
          range_start = range_end = *++p;
        }
        else  range_start = range_end = *p;

        /* if end of pattern then bad pattern (Missing ']') */

        if (!*p)
          return MATCH_PATTERN;

        /* check for range bar */
        if (*++p == '-')
        {
          /* get the range end */

          range_end = *++p;

          /* if end of pattern or construct
             then bad pattern */

          if (range_end == '\0' || range_end == ']')
            return MATCH_PATTERN;

          /* special character range end */
          if (range_end == '\\')
          {
            range_end = *++p;

            /* if end of text then
               we have a bad pattern */
            if (!range_end)
              return MATCH_PATTERN;
          }

          /* move just beyond this range */
          p++;
        }

        /* if the text character is in range then match found.
           make sure the range letters have the proper
           relationship to one another before comparison */

        if (range_start < range_end)
        {
          if (*t >= range_start && *t <= range_end)
          {
            member_match = 1;
            loop = 0;
          }
        }
        else
        {
          if (*t >= range_end && *t <= range_start)
          {
            member_match = 1;
            loop = 0;
          }
        }
      }

      /* if there was a match in an exclusion set then no match */
      /* if there was no match in a member set then no match */

      if ((invert && member_match) || !(invert || member_match))
        return MATCH_RANGE;

      /* if this is not an exclusion then skip the rest of
         the [...] construct that already matched. */

      if (member_match)
      {
        while (*p != ']')
        {
          /* bad pattern (Missing ']') */
          if (!*p)
            return MATCH_PATTERN;

          /* skip exact match */
          if (*p == '\\')
          {
            p++;

            /* if end of text then
               we have a bad pattern */

            if (!*p)
              return MATCH_PATTERN;
          }

          /* move to next pattern char */

          p++;
        }
      }
      break;
    }
    case '\\':  /* next character is quoted and must match exactly */

      /* move pattern pointer to quoted char and fall through */

      p++;

      /* if end of text then we have a bad pattern */

      if (!*p)
        return MATCH_PATTERN;

      /* must match this character exactly */

    default:
      if (*p != *t)
        return MATCH_LITERAL;
    }
  }
  /* if end of text not reached then the pattern fails */

  if (*t)
    return MATCH_END;
  else  return MATCH_VALID;
}

/*----------------------------------------------------------------------------
*
* recursively call matche() with final segment of PATTERN and of TEXT.
*
----------------------------------------------------------------------------*/

int matche_after_star (char *p, char *t)
{
  int match = 0;
  char nextp;

  /* pass over existing ? and * in pattern */

  while ( *p == '?' || *p == '*' )
  {
    /* take one char for each ? and + */

    if (*p == '?')
    {
      /* if end of text then no match */
      if (!*t++)
        return MATCH_ABORT;
    }

    /* move to next char in pattern */

    p++;
  }

  /* if end of pattern we have matched regardless of text left */

  if (!*p)
    return MATCH_VALID;

  /* get the next character to match which must be a literal or '[' */

  nextp = *p;
  if (nextp == '\\')
  {
    nextp = p[1];

    /* if end of text then we have a bad pattern */

    if (!nextp)
      return MATCH_PATTERN;
  }

  /* Continue until we run out of text or definite result seen */

  do
  {
    /* a precondition for matching is that the next character
       in the pattern match the next character in the text or that
       the next pattern char is the beginning of a range.  Increment
       text pointer as we go here */

    if (nextp == *t || nextp == '[')
      match = matche(p, t);

    /* if the end of text is reached then no match */

    if (!*t++)
      match = MATCH_ABORT;

  } while ( match != MATCH_VALID &&
        match != MATCH_ABORT &&
        match != MATCH_PATTERN);

  /* return result */

  return match;
}


/*----------------------------------------------------------------------------
*
* match() is a shell to matche() to return only char values.
*
----------------------------------------------------------------------------*/

char match( char *p, char *t )
{
  int error_type;

  error_type = matche(p,t);
  return (error_type == MATCH_VALID ) ? 1 : 0;
}

#pragma pack(push, 1)
  struct header
  {
    uint32_t magic;
    uint32_t entriesloc;
    uint32_t entriesnum;
  };
  static_assert(sizeof(struct header) == 12);
  struct entry
  {
    char     name[56];
    uint32_t position;
    uint32_t size;
  };
#pragma pack(pop)

FILE *openpakfile(char *pakname)
{
  printf("Opening pack file %s... ", pakname);

  FILE *pak = fopen(pakname, "rb");

  if(pak == NULL)
    printf("open failed! %s.\n", strerror(errno));
  else
    printf("success!\n");

  return pak;
}

int verifyheader(struct header *header, FILE *pak)
{
  printf("Reading %zu byte header... ", sizeof(struct header));

  if(fread(header, sizeof(struct header), 1, pak) == 0 || ferror(pak) != 0)
  {
    printf("read failed! %s.\n", errno ? strerror(errno) : "Not enough bytes read");
    return 0;
  }

  if(header->magic != 0x4B434150)
  {
    printf("File is not a valid PACK file (0x%08x).\n", header->magic);
    return 0;
  }

  if(fseek(pak, header->entriesloc, SEEK_SET) != 0)
  {
    printf("Seek to directory @%u failed.\n", header->entriesloc);
    return 0;
  }

  printf("success! %zu files; directory @%u.\n", header->entriesnum / sizeof(struct entry), header->entriesloc);

  return header->entriesnum / sizeof(struct entry);
}

struct entry *getpakentries(struct header *header, FILE *pak)
{
  printf("Retreiving directory... ");

  if(fseek(pak, header->entriesloc, SEEK_SET) != 0)
  {
    printf("Seek to directory @%u failed.\n", header->entriesloc);
    return NULL;
  }

  struct entry *entries = (struct entry *)calloc(sizeof(struct entry), header->entriesnum);

  if(entries == NULL)
  {
    printf("Error creating %u entry structures.\n", header->entriesnum);
    return NULL;
  }

  size_t numitems = header->entriesnum / sizeof(struct entry);

  size_t items = fread(entries, sizeof(struct entry), numitems, pak);

  if(items != numitems || ferror(pak))
  {
    printf("Read only %zu of %zu items, %s!\n", items, numitems, errno ? strerror(errno) : "Not enough bytes read");
    free(entries);
    return NULL;
  }

  printf("success!\n");
  return entries;
}

void buildpathstructure(char *name)
{
  char drive[_MAX_DRIVE];
  char dir[_MAX_DIR];
  char file[_MAX_FNAME];
  char ext[_MAX_EXT];
  char path[PATH_MAX];

  memset(path, 0, sizeof(path));

  _splitpath(name, drive, dir, file, ext);

  if(!*dir) return;

  char *sep = "\\/";
  char *ptr;

  for(ptr = strtok(dir, sep); ptr; ptr = strtok(NULL, sep))
  {
    if(*path) strcat(path, "/");
    strcat(path, ptr);

    if(_mkdir(path) != -1)
      printf("Created directory %s.\n", path);
  }
}

int extract(char *pakname, char **files)
{
  FILE *pak = openpakfile(pakname);
  if(pak == NULL) return 1;
  struct header header;
  int numitems = verifyheader(&header, pak);
  if(numitems == 0) return 2;
  struct entry *entries = getpakentries(&header, pak);
  if(entries == NULL) return 3;

  int index;
  struct entry *entryptr;
  char **filesptr;
  int numextracted = 0, numerrors = 0;
  FILE *out;
  void *buffer;
  for(filesptr = files; *filesptr; ++filesptr)
    for(index = 0, entryptr = entries; index < numitems; ++index, ++entryptr)
    {
      if(!match(*filesptr, entryptr->name))
        continue;

      buildpathstructure(entryptr->name);

      printf("Extracting %s... ", entryptr->name);

      out = fopen(entryptr->name, "wb");
      if(out == NULL)
      {
        printf("create failed! %s.\n", strerror(errno));
        ++numerrors;
        continue;
      }
      buffer = malloc(entryptr->size);
      if(buffer == NULL)
      {
        ++numerrors;
        printf("alloc %u failed.\n", entryptr->position);
      }
      else if(fseek(pak, entryptr->position, SEEK_SET) != 0)
      {
        printf("seek @%u failed in pak! %s.\n", entryptr->position, strerror(errno));
        ++numerrors;
      }
      else if(entryptr->size > 0 && fread(buffer, entryptr->size, 1, pak) != 1)
      {
        printf("read pak failed! %s.\n", strerror(errno));
        ++numerrors;
      }
      else if(entryptr->size > 0 && fwrite(buffer, entryptr->size, 1, out) != 1)
      {
        printf("write failed! %s.\n", strerror(errno));
        ++numerrors;
      }
      else
      {
        printf("%u bytes, OK!\n", entryptr->size);
        ++numextracted;
      }
      free(buffer);
      fclose(out);
    }
  printf("A total of %u files were extracted.\n", numextracted);
  if(numerrors != 0) printf("A total of %u errors occured.\n", numerrors);

  free(entries);
  fclose(pak);

  return 0;
}

int list(char *filelistname, char *pakname, char **files)
{
  FILE *pak = openpakfile(pakname);
  if(pak == NULL) return 4;
  struct header header;
  int numitems = verifyheader(&header, pak);
  if(numitems == 0) return 5;
  struct entry *entries = getpakentries(&header, pak);
  if(entries == NULL) return 6;
  fclose(pak);

  printf("Creating list file %s... ", filelistname);
  pak = fopen(filelistname, "wt");
  if(pak == NULL)
  {
    printf("create failed! %s.\n", strerror(errno));
    return 7;
  }
  printf("success!\nWriting entries from pak file... ");

  int index;
  struct entry *entryptr;
  char **filesptr;
  int numwritten = 0;
  for(filesptr = files; *filesptr; ++filesptr)
    for(index = 0, entryptr = entries; index < numitems; ++index, ++entryptr)
    {
      if(!match(*filesptr, entryptr->name))
        continue;
      fprintf(pak, "%s\n", entryptr->name);
      ++numwritten;
    }
  fclose(pak);
  printf("success!\n\nA total of %u files written to list.\n", numwritten);

  free(entries);

  return 0;
}

int view(char *pakname, char **files)
{
  FILE *pak = openpakfile(pakname);
  if(pak == NULL) return 4;
  struct header header;
  int numitems = verifyheader(&header, pak);
  if(numitems == 0) return 5;
  struct entry *entries = getpakentries(&header, pak);
  if(entries == NULL) return 6;
  fclose(pak);

  printf("\nId#      Length   Position Filename\n");
  int index, numlisted = 0;
  struct entry *entryptr;
  char **filesptr;
  for(filesptr = files; *filesptr; ++filesptr)
    for(index = 0, entryptr = entries; index < numitems; ++index, ++entryptr)
    {
      if(!match(*filesptr, entryptr->name))
        continue;

      printf("%-4u %10u %10u %s\n", index + 1, entryptr->size, entryptr->position, entryptr->name);

      ++numlisted;
   }
  printf("\nA total of %u files listed.\n", numlisted);

  free(entries);

  return 0;
}

int pack(char *filelistname, char *pakname)
{
  printf("Opening list file %s... ", filelistname);
  FILE *filelist = fopen(filelistname, "rt");
  if(filelist == NULL)
  {
    printf("open failed! %s.\n", strerror(errno));
    return 1;
  }
  printf("success!\n");

  struct header header;
  memset(&header, 0, sizeof(header));
  header.entriesloc = sizeof(header);

  struct entry **entries = NULL;

  char buffer[1024];
  char *bufferptr;
  FILE *entry;
  while(!feof(filelist))
  {
    *buffer = '\0';
    fgets(buffer, sizeof(buffer), filelist);
    for(bufferptr = buffer; *bufferptr; ++bufferptr)
    {
      if(*bufferptr == '\\')
      {
        *bufferptr = '/';
        continue;
      }
      if((unsigned char)*bufferptr >= 32)
        continue;
      *bufferptr = 0;
      break;
    }
    if(*buffer == 0) continue;
    if(strlen(buffer) > 55)
    {
      printf("Warning! Path '%s' too long (>55). Skipped!\n", buffer);
      continue;
    }
    if((long)(header.entriesloc + (header.entriesnum * sizeof(struct entry))) < 0)
    {
      printf("Can't process '%s' 2 gigabyte limit exceeded.\n", buffer);
      return 2;
    }
    if(header.entriesloc < 0)
    {
      printf("Can't process '%s'! 2 million entries limit exceeded.\n", buffer);
      return 3;
    }
    entries = (struct entry**)realloc(entries, (header.entriesnum + 1) * sizeof(void*));
    if(entries == NULL)
    {
      printf("Error %u allocating file entry address space, %s.\n", errno, strerror(errno));
      return 4;
    }
    entries[header.entriesnum] = (struct entry*)malloc(sizeof(struct entry));
    memset(entries[header.entriesnum], 0, sizeof(struct entry));
    if(entries[header.entriesnum] == NULL)
    {
      printf("Error %u allocating file entry structure, %s.\n", errno, strerror(errno));
      return 5;
    }
    strncpy(entries[header.entriesnum]->name, buffer, 55);
    printf("Processing %s... ", entries[header.entriesnum]->name);
    entry = fopen(entries[header.entriesnum]->name, "rb");
    if(entry == NULL)
    {
      printf("open failed! %s.\n", strerror(errno));
      return 7;
    }
    if(fseek(entry, 0, SEEK_END) != 0)
    {
      printf("fseek failed! %s.\n", strerror(errno));
      return 8;
    }
    entries[header.entriesnum]->size = ftell(entry);
    entries[header.entriesnum]->position = header.entriesloc;
    header.entriesloc += entries[header.entriesnum]->size;
    if(fclose(entry) != 0)
    {
      printf("fclose failed! %s.\n", strerror(errno));
      return 9;
    }
    printf("%ub @%u.\n", entries[header.entriesnum]->size, entries[header.entriesnum]->position);
    ++header.entriesnum;
  }
  if(header.entriesnum == 0)
  {
    printf("No files were parsed so theres no point creating a package.\n");
    return 10;
  }
  printf("Parse of %u files from filelist completed.\n", header.entriesnum);
  header.entriesloc = entries[header.entriesnum - 1]->position + entries[header.entriesnum - 1]->size;
  fclose(filelist);
  printf("Creating pack file %s... ", pakname);
  FILE *pak = fopen(pakname, "wb");
  if(pak == NULL)
  {
    printf("create failed! %s.\n", strerror(errno));
    return 11;
  }
  header.magic = 0x4B434150;
  header.entriesnum *= sizeof(struct entry);
  printf("success!\nEntry table is %ub @%u.\nWriting initial header data... ", header.entriesnum, header.entriesloc);
  if(fwrite(&header, sizeof(header), 1, pak) != 1 || ferror(pak) != 0)
  {
    printf("write failed! %s.\n", strerror(errno));
    return 12;
  }
  header.entriesnum /= sizeof(struct entry);
  long index;
  char *databuffer;
  printf("success!\n");
  for(index = 0; index < header.entriesnum; ++index)
  {
    printf("Packing %lu/%u; %s (%ub @%u)... ", index + 1,  header.entriesnum, entries[index]->name, entries[index]->size, entries[index]->position);
    if(entries[index]->size == 0)
    {
      printf("skipped.\n");
      continue;
    }
    entry = fopen(entries[index]->name, "rb");
    if(entry == NULL)
    {
      printf("open failed! %s.\n", strerror(errno));
      return 13;
    }
    databuffer = (char*)malloc(entries[index]->size);
    if(databuffer == NULL)
    {
      printf("malloc failed! %s.\n", strerror(errno));
      return 14;
    }
    if(fread(databuffer, entries[index]->size, 1, entry) != 1 || ferror(entry) != 0)
    {
      printf("read failed! %s.\n", strerror(errno));
      return 15;
    }
    if(fwrite(databuffer, entries[index]->size, 1, pak) != 1 || ferror(pak) != 0)
    {
      printf("write failed! %s.\n", strerror(errno));
      return 16;
    }
    free(databuffer);
    if(fclose(entry) != 0)
    {
      printf("close failed! %s.\n", strerror(errno));
      return 17;
    }
    printf("ok.\n");
  }
  printf("Writing entry structures... ");
  for(index = 0; index < header.entriesnum; ++index)
  {
    if(fwrite(entries[index], sizeof(struct entry), 1, pak) != 1)
    {
      printf("write failed! %s.\n", strerror(errno));
      return 18;
    }
    free(entries[index]);
  }
  long paksize = ftell(pak);
  if(fclose(pak) != 0)
  {
    printf("close failed! %s.\n", strerror(errno));
    return 19;
  }
  printf("success!\nPackage %s is %lu bytes containing %u files.\n", pakname, paksize, header.entriesnum);
  free(entries);

  return 0;
}

int __cdecl main(int argc, char **argv)
{
  short version = 0x0101;
  char *date = __DATE__;

  printf("QPak%zu %u.%u; %s; Quake Package Utility.\n\n",
    sizeof(void*) * 8, version & 0x00FF, (version & 0xFF00) >> 8, date);

  if(argc == 1)
  {
    printf("Usage: QPAK e [pak] [file1[file2[...]]] | l [list] [pak] [file1[file2[...]]]\n"
           "            p [list] [pak] | v [pak] [file1[file2[...]]]\n"
           "\n"
           "Commands:\n"
           "\te[xtract]\tExtract the specified [file]('s) from [pak].\n"
           "\tl[ist]\t\tWrite a list to [list] containing filenames in [pak].\n"
           "\tp[ack]\t\tRead the specified [list] and pack files into [pak].\n"
           "\tv[iew]\t\tView files in [pak] matching specified [file]('s).\n"
           "\n"
           "Optional:\n"
           "\t[list]\t\tIf not specified the default of qpak.txt is used.\n"
           "\t[pak]\t\tIf not specified the default of qpak.pak is used.\n"
           "\t[file]\t\tIf not specified the default of [*.*] (all) is used.\n"
           "\t* and ?\t\tMinimal REGEX is supported for the [file] parameter.\n");
    return 1;
  }

  char **argvptr, *pakfilename = NULL, *listfilename = NULL, **files = NULL;

  enum command { NOTHING, EXTRACT, LIST, PACK, VIEW } command = NOTHING;

  enum state { WANTCOMMAND, WANTLISTFILENAME, WANTPAKFILENAME,
    WANTFILES, WANTMOREFILES, WANTNOTHING } state = WANTCOMMAND;

  for(argvptr = argv + 1; *argvptr; ++argvptr)
  {
    switch(state)
    {
      case WANTCOMMAND:
        switch(tolower(**argvptr))
        {
          case 'e':
            command = EXTRACT;
            state = WANTPAKFILENAME;
            break;
          case 'l':
            command = LIST;
            state = WANTLISTFILENAME;
            break;
          case 'p':
            command = PACK;
            state = WANTLISTFILENAME;
            break;
          case 'v':
            command = VIEW;
            state = WANTPAKFILENAME;
            break;
          default:
            printf("Unknown command '%s' specified.\n", *argvptr);
            return 2;
        }
        break;
      case WANTLISTFILENAME:
        listfilename = *argvptr;
        state = WANTPAKFILENAME;
        break;
      case WANTPAKFILENAME:
        pakfilename = *argvptr;
        switch(command)
        {
          case VIEW:
          case EXTRACT:
          case LIST:
            state = WANTFILES;
            break;
          case PACK:
            state = WANTNOTHING;
            break;
          default:
            printf("Internal error: Unknown command enum '%d' sent to WANTPAKFILENAME.\n", command);
            return 3;
        }
        break;
      case WANTFILES:
        files = argvptr;
        state = WANTMOREFILES;
        break;
      case WANTMOREFILES:
        break;
      case WANTNOTHING:
        printf("Too many parameters '%s' specified.\n", *argvptr);
        break;
      default:
        printf("Internal error: Unknown state enum '%d' in argument '%s'.\n", command, *argvptr);
        return 4;
    }
  }

  if(listfilename == NULL) listfilename = "qpak.txt";

  if(pakfilename == NULL) pakfilename = "qpak.pak";

  if(files == NULL)
  {
    files = calloc(sizeof(char*), 2);
    memset(files, 0, sizeof(char*[2]));
    *files = "*";
  }

  switch(command)
  {
    case NOTHING:
      printf("Internal error: No command specified.\n");
      return 5;
    case LIST:
      return list(listfilename, pakfilename, files);
    case EXTRACT:
      return extract(pakfilename, files);
    case PACK:
      return pack(listfilename, pakfilename);
    case VIEW:
      return view(pakfilename, files);
  }

  printf("Internal error: Unknown command enum '%d' sent to launch.\n", command);
  return 6;
}
