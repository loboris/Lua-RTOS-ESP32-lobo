#include "lauxlib.h"

#include <freertos/FreeRTOS.h>

#include <limits.h>

#include <sys/dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/status.h>
#include <sys/console.h>
#include <drivers/cpu.h>
#include <sys/mount.h>
#include "ff.h"
#include "vfs/fat.h"

#include "lua.h"

extern const char *__progname;
extern uint32_t boot_count;
extern uint8_t flash_unique_id[8];

char lua_syslog_level = 0xff;
FILE *lua_stdout_file = NULL;

void luaC_fullgc (lua_State *L, int isemergency) ;
int edit_main(int argc, char *argv[]);

static int os_stdout(lua_State *L) {
    int total = lua_gettop(L);
    const char *path = NULL;

    if (total == 1) {
    	path = luaL_checkstring(L, 1);
    }

    if (path) {
    	if (lua_stdout_file) {
    		fclose(lua_stdout_file);
    	}

    	lua_stdout_file = fopen(path,"a+");
    } else {
    	if (lua_stdout_file) {
    		fclose(lua_stdout_file);
    		lua_stdout_file = NULL;
    	}
    }
    return 0;
}

static int os_shell(lua_State *L) { 
    luaL_checktype(L, 1, LUA_TBOOLEAN);

	if (lua_toboolean( L, 1 )) {
		status_set(STATUS_LUA_SHELL);
	} else {
		status_clear(STATUS_LUA_SHELL);
	}
    
    return 0;
}

#if LUA_USE_EDITOR
static int os_edit (lua_State *L) {
    const char *path = luaL_checkstring(L, 1);

    // Create file if does not exists
    FILE *fp = fopen(path, "a");
    if (!fp) {
        return luaL_fileresult(L, 0, path);
    }
    fclose(fp);
  
    char* lua_argv[] = {(char *)"edit", (char *)path, NULL};
    edit_main(2, lua_argv);
    console_clear();
    return 0;    
}
#endif

static int os_sleep(lua_State *L) {
	unsigned int seconds = luaL_checkinteger(L, 1);

	cpu_sleep(seconds);
	
	return 0;
}

static int os_set_sleep_calib(lua_State *L) {
	float perc = luaL_checknumber(L, 1);
	if (((perc > -1) && (perc < 1)) || ((perc < -20) || (perc > 20))) {

	}
	uint64_t uperc = (uint64_t)(perc * 10000.0);

	_speep_calib = 1000000LL + uperc;
	lua_pushinteger(L, _speep_calib);
	return 1;
}

static int os_reset_reason(lua_State *L) {
	char buf[64];
	int reason = cpu_reset_reasons(buf);

	lua_pushinteger(L, reason);
	lua_pushstring(L, buf);

	return 2;
}

static int os_loglevel(lua_State *L) {   
    int total = lua_gettop(L);
    int mask = 0;
    int flag = 0;
    int i;
    
    for(i=1;i<=total;i++) {
        flag = luaL_checkinteger(L, i);
        if (((flag < 0) || (flag > 7)) && (flag != 0xff)) {
            return luaL_error(L, "invalid flag");        
        }
        
        if (flag == 0xff) {
            mask |= LOG_UPTO(LOG_DEBUG);          
        } else {
            mask |= LOG_UPTO(flag);
        }
    }
    
    setlogmask(mask);

    return 0;    
}

static int more(const char *path, int stop) {
    FILE *fp;
    int rows = 0;
    int cols = 0;
    int c;
    char ch;
    int lines;

    fp = fopen(path,"r");
    if (!fp) {
        return -1;
    }

    if (stop) {
        console_size(&rows, &cols);
        console_clear();

        rows--;
        lines = 0;
    }

    while((c = fgetc(fp)) != EOF) {
        if ((c == '\n') && (stop)) {
            lines++;
            if (lines == rows) {
                console_statusline(path,": any key for next page, q for exit");
                ch = getchar();
                if ((ch == 'q') || (ch == 'Q')) {
                    console_clearstatusline();
                    break;
                }

                lines = 0;
                console_clear();
            }
        }

        printf("%c",c);
    }

    fclose(fp);

    return 0;
}

static int os_cat(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    int res;
    
    if ((res = more(path, 0)) < 0) {
        return luaL_fileresult(L, 0, path);
    }

    return 0;
}

static int os_more(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    int res;
    
    if ((res = more(path, 1)) < 0) {
        return luaL_fileresult(L, 0, path);
    }

    return 0;
}

