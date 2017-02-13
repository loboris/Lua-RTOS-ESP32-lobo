#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := lua_rtos

all_binaries: configure-idf-lua-rtos configure-idf-lua-rtos-tests

clean: restore-idf

configure-idf-lua-rtos-tests:
	@echo "Configure esp-idf for Lua RTOS tests ..."
	@touch $(PROJECT_PATH)/components/lua_rtos/sys/sys_init.c
#ifeq ("$(wildcard $(IDF_PATH)/components/lua_rtos)","")
#	@ln -s $(PROJECT_PATH)/main/test/lua_rtos $(IDF_PATH)/components/lua_rtos
#endif

configure-idf-lua-rtos:
	@echo "Configure esp-idf ..."
	@cd $(IDF_PATH)/components/lwip/api && git checkout api_msg.c
	@cd $(IDF_PATH)/components/fatfs/src && git checkout ffconf.h
	@cd $(IDF_PATH)/components/fatfs/src/option && rm -f ccsbcs.c
	@echo "Configure esp-idf for Lua RTOS ..."
	@touch $(PROJECT_PATH)/components/lua_rtos/lwip/socket.c
	@cd $(IDF_PATH)/components/lwip/api && git checkout api_msg.c
	@patch -f $(IDF_PATH)/components/lwip/api/api_msg.c $(PROJECT_PATH)/main/patches/api_msg.patch
	@cp -f $(PROJECT_PATH)/main/patches/ffconf.h $(IDF_PATH)/components/fatfs/src/ffconf.h
	@cp -f $(PROJECT_PATH)/main/patches/ccsbcs.c $(IDF_PATH)/components/fatfs/src/option/ccsbcs.c

restore-idf:
	@echo "Restoring esp-idf ..."
	@cd $(IDF_PATH)/components/lwip/api && git checkout api_msg.c
	@cd $(IDF_PATH)/components/fatfs/src && git checkout ffconf.h
	@cd $(IDF_PATH)/components/fatfs/src/option && rm -f ccsbcs.c
	
include $(IDF_PATH)/make/project.mk
