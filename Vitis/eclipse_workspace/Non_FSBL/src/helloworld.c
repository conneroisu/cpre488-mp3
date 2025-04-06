#include "camera_app.h"
#include "fmc_iic.h"
#include "fmc_ipmi.h"
#include "platform.h"
#include "xaxis_switch.h"
#include "xil_cache.h"
#include "xv_hcresampler.h"
#include "xv_vcresampler.h"
#include "xvidc.h"


int main()
{
    init_platform();


    cleanup_platform();
    return 0;
}
