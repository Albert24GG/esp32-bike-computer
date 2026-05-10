#include "hal/gpio_types.h"
#include "soc/lp_timer_struct.h"
#include "soc/soc.h"
#include "ulp_lp_core.h"
#include "ulp_lp_core_gpio.h"
#include "ulp_lp_core_interrupts.h"
#include "ulp_lp_core_utils.h"
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdbool.h>

#include "constants.hpp"

volatile uint64_t s_wheel_periods_buf[WHEEL_PERIODS_BUF_SIZE] = {0};
volatile uint32_t s_wheel_periods_buf_len = 0;
volatile bool is_first_wakeup = true;

static uint64_t last_wheel_counter = 0;

// Local copies of the above buffers, used for double buffering
static uint64_t l_wheel_periods_buf[WHEEL_PERIODS_BUF_SIZE] = {0};
static uint32_t l_wheel_periods_buf_len = 0;

static uint32_t inactivity_cnt = 0;
static bool notify_hp = false;

static uint64_t read_counter(size_t idx) {
  LP_TIMER.update.update = 1;

  while (LP_TIMER.update.update) {
    // Wait for the update to complete
  }

  uint64_t lo = LP_TIMER.counter[idx].lo.counter_lo;
  uint64_t hi = LP_TIMER.counter[idx].hi.counter_hi;
  uint64_t counter = (hi << 32) | lo;
  return counter;
}

static void transfer_wheel_periods_buf_to_hp() {
  memcpy((void *)s_wheel_periods_buf, (const void *)l_wheel_periods_buf,
         sizeof(s_wheel_periods_buf));
  s_wheel_periods_buf_len = l_wheel_periods_buf_len;

  // Reset the local buffer length for the next batch of readings
  l_wheel_periods_buf_len = 0;
}

static void update_wheel_periods_buf() {
  const uint64_t cur_counter = read_counter(0);

  const uint64_t cur_wheel_period = [&]() {
    if (cur_counter < last_wheel_counter) {
      // Handle counter overflow
      return (std::numeric_limits<uint64_t>::max() - last_wheel_counter) + cur_counter + 1;
    } else {
      return cur_counter - last_wheel_counter;
    }
  }();

  last_wheel_counter = cur_counter;

  if (is_first_wakeup) {
    is_first_wakeup = false;
  }
  else {
    l_wheel_periods_buf[l_wheel_periods_buf_len++] = cur_wheel_period;
  }

  if (l_wheel_periods_buf_len >= WHEEL_PERIODS_BUF_SIZE) {
    transfer_wheel_periods_buf_to_hp();
    notify_hp = true;
  }
}

static void timer_handler() {
  ulp_lp_core_lp_timer_intr_clear();

  if (l_wheel_periods_buf_len > 0) {
    transfer_wheel_periods_buf_to_hp();
    inactivity_cnt = 0;
    notify_hp = true;
    return;
  }

  ++inactivity_cnt;
  if (inactivity_cnt >= LP_WAKEUPS_BEFORE_TIMEOUT) {
    inactivity_cnt = 0;
    notify_hp = true;
  }
}

static void gpio_handler() {
  update_wheel_periods_buf();

  inactivity_cnt = 0;

  ulp_lp_core_gpio_clear_intr_status();
}

void LP_CORE_ISR_ATTR ulp_lp_core_lp_io_intr_handler() { gpio_handler(); }

int main(void) {
  ulp_lp_core_intr_enable();
  ulp_lp_core_gpio_intr_enable(static_cast<lp_io_num_t>(SENSOR_PIN),
                               GPIO_INTR_NEGEDGE);

  const auto wakeup_cause = ulp_lp_core_get_wakeup_cause();

  if (wakeup_cause & ULP_LP_CORE_WAKEUP_SOURCE_LP_IO) {
    gpio_handler();
  }
  if (wakeup_cause & ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER) {
    timer_handler();
  }

  if (notify_hp) {
    notify_hp = false;
    ulp_lp_core_wakeup_main_processor();
  }

  return 0;
}