static int os_dmesg(lua_State *L) {
    int res;    
    const char *fname = NULL;

    if (mount_is_mounted("fat")) {
    	if (mount_is_mounted("spiffs")) {
    		fname = "/sd/log/messages.log";
    	} else {
    		fname = "/log/messages.log";
    	}
    }

    if ((res = more(fname, 1)) < 0) {
        return luaL_fileresult(L, 0, fname);
    }

    return 0;
}

static int os_cd (lua_State *L) {
    const char *path = luaL_optstring(L, 1, "/");
      
    if (chdir(path) < 0) {
        return luaL_fileresult(L, 0, path);
    }

    return 0;
}

static int os_pwd (lua_State *L) {
    char path[PATH_MAX + 1];
    
    if (getcwd(path, PATH_MAX)) {
        lua_pushstring(L, path);
        return 1;
    } else {
        return luaL_fileresult(L, 0, path);
    }
}

static int os_mkdir (lua_State *L) {
    const char *path = luaL_optstring(L, 1, NULL);
    char cpath[PATH_MAX + 1];
    
    // If path is not present get the current directory as path
    if (!path) {
        if (!getcwd(cpath, PATH_MAX)) {
            return luaL_fileresult(L, 0, cpath);
        }    
        
        path = cpath;
    }
    
    return luaL_fileresult(L, mkdir(path, 0) == 0, path);
}

static int os_ls (lua_State *L) {
    const char *path = luaL_optstring(L, 1, NULL);
    DIR *dir = NULL;
    struct dirent *ent;
    char type;
    char size[9];
    char cpath[255];
    char tpath[255];
    char tbuffer[80];
    struct stat sb;
    struct tm *tm_info;
    char *lpath = NULL;
    char *ppath = NULL;
    int statok;
    int on_fat = 0;

    // If path is not present get the current directory as path
    if (!path) {
        if (!getcwd(cpath, 255)) {
            return luaL_fileresult(L, 0, cpath);
        }
        path = cpath;
    }

    lpath = mount_resolve_to_logical(path);
    ppath = mount_resolve_to_physical(lpath);

    if (strstr(ppath, "/fat/") == ppath) on_fat = 1;

    free(ppath);

    // Open directory
    dir = opendir(lpath);
    if (!dir) {
        return luaL_fileresult(L, 0, path);
    }

    // Read directory entries
    while ((ent = readdir(dir)) != NULL) {
    	sprintf(tpath, lpath);
        strcat(tpath,"/");
        strcat(tpath,ent->d_name);
        tbuffer[0] = '\0';

        // Get file stat
        statok = stat(tpath, &sb);

    	if (statok == 0) {
			tm_info = localtime(&sb.st_mtime);
			strftime(tbuffer, 80, "%c", tm_info);
        }

    	type = 'd';
        if (ent->d_type == DT_REG) {
            type = 'f';
            if (statok) strcpy(size, "       ?");
            else {
                sprintf(size,"%8d", (int)sb.st_size);
            }
        //} else if (ent->d_type == DT_LNK) {
        //    type = 'l';
        //    sprintf(size,"%8d", ent->d_fsize);
        } else {
            strcpy(size, "       -");
        }

        printf("%c\t%s\t%s\t%s\n",
            type,
            size,
            tbuffer,
            ent->d_name
        );
    }

    closedir(dir);

    free(lpath);

    return 0;
}

// ============================================================================
#include <ctype.h>

// fnmatch defines
#define	FNM_NOMATCH	1	// Match failed.
#define	FNM_NOESCAPE	0x01	// Disable backslash escaping.
#define	FNM_PATHNAME	0x02	// Slash must be matched by slash.
#define	FNM_PERIOD		0x04	// Period must be matched by period.
#define	FNM_LEADING_DIR	0x08	// Ignore /<tail> after Imatch.
#define	FNM_CASEFOLD	0x10	// Case insensitive search.
#define FNM_PREFIX_DIRS	0x20	// Directory prefixes of pattern match too.
#define	EOS	        '\0'

