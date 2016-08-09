
#include "defines.h"
#include "config.h"
#include "ax25.h"
#include "hdlc.h"
#include "chprintf.h"
   
static bool mon_on = false;
static Stream *out;
FBQ mon;

// THREAD_STACK(monitor, STACK_MONITOR);



void mon_init(Stream* outstr)
{
    out = outstr;
    FBQ_INIT(mon, HDLC_DECODER_QUEUE_SIZE);
}




/******************************************************************************
 *  Monitor thread 
 *   Just write out incoming frames. 
 *   Currently this is static. We may consider making it dynamic to save
 *   memory when it is not used. 
 ******************************************************************************/

static THD_FUNCTION(monitor, arg)
{
  (void) arg;
  chRegSetThreadName("Packet monitor");
  while (mon_on)
  {
    /* Wait for frame and then to AFSK decoder/encoder 
     * is not running. 
     */
    FBUF frame = fbq_get(&mon);
    if (!fbuf_empty(&frame)) {
      /* Display it */
       ax25_display_frame(out, &frame);
       chprintf(out, "\r\n");
    }
    
    /* And dispose the frame. Note that also an empty frame should be disposed! */
    fbuf_release(&frame);    
  }
}


static thread_t* mont=NULL;

void mon_activate(bool m)
{ 
   /* Start if not on already */
   bool tstart = m && !mon_on;
   
   /* Stop if not stopped already */
   bool tstop = !m && mon_on;
   
   mon_on = m;
   
   if (tstart) {
      FBQ* mq = (mon_on? &mon : NULL);
      hdlc_subscribe_rx(mq, 0);
      if ( true || !mon_on || GET_BYTE_PARAM(TXMON_ON) )
         hdlc_monitor_tx(mq);
      mont = THREAD_DSTART(monitor, STACK_MONITOR, NORMALPRIO, NULL);  
   }
   if (tstop) {
      hdlc_monitor_tx(NULL);
      hdlc_subscribe_rx(NULL, 0);
      fbq_signal(&mon);
      if (mont!=NULL) chThdWait(mont);
      mont=NULL;
   }
}



