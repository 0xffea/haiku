/*
 * Copyright 2004-2007, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include "toscalls.h"
#include "video.h"
#include "mmu.h"
//#include "images.h"

#include <arch/cpu.h>
#include <boot/stage2.h>
#include <boot/platform.h>
#include <boot/menu.h>
#include <boot/kernel_args.h>
#include <boot/platform/generic/video.h>
#include <util/list.h>
#include <drivers/driver_settings.h>
#include <GraphicsDefs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


//#define TRACE_VIDEO
#ifdef TRACE_VIDEO
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


// XXX: use falcon video monitor detection and build possible mode list there...

// which API to use to handle this mode
// cf. http://toshyp.atari.org/004.htm
/*
enum {
	MODETYPE_XBIOS_ST,
	MODETYPE_XBIOS_TT,
	MODETYPE_XBIOS_FALCON,
	MODETYPE_CENTSCREEN,
	MODETYPE_CRAZYDOTS,
	MODETYPE_CT60,
	MODETYPE_NATFEAT
};
*/

class ModeOps {
public:
	ModeOps(const char *name) { fName = name; fInitStatus = B_NO_INIT; };
	~ModeOps() {};
	const char *Name() const { return fName; };
	virtual status_t	Init() { fInitStatus = B_OK; };
	status_t	InitStatus() const { return fInitStatus; };

	virtual status_t	Enumerate() = 0;
	virtual status_t	Get(struct video_mode *mode) = 0;
	virtual status_t	Set(const struct video_mode *mode) = 0;
	virtual status_t	Unset(const struct video_mode *mode) { return B_OK; };
	virtual addr_t		Framebuffer() { return NULL; };
	struct video_mode	*AllocMode();

	virtual int16	Width(const struct video_mode *mode=NULL);
	virtual int16	Height(const struct video_mode *mode=NULL);
	virtual int16	Depth(const struct video_mode *mode=NULL);
	virtual int16	BytesPerRow(const struct video_mode *mode=NULL);

private:
	const char *fName;
protected:
	status_t fInitStatus;
};

struct video_mode {
	list_link	link;
	ModeOps		*ops;
	color_space	space;
	uint16		mode;
	uint16		width, height, bits_per_pixel;
	uint32		bytes_per_row;
status_t	Set() { ops->Set(this); };
status_t	Unset() { ops->Unset(this); };
};

static struct list sModeList;
static video_mode *sMode, *sDefaultMode;
static uint32 sModeCount;
static addr_t sFrameBuffer;
static bool sModeChosen;


static int
compare_video_modes(video_mode *a, video_mode *b)
{
	int compare = a->width - b->width;
	if (compare != 0)
		return compare;

	compare = a->height - b->height;
	if (compare != 0)
		return compare;

	// TODO: compare video_mode::mode?
	return a->bits_per_pixel - b->bits_per_pixel;
}


/*!	Insert the video mode into the list, sorted by resolution and bit depth.
	Higher resolutions/depths come first.
*/
static void
add_video_mode(video_mode *videoMode)
{
dprintf("add_video_mode(%d x %d %s)\n", videoMode->width, videoMode->height, videoMode->ops->Name());
	video_mode *mode = NULL;
	while ((mode = (video_mode *)list_get_next_item(&sModeList, mode))
			!= NULL) {
		int compare = compare_video_modes(videoMode, mode);
		if (compare == 0) {
			// mode already exists
			return;
		}

		if (compare > 0)
			break;
	}

	list_insert_item_before(&sModeList, mode, videoMode);
	sModeCount++;
}

//	#pragma mark - 


struct video_mode *
ModeOps::AllocMode()
{
	
	video_mode *videoMode = (video_mode *)malloc(sizeof(struct video_mode));
	if (videoMode == NULL)
		return NULL;

	videoMode->ops = this;
	return videoMode;
}

int16
ModeOps::Width(const struct video_mode *mode)
{
	return mode ? mode->width : 0;
}


int16
ModeOps::Height(const struct video_mode *mode)
{
	return mode ? mode->height : 0;
}


int16
ModeOps::Depth(const struct video_mode *mode)
{
	return mode ? mode->bits_per_pixel : 0;
}