//-----------------------------------------------------------------------
static const char * rangematch(const char *pattern, char test, int flags)
{
  int negate, ok;
  char c, c2;

  /*
   * A bracket expression starting with an unquoted circumflex
   * character produces unspecified results (IEEE 1003.2-1992,
   * 3.13.2).  This implementation treats it like '!', for
   * consistency with the regular expression syntax.
   * J.T. Conklin (conklin@ngai.kaleida.com)
   */
  if ( (negate = (*pattern == '!' || *pattern == '^')) ) ++pattern;

  if (flags & FNM_CASEFOLD) test = tolower((unsigned char)test);

  for (ok = 0; (c = *pattern++) != ']';) {
    if (c == '\\' && !(flags & FNM_NOESCAPE)) c = *pattern++;
    if (c == EOS) return (NULL);

    if (flags & FNM_CASEFOLD) c = tolower((unsigned char)c);

    if (*pattern == '-' && (c2 = *(pattern+1)) != EOS && c2 != ']') {
      pattern += 2;
      if (c2 == '\\' && !(flags & FNM_NOESCAPE)) c2 = *pattern++;
      if (c2 == EOS) return (NULL);

      if (flags & FNM_CASEFOLD) c2 = tolower((unsigned char)c2);

      if ((unsigned char)c <= (unsigned char)test &&
          (unsigned char)test <= (unsigned char)c2) ok = 1;
    }
    else if (c == test) ok = 1;
  }
  return (ok == negate ? NULL : pattern);
}

//--------------------------------------------------------------------
static int fnmatch(const char *pattern, const char *string, int flags)
{
  const char *stringstart;
  char c, test;

  for (stringstart = string;;)
    switch (c = *pattern++) {
    case EOS:
      if ((flags & FNM_LEADING_DIR) && *string == '/') return (0);
      return (*string == EOS ? 0 : FNM_NOMATCH);
    case '?':
      if (*string == EOS) return (FNM_NOMATCH);
      if (*string == '/' && (flags & FNM_PATHNAME)) return (FNM_NOMATCH);
      if (*string == '.' && (flags & FNM_PERIOD) &&
          (string == stringstart ||
          ((flags & FNM_PATHNAME) && *(string - 1) == '/')))
              return (FNM_NOMATCH);
      ++string;
      break;
    case '*':
      c = *pattern;
      // Collapse multiple stars.
      while (c == '*') c = *++pattern;

      if (*string == '.' && (flags & FNM_PERIOD) &&
          (string == stringstart ||
          ((flags & FNM_PATHNAME) && *(string - 1) == '/')))
              return (FNM_NOMATCH);

      // Optimize for pattern with * at end or before /.
      if (c == EOS)
        if (flags & FNM_PATHNAME)
          return ((flags & FNM_LEADING_DIR) ||
                    strchr(string, '/') == NULL ?
                    0 : FNM_NOMATCH);
        else return (0);
      else if ((c == '/') && (flags & FNM_PATHNAME)) {
        if ((string = strchr(string, '/')) == NULL) return (FNM_NOMATCH);
        break;
      }

      // General case, use recursion.
      while ((test = *string) != EOS) {
        if (!fnmatch(pattern, string, flags & ~FNM_PERIOD)) return (0);
        if ((test == '/') && (flags & FNM_PATHNAME)) break;
        ++string;
      }
      return (FNM_NOMATCH);
    case '[':
      if (*string == EOS) return (FNM_NOMATCH);
      if ((*string == '/') && (flags & FNM_PATHNAME)) return (FNM_NOMATCH);
      if ((pattern = rangematch(pattern, *string, flags)) == NULL) return (FNM_NOMATCH);
      ++string;
      break;
    case '\\':
      if (!(flags & FNM_NOESCAPE)) {
        if ((c = *pattern++) == EOS) {
          c = '\\';
          --pattern;
        }
      }
      break;
      // FALLTHROUGH
    default:
      if (c == *string) {
      }
      else if ((flags & FNM_CASEFOLD) && (tolower((unsigned char)c) == tolower((unsigned char)*string))) {
      }
      else if ((flags & FNM_PREFIX_DIRS) && *string == EOS && ((c == '/' && string != stringstart) ||
    		  (string == stringstart+1 && *stringstart == '/')))
              return (0);
      else return (FNM_NOMATCH);
      string++;
      break;
    }
  // NOTREACHED
  return 0;
}

// ============================================================================

