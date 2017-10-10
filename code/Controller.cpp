
typedef struct Joystick_hwdata
{
  int   fd;
  char *fname;

  /* The current Linux joystick driver maps hats to two axes */
  struct hwdata_hat
  {
    int axis[2];
  } *hats;

  /* Support for the Linux 2.4 unified input interface */
  uint8 key_map[KEY_MAX - BTN_MISC];
  uint8 abs_map[ABS_MAX];
  struct axis_correct
  {
    int used;
    int coef[3];
  } abs_correct[ABS_MAX];

  int fresh;
}Joystick_hwdata;

typedef struct 
{
  uint8 data[16];
} JoystickGUID;

typedef struct JoystickList_item
{
  int   device_instance;
  
  int   nbuttons = 0;
  int   *buttons;
  int   naxis = 0;
  int   *axis;
  int   *dpad;

  char *path;   /* "/dev/input/event2" or whatever */
  char *name;   /* "SideWinder 3D Pro" or whatever */
  
  JoystickGUID guid;
  dev_t devnum;
  Joystick_hwdata   *hwdata;
  
  JoystickList_item *next;
} JoystickList_item;

JoystickList_item *joystickList      = 0;
JoystickList_item *JoystickList_tail = 0;
uint8 numJoysticks     = 0;
uint8 instance_counter = 0; 


#define test_bit(nr, addr)     (((1UL << ((nr) % (sizeof(long) * 8))) & ((addr)[(nr) / (sizeof(long) * 8)])) != 0)
#define NBITS(x)               (( ((x)-1) / (sizeof(long) * 8)) + 1)

static int
IsJoystick(int fd, char *namebuf, const size_t namebuflen, JoystickGUID *guid)
{
    struct input_id inpid;
    uint16 *guid16 = (uint16 *) ((char *) &guid->data);

    unsigned long evbit[NBITS(EV_MAX)] = { 0 };
    unsigned long keybit[NBITS(KEY_MAX)] = { 0 };
    unsigned long absbit[NBITS(ABS_MAX)] = { 0 };

    if ((ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), evbit) < 0) ||
        (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit) < 0) ||
        (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbit)), absbit) < 0)) {
        return (0);
    }

    if (!(test_bit(EV_KEY, evbit) && test_bit(EV_ABS, evbit) &&
          test_bit(ABS_X, absbit) && test_bit(ABS_Y, absbit)))
      return 0;

    if (ioctl(fd, EVIOCGNAME(namebuflen), namebuf) < 0) 
      return 0;

    if (ioctl(fd, EVIOCGID, &inpid) < 0)
      return 0;

    printf("Joystick: %s, bustype = %d, vendor = 0x%x, product = 0x%x, version = %d\n", 
	    namebuf,      inpid.bustype,inpid.vendor, inpid.product,  inpid.version);

    memset(guid->data, 0, sizeof(guid->data));
    /* We only need 16 bits for each of these; space them out to fill 128. */
    /* Byteswap so devices get same GUID on little/big endian platforms. 
       
       TODO(rob): Do this?
       SDL_SwapLE16(inpid.bustype);
     */
    *(guid16++) = inpid.bustype;
    *(guid16++) = 0;

    if (inpid.vendor && inpid.product && inpid.version) 
      {
        *(guid16++) = inpid.vendor;
        *(guid16++) = 0;
        *(guid16++) = inpid.product;
        *(guid16++) = 0;
        *(guid16++) = inpid.version;
        *(guid16++) = 0;
    } 
    else
      strncpy((char*)guid16, namebuf, sizeof(guid->data) - 4);

    return 1;
}

static int
AddJoystickDevice(const char *path)
{
  struct stat statBuffer;
  int fd      = -1;
  int isstick = 0;
  char namebuf[128];
  JoystickGUID guid;
  JoystickList_item *item;

  if (path == NULL)
    return -1;

  if (stat(path, &statBuffer) == -1) 
    return -1;

  /* Check to make sure it's not already in list. */
  for (item = joystickList; item != NULL; item = item->next) {
    if (statBuffer.st_rdev == item->devnum)
      return -1;
  }

  fd = open(path, O_RDONLY, 0);
  if (fd < 0)
    return -1;

  printf("Checking %s\n", path);

  isstick = IsJoystick(fd, namebuf, sizeof (namebuf), &guid);
  close(fd);
  if (!isstick)
    return -1;

  item = (JoystickList_item*) malloc(sizeof (JoystickList_item));
  if (item == NULL)
    return -1;

  pZERO(item);
  item->devnum = statBuffer.st_rdev;
  item->path   = strdup(path);
  item->name   = strdup(namebuf);
  item->guid   = guid;

  if( (item->path == NULL) || (item->name == NULL) ) 
    {
      free(item->path);
      free(item->name);
      free(item);
      return -1;
    }

  item->device_instance = instance_counter++;
  if (JoystickList_tail == NULL)
    joystickList = JoystickList_tail = item;
  else 
    {
      JoystickList_tail->next = item;
      JoystickList_tail = item;
    }
    
  ++numJoysticks;

  return numJoysticks;
}

