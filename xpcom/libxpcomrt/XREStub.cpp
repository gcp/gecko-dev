#include "nsXULAppAPI.h"


GeckoProcessType
XRE_GetProcessType()
{
  return GeckoProcessType_Default;
}

#define PRINT_CALLED fprintf(stderr, "!!! ERROR: function %s defined in file %s should not be called, needs to be correctly implemented.\n", __FUNCTION__, __FILE__)
