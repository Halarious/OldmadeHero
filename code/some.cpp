#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/joystick.h>
#include <alsa/asoundlib.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
//#include <X11/keysym.h>

#define pZERO(x)             memset((x), 0, sizeof(*(x)))
#define arraySize(array)     (sizeof(array) / sizeof(array[0]))

#define pi 3.14159265359f

typedef int8_t   int8;
typedef int16_t  int16;
typedef int32_t  int32;
typedef int64_t  int64;
typedef int32    bool32;

typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef float    real32;
typedef double   real64;

#include "Controller.cpp"

struct ScreenHandles
{  
  Display  *DisplayHandle;
  Visual   *Visual;
  Drawable  RootWindow;
  Drawable  Window;
  GC        GraphicsContext;
  
  int32 ScreenNum;
  int32 ScreenDepth; 
  int32 BitmapPad;

  uint32 DisplayWidth;
  uint32 DisplayHeight;
  uint32 WindowWidth;
  uint32 WindowHeight;
  uint32 BlackPixel;
  uint32 WhitePixel;
};

struct offscreen_buffer
{
  XImage *BitmapInfo;
  Pixmap BitmapHandle;
  void   *BitmapMemory;

  int BitmapMemorySize;
  int BytesPerPixel;
  int Width;
  int Height;
  int Pitch;
};

bool
PeekEvent(Display *display, XEvent *event) 
{
  if (!XPending(display))
    return false;
  
  XNextEvent(display, event);
  return true;
}

static bool Running = true;
static offscreen_buffer GlobalBackBuffer = {};

void
RenderWeirdGradient(ScreenHandles *ScreenHandles,
		    offscreen_buffer *Buffer, 
		    int BlueOffset, int GreenOffset)
{  
  uint8* Row = (uint8*)Buffer->BitmapMemory;
  for(int Y = 0;
      Y < Buffer->Height;
      ++Y)
    {
      uint32 *Pixel = (uint32*)Row;
      for (int X = 0; 
	   X < Buffer->Width;
	   ++X)
	{
	  uint8 Blue = X + BlueOffset;
	  uint8 Green = Y + GreenOffset;
	  *Pixel++ = ((Green << 8) | Blue);
	}
      Row += Buffer->Pitch;
    }
  
  XPutImage(
	    ScreenHandles->DisplayHandle, Buffer->BitmapHandle, 
	    ScreenHandles->GraphicsContext, Buffer->BitmapInfo,
	    0, 0, //Source
	    0, 0, //Dest
	    Buffer->Width, Buffer->Height);
}

void
ResizeSection(ScreenHandles *ScreenHandles,
	      offscreen_buffer *Buffer, uint32 Width, uint32 Height)
{  
  if(Buffer->BitmapMemory)
    {
      munmap(Buffer->BitmapMemory, 
	     Buffer->BitmapMemorySize);
      Buffer->BitmapMemory = 0;
      Buffer->BitmapInfo->data = 0;
    }
  
  if(Buffer->BitmapInfo)
    {
      XDestroyImage(Buffer->BitmapInfo);
      Buffer->BitmapInfo = 0;
    }

  if(Buffer->BitmapHandle)
    XFreePixmap(ScreenHandles->DisplayHandle, Buffer->BitmapHandle);

  Buffer->BitmapInfo = XCreateImage(
				    ScreenHandles->DisplayHandle, 
				    ScreenHandles->Visual, 
				    ScreenHandles->ScreenDepth, 
				    ZPixmap, 
				    0, 0, 
				    Width, 
				    Height, 
				    ScreenHandles->BitmapPad, 0);
  Buffer->BitmapHandle = XCreatePixmap(
				       ScreenHandles->DisplayHandle, 
				       ScreenHandles->Window, 
				       Width, Height, 
				       ScreenHandles->ScreenDepth);
      
  Buffer->Width  = Width;
  Buffer->Height = Height;
  
  Buffer->BitmapInfo->width          = Buffer->Width;
  Buffer->BitmapInfo->height         = Buffer->Height;
  Buffer->BitmapInfo->format         = ZPixmap;
  Buffer->BitmapInfo->bits_per_pixel = 32;
  Buffer->BytesPerPixel              = (Buffer->BitmapInfo->bits_per_pixel)/8;
  Buffer->BitmapInfo->bytes_per_line = Width*Buffer->BytesPerPixel;
  
  Buffer->BitmapMemorySize  = Buffer->Width*Buffer->Height*Buffer->BytesPerPixel;
  Buffer->BitmapMemory      = mmap(0, Buffer->BitmapMemorySize,
				   PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS,
				   -1, 0);
  Buffer->BitmapInfo->data  = (char*)(Buffer->BitmapMemory);
  Buffer->Pitch             = Width*Buffer->BytesPerPixel;
}


