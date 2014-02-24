 /**
 * handles messages from
 * asterisk extgw call context. It handles inbound message from
 * the gateways
 **/

 // Hack to remove asterisk stupid defines to prevent using mutex / pthreads
 
#ifdef pthread_mutex_t
#undef pthread_mutex_t
#endif

#ifdef pthread_cond_t
#undef pthread_cond_t
#endif

#ifdef pthread_mutex_lock
#undef pthread_mutex_lock
#endif

#ifdef pthread_mutex_unlock
#undef pthread_mutex_unlock
#endif

#ifdef pthread_mutex_trylock
#undef pthread_mutex_trylock
#endif

#ifdef pthread_mutex_init
#undef pthread_mutex_init
#endif

#ifdef pthread_mutex_destroy
#undef pthread_mutex_destroy
#endif

#ifdef pthread_cond_destroy
#undef pthread_cond_destroy
#endif

#ifdef pthread_cond_signal
#undef pthread_cond_signal
#endif

#ifdef pthread_cond_broadcast
#undef pthread_cond_broadcast
#endif

#ifdef pthread_cond_wait
#undef pthread_cond_wait
#endif

#ifdef pthread_cond_timedwait
#undef pthread_cond_timedwait
#endif

#ifdef gethostbyname
#undef gethostbyname
#endif

#ifdef pthread_create
#undef pthread_create
#endif