static int os_list (lua_State *L) {
    const char *path = luaL_optstring(L, 1, NULL);
    const char *match = luaL_optstring(L, 2, NULL);

    DIR *dir = NULL;
    struct dirent *ent;
    char type;
    char size[9];
    char cpath[255];
    char tpath[255];
    char tbuffer[80];
    struct stat sb;
    struct tm *tm_info;
    char *lpath = NULL;
    char *ppath = NULL;
    int statok;
    int on_fat = 0;

    // If path is not present get the current directory as path
    if (!path) {
        if (!getcwd(cpath, 255)) {
            return luaL_fileresult(L, 0, cpath);
        }
        path = cpath;
    }

    lpath = mount_resolve_to_logical(path);
    ppath = mount_resolve_to_physical(lpath);

    if (strstr(ppath, "/fat/") == ppath) on_fat = 1;

    free(ppath);

    // Open directory
    dir = opendir(lpath);
    if (!dir) {
        return luaL_fileresult(L, 0, path);
    }

    // Read directory entries
    uint64_t total = 0;
    int nfiles = 0;
    printf("T  Size      Date/Time         Name\r\n");
    printf("-----------------------------------\r\n");
    while ((ent = readdir(dir)) != NULL) {
    	sprintf(tpath, lpath);
        strcat(tpath,"/");
        strcat(tpath,ent->d_name);
        tbuffer[0] = '\0';

        if ((match == NULL) || (fnmatch(match, tpath, (FNM_PERIOD)) == 0)) {
			// Get file stat
			statok = stat(tpath, &sb);

			if (statok == 0) {
				tm_info = localtime(&sb.st_mtime);
				strftime(tbuffer, 80, "%d/%m/%Y %R", tm_info);
			}

			type = 'd';
			if (ent->d_type == DT_REG) {
				type = 'f';
				nfiles++;
				if (statok) strcpy(size, "       ?");
				else {
					total += sb.st_size;
					if (sb.st_size < (1024*1024)) sprintf(size,"%8d", (int)sb.st_size);
					else if ((sb.st_size/1024) < (1024*1024)) sprintf(size,"%6dKB", (int)(sb.st_size / 1024));
					else sprintf(size,"%6dMB", (int)(sb.st_size / (1024 * 1024)));
				}
			//} else if (ent->d_type == DT_LNK) {
			//    type = 'l';
			//    sprintf(size,"%8d", ent->d_fsize);
			} else {
				strcpy(size, "       -");
			}

			printf("%c  %s  %s  %s\r\n",
				type,
				size,
				tbuffer,
				ent->d_name
			);
        }
    }
    if (total) {
        printf("-----------------------------------\r\n");
    	if (total < (1024*1024)) printf("   %8d", (int)total);
    	else if ((total/1024) < (1024*1024)) printf("   %6dKB", (int)(total / 1024));
    	else printf("   %6dMB", (int)(total / (1024 * 1024)));
    	printf(" in %d files\r\n", nfiles);
    }
    printf("-----------------------------------\r\n");

    closedir(dir);

    free(lpath);

    if (!on_fat) {
    	uint32_t total, used;
    	spiffs_fs_stat(&total, &used);
    	printf("SPIFFS: free %d KB of %d KB\r\n", (total-used) / 1024, total / 1024);
    }
    else {
        FATFS *fs;
        DWORD fre_clust, fre_sect, tot_sect;
        // Get volume information and free clusters
        FRESULT res = f_getfree("/fat", &fre_clust, &fs);
        if (res == 0) {
			// Get total sectors and free sectors
			tot_sect = (fs->n_fatent - 2) * fs->csize;
			fre_sect = fre_clust * fs->csize;
	    	printf("FATFS: free %lu MB of %lu MB\r\n", (fre_sect / 2) / 1024, (tot_sect / 2) / 1024);
    	}
    }

    return 0;    
}
 
static int os_clear (lua_State *L) {
    console_clear();
    
    return 0;
}

static int os_version(lua_State *L) {
   lua_pushstring(L, "Lua RTOS");
   lua_pushstring(L, LUA_OS_VER);   
   lua_pushinteger(L, BUILD_TIME);
   
   return 3;
}

static int os_cpu(lua_State *L) {
	int revission;
	
    char model[18];
    char cpuInfo[26];
    
    cpu_model(model);

	revission = cpu_revission();
	if (revission) {
		sprintf(cpuInfo, "%s rev A%d", model, cpu_revission());
	} else {
		sprintf(cpuInfo, "%s", model);
	}
    
    lua_pushstring(L, cpuInfo);
    
    return 1;
}

static int os_logcons(lua_State *L) { 
    int mask = LOG_NDELAY;
    
    luaL_checktype(L, 1, LUA_TBOOLEAN);
    int cons = lua_toboolean( L, 1 );

    if (cons) {
        mask = mask | LOG_CONS;
    }
    
    closelog();
    openlog(__progname, mask , LOG_LOCAL1);

    return 0;
}

