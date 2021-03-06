
#include "ch.h"
#include "hal.h"
#include "chprintf.h"
#include "ui/commands.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hdlc.h"
#include "fbuf.h"
#include "ax25.h"
#include "config.h"
#include "gps.h"
#include <math.h>
#include "defines.h"

#define NMEA_BUFSIZE   80
#define NMEA_MAXTOKENS 16


THREAD_STACK(nmeaListener, STACK_NMEALISTENER);


/* Defined in commands.c */
uint8_t tokenize(char*, char*[], uint8_t, char*, bool);

/* Current position and time */
posdata_t current_pos; 
timestamp_t current_time = 0; 
date_t current_date;

/* Local handlers */
static void do_rmc (uint8_t, char**, Stream*);
static void do_gga (uint8_t, char**, Stream*);
static void notify_fix (bool);

/* Local variables */
static char buf[NMEA_BUFSIZE+1];
static bool monitor_pos, monitor_raw; 
static bool is_fixed = true;
static virtual_timer_t ttimer; 

BSEMAPHORE_DECL(wait_gps, true);
#define WAIT_FIX chBSemWait(&wait_gps)
#define SIGNAL_FIX chBSemSignal(&wait_gps)

static Stream *_serial, *_shell;

static const SerialConfig _serialConfig = {
  9600
};



/**************************************************************************
 * Read and process NMEA sentences.
 *    Not thread safe. Use in at most one thread, use lock, or allocate
 *    buf on the stack instead. 
 **************************************************************************/
bool nmea_ok = false;

__attribute__((noreturn))
static THD_FUNCTION(nmeaListener, arg)
{
  (void)arg;
  char* argv[16];
  uint8_t argc;
  
  chRegSetThreadName("NMEA GPS Listener");
  while (1) {
    int checksum = 0; 
    int c_checksum;
    
    readline(_serial, buf, NMEA_BUFSIZE);
    
    if (buf[0] != '$')
      continue;
    
    
    /* If requested, show raw NMEA packet on screen */
    if (monitor_raw) 
      chprintf(_shell, "%s\r\n", buf);
    
    
    /* Checksum (optional) */
    uint8_t i = 0;
    for (i=1; i<NMEA_BUFSIZE && buf[i] !='*' && buf[i] != 0 ; i++) 
      checksum ^= buf[i];
    if (buf[i] == '*') {
      buf[i] = 0;
      sscanf(buf+i+1, "%x", &c_checksum);
      if (c_checksum != checksum) 
        continue;
    } 
    nmea_ok = true;
    
    /* Split input line into tokens */
    argc = tokenize(buf, argv, NMEA_MAXTOKENS, ",", false);           
    /* Select command handler */    
    if (strncmp("RMC", argv[0]+3, 3) == 0)
      do_rmc(argc, argv, _shell);
    else if (strncmp("GGA", argv[0]+3, 3) == 0)
      do_gga(argc, argv, _shell);
  }
}


void gps_init(SerialDriver *str, Stream *sh)
{
    _serial = (Stream*) str; 
    _shell = sh;
    setPinMode(GPS_SERIAL_RXD, PAL_MODE_ALTERNATIVE_3);
    setPinMode(GPS_SERIAL_TXD, PAL_MODE_ALTERNATIVE_3);
    sdStart(str, &_serialConfig);   
    chVTObjectInit(&ttimer);
    monitor_pos = monitor_raw = false; 
    THREAD_START(nmeaListener, NORMALPRIO, NULL);
 //   make_output(GPSON); 
 //   set_port(GPSON);
}



posdata_t*  gps_get_pos()
  { return &current_pos; }

timestamp_t gps_get_time()
  { return current_time; } 

date_t gps_get_date()
  { return current_date; } 
  

void gps_on()
{
 //  clear_port(GPSON);
   notify_fix(false);
}


void gps_off()
{ 
   //  set_port(GPSON);
   BLINK_NORMAL   
}



/*************************************************************************
 * Compute distance (in meters) between two gps positions
 * compute bearing based on two gps positions
 *************************************************************************/
 
/* The usual PI/180 constant */
static const double DEG_TO_RAD = 0.017453292519943295769236907684886;

/* Earth's quatratic mean radius for WGS-84 */
static const double EARTH_RADIUS_IN_METERS = 6372797.560856;


/*
 * Computes the arc, in radians, between two WGS-84 positions.
 *   Use the Haversine formula. 
 *   http://en.wikipedia.org/wiki/Law_of_haversines
 */
 