int16
ModeOps::BytesPerRow(const struct video_mode *mode)
{
	return mode ? mode->bytes_per_row : 0;
}


//	#pragma mark - Falcon XBIOS API

class FalconModeOps : public ModeOps {
public:
	FalconModeOps() : ModeOps("Falcon XBIOS") { fInitStatus = B_OK; };
	~FalconModeOps() {};
	virtual status_t	Enumerate();
	virtual status_t	Get(struct video_mode *mode);
	virtual status_t	Set(const struct video_mode *mode);
};


status_t
FalconModeOps::Enumerate()
{
	int16 monitor;
	monitor = VgetMonitor();
	switch (monitor) {
		case 0:
			panic("Monochrome ??");
			break;
		//case 4 & 5: check for CT60
		case 1:
		default:
			dprintf("monitor type %d\n", monitor);
			break;
	}
	return ENODEV;
}


status_t
FalconModeOps::Get(struct video_mode *mode)
{
	int16 m = VsetMode(VM_INQUIRE);
	int bpp;
	int width = 320;
	if (m < 0)
		return B_ERROR;
	bpp = 1 << (m & 0x0007);
	if (m & 0x0008)
		width *= 2;
	bool vga = (m & 0x0010) != 0;
	bool pal = (m & 0x0020) != 0;
	bool overscan = (m & 0x0040) != 0;
	bool st = (m & 0x0080) != 0;
	bool interlace = (m & 0x0100) != 0;
	return ENODEV;
}


status_t
FalconModeOps::Set(const struct video_mode *mode)
{
	return ENODEV;
}


static FalconModeOps sFalconModeOps;


//	#pragma mark - ARAnyM NFVDI API

/* NatFeat VDI */
#define FVDIDRV_NFAPI_VERSION	0x14000960L
#define FVDI_GET_VERSION	0
#define FVDI_GET_FBADDR	11
#define FVDI_SET_RESOLUTION	12
#define FVDI_GET_WIDTH	13
#define FVDI_GET_HEIGHT	14
#define FVDI_OPENWK 15
#define FVDI_CLOSEWK 16
#define FVDI_GETBPP	17


class NFVDIModeOps : public ModeOps {
public:
	NFVDIModeOps() : ModeOps("NFVDI") {};
	~NFVDIModeOps() {};
	virtual status_t	Init();
	virtual status_t	Enumerate();
	virtual status_t	Get(struct video_mode *mode);
	virtual status_t	Set(const struct video_mode *mode);
	virtual status_t	Unset(const struct video_mode *mode);
	virtual addr_t		Framebuffer();

	virtual int16	Width(const struct video_mode *mode=NULL);
	virtual int16	Height(const struct video_mode *mode=NULL);
	virtual int16	Depth(const struct video_mode *mode=NULL);

private:
	int32 fNatFeatId;
};


status_t
NFVDIModeOps::Init()
{
	// NF calls not available when the ctor is called
	fNatFeatId = nat_feat_getid("fVDI");
	if (fNatFeatId == 0)
		return B_ERROR;
	dprintf("fVDI natfeat id %d\n", fNatFeatId);
	
	int32 version = nat_feat_call(fNatFeatId, FVDI_GET_VERSION);
	dprintf("fVDI NF version %lx\n", version);
	if (version < FVDIDRV_NFAPI_VERSION)
		return B_ERROR;
	fInitStatus = B_OK;
	return fInitStatus;
}


status_t
NFVDIModeOps::Enumerate()
{
	if (fNatFeatId == 0)
		return B_NO_INIT;

	video_mode * videoMode;

	videoMode = AllocMode();
	if (videoMode == NULL)
		return B_ERROR;

	Get(videoMode);
	//videoMode->space = ;
	videoMode->mode = 0;
	videoMode->width = 800;
	videoMode->height = 600;
	videoMode->bits_per_pixel = 8;
	videoMode->bytes_per_row = videoMode->width * videoMode->bits_per_pixel / 8;

	add_video_mode(videoMode);


	videoMode = AllocMode();
	if (videoMode == NULL)
		return B_ERROR;

	Get(videoMode);
	//videoMode->space = ;
	videoMode->mode = 0;
	videoMode->width = 1024;
	videoMode->height = 768;
	videoMode->bits_per_pixel = 16;
	videoMode->bytes_per_row = videoMode->width * videoMode->bits_per_pixel / 8;

	add_video_mode(videoMode);


	return B_OK;
}