static int os_stats(lua_State *L) {
    const char *stat = luaL_optstring(L, 1, NULL);

	// Do a garbage collection
	lua_lock(L);
	luaC_fullgc(L, 1);
	lua_unlock(L);
	
    if (stat && strcmp(stat,"mem") == 0) {
        lua_pushinteger(L, xPortGetFreeHeapSize());
        return 1;
    } else {
        printf("Free mem: %d\n",xPortGetFreeHeapSize());        
    }
    
    return 0;
}

static int os_format(lua_State *L) {
	#if 0
    const char *device = luaL_checkstring(L, 1);
    const struct devops *ops;
    char response = ' ';

    // This if check is for future use, to determine drive number
    if (strcmp(device,"cfi") == 0) {
    } else if (strcmp(device,"sd") == 0) {
    } else {
        return luaL_error(L, "device %s not exists for format", device); 
    }
    
    // Check for mounted device
    if (!mount_is_mounted(device)) {
         return luaL_error(L, "device %s is not mounted", device);            
    }
    
    // Confirmation
    while ((response != 'y') && (response != 'Y') && (response != 'n') && (response != 'N')) {
        printf("\r");
        console_erase_l();
        printf("All data in %s will be deleted. Continue? [y/n]: ", device);
        response = fgetc(stdin);
    }
    
    printf("\n");
    
    if ((response == 'y') || (response == 'Y')) {
        ops = getdevops(device);
        (*ops->fo_format)();  
    } else {
        printf("Format cancelled\n");
    }
    #endif
    return 0;
}

static int os_lua_running(lua_State *L) { 
    lua_pushboolean(L, status_get(STATUS_LUA_RUNNING));

    return 1;
}

static int os_lua_interpreter(lua_State *L) { 
    lua_pushboolean(L, status_get(STATUS_LUA_INTERPRETER));

    return 1;
}

static int os_history(lua_State *L) { 
    luaL_checktype(L, 1, LUA_TBOOLEAN);
    
	if (lua_toboolean( L, 1 )) {
		status_set(STATUS_LUA_HISTORY);
	} else {
		status_clear(STATUS_LUA_HISTORY);
	}
    
    return 0;
}

static int os_cp(lua_State *L) {
    const char *src = luaL_optstring(L, 1, NULL);
    const char *dst = luaL_optstring(L, 2, NULL);
    FILE *fsrc, *fdst;
    char c;
    int res1, res2;

    fsrc = fopen(src,"r");
    if (!fsrc) {
        return luaL_fileresult(L, 0, src);
    }

    fdst = fopen(dst,"w");
    if (!fdst) {
        fclose(fsrc);
        return luaL_fileresult(L, 0, dst);
    }
    
    c = fgetc(fsrc);
    while (!feof(fsrc)) {
        fputc(c, fdst);
        c = fgetc(fsrc);    
    }
    
    res1 = fclose(fsrc);
    res2 = fclose(fdst);
    
    if (res1 != 0) {
        return luaL_fileresult(L, 0, src);
    }

    if (res2 != 0) {
        return luaL_fileresult(L, 0, dst);
    }
    
    lua_pushboolean(L, 1);
    return 1;
}

static int os_exists(lua_State *L) {
	const char *fname;
	struct stat sb;
	size_t len;

	fname = luaL_checklstring( L, 1, &len );
	if (stat(fname, &sb) != 0) {
		lua_pushboolean(L, false);
	}
	else lua_pushboolean(L, true);

	return 1;
}

static int os_mountfat(lua_State *L) {
	mount_fatfs();
	return 0;
}

static int os_unmountfat(lua_State *L) {
	unmount_fatfs();
	return 0;
}

