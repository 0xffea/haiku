#ifndef	_OPENBEOS_APP_SERVER_H_
#define	_OPENBEOS_APP_SERVER_H_

#include <OS.h>
#include <Locker.h>
#include <List.h>
#include <Application.h>
#include <Window.h>
#include "Decorator.h"
#include "ServerConfig.h"

class Layer;
class BMessage;
class ServerApp;
class DisplayDriver;

/*!
	\class AppServer AppServer.h
	\brief main manager object for the app_server
	
	File for the main app_server thread. This particular thread monitors for
	application start and quit messages. It also starts the housekeeping threads
	and initializes most of the server's globals.
*/
#if DISPLAYDRIVER == HWDRIVER
class AppServer
#else
class AppServer : public BApplication
#endif
{
public:
	AppServer(void);
	~AppServer(void);
	static int32 PollerThread(void *data);
	static int32 PicassoThread(void *data);
	thread_id Run(void);
	void MainLoop(void);
	bool LoadDecorator(const char *path);
	void DispatchMessage(int32 code, int8 *buffer);
	void Broadcast(int32 code);
	void HandleKeyMessage(int32 code, int8 *buffer);
	ServerApp *FindApp(const char *sig);
private:
	friend Decorator *new_decorator(BRect rect, const char *title, int32 wlook, int32 wfeel,
			int32 wflags, DisplayDriver *ddriver);

	create_decorator *make_decorator;	// global function pointer

	port_id	_messageport,_mouseport;
	image_id _decorator_id;
	bool _quitting_server;
	BList *_applist;
	int32 _active_app;
	ServerApp *_p_active_app;
	thread_id _poller_id, _picasso_id;

	sem_id _active_lock, _applist_lock, _decor_lock;
	bool _exit_poller;
	DisplayDriver *_driver;
};

Decorator *new_decorator(BRect rect, const char *title, int32 wlook, int32 wfeel,
	int32 wflags, DisplayDriver *ddriver);
#endif