static double arcInRadians(posdata_t *from, posdata_t *to)
{
      double latitudeArc  = (from->latitude - to->latitude) * DEG_TO_RAD;
      double longitudeArc = (from->longitude - to->longitude) * DEG_TO_RAD;
      double latitudeH = sin(latitudeArc * 0.5);
      latitudeH *= latitudeH;
      double lontitudeH = sin(longitudeArc * 0.5);
      lontitudeH *= lontitudeH;
      double tmp = cos(from->latitude * DEG_TO_RAD) * cos(to->latitude * DEG_TO_RAD);
      return 2.0 * asin(sqrt(latitudeH + tmp * lontitudeH));
}



uint32_t gps_distance(posdata_t *from, posdata_t *to)
{
    return (uint32_t) round(EARTH_RADIUS_IN_METERS * arcInRadians(from, to));
}


uint16_t gps_bearing(posdata_t *from, posdata_t *to)
{
    double dLon = (from->longitude - to->longitude) * DEG_TO_RAD;
    double toLat = to->latitude * DEG_TO_RAD;
    double fromLat = from->latitude * DEG_TO_RAD;
    if (dLon == 0 && toLat==fromLat)
       return -1;
    double y = sin(dLon) * cos(from->latitude * DEG_TO_RAD);
    double x = cos(toLat) * sin(fromLat) - sin(toLat) * cos(fromLat) * cos(dLon);
    uint16_t brng = (uint16_t) round(atan2(y, x) / DEG_TO_RAD);
    return (brng + 180) % 360; 
}


/****************************************************************
 * Monitoring control
 *   nmea_mon_pos - valid GPRMC position reports
 *   nmea_mon_raw - NMEA packets  
 *   nmea_mon_off - turn it all off
 ****************************************************************/

void gps_mon_pos(void)
   { monitor_pos = true; }
void gps_mon_raw(void)
   { monitor_raw = true; }
void gps_mon_off(void)
   { monitor_pos = monitor_raw = false; }
   

    
   
/****************************************************************
 * Convert position NMEA fields to float (degrees)
 ****************************************************************/

static void str2coord(const uint8_t ndeg, const char* str, float* coord)
{
    float minutes;
    char dstring[ndeg+1];

    /* Format [ddmm.mmmmm] */
    strncpy(dstring, str, ndeg);
    dstring[ndeg] = 0;
    
    sscanf(dstring, "%f", coord);      /* Degrees */
    sscanf(str+ndeg, "%f", &minutes);  /* Minutes */
    *coord += (minutes / 60);
}


/****************************************************************
 * Convert position to latlong format
 ****************************************************************/

char* pos2str_lat(char* buf, posdata_t *pos)
{
    /* Format latitude values, etc. */
    char lat_sn = (pos->latitude < 0 ? 'S' : 'N');
    float latf = fabs(pos->latitude);
    
    sprintf(buf, "%02d %05.2f %c%c", 
	(int)latf, (latf - (int)latf) * 60, lat_sn,'\0');
    return buf;
}       
 
char* pos2str_long(char* buf, posdata_t *pos)
{
    /* Format longitude values, etc. */
    char long_we = (pos->longitude < 0 ? 'W' : 'E');
    float longf = fabs(pos->longitude);
    
    sprintf(buf, "%03d %05.2f %c%c", 
        (int)longf, (longf - (int)longf) * 60, long_we, '\0');
    return buf;
}  
    
       
       
/*****************************************************************
 * Convert date/time NMEA fields (timestamp + date)
 *****************************************************************/
 
static void nmea2time( timestamp_t* t, date_t* d, const char* timestr, const char* datestr)
{
    unsigned int hour, min, sec, day, month, year;
    sscanf(timestr, "%2u%2u%2u", &hour, &min, &sec);
    sscanf(datestr, "%2u%2u%2u", &day, &month, &year);
    d->day = day; d->month = month; d->year = year; 
    d->year += 2000;
    *t = (uint32_t) 
         ((uint32_t) d->day-1) * 86400 +  ((uint32_t)hour) * 3600 + ((uint32_t)min) * 60 + sec;
}