static int os_run (lua_State *L) {
	#if 0
    const char *argCode = luaL_optstring(L, 1, "");
    unsigned int i;
    int done;

    char *code = NULL;
    int code_size = 0;
    
    char *cchunk;
    char chunk_size;

    // Lock tty, avoid other threads to write to console
    //tty_lock();

    if (*argCode) {
        code = (char *)argCode;
        lua_pop(L, 1);

        
        goto skip;
    }
    
    lua_pop(L, 1);

    // Clear received buffer
    uart_consume(CONSOLE_UART);

    // Send 'C' for start
    uart_write(CONSOLE_UART, 'C');
    uart_write(CONSOLE_UART, '\n');

    done = 0;

    for(;;) {
        // Wait for chunk size
        if (!uart_read(CONSOLE_UART, &chunk_size, 1000)) {
            break;
        }

        // More chunks?
        if (chunk_size == 0) {
            done = 1;
            break;
        }
        
        code = realloc(code, code_size + chunk_size + 1);

        // Read chunk
        cchunk = code + code_size;
        for(i=0; i < chunk_size; i++) {
            if (!uart_read(CONSOLE_UART, cchunk++, 1000)) {
                break;
            }
        }
        
        *cchunk = 0x00;
        
        code_size = code_size + chunk_size;

        // Send 'C' for start
        uart_write(CONSOLE_UART, 'C');
        uart_write(CONSOLE_UART, '\n');
    }
        
    if (!done) {
        // Unlock tty, permit other threads to write to console
        //tty_unlock();

        free(code);

        return luaL_error(L, "timeout");
    }
       
 skip:
    // Call load
    lua_getglobal(L, "load"); 
    lua_pushstring(L, (const char *)code);
    lua_pcall(L, 1, 2, 0);

    if (!lua_isnil(L, -1)) {
        free(code);
        //tty_unlock();
        return luaL_error(L, lua_tostring(L, -1));
    }
    
    lua_remove(L, -1);
    
    lua_getglobal(L, "thread"); 
    lua_getfield(L, -1, "start");

    lua_remove(L, -2);   // Remove "thread" from the stack
    lua_insert(L, -2);   // Switch "thread" with parsed function
    
    if (lua_pcall(L, 1, 1, 0) != 0) {
        free(code);
        //tty_unlock();
        return luaL_error(L, lua_tostring(L, -1));
    }
        
    lua_pop(L, 1);
    
    // Unlock tty, permit other threads to write to console
    //tty_unlock();
#endif
    return 0;
}

static int os_bootcount(lua_State *L) {
	lua_pushinteger(L, boot_count);

	return 1;
}

static int os_flash_unique_id(lua_State *L) {
	#if CONFIG_LUA_RTOS_READ_FLASH_UNIQUE_ID
	char buffer[17];

	sprintf(buffer,
			"%02x%02x%02x%02x%02x%02x%02x%02x",
			flash_unique_id[0], flash_unique_id[1],
			flash_unique_id[2], flash_unique_id[3],
			flash_unique_id[4], flash_unique_id[5],
			flash_unique_id[6], flash_unique_id[7]
	);

    lua_pushstring(L, buffer);
	#else
    lua_pushnil(L);
	#endif
	return 1;
}

static int os_checkstack(lua_State *L) {
	UBaseType_t stck = uxTaskGetStackHighWaterMark(NULL);

	lua_pushinteger(L, CONFIG_LUA_RTOS_LUA_STACK_SIZE - stck);
	lua_pushinteger(L, CONFIG_LUA_RTOS_LUA_STACK_SIZE);

	return 2;
}

// ===== Lua compiler ===========================================

#define PRINT_BYTECODE

#include "lobject.h"
#include "lstate.h"
#include "lundump.h"

#define toproto(L,i) getproto(L->top+(i))
#define FUNCTION "(function()end)();"

#ifdef PRINT_BYTECODE
static void PrintFunction(const Proto* f, int full);
#define luaU_print	PrintFunction
#endif

//------------------------------------------------------------------
static int writer(lua_State* L, const void* p, size_t size, void* u)
{
 UNUSED(L);
 return (fwrite(p,size,1,(FILE*)u)!=1) && (size!=0);
}

// os.compile(filename.lua [,stripping, print])
//===================================
static int os_compile( lua_State* L )
{
  int stripping = 1;      // strip debug information
  int listing = 0;
  char output[64];
  size_t len;
  const char *filename = luaL_checklstring( L, 1, &len );
  const Proto* f;

  if (lua_gettop(L) > 1) {
	  stripping = luaL_checkinteger(L, 2);
  }
  if (lua_gettop(L) > 2) {
	  listing = luaL_checkinteger(L, 3);
  }
  // check that filename end with ".lua".
  if ((len < 5) || (strcmp( filename + len - 4, ".lua") != 0)) return luaL_error(L, "not a .lua file");

  if (luaL_loadfile(L,filename) != LUA_OK) return luaL_error(L, "error loading source file");

  f = toproto(L, -1);

  #ifdef PRINT_BYTECODE
  if (listing > 0) luaU_print(f, listing>1);
  #endif

  // make output file name
  strcpy(output, filename);
  output[strlen(output) - 2] = 'c';
  output[strlen(output) - 1] = '\0';

  // open output file
  FILE* D = fopen(output,"wb");
  if (D == NULL) return luaL_error(L, "error opening destination file");

  lua_lock(L);
  luaU_dump(L, f, writer, D, stripping);
  lua_unlock(L);

  if (ferror(D)) {
	  if (fclose(D)) return luaL_error(L, "error closing destination file");
	  return luaL_error(L, "error writing to destination file");
  }
  if (fclose(D)) return luaL_error(L, "error closing destination file");

  return 0;
}