#define util_floor(x) (int)(x)
void
DisplayBufferStrechedInWindow(ScreenHandles *ScreenHandles,
			      uint32 dstWidth, uint32 dstHeight,
			      offscreen_buffer *Buffer)
{
  ResizeSection(ScreenHandles, Buffer, dstWidth, dstHeight);
  
  //TODO(rob): This is hard coded for now, there is a bug/inconsistency with the resoltion
  uint32 srcWidth  = Buffer->Width;
  uint32 srcHeight = Buffer->Height;
  
  uint32 *retPixmap;
  retPixmap = (uint32*)Buffer->BitmapInfo->data;

  uint32* buffer = (uint32*)Buffer->BitmapMemory;
  real32 x_ratio = (real32)srcWidth/dstWidth;
  real32 y_ratio = (real32)srcHeight/dstHeight;

  for(int Y = 0;
      Y < dstHeight;
      ++Y)
    {
      for (int X = 0; 
	   X < dstWidth;
	   ++X)
	{
	  int px = util_floor(X*x_ratio);
	  int py = util_floor(Y*y_ratio);
	  
	  int offset = py*srcWidth + px;
	  retPixmap[Y*dstWidth + X] = buffer[offset]; 
	}
    }
}

void 
DisplayBufferInWindow(ScreenHandles *ScreenHandles, 
		      uint32 Width, uint32 Height,
		      offscreen_buffer *Buffer)
{
  XCopyArea(ScreenHandles->DisplayHandle, 
	    Buffer->BitmapHandle, 
	    ScreenHandles->Window, 
	    ScreenHandles->GraphicsContext, 
	    0, 0, 
	    Width, Height, 
	    0, 0);
}


//NOTE(Rob): Test struct, this will not be used in the future
struct DeviceInfo
{
  //NOTE ALSA
  snd_pcm_t* handle;
  uint32 format;
  uint32 sampleCount;
  uint32 numChannels;
  
  //NOTE PulseAudio
  pa_stream *stream;
  pa_context *context;
  pa_mainloop *mainloop;
  const pa_timing_info *timingInfo;

  void* buffer;
  void* buffer2;

}TestDevice;

void
ALSA_PlayDevice(DeviceInfo device)
{
  static bool filled = false;
  uint32 frameSize   = (device.format/8) * device.numChannels;
  uint32 framesLeft  = device.sampleCount;//-64;
  int32 status;
  
  if(filled)
    return;

  uint8 *p_buffer = (uint8*)(device.buffer);
  while(framesLeft > 0)
    {
      int i  = snd_pcm_avail_update(device.handle);
      status = snd_pcm_writei(device.handle, p_buffer, framesLeft);
      printf("FIRST: %d\n", status);
      status = snd_pcm_writei(device.handle, p_buffer, framesLeft);
      printf("SECOND: %d\n", status);
      
      //snd_pcm_rewind(device.handle, 3*framesLeft);
      //printf("REWIND: %d\n", status);
      
      if(status < 0)
	{
	  switch(status)
	    {
	    case -EPIPE:
	      puts("EPIPE");
	      break;
	    case -ESTRPIPE:
	      puts("ESTRPIPE");
	      break;
	    case -EBADFD:
	      puts("EBADFD");
	      break;
	    case -ENOTTY:
	      puts("ENOTTY");
	      break;
	    case -ENODEV:
	      puts("ENODEV");
	      break;
	    case -EAGAIN:
	      puts("EAGAIN");
	      continue;
	      break;
	    default:
	      puts("default");
	      break;
	    }
	  //TODO(rob): Handle this properly, SDL says recover() does not handle it itself
	  //if(status == -EAGAIN)
	  //continue;

	  status = snd_pcm_recover(device.handle, status, 0);
	  if(status < 0)
	    return;

	  continue;
	}
      p_buffer   += status*frameSize;
      framesLeft -= status;
      filled      = false;
    }
}

