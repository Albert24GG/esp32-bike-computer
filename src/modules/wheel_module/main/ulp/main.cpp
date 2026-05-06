#include <stdbool.h>
#include "ulp_lp_core.h"
#include "ulp_lp_core_utils.h"
#include "ulp_lp_core_gpio.h"


int main (void)
{
    ulp_lp_core_wakeup_main_processor();

    ulp_lp_core_gpio_clear_intr_status();

    return 0;
}