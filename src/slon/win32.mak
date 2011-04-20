CPP=cl.exe
LINK32=link.exe

PGSHARE=\"c:\\postgresql\\9.0\\share\"
PTHREADS_INC=C:\pthreads-win32\include
PTHREADS_LIB=c:\pthreads-win32\lib
LINK32_FLAGS=/libpath:c:\postgresql\9.0\lib libpq.lib libpgport.lib /libpath:$(PTHREADS_LIB) pthreadVC2.lib wsock32.lib kernel32.lib user32.lib advapi32.lib /libpath:c:\gettext\lib intl.lib /NODEFAULTLIB:MSVCRT /DEBUG
OBJS = 	slon.obj		\
	runtime_config.obj	\
	local_listen.obj	\
	remote_listen.obj	\
	remote_worker.obj	\
	sync_thread.obj		\
	cleanup_thread.obj	\
	scheduler.obj		\
	dbutils.obj		\
	conf-file.obj		\
	confoptions.obj		\
	misc.obj                \
	../parsestatements/scanner.obj \
	port\win32service.obj \
	port\pipe.obj \


CPP_FLAGS=/c /D MSVC /D WIN32 /D PGSHARE=$(PGSHARE) /D YY_NO_UNISTD_H /I..\..\ /Ic:\Postgresql\9.0\include /Ic:\Postgresql\9.0\include/server /Ic:\Postgresql\9.0\include/server/port/win32 /I$(PTHREADS_INC) /MDd /Zi 

slon.obj: slon.c
	$(CPP) $(CPP_FLAGS)  slon.c

../parsestatements/scanner.obj: ..\parsestatements\scanner.c
	$(CPP) $(CPP_FLAGS) /Fo..\parsestatements\scanner.obj ..\parsestatements/scanner.c 

runtime_config.obj: runtime_config.c
	$(CPP) $(CPP_FLAGS) runtime_config.c

local_listen.obj: local_listen.c
	$(CPP) $(CPP_FLAGS) local_listen.c

remote_listen.obj: remote_listen.c
	$(CPP) $(CPP_FLAGS)  remote_listen.c

remote_worker.obj: remote_worker.c
	$(CPP) $(CPP_FLAGS) remote_worker.c

sync_thread.obj: sync_thread.c
	$(CPP) $(CPP_FLAGS) sync_thread.c

cleanup_thread.obj: cleanup_thread.c
	$(CPP) $(CPP_FLAGS)  cleanup_thread.c

scheduler.obj: scheduler.c
	$(CPP) $(CPP_FLAGS) scheduler.c

dbutils.obj: dbutils.c
	$(CPP) $(CPP_FLAGS) dbutils.c

conf-file.obj: conf-file.c
	$(CPP) $(CPP_FLAGS) conf-file.c

confoptions.obj: confoptions.c
	$(CPP) $(CPP_FLAGS) confoptions.c

misc.obj: misc.c
	$(CPP) $(CPP_FLAGS) misc.c

port\win32service.obj: port\win32service.c
	$(CPP) $(CPP_FLAGS) /I. /Foport/win32service.obj port\win32service.c

port\pipe.obj: port\pipe.c
	$(CPP) $(CPP_FLAGS) /I. /Foport/pipe.obj port\pipe.c

slon.exe: $(OBJS)
	$(LINK32) $(LINK32_FLAGS) $(OBJS) 