JoystickList_item*
JoystickByIndex(int32 device_index)
{
  JoystickList_item *joystick = joystickList;
  if(device_index < 0 || device_index > numJoysticks)
    return 0;
  
  while(device_index)
    {
      joystick = joystickList->next;

      --device_index;
    }
  return joystick;
}

void
handmade_error(char* message)
{
  
}

void
OpenJoystickDevice(int32 device_index)
{
  JoystickList_item *joystick = JoystickByIndex(device_index);
  int32 joystickFD = -1;

  if(joystick == 0)
    handmade_error("");
  
  char* fd_path = joystick->path;
  joystickFD = open(fd_path, O_RDONLY, 0);
  if(joystickFD < 0)
    handmade_error("");

  joystick->hwdata        = (Joystick_hwdata*) malloc(sizeof(*joystick->hwdata));
  joystick->hwdata->fd    = joystickFD;
  joystick->hwdata->fname = strdup(fd_path);  
  
  int inputEventCode;
  uint64 keybit[NBITS(KEY_MAX)] = {};
  uint64 absbit[NBITS(KEY_MAX)] = {};

  if( (ioctl(joystick->hwdata->fd, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit) >= 0) &&
      (ioctl(joystick->hwdata->fd, EVIOCGBIT(EV_ABS, sizeof(absbit)), absbit) >= 0))
    {
      for(inputEventCode = BTN_MISC; inputEventCode < KEY_MAX; ++inputEventCode)
	{
	  if(test_bit(inputEventCode, keybit))
	    {
	      joystick->hwdata->key_map[inputEventCode - BTN_MISC] = joystick->nbuttons;
	      ++joystick->nbuttons;
	    }
	}

      for(inputEventCode = ABS_X; inputEventCode < ABS_MAX; ++inputEventCode)
	{
	  if(inputEventCode == ABS_HAT0X)
	    {
	      inputEventCode = ABS_HAT3Y;
	      continue;
	    }
	  if(test_bit(inputEventCode, absbit))
	    {
	      joystick->hwdata->abs_map[inputEventCode] = joystick->naxis;
	      ++joystick->naxis;
	    }
	}
      
      for(inputEventCode = ABS_HAT0X; inputEventCode <= ABS_HAT3X; inputEventCode += 2)
	{
	  if(test_bit(inputEventCode, absbit))
	    {
	      joystick->hwdata->abs_map[inputEventCode]   = 0;
	      joystick->hwdata->abs_map[inputEventCode+1] = 1;
	      break;
	    }
	}
    }
  
  int buttonsSize   = joystick->nbuttons * sizeof(int);
  joystick->buttons = (int*) malloc(buttonsSize);
  int axisSize      = joystick->naxis    * sizeof(int);
  joystick->axis    = (int*) malloc(axisSize);
  joystick->dpad    = (int*) malloc(sizeof(int) * 2);
  memset(joystick->buttons, 0, buttonsSize);
  memset(joystick->axis,    0, axisSize);

  fcntl(joystickFD, F_SETFL, O_NONBLOCK);
}

void
CloseJoystickDevice(JoystickList_item *joystick)
{
  free(joystick->buttons);
  free(joystick->axis);
  free(joystick->dpad);

  free(joystick->name);
  free(joystick->hwdata->fname);
  free(joystick->hwdata);
  free(joystick);
}

void
HandleButton(JoystickList_item *joystick, uint16 button, int32 value)
{
  switch(value)
    {
      //TODO(rob): 1 - Pressed, 0 - Released
    case 0:
      printf("Button %d released\n", button);
      joystick->buttons[button] = value;
      break;
    case 1:
      printf("Button %d pressed\n", button);
      joystick->buttons[button] = value;
      break;
    default:
      printf("Button value not 0 or 1? Button %d\n", button);
      break;
    }
}