// --------------------------------------------------------------

#ifdef PRINT_BYTECODE

/*
 * #####################################################################
 * Produce a listing of the compiled bytecode for Lua's virtual machine.
 * Listing bytecodes is useful to learn about Lua's virtual machine.
 * #####################################################################
 *
 * $Id: luac.c,v 1.75 2015/03/12 01:58:27 lhf Exp $
 * print bytecodes
 * See Copyright Notice in lua.h
*/

#include <ctype.h>

#define luac_c
#define LUA_CORE

#include "ldebug.h"
#include "lopcodes.h"

#define VOID(p)		((const void*)(p))

static void PrintString(const TString* ts)
{
 const char* s=getstr(ts);
 size_t i,n=tsslen(ts);
 printf("%c",'"');
 for (i=0; i<n; i++)
 {
  int c=(int)(unsigned char)s[i];
  switch (c)
  {
   case '"':  printf("\\\""); break;
   case '\\': printf("\\\\"); break;
   case '\a': printf("\\a"); break;
   case '\b': printf("\\b"); break;
   case '\f': printf("\\f"); break;
   case '\n': printf("\\n"); break;
   case '\r': printf("\\r"); break;
   case '\t': printf("\\t"); break;
   case '\v': printf("\\v"); break;
   default:	if (isprint(c))
   			printf("%c",c);
		else
			printf("\\%03d",c);
  }
 }
 printf("%c",'"');
}

static void PrintConstant(const Proto* f, int i)
{
 const TValue* o=&f->k[i];
 switch (ttype(o))
 {
  case LUA_TNIL:
	printf("nil");
	break;
  case LUA_TBOOLEAN:
	printf(bvalue(o) ? "true" : "false");
	break;
  case LUA_TNUMFLT:
	{
	char buff[100];
	sprintf(buff,LUA_NUMBER_FMT,fltvalue(o));
	printf("%s",buff);
	if (buff[strspn(buff,"-0123456789")]=='\0') printf(".0");
	break;
	}
  case LUA_TNUMINT:
	printf(LUA_INTEGER_FMT,ivalue(o));
	break;
  case LUA_TSHRSTR: case LUA_TLNGSTR:
	PrintString(tsvalue(o));
	break;
  default:				/* cannot happen */
	printf("? type=%d",ttype(o));
	break;
 }
}

#define UPVALNAME(x) ((f->upvalues[x].name) ? getstr(f->upvalues[x].name) : "-")
#define MYK(x)		(-1-(x))

