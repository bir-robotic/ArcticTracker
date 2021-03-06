#if !defined __FBUF_H__
#define __FBUF_H__

#include "ch.h"
#include "hal.h"
#include "defines.h"
#include <inttypes.h>

#define NILPTR 0xFFFF


#define fbuf_t FBUF
#define fbq_t FBQ

typedef uint16_t fbindex_t;

/*********************************
   Packet buffer chain
 *********************************/
typedef struct _fb
{
   fbindex_t head, wslot, rslot; 
   uint16_t  rpos; 
   uint16_t  length;
}
FBUF; 


/****************************************
   Operations for packet buffer chain
 ****************************************/

void     fbuf_new       (FBUF* b);
FBUF     fbuf_newRef    (FBUF* b);
void     fbuf_release   (FBUF* b);
void     fbuf_reset     (FBUF* b);
void     fbuf_rseek     (FBUF* b, const uint16_t pos);
void     fbuf_putChar   (FBUF* b, const char c);
void     fbuf_write     (FBUF* b, const char* data, const uint16_t size);
void     fbuf_putstr    (FBUF* b, const char *data);
char     fbuf_getChar   (FBUF* b);
void     fbuf_streamRead(Stream *chp, FBUF* b);
uint16_t fbuf_read      (FBUF* b, uint16_t size, char *buf);
void     fbuf_print     (Stream *chp, FBUF* b); 
void     fbuf_insert    (FBUF* b, FBUF* x, uint16_t pos);
void     fbuf_connect   (FBUF* b, FBUF* x, uint16_t pos);
void     fbuf_removeLast(FBUF* b);

fbindex_t fbuf_usedSlots(void);
fbindex_t fbuf_freeSlots(void);
uint16_t fbuf_freeMem(void);

#define fbuf_eof(b) ((b)->rslot == NILPTR)
#define fbuf_length(b) ((b)->length)
#define fbuf_empty(b) ((b)->length == 0)



/*********************************
 *   Queue of packet buffer chains
 *********************************/

typedef struct _fbq
{
  uint8_t size, index, cnt; 
  semaphore_t length, capacity; 
  FBUF *buf; 
} FBQ;



/************************************************
   Operations for queue of packet buffer chains
 ************************************************/

void  _fbq_init (FBQ* q, FBUF* buf, const uint16_t size); 
void  fbq_clear (FBQ* q);
void  fbq_put   (FBQ* q, FBUF b); 
FBUF  fbq_get   (FBQ* q);
void  fbq_signal(FBQ* q);


#define fbq_eof(q)    ( chSemGetCounterI(&((q)->capacity)) >= (q)->size )
#define fbq_full(q)   ( chSemGetCounterI(&((q)->capacity)) == 0 )

#define FBQ_INIT(name,size)   static FBUF name##_fbqbuf[(size)];    \
    _fbq_init(&(name), (name##_fbqbuf), (size));


#endif /* __FBUF_H__ */