void
HandleHat(JoystickList_item *joystick, uint16 axis, int32 value, int *XTest, int *YTest)
{
  if(axis == 0)
    {
      if(value < 0)
	{
	  printf("D-pad axis %d, left was pressed\n", axis);
	  joystick->dpad[axis] = value;
	  *XTest = -16;
	}
      else if(value == 0)
	{
	  printf("D-pad axis %d released\n", axis);
	  joystick->dpad[axis] = value;
	  *XTest = 0;
	}
      else if(value > 0)
	{
	  printf("D-pad axis %d, right was pressed\n", axis);
	  joystick->dpad[axis] = value;
	  *XTest = 16;
	}
    }
  else if(axis == 1)
    {
      if(value < 0)
	{
	  printf("D-pad axis %d, up was pressed\n", axis);
	  joystick->dpad[axis] = value;
	  *YTest = -16;
	}
      else if(value == 0)
	{
	  printf("D-pad axis %d released\n", axis);
	  joystick->dpad[axis] = value;
	  *YTest = 0;
	}
      else if(value > 0)
	{
	  printf("D-pad axis %d, down was pressed\n", axis);
	  joystick->dpad[axis] = value;
	  *YTest = 16;
	}
    }
}

void
HandleAxis(JoystickList_item *joystick, int16 axis, int32 value)
{
  printf("Axis: %d, value: %d\n", axis, value);
  joystick->axis[axis] = value;
}

int
CorrectAxis(JoystickList_item *joystick, int16 code, int32 value)
{

  return 0;
}

void
readJoystickInput(JoystickList_item* joystick, int *XTest, int *YTest)
{
  input_event events[64];
  int32 dataLen = read(joystick->hwdata->fd, events, sizeof(events));

  if(dataLen <= 0)
    return;

  for(int i = 0; i < (dataLen/sizeof(events[0])); i++)
    {
      uint16 code = events[i].code;
      switch(events[i].type)
	{
	case EV_KEY:
	  if(code >= BTN_MISC)
	    {
	      printf("BTN_MISC\n");
	      code -= BTN_MISC;
	      HandleButton(joystick, joystick->hwdata->key_map[code], events[i].value);
	      printf("\n");
	    }   
	  break;
	case EV_REL:
	  printf("EV_REL\n");
	  break;
	case EV_ABS:
	  switch(code)
	    {
	    case ABS_HAT0X:
	    case ABS_HAT0Y:
	    case ABS_HAT1X:
	    case ABS_HAT1Y:
	    case ABS_HAT2X:
	    case ABS_HAT2Y:
	    case ABS_HAT3X:
	    case ABS_HAT3Y:
	      printf("ABS_HAT\n");
	      HandleHat(joystick, joystick->hwdata->abs_map[code], events[i].value, XTest, YTest);
	      break;
	    default:
	      if(code == 2)
		break;
	      printf("ABS_axis\n");
	      CorrectAxis(joystick, code, events[i].value);
	      HandleAxis(joystick, joystick->hwdata->abs_map[code], events[i].value);
	      break;
	    }
	  
	  break;
	  //TODO(rob): What is EV_SYN (SYN_DROPPED seems to be key) ?
	case EV_SYN:
	  //printf("EV_SYN\n");
	  break;
	default:
	  break;
	}
    }
    }

/*
void
OpenHotplugSocket()
{
  struct sockaddr_nl nls;
  struct pollfd pfd;
  char buf[512];
  
  memset(&nls,0,sizeof(struct sockaddr_nl));
  nls.nl_family = AF_NETLINK;
  nls.nl_pid    = getpid();
  nls.nl_groups = -1;

  pfd.events = POLLIN;
  pfd.fd     = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
  if (pfd.fd == -1)
    die("Not root\n");

  if (bind(pfd.fd, (void *)&nls, sizeof(struct sockaddr_nl)))
    die("Bind failed\n");
  
  while (poll(&pfd, 1, -1) != -1) 
    {
      //int i, len = recv(pfd.fd, buf, sizeof(buf), MSG_DONTWAIT);
      //TODO(rob): Recieved some kind of event (we are interested in USB input/output), 
      //           now read them and react accordingly 
    }

  die("poll\n");
}
*/
int
JoystickInit()
{
  char path[PATH_MAX];

  for(int i = 0; i < 32; i++)
    {
      //TODO(Rob): Find a better name and implement this (n chars from src to dest - INTEL)
      snprintf(path, arraySize(path), "/dev/input/event%d", i);
      AddJoystickDevice(path);
    }

  return numJoysticks; //maybe
}