int
ALSA_OpenDevice()//(desiredParams, obtainedParams, char* deviceName)
{
  int status = 0;
  snd_pcm_t *pcm_handle = 0;
  snd_pcm_hw_params_t *hwparams = 0;
  snd_pcm_sw_params_t *swparams = 0;
  snd_pcm_format_t format;

  uint32 sampleRate = 48000;
  uint32 samples = 24000;//1600;//3200;//24000
  uint32 channels = 2;

  status = snd_pcm_open(&pcm_handle, 
			"default", //TODO(rob): Can we fetch a device? Rewind maybe does not work like this
			SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
  if(status < 0)
    {
      printf("Opening PCM failed");
      return -1;
    }
  TestDevice.handle = pcm_handle;

  snd_pcm_hw_params_alloca(&hwparams);
  status = snd_pcm_hw_params_any(pcm_handle, hwparams);
  if(status < 0)
    return -1;
  
  status = snd_pcm_hw_params_set_access(pcm_handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED);
  if(status < 0)
    return -1;
   
  status = snd_pcm_hw_params_set_format(pcm_handle, hwparams, SND_PCM_FORMAT_S16_LE);
  if(status < 0)
    return -1;
  
  TestDevice.format = 16;
 
  status = snd_pcm_hw_params_set_channels_near(pcm_handle, hwparams, &channels);
  if(status < 0)
    return -1;

  TestDevice.numChannels = channels;
  
  status = snd_pcm_hw_params_set_rate_near(pcm_handle, hwparams, &sampleRate, 0);
  if(status < 0)
    return -1;

  snd_pcm_uframes_t frames = samples;
  status = snd_pcm_hw_params_set_period_size_near(pcm_handle, hwparams, &frames, 0);
  if(status < 0)
    return -1;

  TestDevice.sampleCount = (uint32)frames;
  
  uint32 periods = 2;
  status = snd_pcm_hw_params_set_periods_near(pcm_handle, hwparams, &periods, 0);
  if(status < 0)
    return -1;
  
  snd_pcm_uframes_t bufferSizeInFrames = frames*periods;
  status = snd_pcm_hw_params_set_buffer_size_near(pcm_handle, hwparams, &bufferSizeInFrames);
  if(status < 0)
    return -1;

  uint32 bufferSize = samples*sizeof(int32);
  TestDevice.buffer = malloc(bufferSize);

  int32 wavePeriod  = sampleRate/240;
  int16* halfFrame  = (int16*)TestDevice.buffer;
  for(int i = 0; i < samples; i++)
    { 
      if(i > (samples/wavePeriod)*wavePeriod)
	{
	  *halfFrame++ = 0;
	  *halfFrame++ = 0;
	  continue;
	}
      real32 t = (2.0f*pi*(real32)i) / (real32)wavePeriod;
      real32 sinValue = 2*sinf(t);
      int16 sampleValue =  (int16)(sinValue * 1000);
      
      *halfFrame++ = sampleValue;
      *halfFrame++ = sampleValue;
    }
    

  status = snd_pcm_hw_params(pcm_handle, hwparams);
  if(status < 0)
    return -1;
  
  status = snd_pcm_hw_params_get_buffer_size(hwparams, &bufferSizeInFrames);
  if(status < 0)
    return -1;
  
  if(bufferSizeInFrames != samples*2)
    {
      puts("Pissed yo");
      //return -1;
    }

  snd_pcm_sw_params_alloca(&swparams);
  status = snd_pcm_sw_params_current(pcm_handle, swparams);
  if(status < 0)
    return -1;
  
  status = snd_pcm_sw_params_set_avail_min(pcm_handle, swparams, 1);
  if(status < 0)
    return -1;
  
  status = snd_pcm_sw_params_set_start_threshold(pcm_handle, swparams, samples);
  if(status < 0)
    return -1;
  
  snd_pcm_uframes_t boundary;
  status = snd_pcm_sw_params_get_boundary(swparams, &boundary);
  if(status < 0)
    return -1;
  
  status = snd_pcm_sw_params(pcm_handle, swparams);
  if(status < 0)
    {
      printf("Unable to set sw params for playback: %s\n", snd_strerror(status));
      return -1;
    }

  snd_pcm_nonblock(pcm_handle, 0);

  return 0;
}
void
PULSEAUDIO_DisconnectFromPulseServer(pa_context *context, pa_mainloop *mainloop)
{
  if(context)
    {
      pa_context_disconnect(context);
      pa_context_unref(context);
    }
  
  if(mainloop != 0)
    pa_mainloop_free(mainloop);
}

void
PULSEAUDIO_underflow_cb(pa_stream *p, void *userdata)
{
  puts("UNDERFLOW!");
}

int32
PULSEAUDIO_OpenDevice()
{
  int32 sampleCount      = 1600;//24000;
  TestDevice.sampleCount = sampleCount;

  pa_sample_spec sampleSpec;
  sampleSpec.format   = PA_SAMPLE_S16LE; 
  sampleSpec.rate     = 48000;
  sampleSpec.channels = 2;

  //TODO(rob): Fix the 16/8 bullshit, it's size of a sample in bytes
  int32 bufferSizeInBytes = ((16/8) * sampleSpec.channels * sampleCount) * 2;
  TestDevice.buffer       = malloc(bufferSizeInBytes);
  TestDevice.buffer2      = malloc(bufferSizeInBytes);

  pa_buffer_attr bufferAttr;
  bufferAttr.maxlength = bufferSizeInBytes;
  bufferAttr.tlength   = bufferSizeInBytes/2;
  bufferAttr.prebuf    = bufferSizeInBytes/2;
  bufferAttr.minreq    = bufferSizeInBytes/2;

  pa_mainloop *mainLoop = 0;
  if(!(mainLoop = pa_mainloop_new()))
    {
      puts("Main loop failed to create");
      return -1;
    }

  pa_mainloop_api *mainLoopAPI;
  mainLoopAPI = pa_mainloop_get_api(mainLoop);
  
  pa_context *context = pa_context_new(mainLoopAPI, "Handmade PA_context");
  if(!context)
    {
      puts("Context failed to create");
      return -1;
    }

  if(pa_context_connect(context, 0, PA_CONTEXT_NOFLAGS, 0) < 0)
    {
      puts("Failed to connect context");
      return -1;
    }

  int32 state = pa_context_get_state(context);
  while(state != PA_CONTEXT_READY) 
    {
      if(pa_mainloop_iterate(mainLoop, 1, 0) < 0)
	{
	  puts("Failed on main loop iteration");
	  return -1;
	}
      state = pa_context_get_state(context);
      if(state == (PA_CONTEXT_FAILED || PA_CONTEXT_TERMINATED))
	{
	  puts("Bad state");
	  return -1;
	}
    }
  
  pa_channel_map channelMap;
  pa_channel_map_init_auto(&channelMap, sampleSpec.channels, PA_CHANNEL_MAP_WAVEEX);
    
  TestDevice.stream = pa_stream_new(context, "Handmade audio stream", &sampleSpec, &channelMap);
  if(TestDevice.stream == 0)
    {
      puts("Stream initialization failed");
      return -1;
    }

  if(pa_stream_connect_playback(TestDevice.stream, 
				0, 
				&bufferAttr, 
			        PA_STREAM_NOFLAGS, 
				0, 0)               < 0)
    {
      puts("Stream connect playback failed");
      return -1;
    }

  
  state = pa_stream_get_state(TestDevice.stream);
  while(state != PA_STREAM_READY) 
    {
      if(pa_mainloop_iterate(mainLoop, 1, 0) < 0)
	{
	  puts("Failed on main loop iteration");
	  return -1;
	}
      state = pa_stream_get_state(TestDevice.stream);
      if( !((state == PA_STREAM_CREATING) || (state == PA_STREAM_READY)) )
	{
	  puts("Bad state");
	  return -1;
	}
    }

  pa_stream_set_underflow_callback(TestDevice.stream,
				   PULSEAUDIO_underflow_cb,
				   0);		

  TestDevice.mainloop = mainLoop;
  TestDevice.context  = context;

  int32 wavePeriod  = sampleSpec.rate/256;
  int16* halfFrame  = (int16*)TestDevice.buffer;
  int16* halfFrame2 = (int16*)TestDevice.buffer2;
  for(int i = 0; i < sampleCount*2; i++)
    { 
      /*if(i > (sampleCount/wavePeriod)*wavePeriod)
	{
	  *halfFrame++ = 0;
	  *halfFrame++ = 0;
	  continue;
	  }*/
      real32 t          = (2.0f*pi*(real32)i) / (real32)wavePeriod;
      real32 sinValue   = 2*sinf(t);
      int16 sampleValue =  (int16)(sinValue * 1000);
      
      *halfFrame++  = sampleValue;
      *halfFrame++  = sampleValue;
      *halfFrame2++ = 2*sampleValue;
      *halfFrame2++ = 2*sampleValue;
    }
  
  return 0;
}

void
PULSEAUDIO_WaitDevice()
{
  int32 state;
  while(1)
    {
      if(pa_context_get_state(TestDevice.context) != PA_CONTEXT_READY ||
	 pa_stream_get_state(TestDevice.stream) != PA_STREAM_READY ||
	 (state = pa_mainloop_iterate(TestDevice.mainloop, 1, 0) < 0))
	{
	  //TODO(rob): Should disconnect here
	  puts("Device failed");
	}
      if(state == 0)
	break;
    }
}

//NOTE Test function for now, using buffer and buffer2(temporary buffer)
void
PULSEAUDIO_PlayDevice()
{
  int32 writable = pa_stream_writable_size(TestDevice.stream);
  if(pa_stream_write(TestDevice.stream, TestDevice.buffer, TestDevice.sampleCount*2*(16/8) * 2, 0, 0LL, PA_SEEK_RELATIVE) < 0 )
    puts("Write failed!");      
  
  pa_operation *o = pa_stream_update_timing_info(TestDevice.stream, 0, 0);
  //TODO(rob): remove this eventually
  PULSEAUDIO_WaitDevice();
  int32 op;
  do
    {
      pa_mainloop_iterate(TestDevice.mainloop, 0, 0);
      op = pa_operation_get_state(o);
    } while(op != PA_OPERATION_DONE);
  
  pa_operation_unref(o);

  TestDevice.timingInfo = pa_stream_get_timing_info(TestDevice.stream);
  printf("%d\n", TestDevice.timingInfo->read_index);
  perror("ERROR: ");
}

void
PULSEAUDIO_H()
{
  int count = 0;
  static int c = 0;
  int32 op;
  pa_operation *o = pa_stream_update_timing_info(TestDevice.stream, 0, 0);
  do
    {
      int state = pa_mainloop_iterate(TestDevice.mainloop, 0, 0);
      op = pa_operation_get_state(o);
      printf("Iteration: %d, Loop state: %d, Op status: %d\n", count, state, op);
      count++;
    } while(op != PA_OPERATION_DONE);
  
  pa_operation_unref(o);

  if(TestDevice.timingInfo)
    {
      int64 writeIndex = TestDevice.timingInfo->write_index;
      int64 readIndex  = TestDevice.timingInfo->read_index;
      int32 frame      = readIndex / (TestDevice.sampleCount*2*2);
      int64 boundary   = (frame * TestDevice.sampleCount * 2 * 2) + TestDevice.sampleCount * 2 * 2;
      
      //if(pa_stream_write(TestDevice.stream, TestDevice.buffer2, TestDevice.sampleCount*2*(16/8), 0, boundary, PA_SEEK_ABSOLUTE) < 0)
      //if(pa_stream_write(TestDevice.stream, TestDevice.buffer, TestDevice.sampleCount*2*(16/8), 0, 0, PA_SEEK_RELATIVE) < 0)
	puts("Rewrite failed!");
    }
  ++c;
}

void
HandleEvents(XEvent *Event, ScreenHandles ScreenHandles)
{
  switch(Event->type)
    {		  
    case MapNotify:
      {
	printf("MapNotify\n");
      } break;

    case ConfigureNotify:
      {
	printf("ConfNotify\n");
	/*static XWindowAttributes WindowAttributes = {};
	if((Event->xconfigure.width != WindowAttributes.width) || 
	   (Event->xconfigure.height != WindowAttributes.height))
	  {
	    printf("Resize\n");
	    ResizeSection(&GlobalBackBuffer,
			  Event->xconfigure.width,
			  Event->xconfigure.height);
	  }
	XGetWindowAttributes(ScreenHandles.DisplayHandle, 
			     ScreenHandles.Window,
			     &WindowAttributes);*/
      } break;
		  
    case Expose:
      {
	printf("Redraw here\n");
	//ResizeSection(&ScreenHandles,
	//	      &GlobalBackBuffer, Event->xexpose.width, Event->xexpose.height);
	//DisplayBufferStrechedInWindow(&ScreenHandles,
	//			      Event->xexpose.width, 
	//			      Event->xexpose.height,
	//			      &GlobalBackBuffer);
	/*DisplayBufferInWindow(&ScreenHandles, 
			      Event->xexpose.width,
			      Event->xexpose.height,
			      GlobalBackBuffer);*/
      } break;

    case FocusIn:
      {
	printf("Window in focus\n");
      } break;

    case FocusOut:
      {
	printf("Window out of focus\n");
      } break;

    case ButtonRelease:
      {
	Running = false;
      } break;

    case KeyPress:
      {
	if(XLookupKeysym(&(Event->xkey), 0) == XK_space)
	  {
	    printf("Space was pressed\n");    
	  }
	  
	if(XLookupKeysym(&(Event->xkey), 0) == XK_Control_L)
	  printf("Left control was pressed\n");
      } break;

    case ClientMessage:
      {
	Running = false;
	printf("Client Msg\n");
      } break;
    default:
      {
	printf("Default: %d\n", Event->type);
      } break;
    }
}

int 
main(int argc, char **argv)
{
  ScreenHandles ScreenHandles;
  XGCValues GCValues;
  fd_set FileDescriptorSet;

  struct timespec CounterResolutionResult;
  clock_getres(CLOCK_MONOTONIC, &CounterResolutionResult);
  int64 CounterResolution = CounterResolutionResult.tv_nsec;
  real32 frameTime        = 1.0f/30.0f;
  int64 perFrame          = frameTime*1000000000;
  
  ScreenHandles.DisplayHandle = XOpenDisplay(0);
  ScreenHandles.ScreenNum     = DefaultScreen(ScreenHandles.DisplayHandle);
  ScreenHandles.RootWindow    = RootWindow(ScreenHandles.DisplayHandle, 
					   ScreenHandles.ScreenNum);
  ScreenHandles.Visual        = DefaultVisual(ScreenHandles.DisplayHandle, 
					      ScreenHandles.ScreenNum);
  ScreenHandles.BitmapPad     = BitmapPad(ScreenHandles.DisplayHandle);
  ScreenHandles.DisplayWidth  = XDisplayWidth(ScreenHandles.DisplayHandle, 
					     ScreenHandles.ScreenNum);
  ScreenHandles.DisplayHeight = XDisplayHeight(ScreenHandles.DisplayHandle, 
					       ScreenHandles.ScreenNum);
  ScreenHandles.ScreenDepth   = XDefaultDepth(ScreenHandles.DisplayHandle, 
					      ScreenHandles.ScreenNum);
  ScreenHandles.WindowWidth   = (ScreenHandles.DisplayWidth/3);
  ScreenHandles.WindowHeight  = (ScreenHandles.DisplayHeight/3);  
  ScreenHandles.BlackPixel    = BlackPixel(ScreenHandles.DisplayHandle, 
					   ScreenHandles.ScreenNum);
  ScreenHandles.WhitePixel    = WhitePixel(ScreenHandles.DisplayHandle, 
					   ScreenHandles.ScreenNum);


  GCValues.function    = GXcopy;
  GCValues.plane_mask  = 1L;
  GCValues.foreground  = 0;
  GCValues.background  = 1;
  GCValues.line_width  = 0;
  GCValues.line_style  = LineSolid;
  GCValues.fill_style  = FillSolid;
  GCValues.fill_rule   = EvenOddRule;
  GCValues.arc_mode    = ArcPieSlice;
  //GCValues.tile      = 0;
  //GCValues.stipple   = 0;
  GCValues.ts_x_origin = 0;
  GCValues.ts_y_origin = 0;
  GCValues.subwindow_mode = ClipByChildren;
  GCValues.graphics_exposures = True;
  GCValues.clip_x_origin = 0;
  GCValues.clip_y_origin = 0;
  GCValues.clip_mask   = None;
  GCValues.dash_offset = 0;
  GCValues.dashes      = 4;

  if(ScreenHandles.DisplayHandle)
    { 
      ScreenHandles.Window = XCreateWindow(
					   ScreenHandles.DisplayHandle,
					   ScreenHandles.RootWindow,
					   0, 0, 
					   ScreenHandles.WindowWidth,
					   ScreenHandles.WindowHeight,
					   0,
					   ScreenHandles.ScreenDepth, 
					   InputOutput,
					   ScreenHandles.Visual,
					   0, 
					   0);
      if(ScreenHandles.Window)
	{
	  Atom wmDelete = XInternAtom(ScreenHandles.DisplayHandle,
				      "WM_DELETE_WINDOW", true);

	  XSetWMProtocols(ScreenHandles.DisplayHandle, 
			  ScreenHandles.Window, &wmDelete, 1);
	  XStoreName     (ScreenHandles.DisplayHandle, 
			  ScreenHandles.Window, "Handmade window");
	  XSelectInput   (ScreenHandles.DisplayHandle, 
			  ScreenHandles.Window,
			  ExposureMask | KeyPressMask |
			  ButtonPressMask | ButtonReleaseMask |
			  FocusChangeMask | StructureNotifyMask);
	  
	  ScreenHandles.GraphicsContext = XCreateGC(ScreenHandles.DisplayHandle, 
						    ScreenHandles.Window, 
						    1L, &GCValues);
	  XSetForeground (ScreenHandles.DisplayHandle, 
			  ScreenHandles.GraphicsContext,
			  ScreenHandles.WhitePixel);
	  XSetBackground (ScreenHandles.DisplayHandle, 
			  ScreenHandles.GraphicsContext,
			  ScreenHandles.BlackPixel);
	  
	  XMapWindow     (ScreenHandles.DisplayHandle, 
			  ScreenHandles.Window);

	  JoystickInit();
	  //OpenJoystickDevice(1);
	  
	  if(PULSEAUDIO_OpenDevice())
	    {
	      return -1;
	    }

	  ResizeSection(&ScreenHandles, &GlobalBackBuffer, 1280, 720);
	  int XOffset = 0;
	  int YOffset = 0;
	  int ConnectionFileDescriptor = ConnectionNumber(ScreenHandles.DisplayHandle);

	  FD_ZERO(&FileDescriptorSet);
	  FD_SET(ConnectionFileDescriptor, &FileDescriptorSet);
	  struct timeval TimeVal = {};
	  PULSEAUDIO_PlayDevice();
	  struct timespec LastCounter;
	  clock_gettime(CLOCK_MONOTONIC, 
			&LastCounter);
	  short buf[128];
	  while(Running)
	    {
	      //TODO(Rob): Rename this
	      //if(select(ConnectionFileDescriptor+1, &FileDescriptorSet, 0, 0, &TimeVal))
		{
		  while(XPending(ScreenHandles.DisplayHandle))
		    {
		      XEvent Event = {};
		      XNextEvent(ScreenHandles.DisplayHandle, &Event);
		      HandleEvents(&Event, ScreenHandles);
		    }
		}
		//else
		{
		  //printf("No event");
		}
	        
		RenderWeirdGradient(&ScreenHandles, &GlobalBackBuffer, XOffset, YOffset);

		static XWindowAttributes WindowAttributes = {};
		XGetWindowAttributes(ScreenHandles.DisplayHandle, 
				     ScreenHandles.Window,
				     &WindowAttributes);
		//DisplayBufferStrechedInWindow(&ScreenHandles, 
		//			      WindowAttributes.width, WindowAttributes.height,
		//			      &GlobalBackBuffer);
      		
		static int XTest = 0; 
		static int YTest = 0;
		//readJoystickInput(joystickList->next, &XTest, &YTest);
		
		XOffset += XTest;
		YOffset += YTest;

		struct timespec EndCounter;
		clock_gettime(CLOCK_MONOTONIC, 
			      &EndCounter);
		int64 EndCounterNano  = EndCounter.tv_sec*1000000000  + EndCounter.tv_nsec;
		int64 LastCounterNano = LastCounter.tv_sec*1000000000 + LastCounter.tv_nsec;
		int64 CounterElapsed  = EndCounterNano - LastCounterNano;
	        
		if(CounterElapsed < perFrame)
		  {
		    timespec sleep_counter;
		    sleep_counter.tv_sec  = 0;
		    sleep_counter.tv_nsec = perFrame - CounterElapsed;
		    nanosleep(&sleep_counter, 0);
		    //usleep( (perFrame - CounterElapsed)/1000 );		
		  }
		else
		  usleep(CounterElapsed - perFrame );
		  
		//DisplayBufferInWindow(&ScreenHandles, 
		//		      GlobalBackBuffer.Width, GlobalBackBuffer.Height,
		//		      &GlobalBackBuffer);

		//PULSEAUDIO_H();
		clock_gettime(CLOCK_MONOTONIC, 
			      &EndCounter);
		EndCounterNano = EndCounter.tv_sec*1000000000 + EndCounter.tv_nsec;
		CounterElapsed = EndCounterNano - LastCounterNano;
		
		printf("\nTime elapsed: %.2fms\n", ((real32)CounterElapsed / (real32)CounterResolution)/1000000);
		LastCounter = EndCounter;
	    }
	}
      else
	{
	  //TODO(rob): Logging
	}
      
      PULSEAUDIO_DisconnectFromPulseServer(TestDevice.context, TestDevice.mainloop);
      XCloseDisplay(ScreenHandles.DisplayHandle);
    }
  else
    {
      //TODO(rob): Logging
    }
  return(0);
}
