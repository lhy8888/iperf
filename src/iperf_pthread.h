#include "iperf_config.h"

#if defined(HAVE_PTHREAD)

#include <pthread.h>

#if defined(__ANDROID__)

/* Adding missing `pthread` related definitions in Android.
 */

 enum
 {
   PTHREAD_CANCEL_ENABLE,
 #define PTHREAD_CANCEL_ENABLE   PTHREAD_CANCEL_ENABLE
   PTHREAD_CANCEL_DISABLE
 #define PTHREAD_CANCEL_DISABLE  PTHREAD_CANCEL_DISABLE
 };
 enum
 {
   PTHREAD_CANCEL_DEFERRED,
 #define PTHREAD_CANCEL_DEFERRED	PTHREAD_CANCEL_DEFERRED
   PTHREAD_CANCEL_ASYNCHRONOUS
 #define PTHREAD_CANCEL_ASYNCHRONOUS	PTHREAD_CANCEL_ASYNCHRONOUS
 };

#elif defined(_WIN32)

/* MinGW-w64's winpthread usually provides these APIs, but the cancel constants
 * are not always present.  Define the values if the header omitted them.
 */
#ifndef PTHREAD_CANCEL_ENABLE
#define PTHREAD_CANCEL_ENABLE   0
#endif
#ifndef PTHREAD_CANCEL_DISABLE
#define PTHREAD_CANCEL_DISABLE  1
#endif
#ifndef PTHREAD_CANCEL_DEFERRED
#define PTHREAD_CANCEL_DEFERRED 0
#endif
#ifndef PTHREAD_CANCEL_ASYNCHRONOUS
#define PTHREAD_CANCEL_ASYNCHRONOUS 1
#endif

#endif // defined(__ANDROID__) / defined(_WIN32)

#endif // defined(HAVE_PTHREAD)
