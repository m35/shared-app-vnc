#include <IOKit/pwr_mgt/IOPMLib.h>
#include <IOKit/pwr_mgt/IOPM.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <pthread.h>

#include <Carbon/Carbon.h>

#include "rfb.h"

Bool rfbNoDimming = FALSE;
Bool rfbNoSleep   = TRUE;

static pthread_mutex_t  dimming_mutex;
static unsigned long    dim_time;
static unsigned long    sleep_time;
static mach_port_t      master_dev_port;
static io_connect_t     power_mgt;
static Bool initialized            = FALSE;
static Bool dim_time_saved         = FALSE;
static Bool sleep_time_saved       = FALSE;

// GRW - SharedAppVnc
static EventLoopTimerUPP screensaverTimerUPP;
static EventLoopTimerRef screensaverTimer;
static Bool screenSaverOn = TRUE;


// OSXvnc 0.8 - Disable ScreenSaver
void rfbScreensaverTimer(EventLoopTimerRef timer, void *userData)
{
#pragma unused (timer, userData)
    if (rfbNoSleep && rfbClientsConnected())
        UpdateSystemActivity(IdleActivity);
}


static int
saveDimSettings(void)
{
    if (IOPMGetAggressiveness(power_mgt, 
                              kPMMinutesToDim, 
                              &dim_time) != kIOReturnSuccess)
        return -1;

    dim_time_saved = TRUE;
    return 0;
}

static int
restoreDimSettings(void)
{
    if (!dim_time_saved)
        return -1;

    if (IOPMSetAggressiveness(power_mgt, 
                              kPMMinutesToDim, 
                              dim_time) != kIOReturnSuccess)
        return -1;

    dim_time_saved = FALSE;
    dim_time = 0;
    return 0;
}

static int
saveSleepSettings(void)
{
    if (IOPMGetAggressiveness(power_mgt, 
                              kPMMinutesToSleep, 
                              &sleep_time) != kIOReturnSuccess)
        return -1;

    sleep_time_saved = TRUE;
    return 0;
}

static int
restoreSleepSettings(void)
{
    if (!sleep_time_saved)
        return -1;

    if (IOPMSetAggressiveness(power_mgt, 
                              kPMMinutesToSleep, 
                              sleep_time) != kIOReturnSuccess)
        return -1;

    sleep_time_saved = FALSE;
    sleep_time = 0;
    return 0;
}

// GRW - SharedAppVnc
int setScreenSaver(Bool On)
{
	if (On)
	{
		// turn On
		if (!screenSaverOn)
		{
			rfbLog("Turning ScreenSaver On");
			/* remove the screensaver timer */
			RemoveEventLoopTimer(screensaverTimer);
			DisposeEventLoopTimerUPP(screensaverTimerUPP);
			screenSaverOn = TRUE;
		}
	} else {
		// turn Off
		/* setup screen saver disabling timer */
		rfbLog("Turning ScreenSaver Off");
		screensaverTimerUPP = NewEventLoopTimerUPP(rfbScreensaverTimer);
		InstallEventLoopTimer(GetMainEventLoop(),
							  kEventDurationSecond * 30,
							  kEventDurationSecond * 30,
							  screensaverTimerUPP,
							  NULL,
							  &screensaverTimer);
		screenSaverOn = FALSE;
		
	}	
	return 0;
}

// GRW - SharedAppVnc 
int setSleep(Bool On)
{
	if (On)
	{
		// turn On
		rfbLog("Turning Sleep On");
		restoreSleepSettings();
	} else {
		// turn Off
		if (!sleep_time_saved)
		{
			if (saveSleepSettings() < 0) return -1;
			rfbLog("Turning Sleep Off");
			if (IOPMSetAggressiveness(power_mgt, kPMMinutesToSleep, 0) != kIOReturnSuccess)
				return -1;
		}
	}
	return 0;	
}

// GRW - SharedAppVnc 
int setDimming(Bool On)
{
	if (On)
	{
		// turn On
		rfbLog("Turning Dimming On");
		restoreDimSettings();
	} else { 
		// turn Off
	    if (!dim_time_saved)
		{
			if (saveDimSettings() < 0) return -1;
			rfbLog("Turning Dimming Off");
			if (IOPMSetAggressiveness(power_mgt, kPMMinutesToDim, 0) != kIOReturnSuccess)
				return -1;
		}	
	}
	return 0;
}


int
rfbDimmingInit(void)
{
    pthread_mutex_init(&dimming_mutex, NULL);

    if (IOMasterPort(bootstrap_port, &master_dev_port) != kIOReturnSuccess)
        return -1;

    if (!(power_mgt = IOPMFindPowerManagement(master_dev_port)))
        return -1;

    if (rfbNoDimming) {
        if (saveDimSettings() < 0)
            return -1;
        if (IOPMSetAggressiveness(power_mgt, 
                                  kPMMinutesToDim, 0) != kIOReturnSuccess)
            return -1;
    }

    if (rfbNoSleep) {
        if (saveSleepSettings() < 0)
            return -1;
        if (IOPMSetAggressiveness(power_mgt, 
                                  kPMMinutesToSleep, 0) != kIOReturnSuccess)
            return -1;
    }

    initialized = TRUE;
    return 0;
}


int
rfbUndim(void)
{
    int result = -1;

    pthread_mutex_lock(&dimming_mutex);
    
    if (!initialized)
        goto DONE;

    if (!rfbNoDimming) {
        if (saveDimSettings() < 0)
            goto DONE;
        if (IOPMSetAggressiveness(power_mgt, kPMMinutesToDim, 0) != kIOReturnSuccess)
            goto DONE;
        if (restoreDimSettings() < 0)
            goto DONE;
    }
    
    if (!rfbNoSleep) {
        if (saveSleepSettings() < 0)
            goto DONE;
        if (IOPMSetAggressiveness(power_mgt, kPMMinutesToSleep, 0) != kIOReturnSuccess)
            goto DONE;
        if (restoreSleepSettings() < 0)
            goto DONE;
    }

    result = 0;

 DONE:
    pthread_mutex_unlock(&dimming_mutex);
    return result;
}


int
rfbDimmingShutdown(void)
{
    int result = -1;

    if (!initialized)
        goto DONE;

    pthread_mutex_lock(&dimming_mutex);
    if (dim_time_saved)
        if (restoreDimSettings() < 0)
            goto DONE;
    if (sleep_time_saved)
        if (restoreSleepSettings() < 0)
            goto DONE;

    result = 0;

 DONE:
    pthread_mutex_unlock(&dimming_mutex);
    return result;
}