status_t
NFVDIModeOps::Get(struct video_mode *mode)
{
	if (mode == NULL)
		return B_BAD_VALUE;
	if (fNatFeatId == 0)
		return B_NOT_SUPPORTED;
	mode->width = Width();
	mode->height = Height();
	mode->bits_per_pixel = Depth();
	mode->bytes_per_row = mode->width * mode->bits_per_pixel / 8;
	dprintf("Get: %dx%d\n", mode->width, mode->height);
	return B_OK;
}


status_t
NFVDIModeOps::Set(const struct video_mode *mode)
{
	if (mode == NULL)
		return B_BAD_VALUE;
	if (fNatFeatId == 0)
		return B_NOT_SUPPORTED;
	status_t err;
dprintf("fVDI::Set(%ldx%ld %ld)\n", (int32)Width(mode), (int32)Height(mode), (int32)Depth(mode));
	err = nat_feat_call(fNatFeatId, FVDI_SET_RESOLUTION,
		(int32)Width(mode), (int32)Height(mode), (int32)Depth(mode));
	err = toserror(err);
	err = nat_feat_call(fNatFeatId, FVDI_OPENWK);

	return B_OK;
}


status_t
NFVDIModeOps::Unset(const struct video_mode *mode)
{
	if (mode == NULL)
		return B_BAD_VALUE;
	if (fNatFeatId == 0)
		return B_NOT_SUPPORTED;
	nat_feat_call(fNatFeatId, FVDI_CLOSEWK);
	return B_OK;
}


addr_t
NFVDIModeOps::Framebuffer()
{
	addr_t fb;
	if (fNatFeatId == 0)
		return (addr_t)NULL;
	fb = (addr_t)nat_feat_call(fNatFeatId, FVDI_GET_FBADDR);
	dprintf("fb 0x%08lx\n", fb);
	return fb;
}


int16
NFVDIModeOps::Width(const struct video_mode *mode)
{
	if (mode)
		return ModeOps::Width(mode);
	if (fNatFeatId == 0)
		return 0;
	return (int16)nat_feat_call(fNatFeatId, FVDI_GET_WIDTH);
}


int16
NFVDIModeOps::Height(const struct video_mode *mode)
{
	if (mode)
		return ModeOps::Height(mode);
	if (fNatFeatId == 0)
		return 0;
	return (int16)nat_feat_call(fNatFeatId, FVDI_GET_HEIGHT);
}


int16
NFVDIModeOps::Depth(const struct video_mode *mode)
{
	if (mode)
		return ModeOps::Depth(mode);
	if (fNatFeatId == 0)
		return 0;
	return (int16)nat_feat_call(fNatFeatId, FVDI_GETBPP);
}


static NFVDIModeOps sNFVDIModeOps;


//	#pragma mark -


bool
video_mode_hook(Menu *menu, MenuItem *item)
{
	// find selected mode
	video_mode *mode = NULL;

	menu = item->Submenu();
	item = menu->FindMarked();
	if (item != NULL) {
		switch (menu->IndexOf(item)) {
			case 0:
				// "Default" mode special
				sMode = sDefaultMode;
				sModeChosen = false;
				return true;
			//case 1:
				// "Standard VGA" mode special
				// sets sMode to NULL which triggers VGA mode
				//break;
			default:
				mode = (video_mode *)item->Data();
				break;
		}
	}

	if (mode != sMode) {
		// update standard mode
		// ToDo: update fb settings!
		sMode = mode;
platform_switch_to_logo();
	}

	sModeChosen = true;
	return true;
}


