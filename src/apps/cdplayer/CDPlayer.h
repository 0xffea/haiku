/*
	Copyright 1999, Be Incorporated.   All Rights Reserved.
	This file may be used under the terms of the Be Sample Code License.
*/
// This defines the replicant button

#ifndef CDPLAYER_H
#define CDPLAYER_H

#include <Application.h>
#include <View.h>
#include <Window.h>
#include <View.h>
#include <Button.h>
#include <Slider.h>
#include <TextControl.h>
#include <StringView.h>

#include "TrackMenu.h"
#include "CDAudioDevice.h"
#include "CDDBSupport.h"
#include "PlayList.h"

class DrawButton;
class DoubleShotDrawButton;
class TwoStateDrawButton;

class CDPlayer : public BView
{
public:
						CDPlayer(BRect frame, const char *name,	
								uint32 resizeMask = B_FOLLOW_ALL,
								uint32 flags = B_WILL_DRAW | B_NAVIGABLE | 
												B_PULSE_NEEDED);
	virtual 			~CDPlayer();
	
			void		BuildGUI(void);
	
	virtual	void		AttachedToWindow();	
	virtual void 		Pulse();
	virtual	void 		FrameResized(float new_width, float new_height);
	virtual void	 	MessageReceived(BMessage *);

private:
			void				WatchCDState(void);
	
			DrawButton			*fStop,
								*fNextTrack,
								*fPrevTrack,
								*fEject,
								*fSave;
								
			DoubleShotDrawButton
								*fFastFwd,
								*fRewind;
			
			TwoStateDrawButton	*fShuffle,
								*fRepeat,
								*fPlay;
			
			BSlider				*fVolumeSlider;
			
			BStringView			*fCDTitle,
								*fCurrentTrack,
								*fTrackTime,
								*fDiscTime;
			
			BBox				*fCDBox,
								*fTrackBox,
								*fTimeBox;
								
			TrackMenu			*fTrackMenu;
			
			CDState				fCDState;
			
			rgb_color			fStopColor;
			rgb_color			fPlayColor;
			
			CDAudioDevice		fCDDrive;
			PlayList			fPlayList;
			CDState				fWindowState;
			CDDBQuery			fCDQuery;
			CDDBData			fCDData;
			uint8				fVolume;
};


class CDPlayerApplication : public BApplication 
{
public:
	CDPlayerApplication();
};


#endif
