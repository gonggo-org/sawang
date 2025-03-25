#ifndef _GLIBSHIM_H_
#define _GLIBSHIM_H_

#ifdef GLIBSHIM

#include <glib.h>

#ifndef g_ptr_array_copy
extern GPtrArray* g_ptr_array_copy (GPtrArray* array, GCopyFunc func, gpointer user_data);
#endif //g_ptr_array_copy

#endif //GLIBSHIM

#endif //_GLIBSHIM_H_