char* datetime2str(char* buf, date_t d, timestamp_t time)
{
    switch (d.month) {
        case  1: sprintf(buf, "Jan"); break;
        case  2: sprintf(buf, "Feb"); break;
        case  3: sprintf(buf, "Mar"); break;
        case  4: sprintf(buf, "Apr"); break;
        case  5: sprintf(buf, "May"); break;
        case  6: sprintf(buf, "Jun"); break;
        case  7: sprintf(buf, "Jul"); break;
        case  8: sprintf(buf, "Aug"); break;
        case  9: sprintf(buf, "Sep"); break;
        case 10: sprintf(buf, "Oct"); break;
        case 11: sprintf(buf, "Nov"); break;
        case 12: sprintf(buf, "Dec"); break;
        default:  sprintf(buf, "???"); ;
    }
    sprintf(buf+3, " %02u %02u:%02u UTC", d.day, 
      (uint8_t) ((time / 3600) % 24), (uint8_t) ((time / 60) % 60));
    return buf;
}


char* time2str(char* buf, timestamp_t time)
{
    sprintf(buf, "%02u:%02u:%02u", 
      (uint8_t) ((time / 3600) % 24), (uint8_t) ((time / 60) % 60), (uint8_t) (time % 60) );
    return buf;
}
 
 
char* date2str(char* buf, date_t date)
{
   sprintf(buf, "%02hu-%02hu-%4hu", date.day, date.month, date.year);
   return buf;
}


 
/****************************************************************
 * handle changes in GPS lock - mainly change LED blinking
 ****************************************************************/

void notify_fix(bool lock)
{
   if (!lock) 
      BLINK_GPS_SEARCHING
   else {
       if (!is_fixed) {
          SIGNAL_FIX;
       }     
       BLINK_NORMAL
   }
   is_fixed = lock;
}


bool gps_is_fixed()
   { return is_fixed && GET_BYTE_PARAM(TRACKER_ON); }
   
  
/* Return true if we waited */   
bool gps_wait_fix(uint16_t timeout)
{ 
     if (is_fixed) 
        return false;      
     if (timeout > 0) {
       chVTSet(&ttimer, MS2ST(timeout), (vtfunc_t) chBSemSignal, &wait_gps); 
     }
    WAIT_FIX;
    chVTReset(&ttimer);
    return true;
}         

   
uint16_t course_count = 0;  
float altitude = -1;
       
       
       
/****************************************************************
 * Handle RMC line
 ****************************************************************/

static void do_rmc(uint8_t argc, char** argv, Stream *out)
{
    static uint8_t lock_cnt = 4;
    
    char tbuf[9];
    if (argc != 13)                 /* Ignore if wrong format */
       return;
    
    /* get timestamp */
    nmea2time(&current_time, &current_date, argv[1], argv[9]);
    
    if (*argv[2] != 'A') { 
       notify_fix(false);          /* Ignore if receiver not in lock */
       lock_cnt = 4;
       return;
    }
    else
      if (lock_cnt > 0) {
         lock_cnt--;
         return;
      }
      
    lock_cnt = 1;
    notify_fix(true);
   
    current_pos.timestamp = current_time; 
    
    /* get latitude [ddmm.mmmmm] */
    str2coord(2, argv[3], &current_pos.latitude);  
    if (*argv[4] == 'S')
        current_pos.latitude = -current_pos.latitude;
        
     /* get longitude [dddmm.mmmmm] */
    str2coord(3, argv[5], &current_pos.longitude);  
    if (*argv[6] == 'W')
        current_pos.longitude = -current_pos.longitude;
    
    /* get speed [nnn.nn] */
    if (*argv[7] != '\0')
       sscanf(argv[7], "%f", &current_pos.speed);
    else
       current_pos.speed = 0;
       
    /* get course [nnn.nn] */
    if (*argv[8] != '\0') {
       float x;
       sscanf(argv[8], "%f", &x);
       current_pos.course = (uint16_t) x+0.5;
    }
    else
       current_pos.course = 0;
    current_pos.altitude = altitude;
           
    /* If requested, show position on screen */    
    if (monitor_pos) {
      chprintf(out, "TIME: %s, POS: lat=%f, long=%f, SPEED: %f km/h, COURSE: %u deg\r\n", 
          time2str(tbuf, current_pos.timestamp), 
          current_pos.latitude, current_pos.longitude, 
          current_pos.speed*KNOTS2KMH, current_pos.course);
    }
}


/******************************************* 
 * Get altitude from GGA line
 *******************************************/

static void do_gga(uint8_t argc, char** argv, Stream *out)
{
    (void) out;
    if (argc == 15 && *argv[6] > '0')
       sscanf(argv[9], "%f", &altitude);
    else
       altitude = -1; 
}