static void PrintCode(const Proto* f)
{
 const Instruction* code=f->code;
 int pc,n=f->sizecode;
 for (pc=0; pc<n; pc++)
 {
  Instruction i=code[pc];
  OpCode o=GET_OPCODE(i);
  int a=GETARG_A(i);
  int b=GETARG_B(i);
  int c=GETARG_C(i);
  int ax=GETARG_Ax(i);
  int bx=GETARG_Bx(i);
  int sbx=GETARG_sBx(i);
  int line=getfuncline(f,pc);
  printf("\t%d\t",pc+1);
  if (line>0) printf("[%d]\t",line); else printf("[-]\t");
  printf("%-9s\t",luaP_opnames[o]);
  switch (getOpMode(o))
  {
   case iABC:
    printf("%d",a);
    if (getBMode(o)!=OpArgN) printf(" %d",ISK(b) ? (MYK(INDEXK(b))) : b);
    if (getCMode(o)!=OpArgN) printf(" %d",ISK(c) ? (MYK(INDEXK(c))) : c);
    break;
   case iABx:
    printf("%d",a);
    if (getBMode(o)==OpArgK) printf(" %d",MYK(bx));
    if (getBMode(o)==OpArgU) printf(" %d",bx);
    break;
   case iAsBx:
    printf("%d %d",a,sbx);
    break;
   case iAx:
    printf("%d",MYK(ax));
    break;
  }
  switch (o)
  {
   case OP_LOADK:
    printf("\t; "); PrintConstant(f,bx);
    break;
   case OP_GETUPVAL:
   case OP_SETUPVAL:
    printf("\t; %s",UPVALNAME(b));
    break;
   case OP_GETTABUP:
    printf("\t; %s",UPVALNAME(b));
    if (ISK(c)) { printf(" "); PrintConstant(f,INDEXK(c)); }
    break;
   case OP_SETTABUP:
    printf("\t; %s",UPVALNAME(a));
    if (ISK(b)) { printf(" "); PrintConstant(f,INDEXK(b)); }
    if (ISK(c)) { printf(" "); PrintConstant(f,INDEXK(c)); }
    break;
   case OP_GETTABLE:
   case OP_SELF:
    if (ISK(c)) { printf("\t; "); PrintConstant(f,INDEXK(c)); }
    break;
   case OP_SETTABLE:
   case OP_ADD:
   case OP_SUB:
   case OP_MUL:
   case OP_POW:
   case OP_DIV:
   case OP_IDIV:
   case OP_BAND:
   case OP_BOR:
   case OP_BXOR:
   case OP_SHL:
   case OP_SHR:
   case OP_EQ:
   case OP_LT:
   case OP_LE:
    if (ISK(b) || ISK(c))
    {
     printf("\t; ");
     if (ISK(b)) PrintConstant(f,INDEXK(b)); else printf("-");
     printf(" ");
     if (ISK(c)) PrintConstant(f,INDEXK(c)); else printf("-");
    }
    break;
   case OP_JMP:
   case OP_FORLOOP:
   case OP_FORPREP:
   case OP_TFORLOOP:
    printf("\t; to %d",sbx+pc+2);
    break;
   case OP_CLOSURE:
    printf("\t; %p",VOID(f->p[bx]));
    break;
   case OP_SETLIST:
    if (c==0) printf("\t; %d",(int)code[++pc]); else printf("\t; %d",c);
    break;
   case OP_EXTRAARG:
    printf("\t; "); PrintConstant(f,ax);
    break;
   default:
    break;
  }
  printf("\n");
 }
}

#define SS(x)	((x==1)?"":"s")
#define S(x)	(int)(x),SS(x)

static void PrintHeader(const Proto* f)
{
 const char* s=f->source ? getstr(f->source) : "=?";
 if (*s=='@' || *s=='=')
  s++;
 else if (*s==LUA_SIGNATURE[0])
  s="(bstring)";
 else
  s="(string)";
 printf("\n%s <%s:%d,%d> (%d instruction%s at %p)\n",
 	(f->linedefined==0)?"main":"function",s,
	f->linedefined,f->lastlinedefined,
	S(f->sizecode),VOID(f));
 printf("%d%s param%s, %d slot%s, %d upvalue%s, ",
	(int)(f->numparams),f->is_vararg?"+":"",SS(f->numparams),
	S(f->maxstacksize),S(f->sizeupvalues));
 printf("%d local%s, %d constant%s, %d function%s\n",
	S(f->sizelocvars),S(f->sizek),S(f->sizep));
}

static void PrintDebug(const Proto* f)
{
 int i,n;
 n=f->sizek;
 printf("constants (%d) for %p:\n",n,VOID(f));
 for (i=0; i<n; i++)
 {
  printf("\t%d\t",i+1);
  PrintConstant(f,i);
  printf("\n");
 }
 n=f->sizelocvars;
 printf("locals (%d) for %p:\n",n,VOID(f));
 for (i=0; i<n; i++)
 {
  printf("\t%d\t%s\t%d\t%d\n",
  i,getstr(f->locvars[i].varname),f->locvars[i].startpc+1,f->locvars[i].endpc+1);
 }
 n=f->sizeupvalues;
 printf("upvalues (%d) for %p:\n",n,VOID(f));
 for (i=0; i<n; i++)
 {
  printf("\t%d\t%s\t%d\t%d\n",
  i,UPVALNAME(i),f->upvalues[i].instack,f->upvalues[i].idx);
 }
}

static void PrintFunction(const Proto* f, int full)
{
 int i,n=f->sizep;
 PrintHeader(f);
 PrintCode(f);
 if (full) PrintDebug(f);
 for (i=0; i<n; i++) PrintFunction(f->p[i],full);
}

#endif

// ==============================================================