Menu *
video_mode_menu()
{
	Menu *menu = new(nothrow) Menu(CHOICE_MENU, "Select Video Mode");
	MenuItem *item;

	menu->AddItem(item = new(nothrow) MenuItem("Default"));
	item->SetMarked(true);
	item->Select(true);
	item->SetHelpText("The Default video mode is the one currently configured "
		"in the system. If there is no mode configured yet, a viable mode will "
		"be chosen automatically.");

	//menu->AddItem(new(nothrow) MenuItem("Standard VGA"));

	video_mode *mode = NULL;
	while ((mode = (video_mode *)list_get_next_item(&sModeList, mode)) != NULL) {
		char label[64];
		sprintf(label, "%ux%u %u bit (%s)", mode->width, mode->height,
			mode->bits_per_pixel, mode->ops->Name());

		menu->AddItem(item = new(nothrow) MenuItem(label));
		item->SetData(mode);
	}

	menu->AddSeparatorItem();
	menu->AddItem(item = new(nothrow) MenuItem("Return to main menu"));
	item->SetType(MENU_ITEM_NO_CHOICE);

	return menu;
}


void
platform_blit4(addr_t frameBuffer, const uint8 *data,
	uint16 width, uint16 height, uint16 imageWidth, uint16 left, uint16 top)
{
	if (!data)
		return;
}


extern "C" void
platform_set_palette(const uint8 *palette)
{
}


//	#pragma mark -


extern "C" void
platform_switch_to_logo(void)
{
	// in debug mode, we'll never show the logo
	if ((platform_boot_options() & BOOT_OPTION_DEBUG_OUTPUT) != 0)
		return;

	addr_t lastBase = gKernelArgs.frame_buffer.physical_buffer.start;
	size_t lastSize = gKernelArgs.frame_buffer.physical_buffer.size;

	// TODO: implement me
	if (sMode != NULL) {
		sMode->Set();

		gKernelArgs.frame_buffer.width = sMode->ops->Width(sMode);
		gKernelArgs.frame_buffer.height = sMode->ops->Height(sMode);
		gKernelArgs.frame_buffer.bytes_per_row = sMode->ops->BytesPerRow(sMode);
		gKernelArgs.frame_buffer.depth = sMode->ops->Depth(sMode);
		gKernelArgs.frame_buffer.physical_buffer.size = 
			gKernelArgs.frame_buffer.height
			* gKernelArgs.frame_buffer.bytes_per_row;
		gKernelArgs.frame_buffer.physical_buffer.start =
			sMode->ops->Framebuffer();
sFrameBuffer = sMode->ops->Framebuffer();
	} else {
		gKernelArgs.frame_buffer.enabled = false;
		return;
	}
	gKernelArgs.frame_buffer.enabled = true;

#if 0
	// If the new frame buffer is either larger than the old one or located at
	// a different address, we need to remap it, so we first have to throw
	// away its previous mapping
	if (lastBase != 0
		&& (lastBase != gKernelArgs.frame_buffer.physical_buffer.start
			|| lastSize < gKernelArgs.frame_buffer.physical_buffer.size)) {
		mmu_free((void *)sFrameBuffer, lastSize);
		lastBase = 0;
	}
	if (lastBase == 0) {
		// the graphics memory has not been mapped yet!
		sFrameBuffer = mmu_map_physical_memory(
			gKernelArgs.frame_buffer.physical_buffer.start,
			gKernelArgs.frame_buffer.physical_buffer.size, kDefaultPageFlags);
	}
#endif
dprintf("splash fb @ %p\n", sFrameBuffer);
	video_display_splash(sFrameBuffer);
dprintf("splash done\n");
}


extern "C" void
platform_switch_to_text_mode(void)
{
	// TODO: implement me
	if (!gKernelArgs.frame_buffer.enabled) {
		return;
	}

	if (sMode)
		sMode->Unset();

	gKernelArgs.frame_buffer.enabled = 0;
}


extern "C" status_t
platform_init_video(void)
{
	gKernelArgs.frame_buffer.enabled = 0;
	list_init(&sModeList);


	// ToDo: implement me
	dprintf("current video mode: \n");
	dprintf("Vsetmode(-1): 0x%08x\n", VsetMode(VM_INQUIRE));
	sFalconModeOps.Init();
	sFalconModeOps.Enumerate();
	// NF VDI does not implement FVDI_GET_FBADDR :(
	//sNFVDIModeOps.Init();
	//sNFVDIModeOps.Enumerate();
	
	return B_OK;
}

