#include "OS.h"
#include "Roster.h"
#include "Application.h"
#include "SSInputFilter.h"

extern "C" _EXPORT BInputServerFilter* instantiate_input_filter();

SSISFilter *fltr=NULL;

BInputServerFilter* instantiate_input_filter() {  // required C func to build the IS Filter
	return (fltr=new SSISFilter()); 
}

int32 threadFunc (void *data) {
	while (1) {
		snooze(fltr->getSnoozeTime()*1000000); // snoozeTime is in microseconds
		fltr->CheckTime();
	}
	return B_OK; // Should never get here...
}

SSISFilter::SSISFilter() : enabled(false) {
	pref.LoadSettings();	
	blank=pref.GetBlankCorner();
	keep=pref.GetNeverBlankCorner();
	blankTime=snoozeTime=pref.BlankTime();
	watcher=spawn_thread(threadFunc,"ScreenSaverWatcher",0,NULL);	
	resume_thread(watcher);
}

void SSISFilter::Invoke(void) {
	if (current==keep)
		return; // If mouse is in this corner, never invoke.
	be_roster->Launch("application/x-vnd.OBOS-ScreenSaverApp");
	enabled=true;
}

void SSISFilter::Banish(void) {
	BMessenger ssApp ("application/x-vnd.OBOS-ScreenSaverApp",-1,NULL); // Don't care if it fails
	ssApp.SendMessage('MOO1');
	enabled=false;
}

void SSISFilter::CheckTime(void) {
	if ((rtc=real_time_clock())>=lastEventTime+blankTime) 
		Invoke();
	snoozeTime=(enabled)?blankTime:lastEventTime+blankTime-rtc;
}


void SSISFilter::UpdateRectangles(void) {
	BRegion region;
	GetScreenRegion(&region);
	BRect frame=region.Frame();

	topLeft.Set(frame.left,frame.top,frame.left+CORNER_SIZE,frame.top+CORNER_SIZE);
	topRight.Set(frame.right-CORNER_SIZE,frame.top,frame.right,frame.top+CORNER_SIZE);
	bottomLeft.Set(frame.left,frame.bottom-CORNER_SIZE,frame.left+CORNER_SIZE,frame.bottom);
	bottomRight.Set(frame.right-CORNER_SIZE,frame.bottom-CORNER_SIZE,frame.right,frame.bottom);
}

void SSISFilter::Cornered(arrowDirection pos) {
	current=pos;
	if (pos==blank)
		Invoke();
}

filter_result SSISFilter::Filter(BMessage *msg,BList *outList) {
	lastEventTime=real_time_clock();
	if (msg->what==B_SCREEN_CHANGED)
		UpdateRectangles();
	else if (msg->what==B_MOUSE_MOVED) {
		BPoint pos;
		msg->FindPoint("where",&pos);
		if (topLeft.Contains(pos)) 
			Cornered(UPLEFT);
		else if (topRight.Contains(pos)) 
			Cornered(UPRIGHT);
		else if (bottomLeft.Contains(pos)) 
			Cornered(DOWNLEFT);
		else if (bottomRight.Contains(pos)) 
			Cornered(DOWNRIGHT);
		else {
			Cornered(NONE);
			Banish();
			}
		}
	else
		Banish();
}

SSISFilter::~SSISFilter() {
	;
}
